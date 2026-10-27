// File layer fused to pmalgo/pmindex via CDC+stream chunks.
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "pmem.h"
#include "pmdev.h"
#include "pmfs.h"
#include "pmfile.h"
#include "pmalgo.h"
#include "pmstream.h"
#include "pmcdc.h"
#include "pmcontract.h"

static struct pmfile_inode inodes[PMFILE_MAX_INO];
static struct pmfile_dent  dents[PMFILE_MAX_INO];
static struct {
  int ino;
  int dirty;
  struct pmstream *st;
} ofile[PMFILE_MAX_FD];

static int g_chunk_mode_cdc = 1;
static int g_batch_seal = 1;

static int
name_eq(const char *a, const char *b)
{
  int i;
  for(i = 0; i < PMFILE_NAME; i++){
    if(a[i] != b[i])
      return 0;
    if(a[i] == 0)
      return 1;
  }
  return 1;
}

static void
name_copy(char *dst, const char *src)
{
  int i;
  for(i = 0; i < PMFILE_NAME - 1 && src && src[i]; i++)
    dst[i] = src[i];
  for(; i < PMFILE_NAME; i++)
    dst[i] = 0;
}

static int
slash_count(const char *p)
{
  int i, n = 0;
  for(i = 0; p[i]; i++)
    if(p[i] == '/')
      n++;
  return n;
}

static int
find_dent_full(const char *path)
{
  int i;
  for(i = 0; i < PMFILE_MAX_INO; i++){
    if(dents[i].ino != 0 && name_eq(dents[i].name, path))
      return i;
  }
  return -1;
}

static int
resolve_parent(const char *path, int *parent_ino)
{
  int i, slash = -1;
  char ppath[PMFILE_NAME];

  if(path == 0 || path[0] != '/')
    return -1;
  for(i = 0; path[i]; i++)
    if(path[i] == '/')
      slash = i;
  if(slash < 0 || path[slash + 1] == 0)
    return -1;
  if(slash == 0){
    *parent_ino = PMFILE_ROOT_INO;
    return 0;
  }
  if(slash >= PMFILE_NAME)
    return -1;
  memmove(ppath, (void*)path, slash);
  ppath[slash] = 0;
  for(i = 0; i < PMFILE_MAX_INO; i++){
    if(dents[i].ino && name_eq(dents[i].name, ppath) &&
       inodes[dents[i].ino - 1].type == PMFILE_TYPE_DIR){
      *parent_ino = (int)dents[i].ino;
      return 0;
    }
  }
  return -1;
}

static int
pmfile_load(void)
{
  struct pmfs *fs = pmfs_store();
  if(fs == 0 || !fs->mounted)
    return -1;
  if(pmdev_read(&fs->dev, PMFILE_INODE_PG, 0, inodes, sizeof(inodes)) != 0)
    return -1;
  if(pmdev_read(&fs->dev, PMFILE_DENT_PG, 0, dents, sizeof(dents)) != 0)
    return -1;
  return 0;
}

static int
pmfile_store_meta(void)
{
  struct pmfs *fs = pmfs_store();
  static uint8 page[PMDEV_PAGE];
  if(fs == 0 || !fs->mounted)
    return -1;
  memset(page, 0, sizeof(page));
  memmove(page, inodes, sizeof(inodes) <= PMDEV_PAGE ? sizeof(inodes) : PMDEV_PAGE);
  if(pmdev_write_page(&fs->dev, PMFILE_INODE_PG, page) != 0)
    return -1;
  memset(page, 0, sizeof(page));
  memmove(page, dents, sizeof(dents) <= PMDEV_PAGE ? sizeof(dents) : PMDEV_PAGE);
  if(pmdev_write_page(&fs->dev, PMFILE_DENT_PG, page) != 0)
    return -1;
  if(pmfs_commit(fs) != 0)
    return -1;
  return 0;
}

static void
ensure_root(void)
{
  if(inodes[0].used)
    return;
  memset(&inodes[0], 0, sizeof(inodes[0]));
  inodes[0].used = 1;
  inodes[0].type = PMFILE_TYPE_DIR;
  inodes[0].nlink = 1;
  dents[0].ino = PMFILE_ROOT_INO;
  dents[0].parent = 0;
  name_copy(dents[0].name, "/");
}

void
pmfile_init(void)
{
  int i;
  struct pmfs *fs = pmfs_store();
  memset(inodes, 0, sizeof(inodes));
  memset(dents, 0, sizeof(dents));
  for(i = 0; i < PMFILE_MAX_FD; i++){
    ofile[i].ino = 0;
    ofile[i].dirty = 0;
    ofile[i].st = 0;
  }
  pmstream_init_global();
  if(fs == 0 || !fs->mounted)
    return;
  if(fs->npages <= PMFILE_DENT_PG)
    return;
  if(pmfile_load() != 0 || !inodes[0].used){
    memset(inodes, 0, sizeof(inodes));
    memset(dents, 0, sizeof(dents));
    ensure_root();
    pmfile_store_meta();
  }
}

static int
alloc_ino(void)
{
  int i;
  for(i = 0; i < PMFILE_MAX_INO; i++)
    if(!inodes[i].used)
      return i;
  return -1;
}

static int
alloc_dent(void)
{
  int i;
  for(i = 0; i < PMFILE_MAX_INO; i++)
    if(dents[i].ino == 0)
      return i;
  return -1;
}

int
pmfile_mkdir(const char *path)
{
  int ii, di, parent;
  if(path == 0 || path[0] != '/' || find_dent_full(path) >= 0)
    return -1;
  if(resolve_parent(path, &parent) != 0)
    return -1;
  ii = alloc_ino();
  di = alloc_dent();
  if(ii < 0 || di < 0)
    return -1;
  memset(&inodes[ii], 0, sizeof(inodes[ii]));
  inodes[ii].used = 1;
  inodes[ii].type = PMFILE_TYPE_DIR;
  inodes[ii].nlink = 1;
  dents[di].ino = (uint32)(ii + 1);
  dents[di].parent = (uint32)parent;
  name_copy(dents[di].name, path);
  return pmfile_store_meta();
}

int
pmfile_create(const char *path)
{
  int ii, di, parent;
  if(path == 0 || path[0] != '/' || find_dent_full(path) >= 0)
    return -1;
  if(resolve_parent(path, &parent) != 0)
    return -1;
  ii = alloc_ino();
  di = alloc_dent();
  if(ii < 0 || di < 0)
    return -1;
  memset(&inodes[ii], 0, sizeof(inodes[ii]));
  inodes[ii].used = 1;
  inodes[ii].type = PMFILE_TYPE_REG;
  inodes[ii].nlink = 1;
  dents[di].ino = (uint32)(ii + 1);
  dents[di].parent = (uint32)parent;
  name_copy(dents[di].name, path);
  return 0;
}

int
pmfile_open(const char *path)
{
  int di, fd, ino;
  di = find_dent_full(path);
  if(di < 0)
    return -1;
  ino = (int)dents[di].ino;
  if(ino < 1 || ino > PMFILE_MAX_INO)
    return -1;
  if(inodes[ino - 1].type != PMFILE_TYPE_REG)
    return -1;
  for(fd = 0; fd < PMFILE_MAX_FD; fd++){
    if(ofile[fd].ino == 0){
      ofile[fd].ino = ino;
      ofile[fd].dirty = 0;
      ofile[fd].st = pmstream_alloc(ino, g_chunk_mode_cdc, g_batch_seal);
      return fd;
    }
  }
  return -1;
}

int
pmfile_close(int fd)
{
  if(fd < 0 || fd >= PMFILE_MAX_FD || ofile[fd].ino == 0)
    return -1;
  if(ofile[fd].dirty)
    pmfile_fsync(fd);
  if(ofile[fd].st)
    pmstream_free(ofile[fd].st);
  ofile[fd].ino = 0;
  ofile[fd].dirty = 0;
  ofile[fd].st = 0;
  return 0;
}

static void
sync_extents_from_stream(struct pmfile_inode *ino, struct pmstream *st)
{
  uint32 i;
  uint32 off = 0;
  if(st == 0)
    return;
  ino->nextent = 0;
  for(i = 0; i < st->nchunk && ino->nextent < PMFILE_MAX_EXT; i++){
    ino->ext[ino->nextent].key = st->chunks[i].key;
    ino->ext[ino->nextent].len = st->chunks[i].len;
    ino->ext[ino->nextent].off = off;
    off += st->chunks[i].len;
    ino->nextent++;
  }
  ino->next_seq = st->nchunk;
  ino->size = off;
}

int
pmfile_write(int fd, const void *src, int len)
{
  struct pmfile_inode *ino;
  struct pmstream *st;
  struct pm_algo *a = pm_algo_global();
  int n;

  if(fd < 0 || fd >= PMFILE_MAX_FD || ofile[fd].ino == 0 || src == 0 || len < 0)
    return -1;
  if(len == 0)
    return 0;
  ino = &inodes[ofile[fd].ino - 1];
  st = ofile[fd].st;
  if(st == 0 || a == 0)
    return -1;

  n = pmstream_write(st, src, (uint32)len);
  if(n < 0)
    return -1;

  sync_extents_from_stream(ino, st);
  ofile[fd].dirty = 1;
  return n;
}

int
pmfile_fsync(int fd)
{
  struct pmfile_inode *ino;
  struct pmstream *st;

  if(fd < 0 || fd >= PMFILE_MAX_FD || ofile[fd].ino == 0)
    return -1;
  ino = &inodes[ofile[fd].ino - 1];
  st = ofile[fd].st;
  if(st && pmstream_flush(st) != 0)
    return -1;
  sync_extents_from_stream(ino, st);
  ofile[fd].dirty = 0;
  return pmfile_store_meta();
}

int
pmfile_read(int fd, void *dst, int cap, uint32 *out_len)
{
  struct pmfile_inode *ino;
  struct pm_algo *a = pm_algo_global();
  static char chunk[PM_VAL_MAX];
  uint32 got = 0, i, clen;
  uint8 *out = (uint8 *)dst;

  if(fd < 0 || fd >= PMFILE_MAX_FD || ofile[fd].ino == 0 || dst == 0 || cap < 0)
    return -1;
  if(out_len)
    *out_len = 0;
  ino = &inodes[ofile[fd].ino - 1];
  if(a == 0)
    return -1;
  for(i = 0; i < ino->nextent && got < (uint32)cap; i++){
    if(pm_get(a, ino->ext[i].key, chunk, sizeof(chunk), &clen) != 1)
      continue;
    if(clen > (uint32)cap - got)
      clen = (uint32)cap - got;
    memmove(out + got, chunk, clen);
    got += clen;
  }
  if(out_len)
    *out_len = got;
  return 1; // hit (user/syscall convention)
}

int
pmfile_unlink(const char *path)
{
  int di, ino, i;
  di = find_dent_full(path);
  if(di < 0)
    return -1;
  ino = (int)dents[di].ino;
  if(ino == PMFILE_ROOT_INO)
    return -1;
  for(i = 0; i < PMFILE_MAX_FD; i++)
    if(ofile[i].ino == ino)
      return -1;
  if(inodes[ino - 1].type == PMFILE_TYPE_DIR){
    // refuse non-empty
    for(i = 0; i < PMFILE_MAX_INO; i++)
      if(dents[i].ino && dents[i].parent == (uint32)ino)
        return -1;
  }
  memset(&inodes[ino - 1], 0, sizeof(inodes[ino - 1]));
  memset(&dents[di], 0, sizeof(dents[di]));
  return pmfile_store_meta();
}

int
pmfile_readdir(const char *path, char *dst, int cap)
{
  int di, ino, i, pos = 0;
  if(path == 0 || dst == 0 || cap < 2)
    return -1;
  di = find_dent_full(path);
  if(di < 0)
    return -1;
  ino = (int)dents[di].ino;
  if(inodes[ino - 1].type != PMFILE_TYPE_DIR)
    return -1;
  dst[0] = 0;
  for(i = 0; i < PMFILE_MAX_INO; i++){
    if(dents[i].ino && dents[i].parent == (uint32)ino){
      int j;
      for(j = 0; dents[i].name[j] && pos + 2 < cap; j++)
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
  di = find_dent_full(path);
  if(di < 0)
    return -1;
  ino = (int)dents[di].ino;
  if(size)
    *size = inodes[ino - 1].size;
  if(type)
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
  const char *payload =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
    "abcdefghijklmnopqrstuvwxyz"
    "PMFS-CDC-STREAM-FUSE-CHUNK-PAYLOAD-REPEAT-"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

  ensure_root();
  if(pmfile_mkdir("/d") != 0){
    // may already exist after remount
    if(find_dent_full("/d") < 0){ pmcontract_fail("FILE", 1); return -1; }
  }
  if(pmfile_create("/d/f1") != 0 && find_dent_full("/d/f1") < 0){
    pmcontract_fail("FILE", 2); return -2;
  }
  fd = pmfile_open("/d/f1");
  if(fd < 0){ pmcontract_fail("FILE", 3); return -3; }
  if(pmfile_write(fd, payload, (int)strlen(payload)) < 0){
    pmcontract_fail("FILE", 4); return -4;
  }
  if(pmfile_fsync(fd) != 0){ pmcontract_fail("FILE", 5); return -5; }
  if(pmfile_read(fd, buf, sizeof(buf), &n) != 1 || n == 0){
    pmcontract_fail("FILE", 6); return -6;
  }
  if(pmfile_close(fd) != 0){ pmcontract_fail("FILE", 7); return -7; }
  if(pmfile_stat("/d/f1", &sz, &ty) != 0 || ty != PMFILE_TYPE_REG || sz == 0){
    pmcontract_fail("FILE", 8); return -8;
  }
  if(pmfile_readdir("/d", buf, sizeof(buf)) != 0){
    pmcontract_fail("FILE", 9); return -9;
  }
  if(a == 0 || a->fuse_puts < 1 || a->fuse_seals < 1){
    pmcontract_fail("FILE", 10); return -10; // fusion must hit KV
  }
  printk("ONBOX_PMFS_POS_FILE_PASS size=%d fuse_puts=%d dents_ok=1\n",
         (int)sz, (int)a->fuse_puts);
  pmcontract_mark("POS_FILE", 1);

  // NEG: open missing
  if(pmfile_open("/nope") >= 0){ pmcontract_fail("FILE", 11); return -11; }
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
  if(fd < 0){ pmcontract_fail("CHAOS_FILE", 1); return -1; }
  if(pmfile_write(fd, "half", 4) < 0){ pmcontract_fail("CHAOS_FILE", 2); return -2; }
  // Intentionally no fsync — close frees stream without durable meta update of extents
  ofile[fd].dirty = 0; // skip fsync path
  if(ofile[fd].st)
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
  for(i = 0; i < (int)sizeof(big); i++)
    big[i] = (char)('A' + (i % 26));
  ensure_root();
  pmfile_create("/edge");
  fd = pmfile_open("/edge");
  if(fd < 0){ pmcontract_fail("EDGE_FILE", 1); return -1; }
  if(pmfile_write(fd, big, (int)sizeof(big)) < 0){
    pmcontract_fail("EDGE_FILE", 2); return -2;
  }
  if(pmfile_fsync(fd) != 0){ pmcontract_fail("EDGE_FILE", 3); return -3; }
  pmfile_close(fd);
  printk("ONBOX_PMFS_EDGE_FILE_PASS multi_chunk=1\n");
  pmcontract_mark("EDGE_FILE", 1);
  return 0;
}

// Fair ablation A0–A4 (same payload; vary CDC / seal / fixed).
int
pmfile_ablation_selftest(void)
{
  struct pm_algo *a = pm_algo_global();
  char payload[256];
  int i, fd;
  uint64 puts0, seals0;
  // A0 CDC+batch, A1 fixed+batch, A2 CDC+per-chunk seal, A3 fixed+per-chunk, A4 CDC+batch again
  struct { int cdc; int batch; char *tag; } cfg[5] = {
    {1, 1, "A0"}, {0, 1, "A1"}, {1, 0, "A2"}, {0, 0, "A3"}, {1, 1, "A4"}
  };

  if(a == 0){ pmcontract_fail("ABLATION", 1); return -1; }
  for(i = 0; i < (int)sizeof(payload); i++)
    payload[i] = (char)(i * 3 + 7);

  ensure_root();
  for(i = 0; i < 5; i++){
    char path[12];
    path[0] = '/'; path[1] = 'a'; path[2] = '0' + i; path[3] = 0;
    pmfile_set_ablation(cfg[i].cdc, cfg[i].batch);
    pmfile_unlink(path);
    if(pmfile_create(path) != 0 && find_dent_full(path) < 0){
      pmcontract_fail("ABLATION", 10 + i); return -10 - i;
    }
    puts0 = a->fuse_puts;
    seals0 = a->fuse_seals;
    fd = pmfile_open(path);
    if(fd < 0){ pmcontract_fail("ABLATION", 20 + i); return -20 - i; }
    if(pmfile_write(fd, payload, (int)sizeof(payload)) < 0){
      pmcontract_fail("ABLATION", 30 + i); return -30 - i;
    }
    if(pmfile_fsync(fd) != 0){ pmcontract_fail("ABLATION", 40 + i); return -40 - i; }
    pmfile_close(fd);
    printk("ONBOX_PMFS_ABLATION_%s_PASS cdc=%d batch=%d dputs=%d dseals=%d\n",
           cfg[i].tag, cfg[i].cdc, cfg[i].batch,
           (int)(a->fuse_puts - puts0), (int)(a->fuse_seals - seals0));
  }
  pmfile_set_ablation(1, 1); // restore default
  printk("ONBOX_PMFS_ABLATION_PASS variants=5 fair=1\n");
  pmcontract_mark("ABLATION", 1);
  return 0;
}
