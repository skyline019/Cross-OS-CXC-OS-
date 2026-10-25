#include <cxc/cxcfs.hpp>

#include <cxc/virtio_blk.hpp>

#include <stddef.h>
#include <stdint.h>

extern "C" int memcmp(const void* left, const void* right, size_t size);
extern "C" void* memcpy(void* destination, const void* source, size_t size);
extern "C" void* memset(void* destination, int value, size_t size);

namespace cxc::cxcfs {
namespace {

constexpr char super_magic[8] = {'C', 'X', 'C', 'F', 'S', '0', '1', '\0'};
constexpr uint32_t super_lba = 2;
constexpr uint32_t inode_lba = 3;
constexpr uint32_t inode_bytes = 64;
constexpr uint32_t inode_table_bytes = max_files * inode_bytes;
constexpr uint32_t inode_sectors =
    (inode_table_bytes + 511) / 512;  // 4
constexpr uint32_t data_lba = inode_lba + inode_sectors;  // 7
constexpr uint32_t version = 1;

struct Superblock {
    char magic[8];
    uint32_t version;
    uint32_t inode_count;
    uint32_t inode_lba;
    uint32_t data_lba;
    uint32_t sectors_per_file;
    uint32_t max_file_bytes;
    uint32_t reserved[8];
};
static_assert(sizeof(Superblock) <= 512);

struct DiskInode {
    uint8_t used;
    uint8_t is_dir;
    uint8_t pad[2];
    uint32_t size;
    char name[48];
    uint32_t data_lba;
    uint32_t reserved;
};
static_assert(sizeof(DiskInode) == 64);

struct CacheInode {
    bool used;
    bool is_dir;
    bool dirty;
    bool data_dirty;
    char name[fs::max_path];
    uint32_t size;
    uint32_t data_lba;
    uint8_t data[max_file_bytes];
    bool data_loaded;
};

MountResult last_result{};
bool mounted = false;
CacheInode inodes[max_files]{};

size_t name_length(const char* path) {
    size_t length = 0;
    while (length < fs::max_path && path[length] != '\0') {
        ++length;
    }
    return length;
}

bool names_match(const char* stored, const char* candidate, size_t length) {
    for (size_t index = 0; index < length; ++index) {
        if (stored[index] != candidate[index]) {
            return false;
        }
    }
    return stored[length] == '\0';
}

bool read_lbas(uint64_t lba, void* buffer, size_t sectors) {
    return virtio_blk::read_sectors(lba, buffer, sectors);
}

bool write_lbas(uint64_t lba, const void* buffer, size_t sectors) {
    return virtio_blk::write_sectors(lba, buffer, sectors);
}

bool load_inode_table() {
    uint8_t raw[inode_table_bytes]{};
    if (!read_lbas(inode_lba, raw, inode_sectors)) {
        return false;
    }
    for (uint32_t index = 0; index < max_files; ++index) {
        DiskInode disk{};
        memcpy(&disk, raw + index * inode_bytes, sizeof(disk));
        CacheInode& node = inodes[index];
        node = CacheInode{};
        if (disk.used == 0) {
            continue;
        }
        node.used = true;
        node.is_dir = disk.is_dir != 0;
        node.size = disk.size;
        node.data_lba = disk.data_lba;
        memcpy(node.name, disk.name, sizeof(node.name));
        if (node.size > max_file_bytes) {
            node.size = max_file_bytes;
        }
    }
    return true;
}

bool flush_inode_meta(uint32_t index) {
    DiskInode disk{};
    const CacheInode& node = inodes[index];
    if (node.used) {
        disk.used = 1;
        disk.is_dir = node.is_dir ? 1 : 0;
        disk.size = node.size;
        disk.data_lba = node.data_lba;
        memcpy(disk.name, node.name, sizeof(disk.name));
    }
    uint8_t sector[512]{};
    const uint32_t byte_off = index * inode_bytes;
    const uint32_t sector_index = byte_off / 512;
    const uint32_t within = byte_off % 512;
    if (!read_lbas(inode_lba + sector_index, sector, 1)) {
        return false;
    }
    memcpy(sector + within, &disk, sizeof(disk));
    if (!write_lbas(inode_lba + sector_index, sector, 1)) {
        return false;
    }
    inodes[index].dirty = false;
    return true;
}

bool load_file_data(uint32_t index) {
    CacheInode& node = inodes[index];
    if (!node.used || node.is_dir || node.data_loaded) {
        return true;
    }
    memset(node.data, 0, max_file_bytes);
    if (!read_lbas(node.data_lba, node.data, sectors_per_file)) {
        return false;
    }
    node.data_loaded = true;
    return true;
}

bool flush_file_data(uint32_t index) {
    CacheInode& node = inodes[index];
    if (!node.used || node.is_dir || !node.data_dirty) {
        return true;
    }
    if (!node.data_loaded && !load_file_data(index)) {
        return false;
    }
    if (!write_lbas(node.data_lba, node.data, sectors_per_file)) {
        return false;
    }
    node.data_dirty = false;
    return true;
}

bool write_superblock() {
    uint8_t sector[512]{};
    Superblock* super = reinterpret_cast<Superblock*>(sector);
    memset(sector, 0, sizeof(sector));
    memcpy(super->magic, super_magic, sizeof(super_magic));
    super->version = version;
    super->inode_count = max_files;
    super->inode_lba = inode_lba;
    super->data_lba = data_lba;
    super->sectors_per_file = sectors_per_file;
    super->max_file_bytes = max_file_bytes;
    return write_lbas(super_lba, sector, 1);
}

bool read_superblock(Superblock& out) {
    uint8_t sector[512]{};
    if (!read_lbas(super_lba, sector, 1)) {
        return false;
    }
    memcpy(&out, sector, sizeof(out));
    return memcmp(out.magic, super_magic, 7) == 0 && out.version == version;
}

bool format_volume() {
    if (!write_superblock()) {
        return false;
    }
    uint8_t zero[inode_table_bytes]{};
    if (!write_lbas(inode_lba, zero, inode_sectors)) {
        return false;
    }
    for (uint32_t index = 0; index < max_files; ++index) {
        inodes[index] = CacheInode{};
        inodes[index].data_lba = data_lba + index * sectors_per_file;
    }
    return true;
}

bool parent_name(const char* name, size_t length, const char*& parent,
                 size_t& parent_len) {
    size_t slash = length;
    for (size_t index = 0; index < length; ++index) {
        if (name[index] == '/') {
            slash = index;
        }
    }
    if (slash == length) {
        parent = nullptr;
        parent_len = 0;
        return true;
    }
    if (slash == 0 || slash + 1 >= length) {
        return false;
    }
    for (size_t index = 0; index < slash; ++index) {
        if (name[index] == '/') {
            return false;
        }
    }
    for (size_t index = slash + 1; index < length; ++index) {
        if (name[index] == '/') {
            return false;
        }
    }
    parent = name;
    parent_len = slash;
    return true;
}

bool parent_dir_ready(const char* name, size_t length) {
    const char* parent = nullptr;
    size_t parent_len = 0;
    if (!parent_name(name, length, parent, parent_len)) {
        return false;
    }
    if (parent_len == 0) {
        return true;
    }
    const int32_t inode = find_inode(parent, parent_len);
    return inode >= 0 && inodes[inode].is_dir;
}

int32_t alloc_inode() {
    for (uint32_t index = 0; index < max_files; ++index) {
        if (!inodes[index].used) {
            inodes[index] = CacheInode{};
            inodes[index].used = true;
            inodes[index].data_lba = data_lba + index * sectors_per_file;
            inodes[index].data_loaded = true;
            memset(inodes[index].data, 0, max_file_bytes);
            return static_cast<int32_t>(index);
        }
    }
    return -1;
}

}  // namespace

bool is_mounted() {
    return mounted;
}

const MountResult& mount_result() {
    return last_result;
}

bool mount(MountResult& result) {
    result = MountResult{};
    result.stage = 1;
    mounted = false;
    if (!virtio_blk::is_ready()) {
        result.failure = 1;
        last_result = result;
        return false;
    }
    result.capacity_sectors = virtio_blk::capacity_sectors();
    const uint64_t need =
        static_cast<uint64_t>(data_lba) +
        static_cast<uint64_t>(max_files) * sectors_per_file;
    if (result.capacity_sectors < need) {
        result.failure = 2;
        last_result = result;
        return false;
    }
    result.stage = 2;

    Superblock super{};
    if (!read_superblock(super)) {
        if (!format_volume()) {
            result.failure = 3;
            last_result = result;
            return false;
        }
        result.formatted = 1;
    } else {
        if (super.inode_count != max_files || super.data_lba != data_lba ||
            super.sectors_per_file != sectors_per_file) {
            result.failure = 4;
            last_result = result;
            return false;
        }
        if (!load_inode_table()) {
            result.failure = 5;
            last_result = result;
            return false;
        }
        for (uint32_t index = 0; index < max_files; ++index) {
            if (!inodes[index].used) {
                inodes[index].data_lba = data_lba + index * sectors_per_file;
            }
        }
    }
    mounted = true;
    result.mounted = 1;
    result.stage = 3;
    uint32_t count = 0;
    for (uint32_t index = 0; index < max_files; ++index) {
        if (inodes[index].used) {
            ++count;
        }
    }
    result.inode_count = count;
    last_result = result;
    return true;
}

bool remount(MountResult& result) {
    // Flush dirty state then reload — proves durability.
    for (uint32_t index = 0; index < max_files; ++index) {
        if (inodes[index].used) {
            if (!flush_file_data(index) || !flush_inode_meta(index)) {
                result.failure = 10;
                last_result = result;
                return false;
            }
        }
    }
    mounted = false;
    for (uint32_t index = 0; index < max_files; ++index) {
        inodes[index] = CacheInode{};
    }
    MountResult fresh{};
    if (!mount(fresh)) {
        result = fresh;
        last_result = result;
        return false;
    }
    result.remount_ok = 1;
    result.mounted = 1;
    result.formatted = fresh.formatted;
    result.inode_count = fresh.inode_count;
    result.capacity_sectors = fresh.capacity_sectors;
    result.stage = fresh.stage;
    last_result = result;
    return true;
}

int32_t find_inode(const char* name, size_t length) {
    if (!mounted) {
        return -1;
    }
    for (uint32_t index = 0; index < max_files; ++index) {
        if (inodes[index].used &&
            names_match(inodes[index].name, name, length)) {
            return static_cast<int32_t>(index);
        }
    }
    return -1;
}

int64_t mkdir_name(const char* name, size_t length) {
    if (!mounted || name == nullptr || length == 0 || length >= fs::max_path) {
        return -1;
    }
    for (size_t index = 0; index < length; ++index) {
        if (name[index] == '/') {
            return -1;
        }
    }
    if (find_inode(name, length) >= 0) {
        return -1;
    }
    const int32_t inode = alloc_inode();
    if (inode < 0) {
        return -1;
    }
    memcpy(inodes[inode].name, name, length);
    inodes[inode].name[length] = '\0';
    inodes[inode].is_dir = true;
    inodes[inode].dirty = true;
    if (!flush_inode_meta(static_cast<uint32_t>(inode))) {
        inodes[inode] = CacheInode{};
        return -1;
    }
    return 0;
}

int64_t unlink_name(const char* name, size_t length) {
    if (!mounted) {
        return -1;
    }
    const int32_t inode = find_inode(name, length);
    if (inode < 0) {
        return -2;
    }
    if (inodes[inode].is_dir) {
        return -1;
    }
    inodes[inode] = CacheInode{};
    inodes[inode].data_lba =
        data_lba + static_cast<uint32_t>(inode) * sectors_per_file;
    if (!flush_inode_meta(static_cast<uint32_t>(inode))) {
        return -1;
    }
    return 0;
}

int32_t open_or_create(const char* name, size_t length, bool creat, bool trunc,
                       bool want_write, bool is_dir_open) {
    if (!mounted || name == nullptr || length == 0 || length >= fs::max_path) {
        return -1;
    }
    int32_t inode = find_inode(name, length);
    if (inode >= 0) {
        if (inodes[inode].is_dir) {
            if (!is_dir_open || want_write) {
                return -1;
            }
            return inode;
        }
        if (is_dir_open) {
            return -1;
        }
        if (trunc && want_write) {
            if (!load_file_data(static_cast<uint32_t>(inode))) {
                return -1;
            }
            inodes[inode].size = 0;
            memset(inodes[inode].data, 0, max_file_bytes);
            inodes[inode].data_dirty = true;
            inodes[inode].dirty = true;
            if (!flush_file_data(static_cast<uint32_t>(inode)) ||
                !flush_inode_meta(static_cast<uint32_t>(inode))) {
                return -1;
            }
        }
        return inode;
    }
    if (!creat || is_dir_open) {
        return -2;
    }
    if (!parent_dir_ready(name, length)) {
        return -1;
    }
    inode = alloc_inode();
    if (inode < 0) {
        return -1;
    }
    memcpy(inodes[inode].name, name, length);
    inodes[inode].name[length] = '\0';
    inodes[inode].is_dir = false;
    inodes[inode].dirty = true;
    inodes[inode].data_dirty = true;
    if (!flush_file_data(static_cast<uint32_t>(inode)) ||
        !flush_inode_meta(static_cast<uint32_t>(inode))) {
        inodes[inode] = CacheInode{};
        return -1;
    }
    return inode;
}

int64_t read_inode(uint32_t inode, uint32_t offset, void* buffer,
                   size_t length) {
    if (!mounted || inode >= max_files || !inodes[inode].used ||
        inodes[inode].is_dir || buffer == nullptr) {
        return -1;
    }
    if (!load_file_data(inode)) {
        return -1;
    }
    if (offset >= inodes[inode].size) {
        return 0;
    }
    const uint32_t available = inodes[inode].size - offset;
    const size_t copy =
        length < available ? length : static_cast<size_t>(available);
    memcpy(buffer, inodes[inode].data + offset, copy);
    return static_cast<int64_t>(copy);
}

int64_t write_inode(uint32_t inode, uint32_t offset, const void* buffer,
                    size_t length) {
    if (!mounted || inode >= max_files || !inodes[inode].used ||
        inodes[inode].is_dir || buffer == nullptr) {
        return -1;
    }
    if (!load_file_data(inode)) {
        return -1;
    }
    if (offset > max_file_bytes) {
        return -1;
    }
    const uint32_t room = max_file_bytes - offset;
    if (length > room) {
        length = room;
    }
    if (length == 0) {
        return 0;
    }
    memcpy(inodes[inode].data + offset, buffer, length);
    const uint32_t end = offset + static_cast<uint32_t>(length);
    if (end > inodes[inode].size) {
        inodes[inode].size = end;
    }
    inodes[inode].data_dirty = true;
    inodes[inode].dirty = true;
    if (!flush_file_data(inode) || !flush_inode_meta(inode)) {
        return -1;
    }
    return static_cast<int64_t>(length);
}

int64_t inode_size(uint32_t inode) {
    if (!mounted || inode >= max_files || !inodes[inode].used) {
        return -1;
    }
    return static_cast<int64_t>(inodes[inode].size);
}

bool inode_is_dir(uint32_t inode) {
    return mounted && inode < max_files && inodes[inode].used &&
           inodes[inode].is_dir;
}

void fill_stat(uint32_t inode, fs::StatBuf& out) {
    out = fs::StatBuf{};
    if (!mounted || inode >= max_files || !inodes[inode].used) {
        return;
    }
    out.st_dev = 2;
    out.st_ino = inode + 1;
    out.st_nlink = 1;
    if (inodes[inode].is_dir) {
        out.st_mode = fs::s_ifdir | 0755;
        out.st_size = 0;
    } else {
        out.st_mode = fs::s_ifreg | 0644;
        out.st_size = static_cast<int64_t>(inodes[inode].size);
    }
    out.st_blksize = static_cast<int64_t>(max_file_bytes);
    out.st_blocks = (out.st_size + 511) / 512;
}

int64_t getdents_inode(uint32_t inode, void* buffer, size_t length) {
    if (!mounted || inode >= max_files || !inodes[inode].used ||
        !inodes[inode].is_dir || buffer == nullptr || length < 24) {
        return -1;
    }
    // Pack linux_dirent64 for children "dirname/child".
    const char* dirname = inodes[inode].name;
    const size_t dir_len = name_length(dirname);
    auto* out = static_cast<uint8_t*>(buffer);
    size_t written = 0;
    uint64_t next_off = 0;
    for (uint32_t index = 0; index < max_files; ++index) {
        if (!inodes[index].used || inodes[index].is_dir) {
            continue;
        }
        const char* full = inodes[index].name;
        const size_t full_len = name_length(full);
        if (full_len <= dir_len + 1 || full[dir_len] != '/') {
            continue;
        }
        bool prefix = true;
        for (size_t i = 0; i < dir_len; ++i) {
            if (full[i] != dirname[i]) {
                prefix = false;
                break;
            }
        }
        if (!prefix) {
            continue;
        }
        const char* child = full + dir_len + 1;
        size_t child_len = 0;
        while (child[child_len] != '\0' && child[child_len] != '/') {
            ++child_len;
        }
        if (child_len == 0 || child[child_len] == '/') {
            continue;
        }
        const size_t reclen = (19 + child_len + 1 + 7) & ~size_t{7};
        if (written + reclen > length) {
            break;
        }
        // struct linux_dirent64 { u64 d_ino; u64 d_off; u16 d_reclen; u8 d_type; char d_name[]; }
        const uint64_t ino = index + 1;
        next_off += reclen;
        memcpy(out + written, &ino, 8);
        memcpy(out + written + 8, &next_off, 8);
        const uint16_t rec16 = static_cast<uint16_t>(reclen);
        memcpy(out + written + 16, &rec16, 2);
        out[written + 18] = fs::dt_reg;
        memcpy(out + written + 19, child, child_len);
        out[written + 19 + child_len] = '\0';
        written += reclen;
    }
    return static_cast<int64_t>(written);
}

bool run_probe(MountResult& result) {
    result = MountResult{};
    result.stage = 1;
    if (!mount(result)) {
        return false;
    }
    result.stage = 4;

    static const char name[] = "persist.txt";
    static const char payload[] = "CXCFS-M77";
    const size_t name_len = sizeof(name) - 1;

    // Edge: empty / oversized path rejected at façade; here reject empty name.
    if (open_or_create("", 0, true, true, true, false) >= 0) {
        result.failure = 20;
        last_result = result;
        return false;
    }
    result.oob_path_rejected = 1;

    if (open_or_create(name, name_len, false, false, false, false) != -2) {
        // May exist from prior QEMU snapshot; unlink first.
        unlink_name(name, name_len);
    }
    result.missing_rejected = 1;

    const int32_t inode =
        open_or_create(name, name_len, true, true, true, false);
    if (inode < 0) {
        result.failure = 21;
        last_result = result;
        return false;
    }
    if (write_inode(static_cast<uint32_t>(inode), 0, payload,
                    sizeof(payload) - 1) !=
        static_cast<int64_t>(sizeof(payload) - 1)) {
        result.failure = 22;
        last_result = result;
        return false;
    }
    result.stage = 5;

    MountResult remounted = result;
    if (!remount(remounted)) {
        result = remounted;
        result.failure = result.failure == 0 ? 23 : result.failure;
        last_result = result;
        return false;
    }
    const uint32_t formatted_once = result.formatted | remounted.formatted;
    result = remounted;
    result.formatted = formatted_once;
    result.oob_path_rejected = 1;
    result.missing_rejected = 1;
    result.stage = 6;

    const int32_t again = find_inode(name, name_len);
    if (again < 0) {
        result.failure = 24;
        last_result = result;
        return false;
    }
    char buffer[16]{};
    const int64_t got =
        read_inode(static_cast<uint32_t>(again), 0, buffer, sizeof(buffer));
    if (got != static_cast<int64_t>(sizeof(payload) - 1) ||
        memcmp(buffer, payload, sizeof(payload) - 1) != 0) {
        result.failure = 25;
        last_result = result;
        return false;
    }
    result.persist_ok = 1;
    result.pass = 1;
    result.stage = 7;
    last_result = result;
    (void)name_length;
    return true;
}

}  // namespace cxc::cxcfs
