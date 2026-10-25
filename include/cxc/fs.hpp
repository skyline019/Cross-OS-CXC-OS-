#pragma once

#include <stddef.h>
#include <stdint.h>

namespace cxc::fs {

// Linux open flags used by the M39 probe (subset).
constexpr uint64_t o_rdonly = 0;
constexpr uint64_t o_wronly = 1;
constexpr uint64_t o_rdwr = 2;
constexpr uint64_t o_creat = 64;
constexpr uint64_t o_trunc = 512;

constexpr int seek_set = 0;
constexpr int seek_cur = 1;
constexpr int seek_end = 2;

// Linux mode bits used by fstat probes.
constexpr uint32_t s_ifmt = 0170000;
constexpr uint32_t s_ifreg = 0100000;
constexpr uint32_t s_ifdir = 0040000;

// linux_dirent64.d_type values.
constexpr uint8_t dt_unknown = 0;
constexpr uint8_t dt_dir = 4;
constexpr uint8_t dt_reg = 8;

constexpr size_t max_path = 48;
constexpr size_t max_file_bytes = 4096;
constexpr size_t max_files = 16;
constexpr size_t max_getdents = 4096;
constexpr uint32_t fd_stdout = 1;
constexpr uint32_t fd_table_begin = 3;
constexpr uint32_t fd_table_end = 16;

// Linux x86-64 kernel `struct stat` (syscall ABI).
struct StatBuf {
    uint64_t st_dev;
    uint64_t st_ino;
    uint64_t st_nlink;
    uint32_t st_mode;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t __pad0;
    uint64_t st_rdev;
    int64_t st_size;
    int64_t st_blksize;
    int64_t st_blocks;
    uint64_t st_atime;
    uint64_t st_atime_nsec;
    uint64_t st_mtime;
    uint64_t st_mtime_nsec;
    uint64_t st_ctime;
    uint64_t st_ctime_nsec;
    int64_t __unused[3];
};
static_assert(sizeof(StatBuf) == 144);

struct Stats {
    uint32_t opens;
    uint32_t closes;
    uint32_t reads;
    uint32_t writes;
    uint32_t lseeks;
    uint32_t fstats;
    uint32_t stats;
    uint32_t getdents;
    uint32_t mkdirs;
    uint32_t files;
    uint32_t dirs;
};

// Clears inodes and the per-session fd table (call on user session begin).
void reset();
const Stats& stats();

// Path must be a kernel-side NUL-terminated string (already copied).
// Supports flat names and one-level nested paths after mkdir (M42).
int64_t open_path(const char* path, uint64_t flags, uint64_t mode);
int64_t mkdir_path(const char* path, uint64_t mode);
int64_t close_fd(uint32_t fd);
int64_t read_fd(uint32_t fd, void* buffer, size_t length);
int64_t write_fd(uint32_t fd, const void* buffer, size_t length);
int64_t lseek_fd(uint32_t fd, int64_t offset, int whence);
int64_t fstat_fd(uint32_t fd, StatBuf& out);
int64_t stat_path(const char* path, StatBuf& out);
// Directory fd only. Writes packed linux_dirent64 records; returns bytes.
int64_t getdents64_fd(uint32_t fd, void* buffer, size_t length);

}  // namespace cxc::fs
