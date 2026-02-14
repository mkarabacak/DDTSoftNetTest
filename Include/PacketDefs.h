/** @file
  Network packet structure definitions.
  Ethernet, IP, TCP, UDP, ARP, ICMP headers.
**/

#ifndef PACKET_DEFS_H_
#define PACKET_DEFS_H_

#include <Uefi.h>

#pragma pack(1)

//
// Ethernet header (14 bytes)
//
typedef struct {
  UINT8     DstMac[6];
  UINT8     SrcMac[6];
  UINT16    EtherType;
} ETHERNET_HEADER;

#define ETHERTYPE_IPV4  0x0800
#define ETHERTYPE_ARP   0x0806
#define ETHERTYPE_IPV6  0x86DD

//
// ARP header (28 bytes for IPv4)
//
typedef struct {
  UINT16    HardwareType;
  UINT16    ProtocolType;
  UINT8     HardwareLen;
  UINT8     ProtocolLen;
  UINT16    Operation;
  UINT8     SenderMac[6];
  UINT8     SenderIp[4];
  UINT8     TargetMac[6];
  UINT8     TargetIp[4];
} ARP_HEADER;

#define ARP_OP_REQUEST  1
#define ARP_OP_REPLY    2

//
// IPv4 header (20 bytes minimum)
//
typedef struct {
  UINT8     VersionIhl;
  UINT8     Tos;
  UINT16    TotalLength;
  UINT16    Identification;
  UINT16    FlagsFragOffset;
  UINT8     Ttl;
  UINT8     Protocol;
  UINT16    HeaderChecksum;
  UINT8     SrcAddr[4];
  UINT8     DstAddr[4];
} IPV4_HEADER;

#define IP_PROTO_ICMP  1
#define IP_PROTO_TCP   6
#define IP_PROTO_UDP   17

//
// ICMP header (8 bytes)
//
typedef struct {
  UINT8     Type;
  UINT8     Code;
  UINT16    Checksum;
  UINT16    Identifier;
  UINT16    SequenceNumber;
} ICMP_HEADER;

#define ICMP_TYPE_ECHO_REPLY    0
#define ICMP_TYPE_ECHO_REQUEST  8

//
// TCP header (20 bytes minimum)
//
typedef struct {
  UINT16    SrcPort;
  UINT16    DstPort;
  UINT32    SeqNumber;
  UINT32    AckNumber;
  UINT8     DataOffsetReserved;
  UINT8     Flags;
  UINT16    WindowSize;
  UINT16    Checksum;
  UINT16    UrgentPointer;
} TCP_HEADER;

#define TCP_FLAG_FIN  0x01
#define TCP_FLAG_SYN  0x02
#define TCP_FLAG_RST  0x04
#define TCP_FLAG_PSH  0x08
#define TCP_FLAG_ACK  0x10
#define TCP_FLAG_URG  0x20

//
// UDP header (8 bytes)
//
typedef struct {
  UINT16    SrcPort;
  UINT16    DstPort;
  UINT16    Length;
  UINT16    Checksum;
} UDP_HEADER;

#pragma pack()

//
// Maximum sizes
//
#define MAX_ETHERNET_FRAME_SIZE  1518
#define MIN_ETHERNET_FRAME_SIZE  64
#define MAX_IP_PACKET_SIZE       65535
#define DEFAULT_MTU              1500

#endif // PACKET_DEFS_H_
