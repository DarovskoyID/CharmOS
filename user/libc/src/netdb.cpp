#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "net_info.hpp"

namespace {
struct ResultNode {
  addrinfo info;
  sockaddr_storage address;
  char canonical[64];
};
int AddressConfigured(int family) {
  if(family==AF_INET6) return EAI_NONAME; // No configured IPv6 interface yet.
  libc::NetInfo info{};
  if(libc::query_net_info(&info)<0) return EAI_SYSTEM;
  return info.ipv4 ? 0 : EAI_NONAME;
}
bool Decimal(const char* value,uint32_t maximum,uint32_t* output) {
  if(!value || !*value) return false;
  uint32_t number=0;
  for(const char* p=value;*p;++p) {
    if(*p<'0' || *p>'9') return false;
    unsigned digit=static_cast<unsigned>(*p-'0');
    if(number>(maximum-digit)/10) return false;
    number=number*10+digit;
  }
  *output=number;
  return true;
}
int Service(const char* name,bool numeric,int type,uint16_t* port,int* required_type) {
  *port=0; *required_type=0;
  if(!name) return 0;
  uint32_t number=0;
  if(Decimal(name,65535,&number)) { *port=static_cast<uint16_t>(number); return 0; }
  if(numeric) return EAI_NONAME;
  if(!strcmp(name,"http")) { *port=80; *required_type=SOCK_STREAM; }
  else if(!strcmp(name,"https")) { *port=443; *required_type=SOCK_STREAM; }
  else if(!strcmp(name,"domain")) *port=53;
  else return EAI_SERVICE;
  return type && *required_type && type!=*required_type ? EAI_SERVICE : 0;
}
int ResolveV4(const char* name,uint8_t* address) {
  auto net=libc::start_info().net;
  if(!net) { errno=EACCES; return EAI_SYSTEM; }
  auto result=charm::resolve_ipv4(net,name,strlen(name),charm::now_ns()+30000000000ull);
  if(result.ok()) {
    uint32_t wire=static_cast<uint32_t>(result.value);
    memcpy(address,&wire,4);
    return 0;
  }
  using S=charm::Status;
  if(result.status==S::TimedOut || result.status==S::WouldBlock) return EAI_AGAIN;
  if(result.status==S::NotFound || result.status==S::InvalidArgs) return EAI_NONAME;
  errno=libc::errno_from(result.status);
  return EAI_SYSTEM;
}
void MapV4(uint8_t* bytes) {
  uint8_t ipv4[4]; memcpy(ipv4,bytes,4);
  memset(bytes,0,16); bytes[10]=bytes[11]=0xff;
  memcpy(bytes+12,ipv4,4);
}
}
extern "C" void freeaddrinfo(addrinfo* head) {
  while(head) {
    addrinfo* next=head->ai_next;
    // Each result and its address/canonical string are one owned allocation.
    free(head);
    head=next;
  }
}
extern "C" int getaddrinfo(const char* node,const char* service,const addrinfo* hints,addrinfo** output) {
  if(!output) return EAI_FAIL;
  *output=nullptr;
  if(!node && !service) return EAI_NONAME;
  int family=hints ? hints->ai_family : AF_UNSPEC;
  int type=hints ? hints->ai_socktype : 0;
  int protocol=hints ? hints->ai_protocol : 0;
  int flags=hints ? hints->ai_flags : 0;
  constexpr int supported=AI_PASSIVE|AI_CANONNAME|AI_NUMERICHOST|AI_NUMERICSERV|
                          AI_ADDRCONFIG|AI_V4MAPPED|AI_ALL;
  if((flags&~supported) || ((flags&AI_ALL) && !(flags&AI_V4MAPPED))) return EAI_BADFLAGS;
  if(family!=AF_UNSPEC && family!=AF_INET && family!=AF_INET6) return EAI_FAMILY;
  if(type && type!=SOCK_STREAM && type!=SOCK_DGRAM) return EAI_SOCKTYPE;
  if(protocol && protocol!=IPPROTO_TCP && protocol!=IPPROTO_UDP) return EAI_SERVICE;
  if(protocol) {
    int selected=protocol==IPPROTO_TCP ? SOCK_STREAM : SOCK_DGRAM;
    if(type && type!=selected) return EAI_SOCKTYPE;
    type=selected;
  }
  uint16_t port=0;
  int service_type=0;
  int status=Service(service,flags&AI_NUMERICSERV,type,&port,&service_type);
  if(status) return status;
  if(!type && service_type) type=service_type;
  uint8_t address[16]{};
  uint32_t scope=0;
  bool numeric=false;
  bool local_name=node && !strcmp(node,"localhost");
  if(!node) {
    family=family==AF_INET6 ? AF_INET6 : AF_INET;
    if(!(flags&AI_PASSIVE)) address[family==AF_INET ? 0 : 15]=family==AF_INET ? 127 : 1;
    if(family==AF_INET && !(flags&AI_PASSIVE)) address[3]=1;
    numeric=true;
  } else {
    size_t length=strlen(node);
    if(!length || length>253) return EAI_NONAME;
    char literal[254]; memcpy(literal,node,length+1);
    char* zone=strchr(literal,'%');
    if(zone) {
      *zone++=0;
      if(!Decimal(zone,UINT32_MAX,&scope)) return EAI_NONAME;
    }
    if(!zone && family!=AF_INET6 && inet_pton(AF_INET,literal,address)==1) {
      family=AF_INET; numeric=true;
    } else if(family!=AF_INET && inet_pton(AF_INET6,literal,address)==1) {
      family=AF_INET6; numeric=true;
    } else if(!zone && family==AF_INET6 && (flags&AI_V4MAPPED) &&
              inet_pton(AF_INET,literal,address)==1) {
      MapV4(address); numeric=true;
    } else if(zone || (flags&AI_NUMERICHOST)) return EAI_NONAME;
    if(!numeric) {
      // The current DNS capability returns an IPv4 address, not a canonical
      // CNAME. Do not invent an official name for an arbitrary DNS alias.
      if((flags&AI_CANONNAME) && !local_name) return EAI_BADFLAGS;
      if(local_name && family==AF_INET6) address[15]=1;
      else {
        if(family==AF_INET6 && !(flags&AI_V4MAPPED)) return EAI_FAMILY;
        if(local_name) { address[0]=127; address[3]=1; }
        else if((status=ResolveV4(node,address))) return status;
        if(family==AF_INET6) MapV4(address); else family=AF_INET;
      }
    }
  }
  int configured_family=family;
  if(family==AF_INET6) {
    in6_addr aligned{};
    memcpy(&aligned,address,sizeof aligned);
    if(IN6_IS_ADDR_V4MAPPED(&aligned)) configured_family=AF_INET;
  }
  if((flags&AI_ADDRCONFIG) && (status=AddressConfigured(configured_family))) return status;
  addrinfo* head=nullptr;
  addrinfo** tail=&head;
  const int types[]={SOCK_STREAM,SOCK_DGRAM};
  for(int selected:types) {
    if(type && selected!=type) continue;
    auto* result=static_cast<ResultNode*>(calloc(1,sizeof(ResultNode)));
    if(!result) { freeaddrinfo(head); return EAI_MEMORY; }
    result->info.ai_flags=flags;
    result->info.ai_family=family;
    result->info.ai_socktype=selected;
    result->info.ai_protocol=selected==SOCK_STREAM ? IPPROTO_TCP : IPPROTO_UDP;
    result->info.ai_addr=reinterpret_cast<sockaddr*>(&result->address);
    if(family==AF_INET) {
      auto* endpoint=reinterpret_cast<sockaddr_in*>(&result->address);
      endpoint->sin_family=AF_INET; endpoint->sin_port=htons(port);
      memcpy(&endpoint->sin_addr,address,4);
      result->info.ai_addrlen=sizeof *endpoint;
    } else {
      auto* endpoint=reinterpret_cast<sockaddr_in6*>(&result->address);
      endpoint->sin6_family=AF_INET6; endpoint->sin6_port=htons(port);
      endpoint->sin6_scope_id=scope;
      memcpy(&endpoint->sin6_addr,address,16);
      result->info.ai_addrlen=sizeof *endpoint;
    }
    if(!head && node && (flags&AI_CANONNAME)) {
      if(local_name) memcpy(result->canonical,"localhost",10);
      else if(!inet_ntop(family,address,result->canonical,sizeof result->canonical)) {
        free(result); freeaddrinfo(head); return EAI_FAIL;
      }
      result->info.ai_canonname=result->canonical;
    }
    *tail=&result->info;
    tail=&result->info.ai_next;
  }
  *output=head;
  return 0;
}
extern "C" int getnameinfo(const sockaddr* endpoint,socklen_t size,char* host,socklen_t host_size,
                            char* service,socklen_t service_size,int flags) {
  constexpr int supported=NI_NUMERICHOST|NI_NUMERICSERV|NI_NAMEREQD|NI_DGRAM|NI_NOFQDN;
  if(flags&~supported) return EAI_BADFLAGS;
  if(!endpoint || size<sizeof(sa_family_t)) return EAI_FAMILY;
  if((host_size && !host) || (service_size && !service)) return EAI_FAIL;
  if(!host_size && !service_size) return EAI_NONAME;
  uint16_t port=0;
  uint32_t scope=0;
  const void* address=nullptr;
  if(endpoint->sa_family==AF_INET && size>=sizeof(sockaddr_in)) {
    const auto* ipv4=reinterpret_cast<const sockaddr_in*>(endpoint);
    address=&ipv4->sin_addr; port=ntohs(ipv4->sin_port);
  } else if(endpoint->sa_family==AF_INET6 && size>=sizeof(sockaddr_in6)) {
    const auto* ipv6=reinterpret_cast<const sockaddr_in6*>(endpoint);
    address=&ipv6->sin6_addr; port=ntohs(ipv6->sin6_port); scope=ipv6->sin6_scope_id;
  } else return EAI_FAMILY;
  // Reverse DNS is not exposed by the kernel resolver yet. The standard
  // fallback is numeric; NI_NAMEREQD explicitly disallows that fallback.
  if(host_size && (flags&NI_NAMEREQD) && !(flags&NI_NUMERICHOST)) return EAI_NONAME;
  char name[64]{},number[16]{};
  if(host_size && !inet_ntop(endpoint->sa_family,address,name,sizeof name)) return EAI_FAIL;
  if(host_size && scope) {
    size_t used=strlen(name);
    snprintf(name+used,sizeof name-used,"%%%u",scope);
  }
  if(service_size) {
    const char* known=nullptr;
    if(!(flags&NI_NUMERICSERV)) {
      if(port==53) known="domain";
      else if(!(flags&NI_DGRAM) && port==80) known="http";
      else if(!(flags&NI_DGRAM) && port==443) known="https";
    }
    if(known) memcpy(number,known,strlen(known)+1);
    else snprintf(number,sizeof number,"%u",static_cast<unsigned>(port));
  }
  if((host_size && strlen(name)>=host_size) || (service_size && strlen(number)>=service_size))
    return EAI_OVERFLOW;
  if(host_size) memcpy(host,name,strlen(name)+1);
  if(service_size) memcpy(service,number,strlen(number)+1);
  return 0;
}
extern "C" const char* gai_strerror(int error) {
  switch(error) {
    case 0: return "Success";
    case EAI_AGAIN: return "Temporary name resolution failure";
    case EAI_BADFLAGS: return "Unsupported address lookup flags";
    case EAI_FAIL: return "Name resolution failed";
    case EAI_FAMILY: return "Address family unavailable";
    case EAI_MEMORY: return "Out of memory";
    case EAI_NONAME: return "Name or service not known";
    case EAI_SERVICE: return "Service unavailable for socket type";
    case EAI_SOCKTYPE: return "Socket type unavailable";
    case EAI_SYSTEM: return "System error during name resolution";
    case EAI_OVERFLOW: return "Name or service output too small";
    default: return "Unknown address lookup error";
  }
}
