// Small VFS namespace layer and a backend-independent directory entry ABI.
#ifndef XV6_VFS_H
#define XV6_VFS_H

#define VFS_NAME_MAX 27

struct vfs_dirent {
  uint ino;
  short type;
  char name[VFS_NAME_MAX + 1];
};

struct inode;
struct vnode {
  int kind;
  struct inode *ip;
  int pmino;
};

enum { VFS_NONE, VFS_XV6, VFS_PMFS };

// Call inside an xv6 log transaction: native vnode release can reclaim an inode.
int vfs_walk(const char *path, int parent, struct vnode *out, char *name,
             int *trailing_slash);
void vfs_put(struct vnode *node);
int vfs_isdir(struct vnode *node);
int vfs_ismount(struct vnode *parent, const char *name);

#endif
