#!/usr/bin/env python3
"""Wire deep musl HARD plants into src/user.cpp."""
from pathlib import Path

path = Path("/mnt/e/OS/workspace-study/cxc-os/src/user.cpp")
text = path.read_text()

includes = """#include <cxc/musl_strcspn_elf.hpp>
#include <cxc/musl_strspn_elf.hpp>
#include <cxc/musl_strpbrk_elf.hpp>
#include <cxc/musl_strrchr_elf.hpp>
#include <cxc/musl_memmove_elf.hpp>
#include <cxc/musl_strdup_elf.hpp>
#include <cxc/musl_strtol_elf.hpp>
#include <cxc/musl_intscan_elf.hpp>
#include <cxc/musl_shgetc_elf.hpp>
#include <cxc/musl_memrchr_elf.hpp>
#include <cxc/hard_stubs_elf.hpp>
"""

if "musl_strcspn_elf.hpp" not in text:
    text = text.replace(
        '#include <cxc/musl_strlen_elf.hpp>\n',
        '#include <cxc/musl_strlen_elf.hpp>\n' + includes,
    )

deep_calls = r'''        "lea s42(%rip), %rax\n"
        "mov %rax, %rdi\n"
        "lea ssep(%rip), %rax\n"
        "mov %rax, %rsi\n"
        "call strcspn\n"
        "cmp $2, %eax\n"
        "jne .Lmc\n"
        "lea s42(%rip), %rax\n"
        "mov %rax, %rdi\n"
        "lea sdig(%rip), %rax\n"
        "mov %rax, %rsi\n"
        "call strspn\n"
        "cmp $2, %eax\n"
        "jne .Lmc\n"
        "lea s42(%rip), %rax\n"
        "mov %rax, %rdi\n"
        "lea ssep(%rip), %rax\n"
        "mov %rax, %rsi\n"
        "call strpbrk\n"
        "test %rax, %rax\n"
        "je .Lmc\n"
        "lea s42(%rip), %rax\n"
        "mov %rax, %rdi\n"
        "mov $50, %esi\n"
        "call strrchr\n"
        "test %rax, %rax\n"
        "je .Lmc\n"
        "lea -16(%rbp), %rax\n"
        "mov %rax, %rdi\n"
        "lea s42(%rip), %rax\n"
        "mov %rax, %rsi\n"
        "mov $3, %edx\n"
        "call memmove\n"
        "lea s42(%rip), %rax\n"
        "mov %rax, %rdi\n"
        "call strdup\n"
        "test %rax, %rax\n"
        "je .Lmc\n"
        "lea s42(%rip), %rax\n"
        "mov %rax, %rdi\n"
        "xor %esi, %esi\n"
        "mov $10, %edx\n"
        "call strtol\n"
        "cmp $42, %eax\n"
        "jne .Lmc\n"
'''

needle = (
    '        "mov $42, %edi\\n"\n'
    '        "call labs\\n"\n'
)
if "call strcspn" not in text:
    text = text.replace(needle, deep_calls + needle)

# rodata extras + sa tag HD
text = text.replace('.asciz \\"NW\\"\\n"', '.asciz \\"HD\\"\\n"', 1)
if "ssep:" not in text:
    text = text.replace(
        '        "q:\\n"\n'
        '        ".quad 42\\n";',
        '        "ssep:\\n"\n'
        '        ".asciz \\",;\\"\\n"\n'
        '        "sdig:\\n"\n'
        '        ".asciz \\"0123456789\\"\\n"\n'
        '        "q:\\n"\n'
        '        ".quad 42\\n";',
    )

rsp_extra = (
    '        "disk/scsp.o\\n"\n'
    '        "disk/sspn.o\\n"\n'
    '        "disk/spbk.o\\n"\n'
    '        "disk/srch.o\\n"\n'
    '        "disk/mmov.o\\n"\n'
    '        "disk/sdup.o\\n"\n'
    '        "disk/stol.o\\n"\n'
    '        "disk/iscn.o\\n"\n'
    '        "disk/shgt.o\\n"\n'
    '        "disk/mrch.o\\n"\n'
    '        "disk/hstb.o\\n"\n'
)
if "disk/scsp.o" not in text:
    text = text.replace(
        '        "disk/mcpy.o\\n"\n'
        '        "disk/slen.o\\n";',
        rsp_extra
        + '        "disk/mcpy.o\\n"\n'
        + '        "disk/slen.o\\n";',
    )

# Plant writer helper block before lib.c write
plant_block = r'''
    {
        struct Plant {
            const char* path;
            const unsigned char* blob;
            unsigned size;
            uint32_t fail;
        };
        const Plant plants[] = {
            {"disk/scsp.o", musl_strcspn_elf, musl_strcspn_elf_size, 200},
            {"disk/sspn.o", musl_strspn_elf, musl_strspn_elf_size, 203},
            {"disk/spbk.o", musl_strpbrk_elf, musl_strpbrk_elf_size, 206},
            {"disk/srch.o", musl_strrchr_elf, musl_strrchr_elf_size, 209},
            {"disk/mmov.o", musl_memmove_elf, musl_memmove_elf_size, 212},
            {"disk/sdup.o", musl_strdup_elf, musl_strdup_elf_size, 215},
            {"disk/stol.o", musl_strtol_elf, musl_strtol_elf_size, 218},
            {"disk/iscn.o", musl_intscan_elf, musl_intscan_elf_size, 221},
            {"disk/shgt.o", musl_shgetc_elf, musl_shgetc_elf_size, 224},
            {"disk/mrch.o", musl_memrchr_elf, musl_memrchr_elf_size, 227},
            {"disk/hstb.o", hard_stubs_elf, hard_stubs_elf_size, 230},
        };
        for (const Plant& pl : plants) {
            const int64_t pfd = fs::open_path(
                pl.path, fs::o_wronly | fs::o_creat | fs::o_trunc, 0644);
            if (pfd < 0) {
                result.failure = pl.fail;
                return false;
            }
            if (fs::write_fd(static_cast<uint32_t>(pfd), pl.blob, pl.size) !=
                static_cast<int64_t>(pl.size)) {
                (void)fs::close_fd(static_cast<uint32_t>(pfd));
                result.failure = pl.fail + 1;
                return false;
            }
            if (fs::close_fd(static_cast<uint32_t>(pfd)) != 0) {
                result.failure = pl.fail + 2;
                return false;
            }
        }
    }
'''

marker = '''    {
        const int64_t lfd = fs::open_path(
            "disk/lib.c", fs::o_wronly | fs::o_creat | fs::o_trunc, 0644);'''
if "disk/scsp.o" in text and "musl_strcspn_elf," not in text:
    text = text.replace(marker, plant_block + "\n" + marker)

path.write_text(text)
print("patched user.cpp", path.stat().st_size)
