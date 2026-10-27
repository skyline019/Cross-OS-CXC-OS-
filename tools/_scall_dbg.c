#define CC_HOST 1
#include "cxc_cc/cc.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char* lib =
    "CXC_A2\n"
    "struct Pt {\n"
    "x;\n"
    "y;\n"
    "};\n"
    "fn score(){\n"
    "Pt p;\n"
    "Pt* sp;\n"
    "long n;\n"
    "p.x=6;\n"
    "p.y=7;\n"
    "sp=&p;\n"
    "n=sp->x*sp->y;\n"
    "return n;\n"
    "}\n";

static const char* main_s =
    ".text\n.globl main\nmain:\n"
    "jmp .Lok\n"
    ".Lbad:\nmov $1, %eax\nret\n"
    ".Lok:\n"
    "call score\n"
    "cmp $42, %eax\n"
    "jne .Lbad\n"
    "lea buf(%rip), %rax\n"
    "mov %rax, %rdi\n"
    "lea s4(%rip), %rax\n"
    "mov %rax, %rsi\n"
    "call strcpy\n"
    "lea buf(%rip), %rax\n"
    "mov %rax, %rdi\n"
    "lea s2(%rip), %rax\n"
    "mov %rax, %rsi\n"
    "call strcat\n"
    "lea buf(%rip), %rax\n"
    "mov %rax, %rdi\n"
    "lea s42(%rip), %rax\n"
    "mov %rax, %rsi\n"
    "mov $2, %edx\n"
    "call strncmp\n"
    "cmp $0, %eax\n"
    "jne .Lbad\n"
    "lea buf2(%rip), %rax\n"
    "mov %rax, %rdi\n"
    "lea sa(%rip), %rax\n"
    "mov %rax, %rsi\n"
    "call stpcpy\n"
    "lea buf2(%rip), %rax\n"
    "mov %rax, %rdi\n"
    "lea sa(%rip), %rax\n"
    "mov %rax, %rsi\n"
    "call strcmp\n"
    "cmp $0, %eax\n"
    "jne .Lbad\n"
    "mov $53, %edi\n"
    "call isdigit\n"
    "test %eax, %eax\n"
    "je .Lbad\n"
    "lea buf(%rip), %rax\n"
    "mov %rax, %rdi\n"
    "call atoi\n"
    "cmp $42, %eax\n"
    "jne .Lbad\n"
    "mov $42, %edi\n"
    "call labs\n"
    "and $255, %eax\n"
    "ret\n"
    ".data\n"
    "buf:\n.zero 16\n"
    "buf2:\n.zero 16\n"
    ".rodata\n"
    "sa:\n.asciz \"SC\"\n"
    "s4:\n.asciz \"4\"\n"
    "s2:\n.asciz \"2\"\n"
    "s42:\n.asciz \"42\"\n"
    "q:\n.quad 42\n";

int main(void) {
  unsigned char* o = 0;
  size_t n = 0;
  unsigned char* mo = 0;
  size_t mn = 0;
  unsigned char* lc;
  unsigned char* mc;
  const char* paths[14];
  unsigned char* elf = 0;
  size_t en = 0;
  int r;
  FILE* f;

  r = cc_compile_obj(lib, strlen(lib), &o, &n);
  printf("lib r=%d n=%zu\n", r, n);
  lc = (unsigned char*)malloc(n);
  memcpy(lc, o, n);
  f = fopen("disk/lib.o", "wb");
  fwrite(lc, 1, n, f);
  fclose(f);

  r = as_compile(main_s, strlen(main_s), &mo, &mn);
  printf("as r=%d n=%zu fail=%d\n", r, mn, as_last_fail_code());
  mc = (unsigned char*)malloc(mn);
  memcpy(mc, mo, mn);
  f = fopen("disk/main.o", "wb");
  fwrite(mc, 1, mn, f);
  fclose(f);

  paths[0] = "disk/main.o";
  paths[1] = "disk/lib.o";
  paths[2] = "disk/atoi.o";
  paths[3] = "disk/scmp.o";
  paths[4] = "disk/labs.o";
  paths[5] = "disk/dig.o";
  paths[6] = "disk/scat.o";
  paths[7] = "disk/schr.o";
  paths[8] = "disk/snul.o";
  paths[9] = "disk/scpy.o";
  paths[10] = "disk/stpc.o";
  paths[11] = "disk/sncmp.o";
  paths[12] = "disk/mcmp.o";
  paths[13] = "disk/io.elf";
  r = ld_link(paths, 14, &elf, &en);
  printf("full r=%d n=%zu fail=%d\n", r, en, ld_last_fail_code());
  free(lc);
  free(mc);
  return r != 0;
}
