//
// Support functions for system calls that involve file descriptors.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "stat.h"
#include "proc.h"
#include "pmfs.h"
#include "pmfile.h"
#include "pmalgo.h"
#include "vfs.h"
#include "fcntl.h"

extern uint64 sys_sync(void);

struct devsw devsw[NDEV];
struct {
  struct spinlock lock;
  struct file file[NFILE];
} ftable;

// One coherent working buffer per open PMFS inode, shared by independently
// opened struct files. Offsets remain per struct file; dup/fork share offsets.
// The PMFS sleep lock protects every field and serializes append/write/fsync.
struct pm_shared_file {
  int ino;
  int handle;
  int refs; // number of independent struct file objects
  char *data;
  uint len;
  int dirty;
};
static struct pm_shared_file pm_open_files[PMFILE_MAX_FD];

void
fileinit(void)
{
  initlock(&ftable.lock, "ftable");
}

// Allocate a file structure.
struct file *
filealloc(void)
{
  struct file *f;

  acquire(&ftable.lock);
  for (f = ftable.file; f < ftable.file + NFILE; f++) {
    if (f->ref == 0) {
      memset(f, 0, sizeof(*f));
      f->ref = 1;
      release(&ftable.lock);
      return f;
    }
  }
  release(&ftable.lock);
  return 0;
}

// Increment ref count for file f.
struct file *
filedup(struct file *f)
{
  acquire(&ftable.lock);
  if (f->ref < 1)
    panic("filedup");
  f->ref++;
  release(&ftable.lock);
  return f;
}

// Flush the inode's shared working version under the PMFS lock. On failure
// it stays dirty while any independently opened descriptor remains alive.
static int
pmfile_commit_locked(struct pm_shared_file *pm)
{
  if (!pmfs_mounted() || pm == 0)
    return -1;
  if (!pm->dirty)
    return 0;
  if (pmfile_shared_sync(pm->handle, pm->data, pm->len) != 0)
    return -1;
  pm->dirty = 0;
  return 0;
}

int
filefsync(struct file *f)
{
  int rc;
  if (f->type == FD_PMFS) {
    pmfs_lock();
    rc = pmfile_commit_locked(f->pm);
    pmfs_unlock();
    return rc;
  }
  if (f->type == FD_INODE)
    return (int)sys_sync(); // xv6 exposes only global log sync
  return -1;
}

// Caller holds pmfs_lock. On success f owns one reference to a shared buffer;
// no entry is visible to global sync until the initialized file is published.
int
filepm_open_locked(struct file *f, const char *path, int ino, uint size,
                   int readable, int writable, int append, int trunc)
{
  struct pm_shared_file *pm = 0, *free_slot = 0;
  uint32 len = 0, oldlen;
  int i, fresh = 0, olddirty;

  for (i = 0; i < PMFILE_MAX_FD; i++) {
    if (pm_open_files[i].refs && pm_open_files[i].ino == ino)
      pm = &pm_open_files[i];
    if (!pm_open_files[i].refs && free_slot == 0)
      free_slot = &pm_open_files[i];
  }
  if (pm == 0) {
    if (free_slot == 0)
      return -1;
    pm = free_slot;
    memset(pm, 0, sizeof(*pm));
    if ((pm->data = kalloc()) == 0)
      return -1;
    if ((pm->handle = pmfile_shared_open(path)) < 0) {
      kfree(pm->data);
      memset(pm, 0, sizeof(*pm));
      return -1;
    }
    if (pmfile_shared_read(pm->handle, pm->data, PMFILE_MAX_SIZE, &len) != 1 ||
        len != size) {
      pmfile_shared_close(pm->handle, 1);
      kfree(pm->data);
      memset(pm, 0, sizeof(*pm));
      return -1;
    }
    pm->ino = ino;
    pm->len = len;
    fresh = 1;
  }

  oldlen = pm->len;
  olddirty = pm->dirty;
  // Truncating an already clean, empty version changes no observable state;
  // avoid a redundant run/checkpoint and keep the other slot untouched.
  if (trunc && (pm->len != 0 || pm->dirty)) {
    pm->len = 0;
    pm->dirty = 1;
    if (pmfile_commit_locked(pm) != 0) {
      pm->len = oldlen;
      pm->dirty = olddirty;
      if (fresh) {
        pmfile_shared_close(pm->handle, 1);
        kfree(pm->data);
        memset(pm, 0, sizeof(*pm));
      }
      return -1;
    }
  }
  pm->refs++;
  acquire(&ftable.lock);
  f->readable = readable;
  f->writable = writable;
  f->append = append;
  f->off = 0;
  f->pmino = ino;
  f->pm = pm;
  f->type = FD_PMFS;
  release(&ftable.lock);
  return 0;
}

// Takes ownership of the caller's PMFS directory pin.
void
filepm_init_dir(struct file *f, int ino)
{
  acquire(&ftable.lock);
  f->readable = 1;
  f->pmino = ino;
  f->type = FD_PMFS_DIR;
  release(&ftable.lock);
}

// PMFS's lock already protects the shared inode table and its lifetimes.
// Do not pin struct file objects here: an artificial file ref can make the
// concurrent last close return success before the actual flush has run.
int
filepm_sync_all(void)
{
  int i, rc = 0;
  pmfs_lock();
  for (i = 0; i < PMFILE_MAX_FD; i++)
    if (pm_open_files[i].refs && pmfile_commit_locked(&pm_open_files[i]) != 0)
      rc = -1;
  pmfs_unlock();
  return rc;
}

// Close file f; the last PMFS reference commits and reports errors.
int
fileclose(struct file *f)
{
  struct file ff;
  int rc = 0;

  acquire(&ftable.lock);
  if (f->ref < 1)
    panic("fileclose");
  if (--f->ref > 0) {
    release(&ftable.lock);
    return 0;
  }
  ff = *f;
  f->ref = 0;
  f->type = FD_NONE;
  release(&ftable.lock);

  if (ff.type == FD_PIPE) {
    pipeclose(ff.pipe, ff.writable);
  } else if (ff.type == FD_INODE || ff.type == FD_DEVICE) {
    begin_op();
    iput(ff.ip);
    end_op();
  } else if (ff.type == FD_PMFS) {
    pmfs_lock();
    struct pm_shared_file *pm = ff.pm;
    if (pmfile_commit_locked(pm) != 0)
      rc = -1;
    if (--pm->refs == 0) {
      if (pmfile_shared_close(pm->handle, rc != 0) != 0) {
        pmfile_shared_close(pm->handle, 1);
        rc = -1;
      }
      kfree(pm->data);
      memset(pm, 0, sizeof(*pm));
    }
    pmfs_unlock();
  } else if (ff.type == FD_PMFS_DIR) {
    pmfile_unpin(ff.pmino);
  }
  return rc;
}

// Get metadata about file f.
// addr is a user virtual address, pointing to a struct stat.
int
filestat(struct file *f, uint64 addr)
{
  struct proc *p = myproc();
  struct stat st;

  if (f->type == FD_PMFS || f->type == FD_PMFS_DIR) {
    pmfs_lock();
    if (!pmfs_mounted() || pmfile_type_ino(f->pmino) < 0) {
      pmfs_unlock();
      return -1;
    }
    memset(&st, 0, sizeof(st));
    st.dev = 2;
    st.ino = f->pmino;
    st.type = f->type == FD_PMFS_DIR ? T_DIR : T_FILE;
    st.nlink = pmfile_nlink_ino(f->pmino);
    st.size = f->type == FD_PMFS_DIR
                ? (PMFILE_MAX_INO + 2) * sizeof(struct dirent)
                : f->pm->len;
    pmfs_unlock();
    return copyout(p->pagetable, p->sz, addr, (char *)&st, sizeof(st)) < 0 ? -1
                                                                           : 0;
  }
  if (f->type == FD_INODE || f->type == FD_DEVICE) {
    ilock(f->ip);
    stati(f->ip, &st);
    iunlock(f->ip);
    if (copyout(p->pagetable, p->sz, addr, (char *)&st, sizeof(st)) < 0)
      return -1;
    return 0;
  }
  return -1;
}

// Read from file f.
// addr is a user virtual address.
int
fileread(struct file *f, uint64 addr, int n)
{
  int r = 0;

  if (f->readable == 0 || n < 0)
    return -1;

  if (f->type == FD_PMFS_DIR) {
    struct proc *p = myproc();
    struct vfs_dirent ent;
    struct dirent de;
    uint limit = (PMFILE_MAX_INO + 2) * sizeof(de);
    pmfs_lock();
    while (r < n && f->off < limit) {
      uint within = f->off % sizeof(de);
      int take = sizeof(de) - within;
      int found = pmfile_dirent(f->pmino, f->off / sizeof(de), &ent);
      if (found < 0 || (found && strlen(ent.name) > DIRSIZ)) {
        if (!r)
          r = -1; // use getdents for names longer than the xv6 dirent ABI
        break;
      }
      memset(&de, 0, sizeof(de));
      if (found) {
        de.inum = ent.ino;
        memmove(de.name, ent.name, strlen(ent.name));
      }
      if (take > n - r)
        take = n - r;
      if (copyout(p->pagetable, p->sz, addr + r, (char *)&de + within, take) <
          0) {
        if (!r)
          r = -1;
        break;
      }
      f->off += take;
      r += take;
    }
    pmfs_unlock();
  } else if (f->type == FD_PMFS) {
    struct proc *p = myproc();
    pmfs_lock();
    if (!pmfs_mounted()) {
      pmfs_unlock();
      return -1;
    }
    r = f->off < f->pm->len ? f->pm->len - f->off : 0;
    if (r > n)
      r = n;
    if (r && copyout(p->pagetable, p->sz, addr, f->pm->data + f->off, r) < 0)
      r = -1;
    if (r > 0)
      f->off += r;
    pmfs_unlock();
  } else if (f->type == FD_PIPE) {
    r = piperead(f->pipe, addr, n);
  } else if (f->type == FD_DEVICE) {
    if (f->major < 0 || f->major >= NDEV || !devsw[f->major].read)
      return -1;
    r = devsw[f->major].read(1, addr, n);
  } else if (f->type == FD_INODE) {
    ilock(f->ip);
    if ((r = readi(f->ip, 1, addr, f->off, n)) > 0)
      f->off += r;
    iunlock(f->ip);
  } else {
    panic("fileread");
  }

  return r;
}

// Write to file f.
// addr is a user virtual address.
int
filewrite(struct file *f, uint64 addr, int n)
{
  int r, ret = 0;

  if (f->writable == 0 || n < 0)
    return -1;

  if (f->type == FD_PMFS) {
    char tmp[PMFILE_MAX_SIZE];
    struct proc *p = myproc();
    if (n > PMFILE_MAX_SIZE ||
        (n && copyin(p->pagetable, p->sz, tmp, addr, n) < 0))
      return -1;
    pmfs_lock();
    uint off = f->append ? f->pm->len : f->off;
    if (!pmfs_mounted() || off > PMFILE_MAX_SIZE ||
        (uint)n > PMFILE_MAX_SIZE - off) {
      ret = -1;
    } else {
      if (n) {
        struct pm_algo *a = pm_algo_global();
        int identical = off <= f->pm->len && (uint)n <= f->pm->len - off &&
                        memcmp(f->pm->data + off, tmp, n) == 0;
        a->file_write_bytes += n;
        if (identical) {
          a->file_identical_bytes += n;
          a->file_identical_ops++;
        } else {
          if (off > f->pm->len)
            memset(f->pm->data + f->pm->len, 0, off - f->pm->len);
          memmove(f->pm->data + off, tmp, n);
          if (off + n > f->pm->len)
            f->pm->len = off + n;
          f->pm->dirty = 1;
        }
        f->off = off + n;
      }
      ret = n;
    }
    pmfs_unlock();
  } else if (f->type == FD_PIPE) {
    ret = pipewrite(f->pipe, addr, n);
  } else if (f->type == FD_DEVICE) {
    if (f->major < 0 || f->major >= NDEV || !devsw[f->major].write)
      return -1;
    ret = devsw[f->major].write(1, addr, n);
  } else if (f->type == FD_INODE) {
    // write a few blocks at a time to avoid exceeding
    // the maximum log transaction size, including
    // i-node, indirect block, allocation blocks,
    // and 2 blocks of slop for non-aligned writes.
    int max = ((MAXOPBLOCKS - 1 - 1 - 2) / 2) * BSIZE;
    int i = 0;
    while (i < n) {
      int n1 = n - i;
      if (n1 > max)
        n1 = max;

      begin_op();
      ilock(f->ip);
      uint off = f->append ? f->ip->size : f->off;
      if ((r = writei(f->ip, 1, addr + i, off, n1)) > 0)
        f->off = off + r;
      iunlock(f->ip);
      end_op();

      if (r != n1) {
        // error from writei
        break;
      }
      i += r;
    }
    ret = (i == n ? n : -1);
  } else {
    panic("filewrite");
  }

  return ret;
}

// SEEK_SET/CUR/END for regular files; directory descriptors can be rewound.
// Native xv6 does not implement sparse writes; PMFS fills bounded holes with 0.
long
fileseek(struct file *f, long offset, int whence)
{
  long base, end, max, result = -1;
  int dir = 0;
  if (f->type == FD_PMFS || f->type == FD_PMFS_DIR) {
    pmfs_lock();
    dir = f->type == FD_PMFS_DIR;
    end = dir ? (PMFILE_MAX_INO + 2) * sizeof(struct dirent) : f->pm->len;
    max = dir ? end : PMFILE_MAX_SIZE;
  } else if (f->type == FD_INODE) {
    ilock(f->ip);
    dir = f->ip->type == T_DIR;
    end = f->ip->size;
    max = 0xffffffffL;
  } else {
    return -1;
  }
  if (dir) {
    if (whence == SEEK_SET && offset == 0) {
      f->off = 0;
      result = 0;
    }
    goto out;
  }
  if (whence == SEEK_SET)
    base = 0;
  else if (whence == SEEK_CUR)
    base = f->off;
  else if (whence == SEEK_END)
    base = end;
  else
    goto out;
  if (offset < -base || offset > max - base)
    goto out;
  result = base + offset;
  f->off = result;
out:
  if (f->type == FD_PMFS || f->type == FD_PMFS_DIR)
    pmfs_unlock();
  else
    iunlock(f->ip);
  return result;
}

// Backend-independent fixed-size directory entries. f->off tracks native
// dirent slots for both backends, so dup/fork share enumeration position.
int
filegetdents(struct file *f, uint64 addr, int n)
{
  struct proc *p = myproc();
  struct vfs_dirent ent;
  struct dirent de;
  int done = 0, found;
  if (!f->readable || n < (int)sizeof(ent))
    return -1;
  if (f->type == FD_PMFS_DIR) {
    pmfs_lock();
    if (f->off % sizeof(de)) {
      pmfs_unlock();
      return -1;
    }
    while (n - done >= (int)sizeof(ent) &&
           f->off / sizeof(de) < PMFILE_MAX_INO + 2) {
      found = pmfile_dirent(f->pmino, f->off / sizeof(de), &ent);
      if (found < 0) {
        if (!done)
          done = -1;
        break;
      }
      if (found && copyout(p->pagetable, p->sz, addr + done, (char *)&ent,
                           sizeof(ent)) < 0) {
        if (!done)
          done = -1;
        break;
      }
      f->off += sizeof(de);
      if (found)
        done += sizeof(ent);
    }
    pmfs_unlock();
    return done;
  }
  if (f->type != FD_INODE)
    return -1;
  ilock(f->ip);
  if (f->ip->type != T_DIR || f->off % sizeof(de)) {
    iunlock(f->ip);
    return -1;
  }
  while (n - done >= (int)sizeof(ent)) {
    int root = f->ip->dev == ROOTDEV && f->ip->inum == ROOTINO;
    memset(&ent, 0, sizeof(ent));
    if (f->off < f->ip->size) {
      if (readi(f->ip, 0, (uint64)&de, f->off, sizeof(de)) != sizeof(de)) {
        if (!done)
          done = -1;
        break;
      }
      if (!de.inum || (root && namecmp(de.name, "pm") == 0)) {
        f->off += sizeof(de);
        continue;
      }
      ent.ino = de.inum;
      memmove(ent.name, de.name, DIRSIZ);
      ent.name[DIRSIZ] = 0;
      // Native type is available through fstat/stat, as in xv6's old dirent.
    } else if (root && f->off == f->ip->size && pmfs_mounted()) {
      ent.ino = PMFILE_ROOT_INO;
      ent.type = T_DIR;
      safestrcpy(ent.name, "pm", sizeof(ent.name));
    } else {
      break;
    }
    if (copyout(p->pagetable, p->sz, addr + done, (char *)&ent, sizeof(ent)) <
        0) {
      if (!done)
        done = -1;
      break;
    }
    f->off += sizeof(de);
    done += sizeof(ent);
  }
  iunlock(f->ip);
  return done;
}
