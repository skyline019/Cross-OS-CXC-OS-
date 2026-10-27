/* cxc-cc: keep .data/.bss above brk/mmap/stack (M257 16 MiB windows). */
ENTRY(_start)
PHDRS {
  text PT_LOAD FLAGS(5);
  rodata PT_LOAD FLAGS(4);
  data PT_LOAD FLAGS(6);
}
SECTIONS {
  . = 0x400000;
  .text : {
    *(.text*)
  } :text
  . = ALIGN(4096);
  .rodata : {
    *(.rodata*)
    *(.rodata.*)
  } :rodata
  /* brk @ 0x409000 (16MiB), mmap @ 0x2000000, stack @ 0x3800000. */
  . = 0x4000000;
  .data : {
    *(.data*)
    *(.data.*)
  } :data
  .bss : {
    *(.bss*)
    *(.bss.*)
    *(COMMON)
  } :data
  /DISCARD/ : {
    *(.comment)
    *(.note*)
    *(.eh_frame*)
    *(.eh_frame_hdr*)
  }
}
