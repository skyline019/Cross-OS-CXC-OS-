/* Keep PT_LOAD pages clear of the kernel-provided user stack at 0x402000. */
ENTRY(_start)
PHDRS {
  text PT_LOAD FLAGS(5);   /* PF_R|PF_X */
  rodata PT_LOAD FLAGS(4); /* PF_R */
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
  /DISCARD/ : {
    *(.comment)
    *(.note*)
    *(.eh_frame*)
    *(.eh_frame_hdr*)
  }
}
