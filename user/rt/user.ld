/* Раскладка программы пользователя: с 4 МиБ, сегменты по правам (W^X),
   без общих страниц — ядро (lib/elf) иначе откажется загружать образ. */
ENTRY(_start)

PHDRS {
  text   PT_LOAD FLAGS(5);
  rodata PT_LOAD FLAGS(4);
  data   PT_LOAD FLAGS(6);
}

SECTIONS {
  . = 0x400000;
  .text : { *(.text .text.*) } :text
  . = ALIGN(4096);
  .rodata : { *(.rodata .rodata.*) } :rodata
  . = ALIGN(4096);
  .data : { *(.data .data.*) } :data
  .bss : { *(.bss .bss.*) *(COMMON) } :data
  /DISCARD/ : { *(.comment) *(.note .note.*) *(.eh_frame .eh_frame_hdr) }
}
