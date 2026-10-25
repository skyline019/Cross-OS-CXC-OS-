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
            return -1;
        }
        if (trunc && want_write) {
            inodes[inode].size = 0;
            memset(inodes[inode].data, 0, max_file_bytes);
        }
    } else {
        if (!creat) {
            return -1;
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
    const Inode& node = inodes[fds[fd].inode];
    out = StatBuf{};
    out.st_dev = 1;
    out.st_ino = fds[fd].inode + 1;
    out.st_nlink = 1;
    if (node.is_dir) {
        out.st_mode = s_ifdir | 0755;
        out.st_size = 0;
    } else {
        out.st_mode = s_ifreg | 0644;
        out.st_size = static_cast<int64_t>(node.size);
    }
    out.st_blksize = static_cast<int64_t>(max_file_bytes);
    out.st_blocks =
        (out.st_size + 511) / 512;  // 512-byte units, Linux convention.
    ++current_stats.fstats;
    return 0;
}

}  // namespace cxc::fs
