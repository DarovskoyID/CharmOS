#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#define VERIFY(condition) do { if(!(condition)) { printf("network addresses: failed line %d\n",__LINE__); return 1; } } while(0)
namespace {
struct AddressCase { const char* input; const char* canonical; };
constexpr AddressCase cases[]={
  {"::","::"}, {"::1","::1"},
  {"2001:0DB8:0000:0000:0000:0000:0000:0001","2001:db8::1"},
  {"2001:db8:0:1:0:0:0:1","2001:db8:0:1::1"},
  {"2001:0:0:1:0:0:1:1","2001::1:0:0:1:1"},
  {"2001:db8:0:1:2:3:4:5","2001:db8:0:1:2:3:4:5"},
  {"ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff","ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"},
  {"::ffff:192.0.2.128","::ffff:192.0.2.128"},
  {"0:0:0:0:0:ffff:c000:0280","::ffff:192.0.2.128"},
  {"1:2:3:4:5:6:192.0.2.1","1:2:3:4:5:6:c000:201"},
  {"fe80::1234","fe80::1234"}, {"ff02::1","ff02::1"},
};
constexpr const char* invalid6[]={
  "",":",":::1","1::2::3","1:2:3:4:5:6:7","1:2:3:4:5:6:7:8:9",
  "1:2:3:4:5:6:7:8::","1:2:3:4:5:6:7::8","12345::","::g","::1:",
  "2001:db8::1%2","::ffff:256.0.0.1","::ffff:192.0.2","192.0.2.1",
};
constexpr const char* invalid4[]={
  "","1.2.3","1.2.3.4.5","256.0.0.1","01.2.3.4","0x7f.0.0.1",
  "1..2.3","1.2.3.-1","1.2.3.4 ","429496729600.1.2.3",
};
}
extern "C" int main() {
  for(const auto& example:cases) {
    uint8_t address[16]{},again[16]{};
    char text[INET6_ADDRSTRLEN]{};
    VERIFY(inet_pton(AF_INET6,example.input,address)==1);
    VERIFY(inet_ntop(AF_INET6,address,text,sizeof text)==text);
    VERIFY(strcmp(text,example.canonical)==0);
    VERIFY(inet_pton(AF_INET6,text,again)==1 && memcmp(address,again,sizeof address)==0);
  }
  for(const char* text:invalid6) {
    uint8_t destination[16],saved[16];
    memset(destination,0xa5,sizeof destination); memcpy(saved,destination,sizeof saved);
    VERIFY(inet_pton(AF_INET6,text,destination)==0 && !memcmp(destination,saved,sizeof saved));
  }
  for(const char* text:invalid4) {
    uint8_t destination[4]={1,2,3,4};
    VERIFY(inet_pton(AF_INET,text,destination)==0 && destination[0]==1 && destination[3]==4);
  }
  uint32_t state=0x12345678;
  for(unsigned iteration=0;iteration<1000;++iteration) {
    uint8_t original[16],decoded[16];
    for(auto& byte:original) {
      state=state*1664525u+1013904223u;
      byte=static_cast<uint8_t>(state>>24);
    }
    char text[INET6_ADDRSTRLEN]{};
    VERIFY(inet_ntop(AF_INET6,original,text,sizeof text)==text &&
           inet_pton(AF_INET6,text,decoded)==1 && !memcmp(original,decoded,sizeof original));
  }
  alignas(in6_addr) uint8_t address[16]{};
  char tiny[4]={'k','e','e','p'};
  VERIFY(inet_pton(AF_INET6,"2001:db8::1",address)==1);
  errno=0;
  VERIFY(!inet_ntop(AF_INET6,address,tiny,sizeof tiny) && errno==ENOSPC && !memcmp(tiny,"keep",4));
  VERIFY(inet_pton(AF_INET6,"::",address)==1 && IN6_IS_ADDR_UNSPECIFIED(reinterpret_cast<in6_addr*>(address)));
  VERIFY(inet_pton(AF_INET6,"::1",address)==1 && IN6_IS_ADDR_LOOPBACK(reinterpret_cast<in6_addr*>(address)));
  VERIFY(inet_pton(AF_INET6,"::ffff:192.0.2.1",address)==1 && IN6_IS_ADDR_V4MAPPED(reinterpret_cast<in6_addr*>(address)));
  errno=0;
  VERIFY(inet_pton(-1,"anything",address)==-1 && errno==EAFNOSUPPORT);
  errno=0;
  VERIFY(socket(AF_INET6,SOCK_STREAM,0)==-1 && errno==EAFNOSUPPORT);
  printf("network addresses: IPv4/IPv6 parsing, canonical text, bounds and transport distinction ok\n");
  return 0;
}
