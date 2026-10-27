#include <cxc/elf.hpp>

#include <cxc/pmm.hpp>
#include <cxc/vmm.hpp>

#include <stddef.h>
#include <stdint.h>

extern "C" void* memcpy(void*, const void*, size_t);
extern "C" void* memset(void*, int, size_t);

namespace cxc::elf {
namespace {

constexpr uint8_t elf_magic[4] = {0x7f, 'E', 'L', 'F'};
constexpr uint16_t et_exec = 2;
constexpr uint16_t em_x86_64 = 62;
constexpr uint32_t pt_load = 1;
constexpr uint32_t pt_interp = 3;
constexpr uint32_t pt_dynamic = 2;
constexpr uint32_t pf_x = 1;
constexpr uint32_t pf_w = 2;
constexpr uint64_t user_canonical_limit = uint64_t{1} << 47;
constexpr size_t max_phnum = 8;
// M260: match CXCFS 4 MiB ceiling for fat toolchain ELFs.
constexpr size_t max_image = 4u * 1024u * 1024u;

struct Ehdr {
    uint8_t ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t phoff;
    uint64_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
} __attribute__((packed));

struct Phdr {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
} __attribute__((packed));

static_assert(sizeof(Ehdr) == 64);
static_assert(sizeof(Phdr) == 56);

bool read_object(const uint8_t* image, size_t image_size, size_t offset,
                 void* out, size_t length) {
    if (length == 0 || offset > image_size || image_size - offset < length) {
        return false;
    }
    memcpy(out, image + offset, length);
    return true;
}

}  // namespace

bool load_static_et_exec(uint64_t user_root_physical, const uint8_t* image,
                         size_t image_size, LoadResult& result) {
    result = LoadResult{};
    if (user_root_physical == 0 || image == nullptr || image_size < sizeof(Ehdr) ||
        image_size > max_image) {
        result.failure = 1;
        return false;
    }

    Ehdr header{};
    if (!read_object(image, image_size, 0, &header, sizeof(header))) {
        result.failure = 2;
        return false;
    }
    for (size_t index = 0; index < 4; ++index) {
        if (header.ident[index] != elf_magic[index]) {
            result.failure = 3;
            return false;
        }
    }
    if (header.ident[4] != 2 || header.ident[5] != 1 || header.ident[6] != 1) {
        result.failure = 40;
        return false;
    }
    if (header.type != et_exec || header.machine != em_x86_64 ||
        header.version != 1) {
        result.failure = 41;
        return false;
    }
    if (header.ehsize != sizeof(Ehdr) || header.phentsize != sizeof(Phdr)) {
        result.failure = 42;  // packed sizeof mismatch vs file
        return false;
    }
    if (header.phnum == 0 || header.phnum > max_phnum) {
        result.failure = 43;
        return false;
    }
    if (header.entry == 0 || header.entry >= user_canonical_limit) {
        result.failure = 44;
        return false;
    }

    const uint64_t ph_bytes =
        static_cast<uint64_t>(header.phnum) * header.phentsize;
    if (header.phoff > image_size || image_size - header.phoff < ph_bytes) {
        result.failure = 5;
        return false;
    }

    uint32_t load_count = 0;
    for (uint16_t index = 0; index < header.phnum; ++index) {
        Phdr ph{};
        const size_t offset =
            static_cast<size_t>(header.phoff) +
            static_cast<size_t>(index) * header.phentsize;
        if (!read_object(image, image_size, offset, &ph, sizeof(ph))) {
            result.failure = 6;
            return false;
        }
        if (ph.type == pt_interp || ph.type == pt_dynamic) {
            result.failure = 7;
            return false;
        }
        if (ph.type != pt_load) {
            continue;
        }
        // Linker PHDRS may emit an empty data PT_LOAD (vaddr 0, memsz 0)
        // when .data/.bss are unused; skip rather than reject.
        if (ph.memsz == 0) {
            continue;
        }
        if (ph.filesz > ph.memsz || ph.vaddr == 0 ||
            ph.vaddr >= user_canonical_limit ||
            ph.vaddr + ph.memsz < ph.vaddr ||
            ph.vaddr + ph.memsz > user_canonical_limit ||
            ph.offset > image_size ||
            image_size - ph.offset < ph.filesz) {
            result.failure = 8;
            return false;
        }

        // M260: real musl-static ELFs use unaligned PT_LOAD (e.g. 0x403fd0).
        const uint64_t seg_lo = ph.vaddr;
        const uint64_t seg_hi = ph.vaddr + ph.memsz;
        const uint64_t page_lo = seg_lo & ~(pmm::page_size - 1);
        const uint64_t page_hi =
            (seg_hi + pmm::page_size - 1) & ~(pmm::page_size - 1);
        const uint64_t file_hi = seg_lo + ph.filesz;

        uint64_t map_flags = 0;
        if ((ph.flags & pf_w) != 0) {
            map_flags |= vmm::user_map_writable;
        }
        if ((ph.flags & pf_x) != 0) {
            map_flags |= vmm::user_map_executable;
        }
        if ((map_flags & vmm::user_map_writable) != 0 &&
            (map_flags & vmm::user_map_executable) != 0) {
            result.failure = 10;
            return false;
        }

        for (uint64_t page_va = page_lo; page_va < page_hi;
             page_va += pmm::page_size) {
            const uint64_t physical = pmm::allocate_pages(1);
            if (physical == 0) {
                result.failure = 9;
                return false;
            }
            auto* const page = static_cast<uint8_t*>(
                pmm::physical_to_virtual(physical));
            memset(page, 0, pmm::page_size);

            const uint64_t copy_lo = page_va > seg_lo ? page_va : seg_lo;
            const uint64_t copy_hi = (page_va + pmm::page_size) < file_hi
                                        ? (page_va + pmm::page_size)
                                        : file_hi;
            if (copy_lo < copy_hi) {
                memcpy(page + (copy_lo - page_va),
                       image + ph.offset + (copy_lo - seg_lo),
                       static_cast<size_t>(copy_hi - copy_lo));
            }

            if (!vmm::map_user_page(user_root_physical, page_va, physical,
                                    map_flags)) {
                pmm::free_pages(physical, 1);
                result.failure = 11;
                return false;
            }
            ++result.pages;
        }
        ++load_count;
    }

    if (load_count == 0) {
        result.failure = 12;
        return false;
    }
    result.segments = load_count;
    result.entry = header.entry;
    result.at_phent = header.phentsize;
    result.at_phnum = header.phnum;
    result.at_phdr = 0;
    // AT_PHDR = VA of program headers inside a mapped PT_LOAD (M68 musl CRT).
    for (uint16_t index = 0; index < header.phnum; ++index) {
        Phdr ph{};
        const size_t offset =
            static_cast<size_t>(header.phoff) +
            static_cast<size_t>(index) * header.phentsize;
        if (!read_object(image, image_size, offset, &ph, sizeof(ph))) {
            break;
        }
        if (ph.type != pt_load || ph.memsz == 0) {
            continue;
        }
        if (ph.offset <= header.phoff &&
            header.phoff + ph_bytes <= ph.offset + ph.filesz) {
            result.at_phdr = ph.vaddr + (header.phoff - ph.offset);
            break;
        }
    }
    return true;
}

}  // namespace cxc::elf
