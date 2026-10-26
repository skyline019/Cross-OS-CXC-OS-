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
