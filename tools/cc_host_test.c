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
