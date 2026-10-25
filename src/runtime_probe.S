    .text

# bool runtime_write_probe(void* address, uint64_t value)
# Microsoft x64 ABI: RCX=address, RDX=value. The faulting instruction and the
# recovery target are exported so the ring-0 page-fault test can recognize this
# exact probe and resume the same worker with a false return value.
    .globl runtime_write_probe
    .def runtime_write_probe
    .scl 2
    .type 32
    .endef
runtime_write_probe:
    .globl runtime_write_probe_instruction
runtime_write_probe_instruction:
    movq %rdx, (%rcx)
    movl $1, %eax
    ret

    .globl runtime_write_probe_recovery
runtime_write_probe_recovery:
    xorl %eax, %eax
    ret
