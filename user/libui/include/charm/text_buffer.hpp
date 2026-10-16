#pragma once
#include <charm/text_input.hpp>
#include <string.h>
namespace charm::ui {
template<unsigned Capacity> struct TextBuffer {
  char text[Capacity]{}, previous[Capacity]{};
  unsigned length=0, cursor=0, previous_length=0, previous_cursor=0;
  bool undo_available=false;
  void checkpoint() { memcpy(previous,text,length+1); previous_length=length; previous_cursor=cursor; undo_available=true; }
  bool insert(uint32_t cp, bool all=false) {
    char bytes[4]{}; unsigned n=0;
    if (!append(bytes,sizeof bytes,n,cp) || (all?0:length)+n>=Capacity) return false;
    checkpoint(); if(all) { length=0; cursor=0; text[0]=0; }
    memmove(text+cursor+n,text+cursor,length-cursor+1); memcpy(text+cursor,bytes,n); cursor+=n; length+=n; return true;
  }
  void left() { if(cursor) { do { --cursor; } while(cursor && (static_cast<unsigned char>(text[cursor])&0xc0)==0x80); } }
  void right() { if(cursor<length) { ++cursor; while(cursor<length && (static_cast<unsigned char>(text[cursor])&0xc0)==0x80) ++cursor; } }
  bool erase(bool all=false) {
    if(!length || (!cursor && !all)) return false;
    checkpoint(); if(all) { length=0; cursor=0; text[0]=0; return true; }
    unsigned end=cursor; left(); memmove(text+cursor,text+end,length-end+1); length-=end-cursor; return true;
  }
  bool undo() {
    if(!undo_available) return false;
    for(unsigned i=0;i<Capacity;++i) { char c=text[i]; text[i]=previous[i]; previous[i]=c; }
    unsigned n=length; length=previous_length; previous_length=n; n=cursor; cursor=previous_cursor; previous_cursor=n; return true;
  }
  unsigned line_start() const { unsigned p=cursor; while(p && text[p-1]!='\n') --p; return p; }
  unsigned line_end() const { unsigned p=cursor; while(p<length && text[p]!='\n') ++p; return p; }
};
}
