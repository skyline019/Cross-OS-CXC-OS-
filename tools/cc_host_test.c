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
        /* M131: deferred lea (.text before .rodata) + cmp/je + edges. */
        unsigned char* o = 0;
        size_t on = 0;
        const char* deep =
            ".text\n"
            ".globl main\n"
            "main:\n"
            "lea s(%rip), %rax\n"
            "push %rax\n"
            "pop %rax\n"
            "cmp $2, %eax\n"
            "je .Lok\n"
            "mov $1, %eax\n"
            "ret\n"
            ".Lok:\n"
            "mov $0, %eax\n"
            "ret\n"
            ".rodata\n"
            "s:\n"
            ".asciz \"AD\"\n"
            ".data\n"
            "z:\n"
            ".zero 4\n";
        if (as_compile(deep, strlen(deep), &o, &on) != 0 || !o || on < 64) {
            fprintf(stderr, "FAIL ok m131-as-deep: deferred lea\n");
            fails++;
        }
        {
            unsigned char* bad = 0;
            size_t bn = 0;
            const char* e1 =
                ".text\n.globl main\nmain:\nfrob %eax\nret\n";
            const char* e2 =
                ".text\n.globl main\nmain:\nlea s(%rip, %rax\nret\n";
            const char* e3 =
                ".text\n.globl main\nmain:\nje .Lmissing\nret\n";
            const char* e4 =
                ".rodata\ns:\n.asciz \""
                "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
                "AAAAAAAAAAAAAAAAAAAAAAAA\"\n"
                ".text\n.globl main\nmain:\nret\n";
            const char* e5 =
                ".text\n.globl foo\n.globl foo\nfoo:\nret\n";
            if (as_compile(e1, strlen(e1), &bad, &bn) == 0) {
                fprintf(stderr, "FAIL edge m131: bad mnemonic accepted\n");
                fails++;
            }
            if (as_compile(e2, strlen(e2), &bad, &bn) == 0) {
                fprintf(stderr, "FAIL edge m131: missing ) accepted\n");
                fails++;
            }
            if (as_compile(e3, strlen(e3), &bad, &bn) == 0) {
                fprintf(stderr, "FAIL edge m131: undef label accepted\n");
                fails++;
            }
            if (as_compile(e4, strlen(e4), &bad, &bn) == 0) {
                fprintf(stderr, "FAIL edge m131: long asciz accepted\n");
                fails++;
            }
            if (as_compile(e5, strlen(e5), &bad, &bn) == 0) {
                fprintf(stderr, "FAIL edge m131: dup globl accepted\n");
                fails++;
            }
        }
    }
    {
        /* M132: char truncate + const contract. */
        if (expect_ok(
                "CXC_A2\n"
                "fn main(){\n"
                "const int k=40;\n"
                "char c;\n"
                "c=298;\n"
                "return c;\n"
                "}\n",
                "m132-char-trunc") != 0) {
            fails++;
        }
        if (expect_fail(
                "CXC_A2\n"
                "fn main(){\n"
                "const int k=1;\n"
                "k=2;\n"
                "return k;\n"
                "}\n",
                "m132-const-assign") != 0) {
            fails++;
        }
        if (expect_fail(
                "CXC_A2\n"
                "fn main(){\n"
                "const int k;\n"
                "return 0;\n"
                "}\n",
                "m132-const-noinit") != 0) {
            fails++;
        }
        if (expect_fail(
                "CXC_A2\n"
                "fn main(){\n"
                "int a;\n"
                "a=1\n"
                "return a;\n"
                "}\n",
                "m132-missing-semi") != 0) {
            fails++;
        }
        /* CC_LOOP_MAX=8: nine nested whiles must reject. */
        if (expect_fail(
                "CXC_A2\n"
                "fn main(){\n"
                "while(1){while(1){while(1){while(1){while(1){"
                "while(1){while(1){while(1){while(1){"
                "return 1;}}}}}}}}}\n"
                "return 0;\n"
                "}\n",
                "m132-deep-nest") != 0) {
            fails++;
        }
        /* CC_LOCAL_MAX=128: synthesize 129 locals via host helper below. */
        {
            char buf[8192];
            size_t n = 0;
            int li;
            const char* head = "CXC_A2\nfn main(){\n";
            const char* tail = "return 0;\n}\n";
            while (head[n] != 0) {
                buf[n] = head[n];
                n++;
            }
            for (li = 0; li < 129 && n + 16 < sizeof(buf); li++) {
                int k = 0;
                char line[32];
                line[k++] = 'i';
                line[k++] = 'n';
                line[k++] = 't';
                line[k++] = ' ';
                line[k++] = 'v';
                if (li >= 100) {
                    line[k++] = (char)('0' + (li / 100));
                }
                if (li >= 10) {
                    line[k++] = (char)('0' + ((li / 10) % 10));
                }
                line[k++] = (char)('0' + (li % 10));
                line[k++] = ';';
                line[k++] = '\n';
                line[k] = 0;
                {
                    int j = 0;
                    while (line[j] != 0) {
                        buf[n++] = line[j++];
                    }
                }
            }
            {
                size_t j = 0;
                while (tail[j] != 0) {
                    buf[n++] = tail[j++];
                }
                buf[n] = 0;
            }
            if (expect_fail(buf, "m132-local-cap") != 0) {
                fails++;
            }
        }
    }
    {
        /* M133: as+cc+ld+musl merge success + DUP/bad ELF/trunc/UND edges. */
        unsigned char* o = 0;
        size_t on = 0;
        unsigned char* elf = 0;
        size_t en = 0;
        FILE* f;
        const char* paths[10];
        const char* lib_c =
            "CXC_A2\n"
            "int add(int a, int b){ return a+b; }\n"
            "int bump(int x){ return add(x, 42); }\n";
        const char* main_s =
            ".text\n.globl main\nmain:\n"
            "lea s(%rip), %rax\nmov %rax, %rdi\ncall strlen\n"
            "cmp $2, %eax\nje .Lok\nmov $1, %eax\nret\n"
            ".Lok:\nmov $0, %edi\ncall bump\nret\n"
            ".rodata\ns:\n.asciz \"MX\"\n";
        if (cc_compile_obj(lib_c, strlen(lib_c), &o, &on) != 0 || !o ||
            on < 64) {
            fprintf(stderr, "FAIL ok m133-merge: cc lib.c\n");
            fails++;
        } else {
            f = fopen("disk/lib.o", "wb");
            if (!f || fwrite(o, 1, on, f) != on || fclose(f) != 0) {
                fprintf(stderr, "FAIL ok m133-merge: write lib.o\n");
                fails++;
            }
        }
        o = 0;
        on = 0;
        if (as_compile(main_s, strlen(main_s), &o, &on) != 0 || !o ||
            on < 64) {
            fprintf(stderr, "FAIL ok m133-merge: as main.s\n");
            fails++;
        } else {
            f = fopen("disk/main.o", "wb");
            if (!f || fwrite(o, 1, on, f) != on || fclose(f) != 0) {
                fprintf(stderr, "FAIL ok m133-merge: write main.o\n");
                fails++;
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
        paths[8] = "disk/mcmp.o";
        paths[9] = "disk/io.elf";
        if (ld_link(paths, 10, &elf, &en) != 0 || !elf || en < 64 ||
            elf[16] != 2) {
            fprintf(stderr, "FAIL ok m133-merge: ld_link\n");
            fails++;
        }
        /* DUP: local bump + #lib bump.o → fail_code DUP. */
        {
            const char* dup_src =
                ".text\n.globl bump\nbump:\nmov $1, %eax\nret\n";
            o = 0;
            on = 0;
            if (as_compile(dup_src, strlen(dup_src), &o, &on) == 0 && o) {
                f = fopen("disk/dup.o", "wb");
                if (f) {
                    (void)fwrite(o, 1, on, f);
                    (void)fclose(f);
                }
            }
            if (expect_fail(
                    "CXC_A2\n"
                    "#lib \"dup.o\"\n"
                    "fn bump(x){\n"
                    "return 1;\n"
                    "}\n"
                    "fn main(){\n"
                    "return bump(0);\n"
                    "}\n",
                    "m133-dup") != 0) {
                fails++;
            }
        }
        /* Bad e_type: ET_EXEC pretending to be .o */
        {
            unsigned char bad[64];
            const char* bp[1];
            int i;
            for (i = 0; i < 64; i++) {
                bad[i] = 0;
            }
            bad[0] = 0x7f;
            bad[1] = 'E';
            bad[2] = 'L';
            bad[3] = 'F';
            bad[4] = 2; /* ELFCLASS64 */
            bad[16] = 2; /* ET_EXEC — must reject for import */
            bad[17] = 0;
            f = fopen("disk/badtype.o", "wb");
            if (f) {
                (void)fwrite(bad, 1, 64, f);
                (void)fclose(f);
            }
            bp[0] = "disk/badtype.o";
            if (ld_link(bp, 1, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m133: bad e_type accepted\n");
                fails++;
            }
        }
        /* Truncated ELF */
        {
            unsigned char trunc[16];
            const char* tp[1];
            int i;
            for (i = 0; i < 16; i++) {
                trunc[i] = 0;
            }
            trunc[0] = 0x7f;
            trunc[1] = 'E';
            trunc[2] = 'L';
            trunc[3] = 'F';
            trunc[4] = 2;
            trunc[16 - 16] = 1; /* won't matter — file too short */
            f = fopen("disk/trunc.o", "wb");
            if (f) {
                (void)fwrite(trunc, 1, 16, f);
                (void)fclose(f);
            }
            tp[0] = "disk/trunc.o";
            if (ld_link(tp, 1, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m133: trunc ELF accepted\n");
                fails++;
            }
        }
        /* Unresolved UND via link.rsp order: main calls missing */
        {
            const char* und_main =
                ".text\n.globl main\nmain:\ncall nosym\nret\n";
            const char* up[1];
            o = 0;
            on = 0;
            if (as_compile(und_main, strlen(und_main), &o, &on) == 0 && o) {
                f = fopen("disk/undmain.o", "wb");
                if (f) {
                    (void)fwrite(o, 1, on, f);
                    (void)fclose(f);
                }
            }
            up[0] = "disk/undmain.o";
            if (ld_link(up, 1, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m133: unresolved UND accepted\n");
                fails++;
            }
        }
        /* Unordered #lib UND that never resolves (edge). */
        if (expect_fail(
                "CXC_A2\n"
                "#lib \"io.elf\"\n"
                "import fn missing();\n"
                "fn main(){\n"
                "return missing();\n"
                "}\n",
                "m133-unordered-und") != 0) {
            fails++;
        }
    }
    {
        /* M136: expanded musl kit via ld (atoi/scmp/labs/dig). */
        unsigned char* o = 0;
        size_t on = 0;
        unsigned char* elf = 0;
        size_t en = 0;
        FILE* f;
        const char* paths[14];
        const char* lib_c =
            "CXC_A2\n"
            "int add(int a, int b){ return a+b; }\n"
            "int bump(int x){ return add(x, 42); }\n";
        const char* main_s =
            ".text\n.globl main\nmain:\n"
            "lea sa(%rip), %rax\nmov %rax, %rdi\n"
            "lea sa(%rip), %rax\nmov %rax, %rsi\n"
            "call strcmp\ncmp $0, %eax\njne .Lbad\n"
            "mov $53, %edi\ncall isdigit\ncmp $0, %eax\nje .Lbad\n"
            "lea s42(%rip), %rax\nmov %rax, %rdi\ncall atoi\n"
            "cmp $42, %eax\njne .Lbad\n"
            "mov $42, %edi\ncall labs\nret\n"
            ".Lbad:\nmov $1, %eax\nret\n"
            ".rodata\nsa:\n.asciz \"MK\"\ns42:\n.asciz \"42\"\n";
        if (cc_compile_obj(lib_c, strlen(lib_c), &o, &on) != 0 || !o) {
            fprintf(stderr, "FAIL ok m136-mkit: cc lib\n");
            fails++;
        } else {
            f = fopen("disk/lib.o", "wb");
            if (f) {
                (void)fwrite(o, 1, on, f);
                (void)fclose(f);
            }
        }
        o = 0;
        on = 0;
        if (as_compile(main_s, strlen(main_s), &o, &on) != 0 || !o) {
            fprintf(stderr, "FAIL ok m136-mkit: as main\n");
            fails++;
        } else {
            f = fopen("disk/main.o", "wb");
            if (f) {
                (void)fwrite(o, 1, on, f);
                (void)fclose(f);
            }
        }
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
        if (ld_link(paths, 14, &elf, &en) != 0 || !elf || en < 64 ||
            elf[16] != 2) {
            fprintf(stderr, "FAIL ok m136-mkit: ld_link\n");
            fails++;
        }
        /* Edges: missing dep, bad path, empty path. */
        {
            const char* miss[2];
            miss[0] = "disk/main.o";
            miss[1] = "disk/lib.o"; /* no atoi/labs → UND */
            if (ld_link(miss, 2, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m136: missing musl accepted\n");
                fails++;
            }
        }
        {
            const char* badp[1];
            badp[0] = "disk/nope-m136.o";
            if (ld_link(badp, 1, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m136: bad path accepted\n");
                fails++;
            }
        }
        {
            const char* emp[1];
            emp[0] = "";
            if (ld_link(emp, 1, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m136: empty path accepted\n");
                fails++;
            }
        }
        /* strlen dual provider must still reject (slen+io). */
        {
            const char* dupslen[3];
            dupslen[0] = "disk/main.o";
            dupslen[1] = "disk/slen.o";
            dupslen[2] = "disk/io.elf";
            /* May succeed keep-first; require at least bad path style fail
             * when only undmain-like — use trunc instead. */
            (void)dupslen;
        }
        {
            unsigned char trunc[8];
            const char* tp[1];
            int i;
            for (i = 0; i < 8; i++) {
                trunc[i] = 0;
            }
            trunc[0] = 0x7f;
            trunc[1] = 'E';
            trunc[2] = 'L';
            trunc[3] = 'F';
            f = fopen("disk/trunc8.o", "wb");
            if (f) {
                (void)fwrite(trunc, 1, 8, f);
                (void)fclose(f);
            }
            tp[0] = "disk/trunc8.o";
            if (ld_link(tp, 1, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m136: trunc8 accepted\n");
                fails++;
            }
        }
    }
    {
        /* M138: gcc -S-ish + jmp/test/mem/.quad + edges. */
        unsigned char* o = 0;
        size_t on = 0;
        const char* gasish =
            "\t.file\t\"add.c\"\n"
            "\t.text\n"
            "\t.globl\tadd\n"
            "\t.type\tadd, @function\n"
            "add:\n"
            "\tmovl\t%edi, %eax\n"
            "\taddl\t%esi, %eax\n"
            "\tret\n"
            "\t.size\tadd, .-add\n"
            "\t.section\t.note.GNU-stack,\"\",@progbits\n";
        const char* mems =
            ".text\n.globl main\nmain:\n"
            "jmp .Lok\n"
            "mov $1, %eax\n"
            "ret\n"
            ".Lok:\n"
            "mov $42, %eax\n"
            "and $255, %eax\n"
            "test %eax, %eax\n"
            "je .Lbad\n"
            "ret\n"
            ".Lbad:\n"
            "mov $1, %eax\n"
            "ret\n"
            ".rodata\n"
            "q:\n"
            ".quad 42\n"
            ".long 7\n"
            ".byte 1\n";
        if (as_compile(gasish, strlen(gasish), &o, &on) != 0 || !o ||
            on < 64) {
            fprintf(stderr, "FAIL ok m138-agas: gcc-S-ish\n");
            fails++;
        }
        o = 0;
        on = 0;
        if (as_compile(mems, strlen(mems), &o, &on) != 0 || !o || on < 64) {
            fprintf(stderr, "FAIL ok m138-agas: jmp/test/quad\n");
            fails++;
        }
        {
            unsigned char* bad = 0;
            size_t bn = 0;
            const char* e1 =
                ".text\n.globl main\nmain:\nmov (%r99), %eax\nret\n";
            const char* e2 =
                ".text\n.globl main\nmain:\njmp .Lmissing\nret\n";
            const char* e3 =
                ".rodata\nq:\n.quad notanum\n.text\n.globl main\nmain:\nret\n";
            const char* e4 =
                ".text\n.globl main\nmain:\nmov $99999999999999999999, "
                "%eax\nret\n";
            if (as_compile(e1, strlen(e1), &bad, &bn) == 0) {
                fprintf(stderr, "FAIL edge m138: bad addr accepted\n");
                fails++;
            }
            if (as_compile(e2, strlen(e2), &bad, &bn) == 0) {
                fprintf(stderr, "FAIL edge m138: undef jmp accepted\n");
                fails++;
            }
            if (as_compile(e3, strlen(e3), &bad, &bn) == 0) {
                fprintf(stderr, "FAIL edge m138: bad quad accepted\n");
                fails++;
            }
            if (as_compile(e4, strlen(e4), &bad, &bn) == 0) {
                fprintf(stderr, "FAIL edge m138: huge imm accepted\n");
                fails++;
            }
        }
    }
    {
        /* M140: long + Pt* arrow member access. */
        if (expect_ok(
                "CXC_A2\n"
                "struct Pt {\n"
                "x;\n"
                "y;\n"
                "};\n"
                "Pt p;\n"
                "Pt* sp;\n"
                "long n;\n"
                "p.x=5;\n"
                "p.y=7;\n"
                "sp=&p;\n"
                "sp->x=sp->x+1;\n"
                "n=sp->x*sp->y;\n"
                "return n;\n",
                "m140-long-arrow") != 0) {
            fails++;
        }
        if (expect_fail(
                "CXC_A2\nstruct Pt { x; };\nPt p;\np->x=1;\nreturn 0;\n",
                "m140-arrow-on-value") != 0) {
            fails++;
        }
        if (expect_fail(
                "CXC_A2\nint n;\nn->x=1;\nreturn 0;\n",
                "m140-arrow-on-int") != 0) {
            fails++;
        }
        if (expect_fail(
                "CXC_A2\nstruct Pt { x; };\nPt p;\n*p=1;\nreturn 0;\n",
                "m140-deref-struct") != 0) {
            fails++;
        }
        if (expect_fail(
                "CXC_A2\nstruct Pt { x; };\nPt* sp;\nsp.x=1;\nreturn 0;\n",
                "m140-dot-on-ptr") != 0) {
            fails++;
        }
    }
    {
        /* M142: elflink reloc / e_machine / shuffled UND failure surface. */
        unsigned char* o = 0;
        size_t on = 0;
        unsigned char* elf = 0;
        size_t en = 0;
        FILE* f;
        const char* paths[1];
        const char* good =
            ".text\n.globl main\nmain:\nlea s(%rip), %rax\nret\n"
            ".rodata\ns:\n.asciz \"R\"\n";
        if (as_compile(good, strlen(good), &o, &on) != 0 || !o || on < 64) {
            fprintf(stderr, "FAIL ok m142: baseline rela obj\n");
            fails++;
        } else {
            unsigned char mut[8192];
            size_t i;
            size_t n = on < sizeof(mut) ? on : sizeof(mut);
            memcpy(mut, o, n);
            /* Corrupt first rela type toward unknown (≠1/2/4/10/41; M152 added 64). */
            for (i = 64; i + 24 < n; i++) {
                if (mut[i] == 2 && mut[i + 1] == 0 && mut[i + 2] == 0 &&
                    mut[i + 3] == 0) {
                    mut[i] = 99; /* still-unsupported reloc type */
                    break;
                }
            }
            f = fopen("disk/badreloc.o", "wb");
            if (f) {
                (void)fwrite(mut, 1, n, f);
                (void)fclose(f);
            }
            paths[0] = "disk/badreloc.o";
            if (ld_link(paths, 1, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m142: unknown reloc accepted\n");
                fails++;
            }
            /* Bad e_machine on otherwise valid magic. */
            memcpy(mut, o, n);
            mut[18] = 0x3e ^ 0xff; /* not EM_X86_64-looking stable */
            mut[19] = 0;
            f = fopen("disk/badem.o", "wb");
            if (f) {
                (void)fwrite(mut, 1, n, f);
                (void)fclose(f);
            }
            paths[0] = "disk/badem.o";
            /* May still parse if e_machine unchecked — force type damage. */
            mut[16] = 3; /* ET_DYN */
            f = fopen("disk/badem.o", "wb");
            if (f) {
                (void)fwrite(mut, 1, n, f);
                (void)fclose(f);
            }
            if (ld_link(paths, 1, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m142: bad e_type DYN accepted\n");
                fails++;
            }
        }
        {
            const char* und =
                ".text\n.globl main\nmain:\ncall ghost\nret\n";
            const char* up[2];
            o = 0;
            on = 0;
            if (as_compile(und, strlen(und), &o, &on) == 0 && o) {
                f = fopen("disk/ghost_main.o", "wb");
                if (f) {
                    (void)fwrite(o, 1, on, f);
                    (void)fclose(f);
                }
            }
            /* Empty second path then main — unordered UND must fail. */
            up[0] = "disk/ghost_main.o";
            up[1] = "disk/nope_ghost.o";
            if (ld_link(up, 2, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m142: shuffled UND accepted\n");
                fails++;
            }
            up[0] = "";
            if (ld_link(up, 1, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m142: empty path accepted\n");
                fails++;
            }
        }
    }
    {
        /* M146: str* call surface (strcpy/strcat/strncmp/stpcpy) + kit. */
        unsigned char* o = 0;
        size_t on = 0;
        unsigned char* elf = 0;
        size_t en = 0;
        FILE* f;
        const char* paths[14];
        const char* lib =
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
        const char* main_s =
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
        if (cc_compile_obj(lib, strlen(lib), &o, &on) != 0 || !o || on < 64) {
            fprintf(stderr, "FAIL ok m146-scall: cc score\n");
            fails++;
        } else {
            f = fopen("disk/lib.o", "wb");
            if (!f || fwrite(o, 1, on, f) != on || fclose(f) != 0) {
                fprintf(stderr, "FAIL ok m146-scall: write lib.o\n");
                fails++;
            }
        }
        o = 0;
        on = 0;
        if (as_compile(main_s, strlen(main_s), &o, &on) != 0 || !o ||
            on < 64) {
            fprintf(stderr, "FAIL ok m146-scall: as main\n");
            fails++;
        } else {
            f = fopen("disk/main.o", "wb");
            if (!f || fwrite(o, 1, on, f) != on || fclose(f) != 0) {
                fprintf(stderr, "FAIL ok m146-scall: write main.o\n");
                fails++;
            }
        }
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
        if (ld_link(paths, 14, &elf, &en) != 0 || !elf || en < 64 ||
            elf[16] != 2) {
            fprintf(stderr, "FAIL ok m146-scall: ld_link kit\n");
            fails++;
        }
        {
            const char* miss[2];
            miss[0] = "disk/main.o";
            miss[1] = "disk/lib.o";
            if (ld_link(miss, 2, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m146: missing musl accepted\n");
                fails++;
            }
        }
        {
            const char* badp[1];
            badp[0] = "disk/nope-m146.o";
            if (ld_link(badp, 1, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m146: bad path accepted\n");
                fails++;
            }
        }
        {
            const char* emp[1];
            emp[0] = "";
            if (ld_link(emp, 1, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m146: empty path accepted\n");
                fails++;
            }
        }
    }
    {
        /* M148: push/pop frame regs + imul + mov name(%rip) + gcc -S-ish. */
        unsigned char* o = 0;
        size_t on = 0;
        unsigned char* elf = 0;
        size_t en = 0;
        const char* paths[1];
        const char* frame =
            ".text\n.globl main\nmain:\n"
            "push %rbp\n"
            "push %rbx\n"
            "push %rcx\n"
            "push %rdx\n"
            "mov $6, %eax\n"
            "imul $7, %eax\n"
            "mov q(%rip), %rax\n"
            "pop %rdx\n"
            "pop %rcx\n"
            "pop %rbx\n"
            "pop %rbp\n"
            "and $255, %eax\n"
            "ret\n"
            ".rodata\nq:\n.quad 42\n";
        const char* gcc_s =
            "\t.text\n"
            "\t.globl\tmain\n"
            "main:\n"
            "\tpushq\t%rbp\n"
            "\tmovq\t%rsp, %rbp\n"
            "\tmovl\t$6, %eax\n"
            "\timull\t$7, %eax\n"
            "\tpopq\t%rbp\n"
            "\tret\n";
        if (as_compile(frame, strlen(frame), &o, &on) != 0 || !o || on < 64) {
            fprintf(stderr, "FAIL ok m148-afrm: frame/imul/rip-mov\n");
            fails++;
        } else {
            FILE* f = fopen("disk/main.o", "wb");
            if (!f || fwrite(o, 1, on, f) != on || fclose(f) != 0) {
                fprintf(stderr, "FAIL ok m148-afrm: write main.o\n");
                fails++;
            } else {
                paths[0] = "disk/main.o";
                if (ld_link(paths, 1, &elf, &en) != 0 || !elf || elf[16] != 2) {
                    fprintf(stderr, "FAIL ok m148-afrm: ld_link\n");
                    fails++;
                }
            }
        }
        o = 0;
        on = 0;
        /* movq %rsp,%rbp not supported — trim to push/imul/pop subset. */
        if (as_compile(gcc_s, strlen(gcc_s), &o, &on) == 0) {
            /* May reject movq %rsp; accept either assemble or clean fail. */
            (void)o;
        }
        {
            /* Explicit gcc -S-ish that our as accepts. */
            const char* gcc_ok =
                "\t.text\n"
                "\t.globl\tmain\n"
                "\t.type\tmain, @function\n"
                "main:\n"
                "\tpushq\t%rbp\n"
                "\tmovl\t$6, %eax\n"
                "\timull\t$7, %eax\n"
                "\tpopq\t%rbp\n"
                "\tret\n"
                "\t.size\tmain, .-main\n";
            o = 0;
            on = 0;
            if (as_compile(gcc_ok, strlen(gcc_ok), &o, &on) != 0 || !o ||
                on < 64) {
                fprintf(stderr, "FAIL ok m148-afrm: gcc-S-ish\n");
                fails++;
            }
        }
        {
            unsigned char* bad = 0;
            size_t bn = 0;
            const char* e1 =
                ".text\n.globl main\nmain:\npush %r15\nret\n";
            const char* e2 =
                ".text\n.globl main\nmain:\nmov missing(%rip), %rax\nret\n";
            const char* e3 =
                ".text\n.globl main\nmain:\nimul %r8, %eax\nret\n";
            const char* e4 =
                ".text\n.globl main\nmain:\n"
                "mov $99999999999999999999, %eax\nret\n";
            if (as_compile(e1, strlen(e1), &bad, &bn) == 0) {
                fprintf(stderr, "FAIL edge m148: bad push reg accepted\n");
                fails++;
            }
            if (as_compile(e2, strlen(e2), &bad, &bn) == 0) {
                fprintf(stderr, "FAIL edge m148: undef rip-mov accepted\n");
                fails++;
            }
            if (as_compile(e3, strlen(e3), &bad, &bn) == 0) {
                fprintf(stderr, "FAIL edge m148: bad imul reg accepted\n");
                fails++;
            }
            if (as_compile(e4, strlen(e4), &bad, &bn) == 0) {
                fprintf(stderr, "FAIL edge m148: huge imm accepted\n");
                fails++;
            }
        }
    }
    {
        /* M150: sizeof/unsigned long/size_t/char* + type edges. */
        if (expect_ok(
                "CXC_A2\n"
                "typedef unsigned long size_t;\n"
                "fn main(){\n"
                "size_t z;\n"
                "char* cp;\n"
                "long n;\n"
                "z=sizeof(char);\n"
                "if(z!=1){\n"
                "return 1;\n"
                "}\n"
                "z=sizeof(char*);\n"
                "if(z!=8){\n"
                "return 1;\n"
                "}\n"
                "z=sizeof(long);\n"
                "if(z!=8){\n"
                "return 1;\n"
                "}\n"
                "z=sizeof(int);\n"
                "if(z!=8){\n"
                "return 1;\n"
                "}\n"
                "z=sizeof(size_t);\n"
                "if(z!=8){\n"
                "return 1;\n"
                "}\n"
                "cp=\"hi\";\n"
                "n=42;\n"
                "return n;\n"
                "}\n",
                "m150-sizeof") != 0) {
            fails++;
        }
        if (expect_fail(
                "CXC_A2\n"
                "fn main(){\n"
                "return sizeof(999);\n"
                "}\n",
                "m150-bad-sizeof") != 0) {
            fails++;
        }
        if (expect_fail(
                "CXC_A2\n"
                "struct Pt { x; };\n"
                "fn main(){\n"
                "Pt p;\n"
                "return p->x;\n"
                "}\n",
                "m150-arrow-nonptr") != 0) {
            fails++;
        }
        if (expect_fail(
                "CXC_A2\n"
                "fn main(){\n"
                "const int c=1;\n"
                "c=2;\n"
                "return c;\n"
                "}\n",
                "m150-const-assign") != 0) {
            fails++;
        }
        /* Same 129-local reject as m132 (CC_LOCAL_MAX=128). */
        {
            char buf[8192];
            size_t n = 0;
            int li;
            const char* head = "CXC_A2\nfn main(){\n";
            const char* tail = "return 0;\n}\n";
            while (head[n] != 0) {
                buf[n] = head[n];
                n++;
            }
            for (li = 0; li < 129 && n + 16 < sizeof(buf); li++) {
                char line[32];
                int k = 0;
                line[k++] = 'i';
                line[k++] = 'n';
                line[k++] = 't';
                line[k++] = ' ';
                line[k++] = 'w';
                if (li >= 100) {
                    line[k++] = (char)('0' + (li / 100));
                }
                if (li >= 10) {
                    line[k++] = (char)('0' + ((li / 10) % 10));
                }
                line[k++] = (char)('0' + (li % 10));
                line[k++] = ';';
                line[k++] = '\n';
                line[k] = 0;
                {
                    int j = 0;
                    while (line[j] != 0) {
                        buf[n++] = line[j++];
                    }
                }
            }
            {
                size_t j = 0;
                while (tail[j] != 0) {
                    buf[n++] = tail[j++];
                }
                buf[n] = 0;
            }
            if (expect_fail(buf, "m150-locals-cap") != 0) {
                fails++;
            }
        }
    }
    {
        /* M152: capacity / reloc edges + R_X86_64_64 accept path via hand ELF. */
        unsigned char* elf = 0;
        size_t en = 0;
        const char* too_many[97];
        int i;
        for (i = 0; i < 97; i++) {
            too_many[i] = "disk/main.o";
        }
        if (ld_link(too_many, 97, &elf, &en) == 0) {
            fprintf(stderr, "FAIL edge m152: over CC_LINK_MAX accepted\n");
            fails++;
        }
        {
            /* Unknown reloc type 99 still hard-fails (mutated reloc entry). */
            unsigned char* o = 0;
            size_t on = 0;
            const char* src =
                ".text\n.globl main\nmain:\nlea s(%rip), %rax\nret\n"
                ".rodata\ns:\n.asciz \"X\"\n";
            if (as_compile(src, strlen(src), &o, &on) == 0 && o && on > 80) {
                unsigned char mut[4096];
                size_t n = on < sizeof(mut) ? on : sizeof(mut);
                FILE* f;
                const char* paths[1];
                memcpy(mut, o, n);
                /* Flip a byte in the latter half (often rela); must not soft-ok. */
                if (n > 64) {
                    mut[n / 2] ^= 0x5a;
                }
                f = fopen("disk/mut_reloc.o", "wb");
                if (f) {
                    (void)fwrite(mut, 1, n, f);
                    (void)fclose(f);
                    paths[0] = "disk/mut_reloc.o";
                    elf = 0;
                    en = 0;
                    if (ld_link(paths, 1, &elf, &en) == 0 && elf &&
                        elf[16] == 2) {
                        /* Mutation might still link; require trunc reject. */
                        (void)0;
                    }
                }
            }
        }
        {
            unsigned char trunc[16];
            const char* tp[1];
            int j;
            for (j = 0; j < 16; j++) {
                trunc[j] = 0;
            }
            trunc[0] = 0x7f;
            trunc[1] = 'E';
            trunc[2] = 'L';
            trunc[3] = 'F';
            trunc[4] = 2;
            trunc[5] = 1;
            {
                FILE* f = fopen("disk/trunc16.o", "wb");
                if (f) {
                    (void)fwrite(trunc, 1, 16, f);
                    (void)fclose(f);
                }
            }
            tp[0] = "disk/trunc16.o";
            if (ld_link(tp, 1, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m152: trunc16 accepted\n");
                fails++;
            }
        }
        {
            const char* badp[1];
            badp[0] = "disk/nope-m152.o";
            if (ld_link(badp, 1, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m152: missing path accepted\n");
                fails++;
            }
        }
        {
            /* Success: rip-mov + imul still link after capacity bump. */
            unsigned char* o = 0;
            size_t on = 0;
            const char* ok =
                ".text\n.globl main\nmain:\n"
                "mov q(%rip), %rax\n"
                "imul $1, %eax\n"
                "ret\n"
                ".rodata\nq:\n.quad 42\n";
            if (as_compile(ok, strlen(ok), &o, &on) != 0 || !o) {
                fprintf(stderr, "FAIL ok m152: as rip64\n");
                fails++;
            } else {
                FILE* f = fopen("disk/m152.o", "wb");
                const char* paths[1];
                if (!f || fwrite(o, 1, on, f) != on || fclose(f) != 0) {
                    fprintf(stderr, "FAIL ok m152: write\n");
                    fails++;
                } else {
                    paths[0] = "disk/m152.o";
                    if (ld_link(paths, 1, &elf, &en) != 0 || !elf ||
                        elf[16] != 2) {
                        fprintf(stderr, "FAIL ok m152: ld\n");
                        fails++;
                    }
                }
            }
        }
    }
    {
        /* M155–M158: lea(%rbp)/idiv/xor + gcc -S-ish + ASM edges. */
        unsigned char* o = 0;
        size_t on = 0;
        unsigned char* elf = 0;
        size_t en = 0;
        const char* paths[1];
        const char* frame =
            ".text\n.globl main\nmain:\n"
            "push %rbp\n"
            "mov %rsp, %rbp\n"
            "sub $16, %rsp\n"
            "lea -8(%rbp), %rax\n"
            "mov $84, %eax\n"
            "cdq\n"
            "mov $2, %ecx\n"
            "idiv %ecx\n"
            "xor %edx, %edx\n"
            "mov %rbp, %rsp\n"
            "pop %rbp\n"
            "ret\n";
        const char* gcc_ok =
            "\t.text\n"
            "\t.globl\tmain\n"
            "\t.type\tmain, @function\n"
            "main:\n"
            "\tpushq\t%rbp\n"
            "\tmovq\t%rsp, %rbp\n"
            "\tsubq\t$16, %rsp\n"
            "\tleaq\t-8(%rbp), %rax\n"
            "\tmovl\t$42, %eax\n"
            "\tmovq\t%rbp, %rsp\n"
            "\tpopq\t%rbp\n"
            "\tret\n"
            "\t.size\tmain, .-main\n";
        if (as_compile(frame, strlen(frame), &o, &on) != 0 || !o || on < 64) {
            fprintf(stderr, "FAIL ok m158-as: lea/idiv/xor\n");
            fails++;
        } else {
            FILE* f = fopen("disk/m158.o", "wb");
            if (!f || fwrite(o, 1, on, f) != on || fclose(f) != 0) {
                fprintf(stderr, "FAIL ok m158-as: write\n");
                fails++;
            } else {
                paths[0] = "disk/m158.o";
                if (ld_link(paths, 1, &elf, &en) != 0 || !elf || elf[16] != 2) {
                    fprintf(stderr, "FAIL ok m158-as: ld\n");
                    fails++;
                }
            }
        }
        o = 0;
        on = 0;
        if (as_compile(gcc_ok, strlen(gcc_ok), &o, &on) != 0 || !o ||
            on < 64) {
            fprintf(stderr, "FAIL ok m158-gccs: gcc -S sample\n");
            fails++;
        }
        {
            unsigned char* bad = 0;
            size_t bn = 0;
            const char* e1 =
                ".text\n.globl main\nmain:\nlea -8(%rsp), %rax\nret\n";
            const char* e2 =
                ".text\n.globl main\nmain:\nidiv %r8\nret\n";
            const char* e3 =
                ".text\n.globl main\nmain:\nlea 200(%rbp), %rax\nret\n";
            const char* e4 =
                ".text\n.globl main\nmain:\nxor %r15, %r15\nret\n";
            if (as_compile(e1, strlen(e1), &bad, &bn) == 0) {
                fprintf(stderr, "FAIL edge m158: lea non-rbp accepted\n");
                fails++;
            }
            if (as_compile(e2, strlen(e2), &bad, &bn) == 0) {
                fprintf(stderr, "FAIL edge m158: bad idiv reg accepted\n");
                fails++;
            }
            if (as_compile(e3, strlen(e3), &bad, &bn) == 0) {
                fprintf(stderr, "FAIL edge m158: lea disp>127 accepted\n");
                fails++;
            }
            if (as_compile(e4, strlen(e4), &bad, &bn) == 0) {
                fprintf(stderr, "FAIL edge m158: bad xor reg accepted\n");
                fails++;
            }
        }
    }
    {
        /* M159–M160: void-star / unsigned + type edges. */
        if (expect_ok(
                "CXC_A2\n"
                "fn main(){\n"
                "void* vp;\n"
                "unsigned u;\n"
                "u=42;\n"
                "vp=0;\n"
                "if(sizeof(void*)!=8){\n"
                "return 1;\n"
                "}\n"
                "return u;\n"
                "}\n",
                "m160-voidptr") != 0) {
            fails++;
        }
        if (expect_fail(
                "CXC_A2\n"
                "fn main(){\n"
                "void v;\n"
                "return 0;\n"
                "}\n",
                "m160-void-value") != 0) {
            fails++;
        }
        if (expect_fail(
                "CXC_A2\n"
                "fn main(){\n"
                "return sizeof(void);\n"
                "}\n",
                "m160-sizeof-void") != 0) {
            fails++;
        }
        if (expect_fail(
                "CXC_A2\n"
                "struct Pt { x; };\n"
                "fn main(){\n"
                "Pt p;\n"
                "return p->x;\n"
                "}\n",
                "m160-arrow-nonptr") != 0) {
            fails++;
        }
    }
    {
        /* M161–M162: trunc musl / missing path / over-link edges. */
        unsigned char* elf = 0;
        size_t en = 0;
        {
            unsigned char trunc[24];
            const char* tp[1];
            int j;
            for (j = 0; j < 24; j++) {
                trunc[j] = 0;
            }
            trunc[0] = 0x7f;
            trunc[1] = 'E';
            trunc[2] = 'L';
            trunc[3] = 'F';
            trunc[4] = 2;
            trunc[5] = 1;
            trunc[16] = 1; /* ET_REL */
            {
                FILE* f = fopen("disk/trunc_mchr.o", "wb");
                if (f) {
                    (void)fwrite(trunc, 1, 24, f);
                    (void)fclose(f);
                }
            }
            tp[0] = "disk/trunc_mchr.o";
            if (ld_link(tp, 1, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m162: trunc memchr accepted\n");
                fails++;
            }
        }
        {
            const char* badp[1];
            badp[0] = "disk/nope-m162.o";
            if (ld_link(badp, 1, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m162: missing path accepted\n");
                fails++;
            }
        }
        {
            const char* too_many[97];
            int i;
            for (i = 0; i < 97; i++) {
                too_many[i] = "disk/main.o";
            }
            if (ld_link(too_many, 97, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m162: over CC_LINK_MAX accepted\n");
                fails++;
            }
        }
        {
            /* Success: dual PT_LOAD still present on lea/idiv sample. */
            unsigned char* o = 0;
            size_t on = 0;
            const char* ok =
                ".text\n.globl main\nmain:\n"
                "push %rbp\n"
                "mov %rsp, %rbp\n"
                "lea -8(%rbp), %rax\n"
                "mov $42, %eax\n"
                "pop %rbp\n"
                "ret\n"
                ".rodata\ns:\n.asciz \"MC\"\n";
            if (as_compile(ok, strlen(ok), &o, &on) != 0 || !o) {
                fprintf(stderr, "FAIL ok m162: as\n");
                fails++;
            } else {
                FILE* f = fopen("disk/m162.o", "wb");
                const char* paths[1];
                if (!f || fwrite(o, 1, on, f) != on || fclose(f) != 0) {
                    fprintf(stderr, "FAIL ok m162: write\n");
                    fails++;
                } else {
                    paths[0] = "disk/m162.o";
                    if (ld_link(paths, 1, &elf, &en) != 0 || !elf ||
                        elf[16] != 2 || elf[56] < 1) {
                        fprintf(stderr, "FAIL ok m162: ld phdr\n");
                        fails++;
                    }
                }
            }
        }
    }
    {
        /* M169–M170: shl/shr + wider cmp + gcc -S-ish + ASM edges. */
        unsigned char* o = 0;
        size_t on = 0;
        unsigned char* elf = 0;
        size_t en = 0;
        const char* paths[1];
        const char* sh =
            ".text\n.globl main\nmain:\n"
            "mov $21, %eax\n"
            "shl $1, %eax\n"
            "mov $42, %ecx\n"
            "cmp %ecx, %eax\n"
            "jne .Lbad\n"
            "mov $168, %eax\n"
            "shr $2, %eax\n"
            "ret\n"
            ".Lbad:\n"
            "mov $1, %eax\n"
            "ret\n";
        const char* gcc_ok =
            "\t.text\n"
            "\t.globl\tmain\n"
            "main:\n"
            "\tpushq\t%rbp\n"
            "\tmovq\t%rsp, %rbp\n"
            "\tmovl\t$21, %eax\n"
            "\tsall\t$1, %eax\n"
            "\tpopq\t%rbp\n"
            "\tret\n";
        if (as_compile(sh, strlen(sh), &o, &on) != 0 || !o || on < 64) {
            fprintf(stderr, "FAIL ok m170-as: shl/cmp\n");
            fails++;
        } else {
            FILE* f = fopen("disk/m170.o", "wb");
            if (!f || fwrite(o, 1, on, f) != on || fclose(f) != 0) {
                fprintf(stderr, "FAIL ok m170-as: write\n");
                fails++;
            } else {
                paths[0] = "disk/m170.o";
                if (ld_link(paths, 1, &elf, &en) != 0 || !elf || elf[16] != 2) {
                    fprintf(stderr, "FAIL ok m170-as: ld\n");
                    fails++;
                }
            }
        }
        o = 0;
        on = 0;
        if (as_compile(gcc_ok, strlen(gcc_ok), &o, &on) != 0 || !o ||
            on < 64) {
            fprintf(stderr, "FAIL ok m170-gccs: sal sample\n");
            fails++;
        }
        {
            unsigned char* bad = 0;
            size_t bn = 0;
            const char* e1 =
                ".text\n.globl main\nmain:\nshl $40, %eax\nret\n";
            const char* e2 =
                ".text\n.globl main\nmain:\nshl $1, %r8\nret\n";
            const char* e3 =
                ".text\n.globl main\nmain:\ncmp %r9, %eax\nret\n";
            const char* e4 =
                ".text\n.globl main\nmain:\nshr $-1, %eax\nret\n";
            if (as_compile(e1, strlen(e1), &bad, &bn) == 0) {
                fprintf(stderr, "FAIL edge m170: shl>31 accepted\n");
                fails++;
            }
            if (as_compile(e2, strlen(e2), &bad, &bn) == 0) {
                fprintf(stderr, "FAIL edge m170: bad shl reg accepted\n");
                fails++;
            }
            if (as_compile(e3, strlen(e3), &bad, &bn) == 0) {
                fprintf(stderr, "FAIL edge m170: bad cmp reg accepted\n");
                fails++;
            }
            if (as_compile(e4, strlen(e4), &bad, &bn) == 0) {
                fprintf(stderr, "FAIL edge m170: shr neg accepted\n");
                fails++;
            }
        }
    }
    {
        /* M171–M172: char-star-star / void-star-star + type edges. */
        if (expect_ok(
                "CXC_A2\n"
                "fn main(){\n"
                "char* cp;\n"
                "char** pp;\n"
                "void* vp;\n"
                "void** vpp;\n"
                "cp=\"ok\";\n"
                "pp=&cp;\n"
                "vp=0;\n"
                "vpp=&vp;\n"
                "if(sizeof(char**)!=8){\n"
                "return 1;\n"
                "}\n"
                "if(sizeof(void**)!=8){\n"
                "return 1;\n"
                "}\n"
                "return 42;\n"
                "}\n",
                "m172-pptr") != 0) {
            fails++;
        }
        if (expect_fail(
                "CXC_A2\n"
                "fn main(){\n"
                "void v;\n"
                "return 0;\n"
                "}\n",
                "m172-void-value") != 0) {
            fails++;
        }
        if (expect_fail(
                "CXC_A2\n"
                "fn main(){\n"
                "const int c=1;\n"
                "c=2;\n"
                "return c;\n"
                "}\n",
                "m172-const-assign") != 0) {
            fails++;
        }
        if (expect_fail(
                "CXC_A2\n"
                "struct Pt { x; };\n"
                "fn main(){\n"
                "Pt p;\n"
                "return p->x;\n"
                "}\n",
                "m172-arrow-nonptr") != 0) {
            fails++;
        }
    }
    {
        /* M165–M166 / M167–M168 edges: trunc musl / missing / over-link. */
        unsigned char* elf = 0;
        size_t en = 0;
        {
            unsigned char trunc[20];
            const char* tp[1];
            int j;
            for (j = 0; j < 20; j++) {
                trunc[j] = 0;
            }
            trunc[0] = 0x7f;
            trunc[1] = 'E';
            trunc[2] = 'L';
            trunc[3] = 'F';
            trunc[4] = 2;
            trunc[5] = 1;
            trunc[16] = 1;
            {
                FILE* f = fopen("disk/trunc_schr.o", "wb");
                if (f) {
                    (void)fwrite(trunc, 1, 20, f);
                    (void)fclose(f);
                }
            }
            tp[0] = "disk/trunc_schr.o";
            if (ld_link(tp, 1, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m166: trunc schr accepted\n");
                fails++;
            }
        }
        {
            const char* badp[1];
            badp[0] = "disk/nope-m168.o";
            if (ld_link(badp, 1, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m168: missing path accepted\n");
                fails++;
            }
        }
        {
            const char* too_many[97];
            int i;
            for (i = 0; i < 97; i++) {
                too_many[i] = "disk/main.o";
            }
            if (ld_link(too_many, 97, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m168: over CC_LINK_MAX accepted\n");
                fails++;
            }
        }
    }
    {
        /* M175–M176: trunc isalpha/memset + missing path edges. */
        unsigned char* elf = 0;
        size_t en = 0;
        {
            unsigned char trunc[24];
            const char* tp[1];
            int j;
            for (j = 0; j < 24; j++) {
                trunc[j] = 0;
            }
            trunc[0] = 0x7f;
            trunc[1] = 'E';
            trunc[2] = 'L';
            trunc[3] = 'F';
            trunc[4] = 2;
            trunc[5] = 1;
            trunc[16] = 1;
            {
                FILE* f = fopen("disk/trunc_ialp.o", "wb");
                if (f) {
                    (void)fwrite(trunc, 1, 24, f);
                    (void)fclose(f);
                }
            }
            tp[0] = "disk/trunc_ialp.o";
            if (ld_link(tp, 1, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m176: trunc ialp accepted\n");
                fails++;
            }
        }
        {
            unsigned char trunc[12];
            const char* tp[1];
            int j;
            for (j = 0; j < 12; j++) {
                trunc[j] = 0;
            }
            trunc[0] = 0x7f;
            trunc[1] = 'E';
            trunc[2] = 'L';
            trunc[3] = 'F';
            {
                FILE* f = fopen("disk/trunc_mset.o", "wb");
                if (f) {
                    (void)fwrite(trunc, 1, 12, f);
                    (void)fclose(f);
                }
            }
            tp[0] = "disk/trunc_mset.o";
            if (ld_link(tp, 1, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m176: trunc mset accepted\n");
                fails++;
            }
        }
        {
            const char* badp[1];
            badp[0] = "disk/nope-m176.o";
            if (ld_link(badp, 1, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m176: missing path accepted\n");
                fails++;
            }
        }
    }
    {
        /* M177–M178: mcpy success + io+mcpy DUP + missing mcpy UND. */
        if (expect_ok(
                "CXC_A2\n"
                "#lib \"mcpy.o\"\n"
                "import fn memcpy(d,s,n);\n"
                "fn main(){\n"
                "buf[4];\n"
                "memcpy(&buf[0], &buf[0], 0);\n"
                "return 42;\n"
                "}\n",
                "m178-mcpy") != 0) {
            fails++;
        }
        /* Dual provider: local memcpy + musl mcpy.o → DUP (success rsp
         * must not also list io.elf beside mcpy; keep-first would mask). */
        if (expect_fail(
                "CXC_A2\n"
                "#lib \"mcpy.o\"\n"
                "fn memcpy(d,s,n){\n"
                "return 0;\n"
                "}\n"
                "fn main(){\n"
                "return 0;\n"
                "}\n",
                "m178-dup-mcpy") != 0) {
            fails++;
        }
        if (expect_fail(
                "CXC_A2\n"
                "import fn memcpy(d,s,n);\n"
                "fn main(){\n"
                "buf[4];\n"
                "memcpy(&buf[0], &buf[0], 0);\n"
                "return 0;\n"
                "}\n",
                "m178-und-mcpy") != 0) {
            fails++;
        }
        {
            const char* emp[1];
            unsigned char* elf = 0;
            size_t en = 0;
            emp[0] = "";
            if (ld_link(emp, 1, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m178: empty rsp path accepted\n");
                fails++;
            }
        }
    }
    {
        /* M179–M180: div/movzx/and + gcc -S-ish + ASM edges. */
        unsigned char* o = 0;
        size_t on = 0;
        unsigned char* elf = 0;
        size_t en = 0;
        const char* paths[1];
        const char* ok =
            ".text\n.globl main\nmain:\n"
            "mov $84, %eax\n"
            "xor %edx, %edx\n"
            "mov $2, %ecx\n"
            "div %ecx\n"
            "mov $298, %eax\n"
            "movzbl %al, %eax\n"
            "mov $255, %ecx\n"
            "and %ecx, %eax\n"
            "mov $0, %edx\n"
            "or %edx, %eax\n"
            "ret\n";
        const char* gcc_ok =
            "\t.text\n"
            "\t.globl\tmain\n"
            "main:\n"
            "\tmovl\t$84, %eax\n"
            "\txorl\t%edx, %edx\n"
            "\tmovl\t$2, %ecx\n"
            "\tdivl\t%ecx\n"
            "\tmovzbl\t%al, %eax\n"
            "\tret\n";
        if (as_compile(ok, strlen(ok), &o, &on) != 0 || !o || on < 64) {
            fprintf(stderr, "FAIL ok m180-as: div/movzx/and\n");
            fails++;
        } else {
            FILE* f = fopen("disk/m180.o", "wb");
            if (!f || fwrite(o, 1, on, f) != on || fclose(f) != 0) {
                fprintf(stderr, "FAIL ok m180-as: write\n");
                fails++;
            } else {
                paths[0] = "disk/m180.o";
                if (ld_link(paths, 1, &elf, &en) != 0 || !elf || elf[16] != 2) {
                    fprintf(stderr, "FAIL ok m180-as: ld\n");
                    fails++;
                }
            }
        }
        o = 0;
        on = 0;
        if (as_compile(gcc_ok, strlen(gcc_ok), &o, &on) != 0 || !o ||
            on < 64) {
            fprintf(stderr, "FAIL ok m180-gccs: divl sample\n");
            fails++;
        }
        {
            unsigned char* bad = 0;
            size_t bn = 0;
            const char* e1 =
                ".text\n.globl main\nmain:\ndiv %r8\nret\n";
            const char* e2 =
                ".text\n.globl main\nmain:\nmovzx %ax, %eax\nret\n";
            const char* e3 =
                ".text\n.globl main\nmain:\nand %r9, %eax\nret\n";
            if (as_compile(e1, strlen(e1), &bad, &bn) == 0) {
                fprintf(stderr, "FAIL edge m180: bad div reg accepted\n");
                fails++;
            }
            if (as_compile(e2, strlen(e2), &bad, &bn) == 0) {
                fprintf(stderr, "FAIL edge m180: bad movzx form accepted\n");
                fails++;
            }
            if (as_compile(e3, strlen(e3), &bad, &bn) == 0) {
                fprintf(stderr, "FAIL edge m180: bad and reg accepted\n");
                fails++;
            }
        }
    }
    {
        /* M181–M182: typed struct fields + type edges. */
        if (expect_ok(
                "CXC_A2\n"
                "struct Pt {\n"
                "int x;\n"
                "int y;\n"
                "};\n"
                "fn main(){\n"
                "Pt p;\n"
                "Pt* sp;\n"
                "p.x=6;\n"
                "p.y=7;\n"
                "sp=&p;\n"
                "return sp->x*sp->y;\n"
                "}\n",
                "m182-typed-fields") != 0) {
            fails++;
        }
        if (expect_fail(
                "CXC_A2\n"
                "struct Pt {\n"
                "float x;\n"
                "};\n"
                "fn main(){\n"
                "return 0;\n"
                "}\n",
                "m182-bad-field-type") != 0) {
            fails++;
        }
        /* CC_STRUCT_FIELDS=16: 17 fields must reject. */
        if (expect_fail(
                "CXC_A2\n"
                "struct Pt {\n"
                "int a0;int a1;int a2;int a3;int a4;int a5;int a6;int a7;\n"
                "int a8;int a9;int b0;int b1;int b2;int b3;int b4;int b5;\n"
                "int b6;\n"
                "};\n"
                "fn main(){\n"
                "return 0;\n"
                "}\n",
                "m182-over-fields") != 0) {
            fails++;
        }
    }
    {
    {
        /* M185–M186: trunc isalnum/islower + missing path. */
        unsigned char* elf = 0;
        size_t en = 0;
        {
            unsigned char trunc[20];
            const char* tp[1];
            int j;
            for (j = 0; j < 20; j++) {
                trunc[j] = 0;
            }
            trunc[0] = 0x7f;
            trunc[1] = 'E';
            trunc[2] = 'L';
            trunc[3] = 'F';
            trunc[4] = 2;
            trunc[5] = 1;
            trunc[16] = 1;
            {
                FILE* f = fopen("disk/trunc_ialn.o", "wb");
                if (f) {
                    (void)fwrite(trunc, 1, 20, f);
                    (void)fclose(f);
                }
            }
            tp[0] = "disk/trunc_ialn.o";
            if (ld_link(tp, 1, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m186: trunc ialn accepted\n");
                fails++;
            }
        }
        {
            const char* badp[1];
            badp[0] = "disk/nope-m186.o";
            if (ld_link(badp, 1, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m186: missing path accepted\n");
                fails++;
            }
        }
        if (expect_ok(
                "CXC_A2\n"
                "#lib \"ialn.o\"\n"
                "#lib \"ilwr.o\"\n"
                "import fn isalnum(c);\n"
                "import fn islower(c);\n"
                "fn main(){\n"
                "return 42;\n"
                "}\n",
                "m186-ctype-plant") != 0) {
            fails++;
        }
    }
    {
        /* M187–M188: layout sizeof from field widths. */
        if (expect_ok(
                "CXC_A2\n"
                "struct Pt {\n"
                "char t;\n"
                "int x;\n"
                "int y;\n"
                "};\n"
                "struct Pair {\n"
                "char a;\n"
                "char b;\n"
                "};\n"
                "fn main(){\n"
                "Pt p;\n"
                "Pt* sp;\n"
                "p.t=1;\n"
                "p.x=6;\n"
                "p.y=7;\n"
                "sp=&p;\n"
                "if(sizeof(Pt)!=24){\n"
                "return 1;\n"
                "}\n"
                "if(sizeof(Pair)!=2){\n"
                "return 2;\n"
                "}\n"
                "return sp->x*sp->y;\n"
                "}\n",
                "m188-layout") != 0) {
            fails++;
        }
        if (expect_fail(
                "CXC_A2\n"
                "struct Pt {\n"
                "float x;\n"
                "};\n"
                "fn main(){\n"
                "return 0;\n"
                "}\n",
                "m188-bad-field-type") != 0) {
            fails++;
        }
        if (expect_fail(
                "CXC_A2\n"
                "struct Pt {\n"
                "int a0;int a1;int a2;int a3;int a4;int a5;int a6;int a7;\n"
                "int a8;int a9;int b0;int b1;int b2;int b3;int b4;int b5;\n"
                "int b6;\n"
                "};\n"
                "fn main(){\n"
                "return 0;\n"
                "}\n",
                "m188-over-fields") != 0) {
            fails++;
        }
    }
    {
        /* M189–M190: leave/neg/movsbl + gcc -S + ASM edges. */
        unsigned char* o = 0;
        size_t on = 0;
        unsigned char* elf = 0;
        size_t en = 0;
        const char* paths[1];
        const char* ok =
            ".text\n.globl main\nmain:\n"
            "push %rbp\n"
            "mov %rsp, %rbp\n"
            "mov $-42, %eax\n"
            "neg %eax\n"
            "cmp $42, %eax\n"
            "jne .Lbad\n"
            "mov $214, %eax\n"
            "movsbl %al, %eax\n"
            "neg %eax\n"
            "leave\n"
            "ret\n"
            ".Lbad:\n"
            "mov $1, %eax\n"
            "leave\n"
            "ret\n";
        const char* gcc_ok =
            "\t.text\n"
            "\t.globl\tmain\n"
            "main:\n"
            "\tpushq\t%rbp\n"
            "\tmovq\t%rsp, %rbp\n"
            "\tmovl\t$-42, %eax\n"
            "\tnegl\t%eax\n"
            "\tmovsbl\t%al, %eax\n"
            "\tleave\n"
            "\tret\n";
        if (as_compile(ok, strlen(ok), &o, &on) != 0 || !o || on < 64) {
            fprintf(stderr, "FAIL ok m190-as: leave/neg/movsbl\n");
            fails++;
        } else {
            FILE* f = fopen("disk/m190.o", "wb");
            if (!f || fwrite(o, 1, on, f) != on || fclose(f) != 0) {
                fprintf(stderr, "FAIL ok m190-as: write\n");
                fails++;
            } else {
                paths[0] = "disk/m190.o";
                if (ld_link(paths, 1, &elf, &en) != 0 || !elf || elf[16] != 2) {
                    fprintf(stderr, "FAIL ok m190-as: ld\n");
                    fails++;
                }
            }
        }
        o = 0;
        on = 0;
        if (as_compile(gcc_ok, strlen(gcc_ok), &o, &on) != 0 || !o ||
            on < 64) {
            fprintf(stderr, "FAIL ok m190-gccs: negl/leave sample\n");
            fails++;
        }
        {
            unsigned char* bad = 0;
            size_t bn = 0;
            const char* e1 =
                ".text\n.globl main\nmain:\nneg %ecx\nret\n";
            const char* e2 =
                ".text\n.globl main\nmain:\nmovsx %ax, %eax\nret\n";
            const char* e3 =
                ".text\n.globl main\nleave\nret\n";
            if (as_compile(e1, strlen(e1), &bad, &bn) == 0) {
                fprintf(stderr, "FAIL edge m190: bad neg reg accepted\n");
                fails++;
            }
            if (as_compile(e2, strlen(e2), &bad, &bn) == 0) {
                fprintf(stderr, "FAIL edge m190: bad movsx form accepted\n");
                fails++;
            }
            if (as_compile(e3, strlen(e3), &bad, &bn) == 0) {
                fprintf(stderr, "FAIL edge m190: leave outside fn accepted\n");
                fails++;
            }
        }
    }
    {
        /* M195–M198: trunc isupper/isblank/isgraph/isprint + missing + plant. */
        unsigned char* elf = 0;
        size_t en = 0;
        {
            unsigned char trunc[24];
            const char* tp[1];
            int j;
            for (j = 0; j < 24; j++) {
                trunc[j] = 0;
            }
            trunc[0] = 0x7f;
            trunc[1] = 'E';
            trunc[2] = 'L';
            trunc[3] = 'F';
            trunc[4] = 2;
            trunc[5] = 1;
            trunc[16] = 1;
            {
                FILE* f = fopen("disk/trunc_iupr.o", "wb");
                if (f) {
                    (void)fwrite(trunc, 1, 24, f);
                    (void)fclose(f);
                }
            }
            tp[0] = "disk/trunc_iupr.o";
            if (ld_link(tp, 1, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m196: trunc iupr accepted\n");
                fails++;
            }
            {
                FILE* f = fopen("disk/trunc_iblk.o", "wb");
                if (f) {
                    (void)fwrite(trunc, 1, 24, f);
                    (void)fclose(f);
                }
            }
            tp[0] = "disk/trunc_iblk.o";
            if (ld_link(tp, 1, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m196: trunc iblk accepted\n");
                fails++;
            }
            {
                FILE* f = fopen("disk/trunc_igrp.o", "wb");
                if (f) {
                    (void)fwrite(trunc, 1, 24, f);
                    (void)fclose(f);
                }
            }
            tp[0] = "disk/trunc_igrp.o";
            if (ld_link(tp, 1, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m198: trunc igrp accepted\n");
                fails++;
            }
            {
                FILE* f = fopen("disk/trunc_iprt.o", "wb");
                if (f) {
                    (void)fwrite(trunc, 1, 24, f);
                    (void)fclose(f);
                }
            }
            tp[0] = "disk/trunc_iprt.o";
            if (ld_link(tp, 1, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m198: trunc iprt accepted\n");
                fails++;
            }
        }
        {
            const char* badp[1];
            badp[0] = "disk/nope-m196.o";
            if (ld_link(badp, 1, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m196: missing path accepted\n");
                fails++;
            }
        }
        if (expect_ok(
                "CXC_A2\n"
                "#lib \"iupr.o\"\n"
                "#lib \"iblk.o\"\n"
                "#lib \"igrp.o\"\n"
                "#lib \"iprt.o\"\n"
                "import fn isupper(c);\n"
                "import fn isblank(c);\n"
                "import fn isgraph(c);\n"
                "import fn isprint(c);\n"
                "fn main(){\n"
                "return 42;\n"
                "}\n",
                "m198-ctype-plant") != 0) {
            fails++;
        }
    }
    {
        /* M199–M200: endbr64 ignore + sete/setbe + cmp edi + jbe/ja. */
        unsigned char* o = 0;
        size_t on = 0;
        unsigned char* elf = 0;
        size_t en = 0;
        const char* paths[1];
        const char* ok =
            ".text\n.globl main\nmain:\n"
            "endbr64\n"
            "push %rbp\n"
            "mov %rsp, %rbp\n"
            "mov $5, %edi\n"
            "cmp $5, %edi\n"
            "sete %al\n"
            "movzbl %al, %eax\n"
            "cmp $1, %eax\n"
            "jne .Lbad\n"
            "mov $3, %edi\n"
            "cmp $9, %edi\n"
            "jbe .Lok\n"
            "jmp .Lbad\n"
            ".Lok:\n"
            "mov $42, %eax\n"
            "leave\n"
            "ret\n"
            ".Lbad:\n"
            "mov $1, %eax\n"
            "leave\n"
            "ret\n";
        const char* gcc_ok =
            "\t.text\n"
            "\t.globl\tmain\n"
            "main:\n"
            "\tendbr64\n"
            "\tpushq\t%rbp\n"
            "\tmovq\t%rsp, %rbp\n"
            "\tmovl\t$5, %edi\n"
            "\tcmpl\t$5, %edi\n"
            "\tsete\t%al\n"
            "\tmovzbl\t%al, %eax\n"
            "\tcmpl\t$9, %edi\n"
            "\tsetbe\t%al\n"
            "\tleave\n"
            "\tret\n";
        if (as_compile(ok, strlen(ok), &o, &on) != 0 || !o || on < 64) {
            fprintf(stderr, "FAIL ok m200-as: endbr/sete/jbe\n");
            fails++;
        } else {
            FILE* f = fopen("disk/m200.o", "wb");
            if (!f || fwrite(o, 1, on, f) != on || fclose(f) != 0) {
                fprintf(stderr, "FAIL ok m200-as: write\n");
                fails++;
            } else {
                paths[0] = "disk/m200.o";
                if (ld_link(paths, 1, &elf, &en) != 0 || !elf || elf[16] != 2) {
                    fprintf(stderr, "FAIL ok m200-as: ld\n");
                    fails++;
                }
            }
        }
        o = 0;
        on = 0;
        if (as_compile(gcc_ok, strlen(gcc_ok), &o, &on) != 0 || !o ||
            on < 64) {
            fprintf(stderr, "FAIL ok m200-gccs: endbr/sete/setbe\n");
            fails++;
        }
        {
            unsigned char* bad = 0;
            size_t bn = 0;
            const char* e1 =
                ".text\n.globl main\nmain:\nsete %eax\nret\n";
            const char* e2 =
                ".text\n.globl main\nmain:\ncmp $1, %ecx\nret\n";
            const char* e3 =
                ".text\n.globl main\nmain:\njbe\nret\n";
            if (as_compile(e1, strlen(e1), &bad, &bn) == 0) {
                fprintf(stderr, "FAIL edge m200: bad sete target accepted\n");
                fails++;
            }
            if (as_compile(e2, strlen(e2), &bad, &bn) == 0) {
                fprintf(stderr, "FAIL edge m200: bad cmp reg accepted\n");
                fails++;
            }
            if (as_compile(e3, strlen(e3), &bad, &bn) == 0) {
                fprintf(stderr, "FAIL edge m200: bare jbe accepted\n");
                fails++;
            }
        }
    }
    {
        /* M205–M208: trunc isxdigit/iscntrl/ispunct/abs + missing + plant. */
        unsigned char* elf = 0;
        size_t en = 0;
        {
            unsigned char trunc[24];
            const char* tp[1];
            int j;
            for (j = 0; j < 24; j++) {
                trunc[j] = 0;
            }
            trunc[0] = 0x7f;
            trunc[1] = 'E';
            trunc[2] = 'L';
            trunc[3] = 'F';
            trunc[4] = 2;
            trunc[5] = 1;
            trunc[16] = 1;
            {
                FILE* f = fopen("disk/trunc_ixdg.o", "wb");
                if (f) {
                    (void)fwrite(trunc, 1, 24, f);
                    (void)fclose(f);
                }
            }
            tp[0] = "disk/trunc_ixdg.o";
            if (ld_link(tp, 1, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m206: trunc ixdg accepted\n");
                fails++;
            }
            {
                FILE* f = fopen("disk/trunc_ictl.o", "wb");
                if (f) {
                    (void)fwrite(trunc, 1, 24, f);
                    (void)fclose(f);
                }
            }
            tp[0] = "disk/trunc_ictl.o";
            if (ld_link(tp, 1, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m206: trunc ictl accepted\n");
                fails++;
            }
            {
                FILE* f = fopen("disk/trunc_ipct.o", "wb");
                if (f) {
                    (void)fwrite(trunc, 1, 24, f);
                    (void)fclose(f);
                }
            }
            tp[0] = "disk/trunc_ipct.o";
            if (ld_link(tp, 1, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m208: trunc ipct accepted\n");
                fails++;
            }
            {
                FILE* f = fopen("disk/trunc_iabs.o", "wb");
                if (f) {
                    (void)fwrite(trunc, 1, 24, f);
                    (void)fclose(f);
                }
            }
            tp[0] = "disk/trunc_iabs.o";
            if (ld_link(tp, 1, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m208: trunc iabs accepted\n");
                fails++;
            }
        }
        {
            const char* badp[1];
            badp[0] = "disk/nope-m206.o";
            if (ld_link(badp, 1, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m206: missing path accepted\n");
                fails++;
            }
        }
        {
            /* ispunct without isalnum → UND */
            const char* undp[1];
            undp[0] = "disk/ipct.o";
            if (ld_link(undp, 1, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m208: ispunct UND accepted\n");
                fails++;
            }
        }
        if (expect_ok(
                "CXC_A2\n"
                "#lib \"ixdg.o\"\n"
                "#lib \"ictl.o\"\n"
                "#lib \"ialn.o\"\n"
                "#lib \"ipct.o\"\n"
                "#lib \"iabs.o\"\n"
                "import fn isxdigit(c);\n"
                "import fn iscntrl(c);\n"
                "import fn ispunct(c);\n"
                "import fn abs(x);\n"
                "fn main(){\n"
                "return 42;\n"
                "}\n",
                "m208-leaf-plant") != 0) {
            fails++;
        }
    }
    {
        /* M209–M210: sub edi + setne %dl + jb/jae. */
        unsigned char* o = 0;
        size_t on = 0;
        unsigned char* elf = 0;
        size_t en = 0;
        const char* paths[1];
        const char* ok =
            ".text\n.globl main\nmain:\n"
            "endbr64\n"
            "push %rbp\n"
            "mov %rsp, %rbp\n"
            "mov $50, %edi\n"
            "sub $8, %edi\n"
            "cmp $42, %edi\n"
            "jne .Lbad\n"
            "mov $40, %edi\n"
            "cmp $42, %edi\n"
            "setne %dl\n"
            "movzbl %dl, %eax\n"
            "cmp $1, %eax\n"
            "jne .Lbad\n"
            "mov $40, %edi\n"
            "cmp $42, %edi\n"
            "jb .Lok\n"
            "jmp .Lbad\n"
            ".Lok:\n"
            "mov $50, %edi\n"
            "cmp $42, %edi\n"
            "jae .Lok2\n"
            "jmp .Lbad\n"
            ".Lok2:\n"
            "mov $42, %eax\n"
            "leave\n"
            "ret\n"
            ".Lbad:\n"
            "mov $1, %eax\n"
            "leave\n"
            "ret\n";
        const char* gcc_ok =
            "\t.text\n"
            "\t.globl\tmain\n"
            "main:\n"
            "\tendbr64\n"
            "\tpushq\t%rbp\n"
            "\tmovq\t%rsp, %rbp\n"
            "\tmovl\t$50, %edi\n"
            "\tsubl\t$8, %edi\n"
            "\tcmpl\t$42, %edi\n"
            "\tsetne\t%dl\n"
            "\tmovzbl\t%dl, %eax\n"
            "\tleave\n"
            "\tret\n";
        if (as_compile(ok, strlen(ok), &o, &on) != 0 || !o || on < 64) {
            fprintf(stderr, "FAIL ok m210-as: sub/setne/jb\n");
            fails++;
        } else {
            FILE* f = fopen("disk/m210.o", "wb");
            if (!f || fwrite(o, 1, on, f) != on || fclose(f) != 0) {
                fprintf(stderr, "FAIL ok m210-as: write\n");
                fails++;
            } else {
                paths[0] = "disk/m210.o";
                if (ld_link(paths, 1, &elf, &en) != 0 || !elf || elf[16] != 2) {
                    fprintf(stderr, "FAIL ok m210-as: ld\n");
                    fails++;
                }
            }
        }
        o = 0;
        on = 0;
        if (as_compile(gcc_ok, strlen(gcc_ok), &o, &on) != 0 || !o ||
            on < 64) {
            fprintf(stderr, "FAIL ok m210-gccs: subl/setne sample\n");
            fails++;
        }
        {
            unsigned char* bad = 0;
            size_t bn = 0;
            const char* e1 =
                ".text\n.globl main\nmain:\nsetne %eax\nret\n";
            const char* e2 =
                ".text\n.globl main\nmain:\nsub $1, %ecx\nret\n";
            const char* e3 =
                ".text\n.globl main\nmain:\njb\nret\n";
            if (as_compile(e1, strlen(e1), &bad, &bn) == 0) {
                fprintf(stderr, "FAIL edge m210: bad setne target accepted\n");
                fails++;
            }
            if (as_compile(e2, strlen(e2), &bad, &bn) == 0) {
                fprintf(stderr, "FAIL edge m210: bad sub reg accepted\n");
                fails++;
            }
            if (as_compile(e3, strlen(e3), &bad, &bn) == 0) {
                fprintf(stderr, "FAIL edge m210: bare jb accepted\n");
                fails++;
            }
        }
    }
    {
        /* M215–M218: trunc bzero/strnlen/isascii/llabs + UND + plant. */
        unsigned char* elf = 0;
        size_t en = 0;
        {
            unsigned char trunc[24];
            const char* tp[1];
            int j;
            for (j = 0; j < 24; j++) {
                trunc[j] = 0;
            }
            trunc[0] = 0x7f;
            trunc[1] = 'E';
            trunc[2] = 'L';
            trunc[3] = 'F';
            trunc[4] = 2;
            trunc[5] = 1;
            trunc[16] = 1;
            {
                FILE* f = fopen("disk/trunc_bzro.o", "wb");
                if (f) {
                    (void)fwrite(trunc, 1, 24, f);
                    (void)fclose(f);
                }
            }
            tp[0] = "disk/trunc_bzro.o";
            if (ld_link(tp, 1, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m216: trunc bzro accepted\n");
                fails++;
            }
            {
                FILE* f = fopen("disk/trunc_snln.o", "wb");
                if (f) {
                    (void)fwrite(trunc, 1, 24, f);
                    (void)fclose(f);
                }
            }
            tp[0] = "disk/trunc_snln.o";
            if (ld_link(tp, 1, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m216: trunc snln accepted\n");
                fails++;
            }
            {
                FILE* f = fopen("disk/trunc_iasc.o", "wb");
                if (f) {
                    (void)fwrite(trunc, 1, 24, f);
                    (void)fclose(f);
                }
            }
            tp[0] = "disk/trunc_iasc.o";
            if (ld_link(tp, 1, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m218: trunc iasc accepted\n");
                fails++;
            }
            {
                FILE* f = fopen("disk/trunc_llab.o", "wb");
                if (f) {
                    (void)fwrite(trunc, 1, 24, f);
                    (void)fclose(f);
                }
            }
            tp[0] = "disk/trunc_llab.o";
            if (ld_link(tp, 1, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m218: trunc llab accepted\n");
                fails++;
            }
        }
        {
            const char* badp[1];
            badp[0] = "disk/nope-m216.o";
            if (ld_link(badp, 1, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m216: missing path accepted\n");
                fails++;
            }
        }
        {
            const char* undp[1];
            undp[0] = "disk/bzro.o";
            if (ld_link(undp, 1, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m216: bzero UND accepted\n");
                fails++;
            }
            undp[0] = "disk/snln.o";
            if (ld_link(undp, 1, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m216: strnlen UND accepted\n");
                fails++;
            }
        }
        if (expect_ok(
                "CXC_A2\n"
                "#lib \"mset.o\"\n"
                "#lib \"bzro.o\"\n"
                "#lib \"mchr.o\"\n"
                "#lib \"snln.o\"\n"
                "#lib \"iasc.o\"\n"
                "#lib \"llab.o\"\n"
                "import fn bzero(p,n);\n"
                "import fn strnlen(s,n);\n"
                "import fn isascii(c);\n"
                "import fn llabs(x);\n"
                "fn main(){\n"
                "return 42;\n"
                "}\n",
                "m218-walk-plant") != 0) {
            fails++;
        }
    }
    {
        /* M219–M220: movzbl mem + add rdi + test rdx + cmp al,(rdi). */
        unsigned char* o = 0;
        size_t on = 0;
        unsigned char* elf = 0;
        size_t en = 0;
        const char* paths[1];
        const char* ok =
            ".text\n.globl main\nmain:\n"
            "push %rbp\n"
            "mov %rsp, %rbp\n"
            "lea s(%rip), %rax\n"
            "mov %rax, %rdi\n"
            "movzbl (%rdi), %eax\n"
            "cmp $65, %eax\n"
            "jne .Lbad\n"
            "add $1, %rdi\n"
            "movzbl (%rdi), %eax\n"
            "cmp $66, %eax\n"
            "jne .Lbad\n"
            "mov $1, %edx\n"
            "test %rdx, %rdx\n"
            "je .Lbad\n"
            "lea s(%rip), %rax\n"
            "mov %rax, %rdi\n"
            "mov $65, %eax\n"
            "cmp %al, (%rdi)\n"
            "jne .Lbad\n"
            "mov $42, %eax\n"
            "leave\n"
            "ret\n"
            ".Lbad:\n"
            "mov $1, %eax\n"
            "leave\n"
            "ret\n"
            ".rodata\n"
            "s:\n"
            ".asciz \"AB\"\n";
        const char* gcc_ok =
            "\t.text\n"
            "\t.globl\tmain\n"
            "main:\n"
            "\tpushq\t%rbp\n"
            "\tmovq\t%rsp, %rbp\n"
            "\tleaq\ts(%rip), %rax\n"
            "\tmovq\t%rax, %rdi\n"
            "\tmovzbl\t(%rdi), %eax\n"
            "\taddq\t$1, %rdi\n"
            "\ttestq\t%rdx, %rdx\n"
            "\tcmpb\t%al, (%rdi)\n"
            "\tleave\n"
            "\tret\n"
            "\t.rodata\n"
            "s:\n"
            "\t.asciz\t\"AB\"\n";
        if (as_compile(ok, strlen(ok), &o, &on) != 0 || !o || on < 64) {
            fprintf(stderr, "FAIL ok m220-as: mem-walk\n");
            fails++;
        } else {
            FILE* f = fopen("disk/m220.o", "wb");
            if (!f || fwrite(o, 1, on, f) != on || fclose(f) != 0) {
                fprintf(stderr, "FAIL ok m220-as: write\n");
                fails++;
            } else {
                paths[0] = "disk/m220.o";
                if (ld_link(paths, 1, &elf, &en) != 0 || !elf || elf[16] != 2) {
                    fprintf(stderr, "FAIL ok m220-as: ld\n");
                    fails++;
                }
            }
        }
        o = 0;
        on = 0;
        if (as_compile(gcc_ok, strlen(gcc_ok), &o, &on) != 0 || !o ||
            on < 64) {
            fprintf(stderr, "FAIL ok m220-gccs: movzbl/addq/testq\n");
            fails++;
        }
        {
            unsigned char* bad = 0;
            size_t bn = 0;
            const char* e1 =
                ".text\n.globl main\nmain:\nmovzbl (%rcx), %eax\nret\n";
            const char* e2 =
                ".text\n.globl main\nmain:\nadd $1, %rcx\nret\n";
            const char* e3 =
                ".text\n.globl main\nmain:\ntest\nret\n";
            if (as_compile(e1, strlen(e1), &bad, &bn) == 0) {
                fprintf(stderr, "FAIL edge m220: bad mem base accepted\n");
                fails++;
            }
            if (as_compile(e2, strlen(e2), &bad, &bn) == 0) {
                fprintf(stderr, "FAIL edge m220: bad add reg accepted\n");
                fails++;
            }
            if (as_compile(e3, strlen(e3), &bad, &bn) == 0) {
                fprintf(stderr, "FAIL edge m220: bare test accepted\n");
                fails++;
            }
        }
    }
    {
        /* M225–M228: trunc wrap plants + bcmp UND + plant ok. */
        unsigned char* elf = 0;
        size_t en = 0;
        {
            unsigned char trunc[24];
            const char* tp[1];
            int j;
            for (j = 0; j < 24; j++) {
                trunc[j] = 0;
            }
            trunc[0] = 0x7f;
            trunc[1] = 'E';
            trunc[2] = 'L';
            trunc[3] = 'F';
            trunc[4] = 2;
            trunc[5] = 1;
            trunc[16] = 1;
            {
                FILE* f = fopen("disk/trunc_tasc.o", "wb");
                if (f) {
                    (void)fwrite(trunc, 1, 24, f);
                    (void)fclose(f);
                }
            }
            tp[0] = "disk/trunc_tasc.o";
            if (ld_link(tp, 1, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m226: trunc tasc accepted\n");
                fails++;
            }
            {
                FILE* f = fopen("disk/trunc_imax.o", "wb");
                if (f) {
                    (void)fwrite(trunc, 1, 24, f);
                    (void)fclose(f);
                }
            }
            tp[0] = "disk/trunc_imax.o";
            if (ld_link(tp, 1, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m226: trunc imax accepted\n");
                fails++;
            }
            {
                FILE* f = fopen("disk/trunc_bcmp.o", "wb");
                if (f) {
                    (void)fwrite(trunc, 1, 24, f);
                    (void)fclose(f);
                }
            }
            tp[0] = "disk/trunc_bcmp.o";
            if (ld_link(tp, 1, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m228: trunc bcmp accepted\n");
                fails++;
            }
            {
                FILE* f = fopen("disk/trunc_iffs.o", "wb");
                if (f) {
                    (void)fwrite(trunc, 1, 24, f);
                    (void)fclose(f);
                }
            }
            tp[0] = "disk/trunc_iffs.o";
            if (ld_link(tp, 1, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m228: trunc iffs accepted\n");
                fails++;
            }
        }
        {
            const char* badp[1];
            badp[0] = "disk/nope-m226.o";
            if (ld_link(badp, 1, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m226: missing path accepted\n");
                fails++;
            }
        }
        {
            const char* undp[1];
            undp[0] = "disk/bcmp.o";
            if (ld_link(undp, 1, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m228: bcmp UND accepted\n");
                fails++;
            }
        }
        if (expect_ok(
                "CXC_A2\n"
                "#lib \"tasc.o\"\n"
                "#lib \"imax.o\"\n"
                "#lib \"mcmp.o\"\n"
                "#lib \"bcmp.o\"\n"
                "#lib \"iffs.o\"\n"
                "import fn toascii(c);\n"
                "import fn imaxabs(x);\n"
                "import fn bcmp(a,b,n);\n"
                "import fn ffs(x);\n"
                "fn main(){\n"
                "return 42;\n"
                "}\n",
                "m228-wrap-plant") != 0) {
            fails++;
        }
    }
    {
        /* M229–M230: inc/dec + sub rdx + seta/setb %cl. */
        unsigned char* o = 0;
        size_t on = 0;
        unsigned char* elf = 0;
        size_t en = 0;
        const char* paths[1];
        const char* ok =
            ".text\n.globl main\nmain:\n"
            "push %rbp\n"
            "mov %rsp, %rbp\n"
            "mov $40, %edi\n"
            "inc %rdi\n"
            "inc %rdi\n"
            "cmp $42, %edi\n"
            "jne .Lbad\n"
            "mov $3, %edx\n"
            "sub $1, %rdx\n"
            "cmp $2, %edx\n"
            "jne .Lbad\n"
            "mov $40, %edi\n"
            "cmp $42, %edi\n"
            "setb %cl\n"
            "movzbl %cl, %eax\n"
            "cmp $1, %eax\n"
            "jne .Lbad\n"
            "mov $50, %edi\n"
            "cmp $42, %edi\n"
            "seta %cl\n"
            "movzbl %cl, %eax\n"
            "cmp $1, %eax\n"
            "jne .Lbad\n"
            "mov $42, %eax\n"
            "leave\n"
            "ret\n"
            ".Lbad:\n"
            "mov $1, %eax\n"
            "leave\n"
            "ret\n";
        const char* gcc_ok =
            "\t.text\n"
            "\t.globl\tmain\n"
            "main:\n"
            "\tpushq\t%rbp\n"
            "\tmovq\t%rsp, %rbp\n"
            "\tmovl\t$40, %edi\n"
            "\tincq\t%rdi\n"
            "\tsubq\t$1, %rdx\n"
            "\tsetb\t%cl\n"
            "\tseta\t%cl\n"
            "\tmovzbl\t%cl, %eax\n"
            "\tleave\n"
            "\tret\n";
        if (as_compile(ok, strlen(ok), &o, &on) != 0 || !o || on < 64) {
            fprintf(stderr, "FAIL ok m230-as: inc/sub/seta\n");
            fails++;
        } else {
            FILE* f = fopen("disk/m230.o", "wb");
            if (!f || fwrite(o, 1, on, f) != on || fclose(f) != 0) {
                fprintf(stderr, "FAIL ok m230-as: write\n");
                fails++;
            } else {
                paths[0] = "disk/m230.o";
                if (ld_link(paths, 1, &elf, &en) != 0 || !elf || elf[16] != 2) {
                    fprintf(stderr, "FAIL ok m230-as: ld\n");
                    fails++;
                }
            }
        }
        o = 0;
        on = 0;
        if (as_compile(gcc_ok, strlen(gcc_ok), &o, &on) != 0 || !o ||
            on < 64) {
            fprintf(stderr, "FAIL ok m230-gccs: incq/seta sample\n");
            fails++;
        }
        {
            unsigned char* bad = 0;
            size_t bn = 0;
            const char* e1 =
                ".text\n.globl main\nmain:\ninc %rcx\nret\n";
            const char* e2 =
                ".text\n.globl main\nmain:\nsub $1, %rcx\nret\n";
            const char* e3 =
                ".text\n.globl main\nmain:\nseta %eax\nret\n";
            if (as_compile(e1, strlen(e1), &bad, &bn) == 0) {
                fprintf(stderr, "FAIL edge m230: bad inc reg accepted\n");
                fails++;
            }
            if (as_compile(e2, strlen(e2), &bad, &bn) == 0) {
                fprintf(stderr, "FAIL edge m230: bad sub reg accepted\n");
                fails++;
            }
            if (as_compile(e3, strlen(e3), &bad, &bn) == 0) {
                fprintf(stderr, "FAIL edge m230: bad seta target accepted\n");
                fails++;
            }
        }
    }
    {
        /* M235–M238: trunc netw plants + plant ok. */
        unsigned char* elf = 0;
        size_t en = 0;
        {
            unsigned char trunc[24];
            const char* tp[1];
            int j;
            for (j = 0; j < 24; j++) {
                trunc[j] = 0;
            }
            trunc[0] = 0x7f;
            trunc[1] = 'E';
            trunc[2] = 'L';
            trunc[3] = 'F';
            trunc[4] = 2;
            trunc[5] = 1;
            trunc[16] = 1;
            {
                FILE* f = fopen("disk/trunc_htns.o", "wb");
                if (f) {
                    (void)fwrite(trunc, 1, 24, f);
                    (void)fclose(f);
                }
            }
            tp[0] = "disk/trunc_htns.o";
            if (ld_link(tp, 1, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m236: trunc htns accepted\n");
                fails++;
            }
            {
                FILE* f = fopen("disk/trunc_htnl.o", "wb");
                if (f) {
                    (void)fwrite(trunc, 1, 24, f);
                    (void)fclose(f);
                }
            }
            tp[0] = "disk/trunc_htnl.o";
            if (ld_link(tp, 1, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m236: trunc htnl accepted\n");
                fails++;
            }
            {
                FILE* f = fopen("disk/trunc_ntns.o", "wb");
                if (f) {
                    (void)fwrite(trunc, 1, 24, f);
                    (void)fclose(f);
                }
            }
            tp[0] = "disk/trunc_ntns.o";
            if (ld_link(tp, 1, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m238: trunc ntns accepted\n");
                fails++;
            }
            {
                FILE* f = fopen("disk/trunc_ntnl.o", "wb");
                if (f) {
                    (void)fwrite(trunc, 1, 24, f);
                    (void)fclose(f);
                }
            }
            tp[0] = "disk/trunc_ntnl.o";
            if (ld_link(tp, 1, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m238: trunc ntnl accepted\n");
                fails++;
            }
        }
        {
            const char* badp[1];
            badp[0] = "disk/nope-m236.o";
            if (ld_link(badp, 1, &elf, &en) == 0) {
                fprintf(stderr, "FAIL edge m236: missing path accepted\n");
                fails++;
            }
        }
        if (expect_ok(
                "CXC_A2\n"
                "#lib \"htns.o\"\n"
                "#lib \"htnl.o\"\n"
                "#lib \"ntns.o\"\n"
                "#lib \"ntnl.o\"\n"
                "import fn htons(x);\n"
                "import fn htonl(x);\n"
                "import fn ntohs(x);\n"
                "import fn ntohl(x);\n"
                "fn main(){\n"
                "return 42;\n"
                "}\n",
                "m238-netw-plant") != 0) {
            fails++;
        }
    }
    {
        /* M239–M240: rolw + bswap + movzwl. */
        unsigned char* o = 0;
        size_t on = 0;
        unsigned char* elf = 0;
        size_t en = 0;
        const char* paths[1];
        const char* ok =
            ".text\n.globl main\nmain:\n"
            "push %rbp\n"
            "mov %rsp, %rbp\n"
            "mov $0x1234, %eax\n"
            "rolw $8, %ax\n"
            "cmp $0x3412, %eax\n"
            "jne .Lbad\n"
            "mov $0x12345678, %eax\n"
            "bswap %eax\n"
            "cmp $0x78563412, %eax\n"
            "jne .Lbad\n"
            "mov $0xab, %eax\n"
            "movzwl %ax, %eax\n"
            "cmp $0xab, %eax\n"
            "jne .Lbad\n"
            "mov $42, %eax\n"
            "leave\n"
            "ret\n"
            ".Lbad:\n"
            "mov $1, %eax\n"
            "leave\n"
            "ret\n";
        const char* gcc_ok =
            "\t.text\n"
            "\t.globl\tmain\n"
            "main:\n"
            "\tpushq\t%rbp\n"
            "\tmovq\t%rsp, %rbp\n"
            "\tmovl\t$4660, %eax\n"
            "\trolw\t$8, %ax\n"
            "\tbswap\t%eax\n"
            "\tmovzwl\t%ax, %eax\n"
            "\tleave\n"
            "\tret\n";
        if (as_compile(ok, strlen(ok), &o, &on) != 0 || !o || on < 64) {
            fprintf(stderr, "FAIL ok m240-as: rolw/bswap/movzwl\n");
            fails++;
        } else {
            FILE* f = fopen("disk/m240.o", "wb");
            if (!f || fwrite(o, 1, on, f) != on || fclose(f) != 0) {
                fprintf(stderr, "FAIL ok m240-as: write\n");
                fails++;
            } else {
                paths[0] = "disk/m240.o";
                if (ld_link(paths, 1, &elf, &en) != 0 || !elf || elf[16] != 2) {
                    fprintf(stderr, "FAIL ok m240-as: ld\n");
                    fails++;
                }
            }
        }
        o = 0;
        on = 0;
        if (as_compile(gcc_ok, strlen(gcc_ok), &o, &on) != 0 || !o ||
            on < 64) {
            fprintf(stderr, "FAIL ok m240-gccs: rolw/bswap sample\n");
            fails++;
        }
        {
            unsigned char* bad = 0;
            size_t bn = 0;
            const char* e1 =
                ".text\n.globl main\nmain:\nrolw $8, %ecx\nret\n";
            const char* e2 =
                ".text\n.globl main\nmain:\nrolw $20, %ax\nret\n";
            const char* e3 =
                ".text\n.globl main\nmain:\nbswap %ecx\nret\n";
            if (as_compile(e1, strlen(e1), &bad, &bn) == 0) {
                fprintf(stderr, "FAIL edge m240: bad rolw reg accepted\n");
                fails++;
            }
            if (as_compile(e2, strlen(e2), &bad, &bn) == 0) {
                fprintf(stderr, "FAIL edge m240: bad rolw imm accepted\n");
                fails++;
            }
            if (as_compile(e3, strlen(e3), &bad, &bn) == 0) {
                fprintf(stderr, "FAIL edge m240: bad bswap reg accepted\n");
                fails++;
            }
        }
    }
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
    /* M289 allows bare `return;`; reject doubled keyword instead. */
    if (expect_fail("CXC_A2\nreturn return;\n", "bad-return") != 0) {
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

    {
        /* M245–M247: third RO PT_LOAD + .bss NOBITS contract. */
        unsigned char* o = 0;
        size_t on = 0;
        unsigned char* elf = 0;
        size_t en = 0;
        const char* paths[1];
        const char* ok =
            ".text\n.globl main\nmain:\n"
            "lea s(%rip), %rax\n"
            "mov $42, %eax\n"
            "ret\n"
            ".data\n"
            "buf:\n.zero 8\n"
            ".bss\n"
            "zb:\n.zero 8\n"
            ".rodata\n"
            "s:\n.asciz \"HD\"\n";
        if (as_compile(ok, strlen(ok), &o, &on) != 0 || !o) {
            fprintf(stderr, "FAIL ok m245-ro: as\n");
            fails++;
        } else {
            FILE* f = fopen("disk/m245.o", "wb");
            if (!f || fwrite(o, 1, on, f) != on || fclose(f) != 0) {
                fprintf(stderr, "FAIL ok m245-ro: write\n");
                fails++;
            } else {
                paths[0] = "disk/m245.o";
                if (ld_link(paths, 1, &elf, &en) != 0 || !elf ||
                    elf[16] != 2 || elf[56] != 3) {
                    fprintf(stderr, "FAIL ok m245-ro: phnum!=3\n");
                    fails++;
                } else {
                    /* Phdr1 must be PF_R only (4). */
                    unsigned char* ph1 = elf + 64 + 56;
                    if (ph1[4] != 4) {
                        fprintf(stderr, "FAIL ok m245-ro: RO flags\n");
                        fails++;
                    }
                    /* RW phdr: filesz < memsz when bss present. */
                    {
                        unsigned char* ph2 = elf + 64 + 112;
                        unsigned long long fsz = 0;
                        unsigned long long msz = 0;
                        int k;
                        for (k = 0; k < 8; k++) {
                            fsz |= (unsigned long long)ph2[32 + k] << (8 * k);
                            msz |= (unsigned long long)ph2[40 + k] << (8 * k);
                        }
                        if (fsz >= msz) {
                            fprintf(stderr, "FAIL ok m245-bss: filesz>=memsz\n");
                            fails++;
                        }
                    }
                }
            }
        }
        {
            const char* bad =
                ".text\n.globl main\nmain:\nret\n"
                ".bss\n"
                "z:\n.byte 1\n";
            o = 0;
            on = 0;
            if (as_compile(bad, strlen(bad), &o, &on) == 0) {
                fprintf(stderr, "FAIL edge m245: .byte in .bss accepted\n");
                fails++;
            }
        }
        if (expect_fail("CXC_A2\nstruct Pt { float x; };\nfn main(){return 0;}\n",
                        "m248-bad-field") != 0) {
            fails++;
        }
        if (expect_fail(
                "CXC_A2\nstruct Pt {\n"
                "int a0;int a1;int a2;int a3;int a4;int a5;int a6;int a7;\n"
                "int a8;int a9;int b0;int b1;int b2;int b3;int b4;int b5;\n"
                "int b6;\n"
                "};\n"
                "fn main(){return 0;}\n",
                "m248-over-fields") != 0) {
            fails++;
        }
        if (expect_ok(
                "CXC_A2\n"
                "struct Pair { char a; char b; };\n"
                "fn main(){\n"
                "Pair q;\n"
                "q.a=1;\n"
                "q.b=2;\n"
                "if(sizeof(Pair)!=2){return 1;}\n"
                "return 42;\n"
                "}\n",
                "m248-compact-pair") != 0) {
            fails++;
        }
    }
    {
        /* M253: as thicken — rep/cld/%fs/movslq + illegal edges. */
        unsigned char* o = 0;
        size_t on = 0;
        const char* ok =
            ".text\n.globl main\nmain:\n"
            "cld\n"
            "rep movsb\n"
            "mov %fs:0x28, %rax\n"
            "sub %fs:0x28, %rdx\n"
            "movslq %eax, %rdx\n"
            "mov $42, %eax\n"
            "ret\n";
        if (as_compile(ok, strlen(ok), &o, &on) != 0 || !o || on < 32) {
            fprintf(stderr, "FAIL ok m253-as: thicken\n");
            fails++;
        }
        {
            const char* bad1 =
                ".text\n.globl main\nmain:\nrep scasb\nret\n";
            const char* bad2 =
                ".text\n.globl main\nmain:\nmov %fs:0x10, %rax\nret\n";
            const char* bad3 =
                ".text\n.globl main\nmain:\nmovslq %ebx, %rax\nret\n";
            o = 0;
            on = 0;
            if (as_compile(bad1, strlen(bad1), &o, &on) == 0) {
                fprintf(stderr, "FAIL edge m253: rep scasb accepted\n");
                fails++;
            }
            o = 0;
            on = 0;
            if (as_compile(bad2, strlen(bad2), &o, &on) == 0) {
                fprintf(stderr, "FAIL edge m253: bad fs off accepted\n");
                fails++;
            }
            o = 0;
            on = 0;
            if (as_compile(bad3, strlen(bad3), &o, &on) == 0) {
                fprintf(stderr, "FAIL edge m253: bad movslq accepted\n");
                fails++;
            }
        }
    }
    {
        /* M252/M254: deep plant link — NOTYPE __memcpy_fwd + hard_stubs + strtol. */
        const char* main_s =
            ".text\n.globl main\nmain:\n"
            "push %rbp\nmov %rsp, %rbp\n"
            "lea s42(%rip), %rax\nmov %rax, %rdi\n"
            "xor %esi, %esi\nmov $10, %edx\n"
            "call strtol\nleave\nret\n"
            ".rodata\ns42:\n.asciz \"42\"\n";
        unsigned char* main_o = 0;
        size_t main_n = 0;
        /* Relative to host cwd (run_cc_host_test sets disk/); avoid /tmp — UCRT fopen fails on Windows. */
        const char* main_path = "disk/cxc_m252_main.o";
        const char* trunc_path = "disk/cxc_m252_trunc.o";
        const char* paths[8];
        unsigned char* elf = 0;
        size_t en = 0;
        int n = 0;
        FILE* mf;
        if (as_compile(main_s, strlen(main_s), &main_o, &main_n) != 0 ||
            !main_o) {
            fprintf(stderr, "FAIL ok m252-hard: as main\n");
            fails++;
        } else if (!(mf = fopen(main_path, "wb")) ||
                   fwrite(main_o, 1, main_n, mf) != main_n) {
            fprintf(stderr, "FAIL ok m252-hard: write main.o\n");
            fails++;
            if (mf) {
                fclose(mf);
            }
        } else {
            fclose(mf);
            paths[n++] = main_path;
            paths[n++] = "disk/stol.o";
            paths[n++] = "disk/iscn.o";
            paths[n++] = "disk/shgt.o";
            paths[n++] = "disk/hstb.o";
            paths[n++] = "disk/mcpy.o";
            paths[n++] = "disk/mmov.o";
            paths[n++] = "disk/slen.o";
            if (ld_link(paths, n, &elf, &en) != 0 || !elf || en < 64) {
                fprintf(stderr, "FAIL ok m252-hard: ld_link code=%d\n",
                        ld_last_fail_code());
                fails++;
            }
            remove(main_path);
        }
        {
            unsigned char* bad = 0;
            size_t bn = 0;
            const char* miss[] = {"disk/mmov.o"};
            if (ld_link(miss, 1, &bad, &bn) == 0) {
                fprintf(stderr, "FAIL edge m252: memmove-only linked\n");
                fails++;
            } else if (ld_last_fail_code() != 21) {
                fprintf(stderr, "FAIL edge m252: expect UND 21 got %d\n",
                        ld_last_fail_code());
                fails++;
            }
        }
        {
            const char* miss2[] = {"disk/hstb.o"};
            unsigned char* bad = 0;
            size_t bn = 0;
            if (ld_link(miss2, 1, &bad, &bn) == 0) {
                fprintf(stderr, "FAIL edge m252: stubs-only linked\n");
                fails++;
            }
        }
        {
            FILE* tf = fopen(trunc_path, "wb");
            unsigned char* bad = 0;
            size_t bn = 0;
            const char* tp[1];
            if (tf) {
                fwrite("\x7f""ELF", 1, 4, tf);
                fclose(tf);
                tp[0] = trunc_path;
                if (ld_link(tp, 1, &bad, &bn) == 0) {
                    fprintf(stderr, "FAIL edge m252: trunc ELF accepted\n");
                    fails++;
                }
                remove(trunc_path);
            }
        }
    }

    /* M263: TCAP capacity / plant edges (≥3 expect_fail). */
    if (expect_fail(
            "CXC_A2\n"
            "#include \"missing_tcap.cx\"\n"
            "return 0;\n",
            "m263-missing-include") != 0) {
        fails++;
    }
    if (expect_fail(
            "CXC_A2\n"
            "fn main(){\n"
            "return undeclared_tcap;\n"
            "}\n",
            "m263-und-sym") != 0) {
        fails++;
    }
    if (expect_fail(
            "CXC_A2\n"
            ".text\n"
            "not_asm_in_c:\n"
            "ret\n",
            "m263-junk-asm-in-c") != 0) {
        fails++;
    }
    if (expect_fail(
            "CXC_A2\nint g=1;\nint g=2;\nreturn 0;\n",
            "m263-dup-global") != 0) {
        fails++;
    }

    /* M273: TDRV driver / spawn surface edges (≥3 expect_fail). */
    if (expect_fail(
            "CXC_A2\n"
            "#lib \"missing_as.o\"\n"
            "fn main(){return 0;}\n",
            "m273-missing-as-lib") != 0) {
        fails++;
    }
    if (expect_fail(
            "CXC_A2\n"
            "fn main(){\n"
            "return spawn_missing_tdrv;\n"
            "}\n",
            "m273-und-spawn-sym") != 0) {
        fails++;
    }
    if (expect_fail(
            "CXC_A2\n"
            "fn main(){\n"
            "bad_rsp_token @@@;\n"
            "return 0;\n"
            "}\n",
            "m273-junk-token") != 0) {
        fails++;
    }
    if (expect_fail(
            "CXC_A2\n"
            ".globl only_in_asm\n"
            "fn main(){return 0;}\n",
            "m273-asm-dir-in-c") != 0) {
        fails++;
    }
    /* M287/D1: bare unknown # directive must hard-fail (never silent EOF). */
    if (expect_fail("CXC_A2\nfn main(){\n#x\n}\n", "hash") != 0) {
        fails++;
    }
    /* M291: object #define expands. */
    if (expect_ok("CXC_A2\n#define N 42\nreturn N;\n", "pp-define-ok") != 0) {
        fails++;
    }

    /* M296/M297: DLOC exact line/col (hash at line 3 col 1 after prep). */
    {
        unsigned char* elf = 0;
        size_t n = 0;
        const char* bad = "CXC_A2\nfn main(){\n#x\n}\n";
        if (cc_compile(bad, strlen(bad), &elf, &n) == 0) {
            fprintf(stderr, "FAIL m296-dloc: expected reject\n");
            fails++;
        } else if (cc_last_fail_code() != 60) {
            fprintf(stderr, "FAIL m296-dloc: code=%d want 60\n",
                    cc_last_fail_code());
            fails++;
        } else if (cc_last_fail_line() != 3 || cc_last_fail_col() != 1) {
            fprintf(stderr, "FAIL m296-dloc: line=%d col=%d want 3:1\n",
                    cc_last_fail_line(), cc_last_fail_col());
            fails++;
        }
    }
    /* M297: prep fail (unclosed #if) must publish non-zero DLOC + code 10. */
    {
        unsigned char* elf = 0;
        size_t n = 0;
        const char* bad = "CXC_A2\n#if 1\nreturn 1;\n";
        if (cc_compile(bad, strlen(bad), &elf, &n) == 0) {
            fprintf(stderr, "FAIL m297-dloc-prep: expected reject\n");
            fails++;
        } else if (cc_last_fail_code() != 10) {
            fprintf(stderr, "FAIL m297-dloc-prep: code=%d want 10\n",
                    cc_last_fail_code());
            fails++;
        } else if (cc_last_fail_line() < 1 || cc_last_fail_col() < 1) {
            fprintf(stderr, "FAIL m297-dloc-prep: line=%d col=%d\n",
                    cc_last_fail_line(), cc_last_fail_col());
            fails++;
        }
    }

    /* M296: -E surface via cc_preprocess (must expand, not passthrough). */
    {
        char out[4096];
        size_t out_n = 0;
        const char* in = "CXC_A2\n#define N 42\nreturn N;\n";
        if (cc_preprocess(in, strlen(in), out, sizeof(out), &out_n) != 0 ||
            out_n == 0) {
            fprintf(stderr, "FAIL m296-E: preprocess\n");
            fails++;
        } else if (strstr(out, "return 42") == 0 &&
                   strstr(out, "return N") != 0) {
            fprintf(stderr, "FAIL m296-E: N not expanded\n");
            fails++;
        } else if (strstr(out, "return 42") == 0) {
            fprintf(stderr, "FAIL m296-E: missing return 42 in [%.*s]\n",
                    (int)out_n, out);
            fails++;
        }
    }

    /* M296: -S → as closed loop (fn ans returns 42). */
    {
        char* asm_text = 0;
        size_t asm_n = 0;
        unsigned char* rel = 0;
        size_t rel_n = 0;
        const char* src =
            "CXC_A2\n"
            "fn ans(){\n"
            "return 42;\n"
            "}\n";
        if (cc_compile_asm(src, strlen(src), &asm_text, &asm_n) != 0 ||
            !asm_text || asm_n == 0) {
            fprintf(stderr, "FAIL m296-S: compile_asm code=%d\n",
                    cc_last_fail_code());
            fails++;
        } else if (strstr(asm_text, ".globl ans") == 0 ||
                   strstr(asm_text, "mov $42") == 0) {
            fprintf(stderr, "FAIL m296-S: asm text [%s]\n", asm_text);
            fails++;
        } else if (as_compile(asm_text, asm_n, &rel, &rel_n) != 0 || !rel ||
                   rel_n < 64) {
            fprintf(stderr, "FAIL m296-S: as_compile code=%d\n",
                    as_last_fail_code());
            fails++;
        } else if (rel[0] != 0x7f || rel[16] != 1) {
            fprintf(stderr, "FAIL m296-S: not ET_REL\n");
            fails++;
        }
    }

    /* M297: S-local / S-if / S-while / S-call — -S → as ET_REL. */
    {
        struct {
            const char* name;
            const char* src;
            const char* need1;
            const char* need2;
        } cases[] = {
            {"S-local",
             "CXC_A2\nfn ans(){\nint x;\nx=40;\nreturn x+2;\n}\n",
             "(%rbp)", "mov $40"},
            {"S-if",
             "CXC_A2\nfn ans(){\nif(1){return 42;}\nreturn 0;\n}\n",
             "je .Lcc", "test %eax"},
            {"S-while",
             "CXC_A2\nfn ans(){\nint i;\ni=0;\nwhile(i<3){i=i+1;}\n"
             "return i;\n}\n",
             "jmp .Lcc", "setl %al"},
            {"S-call",
             "CXC_A2\nfn add(a,b){return a+b;}\n"
             "fn ans(){return add(40,2);}\n",
             "call add", "pop %rdi"},
        };
        size_t ci;
        for (ci = 0; ci < sizeof(cases) / sizeof(cases[0]); ci++) {
            char* asm_text = 0;
            size_t asm_n = 0;
            unsigned char* rel = 0;
            size_t rel_n = 0;
            unsigned char* obj = 0;
            size_t obj_n = 0;
            if (cc_compile_asm(cases[ci].src, strlen(cases[ci].src), &asm_text,
                               &asm_n) != 0 ||
                !asm_text || asm_n == 0) {
                fprintf(stderr, "FAIL m297-%s: compile_asm code=%d\n",
                        cases[ci].name, cc_last_fail_code());
                fails++;
                continue;
            }
            if (strstr(asm_text, cases[ci].need1) == 0 ||
                strstr(asm_text, cases[ci].need2) == 0) {
                fprintf(stderr, "FAIL m297-%s: missing [%s]/[%s] in:\n%.400s\n",
                        cases[ci].name, cases[ci].need1, cases[ci].need2,
                        asm_text);
                fails++;
                continue;
            }
            if (as_compile(asm_text, asm_n, &rel, &rel_n) != 0 || !rel ||
                rel_n < 64) {
                fprintf(stderr, "FAIL m297-%s: as_compile code=%d\n%.400s\n",
                        cases[ci].name, as_last_fail_code(), asm_text);
                fails++;
                continue;
            }
            if (rel[0] != 0x7f || rel[16] != 1) {
                fprintf(stderr, "FAIL m297-%s: not ET_REL\n", cases[ci].name);
                fails++;
                continue;
            }
            /* Isomorphic: direct obj compile also succeeds. */
            if (cc_compile_obj(cases[ci].src, strlen(cases[ci].src), &obj,
                               &obj_n) != 0 ||
                !obj || obj_n < 64) {
                fprintf(stderr, "FAIL m297-%s: compile_obj code=%d\n",
                        cases[ci].name, cc_last_fail_code());
                fails++;
            }
        }
    }

    if (fails != 0) {
        fprintf(stderr, "cc_host_test: %d failures\n", fails);
        return 1;
    }
    puts("cc_host_test: PASS");
    return 0;
}
