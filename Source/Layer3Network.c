/** @file
  Layer 3 (Network) test implementations.
  Tests IP configuration, ICMP echo/sweep, TTL discovery, MTU path discovery,
  IP fragmentation, IPv6 ND, IP header validation, routing, and duplicate IP detection.

  Uses EFI_ARP_PROTOCOL for MAC resolution and EFI_IP4_PROTOCOL for ICMP
  when the IP4 stack is active (the MNP layer consumes frames from
  SNP.Receive, making raw SNP receive unusable). Falls back to raw SNP
  when protocol stack is unavailable.
**/

#include <DDTSoftNetTest.h>
#include <OsiLayers.h>
#include <TestCases.h>
#include <PacketDefs.h>
#include <Protocol/Arp.h>
#include <Protocol/ServiceBinding.h>
#include <Protocol/Ip4.h>

//
// ICMP echo identifier used across L3 tests
//
#define L3_ICMP_ID  0xDD30

//
// ============================================================
// IP4 async event callback (empty - we poll Token.Status)
// ============================================================
//

STATIC
VOID
EFIAPI
L3Ip4DummyNotify (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  //
  // Nothing needed - Token.Status is checked for completion
  //
}

/**
  ARP completion callback — sets BOOLEAN flag to TRUE.
**/
STATIC
VOID
EFIAPI
L3ArpNotify (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  if (Context != NULL) {
    *((BOOLEAN *)Context) = TRUE;
  }
}

//
// ============================================================
// ARP resolution helpers
// ============================================================
//

/**
  Resolve target MAC via EFI_ARP_PROTOCOL.
  Creates a child ARP instance, configures it with our IP,
  and sends a blocking ARP request. Works correctly even when
  the IP4 stack is active (which consumes raw ARP frames from SNP.Receive).

  @param[in]  NicHandle  NIC handle with ARP service binding.
  @param[in]  LocalIp    Our IPv4 address (4 bytes).
  @param[in]  TargetIp   Target IPv4 to resolve (4 bytes).
  @param[out] TargetMac  Resolved MAC address (6 bytes).

  @retval EFI_SUCCESS      MAC resolved.
  @retval EFI_UNSUPPORTED  ARP protocol not available.
  @retval EFI_TIMEOUT      No ARP reply.
**/
STATIC
EFI_STATUS
L3ArpResolveViaProtocol (
  IN  EFI_HANDLE   NicHandle,
  IN  CONST UINT8  *LocalIp,
  IN  CONST UINT8  *TargetIp,
  OUT UINT8        *TargetMac
  )
{
  EFI_STATUS                    Status;
  EFI_SERVICE_BINDING_PROTOCOL  *ArpSb;
  EFI_ARP_PROTOCOL              *Arp;
  EFI_HANDLE                    ArpChild;
  EFI_ARP_CONFIG_DATA           ArpConfig;
  EFI_IPv4_ADDRESS              StationAddr;
  EFI_MAC_ADDRESS               ResolvedAddr;

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
    return EFI_UNSUPPORTED;
  }

  //
  // Create ARP child instance
  //
  Status = ArpSb->CreateChild (ArpSb, &ArpChild);
  if (EFI_ERROR (Status) || ArpChild == NULL) {
    return EFI_UNSUPPORTED;
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
    return EFI_UNSUPPORTED;
  }

  //
  // Configure ARP instance with our IP
  //
  CopyMem (&StationAddr, LocalIp, 4);

  ZeroMem (&ArpConfig, sizeof (ArpConfig));
  ArpConfig.SwAddressType   = 0x0800;      // IPv4
  ArpConfig.SwAddressLength = 4;
  ArpConfig.StationAddress  = &StationAddr;
  ArpConfig.EntryTimeOut    = 0;           // No cache timeout
  ArpConfig.RetryCount      = 10;
  ArpConfig.RetryTimeOut    = 10000000;    // 1 second (100ns units)

  Status = Arp->Configure (Arp, &ArpConfig);
  if (EFI_ERROR (Status)) {
    ArpSb->DestroyChild (ArpSb, ArpChild);
    return EFI_DEVICE_ERROR;
  }

  //
  // Non-blocking ARP request.
  // Blocking Arp->Request(NULL) raises TPL to TPL_CALLBACK, preventing
  // MNP timer events from firing — ARP replies never get processed.
  // Non-blocking with polling at TPL_APPLICATION allows MNP timer to
  // receive ARP replies from SNP and deliver them to the ARP module.
  //
  {
    BOOLEAN    ArpDone;
    EFI_EVENT  ArpEvent;
    UINTN      PollI;

    ArpDone  = FALSE;
    ArpEvent = NULL;

    Status = gBS->CreateEvent (
                    EVT_NOTIFY_SIGNAL,
                    TPL_CALLBACK,
                    L3ArpNotify,
                    &ArpDone,
                    &ArpEvent
                    );
    if (EFI_ERROR (Status)) {
      Arp->Configure (Arp, NULL);
      ArpSb->DestroyChild (ArpSb, ArpChild);
      return EFI_DEVICE_ERROR;
    }

    ZeroMem (&ResolvedAddr, sizeof (ResolvedAddr));
    Status = Arp->Request (Arp, (VOID *)TargetIp, ArpEvent, &ResolvedAddr);

    if (Status == EFI_SUCCESS) {
      //
      // Cache hit — already resolved
      //
      CopyMem (TargetMac, &ResolvedAddr, 6);
    } else if (!EFI_ERROR (Status) || Status == EFI_NOT_READY) {
      //
      // Request queued — poll at TPL_APPLICATION (up to 10s)
      //
      for (PollI = 0; PollI < 10000 && !ArpDone; PollI++) {
        gBS->Stall (1000);  // 1ms
      }

      if (ArpDone) {
        CopyMem (TargetMac, &ResolvedAddr, 6);
        Status = EFI_SUCCESS;
      } else {
        Status = EFI_TIMEOUT;
      }
    }

    gBS->CloseEvent (ArpEvent);
  }

  Arp->Configure (Arp, NULL);
  ArpSb->DestroyChild (ArpSb, ArpChild);
  return Status;
}

/**
  Resolve target MAC via raw SNP ARP (fallback).
  Used when EFI_ARP_PROTOCOL is not available on the NIC handle.

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
L3ArpResolveViaSnp (
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
  Combined ARP resolver: tries ARP protocol first, falls back to raw SNP.

  @param[in]  Nic        NIC information structure.
  @param[in]  SrcIp      Our IP address (4 bytes).
  @param[in]  TargetIp   Target IP to resolve (4 bytes).
  @param[out] TargetMac  Resolved MAC address (6 bytes).
  @param[in]  TimeoutMs  Timeout in milliseconds (for SNP fallback).

  @retval EFI_SUCCESS    MAC resolved.
  @retval other          Resolution failed.
**/
STATIC
EFI_STATUS
L3ResolveTargetMac (
  IN  NIC_INFO    *Nic,
  IN  CONST UINT8 *SrcIp,
  IN  CONST UINT8 *TargetIp,
  OUT UINT8       *TargetMac,
  IN  UINTN       TimeoutMs
  )
{
  EFI_STATUS  Status;

  //
  // Method 1: Use EFI_ARP_PROTOCOL (works when IP4 stack is active)
  //
  if (Nic->HasArp) {
    Status = L3ArpResolveViaProtocol (Nic->Handle, SrcIp, TargetIp, TargetMac);
    if (!EFI_ERROR (Status)) {
      return EFI_SUCCESS;
    }
  }

  //
  // Method 2: Raw SNP fallback (only works when IP4 stack is NOT active)
  //
  if (Nic->Snp != NULL && Nic->Snp->Mode->State == EfiSimpleNetworkInitialized) {
    return L3ArpResolveViaSnp (Nic->Snp, SrcIp, TargetIp, TargetMac, TimeoutMs);
  }

  return EFI_NOT_READY;
}

//
// ============================================================
// ICMP helpers
// ============================================================
//

/**
  Send ICMP echo request via EFI_IP4_PROTOCOL.
  Creates a temporary IP4 child configured for ICMP, sends echo request,
  and waits for reply. The IP4 stack handles ARP resolution and routing
  internally, so no MAC address is needed.

  @param[in]  NicHandle    NIC handle with IP4 service binding.
  @param[in]  LocalIp      Our IPv4 address (4 bytes).
  @param[in]  SubnetMask   Subnet mask (4 bytes).
  @param[in]  Gateway      Gateway IP (4 bytes, can be all-zero).
  @param[in]  DstIp        Destination IP (4 bytes).
  @param[in]  SeqNum       ICMP sequence number.
  @param[in]  Ttl          IP TTL value.
  @param[in]  PayloadSize  ICMP payload data size.
  @param[in]  TimeoutMs    Timeout in milliseconds.
  @param[out] RttUs        Round-trip time in microseconds.
  @param[out] ReplyType    ICMP reply type (0=echo reply, 11=time exceeded, etc).
  @param[out] ReplyCode    ICMP reply code.

  @retval EFI_SUCCESS      Got a reply (check ReplyType).
  @retval EFI_TIMEOUT      No reply within timeout.
  @retval EFI_UNSUPPORTED  IP4 protocol not available.
  @retval EFI_NOT_READY    TX failed.
**/
STATIC
EFI_STATUS
L3SendIcmpViaIp4 (
  IN  EFI_HANDLE   NicHandle,
  IN  CONST UINT8  *LocalIp,
  IN  CONST UINT8  *SubnetMask,
  IN  CONST UINT8  *Gateway,
  IN  CONST UINT8  *DstIp,
  IN  UINT16       SeqNum,
  IN  UINT8        Ttl,
  IN  UINTN        PayloadSize,
  IN  UINTN        TimeoutMs,
  OUT UINT32       *RttUs,
  OUT UINT8        *ReplyType,
  OUT UINT8        *ReplyCode
  )
{
  EFI_STATUS                    Status;
  EFI_SERVICE_BINDING_PROTOCOL  *Ip4Sb;
  EFI_IP4_PROTOCOL              *Ip4;
  EFI_HANDLE                    Ip4Child;
  EFI_IP4_CONFIG_DATA           Ip4Config;
  EFI_IP4_COMPLETION_TOKEN      TxToken;
  EFI_IP4_COMPLETION_TOKEN      RxToken;
  EFI_IP4_TRANSMIT_DATA         TxData;
  EFI_IP4_OVERRIDE_DATA         Override;
  EFI_EVENT                     TxEvent;
  EFI_EVENT                     RxEvent;
  UINT8                         *IcmpBuf;
  ICMP_HEADER                   *Icmp;
  UINT64                        StartTick;
  UINT64                        CurTick;
  UINTN                         I;
  UINTN                         IcmpLen;
  EFI_STATUS                    Result;
  EFI_IPv4_ADDRESS              ZeroAddr;
  EFI_IPv4_ADDRESS              GwAddr;
  BOOLEAN                       HasGw;

  *RttUs     = 0;
  *ReplyType = 0;
  *ReplyCode = 0;

  IcmpBuf  = NULL;
  TxEvent  = NULL;
  RxEvent  = NULL;
  Ip4Sb    = NULL;
  Ip4      = NULL;
  Ip4Child = NULL;

  //
  // Open IP4 Service Binding
  //
  Status = gBS->OpenProtocol (
                  NicHandle,
                  &gEfiIp4ServiceBindingProtocolGuid,
                  (VOID **)&Ip4Sb,
                  gImageHandle,
                  NicHandle,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );
  if (EFI_ERROR (Status) || Ip4Sb == NULL) {
    return EFI_UNSUPPORTED;
  }

  //
  // Create IP4 child
  //
  Status = Ip4Sb->CreateChild (Ip4Sb, &Ip4Child);
  if (EFI_ERROR (Status) || Ip4Child == NULL) {
    return EFI_UNSUPPORTED;
  }

  //
  // Open IP4 protocol on child
  //
  Status = gBS->OpenProtocol (
                  Ip4Child,
                  &gEfiIp4ProtocolGuid,
                  (VOID **)&Ip4,
                  gImageHandle,
                  NicHandle,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );
  if (EFI_ERROR (Status) || Ip4 == NULL) {
    Ip4Sb->DestroyChild (Ip4Sb, Ip4Child);
    return EFI_UNSUPPORTED;
  }

  //
  // Configure IP4 for ICMP.
  // Use explicit StationAddress so Ip4->Transmit can queue immediately
  // (Transmit triggers internal ARP resolution). UseDefaultAddress may
  // cause Transmit to return EFI_NO_MAPPING if address not yet ready.
  //
  ZeroMem (&Ip4Config, sizeof (Ip4Config));
  Ip4Config.DefaultProtocol    = 1;      // ICMP
  Ip4Config.AcceptIcmpErrors   = TRUE;
  Ip4Config.UseDefaultAddress  = FALSE;
  CopyMem (&Ip4Config.StationAddress, LocalIp, 4);
  CopyMem (&Ip4Config.SubnetMask, SubnetMask, 4);
  Ip4Config.TimeToLive         = Ttl;
  Ip4Config.DoNotFragment      = FALSE;
  Ip4Config.RawData            = FALSE;

  Status = Ip4->Configure (Ip4, &Ip4Config);
  if (EFI_ERROR (Status)) {
    //
    // Explicit failed — fall back to UseDefaultAddress=TRUE.
    //
    ZeroMem (&Ip4Config, sizeof (Ip4Config));
    Ip4Config.DefaultProtocol    = 1;      // ICMP
    Ip4Config.AcceptIcmpErrors   = TRUE;
    Ip4Config.UseDefaultAddress  = TRUE;
    Ip4Config.TimeToLive         = Ttl;
    Ip4Config.DoNotFragment      = FALSE;
    Ip4Config.RawData            = FALSE;

    Status = Ip4->Configure (Ip4, &Ip4Config);
    if (EFI_ERROR (Status)) {
      Ip4Sb->DestroyChild (Ip4Sb, Ip4Child);
      return EFI_UNSUPPORTED;
    }
  }

  //
  // Add default route via gateway if gateway is configured
  //
  HasGw = FALSE;
  for (I = 0; I < 4; I++) {
    if (Gateway[I] != 0) {
      HasGw = TRUE;
      break;
    }
  }

  if (HasGw) {
    ZeroMem (&ZeroAddr, sizeof (ZeroAddr));
    CopyMem (&GwAddr, Gateway, 4);
    Ip4->Routes (Ip4, FALSE, &ZeroAddr, &ZeroAddr, &GwAddr);
  }

  Result = EFI_TIMEOUT;

  //
  // Build ICMP echo request (just ICMP header + payload, no IP/Ethernet headers)
  //
  if (PayloadSize > 65000) {
    PayloadSize = 65000;
  }

  IcmpLen = ICMP_HEADER_SIZE + PayloadSize;
  IcmpBuf = AllocateZeroPool (IcmpLen);
  if (IcmpBuf == NULL) {
    Result = EFI_OUT_OF_RESOURCES;
    goto Cleanup;
  }

  Icmp = (ICMP_HEADER *)IcmpBuf;
  Icmp->Type           = ICMP_TYPE_ECHO_REQUEST;
  Icmp->Code           = 0;
  Icmp->Checksum       = 0;
  Icmp->Identifier     = HTONS (L3_ICMP_ID);
  Icmp->SequenceNumber = HTONS (SeqNum);

  for (I = 0; I < PayloadSize; I++) {
    IcmpBuf[ICMP_HEADER_SIZE + I] = (UINT8)(I & 0xFF);
  }

  Icmp->Checksum = HTONS (PktChecksum (IcmpBuf, IcmpLen));

  //
  // Create TX event
  //
  Status = gBS->CreateEvent (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  L3Ip4DummyNotify,
                  NULL,
                  &TxEvent
                  );
  if (EFI_ERROR (Status)) {
    Result = EFI_DEVICE_ERROR;
    goto Cleanup;
  }

  //
  // Prepare TX data (1 fragment containing ICMP header + payload)
  //
  ZeroMem (&Override, sizeof (Override));
  CopyMem (&Override.SourceAddress, LocalIp, 4);
  Override.TypeOfService = 0;
  Override.TimeToLive    = Ttl;
  Override.DoNotFragment = FALSE;
  Override.Protocol      = 1;  // ICMP

  ZeroMem (&TxData, sizeof (TxData));
  CopyMem (&TxData.DestinationAddress, DstIp, 4);
  TxData.OverrideData                    = &Override;
  TxData.OptionsLength                   = 0;
  TxData.OptionsBuffer                   = NULL;
  TxData.TotalDataLength                 = (UINT32)IcmpLen;
  TxData.FragmentCount                   = 1;
  TxData.FragmentTable[0].FragmentLength = (UINT32)IcmpLen;
  TxData.FragmentTable[0].FragmentBuffer = IcmpBuf;

  ZeroMem (&TxToken, sizeof (TxToken));
  TxToken.Event         = TxEvent;
  TxToken.Status        = EFI_NOT_READY;
  TxToken.Packet.TxData = &TxData;

  StartTick = UtilGetTimestamp ();

  //
  // Transmit ICMP packet with retry.
  // IP4's internal ARP has RetryCount=3 (3s timeout). If ARP fails on the
  // first attempt, TxToken.Status is set to an error. However, the ARP
  // requests sent during the first attempt may have primed the gateway's
  // ARP cache or the platform's ARP cache (via MNP timer processing between
  // attempts). A retry typically succeeds immediately on cache hit.
  // TCP works (2s connect) because it retries internally — same logic here.
  //
  {
    UINTN  Attempt;

    for (Attempt = 0; Attempt < 3; Attempt++) {
      TxToken.Status = EFI_NOT_READY;

      Status = Ip4->Transmit (Ip4, &TxToken);
      if (EFI_ERROR (Status)) {
        //
        // Can't queue TX — stall to let pending events process, then retry
        //
        gBS->Stall (500000);  // 500ms
        continue;
      }

      //
      // Wait for TX completion (up to 4 seconds per attempt).
      // Ip4->Poll triggers MnpPoll which receives frames from SNP,
      // including ARP replies for the internal ARP resolution.
      //
      for (I = 0; I < 4000 && TxToken.Status == EFI_NOT_READY; I++) {
        Ip4->Poll (Ip4);
        gBS->Stall (1000);
      }

      if (!EFI_ERROR (TxToken.Status)) {
        break;  // TX succeeded — ARP resolved, packet sent
      }

      //
      // TX failed (likely ARP timeout). Stall 500ms to allow background
      // MNP timer events to process any late ARP replies, then retry.
      // Do NOT Cancel — Cancel clears pending ARP state.
      //
      if (Attempt < 2) {
        for (I = 0; I < 500; I++) {
          Ip4->Poll (Ip4);
          gBS->Stall (1000);
        }
      }
    }
  }

  if (EFI_ERROR (TxToken.Status)) {
    Result = EFI_NOT_READY;
    goto Cleanup;
  }

  //
  // Create RX event
  //
  Status = gBS->CreateEvent (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  L3Ip4DummyNotify,
                  NULL,
                  &RxEvent
                  );
  if (EFI_ERROR (Status)) {
    Result = EFI_DEVICE_ERROR;
    goto Cleanup;
  }

  //
  // Set up receive token
  //
  ZeroMem (&RxToken, sizeof (RxToken));
  RxToken.Event  = RxEvent;
  RxToken.Status = EFI_NOT_READY;

  Status = Ip4->Receive (Ip4, &RxToken);
  if (EFI_ERROR (Status)) {
    Result = EFI_NOT_READY;
    goto Cleanup;
  }

  //
  // Poll for ICMP reply
  //
  for (I = 0; I < TimeoutMs && RxToken.Status == EFI_NOT_READY; I++) {
    Ip4->Poll (Ip4);
    gBS->Stall (1000);
  }

  if (RxToken.Status == EFI_NOT_READY) {
    //
    // Timeout - cancel pending receive
    //
    Ip4->Cancel (Ip4, &RxToken);
    Ip4->Poll (Ip4);
    Result = EFI_TIMEOUT;
    goto Cleanup;
  }

  if (EFI_ERROR (RxToken.Status)) {
    Result = EFI_DEVICE_ERROR;
    goto Cleanup;
  }

  //
  // Process received ICMP reply
  //
  if (RxToken.Packet.RxData != NULL) {
    EFI_IP4_RECEIVE_DATA  *RxData;

    RxData = RxToken.Packet.RxData;

    CurTick = UtilGetTimestamp ();
    *RttUs = (UINT32)((CurTick - StartTick) * 1000000);

    if (RxData->DataLength >= ICMP_HEADER_SIZE &&
        RxData->FragmentCount > 0 &&
        RxData->FragmentTable[0].FragmentLength >= ICMP_HEADER_SIZE) {
      ICMP_HEADER  *RxIcmp;

      RxIcmp     = (ICMP_HEADER *)RxData->FragmentTable[0].FragmentBuffer;
      *ReplyType = RxIcmp->Type;
      *ReplyCode = RxIcmp->Code;
      Result     = EFI_SUCCESS;
    } else {
      Result = EFI_DEVICE_ERROR;
    }

    //
    // Recycle RX buffer
    //
    gBS->SignalEvent (RxData->RecycleSignal);
  }

Cleanup:
  if (IcmpBuf != NULL) {
    FreePool (IcmpBuf);
  }

  if (TxEvent != NULL) {
    gBS->CloseEvent (TxEvent);
  }

  if (RxEvent != NULL) {
    gBS->CloseEvent (RxEvent);
  }

  if (Ip4 != NULL) {
    Ip4->Configure (Ip4, NULL);
  }

  if (Ip4Child != NULL) {
    Ip4Sb->DestroyChild (Ip4Sb, Ip4Child);
  }

  return Result;
}

/**
  Send an ICMP Echo Request via raw SNP and wait for reply (fallback).
  Used when EFI_IP4_PROTOCOL is not available.

  @param[in]  Snp         SNP protocol instance.
  @param[in]  SrcMac      Source MAC (6 bytes).
  @param[in]  DstMac      Destination MAC (6 bytes).
  @param[in]  SrcIp       Source IP (4 bytes).
  @param[in]  DstIp       Destination IP (4 bytes).
  @param[in]  SeqNum      ICMP sequence number.
  @param[in]  Ttl         IP TTL value.
  @param[in]  PayloadSize ICMP payload data size.
  @param[in]  TimeoutMs   Timeout in milliseconds.
  @param[out] RttUs       Round-trip time in microseconds.
  @param[out] ReplyType   ICMP reply type.
  @param[out] ReplyCode   ICMP reply code.

  @retval EFI_SUCCESS    Got a reply (check ReplyType).
  @retval EFI_TIMEOUT    No reply within timeout.
  @retval EFI_NOT_READY  TX failed.
**/
STATIC
EFI_STATUS
L3SendIcmpViaSnp (
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

  Payload = TxBuf + Offset + ICMP_HEADER_SIZE;
  for (I = 0; I < PayloadSize; I++) {
    Payload[I] = (UINT8)(I & 0xFF);
  }

  TxIcmp->Checksum = HTONS (PktChecksum (TxBuf + Offset, IcmpLen));

  TxLen = Offset + IcmpLen;

  Snp->ReceiveFilters (
    Snp,
    EFI_SIMPLE_NETWORK_RECEIVE_UNICAST |
    EFI_SIMPLE_NETWORK_RECEIVE_BROADCAST,
    0, FALSE, 0, NULL
    );

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

          if (RxIcmp->Type == ICMP_TYPE_ECHO_REPLY &&
              NTOHS (RxIcmp->Identifier) == L3_ICMP_ID) {
            CurTick    = UtilGetTimestamp ();
            *RttUs     = (UINT32)((CurTick - StartTick) * 1000000);
            *ReplyType = RxIcmp->Type;
            *ReplyCode = RxIcmp->Code;
            return EFI_SUCCESS;
          }

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

  @param[in]  Nic        NIC information.
  @param[in]  Config     Test configuration with IPs.
  @param[out] NextHopMac Resolved MAC for next hop (6 bytes).

  @retval EFI_SUCCESS    MAC resolved.
  @retval other          Resolution failed.
**/
STATIC
EFI_STATUS
L3ResolveNextHopMac (
  IN  NIC_INFO    *Nic,
  IN  TEST_CONFIG *Config,
  OUT UINT8       *NextHopMac
  )
{
  CONST UINT8  *ResolveIp;

  if (L3IsSameSubnet (Config->LocalIp.Addr, Config->TargetIp.Addr, Config->SubnetMask.Addr)) {
    ResolveIp = Config->TargetIp.Addr;
  } else {
    ResolveIp = Config->Gateway.Addr;
  }

  return L3ResolveTargetMac (Nic, Config->LocalIp.Addr, ResolveIp, NextHopMac, 3000);
}

/**
  High-level ICMP ping: try IP4 protocol first, fall back to raw SNP.
  Handles ARP resolution internally for the SNP fallback path.

  @param[in]  Nic         NIC information.
  @param[in]  Config      Test configuration.
  @param[in]  DstIp       Destination IP (4 bytes).
  @param[in]  SeqNum      ICMP sequence number.
  @param[in]  Ttl         IP TTL value.
  @param[in]  PayloadSize ICMP payload data size.
  @param[in]  TimeoutMs   Timeout in milliseconds.
  @param[out] RttUs       Round-trip time in microseconds.
  @param[out] ReplyType   ICMP reply type.
  @param[out] ReplyCode   ICMP reply code.

  @retval EFI_SUCCESS    Got a reply.
  @retval EFI_TIMEOUT    No reply within timeout.
  @retval EFI_NOT_READY  Network not available.
**/
STATIC
EFI_STATUS
L3Ping (
  IN  NIC_INFO    *Nic,
  IN  TEST_CONFIG *Config,
  IN  CONST UINT8 *DstIp,
  IN  UINT16      SeqNum,
  IN  UINT8       Ttl,
  IN  UINTN       PayloadSize,
  IN  UINTN       TimeoutMs,
  OUT UINT32      *RttUs,
  OUT UINT8       *ReplyType,
  OUT UINT8       *ReplyCode
  )
{
  EFI_STATUS  Status;

  //
  // Method 1: IP4 protocol (handles ARP and routing internally)
  //
  if (Nic->HasIp4 && Nic->HasIpConfig) {
    Status = L3SendIcmpViaIp4 (
               Nic->Handle,
               Nic->Ipv4Address.Addr,
               Nic->SubnetMask.Addr,
               Nic->Gateway.Addr,
               DstIp,
               SeqNum,
               Ttl,
               PayloadSize,
               TimeoutMs,
               RttUs,
               ReplyType,
               ReplyCode
               );
    //
    // Return only on success or definite timeout.
    // For any other error (NOT_READY, UNSUPPORTED, etc.), fall through
    // to the raw SNP path, which can handle ARP resolution independently.
    //
    if (Status == EFI_SUCCESS || Status == EFI_TIMEOUT) {
      return Status;
    }
  }

  //
  // Method 2: Raw SNP (resolve MAC first, then send via SNP)
  //
  if (Nic->Snp != NULL && Nic->Snp->Mode->State == EfiSimpleNetworkInitialized) {
    UINT8  DstMac[6];

    Status = L3ResolveNextHopMac (Nic, Config, DstMac);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    return L3SendIcmpViaSnp (
             Nic->Snp,
             Nic->Snp->Mode->CurrentAddress.Addr,
             DstMac,
             Config->LocalIp.Addr,
             DstIp,
             SeqNum,
             Ttl,
             PayloadSize,
             TimeoutMs,
             RttUs,
             ReplyType,
             ReplyCode
             );
  }

  return EFI_NOT_READY;
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
  EFI_STATUS  Status;
  UINT32      RttUs;
  UINT8       ReplyType;
  UINT8       ReplyCode;

  Result->PacketsSent = 1;
  Result->BytesSent   = IPV4_MIN_HEADER_SIZE + ICMP_HEADER_SIZE + 32;

  Status = L3Ping (
             Nic, Config,
             Config->TargetIp.Addr,
             1,     // SeqNum
             64,    // TTL
             32,    // PayloadSize
             Config->TimeoutMs > 0 ? Config->TimeoutMs : 3000,
             &RttUs,
             &ReplyType,
             &ReplyCode
             );

  if (EFI_ERROR (Status)) {
    if (Status == EFI_NOT_READY) {
      Result->StatusCode = TEST_RESULT_SKIP;
      UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                     L"Network stack not available for ICMP");
    } else {
      Result->StatusCode = TEST_RESULT_FAIL;
      UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                     L"No ICMP echo reply from %d.%d.%d.%d",
                     Config->TargetIp.Addr[0], Config->TargetIp.Addr[1],
                     Config->TargetIp.Addr[2], Config->TargetIp.Addr[3]);
      UnicodeSPrint (Result->FailReason, sizeof (Result->FailReason),
                     L"ICMP echo request timed out");
      UnicodeSPrint (Result->Suggestion, sizeof (Result->Suggestion),
                     L"Check firewall rules, target IP, and network path");
    }

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
  EFI_STATUS  Status;
  UINT32      RttUs;
  UINT8       ReplyType;
  UINT8       ReplyCode;
  UINTN       I;
  UINTN       Count;
  UINTN       Received;
  UINT32      MinRtt;
  UINT32      MaxRtt;
  UINT64      TotalRtt;

  Count    = (Config->Iterations > 0 && Config->Iterations <= 10) ? Config->Iterations : 5;
  Received = 0;
  MinRtt   = 0xFFFFFFFF;
  MaxRtt   = 0;
  TotalRtt = 0;

  for (I = 0; I < Count; I++) {
    Result->PacketsSent++;

    Status = L3Ping (
               Nic, Config,
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
  EFI_STATUS  Status;
  UINT32      RttUs;
  UINT8       ReplyType;
  UINT8       ReplyCode;
  UINT8       Ttl;
  UINT8       MaxTtl;
  UINTN       HopsResponded;
  BOOLEAN     TargetReached;

  MaxTtl        = 16;
  HopsResponded = 0;
  TargetReached = FALSE;

  for (Ttl = 1; Ttl <= MaxTtl; Ttl++) {
    Result->PacketsSent++;

    Status = L3Ping (
               Nic, Config,
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
  Sends ICMP echo requests with varying payload sizes to discover the path MTU.
  Binary search between minimum and maximum.

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
  EFI_STATUS  Status;
  UINT32      RttUs;
  UINT8       ReplyType;
  UINT8       ReplyCode;
  UINTN       Lo;
  UINTN       Hi;
  UINTN       Mid;
  UINTN       LargestOk;
  UINT16      SeqNum;

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

    Status = L3Ping (
               Nic, Config,
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
  Tests whether the network path handles large ICMP packets correctly.
  Sends a large ICMP payload (1200 bytes) which the IP4 stack may
  fragment if needed.

  PASS: Large packet sent and reply received
  WARN: TX succeeded but no reply
  FAIL: TX failed
**/
EFI_STATUS
TestL3IpFragmentation (
  IN  NIC_INFO         *Nic,
  IN  TEST_CONFIG      *Config,
  OUT TEST_RESULT_DATA *Result
  )
{
  EFI_STATUS  Status;
  UINT32      RttUs;
  UINT8       ReplyType;
  UINT8       ReplyCode;
  UINTN       PayloadSize;

  PayloadSize = 1200;

  Result->PacketsSent = 1;
  Result->BytesSent   = IPV4_MIN_HEADER_SIZE + ICMP_HEADER_SIZE + PayloadSize;

  Status = L3Ping (
             Nic, Config,
             Config->TargetIp.Addr,
             200,
             64,
             PayloadSize,
             3000,
             &RttUs,
             &ReplyType,
             &ReplyCode
             );

  if (!EFI_ERROR (Status) && ReplyType == ICMP_TYPE_ECHO_REPLY) {
    Result->PacketsReceived = 1;
    Result->StatusCode = TEST_RESULT_PASS;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Large ICMP echo OK (payload=%d, RTT=%d us)",
                   PayloadSize, RttUs);
    return EFI_SUCCESS;
  }

  if (!EFI_ERROR (Status)) {
    //
    // Got a reply but not echo reply (maybe dest unreachable)
    //
    Result->PacketsReceived = 1;
    Result->StatusCode = TEST_RESULT_WARN;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Large ICMP sent (payload=%d) got ICMP type %d code %d",
                   PayloadSize, ReplyType, ReplyCode);
    return EFI_SUCCESS;
  }

  if (Status == EFI_TIMEOUT) {
    Result->StatusCode = TEST_RESULT_WARN;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Large ICMP sent (payload=%d) but no reply in 3s", PayloadSize);
    UnicodeSPrint (Result->Detail, sizeof (Result->Detail),
                   L"Frame sent successfully. Reply may require IP reassembly "
                   L"support on the path.");
  } else if (Status == EFI_NOT_READY) {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Large ICMP TX failed (payload=%d)", PayloadSize);
    UnicodeSPrint (Result->Suggestion, sizeof (Result->Suggestion),
                   L"NIC or IP4 stack may not support this frame size");
  } else {
    Result->StatusCode = TEST_RESULT_SKIP;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Network stack not available for large ICMP test");
  }

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
  Sends ICMP echo via IP4 protocol, then validates the reply IP header.
  When using IP4 protocol, the IP header is validated by the stack itself;
  we extract and report the header fields from the RxData.

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
  EFI_STATUS                    Status;
  EFI_SERVICE_BINDING_PROTOCOL  *Ip4Sb;
  EFI_IP4_PROTOCOL              *Ip4;
  EFI_HANDLE                    Ip4Child;
  EFI_IP4_CONFIG_DATA           Ip4Config;
  EFI_IP4_COMPLETION_TOKEN      TxToken;
  EFI_IP4_COMPLETION_TOKEN      RxToken;
  EFI_IP4_TRANSMIT_DATA         TxData;
  EFI_IP4_OVERRIDE_DATA         Override;
  EFI_EVENT                     TxEvent;
  EFI_EVENT                     RxEvent;
  UINT8                         *IcmpBuf;
  ICMP_HEADER                   *Icmp;
  UINTN                         I;
  UINTN                         IcmpLen;
  UINTN                         PayloadSize;
  EFI_IPv4_ADDRESS              ZeroAddr;
  EFI_IPv4_ADDRESS              GwAddr;
  BOOLEAN                       HasGw;

  Ip4Sb    = NULL;
  Ip4      = NULL;
  Ip4Child = NULL;
  IcmpBuf  = NULL;
  TxEvent  = NULL;
  RxEvent  = NULL;

  //
  // Need IP4 protocol for proper ICMP + header extraction
  //
  if (!Nic->HasIp4 || !Nic->HasIpConfig) {
    Result->StatusCode = TEST_RESULT_SKIP;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"IP4 protocol or config not available");
    return EFI_SUCCESS;
  }

  //
  // Open IP4 Service Binding and create child
  //
  Status = gBS->OpenProtocol (
                  Nic->Handle,
                  &gEfiIp4ServiceBindingProtocolGuid,
                  (VOID **)&Ip4Sb,
                  gImageHandle,
                  Nic->Handle,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );
  if (EFI_ERROR (Status)) {
    Result->StatusCode = TEST_RESULT_SKIP;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Cannot open IP4 Service Binding");
    return EFI_SUCCESS;
  }

  Status = Ip4Sb->CreateChild (Ip4Sb, &Ip4Child);
  if (EFI_ERROR (Status)) {
    Result->StatusCode = TEST_RESULT_SKIP;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Cannot create IP4 child");
    return EFI_SUCCESS;
  }

  Status = gBS->OpenProtocol (
                  Ip4Child,
                  &gEfiIp4ProtocolGuid,
                  (VOID **)&Ip4,
                  gImageHandle,
                  Nic->Handle,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );
  if (EFI_ERROR (Status)) {
    Ip4Sb->DestroyChild (Ip4Sb, Ip4Child);
    Result->StatusCode = TEST_RESULT_SKIP;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Cannot open IP4 protocol");
    return EFI_SUCCESS;
  }

  //
  // Configure for ICMP
  //
  ZeroMem (&Ip4Config, sizeof (Ip4Config));
  Ip4Config.DefaultProtocol    = 1;
  Ip4Config.AcceptAnyProtocol  = FALSE;
  Ip4Config.AcceptIcmpErrors   = TRUE;
  Ip4Config.AcceptBroadcast    = FALSE;
  Ip4Config.AcceptPromiscuous  = FALSE;
  Ip4Config.UseDefaultAddress  = FALSE;
  CopyMem (&Ip4Config.StationAddress, Nic->Ipv4Address.Addr, 4);
  CopyMem (&Ip4Config.SubnetMask, Nic->SubnetMask.Addr, 4);
  Ip4Config.TimeToLive         = 64;
  Ip4Config.RawData            = FALSE;

  Status = Ip4->Configure (Ip4, &Ip4Config);
  if (EFI_ERROR (Status)) {
    //
    // Fallback: UseDefaultAddress=TRUE
    //
    ZeroMem (&Ip4Config, sizeof (Ip4Config));
    Ip4Config.DefaultProtocol    = 1;
    Ip4Config.AcceptAnyProtocol  = FALSE;
    Ip4Config.AcceptIcmpErrors   = TRUE;
    Ip4Config.UseDefaultAddress  = TRUE;
    Ip4Config.TimeToLive         = 64;
    Ip4Config.RawData            = FALSE;

    Status = Ip4->Configure (Ip4, &Ip4Config);
    if (EFI_ERROR (Status)) {
      Ip4Sb->DestroyChild (Ip4Sb, Ip4Child);
      Result->StatusCode = TEST_RESULT_SKIP;
      UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                     L"IP4 Configure failed: %r", Status);
      return EFI_SUCCESS;
    }
  }

  //
  // Add default route
  //
  HasGw = FALSE;
  for (I = 0; I < 4; I++) {
    if (Nic->Gateway.Addr[I] != 0) { HasGw = TRUE; break; }
  }

  if (HasGw) {
    ZeroMem (&ZeroAddr, sizeof (ZeroAddr));
    CopyMem (&GwAddr, Nic->Gateway.Addr, 4);
    Ip4->Routes (Ip4, FALSE, &ZeroAddr, &ZeroAddr, &GwAddr);
  }

  //
  // Build ICMP echo request
  //
  PayloadSize = 32;
  IcmpLen     = ICMP_HEADER_SIZE + PayloadSize;
  IcmpBuf     = AllocateZeroPool (IcmpLen);
  if (IcmpBuf == NULL) {
    Ip4->Configure (Ip4, NULL);
    Ip4Sb->DestroyChild (Ip4Sb, Ip4Child);
    Result->StatusCode = TEST_RESULT_ERROR;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Memory allocation failed");
    return EFI_SUCCESS;
  }

  Icmp = (ICMP_HEADER *)IcmpBuf;
  Icmp->Type           = ICMP_TYPE_ECHO_REQUEST;
  Icmp->Code           = 0;
  Icmp->Checksum       = 0;
  Icmp->Identifier     = HTONS (L3_ICMP_ID);
  Icmp->SequenceNumber = HTONS (300);

  for (I = 0; I < PayloadSize; I++) {
    IcmpBuf[ICMP_HEADER_SIZE + I] = (UINT8)(I & 0xFF);
  }

  Icmp->Checksum = HTONS (PktChecksum (IcmpBuf, IcmpLen));

  //
  // Create TX event and transmit
  //
  Status = gBS->CreateEvent (EVT_NOTIFY_SIGNAL, TPL_CALLBACK, L3Ip4DummyNotify, NULL, &TxEvent);
  if (EFI_ERROR (Status)) {
    goto HeaderCleanup;
  }

  ZeroMem (&Override, sizeof (Override));
  CopyMem (&Override.SourceAddress, Nic->Ipv4Address.Addr, 4);
  Override.TimeToLive = 64;
  Override.Protocol   = 1;

  ZeroMem (&TxData, sizeof (TxData));
  CopyMem (&TxData.DestinationAddress, Config->TargetIp.Addr, 4);
  TxData.OverrideData                    = &Override;
  TxData.TotalDataLength                 = (UINT32)IcmpLen;
  TxData.FragmentCount                   = 1;
  TxData.FragmentTable[0].FragmentLength = (UINT32)IcmpLen;
  TxData.FragmentTable[0].FragmentBuffer = IcmpBuf;

  ZeroMem (&TxToken, sizeof (TxToken));
  TxToken.Event         = TxEvent;
  TxToken.Status        = EFI_NOT_READY;
  TxToken.Packet.TxData = &TxData;

  Status = Ip4->Transmit (Ip4, &TxToken);
  if (EFI_ERROR (Status)) {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"ICMP TX failed: %r", Status);
    goto HeaderCleanup;
  }

  Result->PacketsSent = 1;

  //
  // Wait for TX
  //
  for (I = 0; I < 2000 && TxToken.Status == EFI_NOT_READY; I++) {
    Ip4->Poll (Ip4);
    gBS->Stall (1000);
  }

  //
  // Set up receive
  //
  Status = gBS->CreateEvent (EVT_NOTIFY_SIGNAL, TPL_CALLBACK, L3Ip4DummyNotify, NULL, &RxEvent);
  if (EFI_ERROR (Status)) {
    goto HeaderCleanup;
  }

  ZeroMem (&RxToken, sizeof (RxToken));
  RxToken.Event  = RxEvent;
  RxToken.Status = EFI_NOT_READY;

  Status = Ip4->Receive (Ip4, &RxToken);
  if (EFI_ERROR (Status)) {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"IP4 Receive setup failed: %r", Status);
    goto HeaderCleanup;
  }

  //
  // Poll for reply
  //
  for (I = 0; I < 3000 && RxToken.Status == EFI_NOT_READY; I++) {
    Ip4->Poll (Ip4);
    gBS->Stall (1000);
  }

  if (RxToken.Status == EFI_NOT_READY) {
    Ip4->Cancel (Ip4, &RxToken);
    Ip4->Poll (Ip4);
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"No IP reply received to validate");
    UnicodeSPrint (Result->Suggestion, sizeof (Result->Suggestion),
                   L"Verify target responds to ICMP echo");
    goto HeaderCleanup;
  }

  if (EFI_ERROR (RxToken.Status)) {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"IP4 Receive error: %r", RxToken.Status);
    goto HeaderCleanup;
  }

  //
  // Extract and validate IP header from RxData
  //
  if (RxToken.Packet.RxData != NULL) {
    EFI_IP4_RECEIVE_DATA  *RxData;
    EFI_IP4_HEADER        *Hdr;
    UINT8                 Version;
    UINT8                 Ihl;

    RxData = RxToken.Packet.RxData;
    Hdr    = RxData->Header;

    Result->PacketsReceived = 1;
    Result->BytesReceived   = RxData->DataLength + RxData->HeaderLength;

    Version = (Hdr->Version);
    Ihl     = (Hdr->HeaderLength);

    UnicodeSPrint (Result->Detail, sizeof (Result->Detail),
                   L"Ver=%d IHL=%d TotalLen=%d TTL=%d Proto=%d "
                   L"Checksum=OK (validated by IP4 stack) "
                   L"Src=%d.%d.%d.%d Dst=%d.%d.%d.%d",
                   Version, Ihl,
                   RxData->DataLength + RxData->HeaderLength,
                   Hdr->TimeToLive,
                   Hdr->Protocol,
                   Hdr->SourceAddress.Addr[0], Hdr->SourceAddress.Addr[1],
                   Hdr->SourceAddress.Addr[2], Hdr->SourceAddress.Addr[3],
                   Hdr->DestinationAddress.Addr[0], Hdr->DestinationAddress.Addr[1],
                   Hdr->DestinationAddress.Addr[2], Hdr->DestinationAddress.Addr[3]);

    if (Version != 4) {
      Result->StatusCode = TEST_RESULT_FAIL;
      UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                     L"Invalid IP version: %d (expected 4)", Version);
    } else if (Ihl < 5) {
      Result->StatusCode = TEST_RESULT_FAIL;
      UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                     L"Invalid IHL: %d (minimum 5)", Ihl);
    } else if (Hdr->TimeToLive == 0) {
      Result->StatusCode = TEST_RESULT_WARN;
      UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                     L"Reply has TTL=0 (unusual)");
    } else {
      Result->StatusCode = TEST_RESULT_PASS;
      UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                     L"IP header valid: Ver=4 IHL=%d TTL=%d Checksum OK",
                     Ihl, Hdr->TimeToLive);
    }

    //
    // Recycle
    //
    gBS->SignalEvent (RxData->RecycleSignal);
  }

HeaderCleanup:
  if (IcmpBuf != NULL) {
    FreePool (IcmpBuf);
  }

  if (TxEvent != NULL) {
    gBS->CloseEvent (TxEvent);
  }

  if (RxEvent != NULL) {
    gBS->CloseEvent (RxEvent);
  }

  if (Ip4 != NULL) {
    Ip4->Configure (Ip4, NULL);
  }

  if (Ip4Child != NULL) {
    Ip4Sb->DestroyChild (Ip4Sb, Ip4Child);
  }

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
  EFI_STATUS  Status;
  BOOLEAN     HasGateway;
  BOOLEAN     GwOnSubnet;
  UINT8       GwMac[6];
  UINTN       I;
  CHAR16      MacStr[20];

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
  // Try to ARP-resolve the gateway (via ARP protocol or raw SNP)
  //
  Status = L3ResolveTargetMac (
             Nic,
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
  Uses ARP protocol to check if another host claims our IP address.
  Sends ARP request for our own IP and checks if a different MAC responds.

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
  EFI_STATUS                    Status;
  CONST UINT8                   *ProbeIp;
  UINT8                         ResolvedMac[6];
  CHAR16                        MacStr[20];

  //
  // Use our configured IP or the test config local IP
  //
  if (Nic->HasIpConfig) {
    ProbeIp = Nic->Ipv4Address.Addr;
  } else {
    ProbeIp = Config->LocalIp.Addr;
  }

  //
  // Method 1: ARP protocol - resolve our own IP
  // If someone else responds with a different MAC, it's a duplicate.
  //
  if (Nic->HasArp) {
    Status = L3ArpResolveViaProtocol (
               Nic->Handle,
               Nic->Ipv4Address.Addr,
               ProbeIp,
               ResolvedMac
               );

    if (!EFI_ERROR (Status)) {
      //
      // Got a resolution. Check if it's our own MAC or someone else's.
      //
      if (CompareMem (ResolvedMac, Nic->CurrentMac.Addr, 6) != 0) {
        //
        // Different MAC responded - duplicate IP!
        //
        UtilFormatMac (ResolvedMac, MacStr);
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

      //
      // Resolved to our own MAC - this is expected, no duplicate
      //
      Result->StatusCode = TEST_RESULT_PASS;
      UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                     L"No duplicate IP detected for %d.%d.%d.%d",
                     ProbeIp[0], ProbeIp[1], ProbeIp[2], ProbeIp[3]);
      return EFI_SUCCESS;
    }

    //
    // ARP timed out - no one (including us) responded. No duplicate.
    //
    Result->StatusCode = TEST_RESULT_PASS;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"No duplicate IP detected for %d.%d.%d.%d (ARP timeout)",
                   ProbeIp[0], ProbeIp[1], ProbeIp[2], ProbeIp[3]);
    return EFI_SUCCESS;
  }

  //
  // Method 2: Raw SNP gratuitous ARP probe (fallback)
  // This may not work when IP4 stack is active (MNP consumes replies)
  //
  if (Nic->Snp != NULL && Nic->Snp->Mode->State == EfiSimpleNetworkInitialized) {
    EFI_SIMPLE_NETWORK_PROTOCOL  *Snp;
    UINT8                        TxBuf[64];
    UINT8                        RxBuf[MAX_ETHERNET_FRAME_SIZE];
    UINTN                        TxLen;
    UINTN                        RxLen;
    UINTN                        HdrSize;
    UINTN                        I;
    ETHERNET_HEADER              *RxEth;
    ARP_HEADER                   *RxArp;
    UINT8                        ZeroIp[4];

    Snp = Nic->Snp;

    //
    // Build ARP probe: Sender IP = 0.0.0.0, Target IP = our IP
    //
    ZeroMem (ZeroIp, sizeof (ZeroIp));
    ZeroMem (TxBuf, sizeof (TxBuf));
    TxLen = PktBuildArpRequest (TxBuf, Snp->Mode->CurrentAddress.Addr, ZeroIp, ProbeIp);

    Snp->ReceiveFilters (
      Snp,
      EFI_SIMPLE_NETWORK_RECEIVE_UNICAST |
      EFI_SIMPLE_NETWORK_RECEIVE_BROADCAST,
      0, FALSE, 0, NULL
      );

    Status = Snp->Transmit (Snp, 0, TxLen, TxBuf, NULL, NULL, NULL);
    if (EFI_ERROR (Status)) {
      Result->StatusCode = TEST_RESULT_WARN;
      UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                     L"ARP probe TX failed: %r (cannot verify)", Status);
      return EFI_SUCCESS;
    }

    Result->PacketsSent = 1;
    Result->BytesSent   = TxLen;

    //
    // Listen for ARP replies for 3 seconds
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
            if (RxArp->SenderIp[0] == ProbeIp[0] &&
                RxArp->SenderIp[1] == ProbeIp[1] &&
                RxArp->SenderIp[2] == ProbeIp[2] &&
                RxArp->SenderIp[3] == ProbeIp[3]) {
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
  }

  Result->StatusCode = TEST_RESULT_PASS;
  UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                 L"No duplicate IP detected for %d.%d.%d.%d",
                 ProbeIp[0], ProbeIp[1], ProbeIp[2], ProbeIp[3]);

  return EFI_SUCCESS;
}
