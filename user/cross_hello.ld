/* User stack is at 0x420000 (above brk window 0x409000 + 16 pages).
 * mini_libc .data/.bss at 0x402000; musl-gcc fopen may place .data ~0x408000. */
ENTRY(_start)
PHDRS {
  text PT_LOAD FLAGS(5);   /* PF_R|PF_X */
  rodata PT_LOAD FLAGS(4); /* PF_R */
  data PT_LOAD FLAGS(6);   /* PF_R|PF_W */
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
  . = ALIGN(4096);
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
