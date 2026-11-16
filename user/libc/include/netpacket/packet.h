#ifndef _NETPACKET_PACKET_H
#define _NETPACKET_PACKET_H
#include <sys/socket.h>
#ifndef AF_PACKET
#define AF_PACKET 17
#endif
struct sockaddr_ll {
  sa_family_t sll_family;
  unsigned short sll_protocol;
  int sll_ifindex;
  unsigned short sll_hatype;
  unsigned char sll_pkttype;
  unsigned char sll_halen;
  unsigned char sll_addr[8];
};
#endif
