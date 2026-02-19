/** @file
  Protocol probe implementation.
  Periodic echo test for ARP, ICMP, UDP, TCP protocols.
  Each probe sends a message with a sequence ID and expects echo back.
  Reuses patterns from Layer2DataLink.c, Layer3Network.c, Layer4Transport.c.
**/

#include <DDTSoftNetTest.h>
#include <ProtocolProbe.h>
#include <PacketDefs.h>
#include <Protocol/ServiceBinding.h>
#include <Protocol/Arp.h>
#include <Protocol/Ip4.h>

//
// ICMP echo identifier for probes
//
#define PROBE_ICMP_ID  0xDD50

//
// No-op event callback — we poll Token.Status directly
//
STATIC
VOID
EFIAPI
ProbeNotifyStub (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  // Nothing — completion checked via Token.Status
}

//
// ARP completion callback — sets BOOLEAN flag to TRUE
//
STATIC
VOID
EFIAPI
ProbeArpNotify (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  if (Context != NULL) {
    *((BOOLEAN *)Context) = TRUE;
  }
}

// ============================================================
// Probe utility helpers
// ============================================================

/**
  Build the echo payload string.
  Format: "DDTECHO|ID=%04d|TS=%08X" (28 bytes fixed)

  @param[out] Buffer   Output buffer (at least PROBE_PAYLOAD_SIZE bytes).
  @param[in]  SeqId    Sequence ID.
**/
STATIC
VOID
ProbeBuildPayload (
  OUT CHAR8   *Buffer,
  IN  UINT32  SeqId
  )
{
  UINT64  Ts;
  UINTN   I;
  CHAR8   Hex[9];
  CHAR8   IdStr[5];

  //
  // Simple timestamp from stall-based counter
  //
  Ts = UtilGetTimestamp ();

  //
  // Format ID as 4-digit decimal
  //
  IdStr[0] = (CHAR8)('0' + ((SeqId / 1000) % 10));
  IdStr[1] = (CHAR8)('0' + ((SeqId / 100) % 10));
  IdStr[2] = (CHAR8)('0' + ((SeqId / 10) % 10));
  IdStr[3] = (CHAR8)('0' + (SeqId % 10));
  IdStr[4] = 0;

  //
  // Format TS as 8-digit hex
  //
  for (I = 0; I < 8; I++) {
    UINT8  Nib;
    Nib = (UINT8)((Ts >> (28 - I * 4)) & 0xF);
    Hex[I] = (CHAR8)(Nib < 10 ? '0' + Nib : 'A' + Nib - 10);
  }
  Hex[8] = 0;

  //
  // "DDTECHO|ID=xxxx|TS=xxxxxxxx" = 28 chars
  //
  Buffer[0]  = 'D'; Buffer[1]  = 'D'; Buffer[2]  = 'T'; Buffer[3]  = 'E';
  Buffer[4]  = 'C'; Buffer[5]  = 'H'; Buffer[6]  = 'O'; Buffer[7]  = '|';
  Buffer[8]  = 'I'; Buffer[9]  = 'D'; Buffer[10] = '=';
  Buffer[11] = IdStr[0]; Buffer[12] = IdStr[1]; Buffer[13] = IdStr[2]; Buffer[14] = IdStr[3];
  Buffer[15] = '|';
  Buffer[16] = 'T'; Buffer[17] = 'S'; Buffer[18] = '=';
  Buffer[19] = Hex[0]; Buffer[20] = Hex[1]; Buffer[21] = Hex[2]; Buffer[22] = Hex[3];
  Buffer[23] = Hex[4]; Buffer[24] = Hex[5]; Buffer[25] = Hex[6]; Buffer[26] = Hex[7];
  Buffer[27] = 0;

  //
  // Pad remaining bytes with zeros
  //
  for (I = 28; I < PROBE_PAYLOAD_SIZE; I++) {
    Buffer[I] = 0;
  }
}

/**
  Record a probe result into stats.

  @param[in,out] Stats   Probe statistics.
  @param[in]     Status  PROBE_STATUS_* result.
  @param[in]     RttUs   Round-trip time in microseconds (0 if fail/timeout).
**/
STATIC
VOID
ProbeRecordResult (
  IN OUT PROBE_STATS  *Stats,
  IN     UINT32       Status,
  IN     UINT32       RttUs
  )
{
  PROBE_ENTRY  *Entry;

  //
  // Add to ring buffer
  //
  Entry = &Stats->History[Stats->HistoryHead];
  Entry->SeqId  = Stats->NextSeqId;
  Entry->Status = Status;
  Entry->RttUs  = RttUs;

  Stats->HistoryHead = (Stats->HistoryHead + 1) % PROBE_HISTORY_SIZE;
  Stats->NextSeqId++;
  Stats->Sent++;

  if (Status == PROBE_STATUS_PASS) {
    Stats->Received++;

    //
    // Update RTT statistics
    //
    Stats->RttLastUs = RttUs;
    Stats->RttTotalUs += RttUs;
    Stats->RttAvgUs = (UINT32)(Stats->RttTotalUs / Stats->Received);

    if (RttUs < Stats->RttMinUs || Stats->Received == 1) {
      Stats->RttMinUs = RttUs;
    }
    if (RttUs > Stats->RttMaxUs) {
      Stats->RttMaxUs = RttUs;
    }
  } else {
    Stats->Lost++;
  }
}

// ============================================================
// ARP Probe — send ARP Request, expect ARP Reply
// ============================================================

/**
  Execute ARP probe via EFI_ARP_PROTOCOL service binding.
  Creates a child, sends ARP request, polls for reply.

  @param[in]  Nic       NIC info.
  @param[in]  TargetIp  Target IP address.
  @param[out] RttUs     Round-trip time in microseconds.

  @retval EFI_SUCCESS   ARP reply received.
  @retval EFI_TIMEOUT   No reply within timeout.
**/
STATIC
EFI_STATUS
ProbeArpViaProtocol (
  IN  NIC_INFO          *Nic,
  IN  EFI_IPv4_ADDRESS  *TargetIp,
  OUT UINT32            *RttUs
  )
{
  EFI_STATUS                    Status;
  EFI_SERVICE_BINDING_PROTOCOL  *ArpSb;
  EFI_ARP_PROTOCOL              *Arp;
  EFI_HANDLE                    ArpChild;
  EFI_ARP_CONFIG_DATA           ArpConfig;
  EFI_IPv4_ADDRESS              StationAddr;
  EFI_MAC_ADDRESS               ResolvedAddr;
  UINT64                        StartTick;
  UINT64                        EndTick;

  *RttUs   = 0;
  ArpSb    = NULL;
  Arp      = NULL;
  ArpChild = NULL;

  //
  // Open ARP Service Binding
  //
  Status = gBS->OpenProtocol (
                  Nic->Handle,
                  &gEfiArpServiceBindingProtocolGuid,
                  (VOID **)&ArpSb,
                  gImageHandle,
                  Nic->Handle,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );
  if (EFI_ERROR (Status) || ArpSb == NULL) {
    return EFI_UNSUPPORTED;
  }

  //
  // Create ARP child
  //
  Status = ArpSb->CreateChild (ArpSb, &ArpChild);
  if (EFI_ERROR (Status) || ArpChild == NULL) {
    return EFI_UNSUPPORTED;
  }

  Status = gBS->OpenProtocol (
                  ArpChild,
                  &gEfiArpProtocolGuid,
                  (VOID **)&Arp,
                  gImageHandle,
                  Nic->Handle,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );
  if (EFI_ERROR (Status) || Arp == NULL) {
    ArpSb->DestroyChild (ArpSb, ArpChild);
    return EFI_UNSUPPORTED;
  }

  //
  // Configure ARP with our IP
  //
  CopyMem (&StationAddr, &Nic->Ipv4Address, 4);

  ZeroMem (&ArpConfig, sizeof (ArpConfig));
  ArpConfig.SwAddressType   = 0x0800;
  ArpConfig.SwAddressLength = 4;
  ArpConfig.StationAddress  = &StationAddr;
  ArpConfig.EntryTimeOut    = 0;
  ArpConfig.RetryCount      = 3;
  ArpConfig.RetryTimeOut    = 10000000;   // 1 second (100ns units)

  Status = Arp->Configure (Arp, &ArpConfig);
  if (EFI_ERROR (Status)) {
    ArpSb->DestroyChild (ArpSb, ArpChild);
    return EFI_DEVICE_ERROR;
  }

  //
  // Non-blocking ARP request with polling
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
                    ProbeArpNotify,
                    &ArpDone,
                    &ArpEvent
                    );
    if (EFI_ERROR (Status)) {
      Arp->Configure (Arp, NULL);
      ArpSb->DestroyChild (ArpSb, ArpChild);
      return EFI_DEVICE_ERROR;
    }

    ZeroMem (&ResolvedAddr, sizeof (ResolvedAddr));
    StartTick = UtilGetTimestamp ();

    //
    // Delete existing cache entry to force a fresh ARP exchange
    //
    Arp->Delete (Arp, FALSE, (VOID *)TargetIp);

    Status = Arp->Request (Arp, (VOID *)TargetIp, ArpEvent, &ResolvedAddr);

    if (Status == EFI_SUCCESS) {
      //
      // Cache hit
      //
      EndTick = UtilGetTimestamp ();
      *RttUs = (UINT32)((EndTick - StartTick) * 1000000);
    } else if (!EFI_ERROR (Status) || Status == EFI_NOT_READY) {
      //
      // Request queued — poll up to PROBE_TIMEOUT_MS
      //
      for (PollI = 0; PollI < PROBE_TIMEOUT_MS && !ArpDone; PollI++) {
        gBS->Stall (1000);
      }

      if (ArpDone) {
        EndTick = UtilGetTimestamp ();
        *RttUs = (UINT32)((EndTick - StartTick) * 1000000);
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
  Execute ARP probe via raw SNP TX/RX (fallback).

  @param[in]  Nic       NIC info (must have initialized SNP).
  @param[in]  TargetIp  Target IP address (4 bytes).
  @param[out] RttUs     Round-trip time in microseconds.

  @retval EFI_SUCCESS   ARP reply received.
  @retval EFI_TIMEOUT   No reply within timeout.
**/
STATIC
EFI_STATUS
ProbeArpViaSnp (
  IN  NIC_INFO          *Nic,
  IN  EFI_IPv4_ADDRESS  *TargetIp,
  OUT UINT32            *RttUs
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
  UINT64           StartTick;
  UINT64           EndTick;

  *RttUs = 0;

  TxLen = PktBuildArpRequest (
            TxBuf,
            Nic->Snp->Mode->CurrentAddress.Addr,
            (CONST UINT8 *)&Nic->Ipv4Address,
            (CONST UINT8 *)TargetIp
            );

  Nic->Snp->ReceiveFilters (
    Nic->Snp,
    EFI_SIMPLE_NETWORK_RECEIVE_UNICAST |
    EFI_SIMPLE_NETWORK_RECEIVE_BROADCAST,
    0, FALSE, 0, NULL
    );

  StartTick = UtilGetTimestamp ();

  Status = Nic->Snp->Transmit (Nic->Snp, 0, TxLen, TxBuf, NULL, NULL, NULL);
  if (EFI_ERROR (Status)) {
    return EFI_NOT_READY;
  }

  for (I = 0; I < PROBE_TIMEOUT_MS; I++) {
    RxLen   = sizeof (RxBuf);
    HdrSize = 0;
    Status  = Nic->Snp->Receive (Nic->Snp, &HdrSize, &RxLen, RxBuf, NULL, NULL, NULL);

    if (!EFI_ERROR (Status) && RxLen >= ETHERNET_HEADER_SIZE + ARP_HEADER_SIZE) {
      RxEth = (ETHERNET_HEADER *)RxBuf;
      if (NTOHS (RxEth->EtherType) == ETHERTYPE_ARP) {
        RxArp = (ARP_HEADER *)(RxBuf + ETHERNET_HEADER_SIZE);
        if (NTOHS (RxArp->Operation) == ARP_OP_REPLY) {
          EndTick = UtilGetTimestamp ();
          *RttUs = (UINT32)((EndTick - StartTick) * 1000000);
          return EFI_SUCCESS;
        }
      }
    }

    gBS->Stall (1000);
  }

  return EFI_TIMEOUT;
}

// ============================================================
// ICMP Probe — send ICMP Echo Request, expect Echo Reply
// ============================================================

/**
  Execute ICMP probe via EFI_IP4_PROTOCOL.

  @param[in]  Nic       NIC info.
  @param[in]  TargetIp  Target IP address.
  @param[in]  SeqNum    ICMP sequence number.
  @param[out] RttUs     Round-trip time in microseconds.

  @retval EFI_SUCCESS   ICMP echo reply received.
  @retval EFI_TIMEOUT   No reply within timeout.
**/
STATIC
EFI_STATUS
ProbeIcmpViaIp4 (
  IN  NIC_INFO          *Nic,
  IN  EFI_IPv4_ADDRESS  *TargetIp,
  IN  UINT16            SeqNum,
  OUT UINT32            *RttUs
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
  UINT8                         IcmpBuf[ICMP_HEADER_SIZE + PROBE_PAYLOAD_SIZE];
  ICMP_HEADER                   *Icmp;
  UINT64                        StartTick;
  UINTN                         I;
  EFI_STATUS                    Result;

  *RttUs   = 0;
  TxEvent  = NULL;
  RxEvent  = NULL;
  Ip4Sb    = NULL;
  Ip4      = NULL;
  Ip4Child = NULL;

  //
  // Open IP4 Service Binding
  //
  Status = gBS->OpenProtocol (
                  Nic->Handle,
                  &gEfiIp4ServiceBindingProtocolGuid,
                  (VOID **)&Ip4Sb,
                  gImageHandle,
                  Nic->Handle,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );
  if (EFI_ERROR (Status) || Ip4Sb == NULL) {
    return EFI_UNSUPPORTED;
  }

  Status = Ip4Sb->CreateChild (Ip4Sb, &Ip4Child);
  if (EFI_ERROR (Status) || Ip4Child == NULL) {
    return EFI_UNSUPPORTED;
  }

  Status = gBS->OpenProtocol (
                  Ip4Child,
                  &gEfiIp4ProtocolGuid,
                  (VOID **)&Ip4,
                  gImageHandle,
                  Nic->Handle,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );
  if (EFI_ERROR (Status) || Ip4 == NULL) {
    Ip4Sb->DestroyChild (Ip4Sb, Ip4Child);
    return EFI_UNSUPPORTED;
  }

  //
  // Configure IP4 for ICMP
  //
  ZeroMem (&Ip4Config, sizeof (Ip4Config));
  Ip4Config.DefaultProtocol    = 1;     // ICMP
  Ip4Config.AcceptIcmpErrors   = TRUE;
  Ip4Config.UseDefaultAddress  = FALSE;
  CopyMem (&Ip4Config.StationAddress, &Nic->Ipv4Address, 4);
  CopyMem (&Ip4Config.SubnetMask, &Nic->SubnetMask, 4);
  Ip4Config.TimeToLive         = 64;
  Ip4Config.DoNotFragment      = FALSE;
  Ip4Config.RawData            = FALSE;

  Status = Ip4->Configure (Ip4, &Ip4Config);
  if (EFI_ERROR (Status)) {
    //
    // Fallback to default address
    //
    ZeroMem (&Ip4Config, sizeof (Ip4Config));
    Ip4Config.DefaultProtocol    = 1;
    Ip4Config.AcceptIcmpErrors   = TRUE;
    Ip4Config.UseDefaultAddress  = TRUE;
    Ip4Config.TimeToLive         = 64;
    Ip4Config.DoNotFragment      = FALSE;
    Ip4Config.RawData            = FALSE;

    Status = Ip4->Configure (Ip4, &Ip4Config);
    if (EFI_ERROR (Status)) {
      Ip4Sb->DestroyChild (Ip4Sb, Ip4Child);
      return EFI_UNSUPPORTED;
    }
  }

  //
  // Add gateway route if configured
  //
  {
    BOOLEAN HasGw;
    EFI_IPv4_ADDRESS ZeroAddr;
    EFI_IPv4_ADDRESS GwAddr;

    HasGw = FALSE;
    for (I = 0; I < 4; I++) {
      if (Nic->Gateway.Addr[I] != 0) {
        HasGw = TRUE;
        break;
      }
    }
    if (HasGw) {
      ZeroMem (&ZeroAddr, sizeof (ZeroAddr));
      CopyMem (&GwAddr, &Nic->Gateway, 4);
      Ip4->Routes (Ip4, FALSE, &ZeroAddr, &ZeroAddr, &GwAddr);
    }
  }

  Result = EFI_TIMEOUT;

  //
  // Build ICMP echo request (header + payload, no IP/Ethernet)
  //
  ZeroMem (IcmpBuf, sizeof (IcmpBuf));
  Icmp = (ICMP_HEADER *)IcmpBuf;
  Icmp->Type           = ICMP_TYPE_ECHO_REQUEST;
  Icmp->Code           = 0;
  Icmp->Checksum       = 0;
  Icmp->Identifier     = HTONS (PROBE_ICMP_ID);
  Icmp->SequenceNumber = HTONS (SeqNum);

  //
  // Fill payload with probe data
  //
  {
    CHAR8  PayloadStr[PROBE_PAYLOAD_SIZE];
    ProbeBuildPayload (PayloadStr, SeqNum);
    CopyMem (IcmpBuf + ICMP_HEADER_SIZE, PayloadStr, PROBE_PAYLOAD_SIZE);
  }

  Icmp->Checksum = HTONS (PktChecksum (IcmpBuf, ICMP_HEADER_SIZE + PROBE_PAYLOAD_SIZE));

  //
  // Create TX event
  //
  Status = gBS->CreateEvent (
                  EVT_NOTIFY_SIGNAL, TPL_CALLBACK,
                  ProbeNotifyStub, NULL, &TxEvent
                  );
  if (EFI_ERROR (Status)) {
    Result = EFI_DEVICE_ERROR;
    goto IcmpCleanup;
  }

  //
  // Prepare TX data
  //
  ZeroMem (&Override, sizeof (Override));
  CopyMem (&Override.SourceAddress, &Nic->Ipv4Address, 4);
  Override.TypeOfService = 0;
  Override.TimeToLive    = 64;
  Override.DoNotFragment = FALSE;
  Override.Protocol      = 1;  // ICMP

  ZeroMem (&TxData, sizeof (TxData));
  CopyMem (&TxData.DestinationAddress, TargetIp, 4);
  TxData.OverrideData                    = &Override;
  TxData.TotalDataLength                 = ICMP_HEADER_SIZE + PROBE_PAYLOAD_SIZE;
  TxData.FragmentCount                   = 1;
  TxData.FragmentTable[0].FragmentLength = ICMP_HEADER_SIZE + PROBE_PAYLOAD_SIZE;
  TxData.FragmentTable[0].FragmentBuffer = IcmpBuf;

  ZeroMem (&TxToken, sizeof (TxToken));
  TxToken.Event         = TxEvent;
  TxToken.Status        = EFI_NOT_READY;
  TxToken.Packet.TxData = &TxData;

  StartTick = UtilGetTimestamp ();

  //
  // Transmit with retry (ARP may need priming)
  //
  {
    UINTN  Attempt;

    for (Attempt = 0; Attempt < 3; Attempt++) {
      TxToken.Status = EFI_NOT_READY;

      Status = Ip4->Transmit (Ip4, &TxToken);
      if (EFI_ERROR (Status)) {
        gBS->Stall (500000);
        continue;
      }

      for (I = 0; I < 4000 && TxToken.Status == EFI_NOT_READY; I++) {
        Ip4->Poll (Ip4);
        gBS->Stall (1000);
      }

      if (!EFI_ERROR (TxToken.Status)) {
        break;
      }

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
    goto IcmpCleanup;
  }

  //
  // Create RX event
  //
  Status = gBS->CreateEvent (
                  EVT_NOTIFY_SIGNAL, TPL_CALLBACK,
                  ProbeNotifyStub, NULL, &RxEvent
                  );
  if (EFI_ERROR (Status)) {
    Result = EFI_DEVICE_ERROR;
    goto IcmpCleanup;
  }

  //
  // Receive reply
  //
  ZeroMem (&RxToken, sizeof (RxToken));
  RxToken.Event  = RxEvent;
  RxToken.Status = EFI_NOT_READY;

  Status = Ip4->Receive (Ip4, &RxToken);
  if (EFI_ERROR (Status)) {
    Result = EFI_NOT_READY;
    goto IcmpCleanup;
  }

  for (I = 0; I < PROBE_TIMEOUT_MS && RxToken.Status == EFI_NOT_READY; I++) {
    Ip4->Poll (Ip4);
    gBS->Stall (1000);
  }

  if (RxToken.Status == EFI_NOT_READY) {
    Ip4->Cancel (Ip4, &RxToken);
    Ip4->Poll (Ip4);
    Result = EFI_TIMEOUT;
    goto IcmpCleanup;
  }

  if (EFI_ERROR (RxToken.Status)) {
    Result = EFI_DEVICE_ERROR;
    goto IcmpCleanup;
  }

  //
  // Process ICMP reply
  //
  if (RxToken.Packet.RxData != NULL) {
    EFI_IP4_RECEIVE_DATA  *RxData;
    UINT64                 EndTick;

    RxData  = RxToken.Packet.RxData;
    EndTick = UtilGetTimestamp ();
    *RttUs  = (UINT32)((EndTick - StartTick) * 1000000);

    if (RxData->DataLength >= ICMP_HEADER_SIZE &&
        RxData->FragmentCount > 0 &&
        RxData->FragmentTable[0].FragmentLength >= ICMP_HEADER_SIZE) {
      ICMP_HEADER  *RxIcmp;

      RxIcmp = (ICMP_HEADER *)RxData->FragmentTable[0].FragmentBuffer;
      if (RxIcmp->Type == ICMP_TYPE_ECHO_REPLY) {
        Result = EFI_SUCCESS;
      } else {
        Result = EFI_DEVICE_ERROR;
      }
    } else {
      Result = EFI_DEVICE_ERROR;
    }

    gBS->SignalEvent (RxData->RecycleSignal);
  }

IcmpCleanup:
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

// ============================================================
// UDP Probe — send echo payload, expect verbatim echo back
// ============================================================

/**
  Execute UDP probe.
  Creates a UDP4 child, sends payload, waits for echo reply.

  @param[in]  Nic       NIC info.
  @param[in]  TargetIp  Target IP address.
  @param[in]  SeqId     Sequence ID for payload.
  @param[out] RttUs     Round-trip time in microseconds.

  @retval EFI_SUCCESS   Echo reply received and validated.
  @retval EFI_TIMEOUT   No reply within timeout.
**/
STATIC
EFI_STATUS
ProbeUdpEcho (
  IN  NIC_INFO          *Nic,
  IN  EFI_IPv4_ADDRESS  *TargetIp,
  IN  UINT32            SeqId,
  OUT UINT32            *RttUs
  )
{
  EFI_STATUS                    Status;
  EFI_SERVICE_BINDING_PROTOCOL  *UdpSb;
  EFI_HANDLE                    ChildHandle;
  EFI_UDP4_PROTOCOL             *Udp4;
  EFI_UDP4_CONFIG_DATA          UdpConfig;
  EFI_UDP4_COMPLETION_TOKEN     TxToken;
  EFI_UDP4_TRANSMIT_DATA        TxData;
  EFI_UDP4_COMPLETION_TOKEN     RxToken;
  CHAR8                         SendPayload[PROBE_PAYLOAD_SIZE];
  UINT64                        StartTick;
  UINT64                        EndTick;

  *RttUs      = 0;
  ChildHandle = NULL;
  Udp4        = NULL;

  ProbeBuildPayload (SendPayload, SeqId);

  //
  // Create UDP4 child
  //
  Status = gBS->HandleProtocol (
                  Nic->Handle,
                  &gEfiUdp4ServiceBindingProtocolGuid,
                  (VOID **)&UdpSb
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = UdpSb->CreateChild (UdpSb, &ChildHandle);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->HandleProtocol (
                  ChildHandle,
                  &gEfiUdp4ProtocolGuid,
                  (VOID **)&Udp4
                  );
  if (EFI_ERROR (Status)) {
    UdpSb->DestroyChild (UdpSb, ChildHandle);
    return Status;
  }

  //
  // Configure UDP4
  //
  ZeroMem (&UdpConfig, sizeof (UdpConfig));
  UdpConfig.AcceptBroadcast    = FALSE;
  UdpConfig.AcceptPromiscuous  = FALSE;
  UdpConfig.AcceptAnyPort      = FALSE;
  UdpConfig.AllowDuplicatePort = TRUE;
  UdpConfig.TimeToLive         = 64;
  UdpConfig.DoNotFragment      = FALSE;
  UdpConfig.UseDefaultAddress  = FALSE;

  CopyMem (&UdpConfig.StationAddress, &Nic->Ipv4Address, sizeof (EFI_IPv4_ADDRESS));
  CopyMem (&UdpConfig.SubnetMask, &Nic->SubnetMask, sizeof (EFI_IPv4_ADDRESS));
  UdpConfig.StationPort = PROBE_UDP_PORT + 1;   // local port
  CopyMem (&UdpConfig.RemoteAddress, TargetIp, sizeof (EFI_IPv4_ADDRESS));
  UdpConfig.RemotePort = PROBE_UDP_PORT;

  Status = Udp4->Configure (Udp4, &UdpConfig);
  if (EFI_ERROR (Status)) {
    UdpSb->DestroyChild (UdpSb, ChildHandle);
    return Status;
  }

  //
  // Send
  //
  ZeroMem (&TxData, sizeof (TxData));
  TxData.UdpSessionData                = NULL;
  TxData.GatewayAddress                = NULL;
  TxData.DataLength                    = PROBE_PAYLOAD_SIZE;
  TxData.FragmentCount                 = 1;
  TxData.FragmentTable[0].FragmentLength = PROBE_PAYLOAD_SIZE;
  TxData.FragmentTable[0].FragmentBuffer = (VOID *)SendPayload;

  ZeroMem (&TxToken, sizeof (TxToken));
  Status = gBS->CreateEvent (
                  EVT_NOTIFY_SIGNAL, TPL_CALLBACK,
                  ProbeNotifyStub, NULL, &TxToken.Event
                  );
  if (EFI_ERROR (Status)) {
    Udp4->Configure (Udp4, NULL);
    UdpSb->DestroyChild (UdpSb, ChildHandle);
    return Status;
  }

  TxToken.Status        = EFI_NOT_READY;
  TxToken.Packet.TxData = &TxData;

  StartTick = UtilGetTimestamp ();

  Status = Udp4->Transmit (Udp4, &TxToken);
  if (EFI_ERROR (Status)) {
    gBS->CloseEvent (TxToken.Event);
    Udp4->Configure (Udp4, NULL);
    UdpSb->DestroyChild (UdpSb, ChildHandle);
    return Status;
  }

  //
  // Wait for TX completion
  //
  {
    EFI_EVENT  TimerEvent;

    TimerEvent = NULL;
    gBS->CreateEvent (EVT_TIMER, 0, NULL, NULL, &TimerEvent);
    if (TimerEvent != NULL) {
      gBS->SetTimer (TimerEvent, TimerRelative, (UINT64)PROBE_TIMEOUT_MS * 10000);
    }

    while (TxToken.Status == EFI_NOT_READY) {
      Udp4->Poll (Udp4);
      if (TimerEvent != NULL && gBS->CheckEvent (TimerEvent) == EFI_SUCCESS) {
        break;
      }
      gBS->Stall (1000);
    }

    if (TimerEvent != NULL) {
      gBS->CloseEvent (TimerEvent);
    }
  }

  if (TxToken.Status == EFI_NOT_READY) {
    Udp4->Cancel (Udp4, &TxToken);
    gBS->CloseEvent (TxToken.Event);
    Udp4->Configure (Udp4, NULL);
    UdpSb->DestroyChild (UdpSb, ChildHandle);
    return EFI_TIMEOUT;
  }

  Status = TxToken.Status;
  gBS->CloseEvent (TxToken.Event);

  if (EFI_ERROR (Status)) {
    Udp4->Configure (Udp4, NULL);
    UdpSb->DestroyChild (UdpSb, ChildHandle);
    return Status;
  }

  //
  // Receive reply
  //
  ZeroMem (&RxToken, sizeof (RxToken));
  Status = gBS->CreateEvent (
                  EVT_NOTIFY_SIGNAL, TPL_CALLBACK,
                  ProbeNotifyStub, NULL, &RxToken.Event
                  );
  if (EFI_ERROR (Status)) {
    Udp4->Configure (Udp4, NULL);
    UdpSb->DestroyChild (UdpSb, ChildHandle);
    return Status;
  }

  RxToken.Status        = EFI_NOT_READY;
  RxToken.Packet.RxData = NULL;

  Status = Udp4->Receive (Udp4, &RxToken);
  if (EFI_ERROR (Status)) {
    gBS->CloseEvent (RxToken.Event);
    Udp4->Configure (Udp4, NULL);
    UdpSb->DestroyChild (UdpSb, ChildHandle);
    return Status;
  }

  {
    EFI_EVENT  TimerEvent;

    TimerEvent = NULL;
    gBS->CreateEvent (EVT_TIMER, 0, NULL, NULL, &TimerEvent);
    if (TimerEvent != NULL) {
      gBS->SetTimer (TimerEvent, TimerRelative, (UINT64)PROBE_TIMEOUT_MS * 10000);
    }

    while (RxToken.Status == EFI_NOT_READY) {
      Udp4->Poll (Udp4);
      if (TimerEvent != NULL && gBS->CheckEvent (TimerEvent) == EFI_SUCCESS) {
        break;
      }
      gBS->Stall (1000);
    }

    if (TimerEvent != NULL) {
      gBS->CloseEvent (TimerEvent);
    }
  }

  if (RxToken.Status == EFI_NOT_READY) {
    Udp4->Cancel (Udp4, &RxToken);
    gBS->CloseEvent (RxToken.Event);
    Udp4->Configure (Udp4, NULL);
    UdpSb->DestroyChild (UdpSb, ChildHandle);
    return EFI_TIMEOUT;
  }

  Status = RxToken.Status;
  if (!EFI_ERROR (Status) && RxToken.Packet.RxData != NULL) {
    //
    // Validate echo payload matches what we sent
    //
    UINT32  Offset;
    UINT32  CopyLen;
    UINT32  FragI;
    CHAR8   RecvBuf[PROBE_PAYLOAD_SIZE + 1];

    Offset = 0;
    ZeroMem (RecvBuf, sizeof (RecvBuf));
    for (FragI = 0; FragI < RxToken.Packet.RxData->FragmentCount; FragI++) {
      CopyLen = RxToken.Packet.RxData->FragmentTable[FragI].FragmentLength;
      if (Offset + CopyLen > PROBE_PAYLOAD_SIZE) {
        CopyLen = PROBE_PAYLOAD_SIZE - Offset;
      }
      if (CopyLen > 0) {
        CopyMem (
          RecvBuf + Offset,
          RxToken.Packet.RxData->FragmentTable[FragI].FragmentBuffer,
          CopyLen
          );
        Offset += CopyLen;
      }
    }

    EndTick = UtilGetTimestamp ();
    *RttUs = (UINT32)((EndTick - StartTick) * 1000000);

    //
    // Check that echo matches (first 7 bytes = "DDTECHO")
    //
    if (Offset >= 7 &&
        RecvBuf[0] == 'D' && RecvBuf[1] == 'D' && RecvBuf[2] == 'T' &&
        RecvBuf[3] == 'E' && RecvBuf[4] == 'C' && RecvBuf[5] == 'H' &&
        RecvBuf[6] == 'O') {
      Status = EFI_SUCCESS;
    } else {
      Status = EFI_DEVICE_ERROR;
    }

    gBS->SignalEvent (RxToken.Packet.RxData->RecycleSignal);
  }

  gBS->CloseEvent (RxToken.Event);
  Udp4->Configure (Udp4, NULL);
  UdpSb->DestroyChild (UdpSb, ChildHandle);
  return Status;
}

// ============================================================
// TCP Probe — connect, send payload, receive echo, close
// ============================================================

/**
  Execute TCP probe.
  Creates TCP4 child, connects, sends payload, receives echo, closes.

  @param[in]  Nic       NIC info.
  @param[in]  TargetIp  Target IP address.
  @param[in]  SeqId     Sequence ID for payload.
  @param[out] RttUs     Round-trip time in microseconds.

  @retval EFI_SUCCESS   Echo reply received.
  @retval EFI_TIMEOUT   Connection or data transfer timed out.
**/
STATIC
EFI_STATUS
ProbeTcpEcho (
  IN  NIC_INFO          *Nic,
  IN  EFI_IPv4_ADDRESS  *TargetIp,
  IN  UINT32            SeqId,
  OUT UINT32            *RttUs
  )
{
  EFI_STATUS                    Status;
  EFI_SERVICE_BINDING_PROTOCOL  *TcpSb;
  EFI_HANDLE                    ChildHandle;
  EFI_TCP4_PROTOCOL             *Tcp4;
  EFI_TCP4_CONFIG_DATA          TcpConfig;
  EFI_TCP4_CONNECTION_TOKEN     ConnToken;
  EFI_TCP4_IO_TOKEN             TxToken;
  EFI_TCP4_TRANSMIT_DATA        TxData;
  EFI_TCP4_IO_TOKEN             RxToken;
  EFI_TCP4_RECEIVE_DATA         RxData;
  EFI_TCP4_CLOSE_TOKEN          CloseToken;
  CHAR8                         SendPayload[PROBE_PAYLOAD_SIZE];
  CHAR8                         RecvBuf[PROBE_PAYLOAD_SIZE + 1];
  UINT64                        StartTick;
  UINT64                        EndTick;
  UINTN                         I;
  EFI_STATUS                    Result;

  *RttUs      = 0;
  ChildHandle = NULL;
  Tcp4        = NULL;
  Result      = EFI_TIMEOUT;

  ProbeBuildPayload (SendPayload, SeqId);

  //
  // Create TCP4 child
  //
  TcpSb = NULL;
  Status = gBS->HandleProtocol (
                  Nic->Handle,
                  &gEfiTcp4ServiceBindingProtocolGuid,
                  (VOID **)&TcpSb
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = TcpSb->CreateChild (TcpSb, &ChildHandle);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->HandleProtocol (
                  ChildHandle,
                  &gEfiTcp4ProtocolGuid,
                  (VOID **)&Tcp4
                  );
  if (EFI_ERROR (Status)) {
    TcpSb->DestroyChild (TcpSb, ChildHandle);
    return Status;
  }

  //
  // Configure TCP4 for active connect
  //
  ZeroMem (&TcpConfig, sizeof (TcpConfig));
  TcpConfig.TypeOfService                = 0;
  TcpConfig.TimeToLive                   = 64;
  TcpConfig.AccessPoint.UseDefaultAddress = FALSE;
  CopyMem (&TcpConfig.AccessPoint.StationAddress, &Nic->Ipv4Address, sizeof (EFI_IPv4_ADDRESS));
  CopyMem (&TcpConfig.AccessPoint.SubnetMask, &Nic->SubnetMask, sizeof (EFI_IPv4_ADDRESS));
  TcpConfig.AccessPoint.StationPort      = 0;   // Ephemeral
  CopyMem (&TcpConfig.AccessPoint.RemoteAddress, TargetIp, sizeof (EFI_IPv4_ADDRESS));
  TcpConfig.AccessPoint.RemotePort       = PROBE_TCP_PORT;
  TcpConfig.AccessPoint.ActiveFlag       = TRUE;
  TcpConfig.ControlOption                = NULL;

  Status = Tcp4->Configure (Tcp4, &TcpConfig);
  if (EFI_ERROR (Status)) {
    Result = Status;
    goto TcpCleanup;
  }

  //
  // Connect
  //
  ZeroMem (&ConnToken, sizeof (ConnToken));
  Status = gBS->CreateEvent (
                  EVT_NOTIFY_SIGNAL, TPL_CALLBACK,
                  ProbeNotifyStub, NULL, &ConnToken.CompletionToken.Event
                  );
  if (EFI_ERROR (Status)) {
    Result = Status;
    goto TcpCleanup;
  }

  ConnToken.CompletionToken.Status = EFI_NOT_READY;

  StartTick = UtilGetTimestamp ();

  Status = Tcp4->Connect (Tcp4, &ConnToken);
  if (EFI_ERROR (Status)) {
    gBS->CloseEvent (ConnToken.CompletionToken.Event);
    Result = Status;
    goto TcpCleanup;
  }

  {
    EFI_EVENT  TimerEvent;

    TimerEvent = NULL;
    gBS->CreateEvent (EVT_TIMER, 0, NULL, NULL, &TimerEvent);
    if (TimerEvent != NULL) {
      gBS->SetTimer (TimerEvent, TimerRelative, (UINT64)PROBE_TIMEOUT_MS * 10000);
    }

    while (ConnToken.CompletionToken.Status == EFI_NOT_READY) {
      Tcp4->Poll (Tcp4);
      if (TimerEvent != NULL && gBS->CheckEvent (TimerEvent) == EFI_SUCCESS) {
        break;
      }
      gBS->Stall (1000);
    }

    if (TimerEvent != NULL) {
      gBS->CloseEvent (TimerEvent);
    }
  }

  if (ConnToken.CompletionToken.Status == EFI_NOT_READY) {
    Tcp4->Cancel (Tcp4, &ConnToken.CompletionToken);
    Tcp4->Poll (Tcp4);
    gBS->CloseEvent (ConnToken.CompletionToken.Event);
    Result = EFI_TIMEOUT;
    goto TcpCleanup;
  }

  Status = ConnToken.CompletionToken.Status;
  gBS->CloseEvent (ConnToken.CompletionToken.Event);

  if (EFI_ERROR (Status)) {
    Result = Status;
    goto TcpCleanup;
  }

  //
  // Send payload
  //
  ZeroMem (&TxData, sizeof (TxData));
  TxData.Push                         = TRUE;
  TxData.Urgent                       = FALSE;
  TxData.DataLength                   = PROBE_PAYLOAD_SIZE;
  TxData.FragmentCount                = 1;
  TxData.FragmentTable[0].FragmentLength = PROBE_PAYLOAD_SIZE;
  TxData.FragmentTable[0].FragmentBuffer = (VOID *)SendPayload;

  ZeroMem (&TxToken, sizeof (TxToken));
  Status = gBS->CreateEvent (
                  EVT_NOTIFY_SIGNAL, TPL_CALLBACK,
                  ProbeNotifyStub, NULL, &TxToken.CompletionToken.Event
                  );
  if (EFI_ERROR (Status)) {
    Result = Status;
    goto TcpClose;
  }

  TxToken.CompletionToken.Status = EFI_NOT_READY;
  TxToken.Packet.TxData          = &TxData;

  Status = Tcp4->Transmit (Tcp4, &TxToken);
  if (EFI_ERROR (Status)) {
    gBS->CloseEvent (TxToken.CompletionToken.Event);
    Result = Status;
    goto TcpClose;
  }

  {
    EFI_EVENT  TimerEvent;

    TimerEvent = NULL;
    gBS->CreateEvent (EVT_TIMER, 0, NULL, NULL, &TimerEvent);
    if (TimerEvent != NULL) {
      gBS->SetTimer (TimerEvent, TimerRelative, (UINT64)PROBE_TIMEOUT_MS * 10000);
    }

    while (TxToken.CompletionToken.Status == EFI_NOT_READY) {
      Tcp4->Poll (Tcp4);
      if (TimerEvent != NULL && gBS->CheckEvent (TimerEvent) == EFI_SUCCESS) {
        break;
      }
      gBS->Stall (1000);
    }

    if (TimerEvent != NULL) {
      gBS->CloseEvent (TimerEvent);
    }
  }

  if (TxToken.CompletionToken.Status == EFI_NOT_READY) {
    Tcp4->Cancel (Tcp4, &TxToken.CompletionToken);
    Tcp4->Poll (Tcp4);
  }

  Status = TxToken.CompletionToken.Status;
  gBS->CloseEvent (TxToken.CompletionToken.Event);

  if (EFI_ERROR (Status)) {
    Result = Status;
    goto TcpClose;
  }

  //
  // Receive echo
  //
  ZeroMem (RecvBuf, sizeof (RecvBuf));

  ZeroMem (&RxData, sizeof (RxData));
  RxData.UrgentFlag                    = FALSE;
  RxData.DataLength                    = PROBE_PAYLOAD_SIZE;
  RxData.FragmentCount                 = 1;
  RxData.FragmentTable[0].FragmentLength = PROBE_PAYLOAD_SIZE;
  RxData.FragmentTable[0].FragmentBuffer = RecvBuf;

  ZeroMem (&RxToken, sizeof (RxToken));
  Status = gBS->CreateEvent (
                  EVT_NOTIFY_SIGNAL, TPL_CALLBACK,
                  ProbeNotifyStub, NULL, &RxToken.CompletionToken.Event
                  );
  if (EFI_ERROR (Status)) {
    Result = Status;
    goto TcpClose;
  }

  RxToken.CompletionToken.Status = EFI_NOT_READY;
  RxToken.Packet.RxData          = &RxData;

  Status = Tcp4->Receive (Tcp4, &RxToken);
  if (EFI_ERROR (Status)) {
    gBS->CloseEvent (RxToken.CompletionToken.Event);
    Result = Status;
    goto TcpClose;
  }

  {
    EFI_EVENT  TimerEvent;

    TimerEvent = NULL;
    gBS->CreateEvent (EVT_TIMER, 0, NULL, NULL, &TimerEvent);
    if (TimerEvent != NULL) {
      gBS->SetTimer (TimerEvent, TimerRelative, (UINT64)PROBE_TIMEOUT_MS * 10000);
    }

    while (RxToken.CompletionToken.Status == EFI_NOT_READY) {
      Tcp4->Poll (Tcp4);
      if (TimerEvent != NULL && gBS->CheckEvent (TimerEvent) == EFI_SUCCESS) {
        break;
      }
      gBS->Stall (1000);
    }

    if (TimerEvent != NULL) {
      gBS->CloseEvent (TimerEvent);
    }
  }

  if (RxToken.CompletionToken.Status == EFI_NOT_READY) {
    Tcp4->Cancel (Tcp4, &RxToken.CompletionToken);
    Tcp4->Poll (Tcp4);
  }

  Status = RxToken.CompletionToken.Status;
  gBS->CloseEvent (RxToken.CompletionToken.Event);

  if (!EFI_ERROR (Status) && RxToken.Packet.RxData != NULL) {
    EndTick = UtilGetTimestamp ();
    *RttUs = (UINT32)((EndTick - StartTick) * 1000000);

    //
    // Validate echo payload
    //
    if (RecvBuf[0] == 'D' && RecvBuf[1] == 'D' && RecvBuf[2] == 'T' &&
        RecvBuf[3] == 'E' && RecvBuf[4] == 'C' && RecvBuf[5] == 'H' &&
        RecvBuf[6] == 'O') {
      Result = EFI_SUCCESS;
    } else {
      Result = EFI_DEVICE_ERROR;
    }
  } else {
    Result = EFI_ERROR (Status) ? Status : EFI_TIMEOUT;
  }

TcpClose:
  //
  // Graceful close
  //
  ZeroMem (&CloseToken, sizeof (CloseToken));
  CloseToken.AbortOnClose = FALSE;
  if (!EFI_ERROR (gBS->CreateEvent (
        EVT_NOTIFY_SIGNAL, TPL_CALLBACK,
        ProbeNotifyStub, NULL, &CloseToken.CompletionToken.Event))) {
    CloseToken.CompletionToken.Status = EFI_NOT_READY;
    if (!EFI_ERROR (Tcp4->Close (Tcp4, &CloseToken))) {
      for (I = 0; I < 2000 && CloseToken.CompletionToken.Status == EFI_NOT_READY; I++) {
        Tcp4->Poll (Tcp4);
        gBS->Stall (1000);
      }
      if (CloseToken.CompletionToken.Status == EFI_NOT_READY) {
        Tcp4->Cancel (Tcp4, &CloseToken.CompletionToken);
        Tcp4->Poll (Tcp4);
      }
    }
    gBS->CloseEvent (CloseToken.CompletionToken.Event);
  }

TcpCleanup:
  if (Tcp4 != NULL) {
    Tcp4->Configure (Tcp4, NULL);
  }
  if (ChildHandle != NULL && TcpSb != NULL) {
    TcpSb->DestroyChild (TcpSb, ChildHandle);
  }

  return Result;
}

// ============================================================
// Public API
// ============================================================

/**
  Initialize probe stats for a given protocol.
**/
VOID
ProbeInit (
  OUT PROBE_STATS     *Stats,
  IN  PROBE_PROTOCOL  Protocol
  )
{
  ZeroMem (Stats, sizeof (PROBE_STATS));
  Stats->Protocol  = Protocol;
  Stats->RttMinUs  = (UINT32)-1;  // Max value so first probe sets it
  Stats->NextSeqId = 1;
}

/**
  Execute a single probe round-trip.
  Dispatches to the appropriate protocol handler.
**/
EFI_STATUS
ProbeExecuteOnce (
  IN     NIC_INFO          *Nic,
  IN     EFI_IPv4_ADDRESS  *TargetIp,
  IN OUT PROBE_STATS       *Stats
  )
{
  EFI_STATUS  Status;
  UINT32      RttUs;

  RttUs  = 0;
  Status = EFI_UNSUPPORTED;

  switch (Stats->Protocol) {
    case ProbeArp:
      //
      // ARP: try protocol first, then raw SNP
      //
      if (Nic->HasArp) {
        Status = ProbeArpViaProtocol (Nic, TargetIp, &RttUs);
      }
      if (EFI_ERROR (Status) && Nic->Snp != NULL &&
          Nic->Snp->Mode->State == EfiSimpleNetworkInitialized) {
        Status = ProbeArpViaSnp (Nic, TargetIp, &RttUs);
      }
      break;

    case ProbeIcmp:
      //
      // ICMP: via IP4 protocol
      //
      if (Nic->HasIp4) {
        Status = ProbeIcmpViaIp4 (Nic, TargetIp, (UINT16)Stats->NextSeqId, &RttUs);
      }
      break;

    case ProbeUdp:
      //
      // UDP: via UDP4 protocol (port 5000)
      //
      if (Nic->HasUdp4) {
        Status = ProbeUdpEcho (Nic, TargetIp, Stats->NextSeqId, &RttUs);
      }
      break;

    case ProbeTcp:
      //
      // TCP: connect + send + receive + close (port 22)
      //
      if (Nic->HasTcp4) {
        Status = ProbeTcpEcho (Nic, TargetIp, Stats->NextSeqId, &RttUs);
      }
      break;

    default:
      return EFI_UNSUPPORTED;
  }

  //
  // Record result
  //
  if (!EFI_ERROR (Status)) {
    ProbeRecordResult (Stats, PROBE_STATUS_PASS, RttUs);
  } else if (Status == EFI_TIMEOUT) {
    ProbeRecordResult (Stats, PROBE_STATUS_TIMEOUT, 0);
  } else {
    ProbeRecordResult (Stats, PROBE_STATUS_FAIL, 0);
  }

  return EFI_SUCCESS;
}

/**
  Get human-readable protocol name.
**/
CONST CHAR16 *
ProbeGetName (
  IN PROBE_PROTOCOL  Protocol
  )
{
  switch (Protocol) {
    case ProbeArp:   return L"ARP";
    case ProbeIcmp:  return L"ICMP";
    case ProbeUdp:   return L"UDP";
    case ProbeTcp:   return L"TCP";
    default:         return L"Unknown";
  }
}

/**
  Check if a NIC supports the given probe protocol.
**/
BOOLEAN
ProbeIsAvailable (
  IN NIC_INFO        *Nic,
  IN PROBE_PROTOCOL  Protocol
  )
{
  switch (Protocol) {
    case ProbeArp:
      return (BOOLEAN)(Nic->HasArp ||
              (Nic->Snp != NULL && Nic->Snp->Mode->State == EfiSimpleNetworkInitialized));
    case ProbeIcmp:
      return Nic->HasIp4;
    case ProbeUdp:
      return Nic->HasUdp4;
    case ProbeTcp:
      return Nic->HasTcp4;
    default:
      return FALSE;
  }
}
