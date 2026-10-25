/* User stack is at 0x418000 (above brk window ending ~0x415000).
 * .data/.bss live at 0x402000 so mini_libc errno and future CRT can use BSS. */
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
