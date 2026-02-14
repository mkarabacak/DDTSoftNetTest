/** @file
  Network packet parser.
  Parses Ethernet frames into layered structures.
  Validates checksums, provides protocol name helpers.
**/

#include <DDTSoftNetTest.h>
#include <PacketDefs.h>

//
// ============================================================
// Checksum validators
// ============================================================
//

/**
  Validate IPv4 header checksum.

  @param[in] Ip  Pointer to IPv4 header.

  @retval TRUE   Checksum is valid.
  @retval FALSE  Checksum is invalid.
**/
BOOLEAN
PktValidateIpChecksum (
  IN CONST IPV4_HEADER  *Ip
  )
{
  UINTN   HdrLen;
  UINT16  Result;

  HdrLen = IPV4_HDR_LEN (Ip->VersionIhl);
  if (HdrLen < IPV4_MIN_HEADER_SIZE) {
    return FALSE;
  }

  Result = PktChecksum ((CONST UINT8 *)Ip, HdrLen);
  return (Result == 0);
}

/**
  Validate ICMP checksum (covers header + data).

  @param[in] Icmp        Pointer to ICMP header.
  @param[in] IcmpLength  Total ICMP length (header + data).

  @retval TRUE   Checksum is valid.
  @retval FALSE  Checksum is invalid.
**/
BOOLEAN
PktValidateIcmpChecksum (
  IN CONST ICMP_HEADER  *Icmp,
  IN UINTN              IcmpLength
  )
{
  UINT16  Result;

  if (IcmpLength < ICMP_HEADER_SIZE) {
    return FALSE;
  }

  Result = PktChecksum ((CONST UINT8 *)Icmp, IcmpLength);
  return (Result == 0);
}

/**
  Validate TCP checksum using IPv4 pseudo-header.

  @param[in] Ip         Pointer to IPv4 header.
  @param[in] Tcp        Pointer to TCP header.
  @param[in] TcpLength  Total TCP segment length (header + data).

  @retval TRUE   Checksum is valid.
  @retval FALSE  Checksum is invalid.
**/
BOOLEAN
PktValidateTcpChecksum (
  IN CONST IPV4_HEADER  *Ip,
  IN CONST TCP_HEADER   *Tcp,
  IN UINTN              TcpLength
  )
{
  UINT16  Result;

  if (TcpLength < TCP_MIN_HEADER_SIZE) {
    return FALSE;
  }

  Result = PktPseudoChecksum (
             Ip->SrcAddr,
             Ip->DstAddr,
             IP_PROTO_TCP,
             (UINT16)TcpLength,
             (CONST UINT8 *)Tcp,
             TcpLength
             );
  return (Result == 0);
}

/**
  Validate UDP checksum using IPv4 pseudo-header.
  A stored checksum of 0 means "no checksum" and is considered valid.

  @param[in] Ip         Pointer to IPv4 header.
  @param[in] Udp        Pointer to UDP header.
  @param[in] UdpLength  Total UDP datagram length (header + data).

  @retval TRUE   Checksum is valid (or not present).
  @retval FALSE  Checksum is invalid.
**/
BOOLEAN
PktValidateUdpChecksum (
  IN CONST IPV4_HEADER  *Ip,
  IN CONST UDP_HEADER   *Udp,
  IN UINTN              UdpLength
  )
{
  UINT16  Result;

  if (UdpLength < UDP_HEADER_SIZE) {
    return FALSE;
  }

  //
  // UDP checksum is optional; 0 means not computed
  //
  if (Udp->Checksum == 0) {
    return TRUE;
  }

  Result = PktPseudoChecksum (
             Ip->SrcAddr,
             Ip->DstAddr,
             IP_PROTO_UDP,
             (UINT16)UdpLength,
             (CONST UINT8 *)Udp,
             UdpLength
             );
  return (Result == 0);
}

//
// ============================================================
// Full packet parser
// ============================================================
//

/**
  Parse a raw Ethernet frame into a PARSED_PACKET structure.
  Sets pointers into the original buffer (zero-copy).
  Validates checksums for IP and L4 headers.

  @param[in]  Buffer  Raw frame data.
  @param[in]  Length  Frame length in bytes.
  @param[out] Parsed  Parsed packet result.

  @retval EFI_SUCCESS            Packet parsed successfully.
  @retval EFI_INVALID_PARAMETER  NULL buffer or parsed pointer.
  @retval EFI_BUFFER_TOO_SMALL   Frame too short for Ethernet header.
**/
EFI_STATUS
PktParsePacket (
  IN  CONST UINT8    *Buffer,
  IN  UINTN          Length,
  OUT PARSED_PACKET  *Parsed
  )
{
  UINTN   Offset;
  UINTN   IpHdrLen;
  UINTN   IpTotalLen;
  UINTN   L4Length;

  if (Buffer == NULL || Parsed == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  ZeroMem (Parsed, sizeof (PARSED_PACKET));

  //
  // Layer 2: Ethernet
  //
  if (Length < ETHERNET_HEADER_SIZE) {
    return EFI_BUFFER_TOO_SMALL;
  }

  Parsed->HasEthernet = TRUE;
  Parsed->Ethernet    = (ETHERNET_HEADER *)Buffer;
  Parsed->EtherType   = NTOHS (Parsed->Ethernet->EtherType);
  Offset              = ETHERNET_HEADER_SIZE;

  //
  // Layer 3: dispatch on EtherType
  //
  if (Parsed->EtherType == ETHERTYPE_ARP) {
    //
    // ARP
    //
    if (Length < Offset + ARP_HEADER_SIZE) {
      Parsed->Valid = TRUE;
      return EFI_SUCCESS;
    }

    Parsed->HasArp = TRUE;
    Parsed->Arp    = (ARP_HEADER *)(Buffer + Offset);
    Parsed->Valid  = TRUE;
    return EFI_SUCCESS;
  }

  if (Parsed->EtherType != ETHERTYPE_IPV4) {
    //
    // Not IPv4, not ARP — we only set Ethernet fields
    //
    Parsed->Valid = TRUE;
    return EFI_SUCCESS;
  }

  //
  // IPv4
  //
  if (Length < Offset + IPV4_MIN_HEADER_SIZE) {
    Parsed->Valid = TRUE;
    return EFI_SUCCESS;
  }

  Parsed->HasIpv4        = TRUE;
  Parsed->Ipv4           = (IPV4_HEADER *)(Buffer + Offset);
  Parsed->IpChecksumValid = PktValidateIpChecksum (Parsed->Ipv4);

  IpHdrLen   = IPV4_HDR_LEN (Parsed->Ipv4->VersionIhl);
  IpTotalLen = NTOHS (Parsed->Ipv4->TotalLength);

  //
  // Sanity check IP header
  //
  if (IpHdrLen < IPV4_MIN_HEADER_SIZE || Offset + IpTotalLen > Length) {
    Parsed->Valid = TRUE;
    return EFI_SUCCESS;
  }

  L4Length = IpTotalLen - IpHdrLen;
  Offset  += IpHdrLen;

  //
  // Layer 4: dispatch on IP protocol
  //
  switch (Parsed->Ipv4->Protocol) {
    case IP_PROTO_ICMP:
      if (L4Length < ICMP_HEADER_SIZE) {
        break;
      }
      Parsed->HasIcmp         = TRUE;
      Parsed->Icmp            = (ICMP_HEADER *)(Buffer + Offset);
      Parsed->L4ChecksumValid = PktValidateIcmpChecksum (Parsed->Icmp, L4Length);
      Parsed->Payload         = (UINT8 *)(Buffer + Offset + ICMP_HEADER_SIZE);
      Parsed->PayloadLength   = L4Length - ICMP_HEADER_SIZE;
      break;

    case IP_PROTO_TCP:
      if (L4Length < TCP_MIN_HEADER_SIZE) {
        break;
      }
      Parsed->HasTcp          = TRUE;
      Parsed->Tcp             = (TCP_HEADER *)(Buffer + Offset);
      Parsed->L4ChecksumValid = PktValidateTcpChecksum (Parsed->Ipv4, Parsed->Tcp, L4Length);
      {
        UINTN  TcpHdrLen = TCP_HDR_LEN (Parsed->Tcp->DataOffsetReserved);
        if (TcpHdrLen >= TCP_MIN_HEADER_SIZE && TcpHdrLen <= L4Length) {
          Parsed->Payload       = (UINT8 *)(Buffer + Offset + TcpHdrLen);
          Parsed->PayloadLength = L4Length - TcpHdrLen;
        }
      }
      break;

    case IP_PROTO_UDP:
      if (L4Length < UDP_HEADER_SIZE) {
        break;
      }
      Parsed->HasUdp          = TRUE;
      Parsed->Udp             = (UDP_HEADER *)(Buffer + Offset);
      Parsed->L4ChecksumValid = PktValidateUdpChecksum (Parsed->Ipv4, Parsed->Udp, L4Length);
      Parsed->Payload         = (UINT8 *)(Buffer + Offset + UDP_HEADER_SIZE);
      Parsed->PayloadLength   = L4Length - UDP_HEADER_SIZE;
      break;

    default:
      //
      // Unknown L4 protocol — payload starts at L4 offset
      //
      Parsed->Payload       = (UINT8 *)(Buffer + Offset);
      Parsed->PayloadLength = L4Length;
      break;
  }

  Parsed->Valid = TRUE;
  return EFI_SUCCESS;
}

//
// ============================================================
// Protocol name helpers
// ============================================================
//

/**
  Get human-readable name for an EtherType value.

  @param[in] EtherType  EtherType in host byte order.

  @return Pointer to static string.
**/
CONST CHAR16 *
PktGetEtherTypeName (
  IN UINT16  EtherType
  )
{
  switch (EtherType) {
    case ETHERTYPE_IPV4:  return L"IPv4";
    case ETHERTYPE_ARP:   return L"ARP";
    case ETHERTYPE_IPV6:  return L"IPv6";
    default:              return L"Unknown";
  }
}

/**
  Get human-readable name for an IP protocol number.

  @param[in] Protocol  IP protocol number.

  @return Pointer to static string.
**/
CONST CHAR16 *
PktGetIpProtocolName (
  IN UINT8  Protocol
  )
{
  switch (Protocol) {
    case IP_PROTO_ICMP:  return L"ICMP";
    case IP_PROTO_TCP:   return L"TCP";
    case IP_PROTO_UDP:   return L"UDP";
    default:             return L"Unknown";
  }
}

/**
  Get human-readable name for an ICMP type.

  @param[in] Type  ICMP type field.

  @return Pointer to static string.
**/
CONST CHAR16 *
PktGetIcmpTypeName (
  IN UINT8  Type
  )
{
  switch (Type) {
    case ICMP_TYPE_ECHO_REPLY:     return L"Echo Reply";
    case ICMP_TYPE_DEST_UNREACH:   return L"Destination Unreachable";
    case ICMP_TYPE_ECHO_REQUEST:   return L"Echo Request";
    case ICMP_TYPE_TIME_EXCEEDED:  return L"Time Exceeded";
    default:                       return L"Unknown";
  }
}

/**
  Format TCP flags as a readable string (e.g., "SYN ACK").

  @param[in]  Flags       TCP flags byte.
  @param[out] Buffer      Output buffer for flags string.
  @param[in]  BufferSize  Buffer size in CHAR16 units.

  @return Pointer to Buffer.
**/
CONST CHAR16 *
PktGetTcpFlagsStr (
  IN  UINT8   Flags,
  OUT CHAR16  *Buffer,
  IN  UINTN   BufferSize
  )
{
  UINTN  Pos;

  if (Buffer == NULL || BufferSize == 0) {
    return L"";
  }

  Buffer[0] = L'\0';
  Pos = 0;

  if (Flags & TCP_FLAG_SYN) {
    Pos += UnicodeSPrint (Buffer + Pos, (BufferSize - Pos) * sizeof (CHAR16), L"SYN ");
  }
  if (Flags & TCP_FLAG_ACK) {
    Pos += UnicodeSPrint (Buffer + Pos, (BufferSize - Pos) * sizeof (CHAR16), L"ACK ");
  }
  if (Flags & TCP_FLAG_FIN) {
    Pos += UnicodeSPrint (Buffer + Pos, (BufferSize - Pos) * sizeof (CHAR16), L"FIN ");
  }
  if (Flags & TCP_FLAG_RST) {
    Pos += UnicodeSPrint (Buffer + Pos, (BufferSize - Pos) * sizeof (CHAR16), L"RST ");
  }
  if (Flags & TCP_FLAG_PSH) {
    Pos += UnicodeSPrint (Buffer + Pos, (BufferSize - Pos) * sizeof (CHAR16), L"PSH ");
  }
  if (Flags & TCP_FLAG_URG) {
    Pos += UnicodeSPrint (Buffer + Pos, (BufferSize - Pos) * sizeof (CHAR16), L"URG ");
  }

  //
  // Remove trailing space
  //
  if (Pos > 0 && Buffer[Pos - 1] == L' ') {
    Buffer[Pos - 1] = L'\0';
  }

  return Buffer;
}
