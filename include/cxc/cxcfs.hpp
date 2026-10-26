#pragma once

#include <cxc/fs.hpp>

#include <stddef.h>
#include <stdint.h>

namespace cxc::cxcfs {

// On-disk CXC filesystem over virtio-blk (A2 durable root). Paths use the
// `disk/` prefix at the fs façade (e.g. disk/persist.txt).
// LBA0–1 reserved (M75 magic / M76 R/W). Superblock at LBA 2.

// M79→M81: 64 files × 32 KiB; M88 raises extent to 128 KiB for toolchain objs.
constexpr uint32_t max_files = 64;
constexpr uint32_t max_file_bytes = 131072;  // 256 sectors
constexpr uint32_t sectors_per_file = max_file_bytes / 512;

struct MountResult {
    uint32_t stage;
    uint32_t failure;
    uint32_t formatted;
    uint32_t mounted;
    uint32_t persist_ok;
    uint32_t remount_ok;
    uint32_t oob_path_rejected;
    uint32_t missing_rejected;
    uint32_t pass;
    uint32_t inode_count;
    uint64_t capacity_sectors;
};

bool is_mounted();
const MountResult& mount_result();

// Format if needed, then mount. Requires virtio_blk::is_ready().
bool mount(MountResult& result);

// Drop in-memory cache and reload from disk (persistence proof).
bool remount(MountResult& result);

// Inode ops used by fs façade (relative name without disk/ prefix).
int32_t find_inode(const char* name, size_t length);
int64_t mkdir_name(const char* name, size_t length);
int64_t unlink_name(const char* name, size_t length);
// Rename within CXCFS; replaces existing regular dest. Dirs not renamed.
int64_t rename_name(const char* old_name, size_t old_len, const char* new_name,
                    size_t new_len);
int32_t open_or_create(const char* name, size_t length, bool creat, bool trunc,
                       bool want_write, bool is_dir_open);
int64_t read_inode(uint32_t inode, uint32_t offset, void* buffer, size_t length);
int64_t write_inode(uint32_t inode, uint32_t offset, const void* buffer,
                    size_t length);
int64_t inode_size(uint32_t inode);
bool inode_is_dir(uint32_t inode);
void fill_stat(uint32_t inode, fs::StatBuf& out);
int64_t getdents_inode(uint32_t inode, void* buffer, size_t length);

// Kernel contract: mount → write → remount → read + edges.
bool run_probe(MountResult& result);

}  // namespace cxc::cxcfs
