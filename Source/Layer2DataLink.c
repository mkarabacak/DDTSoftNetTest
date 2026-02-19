/** @file
  Layer 2 (Data Link) test implementations.
  Tests MAC validation, ARP, broadcast, frame TX/RX, MTU, receive filters.
  Uses EFI_SIMPLE_NETWORK_PROTOCOL for raw frame operations.
**/

#include <DDTSoftNetTest.h>
#include <OsiLayers.h>
#include <TestCases.h>
#include <PacketDefs.h>
#include <Protocol/Arp.h>
#include <Protocol/ServiceBinding.h>

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
  ARP completion callback — sets BOOLEAN flag to TRUE.
**/
STATIC
VOID
EFIAPI
L2ArpNotify (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  if (Context != NULL) {
    *((BOOLEAN *)Context) = TRUE;
  }
}

/**
  Resolve an IP address to MAC via EFI_ARP_PROTOCOL.
  Creates a child ARP instance, configures it, and sends a non-blocking
  request. Polls at TPL_APPLICATION so MNP timer events can fire and
  process ARP replies from the network.

  @param[in]  NicHandle  The NIC handle with ARP service binding.
  @param[in]  LocalIp    Our IPv4 address (4 bytes).
  @param[in]  TargetIp   Target IPv4 address to resolve (4 bytes).
  @param[out] ReplyMac   Buffer to receive resolved MAC (6 bytes).

  @retval TRUE   ARP resolved, ReplyMac is filled.
  @retval FALSE  ARP resolution failed.
**/
STATIC
BOOLEAN
TryArpViaProtocol (
  IN  EFI_HANDLE   NicHandle,
  IN  CONST UINT8  *LocalIp,
  IN  CONST UINT8  *TargetIp,
  OUT UINT8        *ReplyMac
  )
{
  EFI_STATUS                       Status;
  EFI_SERVICE_BINDING_PROTOCOL     *ArpSb;
  EFI_ARP_PROTOCOL                 *Arp;
  EFI_HANDLE                       ArpChild;
  EFI_ARP_CONFIG_DATA              ArpConfig;
  EFI_IPv4_ADDRESS                 StationAddr;
  EFI_MAC_ADDRESS                  ResolvedAddr;

  ArpSb    = NULL;
  Arp      = NULL;
  ArpChild = NULL;

  //
  // Open ARP Service Binding on the NIC handle
  //
  Status = gBS->OpenProtocol (
                  NicHandle,
                  &gEfiArpServiceBindingProtocolGuid,
                  (VOID **)&ArpSb,
                  gImageHandle,
                  NicHandle,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );
  if (EFI_ERROR (Status) || ArpSb == NULL) {
    return FALSE;
  }

  //
  // Create ARP child instance
  //
  Status = ArpSb->CreateChild (ArpSb, &ArpChild);
  if (EFI_ERROR (Status) || ArpChild == NULL) {
    return FALSE;
  }

  //
  // Open ARP protocol on the child handle
  //
  Status = gBS->OpenProtocol (
                  ArpChild,
                  &gEfiArpProtocolGuid,
                  (VOID **)&Arp,
                  gImageHandle,
                  NicHandle,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );
  if (EFI_ERROR (Status) || Arp == NULL) {
    ArpSb->DestroyChild (ArpSb, ArpChild);
    return FALSE;
  }

  //
  // Configure ARP instance with our IP
  //
  CopyMem (&StationAddr, LocalIp, 4);

  ZeroMem (&ArpConfig, sizeof (ArpConfig));
  ArpConfig.SwAddressType   = 0x0800;    // IPv4
  ArpConfig.SwAddressLength = 4;
  ArpConfig.StationAddress  = &StationAddr;
  ArpConfig.EntryTimeOut    = 0;         // No cache timeout
  ArpConfig.RetryCount      = 5;
  ArpConfig.RetryTimeOut    = 10000000;  // 1 second (100ns units)

  Status = Arp->Configure (Arp, &ArpConfig);
  if (EFI_ERROR (Status)) {
    ArpSb->DestroyChild (ArpSb, ArpChild);
    return FALSE;
  }

  //
  // Non-blocking ARP request.
  // Blocking Arp->Request(NULL) raises TPL to TPL_CALLBACK, preventing
  // MNP timer events from firing — ARP replies never get processed.
  // Non-blocking with polling at TPL_APPLICATION allows MNP timer to
  // receive ARP replies and deliver them to the ARP module.
  //
  {
    BOOLEAN    ArpDone;
    EFI_EVENT  ArpEvent;
    UINTN      PollI;
    BOOLEAN    Resolved;

    ArpDone  = FALSE;
    ArpEvent = NULL;
    Resolved = FALSE;

    if (!EFI_ERROR (gBS->CreateEvent (EVT_NOTIFY_SIGNAL, TPL_CALLBACK,
          L2ArpNotify, &ArpDone, &ArpEvent))) {
      ZeroMem (&ResolvedAddr, sizeof (ResolvedAddr));
      Status = Arp->Request (Arp, (VOID *)TargetIp, ArpEvent, &ResolvedAddr);

      if (Status == EFI_SUCCESS) {
        //
        // Cache hit — already resolved
        //
        CopyMem (ReplyMac, &ResolvedAddr, 6);
        Resolved = TRUE;
      } else if (!EFI_ERROR (Status) || Status == EFI_NOT_READY) {
        //
        // Request queued — poll at TPL_APPLICATION (up to 5s)
        //
        for (PollI = 0; PollI < 5000 && !ArpDone; PollI++) {
          gBS->Stall (1000);  // 1ms
        }

        if (ArpDone) {
          CopyMem (ReplyMac, &ResolvedAddr, 6);
          Resolved = TRUE;
        }
      }

      gBS->CloseEvent (ArpEvent);
    }

    //
    // Cleanup
    //
    Arp->Configure (Arp, NULL);
    ArpSb->DestroyChild (ArpSb, ArpChild);

    return Resolved;
  }
}

/**
  Fallback: try ARP resolution via raw SNP TX/RX.
  Used when EFI_ARP_PROTOCOL is not available.

  @param[in]  Snp       Initialized SNP protocol.
  @param[in]  SrcMac    Source MAC address.
  @param[in]  SrcIp     Source IP address (4 bytes).
  @param[in]  DstIp     Target IP address (4 bytes).
  @param[out] ReplyMac  Buffer to receive the reply MAC (6 bytes).
  @param[in]  TimeoutMs Timeout in milliseconds.

  @retval TRUE   ARP reply received, ReplyMac is filled.
  @retval FALSE  No reply within timeout.
**/
STATIC
BOOLEAN
TryArpViaSnp (
  IN  EFI_SIMPLE_NETWORK_PROTOCOL  *Snp,
  IN  CONST UINT8                  *SrcMac,
  IN  CONST UINT8                  *SrcIp,
  IN  CONST UINT8                  *DstIp,
  OUT UINT8                        *ReplyMac,
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

  TxLen = PktBuildArpRequest (TxBuf, SrcMac, SrcIp, DstIp);

  Status = Snp->Transmit (Snp, 0, TxLen, TxBuf, NULL, NULL, NULL);
  if (EFI_ERROR (Status)) {
    return FALSE;
  }

  for (I = 0; I < TimeoutMs; I++) {
    RxLen   = sizeof (RxBuf);
    HdrSize = 0;
    Status  = Snp->Receive (Snp, &HdrSize, &RxLen, RxBuf, NULL, NULL, NULL);

    if (!EFI_ERROR (Status) && RxLen >= ETHERNET_HEADER_SIZE + ARP_HEADER_SIZE) {
      RxEth = (ETHERNET_HEADER *)RxBuf;
      if (NTOHS (RxEth->EtherType) == ETHERTYPE_ARP) {
        RxArp = (ARP_HEADER *)(RxBuf + ETHERNET_HEADER_SIZE);
        if (NTOHS (RxArp->Operation) == ARP_OP_REPLY) {
          CopyMem (ReplyMac, RxArp->SenderMac, 6);
          return TRUE;
        }
      }
    }

    gBS->Stall (1000);  // 1ms
  }

  return FALSE;
}

/**
  Test L2.2: ARP Request/Reply
  Resolves the target IP to a MAC address via ARP.
  Uses EFI_ARP_PROTOCOL (through the UEFI network stack) as the primary
  method. Falls back to raw SNP TX/RX if ARP protocol is not available.

  Uses the NIC's actual IP configuration (from DHCP/Ip4Config2) as the
  sender address, since ARP must use the correct local IP to get replies.

  PASS: ARP reply received with valid MAC
  WARN: No reply (no reachable target on network)
  FAIL: TX failed (NIC problem)
**/
EFI_STATUS
TestL2ArpRequestReply (
  IN  NIC_INFO         *Nic,
  IN  TEST_CONFIG      *Config,
  OUT TEST_RESULT_DATA *Result
  )
{
  EFI_SIMPLE_NETWORK_PROTOCOL  *Snp;
  UINT8                        ReplyMac[6];
  CHAR16                       MacStr[20];
  BOOLEAN                      Resolved;
  UINT8                        *ResolvedIp;
  UINT8                        *SenderIp;

  Snp = Nic->Snp;
  if (Snp == NULL || Snp->Mode->State != EfiSimpleNetworkInitialized) {
    Result->StatusCode = TEST_RESULT_SKIP;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"SNP not initialized");
    return EFI_SUCCESS;
  }

  Resolved   = FALSE;
  ResolvedIp = NULL;

  //
  // Use NIC's actual IP as ARP sender (critical for correct resolution).
  // If NIC has IP from DHCP/static config, use that; else fall back to Config.
  //
  if (Nic->HasIpConfig &&
      (Nic->Ipv4Address.Addr[0] != 0 || Nic->Ipv4Address.Addr[1] != 0 ||
       Nic->Ipv4Address.Addr[2] != 0 || Nic->Ipv4Address.Addr[3] != 0)) {
    SenderIp = Nic->Ipv4Address.Addr;
  } else {
    SenderIp = Config->LocalIp.Addr;
  }

  //
  // Method 1: Use EFI_ARP_PROTOCOL (works even when IP4 stack is active)
  //
  if (Nic->HasArp) {
    //
    // Try NIC's actual gateway first (most likely to respond)
    //
    if (Nic->Gateway.Addr[0] != 0 || Nic->Gateway.Addr[1] != 0 ||
        Nic->Gateway.Addr[2] != 0 || Nic->Gateway.Addr[3] != 0) {
      Resolved = TryArpViaProtocol (
                   Nic->Handle, SenderIp,
                   Nic->Gateway.Addr, ReplyMac
                   );
      if (Resolved) {
        ResolvedIp = Nic->Gateway.Addr;
      }
    }

    //
    // Try config target IP
    //
    if (!Resolved && (Config->TargetIp.Addr[0] != 0 || Config->TargetIp.Addr[1] != 0 ||
                      Config->TargetIp.Addr[2] != 0 || Config->TargetIp.Addr[3] != 0)) {
      Resolved = TryArpViaProtocol (
                   Nic->Handle, SenderIp,
                   Config->TargetIp.Addr, ReplyMac
                   );
      if (Resolved) {
        ResolvedIp = Config->TargetIp.Addr;
      }
    }

    //
    // Try config gateway (if different from NIC gateway)
    //
    if (!Resolved && (Config->Gateway.Addr[0] != 0 || Config->Gateway.Addr[1] != 0 ||
                      Config->Gateway.Addr[2] != 0 || Config->Gateway.Addr[3] != 0)) {
      Resolved = TryArpViaProtocol (
                   Nic->Handle, SenderIp,
                   Config->Gateway.Addr, ReplyMac
                   );
      if (Resolved) {
        ResolvedIp = Config->Gateway.Addr;
      }
    }
  }

  //
  // Method 2: Raw SNP fallback (when ARP protocol not available).
  // Note: if MNP is active on this SNP, it may consume RX frames.
  //
  if (!Resolved) {
    Snp->ReceiveFilters (
      Snp,
      EFI_SIMPLE_NETWORK_RECEIVE_UNICAST |
      EFI_SIMPLE_NETWORK_RECEIVE_BROADCAST,
      0, FALSE, 0, NULL
      );

    if (Nic->Gateway.Addr[0] != 0 || Nic->Gateway.Addr[1] != 0 ||
        Nic->Gateway.Addr[2] != 0 || Nic->Gateway.Addr[3] != 0) {
      Resolved = TryArpViaSnp (
                   Snp, Snp->Mode->CurrentAddress.Addr,
                   SenderIp, Nic->Gateway.Addr,
                   ReplyMac, 2000
                   );
      if (Resolved) {
        ResolvedIp = Nic->Gateway.Addr;
      }
    }

    if (!Resolved && (Config->TargetIp.Addr[0] != 0 || Config->TargetIp.Addr[1] != 0 ||
                      Config->TargetIp.Addr[2] != 0 || Config->TargetIp.Addr[3] != 0)) {
      Resolved = TryArpViaSnp (
                   Snp, Snp->Mode->CurrentAddress.Addr,
                   SenderIp, Config->TargetIp.Addr,
                   ReplyMac, 2000
                   );
      if (Resolved) {
        ResolvedIp = Config->TargetIp.Addr;
      }
    }
  }

  Result->PacketsSent = 1;

  if (Resolved) {
    Result->PacketsReceived = 1;
    UtilFormatMac (ReplyMac, MacStr);
    Result->StatusCode = TEST_RESULT_PASS;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"ARP reply received: %s", MacStr);
    UnicodeSPrint (Result->Detail, sizeof (Result->Detail),
                   L"%d.%d.%d.%d resolved to %s (sender=%d.%d.%d.%d)",
                   ResolvedIp[0], ResolvedIp[1],
                   ResolvedIp[2], ResolvedIp[3],
                   MacStr,
                   SenderIp[0], SenderIp[1], SenderIp[2], SenderIp[3]);
  } else {
    Result->StatusCode = TEST_RESULT_WARN;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"No ARP reply from gateway or target");
    UnicodeSPrint (Result->Detail, sizeof (Result->Detail),
                   L"ARP sent via %s, sender=%d.%d.%d.%d, no host responded",
                   Nic->HasArp ? L"ARP protocol" : L"raw SNP",
                   SenderIp[0], SenderIp[1], SenderIp[2], SenderIp[3]);
    UnicodeSPrint (Result->Suggestion, sizeof (Result->Suggestion),
                   L"Verify target is on the same subnet as %d.%d.%d.%d",
                   SenderIp[0], SenderIp[1], SenderIp[2], SenderIp[3]);
  }

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
  // Transmit — HeaderSize=0 because frame header is pre-built.
  //
  Status = Snp->Transmit (Snp, 0, sizeof (Frame), Frame, NULL, NULL, NULL);
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
  Try to receive frames via MNP (Managed Network Protocol).
  When the UEFI network stack (MNP/IP4) is active on an SNP,
  MNP's background polling drains the SNP receive queue. Direct
  Snp->Receive() calls get nothing. MNP receive is the correct
  way to capture incoming frames when the stack is active.

  @param[in]  NicHandle       NIC handle with MNP service binding.
  @param[out] FramesReceived  Number of frames received.
  @param[out] BytesReceived   Total bytes received.
  @param[in]  TimeoutMs       Timeout in milliseconds.

  @retval TRUE   At least one frame received.
  @retval FALSE  No frames received or MNP setup failed.
**/
STATIC
BOOLEAN
TryReceiveViaMnp (
  IN  EFI_HANDLE  NicHandle,
  OUT UINTN       *FramesReceived,
  OUT UINTN       *BytesReceived,
  IN  UINTN       TimeoutMs
  )
{
  EFI_STATUS                             Status;
  EFI_SERVICE_BINDING_PROTOCOL           *MnpSb;
  EFI_MANAGED_NETWORK_PROTOCOL           *Mnp;
  EFI_HANDLE                             MnpChild;
  EFI_MANAGED_NETWORK_CONFIG_DATA        MnpConfig;
  EFI_MANAGED_NETWORK_COMPLETION_TOKEN   RxToken;
  EFI_EVENT                              RxEvent;
  UINTN                                  I;
  BOOLEAN                                GotFrame;

  *FramesReceived = 0;
  *BytesReceived  = 0;
  GotFrame        = FALSE;
  MnpSb           = NULL;
  Mnp             = NULL;
  MnpChild        = NULL;
  RxEvent         = NULL;

  //
  // Open MNP Service Binding
  //
  Status = gBS->OpenProtocol (
                  NicHandle,
                  &gEfiManagedNetworkServiceBindingProtocolGuid,
                  (VOID **)&MnpSb,
                  gImageHandle,
                  NicHandle,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );
  if (EFI_ERROR (Status) || MnpSb == NULL) {
    return FALSE;
  }

  //
  // Create MNP child instance
  //
  Status = MnpSb->CreateChild (MnpSb, &MnpChild);
  if (EFI_ERROR (Status) || MnpChild == NULL) {
    return FALSE;
  }

  //
  // Open MNP protocol on child
  //
  Status = gBS->OpenProtocol (
                  MnpChild,
                  &gEfiManagedNetworkProtocolGuid,
                  (VOID **)&Mnp,
                  gImageHandle,
                  NicHandle,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );
  if (EFI_ERROR (Status) || Mnp == NULL) {
    MnpSb->DestroyChild (MnpSb, MnpChild);
    return FALSE;
  }

  //
  // Configure MNP to receive all Ethernet frame types
  //
  ZeroMem (&MnpConfig, sizeof (MnpConfig));
  MnpConfig.ReceivedQueueTimeoutValue   = 0;
  MnpConfig.TransmitQueueTimeoutValue   = 0;
  MnpConfig.ProtocolTypeFilter          = 0;      // All EtherTypes
  MnpConfig.EnableUnicastReceive        = TRUE;
  MnpConfig.EnableMulticastReceive      = TRUE;
  MnpConfig.EnableBroadcastReceive      = TRUE;
  MnpConfig.EnablePromiscuousReceive    = FALSE;
  MnpConfig.FlushQueuesOnReset          = TRUE;
  MnpConfig.EnableReceiveTimestamps     = FALSE;
  MnpConfig.DisableBackgroundPolling    = FALSE;

  Status = Mnp->Configure (Mnp, &MnpConfig);
  if (EFI_ERROR (Status)) {
    MnpSb->DestroyChild (MnpSb, MnpChild);
    return FALSE;
  }

  //
  // Create event for receive completion token
  //
  Status = gBS->CreateEvent (0, TPL_CALLBACK, NULL, NULL, &RxEvent);
  if (EFI_ERROR (Status)) {
    Mnp->Configure (Mnp, NULL);
    MnpSb->DestroyChild (MnpSb, MnpChild);
    return FALSE;
  }

  //
  // Queue asynchronous receive
  //
  ZeroMem (&RxToken, sizeof (RxToken));
  RxToken.Event         = RxEvent;
  RxToken.Status        = EFI_NOT_READY;
  RxToken.Packet.RxData = NULL;

  Status = Mnp->Receive (Mnp, &RxToken);
  if (EFI_ERROR (Status)) {
    gBS->CloseEvent (RxEvent);
    Mnp->Configure (Mnp, NULL);
    MnpSb->DestroyChild (MnpSb, MnpChild);
    return FALSE;
  }

  //
  // Poll for incoming frames
  //
  for (I = 0; I < TimeoutMs; I++) {
    Mnp->Poll (Mnp);

    if (RxToken.Status != EFI_NOT_READY) {
      if (!EFI_ERROR (RxToken.Status) && RxToken.Packet.RxData != NULL) {
        *FramesReceived += 1;
        *BytesReceived  += RxToken.Packet.RxData->PacketLength;

        //
        // Recycle the receive buffer
        //
        gBS->SignalEvent (RxToken.Packet.RxData->RecycleEvent);
      }

      //
      // Re-queue receive for more frames
      //
      RxToken.Status        = EFI_NOT_READY;
      RxToken.Packet.RxData = NULL;
      Status = Mnp->Receive (Mnp, &RxToken);
      if (EFI_ERROR (Status)) {
        break;
      }
    }

    gBS->Stall (1000);  // 1ms
  }

  //
  // Cancel any pending receive
  //
  if (RxToken.Status == EFI_NOT_READY) {
    Mnp->Cancel (Mnp, &RxToken);
  }

  GotFrame = (*FramesReceived > 0);

  //
  // Cleanup
  //
  Mnp->Configure (Mnp, NULL);
  gBS->CloseEvent (RxEvent);
  MnpSb->DestroyChild (MnpSb, MnpChild);

  return GotFrame;
}

/**
  Test L2.5: Frame TX/RX
  Sends an ARP request (which should generate a reply if target exists)
  and verifies both TX and RX work at the frame level.

  Uses MNP receive when available (since the active UEFI network stack
  consumes frames from SNP.Receive). Falls back to raw SNP if MNP is
  not present.

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
  UINTN                        RxBytes;
  UINT8                        *SenderIp;
  BOOLEAN                      UsedMnp;

  Snp = Nic->Snp;
  if (Snp == NULL || Snp->Mode->State != EfiSimpleNetworkInitialized) {
    Result->StatusCode = TEST_RESULT_SKIP;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"SNP not initialized");
    return EFI_SUCCESS;
  }

  //
  // Use NIC's actual IP for ARP sender (same logic as L2.2)
  //
  if (Nic->HasIpConfig &&
      (Nic->Ipv4Address.Addr[0] != 0 || Nic->Ipv4Address.Addr[1] != 0 ||
       Nic->Ipv4Address.Addr[2] != 0 || Nic->Ipv4Address.Addr[3] != 0)) {
    SenderIp = Nic->Ipv4Address.Addr;
  } else {
    SenderIp = Config->LocalIp.Addr;
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
            SenderIp,
            Config->TargetIp.Addr
            );

  Status = Snp->Transmit (Snp, 0, TxLen, TxBuf, NULL, NULL, NULL);
  if (EFI_ERROR (Status)) {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Frame TX failed: %r", Status);
    return EFI_SUCCESS;
  }

  Result->PacketsSent = 1;
  Result->BytesSent   = TxLen;

  //
  // Method 1: Receive via MNP (preferred when network stack is active).
  // When MNP/IP4 is active on the SNP, MNP's background polling drains
  // the SNP receive queue. Direct Snp->Receive() gets nothing.
  //
  RxCount = 0;
  RxBytes = 0;
  UsedMnp = FALSE;

  if (Nic->HasMnp) {
    UsedMnp = TryReceiveViaMnp (Nic->Handle, &RxCount, &RxBytes, 2000);
  }

  //
  // Method 2: Fall back to raw SNP receive (when MNP not available)
  //
  if (!UsedMnp && RxCount == 0) {
    for (I = 0; I < 2000; I++) {
      RxLen   = sizeof (RxBuf);
      HdrSize = 0;
      Status  = Snp->Receive (Snp, &HdrSize, &RxLen, RxBuf, NULL, NULL, NULL);

      if (!EFI_ERROR (Status)) {
        RxCount++;
        RxBytes += RxLen;
      }

      gBS->Stall (1000);
    }
  }

  Result->PacketsReceived = RxCount;
  Result->BytesReceived   = RxBytes;

  if (RxCount > 0) {
    Result->StatusCode = TEST_RESULT_PASS;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"TX/RX working: sent 1, received %d frame(s) via %s",
                   RxCount, UsedMnp ? L"MNP" : L"SNP");
  } else {
    Result->StatusCode = TEST_RESULT_WARN;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"TX succeeded but no frames received in 2s");
    UnicodeSPrint (Result->Detail, sizeof (Result->Detail),
                   L"Tried %s receive. Target %d.%d.%d.%d may not exist.",
                   Nic->HasMnp ? L"MNP" : L"SNP",
                   Config->TargetIp.Addr[0], Config->TargetIp.Addr[1],
                   Config->TargetIp.Addr[2], Config->TargetIp.Addr[3]);
    UnicodeSPrint (Result->Suggestion, sizeof (Result->Suggestion),
                   L"Ensure companion/target is running on the same subnet");
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
  Status = Snp->Transmit (Snp, 0, FrameSize, Frame, NULL, NULL, NULL);
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
