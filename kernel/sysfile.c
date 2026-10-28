//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"
#include "pmfs.h"
#include "pmfile.h"
#include "vfs.h"

static int fdalloc(struct file *f);

// Convert a full VFS component to xv6's historical fixed-width disk name.
static void
native_name(char dst[DIRSIZ], const char *src)
{
  int n = strlen(src);
  memset(dst, 0, DIRSIZ);
  memmove(dst, src, n < DIRSIZ ? n : DIRSIZ);
}

// Open a pinned PMFS inode, or create/open a child of a pinned directory.
// Path construction and opening occur under one lock, so a concurrent rename
// cannot redirect the operation to a different directory.
static int
pm_open_fd(int pmino, const char *name, int omode)
{
  struct file *f;
  char path[PMFILE_NAME];
  uint32 size = 0, type = 0;
  int ino = -1, owned_pin = 0, fd, writable;

  if ((omode & ~(O_WRONLY | O_RDWR | O_CREATE | O_TRUNC | O_APPEND)) ||
      (omode & 3) == 3)
    return -1;
  writable = (omode & (O_WRONLY | O_RDWR)) != 0;
  if ((omode & (O_TRUNC | O_APPEND)) && !writable)
    return -1;
  if ((f = filealloc()) == 0)
    return -1;
  pmfs_lock();
  if (name) {
    if (pmfile_child_path(pmino, name, path) != 0)
      goto bad;
    ino = pmfile_lookup_ino(pmino, name);
    if (ino < 0) {
      if (!(omode & O_CREATE) || pmfile_create(path) != 0 ||
          (ino = pmfile_lookup_ino(pmino, name)) < 0)
        goto bad;
    }
    owned_pin = 1;
  } else {
    ino = pmino; // the VFS walker owns this reference
    if (pmfile_path_ino(ino, path) != 0)
      goto bad;
  }
  if (pmfile_stat(path, &size, &type) != 0)
    goto bad;
  if (type == PMFILE_TYPE_DIR) {
    if (omode != O_RDONLY)
      goto bad;
    if (!owned_pin)
      pmfile_pin(ino);
    owned_pin = 0; // filepm_init_dir takes ownership
    filepm_init_dir(f, ino);
    pmfs_unlock();
    if ((fd = fdalloc(f)) < 0) {
      fileclose(f);
      return -1;
    }
    return fd;
  }
  if (type != PMFILE_TYPE_REG ||
      filepm_open_locked(f, path, ino, size, !(omode & O_WRONLY), writable,
                         (omode & O_APPEND) != 0, (omode & O_TRUNC) != 0) != 0)
    goto bad;
  if (owned_pin)
    pmfile_unpin(ino);
  pmfs_unlock();
  if ((fd = fdalloc(f)) < 0) {
    fileclose(f);
    return -1;
  }
  return fd;

bad:
  if (owned_pin)
    pmfile_unpin(ino);
  pmfs_unlock();
  fileclose(f);
  return -1;
}

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  argint(n, &fd);
  if (fd < 0 || fd >= NOFILE || (f = myproc()->ofile[fd]) == 0)
    return -1;
  if (pfd)
    *pfd = fd;
  if (pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *p = myproc();

  for (fd = 0; fd < NOFILE; fd++) {
    if (p->ofile[fd] == 0) {
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

uint64
sys_dup(void)
{
  struct file *f;
  int fd;

  if (argfd(0, 0, &f) < 0)
    return -1;
  if ((fd = fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

uint64
sys_read(void)
{
  struct file *f;
  int n;
  uint64 p;

  argaddr(1, &p);
  argint(2, &n);
  if (argfd(0, 0, &f) < 0)
    return -1;
  return fileread(f, p, n);
}

uint64
sys_write(void)
{
  struct file *f;
  int n;
  uint64 p;

  argaddr(1, &p);
  argint(2, &n);
  if (argfd(0, 0, &f) < 0)
    return -1;

  return filewrite(f, p, n);
}

uint64
sys_close(void)
{
  int fd;
  struct file *f;

  if (argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;
  return fileclose(f);
}

uint64
sys_fsync(void)
{
  struct file *f;
  if (argfd(0, 0, &f) < 0)
    return -1;
  return filefsync(f);
}

uint64
sys_fstat(void)
{
  struct file *f;
  uint64 st; // user pointer to struct stat

  argaddr(1, &st);
  if (argfd(0, 0, &f) < 0)
    return -1;
  return filestat(f, st);
}

// Hard links are currently supported only within the native xv6 backend.
uint64
sys_link(void)
{
  char name[MAXPATH], diskname[DIRSIZ], new[MAXPATH], old[MAXPATH];
  struct vnode src = {0}, parent = {0};
  struct inode *dp, *ip;
  int slash, rc = -1;

  if (argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;
  begin_op();
  if (vfs_walk(old, 0, &src, 0, 0) != 0 ||
      vfs_walk(new, 1, &parent, name, &slash) != 0 || slash ||
      src.kind != VFS_XV6 || parent.kind != VFS_XV6 ||
      vfs_ismount(&parent, name))
    goto out;
  ip = src.ip;
  dp = parent.ip;
  native_name(diskname, name);
  ilock(ip);
  if (ip->type == T_DIR || ip->nlink < 1 || ip->nlink >= NLINK_MAX) {
    iunlock(ip);
    goto out;
  }
  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  ilock(dp);
  if (dp->nlink == 0 || dp->dev != ip->dev ||
      dirlink(dp, diskname, ip->inum) < 0) {
    iunlock(dp);
    ilock(ip);
    ip->nlink--;
    iupdate(ip);
    iunlock(ip);
    goto out;
  }
  iunlock(dp);
  rc = 0;
out:
  vfs_put(&parent);
  vfs_put(&src);
  end_op();
  return rc;
}

// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for (off = 2 * sizeof(de); off < dp->size; off += sizeof(de)) {
    if (readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if (de.inum != 0)
      return 0;
  }
  return 1;
}

uint64
sys_unlink(void)
{
  struct vnode parent = {0};
  struct inode *ip, *dp;
  struct dirent de;
  char name[MAXPATH], diskname[DIRSIZ], path[MAXPATH], pmpath[PMFILE_NAME];
  uint off, type;
  int slash, rc = -1;

  if (argstr(0, path, MAXPATH) < 0)
    return -1;
  begin_op();
  if (vfs_walk(path, 1, &parent, name, &slash) != 0 ||
      vfs_ismount(&parent, name) || namecmp(name, ".") == 0 ||
      namecmp(name, "..") == 0)
    goto out;
  if (parent.kind == VFS_PMFS) {
    pmfs_lock();
    if (pmfile_child_path(parent.pmino, name, pmpath) == 0 &&
        (!slash ||
         (pmfile_stat(pmpath, 0, &type) == 0 && type == PMFILE_TYPE_DIR)))
      rc = pmfile_unlink(pmpath);
    pmfs_unlock();
    goto out;
  }
  if (parent.kind != VFS_XV6)
    goto out;
  dp = parent.ip;
  native_name(diskname, name);
  ilock(dp);
  if (dp->nlink == 0 || (ip = dirlookup(dp, diskname, &off)) == 0) {
    iunlock(dp);
    goto out;
  }
  ilock(ip);
  if (ip->nlink < 1)
    panic("unlink: nlink < 1");
  if ((slash && ip->type != T_DIR) || (ip->type == T_DIR && !isdirempty(ip))) {
    iunlockput(ip);
    iunlock(dp);
    goto out;
  }
  memset(&de, 0, sizeof(de));
  if (writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if (ip->type == T_DIR) {
    dp->nlink--;
    iupdate(dp);
  }
  iunlock(dp);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  rc = 0;
out:
  vfs_put(&parent);
  end_op();
  return rc;
}

// Consumes the parent inode reference, as the old create(path) did.
static struct inode *
create_at(struct inode *dp, char name[DIRSIZ], short type, short major,
          short minor)
{
  struct inode *ip;

  ilock(dp);

  if (dp->nlink == 0) {
    iunlockput(dp);
    return 0;
  }

  // a new directory's ".." would push dp->nlink past its maximum
  if (type == T_DIR && dp->nlink >= NLINK_MAX) {
    iunlockput(dp);
    return 0;
  }

  if ((ip = dirlookup(dp, name, 0)) != 0) {
    iunlockput(dp);
    ilock(ip);
    if (type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))
      return ip;
    iunlockput(ip);
    return 0;
  }

  if ((ip = ialloc(dp->dev, type)) == 0) {
    iunlockput(dp);
    return 0;
  }

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if (type == T_DIR) { // Create . and .. entries.
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if (dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      goto fail;
  }

  if (dirlink(dp, name, ip->inum) < 0)
    goto fail;

  if (type == T_DIR) {
    // now that success is guaranteed:
    dp->nlink++; // for ".."
    iupdate(dp);
  }

  iunlockput(dp);

  return ip;

fail:
  // something went wrong. de-allocate ip.
  ip->nlink = 0;
  iupdate(ip);
  iunlockput(ip);
  iunlockput(dp);
  return 0;
}

uint64
sys_open(void)
{
  char path[MAXPATH], name[MAXPATH], diskname[DIRSIZ];
  int fd, omode, slash, writable;
  struct vnode node = {0};
  struct file *f;
  struct inode *ip;

  argint(1, &omode);
  if (argstr(0, path, MAXPATH) < 0 ||
      (omode & ~(O_WRONLY | O_RDWR | O_CREATE | O_TRUNC | O_APPEND)) ||
      (omode & 3) == 3)
    return -1;
  writable = (omode & (O_WRONLY | O_RDWR)) != 0;
  if ((omode & (O_TRUNC | O_APPEND)) && !writable)
    return -1;
  begin_op();
  if (omode & O_CREATE) {
    if (vfs_walk(path, 1, &node, name, &slash) != 0 || slash ||
        vfs_ismount(&node, name))
      goto bad;
    if (node.kind == VFS_PMFS) {
      fd = pm_open_fd(node.pmino, name, omode);
      vfs_put(&node);
      end_op();
      return fd;
    }
    native_name(diskname, name);
    ip = node.ip;
    node.kind = VFS_NONE; // create_at consumes the parent reference
    if ((ip = create_at(ip, diskname, T_FILE, 0, 0)) == 0)
      goto bad;
  } else {
    if (vfs_walk(path, 0, &node, 0, 0) != 0)
      goto bad;
    if (node.kind == VFS_PMFS) {
      fd = pm_open_fd(node.pmino, 0, omode);
      vfs_put(&node);
      end_op();
      return fd;
    }
    ip = node.ip;
    node.kind = VFS_NONE; // this function owns the inode reference
    ilock(ip);
    if (ip->type == T_DIR && omode != O_RDONLY) {
      iunlockput(ip);
      goto bad;
    }
  }

  if (ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)) {
    iunlockput(ip);
    goto bad;
  }
  if ((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0) {
    if (f)
      fileclose(f);
    iunlockput(ip);
    goto bad;
  }
  if (ip->type == T_DEVICE) {
    f->type = FD_DEVICE;
    f->major = ip->major;
  } else {
    f->type = FD_INODE;
    f->off = 0;
  }
  f->ip = ip;
  f->readable = !(omode & O_WRONLY);
  f->writable = writable;
  f->append = (omode & O_APPEND) != 0;
  if ((omode & O_TRUNC) && ip->type == T_FILE)
    itrunc(ip);
  iunlock(ip);
  end_op();
  return fd;

bad:
  vfs_put(&node);
  end_op();
  return -1;
}

uint64
sys_mkdir(void)
{
  char path[MAXPATH], name[MAXPATH], diskname[DIRSIZ], pmpath[PMFILE_NAME];
  struct vnode parent = {0};
  struct inode *ip;
  int rc = -1;
  if (argstr(0, path, MAXPATH) < 0)
    return -1;
  begin_op();
  if (vfs_walk(path, 1, &parent, name, 0) != 0 || vfs_ismount(&parent, name))
    goto out;
  if (parent.kind == VFS_PMFS) {
    pmfs_lock();
    if (pmfile_child_path(parent.pmino, name, pmpath) == 0)
      rc = pmfile_mkdir(pmpath);
    pmfs_unlock();
  } else {
    native_name(diskname, name);
    ip = parent.ip;
    parent.kind = VFS_NONE;
    if ((ip = create_at(ip, diskname, T_DIR, 0, 0)) != 0) {
      iunlockput(ip);
      rc = 0;
    }
  }
out:
  vfs_put(&parent);
  end_op();
  return rc;
}

uint64
sys_mknod(void)
{
  struct vnode parent = {0};
  struct inode *ip;
  char path[MAXPATH], name[MAXPATH], diskname[DIRSIZ];
  int major, minor, slash, rc = -1;
  argint(1, &major);
  argint(2, &minor);
  if (argstr(0, path, MAXPATH) < 0)
    return -1;
  begin_op();
  if (vfs_walk(path, 1, &parent, name, &slash) != 0 || slash ||
      parent.kind != VFS_XV6 || vfs_ismount(&parent, name))
    goto out;
  native_name(diskname, name);
  ip = parent.ip;
  parent.kind = VFS_NONE;
  if ((ip = create_at(ip, diskname, T_DEVICE, major, minor)) != 0) {
    iunlockput(ip);
    rc = 0;
  }
out:
  vfs_put(&parent);
  end_op();
  return rc;
}

uint64
sys_chdir(void)
{
  char path[MAXPATH];
  struct vnode node = {0};
  struct inode *next;
  struct proc *p = myproc();
  int rc = -1;
  if (argstr(0, path, MAXPATH) < 0)
    return -1;
  begin_op();
  if (vfs_walk(path, 0, &node, 0, 0) != 0 || !vfs_isdir(&node))
    goto out;
  if (node.kind == VFS_PMFS) {
    if ((next = namei("/")) == 0)
      goto out;
    if (p->cwd_pm)
      pmfile_unpin(p->cwd_pm);
    iput(p->cwd);
    p->cwd = next; // native root is the anchor for crossing /pm/..
    p->cwd_pm = node.pmino;
    node.kind = VFS_NONE; // transfer the PMFS pin to the process
  } else {
    if (p->cwd_pm)
      pmfile_unpin(p->cwd_pm);
    iput(p->cwd);
    p->cwd = node.ip;
    p->cwd_pm = 0;
    node.kind = VFS_NONE;
  }
  rc = 0;
out:
  vfs_put(&node);
  end_op();
  return rc;
}

uint64
sys_exec(void)
{
  char path[MAXPATH], *argv[MAXARG];
  int i;
  uint64 uargv, uarg;

  argaddr(1, &uargv);
  if (argstr(0, path, MAXPATH) < 0)
    return -1;
  memset(argv, 0, sizeof(argv));
  for (i = 0;; i++) {
    if (i >= NELEM(argv)) {
      goto bad;
    }
    if (fetchaddr(uargv + sizeof(uint64) * i, (uint64 *)&uarg) < 0) {
      goto bad;
    }
    if (uarg == 0) {
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc();
    if (argv[i] == 0)
      goto bad;
    if (fetchstr(uarg, argv[i], PGSIZE) < 0)
      goto bad;
  }

  int ret = kexec(path, argv);

  for (i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);

  return ret;

bad:
  for (i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  return -1;
}

uint64
sys_pipe(void)
{
  uint64 fdarray; // user pointer to array of two integers
  struct file *rf, *wf;
  int fd0, fd1;
  struct proc *p = myproc();

  argaddr(0, &fdarray);
  if (pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if ((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0) {
    if (fd0 >= 0)
      p->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  if (copyout(p->pagetable, p->sz, fdarray, (char *)&fd0, sizeof(fd0)) < 0 ||
      copyout(p->pagetable, p->sz, fdarray + sizeof(fd0), (char *)&fd1,
              sizeof(fd1)) < 0) {
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}

uint64
sys_lseek(void)
{
  struct file *f;
  uint64 raw;
  int whence;
  if (argfd(0, 0, &f) < 0)
    return -1;
  argaddr(1, &raw);
  argint(2, &whence);
  return fileseek(f, (long)raw, whence);
}

uint64
sys_getdents(void)
{
  struct file *f;
  uint64 dst;
  int len;
  if (argfd(0, 0, &f) < 0)
    return -1;
  argaddr(1, &dst);
  argint(2, &len);
  return filegetdents(f, dst, len);
}

// Atomic rename is implemented for PMFS; native and cross-backend rename
// return -1 instead of pretending to provide an atomic operation.
uint64
sys_rename(void)
{
  char old[MAXPATH], new[MAXPATH], oname[MAXPATH], nname[MAXPATH];
  char opath[PMFILE_NAME], npath[PMFILE_NAME];
  struct vnode op = {0}, np = {0};
  uint32 type;
  int oslash, nslash, rc = -1;
  if (argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;
  begin_op();
  if (vfs_walk(old, 1, &op, oname, &oslash) != 0 ||
      vfs_walk(new, 1, &np, nname, &nslash) != 0 || op.kind != VFS_PMFS ||
      np.kind != VFS_PMFS)
    goto out;
  pmfs_lock();
  if (pmfile_child_path(op.pmino, oname, opath) == 0 &&
      pmfile_child_path(np.pmino, nname, npath) == 0 &&
      (!oslash ||
       (pmfile_stat(opath, 0, &type) == 0 && type == PMFILE_TYPE_DIR)) &&
      (!nslash ||
       (pmfile_stat(npath, 0, &type) == 0 && type == PMFILE_TYPE_DIR)))
    rc = pmfile_rename(opath, npath);
  pmfs_unlock();
out:
  vfs_put(&np);
  vfs_put(&op);
  end_op();
  return rc;
}
