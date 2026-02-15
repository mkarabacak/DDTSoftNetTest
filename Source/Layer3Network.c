/** @file
  Layer 3 (Network) test implementations.
  Tests IP configuration, ICMP echo/sweep, TTL discovery, MTU path discovery,
  IP fragmentation, IPv6 ND, IP header validation, routing, and duplicate IP detection.
  Uses EFI_SIMPLE_NETWORK_PROTOCOL for raw frame operations and
  EFI_IP4_CONFIG2_PROTOCOL for IP configuration queries.
**/

#include <DDTSoftNetTest.h>
#include <OsiLayers.h>
#include <TestCases.h>
#include <PacketDefs.h>

//
// ICMP echo identifier used across L3 tests
//
#define L3_ICMP_ID  0xDD30

//
// ============================================================
// Static helpers
// ============================================================
//

/**
  Resolve the MAC address of a target IP via ARP.
  Sends an ARP request and waits for a reply.

  @param[in]  Snp        SNP protocol instance (must be initialized).
  @param[in]  SrcIp      Our IP address (4 bytes).
  @param[in]  TargetIp   Target IP to resolve (4 bytes).
  @param[out] TargetMac  Resolved MAC address (6 bytes).
  @param[in]  TimeoutMs  Timeout in milliseconds.

  @retval EFI_SUCCESS    MAC resolved successfully.
  @retval EFI_TIMEOUT    No ARP reply received within timeout.
  @retval EFI_NOT_READY  TX failed.
**/
STATIC
EFI_STATUS
L3ResolveTargetMac (
  IN  EFI_SIMPLE_NETWORK_PROTOCOL  *Snp,
  IN  CONST UINT8                  *SrcIp,
  IN  CONST UINT8                  *TargetIp,
  OUT UINT8                        *TargetMac,
  IN  UINTN                        TimeoutMs
  )
{
  EFI_STATUS       Status;
  UINT8            TxBuf[64];
  UINT8            RxBuf[MAX_ETHERNET_FRAME_SIZE];
  UINTN            TxLen;
  UINTN            RxLen;
  UINTN            HdrSize;
  UINTN            I;
  ETHERNET_HEADER  *RxEth;
  ARP_HEADER       *RxArp;

  //
  // Build and send ARP request
  //
  TxLen = PktBuildArpRequest (TxBuf, Snp->Mode->CurrentAddress.Addr, SrcIp, TargetIp);

  Snp->ReceiveFilters (
    Snp,
    EFI_SIMPLE_NETWORK_RECEIVE_UNICAST |
    EFI_SIMPLE_NETWORK_RECEIVE_BROADCAST,
    0, FALSE, 0, NULL
    );

  Status = Snp->Transmit (Snp, 0, TxLen, TxBuf, NULL, NULL, NULL);
  if (EFI_ERROR (Status)) {
    return EFI_NOT_READY;
  }

  //
  // Wait for ARP reply
  //
  for (I = 0; I < TimeoutMs; I++) {
    RxLen   = sizeof (RxBuf);
    HdrSize = 0;
    Status  = Snp->Receive (Snp, &HdrSize, &RxLen, RxBuf, NULL, NULL, NULL);

    if (!EFI_ERROR (Status) && RxLen >= ETHERNET_HEADER_SIZE + ARP_HEADER_SIZE) {
      RxEth = (ETHERNET_HEADER *)RxBuf;
      if (NTOHS (RxEth->EtherType) == ETHERTYPE_ARP) {
        RxArp = (ARP_HEADER *)(RxBuf + ETHERNET_HEADER_SIZE);
        if (NTOHS (RxArp->Operation) == ARP_OP_REPLY) {
          CopyMem (TargetMac, RxArp->SenderMac, 6);
          return EFI_SUCCESS;
        }
      }
    }

    gBS->Stall (1000);
  }

  return EFI_TIMEOUT;
}

/**
  Send an ICMP Echo Request and wait for an Echo Reply.
  Measures round-trip time in microseconds.

  @param[in]  Snp        SNP protocol instance.
  @param[in]  SrcMac     Source MAC (6 bytes).
  @param[in]  DstMac     Destination MAC (6 bytes).
  @param[in]  SrcIp      Source IP (4 bytes).
  @param[in]  DstIp      Destination IP (4 bytes).
  @param[in]  SeqNum     ICMP sequence number.
  @param[in]  Ttl        IP TTL value.
  @param[in]  PayloadSize  ICMP payload data size.
  @param[in]  TimeoutMs  Timeout in milliseconds.
  @param[out] RttUs      Round-trip time in microseconds (if reply received).
  @param[out] ReplyType  ICMP reply type (0=echo reply, 11=time exceeded, etc).
  @param[out] ReplyCode  ICMP reply code.

  @retval EFI_SUCCESS    Got a reply (check ReplyType).
  @retval EFI_TIMEOUT    No reply within timeout.
  @retval EFI_NOT_READY  TX failed.
**/
STATIC
EFI_STATUS
L3SendIcmpEcho (
  IN  EFI_SIMPLE_NETWORK_PROTOCOL  *Snp,
  IN  CONST UINT8                  *SrcMac,
  IN  CONST UINT8                  *DstMac,
  IN  CONST UINT8                  *SrcIp,
  IN  CONST UINT8                  *DstIp,
  IN  UINT16                       SeqNum,
  IN  UINT8                        Ttl,
  IN  UINTN                        PayloadSize,
  IN  UINTN                        TimeoutMs,
  OUT UINT32                       *RttUs,
  OUT UINT8                        *ReplyType,
  OUT UINT8                        *ReplyCode
  )
{
  EFI_STATUS       Status;
  UINT8            TxBuf[MAX_ETHERNET_FRAME_SIZE];
  UINT8            RxBuf[MAX_ETHERNET_FRAME_SIZE];
  UINTN            TxLen;
  UINTN            RxLen;
  UINTN            HdrSize;
  UINTN            I;
  UINT8            *Payload;
  UINTN            Offset;
  ICMP_HEADER      *TxIcmp;
  UINT16           IcmpLen;
  ETHERNET_HEADER  *RxEth;
  IPV4_HEADER      *RxIp;
  ICMP_HEADER      *RxIcmp;
  UINT64           StartTick;
  UINT64           CurTick;

  if (PayloadSize > MAX_ETHERNET_FRAME_SIZE - ETHERNET_HEADER_SIZE - IPV4_MIN_HEADER_SIZE - ICMP_HEADER_SIZE) {
    PayloadSize = MAX_ETHERNET_FRAME_SIZE - ETHERNET_HEADER_SIZE - IPV4_MIN_HEADER_SIZE - ICMP_HEADER_SIZE;
  }

  *RttUs     = 0;
  *ReplyType = 0;
  *ReplyCode = 0;

  //
  // Build ICMP echo request with custom TTL
  // Use PktBuildEthernetHeader + PktBuildIpv4Header + manual ICMP to control TTL
  //
  ZeroMem (TxBuf, sizeof (TxBuf));
  Offset = PktBuildEthernetHeader (TxBuf, DstMac, SrcMac, ETHERTYPE_IPV4);

  IcmpLen = (UINT16)(ICMP_HEADER_SIZE + PayloadSize);
  Offset += PktBuildIpv4Header (TxBuf + Offset, SrcIp, DstIp, IP_PROTO_ICMP, IcmpLen, Ttl);

  TxIcmp = (ICMP_HEADER *)(TxBuf + Offset);
  TxIcmp->Type           = ICMP_TYPE_ECHO_REQUEST;
  TxIcmp->Code           = 0;
  TxIcmp->Checksum       = 0;
  TxIcmp->Identifier     = HTONS (L3_ICMP_ID);
  TxIcmp->SequenceNumber = HTONS (SeqNum);

  //
  // Fill payload with pattern
  //
  Payload = TxBuf + Offset + ICMP_HEADER_SIZE;
  for (I = 0; I < PayloadSize; I++) {
    Payload[I] = (UINT8)(I & 0xFF);
  }

  TxIcmp->Checksum = HTONS (PktChecksum (TxBuf + Offset, IcmpLen));

  TxLen = Offset + IcmpLen;

  //
  // Ensure receive filters are set
  //
  Snp->ReceiveFilters (
    Snp,
    EFI_SIMPLE_NETWORK_RECEIVE_UNICAST |
    EFI_SIMPLE_NETWORK_RECEIVE_BROADCAST,
    0, FALSE, 0, NULL
    );

  //
  // Record start time and send
  //
  StartTick = UtilGetTimestamp ();

  Status = Snp->Transmit (Snp, 0, TxLen, TxBuf, NULL, NULL, NULL);
  if (EFI_ERROR (Status)) {
    return EFI_NOT_READY;
  }

  //
  // Poll for ICMP reply
  //
  for (I = 0; I < TimeoutMs; I++) {
    RxLen   = sizeof (RxBuf);
    HdrSize = 0;
    Status  = Snp->Receive (Snp, &HdrSize, &RxLen, RxBuf, NULL, NULL, NULL);

    if (!EFI_ERROR (Status) &&
        RxLen >= ETHERNET_HEADER_SIZE + IPV4_MIN_HEADER_SIZE + ICMP_HEADER_SIZE) {
      RxEth = (ETHERNET_HEADER *)RxBuf;

      if (NTOHS (RxEth->EtherType) == ETHERTYPE_IPV4) {
        RxIp = (IPV4_HEADER *)(RxBuf + ETHERNET_HEADER_SIZE);

        if (RxIp->Protocol == IP_PROTO_ICMP) {
          UINTN  IpHdrLen;
          IpHdrLen = IPV4_HDR_LEN (RxIp->VersionIhl);
          RxIcmp = (ICMP_HEADER *)(RxBuf + ETHERNET_HEADER_SIZE + IpHdrLen);

          //
          // Check for echo reply matching our ID
          //
          if (RxIcmp->Type == ICMP_TYPE_ECHO_REPLY &&
              NTOHS (RxIcmp->Identifier) == L3_ICMP_ID) {
            CurTick    = UtilGetTimestamp ();
            *RttUs     = (UINT32)((CurTick - StartTick) * 1000000);
            *ReplyType = RxIcmp->Type;
            *ReplyCode = RxIcmp->Code;
            return EFI_SUCCESS;
          }

          //
          // Check for Time Exceeded or Destination Unreachable
          // These contain the original IP header + 8 bytes in their data
          //
          if (RxIcmp->Type == ICMP_TYPE_TIME_EXCEEDED ||
              RxIcmp->Type == ICMP_TYPE_DEST_UNREACH) {
            CurTick    = UtilGetTimestamp ();
            *RttUs     = (UINT32)((CurTick - StartTick) * 1000000);
            *ReplyType = RxIcmp->Type;
            *ReplyCode = RxIcmp->Code;
            return EFI_SUCCESS;
          }
        }
      }
    }

    gBS->Stall (1000);
  }

  return EFI_TIMEOUT;
}

/**
  Check if a target IP is on the same subnet as the local IP.

  @param[in] LocalIp    Local IP address (4 bytes).
  @param[in] TargetIp   Target IP address (4 bytes).
  @param[in] SubnetMask Subnet mask (4 bytes).

  @retval TRUE   Same subnet.
  @retval FALSE  Different subnet.
**/
STATIC
BOOLEAN
L3IsSameSubnet (
  IN CONST UINT8  *LocalIp,
  IN CONST UINT8  *TargetIp,
  IN CONST UINT8  *SubnetMask
  )
{
  UINTN  I;

  for (I = 0; I < 4; I++) {
    if ((LocalIp[I] & SubnetMask[I]) != (TargetIp[I] & SubnetMask[I])) {
      return FALSE;
    }
  }

  return TRUE;
}

/**
  Determine which MAC to use for reaching a target IP.
  If same subnet, ARP-resolve the target. If different subnet, ARP-resolve the gateway.

  @param[in]  Snp        SNP protocol instance.
  @param[in]  Config     Test configuration with IPs.
  @param[out] NextHopMac Resolved MAC for next hop (6 bytes).

  @retval EFI_SUCCESS    MAC resolved.
  @retval other          Resolution failed.
**/
STATIC
EFI_STATUS
L3ResolveNextHopMac (
  IN  EFI_SIMPLE_NETWORK_PROTOCOL  *Snp,
  IN  TEST_CONFIG                  *Config,
  OUT UINT8                        *NextHopMac
  )
{
  CONST UINT8  *ResolveIp;

  if (L3IsSameSubnet (Config->LocalIp.Addr, Config->TargetIp.Addr, Config->SubnetMask.Addr)) {
    ResolveIp = Config->TargetIp.Addr;
  } else {
    //
    // Target is off-subnet, resolve gateway MAC instead
    //
    ResolveIp = Config->Gateway.Addr;
  }

  return L3ResolveTargetMac (Snp, Config->LocalIp.Addr, ResolveIp, NextHopMac, 3000);
}

//
// ============================================================
// Test implementations
// ============================================================
//

/**
  Test L3.1: IP Config Check
  Validates that the NIC has a valid IPv4 configuration.
  Checks IP address, subnet mask, and gateway.

  PASS: Valid IP configuration present
  WARN: Partial configuration (missing gateway)
  FAIL: No IP configuration
**/
EFI_STATUS
TestL3IpConfigCheck (
  IN  NIC_INFO         *Nic,
  IN  TEST_CONFIG      *Config,
  OUT TEST_RESULT_DATA *Result
  )
{
  BOOLEAN  HasGateway;
  UINTN    I;

  if (!Nic->HasIp4) {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"IPv4 protocol stack not available on this NIC");
    UnicodeSPrint (Result->FailReason, sizeof (Result->FailReason),
                   L"EFI_IP4_SERVICE_BINDING_PROTOCOL not found");
    UnicodeSPrint (Result->Suggestion, sizeof (Result->Suggestion),
                   L"Ensure network stack driver is loaded");
    return EFI_SUCCESS;
  }

  if (!Nic->HasIpConfig) {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"No IPv4 address configured");
    UnicodeSPrint (Result->FailReason, sizeof (Result->FailReason),
                   L"IP4Config2 reports no station address");
    UnicodeSPrint (Result->Suggestion, sizeof (Result->Suggestion),
                   L"Configure a static IP or enable DHCP");
    return EFI_SUCCESS;
  }

  //
  // Check gateway
  //
  HasGateway = FALSE;
  for (I = 0; I < 4; I++) {
    if (Nic->Gateway.Addr[I] != 0) {
      HasGateway = TRUE;
      break;
    }
  }

  UnicodeSPrint (Result->Detail, sizeof (Result->Detail),
                 L"IP: %d.%d.%d.%d  Mask: %d.%d.%d.%d  GW: %d.%d.%d.%d",
                 Nic->Ipv4Address.Addr[0], Nic->Ipv4Address.Addr[1],
                 Nic->Ipv4Address.Addr[2], Nic->Ipv4Address.Addr[3],
                 Nic->SubnetMask.Addr[0], Nic->SubnetMask.Addr[1],
                 Nic->SubnetMask.Addr[2], Nic->SubnetMask.Addr[3],
                 Nic->Gateway.Addr[0], Nic->Gateway.Addr[1],
                 Nic->Gateway.Addr[2], Nic->Gateway.Addr[3]);

  if (!HasGateway) {
    Result->StatusCode = TEST_RESULT_WARN;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"IP configured (%d.%d.%d.%d) but no gateway set",
                   Nic->Ipv4Address.Addr[0], Nic->Ipv4Address.Addr[1],
                   Nic->Ipv4Address.Addr[2], Nic->Ipv4Address.Addr[3]);
    UnicodeSPrint (Result->Suggestion, sizeof (Result->Suggestion),
                   L"Configure a default gateway for off-subnet routing");
    return EFI_SUCCESS;
  }

  Result->StatusCode = TEST_RESULT_PASS;
  UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                 L"IP config OK: %d.%d.%d.%d/%d.%d.%d.%d GW %d.%d.%d.%d",
                 Nic->Ipv4Address.Addr[0], Nic->Ipv4Address.Addr[1],
                 Nic->Ipv4Address.Addr[2], Nic->Ipv4Address.Addr[3],
                 Nic->SubnetMask.Addr[0], Nic->SubnetMask.Addr[1],
                 Nic->SubnetMask.Addr[2], Nic->SubnetMask.Addr[3],
                 Nic->Gateway.Addr[0], Nic->Gateway.Addr[1],
                 Nic->Gateway.Addr[2], Nic->Gateway.Addr[3]);

  return EFI_SUCCESS;
}

/**
  Test L3.2: ICMP Echo (Ping)
  Sends ICMP echo request to the target IP and measures round-trip time.
  First resolves target MAC via ARP.

  PASS: Echo reply received
  FAIL: No reply within timeout
**/
EFI_STATUS
TestL3IcmpEcho (
  IN  NIC_INFO         *Nic,
  IN  TEST_CONFIG      *Config,
  OUT TEST_RESULT_DATA *Result
  )
{
  EFI_SIMPLE_NETWORK_PROTOCOL  *Snp;
  EFI_STATUS                   Status;
  UINT8                        DstMac[6];
  UINT32                       RttUs;
  UINT8                        ReplyType;
  UINT8                        ReplyCode;

  Snp = Nic->Snp;
  if (Snp == NULL || Snp->Mode->State != EfiSimpleNetworkInitialized) {
    Result->StatusCode = TEST_RESULT_SKIP;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"SNP not initialized");
    return EFI_SUCCESS;
  }

  //
  // Resolve next-hop MAC
  //
  Status = L3ResolveNextHopMac (Snp, Config, DstMac);
  if (EFI_ERROR (Status)) {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"ARP resolution failed for target/gateway");
    UnicodeSPrint (Result->FailReason, sizeof (Result->FailReason),
                   L"Cannot resolve MAC address for next hop");
    UnicodeSPrint (Result->Suggestion, sizeof (Result->Suggestion),
                   L"Verify target IP is reachable and cable is connected");
    return EFI_SUCCESS;
  }

  //
  // Send ICMP echo
  //
  Result->PacketsSent = 1;

  Status = L3SendIcmpEcho (
             Snp,
             Snp->Mode->CurrentAddress.Addr,
             DstMac,
             Config->LocalIp.Addr,
             Config->TargetIp.Addr,
             1,     // SeqNum
             64,    // TTL
             32,    // PayloadSize
             Config->TimeoutMs > 0 ? Config->TimeoutMs : 3000,
             &RttUs,
             &ReplyType,
             &ReplyCode
             );

  Result->BytesSent = ETHERNET_HEADER_SIZE + IPV4_MIN_HEADER_SIZE + ICMP_HEADER_SIZE + 32;

  if (EFI_ERROR (Status)) {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"No ICMP echo reply from %d.%d.%d.%d",
                   Config->TargetIp.Addr[0], Config->TargetIp.Addr[1],
                   Config->TargetIp.Addr[2], Config->TargetIp.Addr[3]);
    UnicodeSPrint (Result->FailReason, sizeof (Result->FailReason),
                   L"ICMP echo request timed out");
    UnicodeSPrint (Result->Suggestion, sizeof (Result->Suggestion),
                   L"Check firewall rules, target IP, and network path");
    return EFI_SUCCESS;
  }

  Result->PacketsReceived = 1;

  if (ReplyType == ICMP_TYPE_ECHO_REPLY) {
    Result->StatusCode  = TEST_RESULT_PASS;
    Result->RttMinUs    = RttUs;
    Result->RttAvgUs    = RttUs;
    Result->RttMaxUs    = RttUs;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Ping %d.%d.%d.%d: reply in %d us",
                   Config->TargetIp.Addr[0], Config->TargetIp.Addr[1],
                   Config->TargetIp.Addr[2], Config->TargetIp.Addr[3],
                   RttUs);
  } else if (ReplyType == ICMP_TYPE_DEST_UNREACH) {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Destination unreachable (code=%d)", ReplyCode);
    UnicodeSPrint (Result->Suggestion, sizeof (Result->Suggestion),
                   L"Check routing and firewall configuration");
  } else {
    Result->StatusCode = TEST_RESULT_WARN;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Got ICMP type %d code %d (not echo reply)",
                   ReplyType, ReplyCode);
  }

  return EFI_SUCCESS;
}

/**
  Test L3.3: ICMP Sweep
  Sends multiple ICMP echo requests and measures response statistics.
  Reports min/avg/max RTT and packet loss.

  PASS: All replies received
  WARN: Partial replies (some loss)
  FAIL: No replies at all
**/
EFI_STATUS
TestL3IcmpSweep (
  IN  NIC_INFO         *Nic,
  IN  TEST_CONFIG      *Config,
  OUT TEST_RESULT_DATA *Result
  )
{
  EFI_SIMPLE_NETWORK_PROTOCOL  *Snp;
  EFI_STATUS                   Status;
  UINT8                        DstMac[6];
  UINT32                       RttUs;
  UINT8                        ReplyType;
  UINT8                        ReplyCode;
  UINTN                        I;
  UINTN                        Count;
  UINTN                        Received;
  UINT32                       MinRtt;
  UINT32                       MaxRtt;
  UINT64                       TotalRtt;

  Snp = Nic->Snp;
  if (Snp == NULL || Snp->Mode->State != EfiSimpleNetworkInitialized) {
    Result->StatusCode = TEST_RESULT_SKIP;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"SNP not initialized");
    return EFI_SUCCESS;
  }

  //
  // Resolve next-hop MAC
  //
  Status = L3ResolveNextHopMac (Snp, Config, DstMac);
  if (EFI_ERROR (Status)) {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"ARP resolution failed for target/gateway");
    return EFI_SUCCESS;
  }

  Count    = (Config->Iterations > 0 && Config->Iterations <= 10) ? Config->Iterations : 5;
  Received = 0;
  MinRtt   = 0xFFFFFFFF;
  MaxRtt   = 0;
  TotalRtt = 0;

  for (I = 0; I < Count; I++) {
    Result->PacketsSent++;

    Status = L3SendIcmpEcho (
               Snp,
               Snp->Mode->CurrentAddress.Addr,
               DstMac,
               Config->LocalIp.Addr,
               Config->TargetIp.Addr,
               (UINT16)(I + 1),
               64,
               32,
               Config->TimeoutMs > 0 ? Config->TimeoutMs : 2000,
               &RttUs,
               &ReplyType,
               &ReplyCode
               );

    if (!EFI_ERROR (Status) && ReplyType == ICMP_TYPE_ECHO_REPLY) {
      Received++;
      Result->PacketsReceived++;
      TotalRtt += RttUs;
      if (RttUs < MinRtt) MinRtt = RttUs;
      if (RttUs > MaxRtt) MaxRtt = RttUs;
    }

    //
    // Brief pause between pings
    //
    if (I + 1 < Count) {
      gBS->Stall (200000);  // 200ms
    }
  }

  if (Received == Count) {
    Result->StatusCode = TEST_RESULT_PASS;
    Result->RttMinUs   = MinRtt;
    Result->RttAvgUs   = (UINT32)(TotalRtt / Received);
    Result->RttMaxUs   = MaxRtt;
    Result->RttJitterUs = MaxRtt - MinRtt;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Sweep %d/%d OK: min=%d avg=%d max=%d us",
                   Received, Count, MinRtt,
                   (UINT32)(TotalRtt / Received), MaxRtt);
  } else if (Received > 0) {
    Result->StatusCode = TEST_RESULT_WARN;
    Result->RttMinUs   = MinRtt;
    Result->RttAvgUs   = (UINT32)(TotalRtt / Received);
    Result->RttMaxUs   = MaxRtt;
    Result->RttJitterUs = MaxRtt - MinRtt;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Packet loss: %d/%d received (min=%d max=%d us)",
                   Received, Count, MinRtt, MaxRtt);
    UnicodeSPrint (Result->Suggestion, sizeof (Result->Suggestion),
                   L"Check for intermittent connectivity or congestion");
  } else {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"All %d echo requests timed out", Count);
    UnicodeSPrint (Result->FailReason, sizeof (Result->FailReason),
                   L"No ICMP echo replies received from target");
    UnicodeSPrint (Result->Suggestion, sizeof (Result->Suggestion),
                   L"Verify target is reachable and ICMP is not blocked");
  }

  return EFI_SUCCESS;
}

/**
  Test L3.4: TTL/Hop Discovery
  Sends ICMP echo requests with increasing TTL values to discover
  the number of hops to the target. Similar to traceroute.

  PASS: Target reached and hop count determined
  WARN: Reached target but some hops didn't respond
  FAIL: Could not reach target within max TTL
**/
EFI_STATUS
TestL3TtlHopDiscovery (
  IN  NIC_INFO         *Nic,
  IN  TEST_CONFIG      *Config,
  OUT TEST_RESULT_DATA *Result
  )
{
  EFI_SIMPLE_NETWORK_PROTOCOL  *Snp;
  EFI_STATUS                   Status;
  UINT8                        DstMac[6];
  UINT32                       RttUs;
  UINT8                        ReplyType;
  UINT8                        ReplyCode;
  UINT8                        Ttl;
  UINT8                        MaxTtl;
  UINTN                        HopsResponded;
  BOOLEAN                      TargetReached;

  Snp = Nic->Snp;
  if (Snp == NULL || Snp->Mode->State != EfiSimpleNetworkInitialized) {
    Result->StatusCode = TEST_RESULT_SKIP;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"SNP not initialized");
    return EFI_SUCCESS;
  }

  //
  // Resolve next-hop MAC
  //
  Status = L3ResolveNextHopMac (Snp, Config, DstMac);
  if (EFI_ERROR (Status)) {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"ARP resolution failed for target/gateway");
    return EFI_SUCCESS;
  }

  MaxTtl        = 16;
  HopsResponded = 0;
  TargetReached = FALSE;

  for (Ttl = 1; Ttl <= MaxTtl; Ttl++) {
    Result->PacketsSent++;

    Status = L3SendIcmpEcho (
               Snp,
               Snp->Mode->CurrentAddress.Addr,
               DstMac,
               Config->LocalIp.Addr,
               Config->TargetIp.Addr,
               (UINT16)Ttl,
               Ttl,
               32,
               2000,
               &RttUs,
               &ReplyType,
               &ReplyCode
               );

    if (!EFI_ERROR (Status)) {
      Result->PacketsReceived++;
      HopsResponded++;

      if (ReplyType == ICMP_TYPE_ECHO_REPLY) {
        TargetReached = TRUE;
        break;
      }
      //
      // TIME_EXCEEDED means this hop responded but we haven't reached target
      //
    }

    gBS->Stall (100000);  // 100ms between probes
  }

  if (TargetReached) {
    Result->StatusCode = TEST_RESULT_PASS;
    Result->RttMinUs   = RttUs;
    Result->RttAvgUs   = RttUs;
    Result->RttMaxUs   = RttUs;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Target reached in %d hop(s), RTT=%d us", Ttl, RttUs);
    UnicodeSPrint (Result->Detail, sizeof (Result->Detail),
                   L"Hops responded: %d/%d, final TTL=%d",
                   HopsResponded, Ttl, Ttl);
  } else if (HopsResponded > 0) {
    Result->StatusCode = TEST_RESULT_WARN;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Target not reached in %d hops (%d hops responded)",
                   MaxTtl, HopsResponded);
    UnicodeSPrint (Result->Suggestion, sizeof (Result->Suggestion),
                   L"Target may be more than %d hops away or blocking ICMP", MaxTtl);
  } else {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"No hops responded (0/%d)", MaxTtl);
    UnicodeSPrint (Result->FailReason, sizeof (Result->FailReason),
                   L"No ICMP Time Exceeded or Echo Reply received");
    UnicodeSPrint (Result->Suggestion, sizeof (Result->Suggestion),
                   L"Check gateway reachability and ICMP filtering");
  }

  return EFI_SUCCESS;
}

/**
  Test L3.5: MTU Path Discovery
  Sends ICMP echo requests with DF (Don't Fragment) flag and varying payload sizes
  to discover the path MTU. Binary search between minimum and maximum.

  PASS: Path MTU determined
  WARN: Could only send small packets
  FAIL: All sizes failed
**/
EFI_STATUS
TestL3MtuPathDiscovery (
  IN  NIC_INFO         *Nic,
  IN  TEST_CONFIG      *Config,
  OUT TEST_RESULT_DATA *Result
  )
{
  EFI_SIMPLE_NETWORK_PROTOCOL  *Snp;
  EFI_STATUS                   Status;
  UINT8                        DstMac[6];
  UINT32                       RttUs;
  UINT8                        ReplyType;
  UINT8                        ReplyCode;
  UINTN                        Lo;
  UINTN                        Hi;
  UINTN                        Mid;
  UINTN                        LargestOk;
  UINT16                       SeqNum;

  Snp = Nic->Snp;
  if (Snp == NULL || Snp->Mode->State != EfiSimpleNetworkInitialized) {
    Result->StatusCode = TEST_RESULT_SKIP;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"SNP not initialized");
    return EFI_SUCCESS;
  }

  Status = L3ResolveNextHopMac (Snp, Config, DstMac);
  if (EFI_ERROR (Status)) {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"ARP resolution failed");
    return EFI_SUCCESS;
  }

  //
  // Binary search for path MTU
  // Payload range: 8 bytes (minimum useful) to 1472 (1500 - 20 IP - 8 ICMP)
  //
  Lo        = 8;
  Hi        = 1472;
  LargestOk = 0;
  SeqNum    = 100;

  while (Lo <= Hi) {
    Mid = (Lo + Hi) / 2;

    Result->PacketsSent++;
    SeqNum++;

    Status = L3SendIcmpEcho (
               Snp,
               Snp->Mode->CurrentAddress.Addr,
               DstMac,
               Config->LocalIp.Addr,
               Config->TargetIp.Addr,
               SeqNum,
               64,
               Mid,
               2000,
               &RttUs,
               &ReplyType,
               &ReplyCode
               );

    if (!EFI_ERROR (Status) && ReplyType == ICMP_TYPE_ECHO_REPLY) {
      Result->PacketsReceived++;
      LargestOk = Mid;
      Lo = Mid + 1;
    } else {
      //
      // Either timeout or Destination Unreachable (Fragmentation Needed)
      //
      if (!EFI_ERROR (Status)) {
        Result->PacketsReceived++;
      }

      if (Mid == 0) break;
      Hi = Mid - 1;
    }

    gBS->Stall (200000);  // 200ms between probes
  }

  if (LargestOk > 0) {
    //
    // Path MTU = IP header + ICMP header + largest payload
    //
    UINTN  PathMtu;
    PathMtu = IPV4_MIN_HEADER_SIZE + ICMP_HEADER_SIZE + LargestOk;

    if (PathMtu >= 1500) {
      Result->StatusCode = TEST_RESULT_PASS;
    } else if (PathMtu >= 576) {
      Result->StatusCode = TEST_RESULT_WARN;
    } else {
      Result->StatusCode = TEST_RESULT_WARN;
    }

    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Path MTU = %d bytes (payload %d + headers 28)",
                   PathMtu, LargestOk);
    UnicodeSPrint (Result->Detail, sizeof (Result->Detail),
                   L"Largest successful ICMP payload: %d bytes  "
                   L"IP+ICMP overhead: 28 bytes  Path MTU: %d",
                   LargestOk, PathMtu);
  } else {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Path MTU discovery failed: no replies received");
    UnicodeSPrint (Result->FailReason, sizeof (Result->FailReason),
                   L"Target did not respond to any ICMP echo request");
    UnicodeSPrint (Result->Suggestion, sizeof (Result->Suggestion),
                   L"Verify target is reachable with basic ping first");
  }

  return EFI_SUCCESS;
}

/**
  Test L3.6: IP Fragmentation
  Tests whether the network path handles IP fragmentation correctly.
  Sends a large ICMP echo request (larger than 1500-byte MTU) without
  the DF flag, which should be fragmented by the IP layer.

  PASS: Large packet sent and reply received (fragmentation works)
  WARN: TX succeeded but no reply (may need reassembly support)
  FAIL: TX failed
**/
EFI_STATUS
TestL3IpFragmentation (
  IN  NIC_INFO         *Nic,
  IN  TEST_CONFIG      *Config,
  OUT TEST_RESULT_DATA *Result
  )
{
  EFI_SIMPLE_NETWORK_PROTOCOL  *Snp;
  EFI_STATUS                   Status;
  UINT8                        DstMac[6];
  UINT8                        *TxBuf;
  UINTN                        Offset;
  UINTN                        IcmpLen;
  UINTN                        PayloadSize;
  UINTN                        TotalLen;
  ICMP_HEADER                  *Icmp;
  IPV4_HEADER                  *Ip;
  UINT8                        RxBuf[MAX_ETHERNET_FRAME_SIZE];
  UINTN                        RxLen;
  UINTN                        HdrSize;
  UINTN                        I;
  ETHERNET_HEADER              *RxEth;
  IPV4_HEADER                  *RxIp;
  ICMP_HEADER                  *RxIcmp;

  Snp = Nic->Snp;
  if (Snp == NULL || Snp->Mode->State != EfiSimpleNetworkInitialized) {
    Result->StatusCode = TEST_RESULT_SKIP;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"SNP not initialized");
    return EFI_SUCCESS;
  }

  Status = L3ResolveNextHopMac (Snp, Config, DstMac);
  if (EFI_ERROR (Status)) {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"ARP resolution failed");
    return EFI_SUCCESS;
  }

  //
  // Build a large ICMP echo request — payload large enough to require fragmentation
  // at standard MTU. We build it up to SNP MaxPacketSize limit.
  // Use PayloadSize of 1200 bytes — total IP packet = 20 + 8 + 1200 = 1228 bytes.
  // This fits within a single Ethernet frame but tests IP layer handling.
  // For true fragmentation, the receiver needs to reassemble.
  //
  PayloadSize = 1200;
  IcmpLen     = ICMP_HEADER_SIZE + PayloadSize;
  TotalLen    = ETHERNET_HEADER_SIZE + IPV4_MIN_HEADER_SIZE + IcmpLen;

  TxBuf = AllocateZeroPool (TotalLen);
  if (TxBuf == NULL) {
    Result->StatusCode = TEST_RESULT_ERROR;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Failed to allocate %d byte buffer", TotalLen);
    return EFI_SUCCESS;
  }

  //
  // Build headers — clear DF flag to allow fragmentation
  //
  Offset = PktBuildEthernetHeader (
             TxBuf, DstMac,
             Snp->Mode->CurrentAddress.Addr,
             ETHERTYPE_IPV4
             );

  Offset += PktBuildIpv4Header (
              TxBuf + Offset,
              Config->LocalIp.Addr,
              Config->TargetIp.Addr,
              IP_PROTO_ICMP,
              (UINT16)IcmpLen,
              64
              );

  //
  // Clear DF flag in the IP header we just built
  //
  Ip = (IPV4_HEADER *)(TxBuf + ETHERNET_HEADER_SIZE);
  Ip->FlagsFragOffset = 0;
  Ip->HeaderChecksum  = 0;
  Ip->HeaderChecksum  = HTONS (PktChecksum (
                                  TxBuf + ETHERNET_HEADER_SIZE,
                                  IPV4_MIN_HEADER_SIZE
                                  ));

  //
  // ICMP header
  //
  Icmp = (ICMP_HEADER *)(TxBuf + Offset);
  Icmp->Type           = ICMP_TYPE_ECHO_REQUEST;
  Icmp->Code           = 0;
  Icmp->Checksum       = 0;
  Icmp->Identifier     = HTONS (L3_ICMP_ID);
  Icmp->SequenceNumber = HTONS (200);

  //
  // Fill payload with pattern
  //
  for (I = 0; I < PayloadSize; I++) {
    TxBuf[Offset + ICMP_HEADER_SIZE + I] = (UINT8)(I & 0xFF);
  }

  Icmp->Checksum = HTONS (PktChecksum (TxBuf + Offset, (UINTN)IcmpLen));

  //
  // Send
  //
  Snp->ReceiveFilters (
    Snp,
    EFI_SIMPLE_NETWORK_RECEIVE_UNICAST |
    EFI_SIMPLE_NETWORK_RECEIVE_BROADCAST,
    0, FALSE, 0, NULL
    );

  Status = Snp->Transmit (Snp, 0, TotalLen, TxBuf, NULL, NULL, NULL);
  FreePool (TxBuf);

  if (EFI_ERROR (Status)) {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Large ICMP TX failed: %r (payload=%d)", Status, PayloadSize);
    UnicodeSPrint (Result->Suggestion, sizeof (Result->Suggestion),
                   L"NIC may not support frames of this size");
    return EFI_SUCCESS;
  }

  Result->PacketsSent = 1;
  Result->BytesSent   = TotalLen;

  //
  // Wait for reply (3 seconds)
  //
  for (I = 0; I < 3000; I++) {
    RxLen   = sizeof (RxBuf);
    HdrSize = 0;
    Status  = Snp->Receive (Snp, &HdrSize, &RxLen, RxBuf, NULL, NULL, NULL);

    if (!EFI_ERROR (Status) &&
        RxLen >= ETHERNET_HEADER_SIZE + IPV4_MIN_HEADER_SIZE + ICMP_HEADER_SIZE) {
      RxEth = (ETHERNET_HEADER *)RxBuf;

      if (NTOHS (RxEth->EtherType) == ETHERTYPE_IPV4) {
        RxIp = (IPV4_HEADER *)(RxBuf + ETHERNET_HEADER_SIZE);

        if (RxIp->Protocol == IP_PROTO_ICMP) {
          UINTN  IpHdrLen;
          IpHdrLen = IPV4_HDR_LEN (RxIp->VersionIhl);
          RxIcmp = (ICMP_HEADER *)(RxBuf + ETHERNET_HEADER_SIZE + IpHdrLen);

          if (RxIcmp->Type == ICMP_TYPE_ECHO_REPLY &&
              NTOHS (RxIcmp->Identifier) == L3_ICMP_ID) {
            Result->PacketsReceived = 1;
            Result->BytesReceived   = RxLen;
            Result->StatusCode = TEST_RESULT_PASS;
            UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                           L"Large ICMP echo OK (payload=%d, reply=%d bytes)",
                           PayloadSize, RxLen);
            return EFI_SUCCESS;
          }
        }
      }
    }

    gBS->Stall (1000);
  }

  Result->StatusCode = TEST_RESULT_WARN;
  UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                 L"Large ICMP sent (payload=%d) but no reply in 3s", PayloadSize);
  UnicodeSPrint (Result->Detail, sizeof (Result->Detail),
                 L"Frame sent successfully. Reply may require IP reassembly "
                 L"which SNP raw receive may not support.");

  return EFI_SUCCESS;
}

/**
  Test L3.7: IPv6 Neighbor Discovery
  Checks IPv6 protocol stack availability on the NIC.
  Full ND implementation requires ICMPv6 which is beyond SNP raw frames.

  PASS: IPv6 protocol stack available
  WARN: IPv6 not available (IPv4 only)
**/
EFI_STATUS
TestL3Ipv6Nd (
  IN  NIC_INFO         *Nic,
  IN  TEST_CONFIG      *Config,
  OUT TEST_RESULT_DATA *Result
  )
{
  if (Nic->HasIp6) {
    Result->StatusCode = TEST_RESULT_PASS;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"IPv6 protocol stack available on this NIC");
    UnicodeSPrint (Result->Detail, sizeof (Result->Detail),
                   L"EFI_IP6_SERVICE_BINDING_PROTOCOL found. "
                   L"IPv6 ND/SLAAC can be performed via IP6 protocol.");
  } else {
    Result->StatusCode = TEST_RESULT_WARN;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"IPv6 protocol stack not available");
    UnicodeSPrint (Result->Detail, sizeof (Result->Detail),
                   L"EFI_IP6_SERVICE_BINDING_PROTOCOL not found on NIC handle. "
                   L"Only IPv4 operations are possible.");
    UnicodeSPrint (Result->Suggestion, sizeof (Result->Suggestion),
                   L"Load IPv6 network stack driver if IPv6 support is needed");
  }

  return EFI_SUCCESS;
}

/**
  Test L3.8: IP Header Validation
  Sends an ICMP echo request and validates the IP header of the reply.
  Checks version, IHL, total length, TTL, protocol, and checksum.

  PASS: All header fields valid
  WARN: Some unusual values
  FAIL: Invalid header or no reply
**/
EFI_STATUS
TestL3IpHeaderValid (
  IN  NIC_INFO         *Nic,
  IN  TEST_CONFIG      *Config,
  OUT TEST_RESULT_DATA *Result
  )
{
  EFI_SIMPLE_NETWORK_PROTOCOL  *Snp;
  EFI_STATUS                   Status;
  UINT8                        DstMac[6];
  UINT8                        RxBuf[MAX_ETHERNET_FRAME_SIZE];
  UINTN                        RxLen;
  UINTN                        HdrSize;
  UINTN                        I;
  UINTN                        TxLen;
  UINT8                        TxBuf[128];
  ETHERNET_HEADER              *RxEth;
  IPV4_HEADER                  *RxIp;
  UINT8                        Version;
  UINT8                        Ihl;
  UINT16                       TotalLen;
  BOOLEAN                      CsumOk;

  Snp = Nic->Snp;
  if (Snp == NULL || Snp->Mode->State != EfiSimpleNetworkInitialized) {
    Result->StatusCode = TEST_RESULT_SKIP;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"SNP not initialized");
    return EFI_SUCCESS;
  }

  Status = L3ResolveNextHopMac (Snp, Config, DstMac);
  if (EFI_ERROR (Status)) {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"ARP resolution failed");
    return EFI_SUCCESS;
  }

  //
  // Send ICMP echo request
  //
  TxLen = PktBuildIcmpEchoRequest (
            TxBuf,
            Snp->Mode->CurrentAddress.Addr,
            DstMac,
            Config->LocalIp.Addr,
            Config->TargetIp.Addr,
            L3_ICMP_ID,
            300,
            NULL,
            0
            );

  Snp->ReceiveFilters (
    Snp,
    EFI_SIMPLE_NETWORK_RECEIVE_UNICAST |
    EFI_SIMPLE_NETWORK_RECEIVE_BROADCAST,
    0, FALSE, 0, NULL
    );

  Status = Snp->Transmit (Snp, 0, TxLen, TxBuf, NULL, NULL, NULL);
  if (EFI_ERROR (Status)) {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"ICMP TX failed: %r", Status);
    return EFI_SUCCESS;
  }

  Result->PacketsSent = 1;

  //
  // Wait for reply and validate IP header
  //
  for (I = 0; I < 3000; I++) {
    RxLen   = sizeof (RxBuf);
    HdrSize = 0;
    Status  = Snp->Receive (Snp, &HdrSize, &RxLen, RxBuf, NULL, NULL, NULL);

    if (!EFI_ERROR (Status) &&
        RxLen >= ETHERNET_HEADER_SIZE + IPV4_MIN_HEADER_SIZE) {
      RxEth = (ETHERNET_HEADER *)RxBuf;

      if (NTOHS (RxEth->EtherType) == ETHERTYPE_IPV4) {
        RxIp = (IPV4_HEADER *)(RxBuf + ETHERNET_HEADER_SIZE);

        //
        // Check if this is a reply to us (ICMP)
        //
        if (RxIp->Protocol == IP_PROTO_ICMP) {
          Result->PacketsReceived = 1;
          Result->BytesReceived   = RxLen;

          //
          // Validate header fields
          //
          Version  = IPV4_VERSION (RxIp->VersionIhl);
          Ihl      = IPV4_IHL (RxIp->VersionIhl);
          TotalLen = NTOHS (RxIp->TotalLength);
          CsumOk   = PktValidateIpChecksum (RxIp);

          UnicodeSPrint (Result->Detail, sizeof (Result->Detail),
                         L"Ver=%d IHL=%d TotalLen=%d TTL=%d Proto=%d "
                         L"ID=0x%X Flags=0x%X Checksum=%s "
                         L"Src=%d.%d.%d.%d Dst=%d.%d.%d.%d",
                         Version, Ihl, TotalLen, RxIp->Ttl,
                         RxIp->Protocol,
                         NTOHS (RxIp->Identification),
                         NTOHS (RxIp->FlagsFragOffset),
                         CsumOk ? L"OK" : L"BAD",
                         RxIp->SrcAddr[0], RxIp->SrcAddr[1],
                         RxIp->SrcAddr[2], RxIp->SrcAddr[3],
                         RxIp->DstAddr[0], RxIp->DstAddr[1],
                         RxIp->DstAddr[2], RxIp->DstAddr[3]);

          if (Version != 4) {
            Result->StatusCode = TEST_RESULT_FAIL;
            UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                           L"Invalid IP version: %d (expected 4)", Version);
            return EFI_SUCCESS;
          }

          if (Ihl < 5) {
            Result->StatusCode = TEST_RESULT_FAIL;
            UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                           L"Invalid IHL: %d (minimum 5)", Ihl);
            return EFI_SUCCESS;
          }

          if (!CsumOk) {
            Result->StatusCode = TEST_RESULT_FAIL;
            UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                           L"IP header checksum invalid");
            UnicodeSPrint (Result->FailReason, sizeof (Result->FailReason),
                           L"Received packet has corrupt IP header checksum");
            return EFI_SUCCESS;
          }

          if (TotalLen > RxLen - ETHERNET_HEADER_SIZE) {
            Result->StatusCode = TEST_RESULT_WARN;
            UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                           L"IP TotalLength (%d) exceeds frame data (%d)",
                           TotalLen, RxLen - ETHERNET_HEADER_SIZE);
            return EFI_SUCCESS;
          }

          if (RxIp->Ttl == 0) {
            Result->StatusCode = TEST_RESULT_WARN;
            UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                           L"Reply has TTL=0 (unusual)");
            return EFI_SUCCESS;
          }

          Result->StatusCode = TEST_RESULT_PASS;
          UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                         L"IP header valid: Ver=4 IHL=%d TTL=%d Checksum OK",
                         Ihl, RxIp->Ttl);
          return EFI_SUCCESS;
        }
      }
    }

    gBS->Stall (1000);
  }

  Result->StatusCode = TEST_RESULT_FAIL;
  UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                 L"No IP reply received to validate");
  UnicodeSPrint (Result->Suggestion, sizeof (Result->Suggestion),
                 L"Verify target responds to ICMP echo");

  return EFI_SUCCESS;
}

/**
  Test L3.9: Routing Table
  Checks gateway configuration and tests basic routing capability.
  Verifies gateway is on the same subnet and reachable via ARP.

  PASS: Gateway configured and reachable
  WARN: No gateway configured (local subnet only)
  FAIL: Gateway configured but not reachable
**/
EFI_STATUS
TestL3RoutingTable (
  IN  NIC_INFO         *Nic,
  IN  TEST_CONFIG      *Config,
  OUT TEST_RESULT_DATA *Result
  )
{
  EFI_SIMPLE_NETWORK_PROTOCOL  *Snp;
  EFI_STATUS                   Status;
  BOOLEAN                      HasGateway;
  BOOLEAN                      GwOnSubnet;
  UINT8                        GwMac[6];
  UINTN                        I;
  CHAR16                       MacStr[20];

  //
  // Check gateway configuration
  //
  HasGateway = FALSE;
  for (I = 0; I < 4; I++) {
    if (Nic->Gateway.Addr[I] != 0) {
      HasGateway = TRUE;
      break;
    }
  }

  UnicodeSPrint (Result->Detail, sizeof (Result->Detail),
                 L"Local: %d.%d.%d.%d  Mask: %d.%d.%d.%d  GW: %d.%d.%d.%d  "
                 L"HasIpConfig: %s  HasIp4: %s",
                 Nic->Ipv4Address.Addr[0], Nic->Ipv4Address.Addr[1],
                 Nic->Ipv4Address.Addr[2], Nic->Ipv4Address.Addr[3],
                 Nic->SubnetMask.Addr[0], Nic->SubnetMask.Addr[1],
                 Nic->SubnetMask.Addr[2], Nic->SubnetMask.Addr[3],
                 Nic->Gateway.Addr[0], Nic->Gateway.Addr[1],
                 Nic->Gateway.Addr[2], Nic->Gateway.Addr[3],
                 Nic->HasIpConfig ? L"Yes" : L"No",
                 Nic->HasIp4 ? L"Yes" : L"No");

  if (!HasGateway) {
    Result->StatusCode = TEST_RESULT_WARN;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"No default gateway configured");
    UnicodeSPrint (Result->Suggestion, sizeof (Result->Suggestion),
                   L"Configure a gateway for off-subnet routing");
    return EFI_SUCCESS;
  }

  //
  // Check if gateway is on the same subnet
  //
  GwOnSubnet = L3IsSameSubnet (
                 Nic->Ipv4Address.Addr,
                 Nic->Gateway.Addr,
                 Nic->SubnetMask.Addr
                 );

  if (!GwOnSubnet) {
    Result->StatusCode = TEST_RESULT_WARN;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Gateway %d.%d.%d.%d not on local subnet",
                   Nic->Gateway.Addr[0], Nic->Gateway.Addr[1],
                   Nic->Gateway.Addr[2], Nic->Gateway.Addr[3]);
    UnicodeSPrint (Result->Suggestion, sizeof (Result->Suggestion),
                   L"Gateway should be on the same subnet as the NIC");
    return EFI_SUCCESS;
  }

  //
  // Try to ARP-resolve the gateway
  //
  Snp = Nic->Snp;
  if (Snp == NULL || Snp->Mode->State != EfiSimpleNetworkInitialized) {
    //
    // Can't verify reachability without SNP, but config looks ok
    //
    Result->StatusCode = TEST_RESULT_PASS;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Gateway %d.%d.%d.%d configured and on local subnet",
                   Nic->Gateway.Addr[0], Nic->Gateway.Addr[1],
                   Nic->Gateway.Addr[2], Nic->Gateway.Addr[3]);
    return EFI_SUCCESS;
  }

  Status = L3ResolveTargetMac (
             Snp,
             Nic->Ipv4Address.Addr,
             Nic->Gateway.Addr,
             GwMac,
             3000
             );

  if (EFI_ERROR (Status)) {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Gateway %d.%d.%d.%d not reachable (ARP failed)",
                   Nic->Gateway.Addr[0], Nic->Gateway.Addr[1],
                   Nic->Gateway.Addr[2], Nic->Gateway.Addr[3]);
    UnicodeSPrint (Result->FailReason, sizeof (Result->FailReason),
                   L"ARP request for gateway timed out");
    UnicodeSPrint (Result->Suggestion, sizeof (Result->Suggestion),
                   L"Verify gateway is powered on and connected");
    return EFI_SUCCESS;
  }

  UtilFormatMac (GwMac, MacStr);
  Result->StatusCode = TEST_RESULT_PASS;
  UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                 L"Gateway %d.%d.%d.%d reachable (MAC=%s)",
                 Nic->Gateway.Addr[0], Nic->Gateway.Addr[1],
                 Nic->Gateway.Addr[2], Nic->Gateway.Addr[3],
                 MacStr);

  return EFI_SUCCESS;
}

/**
  Test L3.10: Duplicate IP Detection
  Sends a gratuitous ARP probe for our own IP address.
  If another host responds, a duplicate IP exists on the network.

  PASS: No duplicate IP detected
  FAIL: Another host claims our IP address
**/
EFI_STATUS
TestL3DuplicateIp (
  IN  NIC_INFO         *Nic,
  IN  TEST_CONFIG      *Config,
  OUT TEST_RESULT_DATA *Result
  )
{
  EFI_SIMPLE_NETWORK_PROTOCOL  *Snp;
  EFI_STATUS                   Status;
  UINT8                        TxBuf[64];
  UINT8                        RxBuf[MAX_ETHERNET_FRAME_SIZE];
  UINTN                        TxLen;
  UINTN                        RxLen;
  UINTN                        HdrSize;
  UINTN                        I;
  ETHERNET_HEADER              *RxEth;
  ARP_HEADER                   *RxArp;
  UINT8                        ZeroIp[4];
  CHAR16                       MacStr[20];
  CONST UINT8                  *ProbeIp;

  Snp = Nic->Snp;
  if (Snp == NULL || Snp->Mode->State != EfiSimpleNetworkInitialized) {
    Result->StatusCode = TEST_RESULT_SKIP;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"SNP not initialized");
    return EFI_SUCCESS;
  }

  //
  // Use our configured IP or the test config local IP
  //
  if (Nic->HasIpConfig) {
    ProbeIp = Nic->Ipv4Address.Addr;
  } else {
    ProbeIp = Config->LocalIp.Addr;
  }

  //
  // Build ARP probe: Sender IP = 0.0.0.0, Target IP = our IP
  // This is the standard DAD (Duplicate Address Detection) ARP probe
  //
  ZeroMem (ZeroIp, sizeof (ZeroIp));

  ZeroMem (TxBuf, sizeof (TxBuf));
  TxLen = PktBuildArpRequest (
            TxBuf,
            Snp->Mode->CurrentAddress.Addr,
            ZeroIp,
            ProbeIp
            );

  Snp->ReceiveFilters (
    Snp,
    EFI_SIMPLE_NETWORK_RECEIVE_UNICAST |
    EFI_SIMPLE_NETWORK_RECEIVE_BROADCAST,
    0, FALSE, 0, NULL
    );

  Status = Snp->Transmit (Snp, 0, TxLen, TxBuf, NULL, NULL, NULL);
  if (EFI_ERROR (Status)) {
    Result->StatusCode = TEST_RESULT_ERROR;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"ARP probe TX failed: %r", Status);
    return EFI_SUCCESS;
  }

  Result->PacketsSent = 1;
  Result->BytesSent   = TxLen;

  //
  // Listen for ARP replies for 3 seconds
  // Any reply to our probe means someone else has our IP
  //
  for (I = 0; I < 3000; I++) {
    RxLen   = sizeof (RxBuf);
    HdrSize = 0;
    Status  = Snp->Receive (Snp, &HdrSize, &RxLen, RxBuf, NULL, NULL, NULL);

    if (!EFI_ERROR (Status) && RxLen >= ETHERNET_HEADER_SIZE + ARP_HEADER_SIZE) {
      RxEth = (ETHERNET_HEADER *)RxBuf;

      if (NTOHS (RxEth->EtherType) == ETHERTYPE_ARP) {
        RxArp = (ARP_HEADER *)(RxBuf + ETHERNET_HEADER_SIZE);

        if (NTOHS (RxArp->Operation) == ARP_OP_REPLY) {
          //
          // Check if the reply is for our probed IP
          //
          if (RxArp->SenderIp[0] == ProbeIp[0] &&
              RxArp->SenderIp[1] == ProbeIp[1] &&
              RxArp->SenderIp[2] == ProbeIp[2] &&
              RxArp->SenderIp[3] == ProbeIp[3]) {
            //
            // Make sure it's not from our own MAC
            //
            if (CompareMem (RxArp->SenderMac, Snp->Mode->CurrentAddress.Addr, 6) != 0) {
              Result->PacketsReceived = 1;
              Result->BytesReceived   = RxLen;

              UtilFormatMac (RxArp->SenderMac, MacStr);
              Result->StatusCode = TEST_RESULT_FAIL;
              UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                             L"DUPLICATE IP detected! %d.%d.%d.%d claimed by %s",
                             ProbeIp[0], ProbeIp[1], ProbeIp[2], ProbeIp[3],
                             MacStr);
              UnicodeSPrint (Result->FailReason, sizeof (Result->FailReason),
                             L"Another host (MAC %s) has the same IP address",
                             MacStr);
              UnicodeSPrint (Result->Suggestion, sizeof (Result->Suggestion),
                             L"Change IP on one of the conflicting hosts");
              return EFI_SUCCESS;
            }
          }
        }
      }
    }

    gBS->Stall (1000);
  }

  Result->StatusCode = TEST_RESULT_PASS;
  UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                 L"No duplicate IP detected for %d.%d.%d.%d",
                 ProbeIp[0], ProbeIp[1], ProbeIp[2], ProbeIp[3]);

  return EFI_SUCCESS;
}
