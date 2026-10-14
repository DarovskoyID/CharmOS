/* Раскладка ядра по docs/abi/boot.md §3: верхние 2 ГиБ, сегменты по правам,
   без общих страниц между ними (W^X). Символы __*_start/__*_end использует
   kernel/mm/vmm.cpp, чтобы перенести права секций в таблицы ядра. */
ENTRY(kernel_entry)

PHDRS {
  text   PT_LOAD FLAGS(5);   /* R X */
  rodata PT_LOAD FLAGS(4);   /* R   */
  data   PT_LOAD FLAGS(6);   /* R W */
}

SECTIONS {
  . = 0xffffffff80000000;

  __text_start = .;
  .text : { *(.text .text.*) } :text
  . = ALIGN(4096);
  __text_end = .;

  __rodata_start = .;
  .rodata : { *(.rodata .rodata.*) } :rodata
  . = ALIGN(4096);
  __rodata_end = .;

  __data_start = .;
  .data : { *(.data .data.*) } :data
  .bss : { *(.bss .bss.*) *(COMMON) } :data
  . = ALIGN(4096);
  __kernel_end = .;

  /DISCARD/ : { *(.comment) *(.note .note.*) *(.eh_frame .eh_frame_hdr) }
}
