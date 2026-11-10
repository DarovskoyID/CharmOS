#ifndef _LINK_H
#define _LINK_H
/* Minimal ELF loader view for portable code that only needs ElfW() and
   link_map metadata. CharmOS has no dynamic loader, so the structures are
   present for source compatibility and are never populated. */
#include <elf.h>
#include <stdint.h>

#ifndef ElfW
#define ElfW(type) Elf64_##type
#endif

struct link_map {
  uintptr_t l_addr;
  char* l_name;
  void* l_ld;
  struct link_map* l_next;
  struct link_map* l_prev;
};

#endif
