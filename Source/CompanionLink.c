/** @file
  Companion link communication (UDP control channel).
  Implements handshake protocol and command/response messaging
  between DDTSoft EFI app and the DDTSoft Test Companion.

  Protocol: text-based messages on UDP port 9999.
  Commands (EFI -> Companion): HELLO, PREPARE, START, STOP, RESULT, DONE, GETREPORT
  Responses (Companion -> EFI): ACK, READY, ERROR, REPORT, CONFIRM
**/

#include <DDTSoftNetTest.h>
#include <Protocol/ServiceBinding.h>
#include <Protocol/ManagedNetwork.h>

//
// Notify stub for UDP4 completion tokens
//
STATIC
VOID
EFIAPI
UdpNotifyStub (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  // No-op: we poll Token.Status directly
}

/**
  Initialize the companion link by creating a UDP4 child instance.
  Creates a child via UDP4 service binding and configures it with
  the specified local/remote IP and control channel port.

  @param[in,out]  Link         Companion link context to initialize.
  @param[in]      NicHandle    Handle of the NIC to use.
  @param[in]      LocalIp      Local IPv4 address.
  @param[in]      CompanionIp  Companion IPv4 address.
  @param[in]      SubnetMask   Subnet mask (optional, defaults to 255.255.255.0).

  @retval EFI_SUCCESS           Link initialized successfully.
  @retval EFI_INVALID_PARAMETER Link, LocalIp, or CompanionIp is NULL.
  @retval other                 UDP4 setup failure.
**/
EFI_STATUS
CompanionInit (
  IN OUT COMPANION_LINK    *Link,
  IN     EFI_HANDLE        NicHandle,
  IN     EFI_IPv4_ADDRESS  *LocalIp,
  IN     EFI_IPv4_ADDRESS  *CompanionIp,
  IN     EFI_IPv4_ADDRESS  *SubnetMask  OPTIONAL
  )
{
  EFI_STATUS                    Status;
  EFI_SERVICE_BINDING_PROTOCOL  *Udp4Sb;
  EFI_UDP4_CONFIG_DATA          UdpConfig;
  EFI_IP4_CONFIG2_PROTOCOL      *Ip4Cfg2;
  EFI_IP4_CONFIG2_POLICY        Policy;
  EFI_IP4_CONFIG2_MANUAL_ADDRESS ManualAddr;
  UINTN                         DataSize;

  if (Link == NULL || LocalIp == NULL || CompanionIp == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  ZeroMem (Link, sizeof (COMPANION_LINK));
  Link->State     = COMPANION_DISCONNECTED;
  Link->NicHandle = NicHandle;
  Link->Port      = CONTROL_CHANNEL_PORT;
  Link->TimeoutMs = COMPANION_DEFAULT_TIMEOUT;
  Link->MessageId = 0;

  CopyMem (&Link->LocalIp, LocalIp, sizeof (EFI_IPv4_ADDRESS));
  CopyMem (&Link->CompanionIp, CompanionIp, sizeof (EFI_IPv4_ADDRESS));

  if (SubnetMask != NULL) {
    CopyMem (&Link->SubnetMask, SubnetMask, sizeof (EFI_IPv4_ADDRESS));
  } else {
    Link->SubnetMask.Addr[0] = 255;
    Link->SubnetMask.Addr[1] = 255;
    Link->SubnetMask.Addr[2] = 255;
    Link->SubnetMask.Addr[3] = 0;
  }

  //
  // Step 1: Configure IP4 via IP4Config2 with our static address.
  // This sets the NIC's DEFAULT IP4 instance to our address,
  // so UseDefaultAddress=TRUE in UDP4 will use it.
  // This is more reliable than UseDefaultAddress=FALSE because
  // the default IP4 instance has proper ARP integration.
  //
  Status = gBS->HandleProtocol (
                  NicHandle,
                  &gEfiIp4Config2ProtocolGuid,
                  (VOID **)&Ip4Cfg2
                  );
  if (EFI_ERROR (Status)) {
    UtilSafeStrCpy (Link->StatusMsg, L"IP4Config2 not found on NIC", 128);
    Link->State = COMPANION_ERROR;
    return Status;
  }

  //
  // Set policy to static (overrides DHCP if active)
  //
  Policy   = Ip4Config2PolicyStatic;
  DataSize = sizeof (EFI_IP4_CONFIG2_POLICY);
  Status = Ip4Cfg2->SetData (
                      Ip4Cfg2,
                      Ip4Config2DataTypePolicy,
                      DataSize,
                      &Policy
                      );
  if (EFI_ERROR (Status)) {
    UnicodeSPrint (
      Link->StatusMsg, sizeof (Link->StatusMsg),
      L"IP4Config2 set policy failed (%r)", Status
      );
    Link->State = COMPANION_ERROR;
    return Status;
  }

  gBS->Stall (100000);  // 100ms for policy change

  //
  // Set our static IP address
  //
  CopyMem (&ManualAddr.Address, &Link->LocalIp, sizeof (EFI_IPv4_ADDRESS));
  CopyMem (&ManualAddr.SubnetMask, &Link->SubnetMask, sizeof (EFI_IPv4_ADDRESS));
  DataSize = sizeof (EFI_IP4_CONFIG2_MANUAL_ADDRESS);
  Status = Ip4Cfg2->SetData (
                      Ip4Cfg2,
                      Ip4Config2DataTypeManualAddress,
                      DataSize,
                      &ManualAddr
                      );
  if (EFI_ERROR (Status)) {
    UnicodeSPrint (
      Link->StatusMsg, sizeof (Link->StatusMsg),
      L"IP4Config2 set address failed (%r)", Status
      );
    Link->State = COMPANION_ERROR;
    return Status;
  }

  //
  // Allow IP stack to settle — ARP tables, routes
  //
  gBS->Stall (500000);  // 500ms

  //
  // Step 2: Open UDP4 Service Binding
  //
  Status = gBS->HandleProtocol (
                  NicHandle,
                  &gEfiUdp4ServiceBindingProtocolGuid,
                  (VOID **)&Udp4Sb
                  );
  if (EFI_ERROR (Status)) {
    UtilSafeStrCpy (Link->StatusMsg, L"UDP4 service binding not found", 128);
    Link->State = COMPANION_ERROR;
    return Status;
  }

  //
  // Create UDP4 child instance
  //
  Link->Udp4ChildHandle = NULL;
  Status = Udp4Sb->CreateChild (Udp4Sb, &Link->Udp4ChildHandle);
  if (EFI_ERROR (Status)) {
    UtilSafeStrCpy (Link->StatusMsg, L"Failed to create UDP4 child", 128);
    Link->State = COMPANION_ERROR;
    return Status;
  }

  //
  // Get UDP4 protocol from child handle
  //
  Status = gBS->HandleProtocol (
                  Link->Udp4ChildHandle,
                  &gEfiUdp4ProtocolGuid,
                  (VOID **)&Link->Udp4
                  );
  if (EFI_ERROR (Status)) {
    UtilSafeStrCpy (Link->StatusMsg, L"Failed to get UDP4 protocol", 128);
    Link->State = COMPANION_ERROR;
    Udp4Sb->DestroyChild (Udp4Sb, Link->Udp4ChildHandle);
    Link->Udp4ChildHandle = NULL;
    return Status;
  }

  //
  // Step 3: Configure UDP4 with UseDefaultAddress=TRUE.
  // The default address is now our static IP (set via IP4Config2 above).
  // This is more reliable than UseDefaultAddress=FALSE because the
  // default IP4 instance has full ARP/route integration.
  //
  ZeroMem (&UdpConfig, sizeof (UdpConfig));
  UdpConfig.AcceptBroadcast    = TRUE;
  UdpConfig.AcceptPromiscuous  = FALSE;
  UdpConfig.AcceptAnyPort      = FALSE;
  UdpConfig.AllowDuplicatePort = FALSE;
  UdpConfig.TimeToLive         = 64;
  UdpConfig.TypeOfService      = 0;
  UdpConfig.DoNotFragment      = FALSE;
  UdpConfig.ReceiveTimeout     = 0;
  UdpConfig.TransmitTimeout    = 0;

  //
  // UseDefaultAddress=TRUE: IP4 stack's configured address is used.
  // StationAddress/SubnetMask fields are ignored.
  //
  UdpConfig.UseDefaultAddress  = TRUE;
  UdpConfig.StationPort = Link->Port;

  //
  // Wildcard remote: accept datagrams from ANY source address/port.
  //
  ZeroMem (&UdpConfig.RemoteAddress, sizeof (EFI_IPv4_ADDRESS));
  UdpConfig.RemotePort = 0;

  //
  // UDP4 Configure may need a few attempts while IP4 settles
  //
  {
    UINTN  CfgRetry;

    Status = EFI_NO_MAPPING;
    for (CfgRetry = 0; CfgRetry < 10 && Status == EFI_NO_MAPPING; CfgRetry++) {
      if (CfgRetry > 0) {
        gBS->Stall (200000);  // 200ms
      }
      Status = Link->Udp4->Configure (Link->Udp4, &UdpConfig);
    }
  }

  if (EFI_ERROR (Status)) {
    UnicodeSPrint (
      Link->StatusMsg, sizeof (Link->StatusMsg),
      L"UDP4 configure failed (%r)", Status
      );
    Link->State = COMPANION_ERROR;
    Udp4Sb->DestroyChild (Udp4Sb, Link->Udp4ChildHandle);
    Link->Udp4            = NULL;
    Link->Udp4ChildHandle = NULL;
    return Status;
  }

  //
  // Warm up: poll to let ARP/IP4 process any pending frames
  //
  {
    UINTN  WarmUp;
    for (WarmUp = 0; WarmUp < 5; WarmUp++) {
      Link->Udp4->Poll (Link->Udp4);
      gBS->Stall (100000);  // 100ms
    }
  }

  //
  // Step 4: Create a persistent MNP (Managed Network Protocol) child
  // for reliable receiving. The UDP4 receive path is unreliable in
  // some UEFI implementations (frames arrive at NIC but UDP4->Receive
  // never completes). MNP provides raw frame access at the Ethernet
  // level, bypassing IP4/UDP4 stack issues entirely.
  // MNP multiplexes — each client gets independent copies of matching
  // frames, so this does not interfere with the UDP4/IP4 stack.
  //
  {
    EFI_SERVICE_BINDING_PROTOCOL     *MnpSb;
    EFI_MANAGED_NETWORK_CONFIG_DATA  MnpConfig;
    EFI_STATUS                       MnpStatus;

    Link->MnpChildHandle = NULL;
    Link->Mnp            = NULL;
    MnpSb                = NULL;

    MnpStatus = gBS->HandleProtocol (
                       NicHandle,
                       &gEfiManagedNetworkServiceBindingProtocolGuid,
                       (VOID **)&MnpSb
                       );
    if (!EFI_ERROR (MnpStatus)) {
      MnpStatus = MnpSb->CreateChild (MnpSb, &Link->MnpChildHandle);
    }
    if (!EFI_ERROR (MnpStatus)) {
      MnpStatus = gBS->HandleProtocol (
                         Link->MnpChildHandle,
                         &gEfiManagedNetworkProtocolGuid,
                         (VOID **)&Link->Mnp
                         );
    }
    if (!EFI_ERROR (MnpStatus)) {
      ZeroMem (&MnpConfig, sizeof (MnpConfig));
      MnpConfig.ReceivedQueueTimeoutValue = 0;
      MnpConfig.TransmitQueueTimeoutValue = 0;
      MnpConfig.ProtocolTypeFilter        = 0x0800;  // IPv4 only
      MnpConfig.EnableUnicastReceive      = TRUE;
      MnpConfig.EnableMulticastReceive    = FALSE;
      MnpConfig.EnableBroadcastReceive    = TRUE;
      MnpConfig.EnablePromiscuousReceive  = FALSE;
      MnpConfig.FlushQueuesOnReset        = TRUE;
      MnpConfig.EnableReceiveTimestamps   = FALSE;
      MnpConfig.DisableBackgroundPolling  = FALSE;

      MnpStatus = Link->Mnp->Configure (Link->Mnp, &MnpConfig);
    }
    if (EFI_ERROR (MnpStatus)) {
      //
      // MNP setup failed — clean up partial state.
      // Init succeeds but CompanionConnect will fail.
      //
      if (Link->MnpChildHandle != NULL && MnpSb != NULL) {
        MnpSb->DestroyChild (MnpSb, Link->MnpChildHandle);
      }
      Link->MnpChildHandle = NULL;
      Link->Mnp            = NULL;
    }
  }

  UtilSafeStrCpy (Link->StatusMsg, L"Initialized, ready to connect", 128);
  return EFI_SUCCESS;
}

/**
  Send a raw ASCII command over the UDP control channel.

  @param[in,out]  Link     Companion link context.
  @param[in]      Command  ASCII command string to send.

  @retval EFI_SUCCESS  Command sent successfully.
  @retval EFI_TIMEOUT  Transmit timed out.
  @retval other        Transmit failure.
**/
EFI_STATUS
CompanionSendCommand (
  IN OUT COMPANION_LINK  *Link,
  IN     CONST CHAR8     *Command
  )
{
  EFI_STATUS                 Status;
  EFI_UDP4_COMPLETION_TOKEN  TxToken;
  EFI_UDP4_TRANSMIT_DATA     TxData;
  EFI_UDP4_SESSION_DATA      SessionData;
  UINTN                      Len;
  UINTN                      ElapsedMs;

  if (Link == NULL || Link->Udp4 == NULL || Command == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Len = AsciiStrLen (Command);
  if (Len == 0 || Len > COMPANION_MAX_MSG_SIZE) {
    return EFI_INVALID_PARAMETER;
  }

  //
  // Session data specifies the destination per-packet.
  // Required because RemoteAddress is wildcard (0.0.0.0) in Configure.
  //
  ZeroMem (&SessionData, sizeof (SessionData));
  CopyMem (&SessionData.DestinationAddress, &Link->CompanionIp, sizeof (EFI_IPv4_ADDRESS));
  SessionData.DestinationPort = Link->Port;

  //
  // Set up transmit data with single fragment
  //
  ZeroMem (&TxData, sizeof (TxData));
  TxData.UdpSessionData          = &SessionData;
  TxData.GatewayAddress          = NULL;
  TxData.DataLength              = (UINT32)Len;
  TxData.FragmentCount           = 1;
  TxData.FragmentTable[0].FragmentLength = (UINT32)Len;
  TxData.FragmentTable[0].FragmentBuffer = (VOID *)Command;

  //
  // Create completion token
  //
  ZeroMem (&TxToken, sizeof (TxToken));
  Status = gBS->CreateEvent (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  UdpNotifyStub,
                  NULL,
                  &TxToken.Event
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  TxToken.Status        = EFI_NOT_READY;
  TxToken.Packet.TxData = &TxData;

  //
  // Transmit — retry on EFI_NO_MAPPING (ARP not resolved yet).
  // The IP4 child needs ARP to resolve the destination MAC.
  // Each retry polls aggressively to process ARP responses.
  //
  {
    UINTN  TxRetry;
    UINTN  PollIdx;

    Status = EFI_NO_MAPPING;
    for (TxRetry = 0; TxRetry < 8 && Status == EFI_NO_MAPPING; TxRetry++) {
      if (TxRetry > 0) {
        //
        // Poll aggressively between retries to process ARP
        //
        for (PollIdx = 0; PollIdx < 10; PollIdx++) {
          Link->Udp4->Poll (Link->Udp4);
          gBS->Stall (30000);  // 30ms
        }
      }
      Status = Link->Udp4->Transmit (Link->Udp4, &TxToken);
    }
  }

  if (EFI_ERROR (Status)) {
    gBS->CloseEvent (TxToken.Event);
    return Status;
  }

  //
  // Poll until complete or timeout
  //
  ElapsedMs = 0;
  while (TxToken.Status == EFI_NOT_READY && ElapsedMs < Link->TimeoutMs) {
    Link->Udp4->Poll (Link->Udp4);
    gBS->Stall (1000);
    ElapsedMs++;
  }

  if (TxToken.Status == EFI_NOT_READY) {
    Link->Udp4->Cancel (Link->Udp4, &TxToken);
    gBS->CloseEvent (TxToken.Event);
    return EFI_TIMEOUT;
  }

  Status = TxToken.Status;
  gBS->CloseEvent (TxToken.Event);

  Link->MessageId++;
  return Status;
}

/**
  Receive a response from the companion with timeout.
  Uses SNP (Simple Network Protocol) directly to receive raw Ethernet
  frames, completely bypassing the MNP/IP4/UDP4 receive stack.
  Parses Ethernet+IPv4+UDP headers to find control channel packets.

  Also counts total received frames for diagnostics — the count is
  stored in Link->StatusMsg on timeout to help identify whether the
  NIC is receiving any frames at all.

  @param[in,out]  Link          Companion link context.
  @param[out]     Response      Buffer to receive ASCII response.
  @param[in]      ResponseSize  Size of Response buffer in bytes.
  @param[in]      TimeoutMs     Receive timeout in milliseconds.

  @retval EFI_SUCCESS      Response received successfully.
  @retval EFI_TIMEOUT      No response within timeout period.
  @retval EFI_UNSUPPORTED  SNP not available.
  @retval other            Receive failure.
**/
EFI_STATUS
CompanionReceiveResponse (
  IN OUT COMPANION_LINK  *Link,
  OUT    CHAR8           *Response,
  IN     UINTN           ResponseSize,
  IN     UINT32          TimeoutMs
  )
{
  EFI_STATUS                   Status;
  EFI_SIMPLE_NETWORK_PROTOCOL  *Snp;
  UINTN                        ElapsedMs;
  UINT8                        RxBuf[1600];
  UINTN                        BufSize;
  UINTN                        HeaderSize;
  EFI_TPL                      OldTpl;
  UINTN                        FrameCount;
  UINTN                        Ipv4Count;
  UINTN                        UdpCount;

  if (Link == NULL || Response == NULL || ResponseSize == 0) {
    return EFI_INVALID_PARAMETER;
  }

  //
  // Get SNP directly from the NIC handle.
  // This is the lowest possible level — reads raw frames from the
  // hardware driver, completely bypassing MNP/IP4/UDP4.
  //
  Status = gBS->HandleProtocol (
                  Link->NicHandle,
                  &gEfiSimpleNetworkProtocolGuid,
                  (VOID **)&Snp
                  );
  if (EFI_ERROR (Status) || Snp == NULL) {
    return EFI_UNSUPPORTED;
  }

  if (Snp->Mode->State != EfiSimpleNetworkInitialized) {
    return EFI_NOT_READY;
  }

  //
  // Ensure unicast receive is enabled on the NIC.
  // Some drivers require explicit ReceiveFilters configuration.
  //
  Snp->ReceiveFilters (
         Snp,
         EFI_SIMPLE_NETWORK_RECEIVE_UNICAST |
         EFI_SIMPLE_NETWORK_RECEIVE_BROADCAST,
         0,
         FALSE,
         0,
         NULL
         );

  Response[0] = '\0';
  ElapsedMs   = 0;
  FrameCount  = 0;
  Ipv4Count   = 0;
  UdpCount    = 0;

  while (ElapsedMs < TimeoutMs) {
    //
    // Raise TPL to prevent MNP background polling from consuming
    // frames between our SNP->Receive() calls.
    //
    BufSize    = sizeof (RxBuf);
    HeaderSize = 0;

    OldTpl = gBS->RaiseTPL (TPL_CALLBACK);
    Status = Snp->Receive (Snp, &HeaderSize, &BufSize, RxBuf, NULL, NULL, NULL);
    gBS->RestoreTPL (OldTpl);

    if (Status == EFI_NOT_READY) {
      //
      // No frame in NIC buffer — wait and retry
      //
      gBS->Stall (2000);  // 2ms
      ElapsedMs += 2;
      continue;
    }

    if (EFI_ERROR (Status)) {
      gBS->Stall (2000);
      ElapsedMs += 2;
      continue;
    }

    //
    // Got a raw Ethernet frame.
    //
    FrameCount++;

    //
    // Need at least Ethernet header (14) + IP header (20) + UDP header (8) = 42
    //
    if (BufSize < 42 || HeaderSize < 14) {
      continue;
    }

    //
    // Check EtherType at bytes [12-13] (big-endian): 0x0800 = IPv4
    //
    if (RxBuf[12] != 0x08 || RxBuf[13] != 0x00) {
      continue;
    }

    //
    // Parse IPv4 header (starts after Ethernet header)
    //
    {
      UINT8   *IpHdr;
      UINT16  IpHdrLen;
      UINT16  DstPort;
      UINT16  UdpLen;
      UINT16  PayloadLen;
      UINTN   CopyLen;

      IpHdr = RxBuf + HeaderSize;

      //
      // Verify IPv4 version
      //
      if ((IpHdr[0] >> 4) != 4) {
        continue;
      }

      Ipv4Count++;

      //
      // Check protocol = UDP (17)
      //
      if (IpHdr[9] != 17) {
        continue;
      }

      UdpCount++;

      //
      // Check source IP matches companion
      //
      if (CompareMem (&IpHdr[12], &Link->CompanionIp, 4) != 0) {
        continue;
      }

      IpHdrLen = (UINT16)((IpHdr[0] & 0x0F) * 4);

      if (BufSize < HeaderSize + IpHdrLen + 8) {
        continue;
      }

      //
      // Parse UDP: [0-1] SrcPort, [2-3] DstPort, [4-5] Length
      // All in network byte order (big-endian).
      //
      DstPort = (UINT16)((IpHdr[IpHdrLen + 2] << 8) | IpHdr[IpHdrLen + 3]);

      if (DstPort != Link->Port) {
        continue;
      }

      //
      // Found matching UDP packet — extract payload
      //
      UdpLen     = (UINT16)((IpHdr[IpHdrLen + 4] << 8) | IpHdr[IpHdrLen + 5]);
      PayloadLen = (UINT16)(UdpLen - 8);

      if (PayloadLen > 0 &&
          HeaderSize + IpHdrLen + 8 + PayloadLen <= BufSize) {
        CopyLen = PayloadLen;
        if (CopyLen >= ResponseSize - 1) {
          CopyLen = ResponseSize - 1;
        }
        CopyMem (Response, &IpHdr[IpHdrLen + 8], CopyLen);
        Response[CopyLen] = '\0';
        return EFI_SUCCESS;
      }
    }
  }

  //
  // Timeout — store diagnostic frame counts in StatusMsg.
  // This helps identify whether the NIC is receiving any frames at all.
  //
  UnicodeSPrint (
    Link->StatusMsg, sizeof (Link->StatusMsg),
    L"RX frames:%d ipv4:%d udp:%d (no match in %dms)",
    FrameCount, Ipv4Count, UdpCount, TimeoutMs
    );

  return EFI_TIMEOUT;
}

/**
  Perform handshake with the companion.
  Sends HELLO via UDP4 and receives ACK via MNP (which bypasses the
  unreliable UDP4 receive path). Retries up to 3 times.

  @param[in,out]  Link  Companion link context.

  @retval EFI_SUCCESS       Handshake completed, companion connected.
  @retval EFI_TIMEOUT       No response from companion after all retries.
  @retval EFI_DEVICE_ERROR  Unexpected response.
  @retval other             Communication failure.
**/
EFI_STATUS
CompanionConnect (
  IN OUT COMPANION_LINK  *Link
  )
{
  EFI_STATUS  Status;
  UINTN       Attempt;
  CHAR8       Response[COMPANION_MAX_MSG_SIZE];

  if (Link == NULL || Link->Udp4 == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (Link->Mnp == NULL) {
    Link->State = COMPANION_ERROR;
    UtilSafeStrCpy (Link->StatusMsg, L"MNP not available for receive", 128);
    return EFI_UNSUPPORTED;
  }

  Link->State = COMPANION_CONNECTING;

  for (Attempt = 0; Attempt < 3; Attempt++) {
    UnicodeSPrint (
      Link->StatusMsg, sizeof (Link->StatusMsg),
      L"HELLO attempt %d/3...", Attempt + 1
      );

    //
    // Send HELLO via UDP4 (transmit path works fine).
    // MNP's background polling will automatically queue any
    // incoming ACK frame during the send completion process,
    // so we don't need the "receive-before-send" pattern.
    //
    Status = CompanionSendCommand (Link, "HELLO DDTSoft 1.0\n");
    if (EFI_ERROR (Status)) {
      //
      // Send failed — wait for ARP to settle and retry
      //
      gBS->Stall (1000000);  // 1s
      continue;
    }

    //
    // Receive ACK via MNP (bypasses broken UDP4 receive).
    // MNP provides raw Ethernet frames which CompanionReceiveResponse
    // parses to find our UDP control channel packets.
    //
    Response[0] = '\0';
    Status = CompanionReceiveResponse (Link, Response, sizeof (Response), 2000);
    if (EFI_ERROR (Status)) {
      continue;
    }

    if (AsciiStrnCmp (Response, "ACK", 3) == 0) {
      Link->State = COMPANION_CONNECTED;
      UtilSafeStrCpy (Link->StatusMsg, L"Connected to companion", 128);
      return EFI_SUCCESS;
    }

    if (AsciiStrnCmp (Response, "ERROR", 5) == 0) {
      Link->State = COMPANION_ERROR;
      UtilSafeStrCpy (Link->StatusMsg, L"Companion returned error", 128);
      return EFI_DEVICE_ERROR;
    }

    //
    // Unexpected response — retry
    //
  }

  Link->State = COMPANION_ERROR;
  //
  // StatusMsg already contains frame count diagnostics from the last
  // CompanionReceiveResponse call (e.g., "RX frames:0 ipv4:0 udp:0").
  // Don't overwrite — this diagnostic info is critical for debugging.
  //
  return EFI_TIMEOUT;
}

/**
  Disconnect from the companion.
  Sends DONE and waits for CONFIRM response.

  @param[in,out]  Link  Companion link context.

  @retval EFI_SUCCESS  Disconnected successfully.
  @retval other        Communication failure (link still cleaned up).
**/
EFI_STATUS
CompanionDisconnect (
  IN OUT COMPANION_LINK  *Link
  )
{
  CHAR8  Response[COMPANION_MAX_MSG_SIZE];

  if (Link == NULL || Link->Udp4 == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (Link->State == COMPANION_CONNECTED) {
    //
    // Send DONE, best-effort
    //
    CompanionSendCommand (Link, "DONE\n");
    CompanionReceiveResponse (Link, Response, sizeof (Response), 1000);
  }

  Link->State = COMPANION_DISCONNECTED;
  UtilSafeStrCpy (Link->StatusMsg, L"Disconnected", 128);
  return EFI_SUCCESS;
}

/**
  Destroy the companion link and release UDP4 resources.
  Unconfigures the UDP4 instance and destroys the child handle.

  @param[in,out]  Link  Companion link context.

  @retval EFI_SUCCESS  Link destroyed successfully.
  @retval other        Cleanup failure.
**/
EFI_STATUS
CompanionDestroy (
  IN OUT COMPANION_LINK  *Link
  )
{
  EFI_STATUS                    Status;
  EFI_SERVICE_BINDING_PROTOCOL  *Udp4Sb;

  if (Link == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  //
  // Disconnect if still connected
  //
  if (Link->State == COMPANION_CONNECTED) {
    CompanionDisconnect (Link);
  }

  //
  // Unconfigure and destroy MNP child
  //
  if (Link->Mnp != NULL) {
    Link->Mnp->Configure (Link->Mnp, NULL);
    Link->Mnp = NULL;
  }
  if (Link->MnpChildHandle != NULL) {
    {
      EFI_SERVICE_BINDING_PROTOCOL  *MnpSb;

      Status = gBS->HandleProtocol (
                      Link->NicHandle,
                      &gEfiManagedNetworkServiceBindingProtocolGuid,
                      (VOID **)&MnpSb
                      );
      if (!EFI_ERROR (Status)) {
        MnpSb->DestroyChild (MnpSb, Link->MnpChildHandle);
      }
    }
    Link->MnpChildHandle = NULL;
  }

  //
  // Unconfigure UDP4
  //
  if (Link->Udp4 != NULL) {
    Link->Udp4->Configure (Link->Udp4, NULL);
    Link->Udp4 = NULL;
  }

  //
  // Destroy UDP4 child via service binding
  //
  if (Link->Udp4ChildHandle != NULL) {
    Status = gBS->HandleProtocol (
                    Link->NicHandle,
                    &gEfiUdp4ServiceBindingProtocolGuid,
                    (VOID **)&Udp4Sb
                    );
    if (!EFI_ERROR (Status)) {
      Udp4Sb->DestroyChild (Udp4Sb, Link->Udp4ChildHandle);
    }
    Link->Udp4ChildHandle = NULL;
  }

  Link->State = COMPANION_DISCONNECTED;
  UtilSafeStrCpy (Link->StatusMsg, L"Destroyed", 128);
  return EFI_SUCCESS;
}

/**
  Send PREPARE command to set up a test on the companion side.

  @param[in,out]  Link   Companion link context.
  @param[in]      Layer  OSI layer identifier (e.g., "L1", "L3").
  @param[in]      Test   Test name (e.g., "ICMP_ECHO", "TCP_CONNECT").
  @param[in]      Args   Additional arguments (optional, may be NULL).

  @retval EFI_SUCCESS     Companion is READY for the test.
  @retval EFI_DEVICE_ERROR  Companion returned error.
  @retval other           Communication failure.
**/
EFI_STATUS
CompanionPrepare (
  IN OUT COMPANION_LINK  *Link,
  IN     CONST CHAR8     *Layer,
  IN     CONST CHAR8     *Test,
  IN     CONST CHAR8     *Args  OPTIONAL
  )
{
  EFI_STATUS  Status;
  CHAR8       CmdBuf[COMPANION_MAX_MSG_SIZE];
  CHAR8       Response[COMPANION_MAX_MSG_SIZE];

  if (Link == NULL || Link->State != COMPANION_CONNECTED) {
    return EFI_NOT_READY;
  }

  if (Layer == NULL || Test == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  //
  // Build PREPARE command
  //
  if (Args != NULL && AsciiStrLen (Args) > 0) {
    AsciiSPrint (CmdBuf, sizeof (CmdBuf), "PREPARE %a %a %a\n", Layer, Test, Args);
  } else {
    AsciiSPrint (CmdBuf, sizeof (CmdBuf), "PREPARE %a %a\n", Layer, Test);
  }

  Status = CompanionSendCommand (Link, CmdBuf);
  if (EFI_ERROR (Status)) {
    UtilSafeStrCpy (Link->StatusMsg, L"Failed to send PREPARE", 128);
    return Status;
  }

  //
  // Wait for READY
  //
  Status = CompanionReceiveResponse (Link, Response, sizeof (Response), Link->TimeoutMs);
  if (EFI_ERROR (Status)) {
    UtilSafeStrCpy (Link->StatusMsg, L"No response to PREPARE", 128);
    return Status;
  }

  if (AsciiStrnCmp (Response, "READY", 5) == 0) {
    UtilSafeStrCpy (Link->StatusMsg, L"Companion ready", 128);
    return EFI_SUCCESS;
  }

  if (AsciiStrnCmp (Response, "ERROR", 5) == 0) {
    UtilSafeStrCpy (Link->StatusMsg, L"Companion PREPARE error", 128);
    return EFI_DEVICE_ERROR;
  }

  UtilSafeStrCpy (Link->StatusMsg, L"Unexpected PREPARE response", 128);
  return EFI_DEVICE_ERROR;
}

/**
  Send START command to begin a prepared test.

  @param[in,out]  Link  Companion link context.

  @retval EFI_SUCCESS     Companion acknowledged START.
  @retval EFI_DEVICE_ERROR  Companion returned error.
  @retval other           Communication failure.
**/
EFI_STATUS
CompanionStart (
  IN OUT COMPANION_LINK  *Link
  )
{
  EFI_STATUS  Status;
  CHAR8       Response[COMPANION_MAX_MSG_SIZE];

  if (Link == NULL || Link->State != COMPANION_CONNECTED) {
    return EFI_NOT_READY;
  }

  Status = CompanionSendCommand (Link, "START\n");
  if (EFI_ERROR (Status)) {
    UtilSafeStrCpy (Link->StatusMsg, L"Failed to send START", 128);
    return Status;
  }

  Status = CompanionReceiveResponse (Link, Response, sizeof (Response), Link->TimeoutMs);
  if (EFI_ERROR (Status)) {
    UtilSafeStrCpy (Link->StatusMsg, L"No response to START", 128);
    return Status;
  }

  if (AsciiStrnCmp (Response, "ACK", 3) == 0) {
    UtilSafeStrCpy (Link->StatusMsg, L"Test started", 128);
    return EFI_SUCCESS;
  }

  UtilSafeStrCpy (Link->StatusMsg, L"Unexpected START response", 128);
  return EFI_DEVICE_ERROR;
}

/**
  Send STOP command to halt a running test.

  @param[in,out]  Link  Companion link context.

  @retval EFI_SUCCESS  Companion acknowledged STOP.
  @retval other        Communication failure.
**/
EFI_STATUS
CompanionStop (
  IN OUT COMPANION_LINK  *Link
  )
{
  EFI_STATUS  Status;
  CHAR8       Response[COMPANION_MAX_MSG_SIZE];

  if (Link == NULL || Link->State != COMPANION_CONNECTED) {
    return EFI_NOT_READY;
  }

  Status = CompanionSendCommand (Link, "STOP\n");
  if (EFI_ERROR (Status)) {
    UtilSafeStrCpy (Link->StatusMsg, L"Failed to send STOP", 128);
    return Status;
  }

  Status = CompanionReceiveResponse (Link, Response, sizeof (Response), Link->TimeoutMs);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (AsciiStrnCmp (Response, "ACK", 3) == 0) {
    UtilSafeStrCpy (Link->StatusMsg, L"Test stopped", 128);
    return EFI_SUCCESS;
  }

  return EFI_DEVICE_ERROR;
}

/**
  Send RESULT query and receive test result from the companion.

  @param[in,out]  Link        Companion link context.
  @param[out]     Result      Buffer to receive result data.
  @param[in]      ResultSize  Size of Result buffer in bytes.

  @retval EFI_SUCCESS  Result received successfully.
  @retval other        Communication or protocol failure.
**/
EFI_STATUS
CompanionGetResult (
  IN OUT COMPANION_LINK  *Link,
  OUT    CHAR8           *Result,
  IN     UINTN           ResultSize
  )
{
  EFI_STATUS  Status;

  if (Link == NULL || Link->State != COMPANION_CONNECTED || Result == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Status = CompanionSendCommand (Link, "RESULT\n");
  if (EFI_ERROR (Status)) {
    UtilSafeStrCpy (Link->StatusMsg, L"Failed to send RESULT", 128);
    return Status;
  }

  //
  // Wait for REPORT response (may take longer for large results)
  //
  Status = CompanionReceiveResponse (Link, Result, ResultSize, Link->TimeoutMs * 2);
  if (EFI_ERROR (Status)) {
    UtilSafeStrCpy (Link->StatusMsg, L"No result from companion", 128);
    return Status;
  }

  if (AsciiStrnCmp (Result, "REPORT", 6) == 0) {
    UtilSafeStrCpy (Link->StatusMsg, L"Result received", 128);
    return EFI_SUCCESS;
  }

  if (AsciiStrnCmp (Result, "ERROR", 5) == 0) {
    UtilSafeStrCpy (Link->StatusMsg, L"Companion result error", 128);
    return EFI_DEVICE_ERROR;
  }

  return EFI_SUCCESS;
}
