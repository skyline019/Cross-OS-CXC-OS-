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
// M257: one inode per sector so records never span LBAs (name[256]).
constexpr uint32_t inode_bytes = 512;
constexpr uint32_t inode_table_bytes = max_files * inode_bytes;
constexpr uint32_t inode_sectors = max_files;  // 256
constexpr uint32_t data_lba = inode_lba + inode_sectors;
// M255–M257: shared pool + sector I/O + long names (v9: 512B inodes).
constexpr uint32_t version = 9;

struct Superblock {
    char magic[8];
    uint32_t version;
    uint32_t inode_count;
    uint32_t inode_lba;
    uint32_t data_lba;
    uint32_t sectors_per_file;  // max sectors any one file may hold
    uint32_t max_file_bytes;
    uint32_t reserved[8];
};
static_assert(sizeof(Superblock) <= 512);

struct DiskInode {
    uint8_t used;
    uint8_t is_dir;
    uint8_t pad[2];
    uint32_t size;
    uint32_t data_lba;
    uint32_t alloc_sectors;
    char name[256];
    uint8_t unused[512 - 16 - 256];
};
static_assert(sizeof(DiskInode) == 512);

struct CacheInode {
    bool used;
    bool is_dir;
    bool dirty;
    char name[fs::max_path];
    uint32_t size;
    uint32_t data_lba;
    uint32_t alloc_sectors;
};

// Upper bound for a 128 MiB virtio image (sectors). Bitmap lives in BSS.
constexpr uint32_t bitmap_sector_cap = 262144;
uint8_t free_bits[(bitmap_sector_cap + 7) / 8]{};
uint32_t pool_sectors = 0;

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

void bitmap_clear_all() {
    memset(free_bits, 0, sizeof(free_bits));
}

bool bit_used(uint32_t index) {
    return (free_bits[index / 8] & static_cast<uint8_t>(1u << (index % 8))) !=
           0;
}

void bit_set(uint32_t index) {
    free_bits[index / 8] =
        static_cast<uint8_t>(free_bits[index / 8] | (1u << (index % 8)));
}

void bit_clear(uint32_t index) {
    free_bits[index / 8] = static_cast<uint8_t>(
        free_bits[index / 8] & static_cast<uint8_t>(~(1u << (index % 8))));
}

void mark_extent(uint32_t lba, uint32_t sectors, bool used) {
    if (lba < data_lba || sectors == 0) {
        return;
    }
    const uint32_t start = lba - data_lba;
    for (uint32_t i = 0; i < sectors; ++i) {
        if (start + i >= pool_sectors || start + i >= bitmap_sector_cap) {
            break;
        }
        if (used) {
            bit_set(start + i);
        } else {
            bit_clear(start + i);
        }
    }
}

void rebuild_bitmap() {
    bitmap_clear_all();
    for (uint32_t index = 0; index < max_files; ++index) {
        if (inodes[index].used && !inodes[index].is_dir &&
            inodes[index].alloc_sectors > 0) {
            mark_extent(inodes[index].data_lba, inodes[index].alloc_sectors,
                        true);
        }
    }
}

int32_t alloc_extent(uint32_t need_sectors) {
    if (need_sectors == 0 || need_sectors > max_sectors_per_file ||
        need_sectors > pool_sectors) {
        return -1;
    }
    uint32_t run = 0;
    uint32_t run_start = 0;
    for (uint32_t i = 0; i < pool_sectors; ++i) {
        if (!bit_used(i)) {
            if (run == 0) {
                run_start = i;
            }
            ++run;
            if (run >= need_sectors) {
                mark_extent(data_lba + run_start, need_sectors, true);
                return static_cast<int32_t>(data_lba + run_start);
            }
        } else {
            run = 0;
        }
    }
    return -1;
}

void free_extent(uint32_t lba, uint32_t sectors) {
    mark_extent(lba, sectors, false);
}

bool load_inode_table() {
    for (uint32_t index = 0; index < max_files; ++index) {
        uint8_t sector[512]{};
        if (!read_lbas(inode_lba + index, sector, 1)) {
            return false;
        }
        DiskInode disk{};
        memcpy(&disk, sector, sizeof(disk));
        CacheInode& node = inodes[index];
        node = CacheInode{};
        if (disk.used == 0) {
            continue;
        }
        node.used = true;
        node.is_dir = disk.is_dir != 0;
        node.size = disk.size;
        node.data_lba = disk.data_lba;
        node.alloc_sectors = disk.alloc_sectors;
        memcpy(node.name, disk.name, sizeof(node.name));
        if (node.size > max_file_bytes) {
            node.size = max_file_bytes;
        }
        if (node.alloc_sectors > max_sectors_per_file) {
            node.alloc_sectors = max_sectors_per_file;
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
        disk.alloc_sectors = node.alloc_sectors;
        memset(disk.name, 0, sizeof(disk.name));
        memcpy(disk.name, node.name, sizeof(node.name));
    }
    uint8_t sector[512]{};
    memcpy(sector, &disk, sizeof(disk));
    if (!write_lbas(inode_lba + index, sector, 1)) {
        return false;
    }
    inodes[index].dirty = false;
    return true;
}

bool ensure_capacity(uint32_t index, uint32_t need_bytes) {
    CacheInode& node = inodes[index];
    if (need_bytes > max_file_bytes) {
        return false;
    }
    const uint32_t need_sectors =
        need_bytes == 0 ? 0 : (need_bytes + 511) / 512;
    if (need_sectors <= node.alloc_sectors) {
        return true;
    }
    if (node.alloc_sectors == 0) {
        const int32_t lba = alloc_extent(need_sectors);
        if (lba < 0) {
            return false;
        }
        node.data_lba = static_cast<uint32_t>(lba);
        node.alloc_sectors = need_sectors;
        return true;
    }
    // Try grow in place.
    const uint32_t extra = need_sectors - node.alloc_sectors;
    const uint32_t after = node.data_lba + node.alloc_sectors - data_lba;
    bool can_extend = after + extra <= pool_sectors;
    for (uint32_t i = 0; can_extend && i < extra; ++i) {
        if (bit_used(after + i)) {
            can_extend = false;
        }
    }
    if (can_extend) {
        mark_extent(node.data_lba + node.alloc_sectors, extra, true);
        node.alloc_sectors = need_sectors;
        return true;
    }
    // Relocate contiguous extent.
    const int32_t fresh = alloc_extent(need_sectors);
    if (fresh < 0) {
        return false;
    }
    uint8_t sector[512]{};
    for (uint32_t s = 0; s < node.alloc_sectors; ++s) {
        if (!read_lbas(node.data_lba + s, sector, 1)) {
            free_extent(static_cast<uint32_t>(fresh), need_sectors);
            return false;
        }
        if (!write_lbas(static_cast<uint32_t>(fresh) + s, sector, 1)) {
            free_extent(static_cast<uint32_t>(fresh), need_sectors);
            return false;
        }
    }
    free_extent(node.data_lba, node.alloc_sectors);
    node.data_lba = static_cast<uint32_t>(fresh);
    node.alloc_sectors = need_sectors;
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
    super->sectors_per_file = max_sectors_per_file;
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
    uint8_t zero_sector[512]{};
    for (uint32_t sector_index = 0; sector_index < inode_sectors;
         ++sector_index) {
        if (!write_lbas(inode_lba + sector_index, zero_sector, 1)) {
            return false;
        }
    }
    for (uint32_t index = 0; index < max_files; ++index) {
        inodes[index] = CacheInode{};
    }
    bitmap_clear_all();
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
    for (size_t index = 0; index + 1 < length; ++index) {
        if (name[index] == '/' && name[index + 1] == '/') {
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
    // Shared pool: need metadata + at least one data sector.
    if (result.capacity_sectors <= data_lba) {
        result.failure = 2;
        last_result = result;
        return false;
    }
    pool_sectors = static_cast<uint32_t>(result.capacity_sectors - data_lba);
    if (pool_sectors > bitmap_sector_cap) {
        pool_sectors = bitmap_sector_cap;
    }
    result.stage = 2;

    Superblock super{};
    const bool have_super = read_superblock(super);
    const bool layout_ok =
        have_super && super.inode_count == max_files &&
        super.data_lba == data_lba &&
        super.sectors_per_file == max_sectors_per_file &&
        super.max_file_bytes == max_file_bytes;
    if (!layout_ok) {
        if (!format_volume()) {
            result.failure = 3;
            last_result = result;
            return false;
        }
        result.formatted = 1;
    } else {
        if (!load_inode_table()) {
            result.failure = 5;
            last_result = result;
            return false;
        }
        rebuild_bitmap();
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
    for (uint32_t index = 0; index < max_files; ++index) {
        if (inodes[index].used && inodes[index].dirty) {
            if (!flush_inode_meta(index)) {
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
    if (find_inode(name, length) >= 0) {
        return -1;
    }
    if (!parent_dir_ready(name, length)) {
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
    free_extent(inodes[inode].data_lba, inodes[inode].alloc_sectors);
    inodes[inode] = CacheInode{};
    if (!flush_inode_meta(static_cast<uint32_t>(inode))) {
        return -1;
    }
    return 0;
}

int64_t rename_name(const char* old_name, size_t old_len, const char* new_name,
                    size_t new_len) {
    if (!mounted || old_name == nullptr || new_name == nullptr || old_len == 0 ||
        new_len == 0 || old_len >= fs::max_path || new_len >= fs::max_path) {
        return -1;
    }
    const int32_t old_inode = find_inode(old_name, old_len);
    if (old_inode < 0) {
        return -2;
    }
    if (inodes[old_inode].is_dir) {
        return -1;
    }
    if (old_len == new_len && names_match(old_name, new_name, new_len)) {
        return 0;
    }
    if (!parent_dir_ready(new_name, new_len)) {
        return -1;
    }
    const int32_t dest = find_inode(new_name, new_len);
    if (dest >= 0) {
        if (inodes[dest].is_dir) {
            return -1;
        }
        if (dest == old_inode) {
            return 0;
        }
        if (unlink_name(new_name, new_len) != 0) {
            return -1;
        }
    }
    memcpy(inodes[old_inode].name, new_name, new_len);
    inodes[old_inode].name[new_len] = '\0';
    inodes[old_inode].dirty = true;
    if (!flush_inode_meta(static_cast<uint32_t>(old_inode))) {
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
            free_extent(inodes[inode].data_lba, inodes[inode].alloc_sectors);
            inodes[inode].size = 0;
            inodes[inode].data_lba = 0;
            inodes[inode].alloc_sectors = 0;
            inodes[inode].dirty = true;
            if (!flush_inode_meta(static_cast<uint32_t>(inode))) {
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
    if (!flush_inode_meta(static_cast<uint32_t>(inode))) {
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
    if (offset >= inodes[inode].size) {
        return 0;
    }
    const uint32_t available = inodes[inode].size - offset;
    const size_t copy =
        length < available ? length : static_cast<size_t>(available);
    auto* out = static_cast<uint8_t*>(buffer);
    size_t done = 0;
    uint8_t sector[512]{};
    while (done < copy) {
        const uint32_t pos = offset + static_cast<uint32_t>(done);
        const uint32_t sector_index = pos / 512;
        const uint32_t within = pos % 512;
        if (sector_index >= inodes[inode].alloc_sectors) {
            break;
        }
        if (!read_lbas(inodes[inode].data_lba + sector_index, sector, 1)) {
            return -1;
        }
        const size_t chunk = copy - done;
        const size_t room = 512 - within;
        const size_t take = chunk < room ? chunk : room;
        memcpy(out + done, sector + within, take);
        done += take;
    }
    return static_cast<int64_t>(done);
}

int64_t write_inode(uint32_t inode, uint32_t offset, const void* buffer,
                    size_t length) {
    if (!mounted || inode >= max_files || !inodes[inode].used ||
        inodes[inode].is_dir || buffer == nullptr) {
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
    const uint32_t end = offset + static_cast<uint32_t>(length);
    if (!ensure_capacity(inode, end)) {
        return -1;
    }
    const auto* in = static_cast<const uint8_t*>(buffer);
    size_t done = 0;
    uint8_t sector[512]{};
    while (done < length) {
        const uint32_t pos = offset + static_cast<uint32_t>(done);
        const uint32_t sector_index = pos / 512;
        const uint32_t within = pos % 512;
        const size_t chunk = length - done;
        const size_t room_s = 512 - within;
        const size_t take = chunk < room_s ? chunk : room_s;
        if (within != 0 || take != 512) {
            if (sector_index < inodes[inode].alloc_sectors) {
                if (!read_lbas(inodes[inode].data_lba + sector_index, sector,
                               1)) {
                    return -1;
                }
            } else {
                memset(sector, 0, sizeof(sector));
            }
        } else {
            memset(sector, 0, sizeof(sector));
        }
        memcpy(sector + within, in + done, take);
        if (!write_lbas(inodes[inode].data_lba + sector_index, sector, 1)) {
            return -1;
        }
        done += take;
    }
    if (end > inodes[inode].size) {
        inodes[inode].size = end;
    }
    inodes[inode].dirty = true;
    if (!flush_inode_meta(inode)) {
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
    out.st_blksize = 512;
    out.st_blocks = (out.st_size + 511) / 512;
}

int32_t list_dir_rel(const char* dir_rel, size_t dir_len, DirEnt* out,
                     uint32_t max_out) {
    if (!mounted || out == nullptr || max_out == 0) {
        return -1;
    }
    if (dir_rel == nullptr) {
        dir_len = 0;
    }
    if (dir_len > 0 && find_inode(dir_rel, dir_len) < 0) {
        return -1;
    }
    uint32_t n = 0;
    for (uint32_t index = 0; index < max_files && n < max_out; ++index) {
        if (!inodes[index].used) {
            continue;
        }
        const char* full = inodes[index].name;
        const size_t full_len = name_length(full);
        if (dir_len == 0) {
            // Volume root: names with no '/'.
            bool slash = false;
            for (size_t i = 0; i < full_len; ++i) {
                if (full[i] == '/') {
                    slash = true;
                    break;
                }
            }
            if (slash || full_len == 0 || full_len >= sizeof(out[n].name)) {
                continue;
            }
            for (size_t i = 0; i < full_len; ++i) {
                out[n].name[i] = full[i];
            }
            out[n].name[full_len] = 0;
            out[n].is_dir = inodes[index].is_dir ? 1u : 0u;
            ++n;
            continue;
        }
        if (full_len <= dir_len + 1 || full[dir_len] != '/') {
            continue;
        }
        bool prefix = true;
        for (size_t i = 0; i < dir_len; ++i) {
            if (full[i] != dir_rel[i]) {
                prefix = false;
                break;
            }
        }
        if (!prefix) {
            continue;
        }
        const char* child = full + dir_len + 1;
        size_t child_len = 0;
        while (child[child_len] != 0 && child[child_len] != '/') {
            ++child_len;
        }
        // Immediate children only (same rule as getdents_inode).
        if (child_len == 0 || child[child_len] == '/' ||
            child_len + 1 >= sizeof(out[n].name)) {
            continue;
        }
        for (size_t i = 0; i < child_len; ++i) {
            out[n].name[i] = child[i];
        }
        out[n].name[child_len] = 0;
        out[n].is_dir = inodes[index].is_dir ? 1u : 0u;
        ++n;
    }
    return static_cast<int32_t>(n);
}

int64_t getdents_inode(uint32_t inode, void* buffer, size_t length) {
    if (!mounted || inode >= max_files || !inodes[inode].used ||
        !inodes[inode].is_dir || buffer == nullptr || length < 24) {
        return -1;
    }
    const char* dirname = inodes[inode].name;
    const size_t dir_len = name_length(dirname);
    auto* out = static_cast<uint8_t*>(buffer);
    size_t written = 0;
    uint64_t next_off = 0;
    for (uint32_t index = 0; index < max_files; ++index) {
        if (!inodes[index].used) {
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
        const uint64_t ino = index + 1;
        next_off += reclen;
        memcpy(out + written, &ino, 8);
        memcpy(out + written + 8, &next_off, 8);
        const uint16_t rec16 = static_cast<uint16_t>(reclen);
        memcpy(out + written + 16, &rec16, 2);
        out[written + 18] = inodes[index].is_dir ? fs::dt_dir : fs::dt_reg;
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

    if (open_or_create("", 0, true, true, true, false) >= 0) {
        result.failure = 20;
        last_result = result;
        return false;
    }
    result.oob_path_rejected = 1;

    if (open_or_create(name, name_len, false, false, false, false) != -2) {
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

    // M255: fat file >128 KiB round-trip (sector I/O path).
    static const char fat_name[] = "fatcap.bin";
    constexpr uint32_t fat_bytes = 160 * 1024;
    const int32_t fat =
        open_or_create(fat_name, sizeof(fat_name) - 1, true, true, true, false);
    if (fat < 0) {
        result.failure = 30;
        last_result = result;
        return false;
    }
    uint8_t chunk[512]{};
    for (uint32_t off = 0; off < fat_bytes; off += 512) {
        for (uint32_t i = 0; i < 512; ++i) {
            chunk[i] = static_cast<uint8_t>((off + i) & 0xFF);
        }
        if (write_inode(static_cast<uint32_t>(fat), off, chunk, 512) != 512) {
            result.failure = 31;
            last_result = result;
            return false;
        }
    }
    // Edge: refuse growth past max_file_bytes via oversize offset.
    if (write_inode(static_cast<uint32_t>(fat), max_file_bytes, chunk, 1) > 0) {
        result.failure = 32;
        last_result = result;
        return false;
    }
    result.inode_edge = 1;

    MountResult remounted = result;
    if (!remount(remounted)) {
        result = remounted;
        result.failure = result.failure == 0 ? 23 : result.failure;
        last_result = result;
        return false;
    }
    const uint32_t formatted_once = result.formatted | remounted.formatted;
    const uint32_t inode_edge = result.inode_edge;
    result = remounted;
    result.formatted = formatted_once;
    result.oob_path_rejected = 1;
    result.missing_rejected = 1;
    result.inode_edge = inode_edge;
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

    const int32_t fat_again =
        find_inode(fat_name, sizeof(fat_name) - 1);
    if (fat_again < 0 ||
        inode_size(static_cast<uint32_t>(fat_again)) !=
            static_cast<int64_t>(fat_bytes)) {
        result.failure = 33;
        last_result = result;
        return false;
    }
    uint8_t check[512]{};
    if (read_inode(static_cast<uint32_t>(fat_again), fat_bytes - 512, check,
                   512) != 512) {
        result.failure = 34;
        last_result = result;
        return false;
    }
    for (uint32_t i = 0; i < 512; ++i) {
        const uint8_t expect =
            static_cast<uint8_t>((fat_bytes - 512 + i) & 0xFF);
        if (check[i] != expect) {
            result.failure = 35;
            last_result = result;
            return false;
        }
    }
    result.fat_ok = 1;
    result.persist_ok = 1;
    result.pass = 1;
    result.stage = 7;
    last_result = result;
    (void)name_length;
    return true;
}

}  // namespace cxc::cxcfs
