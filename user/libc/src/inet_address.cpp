#include <arpa/inet.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>

extern "C" const in6_addr in6addr_any=IN6ADDR_ANY_INIT;
extern "C" const in6_addr in6addr_loopback=IN6ADDR_LOOPBACK_INIT;

namespace {
bool ParseV4(const char* text,uint8_t* out) {
  uint8_t bytes[4]{};
  for(unsigned part=0;part<4;++part) {
    const char* first=text;
    unsigned value=0,digits=0;
    while(*text>='0' && *text<='9') {
      if(++digits>3) return false;
      value=value*10+static_cast<unsigned>(*text++-'0');
    }
    if(!digits || value>255 || (digits>1 && *first=='0')) return false;
    bytes[part]=static_cast<uint8_t>(value);
    if(part<3) { if(*text!='.') return false; ++text; }
  }
  if(*text) return false;
  memcpy(out,bytes,sizeof bytes);
  return true;
}
int HexDigit(char digit) {
  if(digit>='0' && digit<='9') return digit-'0';
  if(digit>='a' && digit<='f') return digit-'a'+10;
  if(digit>='A' && digit<='F') return digit-'A'+10;
  return -1;
}
bool ParseV6(const char* text,uint8_t* out) {
  uint8_t bytes[16]{};
  size_t used=0,compressed=sizeof bytes;
  if(*text==':') {
    if(text[1]!=':') return false;
    text+=2; compressed=0;
  }
  while(*text) {
    if(used>=sizeof bytes) return false;
    const char* first=text;
    unsigned value=0,digits=0;
    int hex;
    while((hex=HexDigit(*text))>=0) {
      if(++digits>4) return false;
      value=(value<<4)|static_cast<unsigned>(hex);
      ++text;
    }
    if(*text=='.') {
      if(used>sizeof bytes-4 || !ParseV4(first,bytes+used)) return false;
      used+=4;
      // ParseV4 consumed the complete remaining string.
      text+=strlen(text);
      break;
    }
    if(!digits) return false;
    bytes[used++]=static_cast<uint8_t>(value>>8);
    bytes[used++]=static_cast<uint8_t>(value);
    if(!*text) break;
    if(*text++!=':') return false;
    if(*text==':') {
      if(compressed!=sizeof bytes || used==sizeof bytes) return false;
      compressed=used;
      ++text;
    } else if(!*text) return false;
  }
  if(compressed!=sizeof bytes) {
    size_t zeros=sizeof bytes-used;
    if(!zeros) return false;
    memmove(bytes+compressed+zeros,bytes+compressed,used-compressed);
    memset(bytes+compressed,0,zeros);
    used=sizeof bytes;
  }
  if(used!=sizeof bytes) return false;
  memcpy(out,bytes,sizeof bytes);
  return true;
}
char* Decimal(unsigned value,char* out) {
  if(value>=100) *out++=static_cast<char>('0'+value/100);
  if(value>=10) *out++=static_cast<char>('0'+(value/10)%10);
  *out++=static_cast<char>('0'+value%10);
  return out;
}
char* FormatV4(const uint8_t* bytes,char* out) {
  for(unsigned i=0;i<4;++i) {
    if(i) *out++='.';
    out=Decimal(bytes[i],out);
  }
  *out=0;
  return out;
}
void FormatV6(const uint8_t* bytes,char* out) {
  bool mapped=bytes[10]==0xff && bytes[11]==0xff;
  for(unsigned i=0;i<10;++i) mapped=mapped && bytes[i]==0;
  if(mapped) {
    memcpy(out,"::ffff:",7);
    FormatV4(bytes+12,out+7);
    return;
  }
  uint16_t words[8];
  for(unsigned i=0;i<8;++i) words[i]=static_cast<uint16_t>(unsigned{bytes[2*i]}*256+bytes[2*i+1]);
  unsigned best_start=8,best_length=1;
  for(unsigned i=0;i<8;) {
    if(words[i]) { ++i; continue; }
    unsigned start=i;
    while(i<8 && !words[i]) ++i;
    if(i-start>best_length) { best_start=start; best_length=i-start; }
  }
  constexpr char digits[]="0123456789abcdef";
  for(unsigned i=0;i<8;) {
    if(i==best_start) {
      *out++=':'; *out++=':';
      i+=best_length;
      continue;
    }
    if(i && i!=best_start+best_length) *out++=':';
    unsigned word=words[i++];
    bool started=false;
    for(int shift=12;shift>=0;shift-=4) {
      unsigned digit=(word>>shift)&15;
      if(digit || started || !shift) { *out++=digits[digit]; started=true; }
    }
  }
  *out=0;
}
}
extern "C" int inet_pton(int family,const char* text,void* address) {
  if(family!=AF_INET && family!=AF_INET6) { errno=EAFNOSUPPORT; return -1; }
  if(!text || !address) { errno=EFAULT; return -1; }
  return family==AF_INET ? ParseV4(text,static_cast<uint8_t*>(address)) :
                          ParseV6(text,static_cast<uint8_t*>(address));
}
extern "C" const char* inet_ntop(int family,const void* address,char* text,socklen_t capacity) {
  if(family!=AF_INET && family!=AF_INET6) { errno=EAFNOSUPPORT; return nullptr; }
  if(!text || !address) { errno=EFAULT; return nullptr; }
  char result[46];
  if(family==AF_INET) FormatV4(static_cast<const uint8_t*>(address),result);
  else FormatV6(static_cast<const uint8_t*>(address),result);
  size_t size=strlen(result)+1;
  if(size>capacity) { errno=ENOSPC; return nullptr; }
  memcpy(text,result,size);
  return text;
}
