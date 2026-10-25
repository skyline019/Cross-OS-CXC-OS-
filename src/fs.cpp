#include <cxc/fs.hpp>

#include <stddef.h>
#include <stdint.h>

extern "C" void* memcpy(void*, const void*, size_t);
extern "C" void* memset(void*, int, size_t);

namespace cxc::fs {
namespace {

struct Inode {
    bool used;
    bool is_dir;
    char name[max_path];
    uint32_t size;
    uint8_t data[max_file_bytes];
};

struct FileDesc {
    bool used;
    uint32_t inode;
    uint32_t offset;
    uint32_t writable;
    uint32_t readable;
};

Inode inodes[max_files]{};
FileDesc fds[fd_table_end]{};
Stats current_stats{};

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

// Strip a single leading '/'; reject empty, trailing '/', or '//'.
bool normalize(const char* path, const char*& out, size_t& out_len) {
    if (path == nullptr) {
        return false;
    }
    const size_t length = name_length(path);
    if (length == 0 || length >= max_path || path[length] != '\0') {
        return false;
    }
    out = path;
    out_len = length;
    if (path[0] == '/') {
        if (length < 2) {
            return false;
        }
        out = path + 1;
        out_len = length - 1;
    }
    if (out_len == 0 || out[out_len - 1] == '/') {
        return false;
    }
    for (size_t index = 0; index < out_len; ++index) {
        if (out[index] == '\0') {
            return false;
        }
        if (out[index] == '/' && index + 1 < out_len && out[index + 1] == '/') {
            return false;
        }
    }
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

// Parent directory name for "dir/file" → "dir". Single-component → empty.
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
    // Only one slash level for the thin slice.
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
    for (uint32_t fd = 0; fd < fd_table_end; ++fd) {
        fds[fd] = FileDesc{};
    }
    current_stats = Stats{};
}

const Stats& stats() {
    return current_stats;
}

int64_t mkdir_path(const char* path, uint64_t /*mode*/) {
    const char* name = nullptr;
    size_t name_len = 0;
    if (!normalize(path, name, name_len)) {
        return -1;
    }
    // Single-component directory names only (no nested mkdir yet).
    for (size_t index = 0; index < name_len; ++index) {
        if (name[index] == '/') {
            return -1;
        }
    }
    if (find_inode(name, name_len) >= 0) {
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
    const char* name = nullptr;
    size_t name_len = 0;
    if (!normalize(path, name, name_len)) {
        return -1;
    }
    const int32_t inode = find_inode(name, name_len);
    if (inode < 0) {
        return -2;  // -ENOENT
    }
    if (inodes[inode].is_dir) {
        return -1;
    }
    for (uint32_t fd = fd_table_begin; fd < fd_table_end; ++fd) {
        if (fds[fd].used && fds[fd].inode == static_cast<uint32_t>(inode)) {
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

int64_t open_path(const char* path, uint64_t flags, uint64_t /*mode*/) {
    const char* name = nullptr;
    size_t name_len = 0;
    if (!normalize(path, name, name_len)) {
        return -1;
    }

    const bool creat = (flags & o_creat) != 0;
    const bool trunc = (flags & o_trunc) != 0;
    const uint64_t access = flags & 3ull;
    const bool want_read = access == o_rdonly || access == o_rdwr;
    const bool want_write = access == o_wronly || access == o_rdwr;
    if (!want_read && !want_write) {
        return -1;
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
    fds[fd] = FileDesc{};
    ++current_stats.closes;
    return 0;
}

int64_t read_fd(uint32_t fd, void* buffer, size_t length) {
    if (buffer == nullptr || length == 0 || fd < fd_table_begin ||
        fd >= fd_table_end || !fds[fd].used || fds[fd].readable == 0) {
        return -1;
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
    Inode& node = inodes[fds[fd].inode];
    if (node.is_dir) {
        return -1;
    }

    int64_t base = 0;
    if (whence == seek_set) {
        base = 0;
    } else if (whence == seek_cur) {
        base = static_cast<int64_t>(fds[fd].offset);
    } else if (whence == seek_end) {
        base = static_cast<int64_t>(node.size);
    } else {
        return -1;
    }

    const int64_t next = base + offset;
    if (next < 0 || static_cast<uint64_t>(next) > max_file_bytes) {
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
    fill_stat(fds[fd].inode, out);
    ++current_stats.fstats;
    return 0;
}

int64_t stat_path(const char* path, StatBuf& out) {
    const char* name = nullptr;
    size_t name_len = 0;
    if (!normalize(path, name, name_len)) {
        return -1;
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

}  // namespace cxc::fs
