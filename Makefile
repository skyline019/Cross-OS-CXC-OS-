K=kernel
U=user

OBJS = \
  $K/entry.o \
  $K/start.o \
  $K/console.o \
  $K/printk.o \
  $K/uart.o \
  $K/kalloc.o \
  $K/spinlock.o \
  $K/string.o \
  $K/main.o \
  $K/vm.o \
  $K/proc.o \
  $K/swtch.o \
  $K/trampoline.o \
  $K/trap.o \
  $K/syscall.o \
  $K/sysproc.o \
  $K/bio.o \
  $K/fs.o \
  $K/log.o \
  $K/sleeplock.o \
  $K/file.o \
  $K/pipe.o \
  $K/exec.o \
  $K/sysfile.o \
  $K/kernelvec.o \
  $K/plic.o \
  $K/virtio_disk.o \
  $K/pmem.o \
  $K/pmfile.o \
  $K/syspmfile.o \
  $K/pmcdc.o \
  $K/pmstream.o \
  $K/pmdev.o \
  $K/pmfs.o \
  $K/pmalgo.o \
  $K/pmindex.o \
  $K/pmcontract.o \
  $K/syspmfs.o

# riscv64-unknown-elf- or riscv64-linux-gnu-
# perhaps in /opt/riscv/bin
#TOOLPREFIX = 

# Try to infer the correct TOOLPREFIX if not set
ifndef TOOLPREFIX
TOOLPREFIX := $(shell if riscv64-unknown-elf-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
	then echo 'riscv64-unknown-elf-'; \
	elif riscv64-elf-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
	then echo 'riscv64-elf-'; \
	elif riscv64-none-elf-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
	then echo 'riscv64-none-elf-'; \
	elif riscv64-linux-gnu-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
	then echo 'riscv64-linux-gnu-'; \
	elif riscv64-unknown-linux-gnu-objdump -i 2>&1 | grep 'elf64-big' >/dev/null 2>&1; \
	then echo 'riscv64-unknown-linux-gnu-'; \
	else echo "***" 1>&2; \
	echo "*** Error: Couldn't find a riscv64 version of GCC/binutils." 1>&2; \
	echo "*** To turn off this error, run 'gmake TOOLPREFIX= ...'." 1>&2; \
	echo "***" 1>&2; exit 1; fi)
endif

QEMU = qemu-system-riscv64
MIN_QEMU_VERSION = 7.2

CC = $(TOOLPREFIX)gcc
LD = $(TOOLPREFIX)ld
OBJCOPY = $(TOOLPREFIX)objcopy
OBJDUMP = $(TOOLPREFIX)objdump

# Deterministic builds.
DETFLAGS = -ffile-prefix-map=$(CURDIR)=.

CFLAGS = -Wall -Werror -Wno-unknown-attributes -O -fno-omit-frame-pointer -ggdb -gdwarf-2
CFLAGS += $(DETFLAGS)
CFLAGS += -march=rv64gc
CFLAGS += -std=gnu99
CFLAGS += -MD
CFLAGS += -mcmodel=medany
CFLAGS += -ffreestanding
CFLAGS += -fno-common -nostdlib
CFLAGS += -fno-delete-null-pointer-checks
CFLAGS += -fno-builtin-strncpy -fno-builtin-strncmp -fno-builtin-strlen -fno-builtin-memset
CFLAGS += -fno-builtin-memmove -fno-builtin-memcmp -fno-builtin-log -fno-builtin-bzero
CFLAGS += -fno-builtin-strchr -fno-builtin-exit -fno-builtin-malloc -fno-builtin-putc
CFLAGS += -fno-builtin-free
CFLAGS += -fno-builtin-memcpy -Wno-main
CFLAGS += -fno-builtin-printf -fno-builtin-fprintf -fno-builtin-vprintf
CFLAGS += -I.
# Opt-in experimental modes; rebuild cleanly after changing these flags.
CFLAGS += $(EXTRA_CFLAGS)
CFLAGS += $(shell $(CC) -fno-stack-protector -E -x c /dev/null >/dev/null 2>&1 && echo -fno-stack-protector)

# Disable PIE when possible (for Ubuntu 16.10 toolchain)
ifneq ($(shell $(CC) -dumpspecs 2>/dev/null | grep -e '[^f]no-pie'),)
CFLAGS += -fno-pie -no-pie
endif
ifneq ($(shell $(CC) -dumpspecs 2>/dev/null | grep -e '[^f]nopie'),)
CFLAGS += -fno-pie -nopie
endif

LDFLAGS = -z max-page-size=4096

$K/kernel: $(OBJS) $K/kernel.ld
	$(LD) $(LDFLAGS) -T $K/kernel.ld -o $K/kernel $(OBJS) 
	$(OBJDUMP) -S $K/kernel > $K/kernel.asm
	$(OBJDUMP) -t $K/kernel | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $K/kernel.sym

$K/%.o: $K/%.S
	$(CC) -march=rv64gc -g $(DETFLAGS) -c -o $@ $<

tags: $(OBJS)
	etags kernel/*.S kernel/*.c

ULIB = $U/ulib.o $U/usys.o $U/printf.o $U/umalloc.o

_%: %.o $(ULIB) $U/user.ld
	$(LD) $(LDFLAGS) -T $U/user.ld -o $@ $< $(ULIB)
	$(OBJDUMP) -S $@ > $*.asm
	$(OBJDUMP) -t $@ | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $*.sym

$U/usys.S : $U/usys.pl
	perl $U/usys.pl > $U/usys.S

$U/usys.o : $U/usys.S
	$(CC) $(CFLAGS) -c -o $U/usys.o $U/usys.S

$U/_forktest: $U/forktest.o $(ULIB)
	# forktest has less library code linked in - needs to be small
	# in order to be able to max out the proc table.
	$(LD) $(LDFLAGS) -N -e main -Ttext 0 -o $U/_forktest $U/forktest.o $U/ulib.o $U/usys.o
	$(OBJDUMP) -S $U/_forktest > $U/forktest.asm

mkfs/mkfs: mkfs/mkfs.c $K/fs.h $K/param.h
	gcc -Wno-unknown-attributes -I. -o mkfs/mkfs mkfs/mkfs.c

# Prevent deletion of intermediate files, e.g. cat.o, after first build, so
# that disk image changes after first build are persistent until clean.  More
# details:
# http://www.gnu.org/software/make/manual/html_node/Chained-Rules.html
.PRECIOUS: %.o

UPROGS=\
	$U/_cat\
	$U/_echo\
	$U/_forktest\
	$U/_grep\
	$U/_init\
	$U/_kill\
	$U/_ln\
	$U/_ls\
	$U/_mkdir\
	$U/_rm\
	$U/_sh\
	$U/_stressfs\
	$U/_usertests\
	$U/_grind\
	$U/_wc\
	$U/_zombie\
	$U/_logstress\
	$U/_forphan\
	$U/_dorphan\
	$U/_sync\
	$U/_pmfs_probe\
	$U/_pmfs_bench\
	$U/_pmformat\
	$U/_pmfs_regress\
	$U/_pmfs_posix\
	$U/_pmfs_file\

fs.img: mkfs/mkfs README $(UPROGS)
	mkfs/mkfs fs.img README $(UPROGS)

-include kernel/*.d user/*.d

clean: 
	rm -f *.tex *.dvi *.idx *.aux *.log *.ind *.ilg \
	*/*.o */*.d */*.asm */*.sym \
	$K/kernel fs.img \
	mkfs/mkfs .gdbinit \
        $U/usys.S \
	$(UPROGS)

# try to generate a unique GDB port
GDBPORT = $(shell expr `id -u` % 5000 + 25000)
# QEMU's gdb stub command line changed in 0.11
QEMUGDB = $(shell if $(QEMU) -help | grep -q '^-gdb'; \
	then echo "-gdb tcp::$(GDBPORT)"; \
	else echo "-s -p $(GDBPORT)"; fi)
ifndef CPUS
CPUS := 3
endif

QEMUOPTS = -machine virt -bios none -kernel $K/kernel -m 128M -smp $(CPUS) -nographic
QEMUOPTS += -global virtio-mmio.force-legacy=false
QEMUOPTS += -drive file=fs.img,if=none,format=raw,id=x0
QEMUOPTS += -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0

# MAP mode: DRAM past PHYSTOP backs PMEMBASE (32MiB) → total 160MiB.
QEMUOPTS_PMEM = -machine virt -bios none -kernel $K/kernel -m 160M -smp $(CPUS) -nographic
QEMUOPTS_PMEM += -global virtio-mmio.force-legacy=false
QEMUOPTS_PMEM += -drive file=fs.img,if=none,format=raw,id=x0
QEMUOPTS_PMEM += -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0

# File-backed guest RAM for PMEM2 cold-reboot (set PMEM_RAM_FILE).
# -m must match backend size or QEMU rejects NUMA layout.
PMEM_RAM_FILE ?= guest-ram.img
QEMUOPTS_PMEM_FILE = -machine virt -bios none -kernel $K/kernel -m 160M -smp $(CPUS) -nographic
QEMUOPTS_PMEM_FILE += -object memory-backend-file,id=mem,size=160M,mem-path=$(PMEM_RAM_FILE),share=on
QEMUOPTS_PMEM_FILE += -numa node,memdev=mem
QEMUOPTS_PMEM_FILE += -global virtio-mmio.force-legacy=false
QEMUOPTS_PMEM_FILE += -drive file=fs.img,if=none,format=raw,id=x0
QEMUOPTS_PMEM_FILE += -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0

qemu: check-qemu-version $K/kernel fs.img
	$(QEMU) $(QEMUOPTS)

# Run already-built kernel at -m 160M (kernel must be built with -DPMEM_MAP).
qemu-pmem-run: check-qemu-version $K/kernel fs.img
	$(QEMU) $(QEMUOPTS_PMEM)

# File-backed MAP boot (kernel must already be -DPMEM_MAP).
qemu-pmem-file: check-qemu-version $K/kernel fs.img
	@test -f $(PMEM_RAM_FILE) || truncate -s 160M $(PMEM_RAM_FILE)
	$(QEMU) $(QEMUOPTS_PMEM_FILE)

# MAP mode: force-rebuild with -DPMEM_MAP then -m 160M.
qemu-pmem: check-qemu-version
	$(MAKE) clean
	$(MAKE) CFLAGS="$(CFLAGS) -DPMEM_MAP" $K/kernel fs.img
	$(QEMU) $(QEMUOPTS_PMEM)

# The historical boot contracts deliberately overwrite test data. These
# targets rebuild with selftests enabled; use only in a disposable checkout.
contract-build:
	$(MAKE) clean
	$(MAKE) EXTRA_CFLAGS="$(EXTRA_CFLAGS) -DPMFS_SELFTEST" $K/kernel fs.img

contract: contract-build
	python3 tools/contract_pmfs.py

contract-strict: contract-build
	python3 tools/contract_pmfs.py --strict --probe --posix --file

contract-pmem:
	$(MAKE) clean
	$(MAKE) EXTRA_CFLAGS="$(EXTRA_CFLAGS) -DPMEM_MAP -DPMFS_SELFTEST" $K/kernel fs.img
	python3 tools/contract_pmfs.py --pmem --strict --probe --posix --file
	$(MAKE) clean
	$(MAKE) $K/kernel fs.img

# Only tests shared guest-RAM magic, not physical power failure or NVDIMM.
contract-pmem2:
	$(MAKE) clean
	$(MAKE) EXTRA_CFLAGS="$(EXTRA_CFLAGS) -DPMEM_MAP -DPMFS_SELFTEST" $K/kernel fs.img
	python3 tools/contract_pmem2.py
	$(MAKE) clean
	$(MAKE) $K/kernel fs.img

contract-full: contract-build
	python3 tools/contract_pmfs.py --strict --probe --posix --file --bench

# These targets rebuild fs.img: use a disposable checkout. They print raw
# operation counts and r_time ticks; no historical document is overwritten.
bench-fill: kernel-emul
	python3 tools/run_bench_fill.py

bench-sweep: kernel-emul
	python3 tools/run_bench_sweep.py

# Independent protocol specification model with three mutation controls.
test-model:
	python3 tools/check_persist_model.py

# Software-boundary fault injection is test-only and can hang the guest.
# The harness uses temporary copies of fs.img and the shared guest-RAM file.
recovery-build:
	$(MAKE) clean
	$(MAKE) EXTRA_CFLAGS="$(EXTRA_CFLAGS) -DPMEM_MAP -DPMFS_FAULT_INJECT" $K/kernel fs.img

test-recovery: recovery-build
	python3 tools/pmfs_recovery.py --cpus $(CPUS) $(RECOVERY_ARGS)

test-recovery-zicbom:
	$(MAKE) clean
	$(MAKE) EXTRA_CFLAGS="$(EXTRA_CFLAGS) -DPMEM_MAP -DPMFS_FAULT_INJECT -DPMEM_ZICBOM" $K/kernel fs.img
	python3 tools/pmfs_recovery.py --cpus $(CPUS) --zicbom $(RECOVERY_ARGS)

# Requires the experimental QEMU 9.2.0 patch in tools/patches. The backend
# is an independent file-backed nvdimm; process kill is not physical power loss.
test-recovery-nvdimm:
	$(MAKE) clean
	$(MAKE) EXTRA_CFLAGS="$(EXTRA_CFLAGS) -DPMEM_MAP -DPMEM_NVDIMM -DPMFS_FAULT_INJECT" $K/kernel fs.img
	python3 tools/pmfs_recovery.py --cpus $(CPUS) --nvdimm --qemu $(QEMU) $(RECOVERY_ARGS)

test-recovery-nvdimm-zicbom:
	$(MAKE) clean
	$(MAKE) EXTRA_CFLAGS="$(EXTRA_CFLAGS) -DPMEM_MAP -DPMEM_NVDIMM -DPMFS_FAULT_INJECT -DPMEM_ZICBOM" $K/kernel fs.img
	python3 tools/pmfs_recovery.py --cpus $(CPUS) --nvdimm --zicbom --qemu $(QEMU) $(RECOVERY_ARGS)

# Explicit rebuild helpers for sweep / CI
kernel-pmem:
	$(MAKE) clean
	$(MAKE) CFLAGS="$(CFLAGS) -DPMEM_MAP" $K/kernel fs.img

kernel-emul:
	$(MAKE) clean
	$(MAKE) $K/kernel fs.img

.gdbinit: .gdbinit.tmpl-riscv
	sed "s/:1234/:$(GDBPORT)/" < $^ > $@

qemu-gdb: $K/kernel .gdbinit fs.img
	@echo "*** Now run 'gdb' in another window." 1>&2
	$(QEMU) $(QEMUOPTS) -S $(QEMUGDB)

print-gdbport:
	@echo $(GDBPORT)

QEMU_VERSION := $(shell $(QEMU) --version | head -n 1 | sed -E 's/^QEMU emulator version ([0-9]+\.[0-9]+)\..*/\1/')
check-qemu-version:
	@if [ "$(shell echo "$(QEMU_VERSION) >= $(MIN_QEMU_VERSION)" | bc)" -eq 0 ]; then \
		echo "ERROR: Need qemu version >= $(MIN_QEMU_VERSION)"; \
		exit 1; \
	fi

.PHONY: fmt
fmt:
	clang-format -i $(wildcard kernel/*.[ch] user/*.[ch] mkfs/*.c)
