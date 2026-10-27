#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "pmcontract.h"

// Peer stages: core + CDC/STREAM + POS/NEG/CHAOS file + ablation.
enum { NREQ = 14 };

static char *required[NREQ] = {
  "PMEM0", "MOUNT", "STORE", "INDEX", "ALGO", "CRASH", "PERSIST",
  "FILE", "CDC", "STREAM",
  "POS_FILE", "NEG_FILE", "CHAOS_FILE", "ABLATION"
};
static int req_ok[NREQ];

static int
streq(char *a, char *b)
{
  while(*a && *b && *a == *b){
    a++;
    b++;
  }
  return *a == 0 && *b == 0;
}

static int
stage_idx(char *stage)
{
  int i;
  for(i = 0; i < NREQ; i++){
    if(streq(stage, required[i]))
      return i;
  }
  return -1;
}

void
pmcontract_mark(char *stage, int ok)
{
  int i = stage_idx(stage);
  if(i >= 0 && ok)
    req_ok[i] = 1;
}

void
pmcontract_pass(char *stage, char *fields)
{
  if(fields && fields[0])
    printk("ONBOX_PMFS_%s_PASS %s\n", stage, fields);
  else
    printk("ONBOX_PMFS_%s_PASS\n", stage);
  pmcontract_mark(stage, 1);
}

void
pmcontract_fail(char *stage, int code)
{
  printk("ONBOX_PMFS_%s_FAIL stage=%s code=%d\n", stage, stage, code);
}

void
pmcontract_gate(void)
{
  int i;
  for(i = 0; i < NREQ; i++){
    if(!req_ok[i]){
      printk("ONBOX_PMFS_GATE_FAIL missing=%s\n", required[i]);
      return;
    }
  }
  printk("ONBOX_PMFS_PASS\n");
}
