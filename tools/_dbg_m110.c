#define CC_HOST 1
#include "cxc_cc/cc.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    unsigned char* elf = 0;
    size_t n = 0;
    const char* cases[] = {
        "CXC_A2\n#lib \"crt.elf\"\n"
        "import fn cwrite(a,b);\n"
        "fn main(){\ncwrite(\"AB\", 2);\nreturn 0;\n}\n",
        "CXC_A2\n#lib \"rt.elf\"\n#lib \"crt.elf\"\n"
        "import fn cwrite(a,b);\n"
        "fn main(){\ncwrite(\"AB\", 2);\nreturn 0;\n}\n",
        "CXC_A2\n#lib \"rt.elf\"\n#lib \"crt.elf\"\n"
        "import fn ans();\nimport fn cwrite(a,b);\n"
        "fn main(){\nans();\ncwrite(\"AB\", 2);\nreturn 0;\n}\n",
        "CXC_A2\n#lib \"rt.elf\"\n#lib \"crt.elf\"\n"
        "import fn twice(a);\n"
        "fn main(){\nreturn twice(21);\n}\n",
    };
    int i;
    for (i = 0; i < 4; i++) {
        elf = 0;
        n = 0;
        int r = cc_compile(cases[i], strlen(cases[i]), &elf, &n);
        printf("case%d r=%d fail=%d n=%zu\n", i, r, cc_last_fail_code(), n);
    }
    return 0;
}
