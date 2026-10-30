struct pm_shared_file;

struct file {
  enum { FD_NONE, FD_PIPE, FD_INODE, FD_DEVICE, FD_PMFS, FD_PMFS_DIR } type;
  int ref; // reference count
  char readable;
  char writable;
  char append;       // choose end of file at each write while holding its lock
  struct pipe *pipe; // FD_PIPE
  struct inode *ip;  // FD_INODE and FD_DEVICE
  uint off;          // FD_INODE
  short major;       // FD_DEVICE
  int pmino;
  struct pm_shared_file *pm; // coherent data shared by independent PMFS opens
};

#define major(dev)  ((dev) >> 16 & 0xFFFF)
#define minor(dev)  ((dev) & 0xFFFF)
#define mkdev(m, n) ((uint)((m) << 16 | (n)))

// in-memory copy of an inode
struct inode {
  uint dev;                // Device number
  uint inum;               // Inode number
  int ref;                 // Reference count
  struct sleeplock iolock; // serializes whole ordinary file I/O syscalls
  struct sleeplock lock;   // protects everything below here
  int valid;               // inode has been read from disk?

  short type; // copy of disk inode
  short major;
  short minor;
  short nlink;
  uint size;
  uint addrs[NDIRECT + 1];
};

// map major device number to device functions.
struct devsw {
  int (*read)(int, uint64, int);
  int (*write)(int, uint64, int);
};

extern struct devsw devsw[];

#define CONSOLE 1
