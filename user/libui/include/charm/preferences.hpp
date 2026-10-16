#pragma once
#include <stdint.h>
namespace charm::ui {
// Disk format v1, documented in plans/DESKTOP-APPS.md. Not a syscall ABI.
struct Preferences { uint32_t magic, version, generation, flags, checksum; };
static_assert(sizeof(Preferences)==20);
inline uint32_t preference_checksum(const Preferences& p) {
  uint32_t hash=2166136261u;
  const uint32_t words[]={p.magic,p.version,p.generation,p.flags};
  for (uint32_t word : words) for (unsigned i=0;i<4;++i) { hash^=(word>>(i*8))&255u; hash*=16777619u; }
  return hash;
}
inline Preferences preferences(uint32_t generation,uint32_t flags) {
  Preferences p{0x46504348,1,generation,flags,0}; p.checksum=preference_checksum(p); return p;
}
inline bool valid(const Preferences& p) { return p.magic==0x46504348 && p.version==1 && p.generation!=0 && (p.flags&~7u)==0 && p.checksum==preference_checksum(p); }
}
