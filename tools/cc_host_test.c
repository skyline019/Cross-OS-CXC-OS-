/* Host-side strict compile tests for freestanding cxc-cc (M97). */
#define CC_HOST 1
#include "cxc_cc/cc.h"

#include <stdio.h>
#include <string.h>

static int expect_ok(const char* src, const char* tag) {
    unsigned char* elf = 0;
    size_t n = 0;
    if (cc_compile(src, strlen(src), &elf, &n) != 0 || !elf || n < 64) {
        fprintf(stderr, "FAIL ok %s: compile\n", tag);
        return -1;
    }
    if (elf[0] != 0x7f || elf[1] != 'E' || elf[2] != 'L' || elf[3] != 'F') {
        fprintf(stderr, "FAIL ok %s: magic\n", tag);
        return -1;
    }
    return 0;
}

static int expect_fail(const char* src, const char* tag) {
    unsigned char* elf = 0;
    size_t n = 0;
    if (cc_compile(src, strlen(src), &elf, &n) == 0) {
        fprintf(stderr, "FAIL edge %s: expected reject\n", tag);
        return -1;
    }
    return 0;
}

int main(void) {
    int fails = 0;

    if (expect_ok(
            "CXC_A2\ni=0;\nn=0;\nwhile(i<6){\nn=n+7;\ni=i+1;\n}\n"
            "if(n==42){\nwrite HI;\n}\nreturn n;\n",
            "m96-ctrl") != 0) {
        fails++;
    }
    if (expect_ok(
            "CXC_A2\n"
            "a[6];\n"
            "i=0;\n"
            "n=0;\n"
            "while(1){\n"
            "if(i==6){ break; }\n"
            "if(i==0){ i=i+1; continue; }\n"
            "a[i]=7;\n"
            "n=n+a[i];\n"
            "i=i+1;\n"
            "}\n"
            "if(n==35 && !(i!=6)){\n"
            "write OK;\n"
            "}\n"
            "return n+7;\n",
            "m97-lang") != 0) {
        fails++;
    }
    if (expect_ok(
            "CXC_A2\nx=0;\nif(1||0){ x=1; }\nif(0&&1){ x=0; }\n"
            "return -(-42);\n",
            "logic-unary") != 0) {
        fails++;
    }
    if (expect_ok(
            "CXC_A2\ninput n;\nprint n;\nif(n==35){\nwrite IO;\n}\n"
            "return n+7;\n",
            "m98-iorw") != 0) {
        fails++;
    }
    if (expect_ok("CXC_A2\nprint 0;\nreturn 0;\n", "print-zero") != 0) {
        fails++;
    }
    if (expect_ok(
            "CXC_A2\n"
            "fn add(a,b){\nreturn a+b;\n}\n"
            "fn mul(a,b){\nreturn a*b;\n}\n"
            "n=mul(add(3,4),6);\n"
            "print n;\n"
            "return n;\n",
            "m99-func") != 0) {
        fails++;
    }
    if (expect_ok(
            "CXC_A2\n"
            "fn sum(n){\n"
            "if(n==0){ return 0; }\n"
            "return n+sum(n-1);\n"
            "}\n"
            "return sum(8);\n",
            "m99-recur") != 0) {
        fails++;
    }
    if (expect_ok(
            "CXC_A2\n"
            "struct Pt {\n"
            "x;\n"
            "y;\n"
            "};\n"
            "Pt p;\n"
            "p.x=5;\n"
            "p.y=7;\n"
            "q=&p.x;\n"
            "*q=*q+1;\n"
            "n=p.x*p.y;\n"
            "print n;\n"
            "return n;\n",
            "m100-data") != 0) {
        fails++;
    }
    if (expect_ok(
            "CXC_A2\n"
            "n=0;\n"
            "for(i=0;i<3;i=i+1){\n"
            "n=n+10;\n"
            "}\n"
            "return n+12;\n",
            "m101-for") != 0) {
        fails++;
    }
    if (expect_ok(
            "CXC_A2\n"
            "#include \"lib.cx\"\n"
            "n=0;\n"
            "for(i=0;i<3;i=i+1){\n"
            "n=n+10;\n"
            "}\n"
            "n=add(n,12);\n"
            "print n;\n"
            "if(n==42){\n"
            "write IN;\n"
            "}\n"
            "return n;\n",
            "m101-incl") != 0) {
        fails++;
    }
    if (expect_ok(
            "CXC_A2\n"
            "#include \"lib.cx\"\n"
            "int g=10;\n"
            "fn main(){\n"
            "int n;\n"
            "n=g;\n"
            "do{\n"
            "n+=1;\n"
            "}while(n<30);\n"
            "n=add(n,12);\n"
            "print n;\n"
            "if(n==42){\n"
            "write \"CR\";\n"
            "}\n"
            "return n;\n"
            "}\n",
            "m102-crt") != 0) {
        fails++;
    }
    if (expect_ok(
            "CXC_A2\n"
            "int k=2;\n"
            "fn main(){\n"
            "int n;\n"
            "n=k;\n"
            "switch(n){\n"
            "case 1:\n"
            "n=0;\n"
            "break;\n"
            "case 2:\n"
            "n=40;\n"
            "break;\n"
            "default:\n"
            "n=1;\n"
            "break;\n"
            "}\n"
            "n=n>0?n+2:0;\n"
            "n=n%100;\n"
            "c='*';\n"
            "n=n+(c-42);\n"
            "print n;\n"
            "if(n==42){\n"
            "write \"CS\";\n"
            "}\n"
            "return n;\n"
            "}\n",
            "m103-case") != 0) {
        fails++;
    }
    if (expect_ok(
            "CXC_A2\n"
            "enum { TEN=0x0a, TW=20 };\n"
            "fn main(){\n"
            "int n;\n"
            "n=TEN+TW+(1<<2)+sizeof(n);\n"
            "print n;\n"
            "if(n==42){\n"
            "write \"EN\";\n"
            "}\n"
            "return n;\n"
            "}\n",
            "m104-enum") != 0) {
        fails++;
    }
    if (expect_ok(
            "CXC_A2\n"
            "n=0x20;\n"
            "n=n>>1;\n"
            "return n;\n",
            "m104-shift") != 0) {
        fails++;
    }
    if (expect_ok(
            "CXC_A2\n"
            "n=0;\n"
            "n+=21;\n"
            "n++;\n"
            "n++;\n"
            "return n;\n",
            "m102-asgn") != 0) {
        fails++;
    }
    if (expect_ok(
            "CXC_A2\n"
            "enum { TEN=0x0a, TW=20 };\n"
            "fn main(){\n"
            "int n;\n"
            "n=TEN+TW+(1<<2)+sizeof(n);\n"
            "print n;\n"
            "if(n==42){\n"
            "write \"EN\";\n"
            "}\n"
            "return n;\n"
            "}\n",
            "m104-enum") != 0) {
        fails++;
    }
    if (expect_ok("CXC_A2\nn=0x2a;\nreturn n;\n", "m104-hex") != 0) {
        fails++;
    }
    if (expect_ok("CXC_A2\nn=1<<3;\nreturn n+(1>>0);\n", "m104-shift") != 0) {
        fails++;
    }
    if (expect_ok(
            "CXC_A2\n"
            "#link \"lib.c\"\n"
            "extern fn add(a,b);\n"
            "fn main(){\n"
            "int n;\n"
            "n=add(10,32);\n"
            "print n;\n"
            "if(n==42){\n"
            "write \"LN\";\n"
            "}\n"
            "return n;\n"
            "}\n",
            "m105-link") != 0) {
        fails++;
    }
    if (expect_ok(
            "CXC_A2\n"
            "#lib \"rt.elf\"\n"
            "import fn add(a,b);\n"
            "fn main(){\n"
            "int n;\n"
            "n=add(10,32);\n"
            "print n;\n"
            "if(n==42){\n"
            "write \"LB\";\n"
            "}\n"
            "return n;\n"
            "}\n",
            "m106-lib") != 0) {
        fails++;
    }
    if (expect_ok(
            "CXC_A2\n"
            "#lib \"rt.elf\"\n"
            "import fn add(a,b);\n"
            "import fn mul(a,b);\n"
            "import fn mac(a,b,c);\n"
            "fn main(){\n"
            "int n;\n"
            "n=mac(mul(add(5,2),3),2,0);\n"
            "print n;\n"
            "if(n==42){\n"
            "write \"MH\";\n"
            "}\n"
            "return n;\n"
            "}\n",
            "m107-math") != 0) {
        fails++;
    }
    if (expect_ok(
            "CXC_A2\n"
            "#lib \"rt.elf\"\n"
            "import fn mac(a,b,c);\n"
            "fn main(){\n"
            "return mac(6,7,0);\n"
            "}\n",
            "m107-mac") != 0) {
        fails++;
    }
    if (expect_ok(
            "CXC_A2\n"
            "#lib \"rt.elf\"\n"
            "import fn twice(a);\n"
            "fn main(){\n"
            "int n;\n"
            "n=twice(21);\n"
            "print n;\n"
            "if(n==42){\n"
            "write \"PC\";\n"
            "}\n"
            "return n;\n"
            "}\n",
            "m108-pc32") != 0) {
        fails++;
    }
    if (expect_ok(
            "CXC_A2\n"
            "#lib \"rt.elf\"\n"
            "import fn twice(a);\n"
            "import fn mac(a,b,c);\n"
            "fn main(){\n"
            "return mac(twice(10),2,2);\n"
            "}\n",
            "m108-compose") != 0) {
        fails++;
    }
    if (expect_ok(
            "CXC_A2\n"
            "#lib \"rt.elf\"\n"
            "#lib \"crt.elf\"\n"
            "import fn ans();\n"
            "fn main(){\n"
            "int n;\n"
            "n=ans();\n"
            "print n;\n"
            "if(n==42){\n"
            "write \"XL\";\n"
            "}\n"
            "return n;\n"
            "}\n",
            "m109-xlib") != 0) {
        fails++;
    }
    if (expect_ok(
            "CXC_A2\n"
            "#lib \"rt.elf\"\n"
            "#lib \"crt.elf\"\n"
            "#lib \"io.elf\"\n"
            "import fn ans();\n"
            "import fn cwrite(p,n);\n"
            "fn main(){\n"
            "int n;\n"
            "n=ans();\n"
            "cwrite(\"WMSG:IO\\n\", 8);\n"
            "print n;\n"
            "return n;\n"
            "}\n",
            "m110-libc") != 0) {
        fails++;
    }
    if (expect_ok(
            "CXC_A2\n"
            "#lib \"io.elf\"\n"
            "import fn cwrite(p,n);\n"
            "fn main(){\n"
            "cwrite(\"AB\", 2);\n"
            "return 0;\n"
            "}\n",
            "m110-cwrite") != 0) {
        fails++;
    }
    {
        unsigned char* o = 0;
        size_t on = 0;
        FILE* f;
        const char* lib =
            "CXC_A2\n"
            "int add(int a, int b){\n"
            "return a+b;\n"
            "}\n"
            "int bump(int x){\n"
            "return add(x, 7);\n"
            "}\n";
        if (cc_compile_obj(lib, strlen(lib), &o, &on) != 0 || !o || on < 64) {
            fprintf(stderr, "FAIL ok m114-cfn-obj: compile_obj\n");
            fails++;
        } else if (o[0] != 0x7f || o[16] != 1) {
            fprintf(stderr, "FAIL ok m114-cfn-obj: not ET_REL\n");
            fails++;
        } else {
            /* M122/M123: multi-fn obj carries .rela.text (PLT32). */
            int found_rela = 0;
            size_t i;
            for (i = 0; i + 10 < on; i++) {
                if (o[i] == '.' && o[i + 1] == 'r' && o[i + 2] == 'e' &&
                    o[i + 3] == 'l' && o[i + 4] == 'a' && o[i + 5] == '.' &&
                    o[i + 6] == 't' && o[i + 7] == 'e' && o[i + 8] == 'x' &&
                    o[i + 9] == 't') {
                    found_rela = 1;
                    break;
                }
            }
            if (!found_rela) {
                fprintf(stderr, "FAIL ok m122-rela: missing .rela.text\n");
                fails++;
            }
            f = fopen("disk/lib.o", "wb");
            if (!f || fwrite(o, 1, on, f) != on || fclose(f) != 0) {
                fprintf(stderr, "FAIL ok m114-cfn-obj: write lib.o\n");
                fails++;
            }
        }
    }
    if (expect_ok(
            "CXC_A2\n"
            "#lib \"lib.o\"\n"
            "#lib \"io.elf\"\n"
            "import fn bump(x);\n"
            "import fn cwrite(p,n);\n"
            "import fn strlen(s);\n"
            "int main(void){\n"
            "int n;\n"
            "int m;\n"
            "m=strlen(\"WMSG:CF\\n\");\n"
            "cwrite(\"WMSG:CF\\n\", m);\n"
            "n=bump(35);\n"
            "print n;\n"
            "return n;\n"
            "}\n",
            "m114-cfn") != 0) {
        fails++;
    }
    if (expect_ok(
            "CXC_A2\n"
            "#lib \"io.elf\"\n"
            "#lib \"math.o\"\n"
            "#lib \"host.o\"\n"
            "import fn bump(x);\n"
            "int main(void){\n"
            "int n;\n"
            "n=bump(35);\n"
            "print n;\n"
            "return n;\n"
            "}\n",
            "m118-host-gotpcrel") != 0) {
        fails++;
    }
    if (expect_ok(
            "CXC_A2\n"
            "#lib \"io.elf\"\n"
            "#lib \"musl.o\"\n"
            "import fn labs(x);\n"
            "import fn cwrite(p,n);\n"
            "import fn strlen(s);\n"
            "int main(void){\n"
            "int n;\n"
            "int m;\n"
            "m=strlen(\"WMSG:ML\\n\");\n"
            "cwrite(\"WMSG:ML\\n\", m);\n"
            "n=labs(0-42);\n"
            "print n;\n"
            "return n;\n"
            "}\n",
            "m119-musl-labs") != 0) {
        fails++;
    }
    if (expect_ok(
            "CXC_A2\n"
            "#lib \"io.elf\"\n"
            "#lib \"dig.o\"\n"
            "#lib \"labs.o\"\n"
            "import fn isdigit(c);\n"
            "import fn labs(x);\n"
            "import fn cwrite(p,n);\n"
            "import fn strlen(s);\n"
            "int main(void){\n"
            "int n;\n"
            "int m;\n"
            "m=strlen(\"WMSG:MD\\n\");\n"
            "cwrite(\"WMSG:MD\\n\", m);\n"
            "n=isdigit(50);\n"
            "n=n+41;\n"
            "n=labs(0-n);\n"
            "print n;\n"
            "return n;\n"
            "}\n",
            "m120-musl-isdigit-multi") != 0) {
        fails++;
    }
    if (expect_ok(
            "CXC_A2\n"
            "#lib \"slen.o\"\n"
            "#lib \"mcmp.o\"\n"
            "#lib \"mcpy.o\"\n"
            "#lib \"io.elf\"\n"
            "import fn strlen(s);\n"
            "import fn memcmp(a,b,n);\n"
            "import fn memcpy(d,s,n);\n"
            "import fn cwrite(p,n);\n"
            "int main(void){\n"
            "int n;\n"
            "int m;\n"
            "m=strlen(\"WMSG:MM\\n\");\n"
            "memcpy(\"WMSG:MM\\n\", \"WMSG:MM\\n\", 0);\n"
            "cwrite(\"WMSG:MM\\n\", m);\n"
            "n=memcmp(\"42\", \"42\", 2);\n"
            "n=n+42;\n"
            "print n;\n"
            "return n;\n"
            "}\n",
            "m121-musl-mem-trio") != 0) {
        fails++;
    }
    {
        unsigned char* o = 0;
        size_t on = 0;
        FILE* f;
        const char* lib =
            "CXC_A2\n"
            "int add(int a, int b){\n"
            "return a+b;\n"
            "}\n"
            "int bump(int x){\n"
            "return add(x, 42);\n"
            "}\n"
            "int msg(void){\n"
            "return \"CP\";\n"
            "}\n";
        if (cc_compile_obj(lib, strlen(lib), &o, &on) != 0 || !o || on < 64) {
            fprintf(stderr, "FAIL ok m123-rodata-obj: compile_obj\n");
            fails++;
        } else {
            int found_ro = 0;
            int found_rela = 0;
            size_t i;
            for (i = 0; i + 7 < on; i++) {
                if (o[i] == '.' && o[i + 1] == 'r' && o[i + 2] == 'o' &&
                    o[i + 3] == 'd' && o[i + 4] == 'a' && o[i + 5] == 't' &&
                    o[i + 6] == 'a') {
                    found_ro = 1;
                }
                if (o[i] == '.' && o[i + 1] == 'r' && o[i + 2] == 'e' &&
                    o[i + 3] == 'l' && o[i + 4] == 'a' && o[i + 5] == '.') {
                    found_rela = 1;
                }
            }
            if (!found_ro || !found_rela) {
                fprintf(stderr,
                        "FAIL ok m123-rodata-obj: missing .rodata/.rela\n");
                fails++;
            }
            f = fopen("disk/lib.o", "wb");
            if (!f || fwrite(o, 1, on, f) != on || fclose(f) != 0) {
                fprintf(stderr, "FAIL ok m123-rodata-obj: write lib.o\n");
                fails++;
            }
        }
    }
    if (expect_ok(
            "CXC_A2\n"
            "#lib \"lib.o\"\n"
            "#lib \"scmp.o\"\n"
            "#lib \"slen.o\"\n"
            "#lib \"io.elf\"\n"
            "import fn bump(x);\n"
            "import fn msg();\n"
            "import fn strcmp(a,b);\n"
            "import fn strlen(s);\n"
            "import fn cwrite(p,n);\n"
            "int main(void){\n"
            "int n;\n"
            "int m;\n"
            "m=strlen(\"WMSG:RD\\n\");\n"
            "cwrite(\"WMSG:RD\\n\", m);\n"
            "n=strcmp(msg(), \"CP\");\n"
            "n=bump(n);\n"
            "print n;\n"
            "return n;\n"
            "}\n",
            "m123-rodata-strcmp") != 0) {
        fails++;
    }
    if (expect_ok(
            "CXC_A2\n"
            "#lib \"lib.o\"\n"
            "#lib \"scpy.o\"\n"
            "#lib \"stpc.o\"\n"
            "#lib \"sncmp.o\"\n"
            "#lib \"slen.o\"\n"
            "#lib \"io.elf\"\n"
            "import fn bump(x);\n"
            "import fn msg();\n"
            "import fn strcpy(d,s);\n"
            "import fn strncmp(a,b,n);\n"
            "import fn strlen(s);\n"
            "import fn cwrite(p,n);\n"
            "int main(void){\n"
            "int buf[4];\n"
            "int n;\n"
            "int m;\n"
            "m=strlen(\"WMSG:CP\\n\");\n"
            "cwrite(\"WMSG:CP\\n\", m);\n"
            "strcpy(&buf[0], msg());\n"
            "n=strncmp(&buf[0], \"CP\", 2);\n"
            "n=bump(n);\n"
            "print n;\n"
            "return n;\n"
            "}\n",
            "m124-strcpy-stpcpy") != 0) {
        fails++;
    }
    {
        unsigned char* o = 0;
        size_t on = 0;
        FILE* f;
        const char* lib =
            "CXC_A2\n"
            "import fn strncmp(a,b,n);\n"
            "int add(int a, int b){\n"
            "return a+b;\n"
            "}\n"
            "int bump(int x){\n"
            "return add(x, 42);\n"
            "}\n"
            "int msg(void){\n"
            "return \"LU\";\n"
            "}\n"
            "int chk(void){\n"
            "return strncmp(msg(), \"LU\", 2);\n"
            "}\n";
        if (cc_compile_obj(lib, strlen(lib), &o, &on) != 0 || !o || on < 64) {
            fprintf(stderr, "FAIL ok m125-lund-obj: compile_obj\n");
            fails++;
        } else {
            f = fopen("disk/lib.o", "wb");
            if (!f || fwrite(o, 1, on, f) != on || fclose(f) != 0) {
                fprintf(stderr, "FAIL ok m125-lund-obj: write lib.o\n");
                fails++;
            }
        }
    }
    if (expect_ok(
            "CXC_A2\n"
            "#lib \"lib.o\"\n"
            "#lib \"scpy.o\"\n"
            "#lib \"stpc.o\"\n"
            "#lib \"sncmp.o\"\n"
            "#lib \"slen.o\"\n"
            "#lib \"io.elf\"\n"
            "import fn bump(x);\n"
            "import fn chk();\n"
            "import fn msg();\n"
            "import fn strcpy(d,s);\n"
            "import fn strlen(s);\n"
            "import fn cwrite(p,n);\n"
            "int main(void){\n"
            "int buf[4];\n"
            "int n;\n"
            "int m;\n"
            "m=strlen(\"WMSG:LU\\n\");\n"
            "cwrite(\"WMSG:LU\\n\", m);\n"
            "strcpy(&buf[0], msg());\n"
            "n=chk();\n"
            "n=bump(n);\n"
            "print n;\n"
            "return n;\n"
            "}\n",
            "m125-deferred-und") != 0) {
        fails++;
    }
    {
        unsigned char* o = 0;
        size_t on = 0;
        FILE* f;
        const char* asrc =
            ".text\n"
            ".globl add\n"
            "add:\n"
            "mov %edi, %eax\n"
            "add %esi, %eax\n"
            "ret\n"
            ".globl bump\n"
            "bump:\n"
            "mov $42, %esi\n"
            "call add\n"
            "ret\n";
        const char* msrc =
            ".rodata\n"
            "xa:\n"
            ".asciz \"XA\"\n"
            ".text\n"
            ".globl msg\n"
            "msg:\n"
            "lea xa(%rip), %rax\n"
            "ret\n";
        if (as_compile(asrc, strlen(asrc), &o, &on) != 0 || !o || on < 64) {
            fprintf(stderr, "FAIL ok m128-as-obj: as_compile lib\n");
            fails++;
        } else {
            f = fopen("disk/lib.o", "wb");
            if (!f || fwrite(o, 1, on, f) != on || fclose(f) != 0) {
                fprintf(stderr, "FAIL ok m128-as-obj: write lib.o\n");
                fails++;
            }
        }
        o = 0;
        on = 0;
        if (as_compile(msrc, strlen(msrc), &o, &on) != 0 || !o || on < 64) {
            fprintf(stderr, "FAIL ok m128-as-obj: as_compile msg\n");
            fails++;
        } else {
            f = fopen("disk/msg.o", "wb");
            if (!f || fwrite(o, 1, on, f) != on || fclose(f) != 0) {
                fprintf(stderr, "FAIL ok m128-as-obj: write msg.o\n");
                fails++;
            }
        }
    }
    if (expect_ok(
            "CXC_A2\n"
            "#lib \"lib.o\"\n"
            "#lib \"msg.o\"\n"
            "#lib \"scat.o\"\n"
            "#lib \"schr.o\"\n"
            "#lib \"snul.o\"\n"
            "#lib \"scpy.o\"\n"
            "#lib \"stpc.o\"\n"
            "#lib \"sncmp.o\"\n"
            "#lib \"slen.o\"\n"
            "#lib \"io.elf\"\n"
            "import fn bump(x);\n"
            "import fn msg();\n"
            "import fn strcpy(d,s);\n"
            "import fn strcat(d,s);\n"
            "import fn strchr(s,c);\n"
            "import fn strncmp(a,b,n);\n"
            "import fn strlen(s);\n"
            "import fn cwrite(p,n);\n"
            "int main(void){\n"
            "int buf[8];\n"
            "int p;\n"
            "int n;\n"
            "int m;\n"
            "m=strlen(\"WMSG:RO\\n\");\n"
            "cwrite(\"WMSG:RO\\n\", m);\n"
            "strcpy(&buf[0], msg());\n"
            "strcat(&buf[0], \"YZ\");\n"
            "p=strchr(&buf[0], 65);\n"
            "n=strncmp(p, \"AYZ\", 3);\n"
            "n=bump(n);\n"
            "print n;\n"
            "return n;\n"
            "}\n",
            "m128-as-rodata-kit") != 0) {
        fails++;
    }
    {
        unsigned char* o = 0;
        size_t on = 0;
        unsigned char* elf = 0;
        size_t en = 0;
        FILE* f;
        const char* paths[2];
        const char* lib =
            ".text\n"
            ".globl add\n"
            "add:\n"
            "mov %edi, %eax\n"
            "add %esi, %eax\n"
            "ret\n"
            ".globl bump\n"
            "bump:\n"
            "mov $42, %esi\n"
            "call add\n"
            "ret\n";
        const char* main_s =
            ".text\n"
            ".globl main\n"
            "main:\n"
            "mov $0, %edi\n"
            "call bump\n"
            "ret\n";
        if (as_compile(lib, strlen(lib), &o, &on) != 0 || !o || on < 64) {
            fprintf(stderr, "FAIL ok m129-ld: as lib\n");
            fails++;
        } else {
            f = fopen("disk/lib.o", "wb");
            if (!f || fwrite(o, 1, on, f) != on || fclose(f) != 0) {
                fprintf(stderr, "FAIL ok m129-ld: write lib.o\n");
                fails++;
            }
        }
        o = 0;
        on = 0;
        if (as_compile(main_s, strlen(main_s), &o, &on) != 0 || !o ||
            on < 64) {
            fprintf(stderr, "FAIL ok m129-ld: as main\n");
            fails++;
        } else {
            f = fopen("disk/main.o", "wb");
            if (!f || fwrite(o, 1, on, f) != on || fclose(f) != 0) {
                fprintf(stderr, "FAIL ok m129-ld: write main.o\n");
                fails++;
            }
        }
        paths[0] = "disk/main.o";
        paths[1] = "disk/lib.o";
        if (ld_link(paths, 2, &elf, &en) != 0 || !elf || en < 64) {
            fprintf(stderr, "FAIL ok m129-ld: ld_link\n");
            fails++;
        } else if (elf[16] != 2) {
            fprintf(stderr, "FAIL ok m129-ld: not ET_EXEC\n");
            fails++;
        }
    }
    {
        /* M130: ld + musl string objs + io; edges. */
        unsigned char* o = 0;
        size_t on = 0;
        unsigned char* elf = 0;
        size_t en = 0;
        FILE* f;
        const char* paths[9];
        const char* lib =
            ".text\n.globl add\nadd:\nmov %edi, %eax\nadd %esi, %eax\nret\n"
            ".globl bump\nbump:\nmov $42, %esi\ncall add\nret\n";
        const char* main_s =
            ".rodata\ns:\n.asciz \"LL\"\n.text\n.globl main\nmain:\n"
            "lea s(%rip), %rax\nmov %rax, %rdi\ncall strlen\n"
            "mov $0, %edi\ncall bump\nret\n";
        const char* nomain =
            ".text\n.globl bump\nbump:\nmov $1, %eax\nret\n";
        if (as_compile(lib, strlen(lib), &o, &on) == 0 && o) {
            f = fopen("disk/lib.o", "wb");
            if (f) {
                (void)fwrite(o, 1, on, f);
                (void)fclose(f);
            }
        }
        o = 0;
        on = 0;
        if (as_compile(main_s, strlen(main_s), &o, &on) == 0 && o) {
            f = fopen("disk/main.o", "wb");
            if (f) {
                (void)fwrite(o, 1, on, f);
                (void)fclose(f);
            }
        }
        paths[0] = "disk/main.o";
        paths[1] = "disk/lib.o";
        paths[2] = "disk/scat.o";
        paths[3] = "disk/schr.o";
        paths[4] = "disk/snul.o";
        paths[5] = "disk/scpy.o";
        paths[6] = "disk/stpc.o";
        paths[7] = "disk/sncmp.o";
        paths[8] = "disk/io.elf";
        if (ld_link(paths, 9, &elf, &en) != 0 || !elf || en < 64 ||
            elf[16] != 2) {
            fprintf(stderr, "FAIL ok m130-ldlib: ld_link\n");
            fails++;
        }
        {
            const char* badp[1];
            badp[0] = "disk/nope.o";
            if (ld_link(badp, 1, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m130: bad path accepted\n");
                fails++;
            }
        }
        {
            const char* emp[1];
            emp[0] = "";
            if (ld_link(emp, 1, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m130: empty path accepted\n");
                fails++;
            }
        }
        o = 0;
        on = 0;
        if (as_compile(nomain, strlen(nomain), &o, &on) == 0 && o) {
            f = fopen("disk/nomain.o", "wb");
            if (f) {
                (void)fwrite(o, 1, on, f);
                (void)fclose(f);
            }
            {
                const char* np[1];
                np[0] = "disk/nomain.o";
                if (ld_link(np, 1, &elf, &en) == 0) {
                    fprintf(stderr, "FAIL edge m130: missing main accepted\n");
                    fails++;
                }
            }
        }
    }
    {
        /* Restore bump+7 lib.o for m113 regression. */
        unsigned char* o = 0;
        size_t on = 0;
        FILE* f;
        const char* lib =
            "CXC_A2\n"
            "int add(int a, int b){\n"
            "return a+b;\n"
            "}\n"
            "int bump(int x){\n"
            "return add(x, 7);\n"
            "}\n";
        if (cc_compile_obj(lib, strlen(lib), &o, &on) == 0 && o && on >= 64) {
            f = fopen("disk/lib.o", "wb");
            if (f) {
                (void)fwrite(o, 1, on, f);
                (void)fclose(f);
            }
        }
    }
    if (expect_ok(
            "CXC_A2\n"
            "#lib \"lib.o\"\n"
            "#lib \"io.elf\"\n"
            "import fn bump(x);\n"
            "import fn cwrite(p,n);\n"
            "import fn strlen(s);\n"
            "fn main(){\n"
            "int n;\n"
            "int m;\n"
            "m=strlen(\"WMSG:OB\\n\");\n"
            "cwrite(\"WMSG:OB\\n\", m);\n"
            "n=bump(35);\n"
            "print n;\n"
            "return n;\n"
            "}\n",
            "m113-self-obj") != 0) {
        fails++;
    }
    if (expect_ok(
            "CXC_A2\n"
            "#lib \"rt.elf\"\n"
            "#lib \"crt.elf\"\n"
            "#lib \"io.elf\"\n"
            "import fn ans();\n"
            "import fn cwrite(p,n);\n"
            "import fn strlen(s);\n"
            "import fn memset(d,c,n);\n"
            "import fn memcmp(a,b,n);\n"
            "typedef int i32;\n"
            "fn main(){\n"
            "i32 n;\n"
            "i32 m;\n"
            "i32 d;\n"
            "buf[2];\n"
            "memset(&buf[0], 0, 16);\n"
            "d=memcmp(&buf[0], &buf[0], 16);\n"
            "m=strlen(\"WMSG:MS\\n\");\n"
            "cwrite(\"WMSG:MS\\n\", m);\n"
            "n=ans();\n"
            "print n;\n"
            "return n+d;\n"
            "}\n",
            "m112-musl") != 0) {
        fails++;
    }
    {
        unsigned char* o = 0;
        size_t on = 0;
        if (cc_compile_obj("CXC_A2\nfn main(){\nreturn 0;\n}\n",
                           strlen("CXC_A2\nfn main(){\nreturn 0;\n}\n"), &o,
                           &on) == 0) {
            fprintf(stderr, "FAIL edge bad-obj-main: expected reject\n");
            fails++;
        }
    }
    if (expect_ok(
            "CXC_A2\n"
            "#lib \"io.elf\"\n"
            "import fn strlen(s);\n"
            "fn main(){\n"
            "return strlen(\"abcd\");\n"
            "}\n",
            "m111-strlen") != 0) {
        fails++;
    }
    if (expect_ok(
            "CXC_A2\n"
            "#lib \"io.elf\"\n"
            "import fn memcpy(d,s,n);\n"
            "fn main(){\n"
            "a[2];\n"
            "b[2];\n"
            "a[0]=1;\n"
            "a[1]=2;\n"
            "memcpy(&b[0], &a[0], 16);\n"
            "return b[0]+b[1]+39;\n"
            "}\n",
            "m111-memcpy") != 0) {
        fails++;
    }
    if (expect_ok(
            "CXC_A2\n"
            "#lib \"io.elf\"\n"
            "import fn memset(d,c,n);\n"
            "import fn memcmp(a,b,n);\n"
            "fn main(){\n"
            "a[2];\n"
            "memset(&a[0], 0, 16);\n"
            "return memcmp(&a[0], &a[0], 16);\n"
            "}\n",
            "m112-memset-memcmp") != 0) {
        fails++;
    }
    if (expect_ok(
            "CXC_A2\n"
            "typedef int i32;\n"
            "i32 g=7;\n"
            "fn main(){\n"
            "i32 x=5;\n"
            "return x+g+30;\n"
            "}\n",
            "m112-typedef") != 0) {
        fails++;
    }
    if (expect_fail(
            "CXC_A2\ntypedef int i32;\ntypedef int i32;\nreturn 0;\n",
            "bad-dup-typedef") != 0) {
        fails++;
    }
    if (expect_ok(
            "CXC_A2\n"
            "a[3];\n"
            "a[0]=7;\n"
            "a[1]=14;\n"
            "a[2]=21;\n"
            "p=&a[0];\n"
            "n=p[0]+p[1]+p[2];\n"
            "return n;\n",
            "m101-ptr-idx") != 0) {
        fails++;
    }
    if (expect_fail(
            "CXC_A2\nstruct Pt { x; };\nPt p;\np.z=1;\nreturn 0;\n",
            "bad-field") != 0) {
        fails++;
    }
    if (expect_fail(
            "CXC_A2\n#include \"missing.cx\"\nreturn 0;\n",
            "bad-include") != 0) {
        fails++;
    }
    if (expect_fail(
            "CXC_A2\nfn main(a){\nreturn a;\n}\n",
            "bad-main-arity") != 0) {
        fails++;
    }
    if (expect_fail("CXC_A2\nint g=1;\nint g=2;\nreturn 0;\n",
                    "bad-dup-global") != 0) {
        fails++;
    }
    if (expect_fail(
            "CXC_A2\nswitch(1){\ndefault:\nbreak;\ndefault:\nbreak;\n}\n"
            "return 0;\n",
            "bad-dup-default") != 0) {
        fails++;
    }
    if (expect_fail(
            "CXC_A2\nenum { A=1, A=2 };\nreturn 0;\n",
            "bad-dup-enum") != 0) {
        fails++;
    }
    if (expect_fail(
            "CXC_A2\nextern fn missing(a);\nfn main(){\nreturn missing(1);\n}\n",
            "bad-unresolved") != 0) {
        fails++;
    }
    if (expect_fail(
            "CXC_A2\nimport fn add(a,b);\nfn main(){\nreturn add(1,2);\n}\n",
            "bad-nolib") != 0) {
        fails++;
    }
    if (expect_fail(
            "CXC_A2\n"
            "#lib \"missing.elf\"\n"
            "import fn add(a,b);\n"
            "fn main(){\nreturn add(1,2);\n}\n",
            "bad-missing-lib") != 0) {
        fails++;
    }
    if (expect_fail(
            "CXC_A2\n"
            "#lib \"rt.elf\"\n"
            "import fn mac(a,b,c);\n"
            "fn main(){\nreturn mac(1,2);\n}\n",
            "bad-mac-arity") != 0) {
        fails++;
    }
    if (expect_fail(
            "CXC_A2\n"
            "#lib \"crt.elf\"\n"
            "import fn ans();\n"
            "fn main(){\nreturn ans();\n}\n",
            "bad-cross-und") != 0) {
        fails++;
    }
    if (expect_fail(
            "CXC_A2\n"
            "fn main(){\nreturn \"bad\\q\";\n}\n",
            "bad-escape") != 0) {
        fails++;
    }
    if (expect_fail("CXC_A2\n*missing=1;\nreturn 0;\n", "bad-deref") != 0) {
        fails++;
    }
    if (expect_fail("CXC_A2\ninput ;\n", "bad-input") != 0) {
        fails++;
    }
    if (expect_fail("CXC_A2\nprint ;\n", "bad-print") != 0) {
        fails++;
    }
    if (expect_fail(
            "CXC_A2\nfn add(a,b){ return a+b; }\nn=add(1);\nreturn n;\n",
            "bad-arity") != 0) {
        fails++;
    }
    if (expect_fail("CXC_A2\nn=missing(1);\nreturn n;\n", "bad-call") != 0) {
        fails++;
    }
    if (expect_fail("CXC_A2\nreturn ;\n", "bad-return") != 0) {
        fails++;
    }
    if (expect_fail("CXC_A2\ni=;\n", "bad-assign") != 0) {
        fails++;
    }
    if (expect_fail("CXC_A2\nwhile(1){ break\n", "bad-semi") != 0) {
        fails++;
    }
    if (expect_fail("CXC_A2\nbreak;\n", "bare-break") != 0) {
        fails++;
    }
    if (expect_fail("notmagic\nreturn 1;\n", "bad-magic") != 0) {
        fails++;
    }
    if (expect_fail("CXC_A2\n{\n", "unclosed") != 0) {
        fails++;
    }
    if (expect_fail("CXC_A2\na[0];\n", "zero-array") != 0) {
        fails++;
    }

    if (fails != 0) {
        fprintf(stderr, "cc_host_test: %d failures\n", fails);
        return 1;
    }
    puts("cc_host_test: PASS");
    return 0;
}
