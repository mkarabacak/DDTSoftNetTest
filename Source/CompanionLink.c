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
  // Open UDP4 Service Binding on the NIC handle
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
  // Configure UDP4 instance
  //
  ZeroMem (&UdpConfig, sizeof (UdpConfig));
  UdpConfig.AcceptBroadcast    = FALSE;
  UdpConfig.AcceptPromiscuous  = FALSE;
  UdpConfig.AcceptAnyPort      = FALSE;
  UdpConfig.AllowDuplicatePort = FALSE;
  UdpConfig.TimeToLive         = 64;
  UdpConfig.TypeOfService      = 0;
  UdpConfig.DoNotFragment      = FALSE;
  UdpConfig.ReceiveTimeout     = 0;
  UdpConfig.TransmitTimeout    = 0;
  UdpConfig.UseDefaultAddress  = FALSE;

  CopyMem (&UdpConfig.StationAddress, &Link->LocalIp, sizeof (EFI_IPv4_ADDRESS));
  CopyMem (&UdpConfig.SubnetMask, &Link->SubnetMask, sizeof (EFI_IPv4_ADDRESS));
  UdpConfig.StationPort = Link->Port;

  CopyMem (&UdpConfig.RemoteAddress, &Link->CompanionIp, sizeof (EFI_IPv4_ADDRESS));
  UdpConfig.RemotePort = Link->Port;

  Status = Link->Udp4->Configure (Link->Udp4, &UdpConfig);
  if (EFI_ERROR (Status)) {
    UtilSafeStrCpy (Link->StatusMsg, L"UDP4 configure failed", 128);
    Link->State = COMPANION_ERROR;
    Udp4Sb->DestroyChild (Udp4Sb, Link->Udp4ChildHandle);
    Link->Udp4            = NULL;
    Link->Udp4ChildHandle = NULL;
    return Status;
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
  // Set up transmit data with single fragment
  //
  ZeroMem (&TxData, sizeof (TxData));
  TxData.UdpSessionData          = NULL;
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
  // Transmit
  //
  Status = Link->Udp4->Transmit (Link->Udp4, &TxToken);
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

  @param[in,out]  Link          Companion link context.
  @param[out]     Response      Buffer to receive ASCII response.
  @param[in]      ResponseSize  Size of Response buffer in bytes.
  @param[in]      TimeoutMs     Receive timeout in milliseconds.

  @retval EFI_SUCCESS  Response received successfully.
  @retval EFI_TIMEOUT  No response within timeout period.
  @retval other        Receive failure.
**/
EFI_STATUS
CompanionReceiveResponse (
  IN OUT COMPANION_LINK  *Link,
  OUT    CHAR8           *Response,
  IN     UINTN           ResponseSize,
  IN     UINT32          TimeoutMs
  )
{
  EFI_STATUS                 Status;
  EFI_UDP4_COMPLETION_TOKEN  RxToken;
  UINTN                      ElapsedMs;
  UINTN                      TotalLen;
  UINTN                      CopyLen;
  UINTN                      I;

  if (Link == NULL || Link->Udp4 == NULL || Response == NULL || ResponseSize == 0) {
    return EFI_INVALID_PARAMETER;
  }

  //
  // Create completion token
  //
  ZeroMem (&RxToken, sizeof (RxToken));
  Status = gBS->CreateEvent (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  UdpNotifyStub,
                  NULL,
                  &RxToken.Event
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  RxToken.Status        = EFI_NOT_READY;
  RxToken.Packet.RxData = NULL;

  //
  // Issue receive
  //
  Status = Link->Udp4->Receive (Link->Udp4, &RxToken);
  if (EFI_ERROR (Status)) {
    gBS->CloseEvent (RxToken.Event);
    return Status;
  }

  //
  // Poll until data arrives or timeout
  //
  ElapsedMs = 0;
  while (RxToken.Status == EFI_NOT_READY && ElapsedMs < TimeoutMs) {
    Link->Udp4->Poll (Link->Udp4);
    gBS->Stall (1000);
    ElapsedMs++;
  }

  if (RxToken.Status == EFI_NOT_READY) {
    Link->Udp4->Cancel (Link->Udp4, &RxToken);
    gBS->CloseEvent (RxToken.Event);
    return EFI_TIMEOUT;
  }

  if (EFI_ERROR (RxToken.Status)) {
    gBS->CloseEvent (RxToken.Event);
    return RxToken.Status;
  }

  //
  // Copy received data from fragments
  //
  Response[0] = '\0';
  if (RxToken.Packet.RxData != NULL) {
    TotalLen = 0;
    for (I = 0; I < RxToken.Packet.RxData->FragmentCount; I++) {
      CopyLen = RxToken.Packet.RxData->FragmentTable[I].FragmentLength;
      if (TotalLen + CopyLen >= ResponseSize - 1) {
        CopyLen = ResponseSize - 1 - TotalLen;
      }
      if (CopyLen > 0) {
        CopyMem (
          Response + TotalLen,
          RxToken.Packet.RxData->FragmentTable[I].FragmentBuffer,
          CopyLen
          );
        TotalLen += CopyLen;
      }
    }
    Response[TotalLen] = '\0';

    //
    // Recycle the receive buffer
    //
    gBS->SignalEvent (RxToken.Packet.RxData->RecycleSignal);
  }

  gBS->CloseEvent (RxToken.Event);
  return EFI_SUCCESS;
}

/**
  Perform handshake with the companion.
  Sends HELLO and waits for ACK response.

  @param[in,out]  Link  Companion link context.

  @retval EFI_SUCCESS     Handshake completed, companion connected.
  @retval EFI_TIMEOUT     No response from companion.
  @retval EFI_DEVICE_ERROR  Unexpected response.
  @retval other           Communication failure.
**/
EFI_STATUS
CompanionConnect (
  IN OUT COMPANION_LINK  *Link
  )
{
  EFI_STATUS  Status;
  CHAR8       Response[COMPANION_MAX_MSG_SIZE];

  if (Link == NULL || Link->Udp4 == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Link->State = COMPANION_CONNECTING;
  UtilSafeStrCpy (Link->StatusMsg, L"Sending HELLO...", 128);

  //
  // Send HELLO with version
  //
  Status = CompanionSendCommand (Link, "HELLO DDTSoft 1.0\n");
  if (EFI_ERROR (Status)) {
    Link->State = COMPANION_ERROR;
    UtilSafeStrCpy (Link->StatusMsg, L"Failed to send HELLO", 128);
    return Status;
  }

  //
  // Wait for ACK
  //
  UtilSafeStrCpy (Link->StatusMsg, L"Waiting for ACK...", 128);

  Status = CompanionReceiveResponse (Link, Response, sizeof (Response), Link->TimeoutMs);
  if (EFI_ERROR (Status)) {
    Link->State = COMPANION_ERROR;
    if (Status == EFI_TIMEOUT) {
      UtilSafeStrCpy (Link->StatusMsg, L"Timeout: no response from companion", 128);
    } else {
      UtilSafeStrCpy (Link->StatusMsg, L"Receive error during handshake", 128);
    }
    return Status;
  }

  //
  // Check for ACK response
  //
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

  Link->State = COMPANION_ERROR;
  UtilSafeStrCpy (Link->StatusMsg, L"Unexpected response to HELLO", 128);
  return EFI_DEVICE_ERROR;
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
