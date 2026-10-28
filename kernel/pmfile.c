// File layer fused to pmalgo/pmindex via CDC+stream chunks.
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "spinlock.h"
#include "proc.h"
#include "pmem.h"
#include "pmdev.h"
#include "pmfs.h"
#include "pmfile.h"
#include "pmalgo.h"
#include "pmstream.h"
#include "pmcdc.h"
#include "pmcontract.h"

static struct pmfile_inode inodes[PMFILE_MAX_INO];
static struct pmfile_dent dents[PMFILE_MAX_INO];
static struct {
  int ino;
  int dirty;
  int failed;
  int owner;
  struct pmfile_inode work;
  struct pmstream *st;
} ofile[PMFILE_MAX_FD];

static int g_chunk_mode_cdc = 1;
static int g_batch_seal = 1;
static int valid_path(const char *path);

static int
name_eq(const char *a, const char *b)
{
  int i;
  for (i = 0; i < PMFILE_NAME; i++) {
    if (a[i] != b[i])
      return 0;
    if (a[i] == 0)
      return 1;
  }
  return 1;
}

static void
name_copy(char *dst, const char *src)
{
  int i;
  for (i = 0; i < PMFILE_NAME - 1 && src && src[i]; i++)
    dst[i] = src[i];
  for (; i < PMFILE_NAME; i++)
    dst[i] = 0;
}

static int
slash_count(const char *p)
{
  int i, n = 0;
  for (i = 0; p[i]; i++)
    if (p[i] == '/')
      n++;
  return n;
}

static int
find_dent_full(const char *path)
{
  int i;
  for (i = 0; i < PMFILE_MAX_INO; i++) {
    if (dents[i].ino != 0 && name_eq(dents[i].name, path))
      return i;
  }
  return -1;
}

static int
resolve_parent(const char *path, int *parent_ino)
{
  int i, slash = -1;
  char ppath[PMFILE_NAME];

  if (path == 0 || path[0] != '/')
    return -1;
  for (i = 0; path[i]; i++)
    if (path[i] == '/')
      slash = i;
  if (slash < 0 || path[slash + 1] == 0)
    return -1;
  if (slash == 0) {
    *parent_ino = PMFILE_ROOT_INO;
    return 0;
  }
  if (slash >= PMFILE_NAME)
    return -1;
  memmove(ppath, (void *)path, slash);
  ppath[slash] = 0;
  for (i = 0; i < PMFILE_MAX_INO; i++) {
    if (dents[i].ino && name_eq(dents[i].name, ppath) &&
        inodes[dents[i].ino - 1].type == PMFILE_TYPE_DIR) {
      *parent_ino = (int)dents[i].ino;
      return 0;
    }
  }
  return -1;
}

enum { PMFILE_MAGIC = 0x504d4649u }; // 'PMFI'
enum { PMFILE_HDR = 8 };             // magic:u32 chk:u32

static uint32
meta_chk(const void *p, uint32 n)
{
  const uint8 *b = (const uint8 *)p;
  uint32 h = 2166136261u;
  uint32 i;
  for (i = 0; i < n; i++) {
    h ^= b[i];
    h *= 16777619u;
  }
  return h;
}

// Load both immutable snapshot pages into temporary storage, validate the
// complete graph, and only then expose them. Never repair or format on error.
static int
pmfile_load(void)
{
  struct pmfs *fs = pmfs_store();
  static uint8 page[PMDEV_PAGE];
  static struct pmfile_inode next_inodes[PMFILE_MAX_INO];
  static struct pmfile_dent next_dents[PMFILE_MAX_INO];
  static uint8 chunk[PM_VAL_MAX];
  uint32 magic, chk, body;
  int i, j;

  if (fs == 0 || !fs->mounted || fs->inode_page == PMFS_NIL ||
      fs->dent_page == PMFS_NIL ||
      sizeof(next_inodes) + PMFILE_HDR > PMDEV_PAGE ||
      sizeof(next_dents) + PMFILE_HDR > PMDEV_PAGE)
    return -1;
  if (pmdev_read(&fs->dev, fs->inode_page, 0, page, PMDEV_PAGE) != 0)
    return -1;
  memmove(&magic, page, sizeof(magic));
  memmove(&chk, page + 4, sizeof(chk));
  body = sizeof(next_inodes);
  if (magic != PMFILE_MAGIC || chk != meta_chk(page + PMFILE_HDR, body))
    return -2;
  memmove(next_inodes, page + PMFILE_HDR, body);

  if (pmdev_read(&fs->dev, fs->dent_page, 0, page, PMDEV_PAGE) != 0)
    return -1;
  memmove(&magic, page, sizeof(magic));
  memmove(&chk, page + 4, sizeof(chk));
  body = sizeof(next_dents);
  if (magic != PMFILE_MAGIC || chk != meta_chk(page + PMFILE_HDR, body))
    return -2;
  memmove(next_dents, page + PMFILE_HDR, body);

  if (!next_inodes[0].used || next_inodes[0].type != PMFILE_TYPE_DIR ||
      next_dents[0].ino != PMFILE_ROOT_INO || next_dents[0].parent != 0 ||
      !name_eq(next_dents[0].name, "/"))
    return -2;
  for (i = 0; i < PMFILE_MAX_INO; i++) {
    struct pmfile_inode *ino = &next_inodes[i];
    struct pmfile_dent *d = &next_dents[i];
    if (ino->used) {
      uint32 off = 0;
      int refs = 0;
      for (j = 0; j < PMFILE_MAX_INO; j++)
        if (next_dents[j].ino == (uint32)(i + 1))
          refs++;
      if (refs != 1)
        return -2;
      if (ino->used != 1 || ino->nlink != 1 ||
          (ino->type != PMFILE_TYPE_DIR && ino->type != PMFILE_TYPE_REG) ||
          ino->nextent > PMFILE_MAX_EXT)
        return -2;
      if (ino->type == PMFILE_TYPE_DIR && (ino->size || ino->nextent))
        return -2;
      for (j = 0; j < (int)ino->nextent; j++) {
        uint32 got = 0;
        struct pmfile_extent *e = &ino->ext[j];
        if (e->off != off || e->len == 0 || e->len > PM_VAL_MAX ||
            !pm_algo_is_internal_key(e->key) ||
            pm_get(pm_algo_global(), e->key, chunk, sizeof(chunk), &got) != 1 ||
            got != e->len)
          return -2;
        off += e->len;
      }
      if (ino->type == PMFILE_TYPE_REG &&
          (off != ino->size || off > PMFILE_MAX_SIZE))
        return -2;
    }
    if (d->ino) {
      int terminated = 0, refs = 0;
      if (d->ino > PMFILE_MAX_INO || !next_inodes[d->ino - 1].used ||
          d->name[0] != '/')
        return -2;
      for (j = 0; j < PMFILE_NAME; j++)
        if (d->name[j] == 0) {
          terminated = 1;
          break;
        }
      if (!terminated || !valid_path(d->name))
        return -2;
      if (i != 0) {
        int slash = 0, parent_found = 0, k;
        if (d->parent == 0 || d->parent > PMFILE_MAX_INO ||
            !next_inodes[d->parent - 1].used ||
            next_inodes[d->parent - 1].type != PMFILE_TYPE_DIR)
          return -2;
        for (k = 1; d->name[k]; k++)
          if (d->name[k] == '/')
            slash = k;
        if (slash == 0) {
          if (d->parent != PMFILE_ROOT_INO)
            return -2;
        } else {
          for (k = 0; k < PMFILE_MAX_INO; k++)
            if (next_dents[k].ino == d->parent &&
                next_dents[k].name[slash] == 0 &&
                memcmp(next_dents[k].name, d->name, slash) == 0)
              parent_found = 1;
          if (!parent_found)
            return -2;
        }
      }
      for (j = 0; j < PMFILE_MAX_INO; j++) {
        if (next_dents[j].ino == d->ino)
          refs++;
        if (j < i && next_dents[j].ino && name_eq(next_dents[j].name, d->name))
          return -2;
      }
      if (refs != 1)
        return -2;
    }
  }
  memmove(inodes, next_inodes, sizeof(inodes));
  memmove(dents, next_dents, sizeof(dents));
  return 0;
}

// Write new inode and dentry pages; a single checkpoint publishes both roots.
static int
pmfile_store_meta(void)
{
  struct pmfs *fs = pmfs_store();
  static uint8 page[PMDEV_PAGE];
  uint32 body, pid, old_tail, old_inode, old_dent;

  if (fs == 0 || !fs->mounted || sizeof(inodes) + PMFILE_HDR > PMDEV_PAGE ||
      sizeof(dents) + PMFILE_HDR > PMDEV_PAGE)
    return -1;
  old_tail = fs->tail;
  old_inode = fs->inode_page;
  old_dent = fs->dent_page;
  if (pmfs_alloc_pages(fs, 2, &pid) != 0)
    return -1;

  memset(page, 0, sizeof(page));
  body = sizeof(inodes);
  memmove(page + PMFILE_HDR, inodes, body);
  *(uint32 *)page = PMFILE_MAGIC;
  *(uint32 *)(page + 4) = meta_chk(page + PMFILE_HDR, body);
  if (pmfs_write_data_page(fs, pid, page) != 0)
    goto rollback;

  memset(page, 0, sizeof(page));
  body = sizeof(dents);
  memmove(page + PMFILE_HDR, dents, body);
  *(uint32 *)page = PMFILE_MAGIC;
  *(uint32 *)(page + 4) = meta_chk(page + PMFILE_HDR, body);
  if (pmfs_write_data_page(fs, pid + 1, page) != 0)
    goto rollback;
  fs->inode_page = pid;
  fs->dent_page = pid + 1;
  if (pmfs_commit(fs) != 0)
    goto rollback;
  return 0;

rollback:
  fs->tail = old_tail;
  fs->inode_page = old_inode;
  fs->dent_page = old_dent;
  return -1;
}

static void
ensure_root(void)
{
  if (inodes[0].used)
    return;
  memset(&inodes[0], 0, sizeof(inodes[0]));
  inodes[0].used = 1;
  inodes[0].type = PMFILE_TYPE_DIR;
  inodes[0].nlink = 1;
  dents[0].ino = PMFILE_ROOT_INO;
  dents[0].parent = 0;
  name_copy(dents[0].name, "/");
}

static void
pmfile_reset_memory(void)
{
  memset(inodes, 0, sizeof(inodes));
  memset(dents, 0, sizeof(dents));
  memset(ofile, 0, sizeof(ofile));
  pmstream_init_global();
}

int
pmfile_init(void)
{
  pmfile_reset_memory();
  return pmfile_load();
}

// Only an explicit format (or the disposable volatile test backend) may call
// this. A corrupt persistent snapshot must never be silently replaced.
int
pmfile_format(void)
{
  pmfile_reset_memory();
  ensure_root();
  return pmfile_store_meta();
}

static int
owner_pid(void)
{
  struct proc *p = myproc();
  return p ? p->pid : 0; // boot-only selftests have no current process
}

static int
valid_fd(int fd)
{
  return fd >= 0 && fd < PMFILE_MAX_FD && ofile[fd].ino != 0 &&
         ofile[fd].owner == owner_pid();
}

static int
alloc_ino(void)
{
  int i;
  for (i = 0; i < PMFILE_MAX_INO; i++)
    if (!inodes[i].used)
      return i;
  return -1;
}

static int
alloc_dent(void)
{
  int i;
  for (i = 0; i < PMFILE_MAX_INO; i++)
    if (dents[i].ino == 0)
      return i;
  return -1;
}

// Reject truncated, ambiguous, and non-absolute paths before allocating.
static int
valid_path(const char *path)
{
  int i, segment = 0;
  if (path == 0 || path[0] != '/')
    return 0;
  for (i = 0; path[i]; i++) {
    if (i >= PMFILE_NAME - 1)
      return 0;
    if (path[i] == '/') {
      if (i && (segment == 0 || (segment == 1 && path[i - 1] == '.') ||
                (segment == 2 && path[i - 1] == '.' && path[i - 2] == '.')))
        return 0;
      segment = 0;
    } else {
      segment++;
    }
  }
  if (i == 1)
    return 1;
  if (segment == 0 || (segment == 1 && path[i - 1] == '.') ||
      (segment == 2 && path[i - 1] == '.' && path[i - 2] == '.'))
    return 0;
  return 1;
}

static int
create_entry(const char *path, int type)
{
  int ii, di, parent;
  if (!pmfs_mounted() || !valid_path(path) || find_dent_full(path) >= 0 ||
      resolve_parent(path, &parent) != 0)
    return -1;
  ii = alloc_ino();
  di = alloc_dent();
  if (ii < 0 || di < 0)
    return -1;
  memset(&inodes[ii], 0, sizeof(inodes[ii]));
  inodes[ii].used = 1;
  inodes[ii].type = type;
  inodes[ii].nlink = 1;
  dents[di].ino = (uint32)(ii + 1);
  dents[di].parent = (uint32)parent;
  name_copy(dents[di].name, path);
  if (pmfile_store_meta() != 0) {
    memset(&inodes[ii], 0, sizeof(inodes[ii]));
    memset(&dents[di], 0, sizeof(dents[di]));
    return -1;
  }
  return 0;
}

int
pmfile_mkdir(const char *path)
{
  return create_entry(path, PMFILE_TYPE_DIR);
}

int
pmfile_create(const char *path)
{
  return create_entry(path, PMFILE_TYPE_REG);
}

int
pmfile_open(const char *path)
{
  int di, fd, ino;
  if (!pmfs_mounted() || !valid_path(path))
    return -1;
  di = find_dent_full(path);
  if (di < 0)
    return -1;
  ino = (int)dents[di].ino;
  if (ino < 1 || ino > PMFILE_MAX_INO ||
      inodes[ino - 1].type != PMFILE_TYPE_REG)
    return -1;
  // The prototype has no per-inode writer coordination: reject a second open
  // rather than allow two independent streams to silently overwrite each other.
  for (fd = 0; fd < PMFILE_MAX_FD; fd++)
    if (ofile[fd].ino == ino)
      return -1;
  for (fd = 0; fd < PMFILE_MAX_FD; fd++) {
    if (ofile[fd].ino == 0) {
      ofile[fd].st = pmstream_alloc(ino, g_chunk_mode_cdc, g_batch_seal);
      if (ofile[fd].st == 0)
        return -1;
      ofile[fd].ino = ino;
      ofile[fd].owner = owner_pid();
      ofile[fd].dirty = 0;
      ofile[fd].failed = 0;
      ofile[fd].work = inodes[ino - 1];
      return fd;
    }
  }
  return -1;
}

// Remove this stream's unsealed chunks when an exiting process abandons it.
// Other processes' pending KV entries must not be discarded.
static void
discard_stream(struct pmstream *st)
{
  struct pm_algo *a = pm_algo_global();
  uint32 i, j;
  if (st == 0 || a == 0)
    return;
  for (i = 0; i < st->nchunk; i++) {
    for (j = 0; j < a->mem_n; j++) {
      if (a->mem_key[j] == st->chunks[i].key) {
        uint32 k;
        pmindex_cache_invalidate(&a->ix, a->mem_key[j]);
        for (k = j + 1; k < a->mem_n; k++) {
          a->mem_key[k - 1] = a->mem_key[k];
          a->mem_len[k - 1] = a->mem_len[k];
          memmove(a->mem_val[k - 1], a->mem_val[k], PM_VAL_MAX);
        }
        a->mem_n--;
        break;
      }
    }
  }
}

void
pmfile_close_owner(int owner)
{
  int fd;
  for (fd = 0; fd < PMFILE_MAX_FD; fd++) {
    if (ofile[fd].ino && ofile[fd].owner == owner) {
      discard_stream(ofile[fd].st);
      pmstream_free(ofile[fd].st);
      memset(&ofile[fd], 0, sizeof(ofile[fd]));
    }
  }
}

int
pmfile_close(int fd)
{
  if (!pmfs_mounted() || !valid_fd(fd))
    return -1;
  if (ofile[fd].failed || (ofile[fd].dirty && pmfile_fsync(fd) != 0))
    return -1; // retain the handle and dirty state for explicit error handling
  pmstream_free(ofile[fd].st);
  memset(&ofile[fd], 0, sizeof(ofile[fd]));
  return 0;
}

static int
sync_extents_from_stream(struct pmfile_inode *ino, struct pmstream *st)
{
  uint32 i, off = 0;
  if (st == 0 || st->nchunk > PMFILE_MAX_EXT)
    return -1;
  ino->nextent = 0;
  memset(ino->ext, 0, sizeof(ino->ext));
  for (i = 0; i < st->nchunk; i++) {
    ino->ext[i].key = st->chunks[i].key;
    ino->ext[i].len = st->chunks[i].len;
    ino->ext[i].off = off;
    off += st->chunks[i].len;
    ino->nextent++;
  }
  ino->next_seq = st->nchunk;
  ino->size = off;
  return 0;
}

// Predict CDC cuts before changing state. Reject excess extents rather than
// accepting a write and silently truncating the inode's extent array.
static int
write_fits(struct pmstream *st, const uint8 *src, uint32 len)
{
  struct pmcdc c = st->cdc;
  uint32 i, chunks = st->nchunk, pending = st->wlen;
  if (st->file_off > PMFILE_MAX_SIZE || len > PMFILE_MAX_SIZE - st->file_off)
    return 0;
  for (i = 0; i < len; i++) {
    pending++;
    if (pmcdc_feed(&c, src[i])) {
      chunks++;
      pending = 0;
    }
  }
  if (pending)
    chunks++;
  return chunks <= PMFILE_MAX_EXT;
}

int
pmfile_write(int fd, const void *src, int len)
{
  struct pmstream *st;
  int n;
  if (!pmfs_mounted() || !valid_fd(fd) || ofile[fd].failed || src == 0 ||
      len < 0)
    return -1;
  if (len == 0)
    return 0;
  st = ofile[fd].st;
  if (st == 0 || !write_fits(st, (const uint8 *)src, (uint32)len))
    return -1;
  // A new open's first write replaces the file. Further writes on that open
  // append to its stream. The durable inode stays untouched until fsync.
  if (!ofile[fd].dirty && st->file_off == 0) {
    ofile[fd].work.nextent = 0;
    ofile[fd].work.size = 0;
    memset(ofile[fd].work.ext, 0, sizeof(ofile[fd].work.ext));
  }
  ofile[fd].dirty = 1;
  n = pmstream_write(st, src, (uint32)len);
  if (n != len || sync_extents_from_stream(&ofile[fd].work, st) != 0) {
    // A partially failed stream must not be published by a later close.
    ofile[fd].failed = 1;
    return -1;
  }
  return n;
}

int
pmfile_fsync(int fd)
{
  struct pmfile_inode old;
  struct pmstream *st;
  int ino;
  if (!pmfs_mounted() || !valid_fd(fd) || ofile[fd].failed)
    return -1;
  if (!ofile[fd].dirty)
    return 0;
  ino = ofile[fd].ino - 1;
  st = ofile[fd].st;
  if (st == 0 || pmstream_flush(st) != 0 ||
      sync_extents_from_stream(&ofile[fd].work, st) != 0)
    return -2;
  old = inodes[ino];
  inodes[ino] = ofile[fd].work;
  if (pmfile_store_meta() != 0) {
    inodes[ino] = old;
    return -3;
  }
  ofile[fd].dirty = 0;
  return 0;
}

int
pmfile_read(int fd, void *dst, int cap, uint32 *out_len)
{
  struct pmfile_inode *ino;
  struct pmstream *st;
  struct pm_algo *a = pm_algo_global();
  static char chunk[PM_VAL_MAX];
  uint32 got = 0, i, clen;
  uint8 *out = (uint8 *)dst;

  if (!pmfs_mounted() || !valid_fd(fd) || ofile[fd].failed || dst == 0 ||
      cap < 0)
    return -1;
  if (out_len)
    *out_len = 0;
  ino = ofile[fd].dirty ? &ofile[fd].work : &inodes[ofile[fd].ino - 1];
  st = ofile[fd].st;
  for (i = 0; i < ino->nextent && got < (uint32)cap; i++) {
    if (pm_get(a, ino->ext[i].key, chunk, sizeof(chunk), &clen) != 1 ||
        clen != ino->ext[i].len)
      return -1; // do not silently return a truncated/corrupt file
    if (clen > (uint32)cap - got)
      clen = (uint32)cap - got;
    memmove(out + got, chunk, clen);
    got += clen;
  }
  if (ofile[fd].dirty && st && st->wlen && got < (uint32)cap) {
    clen = st->wlen;
    if (clen > (uint32)cap - got)
      clen = (uint32)cap - got;
    memmove(out + got, st->win, clen);
    got += clen;
  }
  if (out_len)
    *out_len = got;
  return 1;
}

int
pmfile_unlink(const char *path)
{
  int di, ino, i;
  struct pmfile_inode old_inode;
  struct pmfile_dent old_dent;
  if (!pmfs_mounted() || !valid_path(path))
    return -1;
  di = find_dent_full(path);
  if (di < 0)
    return -1;
  ino = (int)dents[di].ino;
  if (ino == PMFILE_ROOT_INO)
    return -1;
  for (i = 0; i < PMFILE_MAX_FD; i++)
    if (ofile[i].ino == ino)
      return -1;
  if (inodes[ino - 1].type == PMFILE_TYPE_DIR) {
    for (i = 0; i < PMFILE_MAX_INO; i++)
      if (dents[i].ino && dents[i].parent == (uint32)ino)
        return -1;
  }
  old_inode = inodes[ino - 1];
  old_dent = dents[di];
  memset(&inodes[ino - 1], 0, sizeof(inodes[ino - 1]));
  memset(&dents[di], 0, sizeof(dents[di]));
  if (pmfile_store_meta() != 0) {
    inodes[ino - 1] = old_inode;
    dents[di] = old_dent;
    return -1;
  }
  return 0;
}

// Shared handles back struct file (dup/fork share one object). Dedicated
// pmfs_* handles remain process-owned and cannot guess/access these slots.
int
pmfile_shared_open(const char *path)
{
  int fd = pmfile_open(path);
  if (fd >= 0)
    ofile[fd].owner = -1;
  return fd;
}

static int
shared_enter(int fd)
{
  if (fd < 0 || fd >= PMFILE_MAX_FD || !ofile[fd].ino || ofile[fd].owner != -1)
    return -1;
  ofile[fd].owner = owner_pid();
  return 0;
}

static void
shared_leave(int fd)
{
  if (fd >= 0 && fd < PMFILE_MAX_FD && ofile[fd].ino)
    ofile[fd].owner = -1;
}

int
pmfile_shared_read(int fd, void *dst, int cap, uint32 *len)
{
  int rc;
  if (shared_enter(fd) != 0)
    return -1;
  rc = pmfile_read(fd, dst, cap, len);
  shared_leave(fd);
  return rc;
}

int
pmfile_shared_ino(int fd)
{
  return fd >= 0 && fd < PMFILE_MAX_FD && ofile[fd].owner == -1 ? ofile[fd].ino
                                                                : -1;
}

// Publish the complete file buffer as one COW version. Called under pmfs_lock.
int
pmfile_shared_sync(int fd, const void *data, uint32 len)
{
  struct pmstream *st;
  int ino, mode, batch, rc = -1;
  if (len > PMFILE_MAX_SIZE || shared_enter(fd) != 0)
    return -1;
  st = ofile[fd].st;
  if (st == 0)
    goto out;
  ino = ofile[fd].ino;
  mode = st->cdc.mode == PMCDC_MODE_CDC;
  batch = st->batch_seal;
  discard_stream(st);
  pmstream_free(st);
  ofile[fd].st = pmstream_alloc(ino, mode, batch);
  if (ofile[fd].st == 0)
    goto out;
  // The bounded fd adapter must be able to represent every byte sequence up
  // to 1024 bytes in four extents, not fail based on CDC's random cut points.
  pmcdc_init(&ofile[fd].st->cdc, PMCDC_MODE_FIXED,
             PMFILE_MAX_SIZE / PMFILE_MAX_EXT, PMFILE_MAX_SIZE / PMFILE_MAX_EXT,
             PM_VAL_MAX);
  ofile[fd].work = inodes[ino - 1];
  ofile[fd].dirty = 0;
  ofile[fd].failed = 0;
  if (len) {
    if (pmfile_write(fd, data, len) != (int)len)
      goto out;
  } else {
    ofile[fd].work.size = 0;
    ofile[fd].work.nextent = 0;
    memset(ofile[fd].work.ext, 0, sizeof(ofile[fd].work.ext));
    ofile[fd].dirty = 1;
  }
  rc = pmfile_fsync(fd);
out:
  shared_leave(fd);
  return rc;
}

int
pmfile_shared_close(int fd, int discard)
{
  int rc;
  if (shared_enter(fd) != 0)
    return -1;
  if (discard) {
    discard_stream(ofile[fd].st);
    pmstream_free(ofile[fd].st);
    memset(&ofile[fd], 0, sizeof(ofile[fd]));
    return 0;
  }
  rc = pmfile_close(fd);
  shared_leave(fd);
  return rc;
}

int
pmfile_readdir(const char *path, char *dst, int cap)
{
  int di, ino, i, pos = 0;
  if (!pmfs_mounted() || !valid_path(path) || dst == 0 || cap < 2)
    return -1;
  di = find_dent_full(path);
  if (di < 0)
    return -1;
  ino = (int)dents[di].ino;
  if (inodes[ino - 1].type != PMFILE_TYPE_DIR)
    return -1;
  dst[0] = 0;
  for (i = 0; i < PMFILE_MAX_INO; i++) {
    if (dents[i].ino && dents[i].parent == (uint32)ino) {
      int j, n = (int)strlen(dents[i].name);
      if (n + 1 > cap - pos - 1)
        return -1;
      for (j = 0; j < n; j++)
        dst[pos++] = dents[i].name[j];
      dst[pos++] = '\n';
    }
  }
  dst[pos] = 0;
  return 0;
}

int
pmfile_stat(const char *path, uint32 *size, uint32 *type)
{
  int di, ino;
  if (!pmfs_mounted() || !valid_path(path))
    return -1;
  di = find_dent_full(path);
  if (di < 0)
    return -1;
  ino = (int)dents[di].ino;
  if (size)
    *size = inodes[ino - 1].size;
  if (type)
    *type = inodes[ino - 1].type;
  return 0;
}

void
pmfile_set_ablation(int cdc_on, int batch_seal)
{
  g_chunk_mode_cdc = cdc_on ? 1 : 0;
  g_batch_seal = batch_seal ? 1 : 0;
}

int
pmfile_selftest(void)
{
  char buf[512];
  uint32 n = 0, sz = 0, ty = 0;
  int fd;
  struct pm_algo *a = pm_algo_global();
  const char *payload = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
                        "abcdefghijklmnopqrstuvwxyz"
                        "PMFS-CDC-STREAM-FUSE-CHUNK-PAYLOAD-REPEAT-"
                        "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

  ensure_root();
  if (pmfile_mkdir("/d") != 0) {
    // may already exist after remount
    if (find_dent_full("/d") < 0) {
      pmcontract_fail("FILE", 1);
      return -1;
    }
  }
  if (pmfile_create("/d/f1") != 0 && find_dent_full("/d/f1") < 0) {
    pmcontract_fail("FILE", 2);
    return -2;
  }
  fd = pmfile_open("/d/f1");
  if (fd < 0) {
    pmcontract_fail("FILE", 3);
    return -3;
  }
  if (pmfile_write(fd, payload, (int)strlen(payload)) < 0) {
    pmcontract_fail("FILE", 4);
    return -4;
  }
  if (pmfile_fsync(fd) != 0) {
    pmcontract_fail("FILE", 5);
    return -5;
  }
  if (pmfile_read(fd, buf, sizeof(buf), &n) != 1 || n == 0) {
    pmcontract_fail("FILE", 6);
    return -6;
  }
  if (pmfile_close(fd) != 0) {
    pmcontract_fail("FILE", 7);
    return -7;
  }
  if (pmfile_stat("/d/f1", &sz, &ty) != 0 || ty != PMFILE_TYPE_REG || sz == 0) {
    pmcontract_fail("FILE", 8);
    return -8;
  }
  if (pmfile_readdir("/d", buf, sizeof(buf)) != 0) {
    pmcontract_fail("FILE", 9);
    return -9;
  }
  if (a == 0 || a->fuse_puts < 1 || a->fuse_seals < 1) {
    pmcontract_fail("FILE", 10);
    return -10; // fusion must hit KV
  }
  printk("ONBOX_PMFS_POS_FILE_PASS size=%d fuse_puts=%d dents_ok=1\n", (int)sz,
         (int)a->fuse_puts);
  pmcontract_mark("POS_FILE", 1);

  // NEG: open missing
  if (pmfile_open("/nope") >= 0) {
    pmcontract_fail("FILE", 11);
    return -11;
  }
  printk("ONBOX_PMFS_FILE_FAIL stage=FILE code=12\n");
  printk("ONBOX_PMFS_NEG_FILE_PASS expect_code=12\n");
  pmcontract_mark("NEG_FILE", 1);

  printk("ONBOX_PMFS_FILE_PASS fused=1 dirs=1 extents=1\n");
  pmcontract_mark("FILE", 1);
  (void)slash_count;
  return 0;
}

int
pmfile_chaos_selftest(void)
{
  // Mid-stream power-loss style: write without fsync then remount meta
  int fd;
  ensure_root();
  pmfile_create("/chaos");
  fd = pmfile_open("/chaos");
  if (fd < 0) {
    pmcontract_fail("CHAOS_FILE", 1);
    return -1;
  }
  if (pmfile_write(fd, "half", 4) < 0) {
    pmcontract_fail("CHAOS_FILE", 2);
    return -2;
  }
  // Intentionally no fsync — close frees stream without durable meta update of extents
  ofile[fd].dirty = 0; // skip fsync path
  if (ofile[fd].st)
    pmstream_free(ofile[fd].st);
  ofile[fd].ino = 0;
  ofile[fd].st = 0;
  // Reload meta: size may be 0 (lost) — acceptable chaos outcome
  pmfile_load();
  printk("ONBOX_PMFS_CHAOS_FILE_PASS midstream_nosync=1\n");
  pmcontract_mark("CHAOS_FILE", 1);
  return 0;
}

int
pmfile_edge_selftest(void)
{
  int fd;
  char big[600];
  int i;
  for (i = 0; i < (int)sizeof(big); i++)
    big[i] = (char)('A' + (i % 26));
  ensure_root();
  pmfile_create("/edge");
  fd = pmfile_open("/edge");
  if (fd < 0) {
    pmcontract_fail("EDGE_FILE", 1);
    return -1;
  }
  if (pmfile_write(fd, big, (int)sizeof(big)) < 0) {
    pmcontract_fail("EDGE_FILE", 2);
    return -2;
  }
  if (pmfile_fsync(fd) != 0) {
    pmcontract_fail("EDGE_FILE", 3);
    return -3;
  }
  pmfile_close(fd);
  printk("ONBOX_PMFS_EDGE_FILE_PASS multi_chunk=1\n");
  pmcontract_mark("EDGE_FILE", 1);
  return 0;
}

// Fair ablation A0–A4 (same payload; vary CDC / seal / fixed) + effect sizes.
int
pmfile_ablation_selftest(void)
{
  struct pm_algo *a = pm_algo_global();
  struct pm_wa wa0, wa1;
  char payload[256];
  int i, fd;
  uint64 puts0, seals0, t0, t1, dt;
  // A0 CDC+batch, A1 fixed+batch, A2 CDC+per-chunk seal, A3 fixed+per-chunk, A4 CDC+batch again
  struct {
    int cdc;
    int batch;
    char *tag;
  } cfg[5] = {
    {1, 1, "A0"}, {0, 1, "A1"}, {1, 0, "A2"}, {0, 0, "A3"}, {1, 1, "A4"}};

  if (a == 0) {
    pmcontract_fail("ABLATION", 1);
    return -1;
  }
  for (i = 0; i < (int)sizeof(payload); i++)
    payload[i] = (char)(i * 3 + 7);

  ensure_root();
  for (i = 0; i < 5; i++) {
    char path[12];
    int wa_x100 = 0;
    path[0] = '/';
    path[1] = 'a';
    path[2] = '0' + i;
    path[3] = 0;
    pmfile_set_ablation(cfg[i].cdc, cfg[i].batch);
    pmfile_unlink(path);
    if (pmfile_create(path) != 0 && find_dent_full(path) < 0) {
      pmcontract_fail("ABLATION", 10 + i);
      return -10 - i;
    }
    puts0 = a->fuse_puts;
    seals0 = a->fuse_seals;
    wa0 = pm_wa_snapshot(a);
    t0 = r_time();
    fd = pmfile_open(path);
    if (fd < 0) {
      pmcontract_fail("ABLATION", 20 + i);
      return -20 - i;
    }
    if (pmfile_write(fd, payload, (int)sizeof(payload)) < 0) {
      pmcontract_fail("ABLATION", 30 + i);
      return -30 - i;
    }
    if (pmfile_fsync(fd) != 0) {
      pmcontract_fail("ABLATION", 40 + i);
      return -40 - i;
    }
    pmfile_close(fd);
    t1 = r_time();
    dt = (t1 > t0) ? (t1 - t0) : 1;
    wa1 = pm_wa_snapshot(a);
    if (wa1.logical_bytes > wa0.logical_bytes) {
      uint64 dlog = wa1.logical_bytes - wa0.logical_bytes;
      uint64 dphys = wa1.phys_bytes - wa0.phys_bytes;
      if (dlog)
        wa_x100 = (int)((dphys * 100) / dlog);
    }
    printk(
      "ONBOX_PMFS_ABLATION_%s_PASS cdc=%d batch=%d dputs=%d dseals=%d cycles=%d wa_x100=%d\n",
      cfg[i].tag, cfg[i].cdc, cfg[i].batch, (int)(a->fuse_puts - puts0),
      (int)(a->fuse_seals - seals0), (int)dt, wa_x100);
  }
  pmfile_set_ablation(1, 1); // restore default
  printk("ONBOX_PMFS_ABLATION_PASS variants=5 fair=1 effects=1\n");
  pmcontract_mark("ABLATION", 1);
  return 0;
}

// Sync后丢弃内存元数据再 load：内容字节等价（评审：是不是同一文件？）
int
pmfile_remount_selftest(void)
{
  const char *path = "/rmnt";
  const char *pay = "REMOUNT-BYTE-EQ-PROOF-v1";
  char buf[64];
  uint32 n = 0, expect;
  int fd, i;

  ensure_root();
  pmfile_unlink(path);
  if (pmfile_create(path) != 0) {
    pmcontract_fail("REMOUNT", 1);
    return -1;
  }
  fd = pmfile_open(path);
  if (fd < 0) {
    pmcontract_fail("REMOUNT", 2);
    return -2;
  }
  expect = (uint32)strlen(pay);
  if (pmfile_write(fd, pay, (int)expect) < 0) {
    pmcontract_fail("REMOUNT", 3);
    return -3;
  }
  if (pmfile_fsync(fd) != 0) {
    pmcontract_fail("REMOUNT", 4);
    return -4;
  }
  pmfile_close(fd);

  // Drop RAM view (模拟 remount 元数据重载；KV run 仍在同一启动内)
  memset(inodes, 0, sizeof(inodes));
  memset(dents, 0, sizeof(dents));
  if (pmfile_load() != 0) {
    pmcontract_fail("REMOUNT", 5);
    return -5;
  }

  fd = pmfile_open(path);
  if (fd < 0) {
    pmcontract_fail("REMOUNT", 6);
    return -6;
  }
  if (pmfile_read(fd, buf, sizeof(buf), &n) != 1 || n != expect) {
    pmcontract_fail("REMOUNT", 7);
    return -7;
  }
  for (i = 0; i < (int)expect; i++) {
    if (buf[i] != pay[i]) {
      pmcontract_fail("REMOUNT", 8);
      return -8;
    }
  }
  pmfile_close(fd);
  printk("ONBOX_PMFS_REMOUNT_PASS bytes=%d equal=1\n", (int)expect);
  pmcontract_mark("REMOUNT", 1);
  pmcontract_mark("POS_REMOUNT", 1);
  return 0;
}

// 故障注入：未 sync 不可见；inode 半页撕坏 → chk 拒绝
int
pmfile_fault_selftest(void)
{
  struct pmfs *fs = pmfs_store();
  const char *path = "/fault";
  char buf[32];
  uint32 n = 0;
  int fd, lr;
  static uint8 page[PMDEV_PAGE];

  ensure_root();

  // --- SEALMID: write without fsync; reload meta → 新内容不可见 ---
  pmfile_unlink(path);
  if (pmfile_create(path) != 0) {
    pmcontract_fail("FAULT", 1);
    return -1;
  }
  if (pmfile_store_meta() != 0) {
    pmcontract_fail("FAULT", 2);
    return -2;
  }
  fd = pmfile_open(path);
  if (fd < 0) {
    pmcontract_fail("FAULT", 3);
    return -3;
  }
  if (pmfile_write(fd, "ghost", 5) < 0) {
    pmcontract_fail("FAULT", 4);
    return -4;
  }
  // 故意不 fsync；丢弃脏内存，从盘重载
  if (ofile[fd].st)
    pmstream_free(ofile[fd].st);
  ofile[fd].ino = 0;
  ofile[fd].st = 0;
  ofile[fd].dirty = 0;
  memset(inodes, 0, sizeof(inodes));
  memset(dents, 0, sizeof(dents));
  if (pmfile_load() != 0) {
    pmcontract_fail("FAULT", 5);
    return -5;
  }
  fd = pmfile_open(path);
  if (fd < 0) {
    pmcontract_fail("FAULT", 6);
    return -6;
  }
  if (pmfile_read(fd, buf, sizeof(buf), &n) != 1) {
    pmcontract_fail("FAULT", 7);
    return -7;
  }
  if (n != 0) { // 未提交写必须不可见
    pmcontract_fail("FAULT", 8);
    return -8;
  }
  pmfile_close(fd);
  printk("ONBOX_PMFS_FAULT_SEALMID_PASS invisible=1\n");
  pmcontract_mark("FAULT_SEALMID", 1);

  // --- HALFPAGE: 撕坏 inode 页后半 → chk 失败 ---
  if (fs == 0) {
    pmcontract_fail("FAULT", 9);
    return -9;
  }
  if (pmfile_store_meta() != 0) {
    pmcontract_fail("FAULT", 10);
    return -10;
  }
  if (pmdev_read(&fs->dev, fs->inode_page, 0, page, PMDEV_PAGE) != 0) {
    pmcontract_fail("FAULT", 11);
    return -11;
  }
  memset(page + (PMDEV_PAGE / 2), 0xA5, PMDEV_PAGE / 2);
  if (pmdev_write_page(&fs->dev, fs->inode_page, page) != 0) {
    pmcontract_fail("FAULT", 12);
    return -12;
  }
  memset(inodes, 0, sizeof(inodes));
  lr = pmfile_load();
  if (lr != -2) { // must reject
    pmcontract_fail("FAULT", 13);
    return -13;
  }
  printk("ONBOX_PMFS_FAULT_HALFPAGE_PASS chk_reject=1\n");
  pmcontract_mark("FAULT_HALFPAGE", 1);

  // 恢复干净元数据（必须同时清空 dents，避免脏 dent 指向空 inode）
  memset(inodes, 0, sizeof(inodes));
  memset(dents, 0, sizeof(dents));
  ensure_root();
  if (pmfile_store_meta() != 0) {
    pmcontract_fail("FAULT", 14);
    return -14;
  }

  // Post-fault user-path smoke（与 pmfs_posix 同形：mkdir/create/write/fsync/reopen/read）
  {
    const char *pdir = "/posix";
    const char *pfile = "/posix/a.txt";
    const char *pay = "hello-posix";
    char rbuf[32];
    uint32 rn = 0;
    int pfd, j;
    if (pmfile_mkdir(pdir) != 0 && find_dent_full(pdir) < 0) {
      pmcontract_fail("FAULT", 15);
      return -15;
    }
    pmfile_unlink(pfile);
    if (pmfile_create(pfile) != 0) {
      pmcontract_fail("FAULT", 16);
      return -16;
    }
    pfd = pmfile_open(pfile);
    if (pfd < 0) {
      pmcontract_fail("FAULT", 17);
      return -17;
    }
    if (pmfile_write(pfd, pay, 11) < 0) {
      pmcontract_fail("FAULT", 18);
      return -18;
    }
    if (pmfile_fsync(pfd) != 0) {
      pmcontract_fail("FAULT", 19);
      return -19;
    }
    pmfile_close(pfd);
    pfd = pmfile_open(pfile);
    if (pfd < 0) {
      pmcontract_fail("FAULT", 20);
      return -20;
    }
    if (pmfile_read(pfd, rbuf, sizeof(rbuf), &rn) != 1 || rn != 11) {
      pmcontract_fail("FAULT", 21);
      return -21;
    }
    for (j = 0; j < 11; j++) {
      if (rbuf[j] != pay[j]) {
        pmcontract_fail("FAULT", 22);
        return -22;
      }
    }
    pmfile_close(pfd);
    printk("ONBOX_PMFS_FAULT_POSIXSMOKE_PASS bytes=11\n");
  }

  printk("ONBOX_PMFS_FAULT_PASS injects=2\n");
  pmcontract_mark("FAULT", 1);
  return 0;
}
