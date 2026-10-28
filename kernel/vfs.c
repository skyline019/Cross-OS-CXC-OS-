// Component-by-component namespace traversal. PMFS is mounted at the
// reserved /pm entry of the native root. This is a fixed mount, not yet a
// dynamic mount table or a general POSIX VFS.
#include "types.h"
#include "param.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "stat.h"
#include "defs.h"
#include "pmfs.h"
#include "pmfile.h"
#include "vfs.h"

void
vfs_put(struct vnode *node)
{
  if (node->kind == VFS_XV6)
    iput(node->ip);
  else if (node->kind == VFS_PMFS)
    pmfile_unpin(node->pmino);
  node->kind = VFS_NONE;
  node->ip = 0;
  node->pmino = 0;
}

int
vfs_isdir(struct vnode *node)
{
  int result = 0;
  if (node->kind == VFS_XV6) {
    ilock(node->ip);
    result = node->ip->type == T_DIR && node->ip->nlink != 0;
    iunlock(node->ip);
  } else if (node->kind == VFS_PMFS) {
    pmfs_lock();
    result = pmfile_type_ino(node->pmino) == PMFILE_TYPE_DIR;
    pmfs_unlock();
  }
  return result;
}

int
vfs_ismount(struct vnode *parent, const char *name)
{
  return parent->kind == VFS_XV6 && parent->ip->dev == ROOTDEV &&
         parent->ip->inum == ROOTINO && name[0] == 'p' && name[1] == 'm' &&
         name[2] == 0;
}

// The caller keeps the returned vnode pinned, and releases it with vfs_put.
// Each element is actually looked up before processing the next one: in
// particular, "missing/../x" must not be normalized to "x".
int
vfs_walk(const char *path, int parent, struct vnode *out, char *name,
         int *trailing_slash)
{
  struct vnode cur = {0}, next = {0};
  struct proc *p = myproc();
  char elem[MAXPATH], diskname[DIRSIZ];
  const char *start;
  int len, last, slash, pm;

  if (path == 0 || *path == 0 || out == 0 || (parent && name == 0))
    return -1;
  len = strlen(path);
  slash = len > 0 && path[len - 1] == '/';
  if (trailing_slash)
    *trailing_slash = slash;

  if (*path == '/') {
    cur.kind = VFS_XV6;
    cur.ip = namei("/");
  } else if (p && p->cwd_pm) {
    cur.kind = VFS_PMFS;
    cur.pmino = p->cwd_pm;
    pmfile_pin(cur.pmino); // the process already owns a pin
  } else {
    cur.kind = VFS_XV6;
    cur.ip = p && p->cwd ? idup(p->cwd) : 0;
  }
  if (cur.kind == VFS_XV6 && cur.ip == 0)
    return -1;

  for (;;) {
    while (*path == '/')
      path++;
    if (*path == 0)
      break;
    start = path;
    while (*path && *path != '/')
      path++;
    len = path - start;
    if (len <= 0 || len >= MAXPATH)
      goto bad;
    memmove(elem, start, len);
    elem[len] = 0;
    while (*path == '/')
      path++;
    last = *path == 0;
    if (parent && last) {
      if (!vfs_isdir(&cur))
        goto bad;
      safestrcpy(name, elem, MAXPATH);
      *out = cur;
      return 0;
    }

    memset(&next, 0, sizeof(next));
    if (cur.kind == VFS_XV6) {
      // Check that the traversed native inode is live and is a directory.
      ilock(cur.ip);
      if (cur.ip->type != T_DIR || cur.ip->nlink == 0) {
        iunlock(cur.ip);
        goto bad;
      }
      if (vfs_ismount(&cur, elem)) {
        iunlock(cur.ip);
        pmfs_lock();
        pm = pmfs_mounted() ? PMFILE_ROOT_INO : 0;
        if (pm)
          pmfile_pin(pm);
        pmfs_unlock();
        if (!pm)
          goto bad;
        next.kind = VFS_PMFS;
        next.pmino = pm;
      } else {
        // Preserve xv6's historical 14-byte native name semantics.
        memset(diskname, 0, sizeof(diskname));
        memmove(diskname, elem, len < DIRSIZ ? len : DIRSIZ);
        next.ip = dirlookup(cur.ip, diskname, 0);
        iunlock(cur.ip);
        if (next.ip == 0)
          goto bad;
        next.kind = VFS_XV6;
      }
    } else {
      pmfs_lock();
      if (cur.pmino == PMFILE_ROOT_INO && elem[0] == '.' && elem[1] == '.' &&
          elem[2] == 0) {
        pm = pmfile_type_ino(cur.pmino) == PMFILE_TYPE_DIR ? -2 : -1;
      } else {
        // Returns a pinned child, including "." and an in-PMFS "..".
        pm = pmfile_lookup_ino(cur.pmino, elem);
      }
      pmfs_unlock();
      if (pm == -2) {
        next.kind = VFS_XV6;
        next.ip = namei("/");
        if (next.ip == 0)
          goto bad;
      } else if (pm > 0) {
        next.kind = VFS_PMFS;
        next.pmino = pm;
      } else {
        goto bad;
      }
    }
    vfs_put(&cur);
    cur = next;
  }
  if (parent || (slash && !vfs_isdir(&cur)))
    goto bad;
  *out = cur;
  return 0;

bad:
  vfs_put(&cur);
  return -1;
}
