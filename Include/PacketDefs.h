/** @file
  Network packet structure definitions.
  Ethernet, IP, TCP, UDP, ARP, ICMP headers.
  Byte order macros, parsed packet structure, builder/parser declarations.
**/

#ifndef PACKET_DEFS_H_
#define PACKET_DEFS_H_

#include <Uefi.h>
#include <Library/BaseLib.h>

//
// Byte order conversion (x86_64 is little-endian, network is big-endian)
//
#define HTONS(x)  SwapBytes16(x)
#define NTOHS(x)  SwapBytes16(x)
#define HTONL(x)  SwapBytes32(x)
#define NTOHL(x)  SwapBytes32(x)

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
#define ARP_HW_ETHERNET 1

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

#define IP_FLAG_DF     0x4000
#define IP_FLAG_MF     0x2000
#define IP_FRAG_MASK   0x1FFF

#define IPV4_VERSION_IHL(ver, ihl)  (((ver) << 4) | (ihl))
#define IPV4_IHL(verihl)            ((verihl) & 0x0F)
#define IPV4_VERSION(verihl)        (((verihl) >> 4) & 0x0F)
#define IPV4_HDR_LEN(verihl)        (IPV4_IHL(verihl) * 4)

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

#define ICMP_TYPE_ECHO_REPLY      0
#define ICMP_TYPE_DEST_UNREACH    3
#define ICMP_TYPE_ECHO_REQUEST    8
#define ICMP_TYPE_TIME_EXCEEDED   11

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

#define TCP_DATA_OFFSET(doff)  (((doff) >> 4) & 0x0F)
#define TCP_HDR_LEN(doff)      (TCP_DATA_OFFSET(doff) * 4)

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
#define ETHERNET_HEADER_SIZE     14
#define IPV4_MIN_HEADER_SIZE     20
#define ICMP_HEADER_SIZE         8
#define TCP_MIN_HEADER_SIZE      20
#define UDP_HEADER_SIZE          8
#define ARP_HEADER_SIZE          28

//
// Broadcast MAC
//
#define ETHERNET_BROADCAST_MAC   { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }

//
// Parsed packet result (pointers into original buffer, no copy)
//
typedef struct {
  BOOLEAN           Valid;

  // Layer 2
  BOOLEAN           HasEthernet;
  ETHERNET_HEADER   *Ethernet;
  UINT16            EtherType;

  // Layer 3
  BOOLEAN           HasIpv4;
  IPV4_HEADER       *Ipv4;
  BOOLEAN           HasArp;
  ARP_HEADER        *Arp;

  // Layer 4
  BOOLEAN           HasIcmp;
  ICMP_HEADER       *Icmp;
  BOOLEAN           HasTcp;
  TCP_HEADER        *Tcp;
  BOOLEAN           HasUdp;
  UDP_HEADER        *Udp;

  // Payload (after all headers)
  UINT8             *Payload;
  UINTN             PayloadLength;

  // Checksum validation
  BOOLEAN           IpChecksumValid;
  BOOLEAN           L4ChecksumValid;
} PARSED_PACKET;

//
// ============================================================
// PacketBuilder functions (PacketBuilder.c)
// ============================================================
//

//
// Internet checksum (RFC 1071)
//
UINT16 PktChecksum    (IN CONST UINT8 *Data, IN UINTN Length);

//
// Pseudo-header checksum for TCP/UDP
//
UINT16 PktPseudoChecksum (
  IN CONST UINT8  *SrcIp,
  IN CONST UINT8  *DstIp,
  IN UINT8        Protocol,
  IN UINT16       Length,
  IN CONST UINT8  *Data,
  IN UINTN        DataLength
  );

//
// Low-level header builders (return bytes written)
//
UINTN PktBuildEthernetHeader (
  OUT UINT8        *Buffer,
  IN  CONST UINT8  *DstMac,
  IN  CONST UINT8  *SrcMac,
  IN  UINT16       EtherType
  );

UINTN PktBuildIpv4Header (
  OUT UINT8        *Buffer,
  IN  CONST UINT8  *SrcIp,
  IN  CONST UINT8  *DstIp,
  IN  UINT8        Protocol,
  IN  UINT16       PayloadLength,
  IN  UINT8        Ttl
  );

//
// High-level packet builders (return total frame size)
//
UINTN PktBuildArpRequest (
  OUT UINT8        *Buffer,
  IN  CONST UINT8  *SrcMac,
  IN  CONST UINT8  *SrcIp,
  IN  CONST UINT8  *TargetIp
  );

UINTN PktBuildArpReply (
  OUT UINT8        *Buffer,
  IN  CONST UINT8  *SrcMac,
  IN  CONST UINT8  *SrcIp,
  IN  CONST UINT8  *DstMac,
  IN  CONST UINT8  *DstIp
  );

UINTN PktBuildIcmpEchoRequest (
  OUT UINT8        *Buffer,
  IN  CONST UINT8  *SrcMac,
  IN  CONST UINT8  *DstMac,
  IN  CONST UINT8  *SrcIp,
  IN  CONST UINT8  *DstIp,
  IN  UINT16       Identifier,
  IN  UINT16       SequenceNumber,
  IN  CONST UINT8  *Data      OPTIONAL,
  IN  UINTN        DataLength
  );

UINTN PktBuildUdpPacket (
  OUT UINT8        *Buffer,
  IN  CONST UINT8  *SrcMac,
  IN  CONST UINT8  *DstMac,
  IN  CONST UINT8  *SrcIp,
  IN  CONST UINT8  *DstIp,
  IN  UINT16       SrcPort,
  IN  UINT16       DstPort,
  IN  CONST UINT8  *Data      OPTIONAL,
  IN  UINTN        DataLength
  );

UINTN PktBuildTcpPacket (
  OUT UINT8        *Buffer,
  IN  CONST UINT8  *SrcMac,
  IN  CONST UINT8  *DstMac,
  IN  CONST UINT8  *SrcIp,
  IN  CONST UINT8  *DstIp,
  IN  UINT16       SrcPort,
  IN  UINT16       DstPort,
  IN  UINT32       SeqNum,
  IN  UINT32       AckNum,
  IN  UINT8        Flags,
  IN  UINT16       WindowSize,
  IN  CONST UINT8  *Data      OPTIONAL,
  IN  UINTN        DataLength
  );

//
// ============================================================
// PacketParser functions (PacketParser.c)
// ============================================================
//

//
// Full packet parser
//
EFI_STATUS PktParsePacket (
  IN  CONST UINT8    *Buffer,
  IN  UINTN          Length,
  OUT PARSED_PACKET  *Parsed
  );

//
// Individual checksum validators
//
BOOLEAN PktValidateIpChecksum   (IN CONST IPV4_HEADER *Ip);
BOOLEAN PktValidateIcmpChecksum (IN CONST ICMP_HEADER *Icmp, IN UINTN IcmpLength);
BOOLEAN PktValidateTcpChecksum  (IN CONST IPV4_HEADER *Ip, IN CONST TCP_HEADER *Tcp, IN UINTN TcpLength);
BOOLEAN PktValidateUdpChecksum  (IN CONST IPV4_HEADER *Ip, IN CONST UDP_HEADER *Udp, IN UINTN UdpLength);

//
// Protocol name helpers
//
CONST CHAR16 * PktGetEtherTypeName  (IN UINT16 EtherType);
CONST CHAR16 * PktGetIpProtocolName (IN UINT8 Protocol);
CONST CHAR16 * PktGetIcmpTypeName   (IN UINT8 Type);
CONST CHAR16 * PktGetTcpFlagsStr    (IN UINT8 Flags, OUT CHAR16 *Buffer, IN UINTN BufferSize);

#endif // PACKET_DEFS_H_
