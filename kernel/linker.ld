/* Раскладка ядра по docs/abi/boot.md §3: верхние 2 ГиБ, сегменты по правам,
   без общих страниц между ними (W^X). */
ENTRY(kernel_entry)

PHDRS {
  text   PT_LOAD FLAGS(5);   /* R X */
  rodata PT_LOAD FLAGS(4);   /* R   */
  data   PT_LOAD FLAGS(6);   /* R W */
}

SECTIONS {
  . = 0xffffffff80000000;

  .text : { *(.text .text.*) } :text

  . = ALIGN(4096);
  .rodata : { *(.rodata .rodata.*) } :rodata

  . = ALIGN(4096);
  .data : { *(.data .data.*) } :data
  .bss : { *(.bss .bss.*) *(COMMON) } :data

  /DISCARD/ : { *(.comment) *(.note .note.*) *(.eh_frame .eh_frame_hdr) }
}
