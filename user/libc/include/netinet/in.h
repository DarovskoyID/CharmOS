#ifndef _NETINET_IN_H
#define _NETINET_IN_H
#include <stdint.h>
#include <sys/socket.h>
typedef uint32_t in_addr_t;
typedef uint16_t in_port_t;
struct in_addr {
  in_addr_t s_addr;
};
struct sockaddr_in {
  sa_family_t sin_family;
  in_port_t sin_port;
  struct in_addr sin_addr;
  char sin_zero[8];
};
struct in6_addr {
  union { uint8_t s6_addr[16]; uint32_t __alignment[4]; };
};
struct sockaddr_in6 {
  sa_family_t sin6_family;
  in_port_t sin6_port;
  uint32_t sin6_flowinfo;
  struct in6_addr sin6_addr;
  uint32_t sin6_scope_id;
};
#define INET_ADDRSTRLEN 16
#define INET6_ADDRSTRLEN 46
#define IP_MTU_DISCOVER 10
#define IP_PMTUDISC_DONT 0
#define IP_PMTUDISC_WANT 1
#define IP_PMTUDISC_DO 2
#define IP_PMTUDISC_PROBE 3
#define IPV6_MTU_DISCOVER 23
#define IPV6_PMTUDISC_DONT 0
#define IPV6_PMTUDISC_WANT 1
#define IPV6_PMTUDISC_DO 2
#define IPV6_PMTUDISC_PROBE 3
#define IN6ADDR_ANY_INIT {{{0}}}
#define IN6ADDR_LOOPBACK_INIT {{{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1}}}
#ifdef __cplusplus
extern "C" {
#endif
extern const struct in6_addr in6addr_any;
extern const struct in6_addr in6addr_loopback;
#ifdef __cplusplus
}
static_assert(sizeof(in6_addr)==16 && sizeof(sockaddr_in6)==28);
static_assert(__builtin_offsetof(sockaddr_in6,sin6_addr)==8 && __builtin_offsetof(sockaddr_in6,sin6_scope_id)==24);
#endif
static inline int __charm_in6_zeros(const struct in6_addr* a,unsigned count) {
  for(unsigned i=0;i<count;++i) if(a->s6_addr[i]) return 0;
  return 1;
}
static inline int __charm_in6_equal(const struct in6_addr* a,const struct in6_addr* b) {
  for(unsigned i=0;i<16;++i) if(a->s6_addr[i]!=b->s6_addr[i]) return 0;
  return 1;
}
#define IN6_IS_ADDR_UNSPECIFIED(a) (__charm_in6_zeros((a),16))
#define IN6_IS_ADDR_LOOPBACK(a) (__charm_in6_zeros((a),15) && (a)->s6_addr[15]==1)
#define IN6_IS_ADDR_V4MAPPED(a) (__charm_in6_zeros((a),10) && (a)->s6_addr[10]==255 && (a)->s6_addr[11]==255)
#define IN6_IS_ADDR_MULTICAST(a) ((a)->s6_addr[0]==255)
#define IN6_IS_ADDR_LINKLOCAL(a) ((a)->s6_addr[0]==254 && ((a)->s6_addr[1]&192)==128)
#define IN6_IS_ADDR_SITELOCAL(a) ((a)->s6_addr[0]==254 && ((a)->s6_addr[1]&192)==192)
#define IN6_ARE_ADDR_EQUAL(a,b) (__charm_in6_equal((a),(b)))
#define INADDR_ANY ((in_addr_t)0)
#define INADDR_LOOPBACK ((in_addr_t)0x7f000001)
#define INADDR_BROADCAST ((in_addr_t)0xffffffff)
#define INADDR_NONE INADDR_BROADCAST
#define IPPROTO_IP 0
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17
#define IPPROTO_IPV6 41
#define IPPROTO_ICMPV6 58
#define IPV6_V6ONLY 26
/* Опции сокета IP/IPv6 (Linux-совместимые значения). Мультикаст ядром CharmOS
   не реализован: setsockopt вернёт ENOTSUP; константы нужны для компиляции. */
#define IP_TOS 1
#define IP_RECVTOS 13
#define IP_MULTICAST_IF 32
#define IP_MULTICAST_TTL 33
#define IP_MULTICAST_LOOP 34
#define IP_ADD_MEMBERSHIP 35
#define IP_DROP_MEMBERSHIP 36
#define IP_DEFAULT_MULTICAST_TTL 1
#define IP_DEFAULT_MULTICAST_LOOP 1
#define IPV6_MULTICAST_HOPS 18
#define IPV6_MULTICAST_IF 17
#define IPV6_MULTICAST_LOOP 19
#define IPV6_ADD_MEMBERSHIP 20
#define IPV6_DROP_MEMBERSHIP 21
#define IPV6_JOIN_GROUP IPV6_ADD_MEMBERSHIP
#define IPV6_LEAVE_GROUP IPV6_DROP_MEMBERSHIP
#define MCAST_JOIN_SOURCE_GROUP 46
#define MCAST_LEAVE_SOURCE_GROUP 47
#define IPV6_RECVTCLASS 66
#define IPV6_TCLASS 67
struct ip_mreq {
  struct in_addr imr_multiaddr;
  struct in_addr imr_interface;
};
struct ip_mreq_source {
  struct in_addr imr_multiaddr;
  struct in_addr imr_sourceaddr;
  struct in_addr imr_interface;
};
/* Source-compatible request layouts only. Native setsockopt rejects multicast
   operations with ENOTSUP; these structures are never kernel wire messages. */
struct ip_mreqn {
  struct in_addr imr_multiaddr;
  struct in_addr imr_address;
  int imr_ifindex;
};
struct ipv6_mreq {
  struct in6_addr ipv6mr_multiaddr;
  unsigned int ipv6mr_interface;
};
struct group_source_req {
  uint32_t gsr_interface;
  struct sockaddr_storage gsr_group;
  struct sockaddr_storage gsr_source;
};
static inline uint16_t htons(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }
static inline uint16_t ntohs(uint16_t v) { return htons(v); }
static inline uint32_t htonl(uint32_t v) { return __builtin_bswap32(v); }
static inline uint32_t ntohl(uint32_t v) { return __builtin_bswap32(v); }
#endif
