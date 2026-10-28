#include <cxc/fs.hpp>

#include <cxc/cxcfs.hpp>
#include <cxc/fbdev.hpp>
#include <cxc/tty.hpp>

#include <stddef.h>
#include <stdint.h>

extern "C" void* memcpy(void*, const void*, size_t);
extern "C" void* memset(void*, int, size_t);

namespace cxc::fs {
namespace {

constexpr int64_t errno_again = -11;   // -EAGAIN
constexpr int64_t errno_notdir = -20;  // -ENOTDIR
constexpr int64_t errno_range = -34;   // -ERANGE
constexpr int64_t errno_pipe = -32;    // -EPIPE

bool path_is_dev_fb0(const char* name, size_t len) {
    return len == 7 && name[0] == 'd' && name[1] == 'e' && name[2] == 'v' &&
           name[3] == '/' && name[4] == 'f' && name[5] == 'b' && name[6] == '0';
}

bool path_is_dev_tty0(const char* name, size_t len) {
    return len == 8 && name[0] == 'd' && name[1] == 'e' && name[2] == 'v' &&
           name[3] == '/' && name[4] == 't' && name[5] == 't' &&
           name[6] == 'y' && name[7] == '0';
}

struct Inode {
    bool used;
    bool is_dir;
    char name[max_path];
    uint32_t size;
    uint8_t data[max_file_bytes];
};

struct FileDesc {
    bool used;
    uint8_t backend;
    uint32_t inode;
    uint32_t offset;
    uint32_t writable;
    uint32_t readable;
};

struct PipeBuf {
    bool used;
    uint32_t readers;
    uint32_t writers;
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    uint8_t data[pipe_capacity];
};

// Paths `disk/...` go to durable CXCFS (virtio-blk); everything else stays ramfs.
bool disk_rest(const char* name, size_t length, const char*& rest,
               size_t& rest_len) {
    if (length < 6) {
        return false;
    }
    if (name[0] != 'd' || name[1] != 'i' || name[2] != 's' || name[3] != 'k' ||
        name[4] != '/') {
        return false;
    }
    rest = name + 5;
    rest_len = length - 5;
    return rest_len > 0 && rest_len < max_path;
}

Inode inodes[max_files]{};
FileDesc tables[2][fd_table_end]{};
char cwd_storage[2][max_path]{};
size_t cwd_len[2]{};
uint32_t live_table = 0;
PipeBuf pipes[max_pipes]{};
Stats current_stats{};

// Freestanding-safe: no dynamic FileDesc* init (ctors may not run).
#define fds (tables[live_table])
#define cwd (cwd_storage[live_table])
#define cwd_n (cwd_len[live_table])

size_t name_length(const char* path) {
    size_t length = 0;
    while (length < max_path && path[length] != '\0') {
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

int32_t find_inode(const char* name, size_t length);

bool path_is_dir(const char* name, size_t name_len) {
    if (name_len == 0) {
        return true;  // logical root
    }
    const char* rest = nullptr;
    size_t rest_len = 0;
    if (disk_rest(name, name_len, rest, rest_len)) {
        const int32_t inode = cxcfs::find_inode(rest, rest_len);
        return inode >= 0 && cxcfs::inode_is_dir(static_cast<uint32_t>(inode));
    }
    const int32_t inode = find_inode(name, name_len);
    return inode >= 0 && inodes[inode].is_dir;
}

// Resolve user path against cwd into abs (no leading '/'; empty = root).
bool resolve_path(const char* path, char* abs, size_t& abs_len) {
    if (path == nullptr || abs == nullptr) {
        return false;
    }
    const size_t raw_len = name_length(path);
    if (raw_len == 0 || raw_len >= max_path || path[raw_len] != '\0') {
        return false;
    }

    char stack[max_path]{};
    size_t stack_len = 0;

    auto push_comp = [&](const char* comp, size_t comp_len) -> bool {
        if (comp_len == 0) {
            return false;
        }
        if (comp_len == 1 && comp[0] == '.') {
            return true;
        }
        if (comp_len == 2 && comp[0] == '.' && comp[1] == '.') {
            // Pop last component; stay at root if empty.
            if (stack_len == 0) {
                return true;
            }
            size_t cut = stack_len;
            while (cut > 0 && stack[cut - 1] != '/') {
                --cut;
            }
            if (cut > 0 && stack[cut - 1] == '/') {
                --cut;
            }
            stack_len = cut;
            stack[stack_len] = '\0';
            return true;
        }
        if (stack_len > 0) {
            if (stack_len + 1 + comp_len >= max_path) {
                return false;
            }
            stack[stack_len++] = '/';
        } else if (comp_len >= max_path) {
            return false;
        }
        if (stack_len + comp_len >= max_path) {
            return false;
        }
        memcpy(stack + stack_len, comp, comp_len);
        stack_len += comp_len;
        stack[stack_len] = '\0';
        return true;
    };

    auto walk = [&](const char* text, size_t text_len) -> bool {
        size_t i = 0;
        while (i < text_len) {
            while (i < text_len && text[i] == '/') {
                ++i;
            }
            if (i >= text_len) {
                break;
            }
            size_t j = i;
            while (j < text_len && text[j] != '/') {
                ++j;
            }
            if (!push_comp(text + i, j - i)) {
                return false;
            }
            i = j;
        }
        return true;
    };

    if (path[0] == '/') {
        // Absolute: root "/" or "/a/b".
        if (raw_len > 1 && path[raw_len - 1] == '/') {
            return false;
        }
        if (raw_len > 1 && !walk(path + 1, raw_len - 1)) {
            return false;
        }
    } else {
        if (path[raw_len - 1] == '/') {
            return false;
        }
        if (cwd_n > 0 && !walk(cwd, cwd_n)) {
            return false;
        }
        if (!walk(path, raw_len)) {
            return false;
        }
    }

    abs_len = stack_len;
    if (stack_len > 0) {
        memcpy(abs, stack, stack_len);
    }
    abs[stack_len] = '\0';
    return true;
}

int32_t find_inode(const char* name, size_t length) {
    for (uint32_t index = 0; index < max_files; ++index) {
        if (inodes[index].used &&
            names_match(inodes[index].name, name, length)) {
            return static_cast<int32_t>(index);
        }
    }
    return -1;
}

int32_t alloc_inode() {
    for (uint32_t index = 0; index < max_files; ++index) {
        if (!inodes[index].used) {
            inodes[index] = Inode{};
            inodes[index].used = true;
            return static_cast<int32_t>(index);
        }
    }
    return -1;
}

int32_t alloc_fd() {
    for (uint32_t fd = fd_table_begin; fd < fd_table_end; ++fd) {
        if (!fds[fd].used) {
            return static_cast<int32_t>(fd);
        }
    }
    return -1;
}

int32_t alloc_pipe() {
    for (uint32_t index = 0; index < max_pipes; ++index) {
        if (!pipes[index].used) {
            pipes[index] = PipeBuf{};
            pipes[index].used = true;
            return static_cast<int32_t>(index);
        }
    }
    return -1;
}

void bump_pipe_refs(const FileDesc& slot) {
    if (!slot.used || slot.backend != backend_pipe ||
        slot.inode >= max_pipes || !pipes[slot.inode].used) {
        return;
    }
    if (slot.readable != 0) {
        ++pipes[slot.inode].readers;
    }
    if (slot.writable != 0) {
        ++pipes[slot.inode].writers;
    }
}

void release_pipe_end(FileDesc& slot) {
    if (!slot.used || slot.backend != backend_pipe ||
        slot.inode >= max_pipes || !pipes[slot.inode].used) {
        return;
    }
    PipeBuf& pipe = pipes[slot.inode];
    if (slot.readable != 0 && pipe.readers > 0) {
        --pipe.readers;
    }
    if (slot.writable != 0 && pipe.writers > 0) {
        --pipe.writers;
    }
    if (pipe.readers == 0 && pipe.writers == 0) {
        pipes[slot.inode] = PipeBuf{};
    }
}

// Parent directory for "a/b/c" → "a/b". Single-component → empty (root).
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
    // Reject empty components ("a//b") and trailing slash (already filtered).
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

void fill_stat(uint32_t inode_index, StatBuf& out) {
    const Inode& node = inodes[inode_index];
    out = StatBuf{};
    out.st_dev = 1;
    out.st_ino = inode_index + 1;
    out.st_nlink = 1;
    if (node.is_dir) {
        out.st_mode = s_ifdir | 0755;
        out.st_size = 0;
    } else {
        out.st_mode = s_ifreg | 0644;
        out.st_size = static_cast<int64_t>(node.size);
    }
    out.st_blksize = static_cast<int64_t>(max_file_bytes);
    out.st_blocks = (out.st_size + 511) / 512;
}

}  // namespace

void reset() {
    for (uint32_t index = 0; index < max_files; ++index) {
        inodes[index] = Inode{};
    }
    for (uint32_t table = 0; table < 2; ++table) {
        for (uint32_t fd = 0; fd < fd_table_end; ++fd) {
            tables[table][fd] = FileDesc{};
        }
    }
    for (uint32_t index = 0; index < max_pipes; ++index) {
        pipes[index] = PipeBuf{};
    }
    for (uint32_t table = 0; table < 2; ++table) {
        memset(cwd_storage[table], 0, max_path);
        cwd_len[table] = 0;
    }
    live_table = 0;
    current_stats = Stats{};
}

const Stats& stats() {
    return current_stats;
}

int64_t mkdir_path(const char* path, uint64_t /*mode*/) {
    char resolved[max_path]{};
    size_t name_len = 0;
    if (!resolve_path(path, resolved, name_len) || name_len == 0) {
        return -1;
    }
    const char* name = resolved;
    const char* rest = nullptr;
    size_t rest_len = 0;
    if (disk_rest(name, name_len, rest, rest_len)) {
        const int64_t rc = cxcfs::mkdir_name(rest, rest_len);
        if (rc == 0) {
            ++current_stats.mkdirs;
            ++current_stats.dirs;
        }
        return rc;
    }
    if (find_inode(name, name_len) >= 0) {
        return -1;
    }
    if (!parent_dir_ready(name, name_len)) {
        return -1;
    }
    const int32_t inode = alloc_inode();
    if (inode < 0) {
        return -1;
    }
    memcpy(inodes[inode].name, name, name_len);
    inodes[inode].name[name_len] = '\0';
    inodes[inode].is_dir = true;
    ++current_stats.mkdirs;
    ++current_stats.dirs;
    return 0;
}

int64_t unlink_path(const char* path) {
    char resolved[max_path]{};
    size_t name_len = 0;
    if (!resolve_path(path, resolved, name_len) || name_len == 0) {
        return -1;
    }
    const char* name = resolved;
    const char* rest = nullptr;
    size_t rest_len = 0;
    if (disk_rest(name, name_len, rest, rest_len)) {
        for (uint32_t fd = fd_table_begin; fd < fd_table_end; ++fd) {
            if (fds[fd].used && fds[fd].backend == backend_disk) {
                // Busy check via name match would need reverse lookup; deny if
                // any disk fd open for simplicity when inode matches.
                const int32_t inode = cxcfs::find_inode(rest, rest_len);
                if (inode >= 0 &&
                    fds[fd].inode == static_cast<uint32_t>(inode)) {
                    return -1;
                }
            }
        }
        const int64_t rc = cxcfs::unlink_name(rest, rest_len);
        if (rc == 0) {
            ++current_stats.unlinks;
        }
        return rc;
    }
    const int32_t inode = find_inode(name, name_len);
    if (inode < 0) {
        return -2;  // -ENOENT
    }
    if (inodes[inode].is_dir) {
        return -1;
    }
    for (uint32_t fd = fd_table_begin; fd < fd_table_end; ++fd) {
        if (fds[fd].used && fds[fd].backend == backend_ram &&
            fds[fd].inode == static_cast<uint32_t>(inode)) {
            return -1;
        }
    }
    inodes[inode] = Inode{};
    ++current_stats.unlinks;
    if (current_stats.files > 0) {
        --current_stats.files;
    }
    return 0;
}

int64_t rename_path(const char* old_path, const char* new_path) {
    char old_resolved[max_path]{};
    char new_resolved[max_path]{};
    size_t old_len = 0;
    size_t new_len = 0;
    if (!resolve_path(old_path, old_resolved, old_len) || old_len == 0 ||
        !resolve_path(new_path, new_resolved, new_len) || new_len == 0) {
        return -1;
    }
    const char* old_name = old_resolved;
    const char* new_name = new_resolved;
    const char* old_rest = nullptr;
    size_t old_rest_len = 0;
    const char* new_rest = nullptr;
    size_t new_rest_len = 0;
    const bool old_disk = disk_rest(old_name, old_len, old_rest, old_rest_len);
    const bool new_disk = disk_rest(new_name, new_len, new_rest, new_rest_len);
    if (old_disk != new_disk) {
        return -1;  // no cross-backend rename
    }
    if (old_disk) {
        for (uint32_t fd = fd_table_begin; fd < fd_table_end; ++fd) {
            if (fds[fd].used && fds[fd].backend == backend_disk) {
                const int32_t inode = cxcfs::find_inode(old_rest, old_rest_len);
                if (inode >= 0 &&
                    fds[fd].inode == static_cast<uint32_t>(inode)) {
                    return -1;
                }
            }
        }
        const int64_t rc =
            cxcfs::rename_name(old_rest, old_rest_len, new_rest, new_rest_len);
        if (rc == 0) {
            ++current_stats.renames;
        }
        return rc;
    }
    const int32_t old_inode = find_inode(old_name, old_len);
    if (old_inode < 0) {
        return -2;
    }
    if (inodes[old_inode].is_dir) {
        return -1;
    }
    for (uint32_t fd = fd_table_begin; fd < fd_table_end; ++fd) {
        if (fds[fd].used && fds[fd].backend == backend_ram &&
            fds[fd].inode == static_cast<uint32_t>(old_inode)) {
            return -1;
        }
    }
    if (old_len == new_len && names_match(old_name, new_name, new_len)) {
        ++current_stats.renames;
        return 0;
    }
    const int32_t dest = find_inode(new_name, new_len);
    if (dest >= 0) {
        if (inodes[dest].is_dir) {
            return -1;
        }
        if (dest != old_inode) {
            char dest_path[max_path]{};
            memcpy(dest_path, new_name, new_len);
            if (unlink_path(dest_path) != 0) {
                return -1;
            }
        }
    }
    memcpy(inodes[old_inode].name, new_name, new_len);
    inodes[old_inode].name[new_len] = '\0';
    ++current_stats.renames;
    return 0;
}

int64_t open_path(const char* path, uint64_t flags, uint64_t /*mode*/) {
    char resolved[max_path]{};
    size_t name_len = 0;
    if (!resolve_path(path, resolved, name_len) || name_len == 0) {
        return -1;
    }
    const char* name = resolved;

    const bool creat = (flags & o_creat) != 0;
    const bool trunc = (flags & o_trunc) != 0;
    const uint64_t access = flags & 3ull;
    const bool want_read = access == o_rdonly || access == o_rdwr;
    const bool want_write = access == o_wronly || access == o_rdwr;
    if (!want_read && !want_write) {
        return -1;
    }

    if (path_is_dev_fb0(name, name_len) || path_is_dev_tty0(name, name_len)) {
        if (creat || trunc) {
            return -1;
        }
        const uint8_t backend =
            path_is_dev_fb0(name, name_len) ? backend_fb : backend_tty;
        const int32_t fd = alloc_fd();
        if (fd < 0) {
            return -1;
        }
        fds[fd].used = true;
        fds[fd].backend = backend;
        fds[fd].inode = 0;
        fds[fd].offset = 0;
        fds[fd].readable = want_read ? 1u : 0u;
        fds[fd].writable = want_write ? 1u : 0u;
        ++current_stats.opens;
        return fd;
    }

    const char* rest = nullptr;
    size_t rest_len = 0;
    if (disk_rest(name, name_len, rest, rest_len)) {
        const int32_t existing = cxcfs::find_inode(rest, rest_len);
        const bool is_dir =
            existing >= 0 &&
            cxcfs::inode_is_dir(static_cast<uint32_t>(existing));
        int32_t disk_inode = -1;
        if (is_dir) {
            if (want_write || creat || trunc) {
                return -1;
            }
            disk_inode = cxcfs::open_or_create(rest, rest_len, false, false,
                                               false, true);
        } else {
            disk_inode = cxcfs::open_or_create(rest, rest_len, creat, trunc,
                                               want_write, false);
        }
        if (disk_inode < 0) {
            return disk_inode;
        }
        const int32_t fd = alloc_fd();
        if (fd < 0) {
            return -1;
        }
        fds[fd].used = true;
        fds[fd].backend = backend_disk;
        fds[fd].inode = static_cast<uint32_t>(disk_inode);
        fds[fd].offset = 0;
        fds[fd].readable = want_read ? 1u : 0u;
        fds[fd].writable = want_write ? 1u : 0u;
        ++current_stats.opens;
        if (creat && !is_dir) {
            ++current_stats.files;
        }
        return fd;
    }

    int32_t inode = find_inode(name, name_len);
    if (inode >= 0) {
        if (inodes[inode].is_dir) {
            // Directory open: read-only (M50 getdents).
            if (want_write || creat || trunc) {
                return -1;
            }
        } else if (trunc && want_write) {
            inodes[inode].size = 0;
            memset(inodes[inode].data, 0, max_file_bytes);
        }
    } else {
        if (!creat) {
            return -2;  // -ENOENT (mini_libc errno)
        }
        if (!parent_dir_ready(name, name_len)) {
            return -1;
        }
        inode = alloc_inode();
        if (inode < 0) {
            return -1;
        }
        memcpy(inodes[inode].name, name, name_len);
        inodes[inode].name[name_len] = '\0';
        inodes[inode].is_dir = false;
        ++current_stats.files;
    }

    const int32_t fd = alloc_fd();
    if (fd < 0) {
        return -1;
    }
    fds[fd].used = true;
    fds[fd].backend = backend_ram;
    fds[fd].inode = static_cast<uint32_t>(inode);
    fds[fd].offset = 0;
    fds[fd].readable = want_read ? 1u : 0u;
    fds[fd].writable = want_write ? 1u : 0u;
    ++current_stats.opens;
    return fd;
}

int64_t close_fd(uint32_t fd) {
    if (fd < fd_table_begin || fd >= fd_table_end || !fds[fd].used) {
        return -1;
    }
    release_pipe_end(fds[fd]);
    fds[fd] = FileDesc{};
    ++current_stats.closes;
    return 0;
}

int32_t fd_backend(uint32_t fd) {
    if (fd < fd_table_begin || fd >= fd_table_end || !fds[fd].used) {
        return -1;
    }
    return static_cast<int32_t>(fds[fd].backend);
}

int64_t read_fd(uint32_t fd, void* buffer, size_t length) {
    if (buffer == nullptr || length == 0 || fd < fd_table_begin ||
        fd >= fd_table_end || !fds[fd].used || fds[fd].readable == 0) {
        return -1;
    }
    if (fds[fd].backend == backend_tty) {
        const size_t got =
            tty::read(static_cast<char*>(buffer), length);
        ++current_stats.reads;
        return static_cast<int64_t>(got);
    }
    if (fds[fd].backend == backend_fb) {
        ++current_stats.reads;
        return 0;
    }
    if (fds[fd].backend == backend_pipe) {
        if (fds[fd].inode >= max_pipes || !pipes[fds[fd].inode].used) {
            return -1;
        }
        PipeBuf& pipe = pipes[fds[fd].inode];
        if (pipe.count == 0) {
            ++current_stats.reads;
            return pipe.writers == 0 ? 0 : errno_again;
        }
        auto* out = static_cast<uint8_t*>(buffer);
        const size_t copy =
            length < pipe.count ? length : static_cast<size_t>(pipe.count);
        for (size_t i = 0; i < copy; ++i) {
            out[i] = pipe.data[pipe.head];
            pipe.head = (pipe.head + 1) % pipe_capacity;
        }
        pipe.count -= static_cast<uint32_t>(copy);
        ++current_stats.reads;
        return static_cast<int64_t>(copy);
    }
    if (fds[fd].backend == backend_disk) {
        if (cxcfs::inode_is_dir(fds[fd].inode)) {
            return -1;
        }
        const int64_t got =
            cxcfs::read_inode(fds[fd].inode, fds[fd].offset, buffer, length);
        if (got > 0) {
            fds[fd].offset += static_cast<uint32_t>(got);
        }
        if (got >= 0) {
            ++current_stats.reads;
        }
        return got;
    }
    Inode& node = inodes[fds[fd].inode];
    if (node.is_dir) {
        return -1;
    }
    if (fds[fd].offset >= node.size) {
        ++current_stats.reads;
        return 0;
    }
    const uint32_t available = node.size - fds[fd].offset;
    const size_t copy =
        length < available ? length : static_cast<size_t>(available);
    memcpy(buffer, node.data + fds[fd].offset, copy);
    fds[fd].offset += static_cast<uint32_t>(copy);
    ++current_stats.reads;
    return static_cast<int64_t>(copy);
}

int64_t write_fd(uint32_t fd, const void* buffer, size_t length) {
    if (buffer == nullptr || length == 0 || fd < fd_table_begin ||
        fd >= fd_table_end || !fds[fd].used || fds[fd].writable == 0) {
        return -1;
    }
    if (fds[fd].backend == backend_tty) {
        const size_t got =
            tty::write(static_cast<const char*>(buffer), length);
        ++current_stats.writes;
        return static_cast<int64_t>(got);
    }
    if (fds[fd].backend == backend_fb) {
        ++current_stats.writes;
        return static_cast<int64_t>(length);
    }
    if (fds[fd].backend == backend_pipe) {
        if (fds[fd].inode >= max_pipes || !pipes[fds[fd].inode].used) {
            return -1;
        }
        PipeBuf& pipe = pipes[fds[fd].inode];
        if (pipe.readers == 0) {
            return errno_pipe;
        }
        const uint32_t space = pipe_capacity - pipe.count;
        if (space == 0) {
            return errno_again;
        }
        const auto* in = static_cast<const uint8_t*>(buffer);
        const size_t copy =
            length < space ? length : static_cast<size_t>(space);
        for (size_t i = 0; i < copy; ++i) {
            pipe.data[pipe.tail] = in[i];
            pipe.tail = (pipe.tail + 1) % pipe_capacity;
        }
        pipe.count += static_cast<uint32_t>(copy);
        ++current_stats.writes;
        return static_cast<int64_t>(copy);
    }
    if (fds[fd].backend == backend_disk) {
        if (cxcfs::inode_is_dir(fds[fd].inode)) {
            return -1;
        }
        const int64_t got =
            cxcfs::write_inode(fds[fd].inode, fds[fd].offset, buffer, length);
        if (got > 0) {
            fds[fd].offset += static_cast<uint32_t>(got);
        }
        if (got >= 0) {
            ++current_stats.writes;
        }
        return got;
    }
    Inode& node = inodes[fds[fd].inode];
    if (node.is_dir) {
        return -1;
    }
    if (fds[fd].offset >= max_file_bytes) {
        return -1;
    }
    const uint32_t space = max_file_bytes - fds[fd].offset;
    const size_t copy =
        length < space ? length : static_cast<size_t>(space);
    if (copy == 0) {
        return -1;
    }
    memcpy(node.data + fds[fd].offset, buffer, copy);
    fds[fd].offset += static_cast<uint32_t>(copy);
    if (fds[fd].offset > node.size) {
        node.size = fds[fd].offset;
    }
    ++current_stats.writes;
    return static_cast<int64_t>(copy);
}

int64_t lseek_fd(uint32_t fd, int64_t offset, int whence) {
    if (fd < fd_table_begin || fd >= fd_table_end || !fds[fd].used) {
        return -1;
    }
    if (fds[fd].backend == backend_pipe || fds[fd].backend == backend_fb ||
        fds[fd].backend == backend_tty) {
        return -1;  // ESPIPE
    }
    int64_t size = 0;
    uint32_t size_cap = max_file_bytes;
    if (fds[fd].backend == backend_disk) {
        if (cxcfs::inode_is_dir(fds[fd].inode)) {
            return -1;
        }
        size = cxcfs::inode_size(fds[fd].inode);
        if (size < 0) {
            return -1;
        }
        size_cap = cxcfs::max_file_bytes;
    } else {
        Inode& node = inodes[fds[fd].inode];
        if (node.is_dir) {
            return -1;
        }
        size = static_cast<int64_t>(node.size);
    }

    int64_t base = 0;
    if (whence == seek_set) {
        base = 0;
    } else if (whence == seek_cur) {
        base = static_cast<int64_t>(fds[fd].offset);
    } else if (whence == seek_end) {
        base = size;
    } else {
        return -1;
    }

    const int64_t next = base + offset;
    if (next < 0 || static_cast<uint64_t>(next) > size_cap) {
        return -1;
    }
    fds[fd].offset = static_cast<uint32_t>(next);
    ++current_stats.lseeks;
    return next;
}

int64_t fstat_fd(uint32_t fd, StatBuf& out) {
    if (fd < fd_table_begin || fd >= fd_table_end || !fds[fd].used) {
        return -1;
    }
    if (fds[fd].backend == backend_pipe) {
        out = StatBuf{};
        out.st_mode = 0010000 | 0600;  // S_IFIFO
        out.st_nlink = 1;
        out.st_blksize = static_cast<int64_t>(pipe_capacity);
        ++current_stats.fstats;
        return 0;
    }
    if (fds[fd].backend == backend_fb || fds[fd].backend == backend_tty) {
        out = StatBuf{};
        out.st_mode = s_ifchr | 0666;
        out.st_nlink = 1;
        out.st_rdev = fds[fd].backend == backend_fb ? 29ull : 4ull;
        out.st_blksize = 4096;
        if (fds[fd].backend == backend_fb) {
            out.st_size = static_cast<int64_t>(fbdev::fb_size());
        }
        ++current_stats.fstats;
        return 0;
    }
    if (fds[fd].backend == backend_disk) {
        cxcfs::fill_stat(fds[fd].inode, out);
    } else {
        fill_stat(fds[fd].inode, out);
    }
    ++current_stats.fstats;
    return 0;
}

int64_t stat_path(const char* path, StatBuf& out) {
    char resolved[max_path]{};
    size_t name_len = 0;
    if (!resolve_path(path, resolved, name_len) || name_len == 0) {
        return -1;
    }
    const char* name = resolved;
    if (path_is_dev_fb0(name, name_len) || path_is_dev_tty0(name, name_len)) {
        out = StatBuf{};
        out.st_mode = s_ifchr | 0666;
        out.st_nlink = 1;
        out.st_rdev = path_is_dev_fb0(name, name_len) ? 29ull : 4ull;
        out.st_blksize = 4096;
        if (path_is_dev_fb0(name, name_len)) {
            out.st_size = static_cast<int64_t>(fbdev::fb_size());
        }
        ++current_stats.stats;
        return 0;
    }
    const char* rest = nullptr;
    size_t rest_len = 0;
    if (disk_rest(name, name_len, rest, rest_len)) {
        const int32_t inode = cxcfs::find_inode(rest, rest_len);
        if (inode < 0) {
            return -1;
        }
        cxcfs::fill_stat(static_cast<uint32_t>(inode), out);
        ++current_stats.stats;
        return 0;
    }
    const int32_t inode = find_inode(name, name_len);
    if (inode < 0) {
        return -1;
    }
    fill_stat(static_cast<uint32_t>(inode), out);
    ++current_stats.stats;
    return 0;
}

int64_t getdents64_fd(uint32_t fd, void* buffer, size_t length) {
    if (buffer == nullptr || length == 0 || length > max_getdents ||
        fd < fd_table_begin || fd >= fd_table_end || !fds[fd].used) {
        return -1;
    }
    if (fds[fd].backend == backend_pipe || fds[fd].backend == backend_fb ||
        fds[fd].backend == backend_tty) {
        return -1;
    }
    if (fds[fd].backend == backend_disk) {
        if (!cxcfs::inode_is_dir(fds[fd].inode) || fds[fd].readable == 0) {
            return -1;
        }
        // Simple one-shot listing (cookie ignored for M77).
        if (fds[fd].offset != 0) {
            return 0;
        }
        const int64_t got =
            cxcfs::getdents_inode(fds[fd].inode, buffer, length);
        if (got >= 0) {
            fds[fd].offset = 1;
            ++current_stats.getdents;
        }
        return got;
    }
    const Inode& dir = inodes[fds[fd].inode];
    if (!dir.is_dir || fds[fd].readable == 0) {
        return -1;
    }

    const char* dir_name = dir.name;
    const size_t dir_len = name_length(dir_name);

    // Cookie layout: 0=".", 1="..", 2+ = child index among matching inodes.
    uint32_t cookie = fds[fd].offset;
    size_t written = 0;
    auto* out = static_cast<uint8_t*>(buffer);

    auto emit = [&](uint64_t ino, uint8_t dtype, const char* name,
                    size_t name_len, uint32_t next_cookie) -> bool {
        const size_t base = 19 + name_len + 1;  // header + name + NUL
        const size_t reclen = (base + 7) & ~size_t{7};
        if (written + reclen > length) {
            return false;
        }
        // linux_dirent64 packed fields.
        *reinterpret_cast<uint64_t*>(out + written) = ino;
        *reinterpret_cast<int64_t*>(out + written + 8) =
            static_cast<int64_t>(next_cookie);
        *reinterpret_cast<uint16_t*>(out + written + 16) =
            static_cast<uint16_t>(reclen);
        out[written + 18] = dtype;
        memcpy(out + written + 19, name, name_len);
        out[written + 19 + name_len] = '\0';
        for (size_t pad = 19 + name_len + 1; pad < reclen; ++pad) {
            out[written + pad] = 0;
        }
        written += reclen;
        return true;
    };

    if (cookie == 0) {
        if (!emit(fds[fd].inode + 1, dt_dir, ".", 1, 1)) {
            return written == 0 ? -1 : static_cast<int64_t>(written);
        }
        cookie = 1;
    }
    if (cookie == 1) {
        if (!emit(1, dt_dir, "..", 2, 2)) {
            fds[fd].offset = 1;
            ++current_stats.getdents;
            return static_cast<int64_t>(written);
        }
        cookie = 2;
    }

    uint32_t child_ord = 0;
    for (uint32_t index = 0; index < max_files; ++index) {
        if (!inodes[index].used) {
            continue;
        }
        const char* child = inodes[index].name;
        const size_t child_len = name_length(child);
        if (child_len <= dir_len + 1) {
            continue;
        }
        bool prefix = true;
        for (size_t i = 0; i < dir_len; ++i) {
            if (child[i] != dir_name[i]) {
                prefix = false;
                break;
            }
        }
        if (!prefix || child[dir_len] != '/') {
            continue;
        }
        bool nested = false;
        for (size_t i = dir_len + 1; i < child_len; ++i) {
            if (child[i] == '/') {
                nested = true;
                break;
            }
        }
        if (nested) {
            continue;
        }

        const uint32_t this_cookie = 2 + child_ord;
        ++child_ord;
        if (this_cookie < cookie) {
            continue;
        }

        const char* base_name = child + dir_len + 1;
        const size_t base_len = child_len - (dir_len + 1);
        const uint8_t dtype = inodes[index].is_dir ? dt_dir : dt_reg;
        if (!emit(index + 1, dtype, base_name, base_len, this_cookie + 1)) {
            fds[fd].offset = this_cookie;
            ++current_stats.getdents;
            return static_cast<int64_t>(written);
        }
        cookie = this_cookie + 1;
    }

    fds[fd].offset = cookie;
    ++current_stats.getdents;
    return static_cast<int64_t>(written);
}

int64_t pipe_fds(int32_t out[2]) {
    if (out == nullptr) {
        return -1;
    }
    const int32_t pipe_id = alloc_pipe();
    if (pipe_id < 0) {
        return -1;
    }
    const int32_t rfd = alloc_fd();
    if (rfd < 0) {
        pipes[static_cast<uint32_t>(pipe_id)] = PipeBuf{};
        return -1;
    }
    // Mark read end used before allocating write end (alloc_fd scans .used).
    fds[static_cast<uint32_t>(rfd)].used = true;
    fds[static_cast<uint32_t>(rfd)].backend = backend_pipe;
    fds[static_cast<uint32_t>(rfd)].inode = static_cast<uint32_t>(pipe_id);
    fds[static_cast<uint32_t>(rfd)].offset = 0;
    fds[static_cast<uint32_t>(rfd)].readable = 1;
    fds[static_cast<uint32_t>(rfd)].writable = 0;

    const int32_t wfd = alloc_fd();
    if (wfd < 0) {
        fds[static_cast<uint32_t>(rfd)] = FileDesc{};
        pipes[static_cast<uint32_t>(pipe_id)] = PipeBuf{};
        return -1;
    }
    fds[static_cast<uint32_t>(wfd)].used = true;
    fds[static_cast<uint32_t>(wfd)].backend = backend_pipe;
    fds[static_cast<uint32_t>(wfd)].inode = static_cast<uint32_t>(pipe_id);
    fds[static_cast<uint32_t>(wfd)].offset = 0;
    fds[static_cast<uint32_t>(wfd)].readable = 0;
    fds[static_cast<uint32_t>(wfd)].writable = 1;

    pipes[static_cast<uint32_t>(pipe_id)].readers = 1;
    pipes[static_cast<uint32_t>(pipe_id)].writers = 1;

    out[0] = rfd;
    out[1] = wfd;
    ++current_stats.pipes;
    ++current_stats.opens;
    ++current_stats.opens;
    return 0;
}

int64_t dup_fd(uint32_t oldfd) {
    if (oldfd < fd_table_begin || oldfd >= fd_table_end || !fds[oldfd].used) {
        return -1;
    }
    const int32_t neu = alloc_fd();
    if (neu < 0) {
        return -1;
    }
    fds[static_cast<uint32_t>(neu)] = fds[oldfd];
    bump_pipe_refs(fds[static_cast<uint32_t>(neu)]);
    ++current_stats.dups;
    ++current_stats.opens;
    return neu;
}

int64_t dup2_fd(uint32_t oldfd, uint32_t newfd) {
    if (oldfd < fd_table_begin || oldfd >= fd_table_end || !fds[oldfd].used) {
        return -1;
    }
    if (newfd < fd_table_begin || newfd >= fd_table_end) {
        return -1;
    }
    if (oldfd == newfd) {
        return static_cast<int64_t>(newfd);
    }
    if (fds[newfd].used) {
        if (close_fd(newfd) != 0) {
            return -1;
        }
    }
    fds[newfd] = fds[oldfd];
    bump_pipe_refs(fds[newfd]);
    ++current_stats.dups;
    ++current_stats.opens;
    return static_cast<int64_t>(newfd);
}

void fork_clone_fd_table() {
    const uint32_t parent = live_table;
    const uint32_t child = parent == 0 ? 1u : 0u;
    for (uint32_t fd = 0; fd < fd_table_end; ++fd) {
        tables[child][fd] = tables[parent][fd];
        bump_pipe_refs(tables[child][fd]);
    }
    memcpy(cwd_storage[child], cwd_storage[parent], max_path);
    cwd_len[child] = cwd_len[parent];
    live_table = child;
}

void fork_restore_parent_fd_table() {
    const uint32_t parent = live_table == 0 ? 1u : 0u;
    for (uint32_t fd = fd_table_begin; fd < fd_table_end; ++fd) {
        if (fds[fd].used) {
            (void)close_fd(fd);
        }
    }
    live_table = parent;
}

int64_t chdir_path(const char* path) {
    char resolved[max_path]{};
    size_t abs_len = 0;
    if (!resolve_path(path, resolved, abs_len)) {
        return -1;
    }
    if (!path_is_dir(resolved, abs_len)) {
        // Missing → ENOENT; present non-dir → ENOTDIR.
        if (abs_len == 0) {
            return -1;
        }
        const char* rest = nullptr;
        size_t rest_len = 0;
        bool exists = false;
        if (disk_rest(resolved, abs_len, rest, rest_len)) {
            exists = cxcfs::find_inode(rest, rest_len) >= 0;
        } else {
            exists = find_inode(resolved, abs_len) >= 0;
        }
        return exists ? errno_notdir : static_cast<int64_t>(-2);
    }
    if (abs_len > 0) {
        memcpy(cwd, resolved, abs_len);
    }
    cwd[abs_len] = '\0';
    cwd_n = abs_len;
    ++current_stats.chdirs;
    return 0;
}

int64_t getcwd_path(char* buffer, size_t size) {
    if (buffer == nullptr || size == 0) {
        return -1;
    }
    const size_t needed = cwd_n == 0 ? 2 : cwd_n + 2;  // '/' + cwd + NUL
    if (size < needed) {
        return errno_range;
    }
    buffer[0] = '/';
    if (cwd_n > 0) {
        memcpy(buffer + 1, cwd, cwd_n);
    }
    buffer[needed - 1] = '\0';
    ++current_stats.getcwds;
    return static_cast<int64_t>(needed);
}

#undef fds
#undef cwd
#undef cwd_n

}  // namespace cxc::fs
