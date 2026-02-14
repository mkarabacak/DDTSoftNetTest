/** @file
  Network packet builder.
  Constructs Ethernet, ARP, IPv4, ICMP, UDP, TCP frames.
  Internet checksum (RFC 1071) and pseudo-header checksum.
**/

#include <DDTSoftNetTest.h>
#include <PacketDefs.h>

//
// ============================================================
// Checksum routines
// ============================================================
//

/**
  Compute Internet checksum per RFC 1071.
  Works on any data buffer — used for IP header, ICMP, etc.

  @param[in] Data    Pointer to data.
  @param[in] Length  Byte count.

  @return 16-bit one's complement checksum.
**/
UINT16
PktChecksum (
  IN CONST UINT8  *Data,
  IN UINTN        Length
  )
{
  UINT32  Sum;
  UINTN   Index;

  Sum = 0;

  //
  // Sum 16-bit words
  //
  for (Index = 0; Index + 1 < Length; Index += 2) {
    Sum += (UINT16)((Data[Index] << 8) | Data[Index + 1]);
  }

  //
  // If odd byte, pad with zero
  //
  if (Index < Length) {
    Sum += (UINT16)(Data[Index] << 8);
  }

  //
  // Fold 32-bit sum into 16 bits
  //
  while (Sum >> 16) {
    Sum = (Sum & 0xFFFF) + (Sum >> 16);
  }

  return (UINT16)(~Sum);
}

/**
  Compute TCP/UDP checksum with IPv4 pseudo-header.

  @param[in] SrcIp       Source IP (4 bytes).
  @param[in] DstIp       Destination IP (4 bytes).
  @param[in] Protocol    IP protocol number (6=TCP, 17=UDP).
  @param[in] Length       L4 segment length (header + data).
  @param[in] Data        Pointer to L4 header + data.
  @param[in] DataLength  Actual data buffer length.

  @return 16-bit one's complement checksum.
**/
UINT16
PktPseudoChecksum (
  IN CONST UINT8  *SrcIp,
  IN CONST UINT8  *DstIp,
  IN UINT8        Protocol,
  IN UINT16       Length,
  IN CONST UINT8  *Data,
  IN UINTN        DataLength
  )
{
  UINT32  Sum;
  UINTN   Index;

  Sum = 0;

  //
  // Pseudo-header: SrcIp(4) + DstIp(4) + Zero(1) + Protocol(1) + Length(2)
  //
  Sum += (UINT16)((SrcIp[0] << 8) | SrcIp[1]);
  Sum += (UINT16)((SrcIp[2] << 8) | SrcIp[3]);
  Sum += (UINT16)((DstIp[0] << 8) | DstIp[1]);
  Sum += (UINT16)((DstIp[2] << 8) | DstIp[3]);
  Sum += (UINT16)Protocol;
  Sum += Length;

  //
  // Sum the L4 data
  //
  for (Index = 0; Index + 1 < DataLength; Index += 2) {
    Sum += (UINT16)((Data[Index] << 8) | Data[Index + 1]);
  }

  if (Index < DataLength) {
    Sum += (UINT16)(Data[Index] << 8);
  }

  //
  // Fold
  //
  while (Sum >> 16) {
    Sum = (Sum & 0xFFFF) + (Sum >> 16);
  }

  return (UINT16)(~Sum);
}

//
// ============================================================
// Low-level header builders
// ============================================================
//

/**
  Build Ethernet header at Buffer.

  @param[out] Buffer     Output buffer (at least 14 bytes).
  @param[in]  DstMac     Destination MAC (6 bytes).
  @param[in]  SrcMac     Source MAC (6 bytes).
  @param[in]  EtherType  EtherType in host byte order.

  @return Bytes written (always 14).
**/
UINTN
PktBuildEthernetHeader (
  OUT UINT8        *Buffer,
  IN  CONST UINT8  *DstMac,
  IN  CONST UINT8  *SrcMac,
  IN  UINT16       EtherType
  )
{
  ETHERNET_HEADER  *Eth;

  Eth = (ETHERNET_HEADER *)Buffer;
  CopyMem (Eth->DstMac, DstMac, 6);
  CopyMem (Eth->SrcMac, SrcMac, 6);
  Eth->EtherType = HTONS (EtherType);

  return ETHERNET_HEADER_SIZE;
}

/**
  Build IPv4 header at Buffer.
  Sets Version=4, IHL=5 (no options), ID=0, DF flag, computed checksum.

  @param[out] Buffer         Output buffer (at least 20 bytes).
  @param[in]  SrcIp          Source IP (4 bytes).
  @param[in]  DstIp          Destination IP (4 bytes).
  @param[in]  Protocol       IP protocol (ICMP=1, TCP=6, UDP=17).
  @param[in]  PayloadLength  Bytes after IP header.
  @param[in]  Ttl            Time to live.

  @return Bytes written (always 20).
**/
UINTN
PktBuildIpv4Header (
  OUT UINT8        *Buffer,
  IN  CONST UINT8  *SrcIp,
  IN  CONST UINT8  *DstIp,
  IN  UINT8        Protocol,
  IN  UINT16       PayloadLength,
  IN  UINT8        Ttl
  )
{
  IPV4_HEADER  *Ip;

  Ip = (IPV4_HEADER *)Buffer;
  Ip->VersionIhl      = IPV4_VERSION_IHL (4, 5);
  Ip->Tos             = 0;
  Ip->TotalLength     = HTONS ((UINT16)(IPV4_MIN_HEADER_SIZE + PayloadLength));
  Ip->Identification  = 0;
  Ip->FlagsFragOffset = HTONS (IP_FLAG_DF);
  Ip->Ttl             = Ttl;
  Ip->Protocol        = Protocol;
  Ip->HeaderChecksum  = 0;
  CopyMem (Ip->SrcAddr, SrcIp, 4);
  CopyMem (Ip->DstAddr, DstIp, 4);

  //
  // Compute IP header checksum
  //
  Ip->HeaderChecksum = HTONS (PktChecksum (Buffer, IPV4_MIN_HEADER_SIZE));

  return IPV4_MIN_HEADER_SIZE;
}

//
// ============================================================
// High-level packet builders
// ============================================================
//

/**
  Build a complete ARP Request frame.

  @param[out] Buffer    Output buffer (at least 42 bytes).
  @param[in]  SrcMac    Sender MAC (6 bytes).
  @param[in]  SrcIp     Sender IP (4 bytes).
  @param[in]  TargetIp  Target IP (4 bytes).

  @return Total frame size (42 bytes).
**/
UINTN
PktBuildArpRequest (
  OUT UINT8        *Buffer,
  IN  CONST UINT8  *SrcMac,
  IN  CONST UINT8  *SrcIp,
  IN  CONST UINT8  *TargetIp
  )
{
  UINTN        Offset;
  ARP_HEADER   *Arp;
  UINT8        BroadcastMac[6] = ETHERNET_BROADCAST_MAC;

  Offset = PktBuildEthernetHeader (Buffer, BroadcastMac, SrcMac, ETHERTYPE_ARP);

  Arp = (ARP_HEADER *)(Buffer + Offset);
  Arp->HardwareType = HTONS (ARP_HW_ETHERNET);
  Arp->ProtocolType = HTONS (ETHERTYPE_IPV4);
  Arp->HardwareLen  = 6;
  Arp->ProtocolLen  = 4;
  Arp->Operation    = HTONS (ARP_OP_REQUEST);
  CopyMem (Arp->SenderMac, SrcMac, 6);
  CopyMem (Arp->SenderIp, SrcIp, 4);
  ZeroMem (Arp->TargetMac, 6);
  CopyMem (Arp->TargetIp, TargetIp, 4);

  return Offset + ARP_HEADER_SIZE;
}

/**
  Build a complete ARP Reply frame.

  @param[out] Buffer  Output buffer (at least 42 bytes).
  @param[in]  SrcMac  Sender MAC (6 bytes).
  @param[in]  SrcIp   Sender IP (4 bytes).
  @param[in]  DstMac  Target MAC (6 bytes).
  @param[in]  DstIp   Target IP (4 bytes).

  @return Total frame size (42 bytes).
**/
UINTN
PktBuildArpReply (
  OUT UINT8        *Buffer,
  IN  CONST UINT8  *SrcMac,
  IN  CONST UINT8  *SrcIp,
  IN  CONST UINT8  *DstMac,
  IN  CONST UINT8  *DstIp
  )
{
  UINTN        Offset;
  ARP_HEADER   *Arp;

  Offset = PktBuildEthernetHeader (Buffer, DstMac, SrcMac, ETHERTYPE_ARP);

  Arp = (ARP_HEADER *)(Buffer + Offset);
  Arp->HardwareType = HTONS (ARP_HW_ETHERNET);
  Arp->ProtocolType = HTONS (ETHERTYPE_IPV4);
  Arp->HardwareLen  = 6;
  Arp->ProtocolLen  = 4;
  Arp->Operation    = HTONS (ARP_OP_REPLY);
  CopyMem (Arp->SenderMac, SrcMac, 6);
  CopyMem (Arp->SenderIp, SrcIp, 4);
  CopyMem (Arp->TargetMac, DstMac, 6);
  CopyMem (Arp->TargetIp, DstIp, 4);

  return Offset + ARP_HEADER_SIZE;
}

/**
  Build a complete ICMP Echo Request packet (Ethernet + IP + ICMP).

  @param[out] Buffer          Output buffer.
  @param[in]  SrcMac          Source MAC (6 bytes).
  @param[in]  DstMac          Destination MAC (6 bytes).
  @param[in]  SrcIp           Source IP (4 bytes).
  @param[in]  DstIp           Destination IP (4 bytes).
  @param[in]  Identifier      Echo identifier.
  @param[in]  SequenceNumber  Echo sequence number.
  @param[in]  Data            Optional payload data.
  @param[in]  DataLength      Payload data length.

  @return Total frame size.
**/
UINTN
PktBuildIcmpEchoRequest (
  OUT UINT8        *Buffer,
  IN  CONST UINT8  *SrcMac,
  IN  CONST UINT8  *DstMac,
  IN  CONST UINT8  *SrcIp,
  IN  CONST UINT8  *DstIp,
  IN  UINT16       Identifier,
  IN  UINT16       SequenceNumber,
  IN  CONST UINT8  *Data      OPTIONAL,
  IN  UINTN        DataLength
  )
{
  UINTN        Offset;
  ICMP_HEADER  *Icmp;
  UINT16       IcmpLen;

  IcmpLen = (UINT16)(ICMP_HEADER_SIZE + DataLength);

  //
  // Ethernet header
  //
  Offset = PktBuildEthernetHeader (Buffer, DstMac, SrcMac, ETHERTYPE_IPV4);

  //
  // IP header
  //
  Offset += PktBuildIpv4Header (Buffer + Offset, SrcIp, DstIp, IP_PROTO_ICMP, IcmpLen, 64);

  //
  // ICMP header
  //
  Icmp = (ICMP_HEADER *)(Buffer + Offset);
  Icmp->Type           = ICMP_TYPE_ECHO_REQUEST;
  Icmp->Code           = 0;
  Icmp->Checksum       = 0;
  Icmp->Identifier     = HTONS (Identifier);
  Icmp->SequenceNumber = HTONS (SequenceNumber);

  //
  // Copy payload data after ICMP header
  //
  if (Data != NULL && DataLength > 0) {
    CopyMem (Buffer + Offset + ICMP_HEADER_SIZE, Data, DataLength);
  }

  //
  // ICMP checksum covers ICMP header + data
  //
  Icmp->Checksum = HTONS (PktChecksum (Buffer + Offset, IcmpLen));

  return Offset + IcmpLen;
}

/**
  Build a complete UDP packet (Ethernet + IP + UDP + data).

  @param[out] Buffer      Output buffer.
  @param[in]  SrcMac      Source MAC (6 bytes).
  @param[in]  DstMac      Destination MAC (6 bytes).
  @param[in]  SrcIp       Source IP (4 bytes).
  @param[in]  DstIp       Destination IP (4 bytes).
  @param[in]  SrcPort     Source port (host byte order).
  @param[in]  DstPort     Destination port (host byte order).
  @param[in]  Data        Optional payload data.
  @param[in]  DataLength  Payload data length.

  @return Total frame size.
**/
UINTN
PktBuildUdpPacket (
  OUT UINT8        *Buffer,
  IN  CONST UINT8  *SrcMac,
  IN  CONST UINT8  *DstMac,
  IN  CONST UINT8  *SrcIp,
  IN  CONST UINT8  *DstIp,
  IN  UINT16       SrcPort,
  IN  UINT16       DstPort,
  IN  CONST UINT8  *Data      OPTIONAL,
  IN  UINTN        DataLength
  )
{
  UINTN        Offset;
  UINTN        UdpOffset;
  UDP_HEADER   *Udp;
  UINT16       UdpLen;

  UdpLen = (UINT16)(UDP_HEADER_SIZE + DataLength);

  //
  // Ethernet header
  //
  Offset = PktBuildEthernetHeader (Buffer, DstMac, SrcMac, ETHERTYPE_IPV4);

  //
  // IP header
  //
  Offset += PktBuildIpv4Header (Buffer + Offset, SrcIp, DstIp, IP_PROTO_UDP, UdpLen, 64);

  //
  // UDP header
  //
  UdpOffset = Offset;
  Udp = (UDP_HEADER *)(Buffer + Offset);
  Udp->SrcPort  = HTONS (SrcPort);
  Udp->DstPort  = HTONS (DstPort);
  Udp->Length   = HTONS (UdpLen);
  Udp->Checksum = 0;

  //
  // Copy payload data after UDP header
  //
  if (Data != NULL && DataLength > 0) {
    CopyMem (Buffer + Offset + UDP_HEADER_SIZE, Data, DataLength);
  }

  //
  // UDP checksum (with pseudo-header)
  //
  Udp->Checksum = HTONS (
                    PktPseudoChecksum (
                      SrcIp,
                      DstIp,
                      IP_PROTO_UDP,
                      UdpLen,
                      Buffer + UdpOffset,
                      UdpLen
                      )
                    );

  //
  // UDP checksum of 0x0000 means "no checksum"; if computed result is 0, use 0xFFFF
  //
  if (Udp->Checksum == 0) {
    Udp->Checksum = 0xFFFF;
  }

  return Offset + UdpLen;
}

/**
  Build a complete TCP packet (Ethernet + IP + TCP + data).

  @param[out] Buffer      Output buffer.
  @param[in]  SrcMac      Source MAC (6 bytes).
  @param[in]  DstMac      Destination MAC (6 bytes).
  @param[in]  SrcIp       Source IP (4 bytes).
  @param[in]  DstIp       Destination IP (4 bytes).
  @param[in]  SrcPort     Source port (host byte order).
  @param[in]  DstPort     Destination port (host byte order).
  @param[in]  SeqNum      Sequence number.
  @param[in]  AckNum      Acknowledgment number.
  @param[in]  Flags       TCP flags (SYN, ACK, FIN, etc.).
  @param[in]  WindowSize  Window size (host byte order).
  @param[in]  Data        Optional payload data.
  @param[in]  DataLength  Payload data length.

  @return Total frame size.
**/
UINTN
PktBuildTcpPacket (
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
  )
{
  UINTN        Offset;
  UINTN        TcpOffset;
  TCP_HEADER   *Tcp;
  UINT16       TcpLen;

  TcpLen = (UINT16)(TCP_MIN_HEADER_SIZE + DataLength);

  //
  // Ethernet header
  //
  Offset = PktBuildEthernetHeader (Buffer, DstMac, SrcMac, ETHERTYPE_IPV4);

  //
  // IP header
  //
  Offset += PktBuildIpv4Header (Buffer + Offset, SrcIp, DstIp, IP_PROTO_TCP, TcpLen, 64);

  //
  // TCP header (data offset = 5, no options)
  //
  TcpOffset = Offset;
  Tcp = (TCP_HEADER *)(Buffer + Offset);
  Tcp->SrcPort            = HTONS (SrcPort);
  Tcp->DstPort            = HTONS (DstPort);
  Tcp->SeqNumber          = HTONL (SeqNum);
  Tcp->AckNumber          = HTONL (AckNum);
  Tcp->DataOffsetReserved = (5 << 4);   // Data offset = 5 (20 bytes), reserved = 0
  Tcp->Flags              = Flags;
  Tcp->WindowSize         = HTONS (WindowSize);
  Tcp->Checksum           = 0;
  Tcp->UrgentPointer      = 0;

  //
  // Copy payload data after TCP header
  //
  if (Data != NULL && DataLength > 0) {
    CopyMem (Buffer + Offset + TCP_MIN_HEADER_SIZE, Data, DataLength);
  }

  //
  // TCP checksum (with pseudo-header)
  //
  Tcp->Checksum = HTONS (
                    PktPseudoChecksum (
                      SrcIp,
                      DstIp,
                      IP_PROTO_TCP,
                      TcpLen,
                      Buffer + TcpOffset,
                      TcpLen
                      )
                    );

  return Offset + TcpLen;
}
