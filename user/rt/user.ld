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
  .text : {
    __charm_text_start = .;
    *(.text .text.*)
    __charm_text_end = .;
  } :text
  . = ALIGN(4096);
  .rodata : { *(.rodata .rodata.*) } :rodata
  . = ALIGN(4096);
  .data : { *(.data .data.*) } :data
  .preinit_array : ALIGN(8) {
    __preinit_array_start = .;
    KEEP(*(.preinit_array))
    __preinit_array_end = .;
  } :data
  .init_array : ALIGN(8) {
    __init_array_start = .;
    KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*))) KEEP(*(.init_array))
    __init_array_end = .;
  } :data
  .fini_array : ALIGN(8) {
    __fini_array_start = .;
    KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*))) KEEP(*(.fini_array))
    __fini_array_end = .;
  } :data
  .bss : { *(.bss .bss.*) *(COMMON) } :data
  /DISCARD/ : { *(.comment) *(.note .note.*) *(.eh_frame .eh_frame_hdr) }
}
