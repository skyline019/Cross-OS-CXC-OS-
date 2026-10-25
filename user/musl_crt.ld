/* Static musl CRT: FILEHDR+PHDRS share the text PT_LOAD so AT_PHDR is valid. */
ENTRY(_start)
PHDRS {
  text PT_LOAD FILEHDR PHDRS FLAGS(5); /* PF_R|PF_X — headers + .text */
  rodata PT_LOAD FLAGS(4);             /* PF_R */
  data PT_LOAD FLAGS(6);               /* PF_R|PF_W */
}
SECTIONS {
  /* Headers occupy [0x400000, 0x400000+SIZEOF_HEADERS); text follows. */
  . = 0x400000 + SIZEOF_HEADERS;
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
