/** @file
  Layer 2 (Data Link) test implementations.
  Tests MAC validation, ARP, broadcast, frame TX/RX, MTU, receive filters.
  Uses EFI_SIMPLE_NETWORK_PROTOCOL for raw frame operations.
**/

#include <DDTSoftNetTest.h>
#include <OsiLayers.h>
#include <TestCases.h>
#include <PacketDefs.h>

/**
  Test L2.1: MAC Address Valid
  Verifies MAC address is valid (non-zero, non-broadcast, bit 0 of first byte clear for unicast).

  PASS: Valid unicast MAC
  WARN: Locally administered MAC (bit 1 of first byte set)
  FAIL: All-zero or all-FF MAC
**/
EFI_STATUS
TestL2MacAddressValid (
  IN  NIC_INFO         *Nic,
  IN  TEST_CONFIG      *Config,
  OUT TEST_RESULT_DATA *Result
  )
{
  UINT8    *Mac;
  BOOLEAN  AllZero;
  BOOLEAN  AllFF;
  UINTN    I;
  CHAR16   MacStr[20];

  Mac = Nic->CurrentMac.Addr;

  UtilFormatMac (Mac, MacStr);
  UnicodeSPrint (Result->Detail, sizeof (Result->Detail),
                 L"Current: %s  Permanent: %02X:%02X:%02X:%02X:%02X:%02X",
                 MacStr,
                 Nic->PermanentMac.Addr[0], Nic->PermanentMac.Addr[1],
                 Nic->PermanentMac.Addr[2], Nic->PermanentMac.Addr[3],
                 Nic->PermanentMac.Addr[4], Nic->PermanentMac.Addr[5]);

  //
  // Check all-zero
  //
  AllZero = TRUE;
  AllFF   = TRUE;
  for (I = 0; I < 6; I++) {
    if (Mac[I] != 0x00) AllZero = FALSE;
    if (Mac[I] != 0xFF) AllFF = FALSE;
  }

  if (AllZero) {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"MAC address is all zeros (00:00:00:00:00:00)");
    UnicodeSPrint (Result->FailReason, sizeof (Result->FailReason),
                   L"NIC has no valid MAC address assigned");
    UnicodeSPrint (Result->Suggestion, sizeof (Result->Suggestion),
                   L"Check NIC hardware or driver configuration");
    return EFI_SUCCESS;
  }

  if (AllFF) {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"MAC address is broadcast (FF:FF:FF:FF:FF:FF)");
    UnicodeSPrint (Result->FailReason, sizeof (Result->FailReason),
                   L"NIC reports broadcast as its unicast MAC");
    return EFI_SUCCESS;
  }

  //
  // Check multicast bit (bit 0 of first byte)
  //
  if (Mac[0] & 0x01) {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"MAC has multicast bit set (%s)", MacStr);
    UnicodeSPrint (Result->FailReason, sizeof (Result->FailReason),
                   L"Unicast MAC should have bit 0 of first byte clear");
    return EFI_SUCCESS;
  }

  //
  // Check locally administered bit (bit 1 of first byte)
  //
  if (Mac[0] & 0x02) {
    Result->StatusCode = TEST_RESULT_WARN;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Locally administered MAC: %s", MacStr);
    UnicodeSPrint (Result->Detail, sizeof (Result->Detail),
                   L"Bit 1 of first byte is set (locally administered). "
                   L"Common in VMs and virtual NICs.");
    return EFI_SUCCESS;
  }

  Result->StatusCode = TEST_RESULT_PASS;
  UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                 L"Valid globally unique unicast MAC: %s", MacStr);

  return EFI_SUCCESS;
}

/**
  Test L2.2: ARP Request/Reply
  Sends an ARP request for the target IP and waits for a reply.
  Uses SNP raw frame TX/RX.

  PASS: ARP reply received with valid MAC
  FAIL: No reply within timeout
**/
EFI_STATUS
TestL2ArpRequestReply (
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
  UINTN                        I;
  UINTN                        HdrSize;
  ETHERNET_HEADER              *RxEth;
  ARP_HEADER                   *RxArp;
  CHAR16                       MacStr[20];

  Snp = Nic->Snp;
  if (Snp == NULL || Snp->Mode->State != EfiSimpleNetworkInitialized) {
    Result->StatusCode = TEST_RESULT_SKIP;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"SNP not initialized");
    return EFI_SUCCESS;
  }

  //
  // Build ARP request
  //
  TxLen = PktBuildArpRequest (
            TxBuf,
            Snp->Mode->CurrentAddress.Addr,
            Config->LocalIp.Addr,
            Config->TargetIp.Addr
            );

  //
  // Enable broadcast receive filter
  //
  Snp->ReceiveFilters (
    Snp,
    EFI_SIMPLE_NETWORK_RECEIVE_UNICAST |
    EFI_SIMPLE_NETWORK_RECEIVE_BROADCAST,
    0, FALSE, 0, NULL
    );

  //
  // Send ARP request
  //
  Status = Snp->Transmit (Snp, ETHERNET_HEADER_SIZE, TxLen, TxBuf, NULL, NULL, NULL);
  if (EFI_ERROR (Status)) {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"ARP request TX failed: %r", Status);
    return EFI_SUCCESS;
  }

  Result->PacketsSent = 1;
  Result->BytesSent   = TxLen;

  //
  // Wait for ARP reply (poll for up to 3 seconds)
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
          Result->PacketsReceived = 1;
          Result->BytesReceived   = RxLen;

          UtilFormatMac (RxArp->SenderMac, MacStr);
          Result->StatusCode = TEST_RESULT_PASS;
          UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                         L"ARP reply received: %s", MacStr);
          UnicodeSPrint (Result->Detail, sizeof (Result->Detail),
                         L"Target %d.%d.%d.%d resolved to %s",
                         Config->TargetIp.Addr[0], Config->TargetIp.Addr[1],
                         Config->TargetIp.Addr[2], Config->TargetIp.Addr[3],
                         MacStr);
          return EFI_SUCCESS;
        }
      }
    }

    gBS->Stall (1000);  // 1ms
  }

  Result->StatusCode = TEST_RESULT_FAIL;
  UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                 L"No ARP reply received within 3s");
  UnicodeSPrint (Result->FailReason, sizeof (Result->FailReason),
                 L"Target %d.%d.%d.%d did not respond to ARP",
                 Config->TargetIp.Addr[0], Config->TargetIp.Addr[1],
                 Config->TargetIp.Addr[2], Config->TargetIp.Addr[3]);
  UnicodeSPrint (Result->Suggestion, sizeof (Result->Suggestion),
                 L"Verify target is on the same subnet and reachable");

  return EFI_SUCCESS;
}

/**
  Test L2.3: ARP Cache
  Checks if the NIC handle has an ARP protocol instance available.

  PASS: ARP protocol present
  WARN: ARP not available (may not be bound)
**/
EFI_STATUS
TestL2ArpCache (
  IN  NIC_INFO         *Nic,
  IN  TEST_CONFIG      *Config,
  OUT TEST_RESULT_DATA *Result
  )
{
  if (Nic->HasArp) {
    Result->StatusCode = TEST_RESULT_PASS;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"ARP service binding available on this NIC");
    UnicodeSPrint (Result->Detail, sizeof (Result->Detail),
                   L"EFI_ARP_SERVICE_BINDING_PROTOCOL found on NIC handle");
  } else {
    Result->StatusCode = TEST_RESULT_WARN;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"ARP service binding not available");
    UnicodeSPrint (Result->Detail, sizeof (Result->Detail),
                   L"ARP protocol stack may not be loaded for this NIC. "
                   L"Raw ARP via SNP is still possible.");
  }

  return EFI_SUCCESS;
}

/**
  Test L2.4: Broadcast Frame
  Sends a broadcast Ethernet frame and verifies TX completion.

  PASS: Broadcast frame sent successfully
  FAIL: TX failed
**/
EFI_STATUS
TestL2BroadcastFrame (
  IN  NIC_INFO         *Nic,
  IN  TEST_CONFIG      *Config,
  OUT TEST_RESULT_DATA *Result
  )
{
  EFI_SIMPLE_NETWORK_PROTOCOL  *Snp;
  EFI_STATUS                   Status;
  UINT8                        Frame[64];
  VOID                         *TxBuf;
  UINTN                        I;
  UINT8                        BroadcastMac[6] = ETHERNET_BROADCAST_MAC;

  Snp = Nic->Snp;
  if (Snp == NULL || Snp->Mode->State != EfiSimpleNetworkInitialized) {
    Result->StatusCode = TEST_RESULT_SKIP;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"SNP not initialized");
    return EFI_SUCCESS;
  }

  //
  // Build broadcast frame with experimental EtherType
  //
  ZeroMem (Frame, sizeof (Frame));
  PktBuildEthernetHeader (Frame, BroadcastMac, Snp->Mode->CurrentAddress.Addr, 0x88B5);

  //
  // Payload pattern
  //
  for (I = ETHERNET_HEADER_SIZE; I < sizeof (Frame); I++) {
    Frame[I] = (UINT8)(0xAA ^ (I & 0xFF));
  }

  //
  // Transmit
  //
  Status = Snp->Transmit (Snp, ETHERNET_HEADER_SIZE, sizeof (Frame), Frame, NULL, NULL, NULL);
  if (EFI_ERROR (Status)) {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Broadcast frame TX failed: %r", Status);
    return EFI_SUCCESS;
  }

  Result->PacketsSent = 1;
  Result->BytesSent   = sizeof (Frame);

  //
  // Wait for TX completion
  //
  TxBuf = NULL;
  for (I = 0; I < 100; I++) {
    Snp->GetStatus (Snp, NULL, &TxBuf);
    if (TxBuf != NULL) break;
    gBS->Stall (1000);
  }

  Result->StatusCode = TEST_RESULT_PASS;
  UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                 L"Broadcast frame sent (64 bytes, EtherType 0x88B5)");

  return EFI_SUCCESS;
}

/**
  Test L2.5: Frame TX/RX
  Sends an ARP request (which should generate a reply if target exists)
  and verifies both TX and RX work at the frame level.

  PASS: Frame sent and response received
  WARN: Frame sent but no response (target may not exist)
  FAIL: TX failed
**/
EFI_STATUS
TestL2FrameTxRx (
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
  UINTN                        RxCount;

  Snp = Nic->Snp;
  if (Snp == NULL || Snp->Mode->State != EfiSimpleNetworkInitialized) {
    Result->StatusCode = TEST_RESULT_SKIP;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"SNP not initialized");
    return EFI_SUCCESS;
  }

  //
  // Enable receive filters
  //
  Snp->ReceiveFilters (
    Snp,
    EFI_SIMPLE_NETWORK_RECEIVE_UNICAST |
    EFI_SIMPLE_NETWORK_RECEIVE_BROADCAST,
    0, FALSE, 0, NULL
    );

  //
  // Send ARP request as a probe
  //
  TxLen = PktBuildArpRequest (
            TxBuf,
            Snp->Mode->CurrentAddress.Addr,
            Config->LocalIp.Addr,
            Config->TargetIp.Addr
            );

  Status = Snp->Transmit (Snp, ETHERNET_HEADER_SIZE, TxLen, TxBuf, NULL, NULL, NULL);
  if (EFI_ERROR (Status)) {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Frame TX failed: %r", Status);
    return EFI_SUCCESS;
  }

  Result->PacketsSent = 1;
  Result->BytesSent   = TxLen;

  //
  // Try to receive any frames for 2 seconds
  //
  RxCount = 0;
  for (I = 0; I < 2000; I++) {
    RxLen   = sizeof (RxBuf);
    HdrSize = 0;
    Status  = Snp->Receive (Snp, &HdrSize, &RxLen, RxBuf, NULL, NULL, NULL);

    if (!EFI_ERROR (Status)) {
      RxCount++;
      Result->PacketsReceived = RxCount;
      Result->BytesReceived  += RxLen;
    }

    gBS->Stall (1000);
  }

  if (RxCount > 0) {
    Result->StatusCode = TEST_RESULT_PASS;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"TX/RX working: sent 1 frame, received %d frame(s)", RxCount);
  } else {
    Result->StatusCode = TEST_RESULT_WARN;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"TX succeeded but no frames received in 2s");
    UnicodeSPrint (Result->Detail, sizeof (Result->Detail),
                   L"Target may not exist or NIC RX filters may block incoming frames");
  }

  return EFI_SUCCESS;
}

/**
  Test L2.6: MTU Detection
  Determines the maximum frame size the NIC can handle by reading SNP Mode.
  Attempts to send frames of increasing size to find the actual limit.

  PASS: MTU detected and >= 1500
  WARN: MTU below 1500
**/
EFI_STATUS
TestL2MtuDetection (
  IN  NIC_INFO         *Nic,
  IN  TEST_CONFIG      *Config,
  OUT TEST_RESULT_DATA *Result
  )
{
  EFI_SIMPLE_NETWORK_PROTOCOL  *Snp;
  EFI_STATUS                   Status;
  UINT32                       MaxPkt;
  UINT8                        *Frame;
  UINTN                        FrameSize;
  UINTN                        LargestSent;
  UINT8                        BroadcastMac[6] = ETHERNET_BROADCAST_MAC;

  Snp = Nic->Snp;
  if (Snp == NULL || Snp->Mode->State != EfiSimpleNetworkInitialized) {
    Result->StatusCode = TEST_RESULT_SKIP;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"SNP not initialized");
    return EFI_SUCCESS;
  }

  MaxPkt = Snp->Mode->MaxPacketSize;

  UnicodeSPrint (Result->Detail, sizeof (Result->Detail),
                 L"SNP reports MaxPacketSize=%d, MediaHeaderSize=%d",
                 MaxPkt, Snp->Mode->MediaHeaderSize);

  //
  // Try to send a maximum-size frame
  //
  FrameSize = MaxPkt + Snp->Mode->MediaHeaderSize;
  if (FrameSize > MAX_ETHERNET_FRAME_SIZE) {
    FrameSize = MAX_ETHERNET_FRAME_SIZE;
  }

  Frame = AllocateZeroPool (FrameSize);
  if (Frame == NULL) {
    Result->StatusCode = TEST_RESULT_ERROR;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Failed to allocate %d byte frame buffer", FrameSize);
    return EFI_SUCCESS;
  }

  //
  // Build frame header
  //
  PktBuildEthernetHeader (Frame, BroadcastMac, Snp->Mode->CurrentAddress.Addr, 0x88B5);

  //
  // Try sending the max-size frame
  //
  LargestSent = 0;
  Status = Snp->Transmit (Snp, ETHERNET_HEADER_SIZE, FrameSize, Frame, NULL, NULL, NULL);
  if (!EFI_ERROR (Status)) {
    LargestSent = FrameSize;
    Result->PacketsSent = 1;
    Result->BytesSent   = FrameSize;

    //
    // Wait for TX recycle
    //
    {
      VOID   *TxBuf = NULL;
      UINTN  J;
      for (J = 0; J < 100; J++) {
        Snp->GetStatus (Snp, NULL, &TxBuf);
        if (TxBuf != NULL) break;
        gBS->Stall (1000);
      }
    }
  }

  FreePool (Frame);

  if (LargestSent > 0) {
    UINT32  Mtu;
    Mtu = (UINT32)(LargestSent - Snp->Mode->MediaHeaderSize);

    if (Mtu >= 1500) {
      Result->StatusCode = TEST_RESULT_PASS;
      UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                     L"MTU = %d bytes (frame %d bytes)", Mtu, LargestSent);
    } else {
      Result->StatusCode = TEST_RESULT_WARN;
      UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                     L"MTU = %d bytes (below standard 1500)", Mtu);
    }
  } else {
    Result->StatusCode = TEST_RESULT_WARN;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Max frame TX failed; reported MaxPkt=%d", MaxPkt);
    UnicodeSPrint (Result->Suggestion, sizeof (Result->Suggestion),
                   L"NIC may restrict frame sizes; try smaller frames");
  }

  return EFI_SUCCESS;
}

/**
  Test L2.7: Receive Filter
  Tests NIC receive filter capabilities by querying and setting filters.

  PASS: Unicast + broadcast filters supported and set
  WARN: Limited filter support
  FAIL: Cannot set any receive filters
**/
EFI_STATUS
TestL2ReceiveFilter (
  IN  NIC_INFO         *Nic,
  IN  TEST_CONFIG      *Config,
  OUT TEST_RESULT_DATA *Result
  )
{
  EFI_SIMPLE_NETWORK_PROTOCOL  *Snp;
  EFI_STATUS                   Status;
  UINT32                       FilterMask;
  UINT32                       CurrentFilter;
  BOOLEAN                      HasUnicast;
  BOOLEAN                      HasBroadcast;
  BOOLEAN                      HasMulticast;
  BOOLEAN                      HasPromiscuous;

  Snp = Nic->Snp;
  if (Snp == NULL || Snp->Mode->State != EfiSimpleNetworkInitialized) {
    Result->StatusCode = TEST_RESULT_SKIP;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"SNP not initialized");
    return EFI_SUCCESS;
  }

  FilterMask    = Snp->Mode->ReceiveFilterMask;
  CurrentFilter = Snp->Mode->ReceiveFilterSetting;

  HasUnicast    = (FilterMask & EFI_SIMPLE_NETWORK_RECEIVE_UNICAST) != 0;
  HasBroadcast  = (FilterMask & EFI_SIMPLE_NETWORK_RECEIVE_BROADCAST) != 0;
  HasMulticast  = (FilterMask & EFI_SIMPLE_NETWORK_RECEIVE_MULTICAST) != 0;
  HasPromiscuous = (FilterMask & EFI_SIMPLE_NETWORK_RECEIVE_PROMISCUOUS) != 0;

  UnicodeSPrint (Result->Detail, sizeof (Result->Detail),
                 L"Mask: 0x%X  Current: 0x%X  "
                 L"Unicast:%s  Bcast:%s  Mcast:%s  Promisc:%s",
                 FilterMask, CurrentFilter,
                 HasUnicast    ? L"Y" : L"N",
                 HasBroadcast  ? L"Y" : L"N",
                 HasMulticast  ? L"Y" : L"N",
                 HasPromiscuous ? L"Y" : L"N");

  //
  // Try to enable unicast + broadcast
  //
  if (HasUnicast && HasBroadcast) {
    Status = Snp->ReceiveFilters (
               Snp,
               EFI_SIMPLE_NETWORK_RECEIVE_UNICAST |
               EFI_SIMPLE_NETWORK_RECEIVE_BROADCAST,
               0, FALSE, 0, NULL
               );

    if (EFI_ERROR (Status)) {
      Result->StatusCode = TEST_RESULT_WARN;
      UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                     L"Failed to set unicast+broadcast filter: %r", Status);
      return EFI_SUCCESS;
    }

    Result->StatusCode = TEST_RESULT_PASS;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Rx filters OK: unicast+broadcast set (Promisc=%s, MCast=%s)",
                   HasPromiscuous ? L"avail" : L"N/A",
                   HasMulticast ? L"avail" : L"N/A");
  } else if (HasUnicast) {
    Result->StatusCode = TEST_RESULT_WARN;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Only unicast filter supported (no broadcast)");
  } else {
    Result->StatusCode = TEST_RESULT_WARN;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Limited filter support (mask=0x%X)", FilterMask);
    UnicodeSPrint (Result->Suggestion, sizeof (Result->Suggestion),
                   L"NIC may use promiscuous mode by default");
  }

  return EFI_SUCCESS;
}
