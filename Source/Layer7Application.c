/** @file
  Layer 7 (Application) test implementations.
  Tests DHCP discovery/lease, DNS resolution, and HTTP connectivity.
  Uses EFI_DHCP4_PROTOCOL, EFI_DNS4_PROTOCOL, and EFI_HTTP_PROTOCOL.
**/

#include <DDTSoftNetTest.h>
#include <OsiLayers.h>
#include <TestCases.h>
#include <PacketDefs.h>

//
// ============================================================
// Static helper: notification stub for async completion tokens
// ============================================================
//
STATIC
VOID
EFIAPI
L7NotifyStub (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  //
  // Nothing to do; we poll for completion
  //
}

//
// ============================================================
// DHCP4 helpers
// ============================================================
//

/**
  Create a DHCP4 child instance on the given NIC handle.

  @param[in]  NicHandle    NIC handle with DHCP4 service binding.
  @param[out] ChildHandle  Receives created child handle.
  @param[out] Dhcp4        Receives DHCP4 protocol instance.

  @retval EFI_SUCCESS  Child created and protocol retrieved.
**/
STATIC
EFI_STATUS
L7CreateDhcpChild (
  IN  EFI_HANDLE           NicHandle,
  OUT EFI_HANDLE           *ChildHandle,
  OUT EFI_DHCP4_PROTOCOL   **Dhcp4
  )
{
  EFI_STATUS                    Status;
  EFI_SERVICE_BINDING_PROTOCOL  *Sb;

  *ChildHandle = NULL;
  *Dhcp4       = NULL;

  Status = gBS->OpenProtocol (
                  NicHandle,
                  &gEfiDhcp4ServiceBindingProtocolGuid,
                  (VOID **)&Sb,
                  NULL,
                  NULL,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = Sb->CreateChild (Sb, ChildHandle);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->OpenProtocol (
                  *ChildHandle,
                  &gEfiDhcp4ProtocolGuid,
                  (VOID **)Dhcp4,
                  NULL,
                  NULL,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );
  if (EFI_ERROR (Status)) {
    Sb->DestroyChild (Sb, *ChildHandle);
    *ChildHandle = NULL;
    return Status;
  }

  return EFI_SUCCESS;
}

/**
  Destroy a DHCP4 child instance.
**/
STATIC
VOID
L7DestroyDhcpChild (
  IN EFI_HANDLE           NicHandle,
  IN EFI_HANDLE           ChildHandle,
  IN EFI_DHCP4_PROTOCOL   *Dhcp4
  )
{
  EFI_SERVICE_BINDING_PROTOCOL  *Sb;
  EFI_STATUS                    Status;

  if (Dhcp4 != NULL) {
    Dhcp4->Stop (Dhcp4);
    Dhcp4->Configure (Dhcp4, NULL);
  }

  Status = gBS->OpenProtocol (
                  NicHandle,
                  &gEfiDhcp4ServiceBindingProtocolGuid,
                  (VOID **)&Sb,
                  NULL,
                  NULL,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );
  if (!EFI_ERROR (Status) && ChildHandle != NULL) {
    Sb->DestroyChild (Sb, ChildHandle);
  }
}

//
// ============================================================
// DNS4 helpers
// ============================================================
//

/**
  Create a DNS4 child instance on the given NIC handle.
**/
STATIC
EFI_STATUS
L7CreateDnsChild (
  IN  EFI_HANDLE          NicHandle,
  OUT EFI_HANDLE          *ChildHandle,
  OUT EFI_DNS4_PROTOCOL   **Dns4
  )
{
  EFI_STATUS                    Status;
  EFI_SERVICE_BINDING_PROTOCOL  *Sb;

  *ChildHandle = NULL;
  *Dns4        = NULL;

  Status = gBS->OpenProtocol (
                  NicHandle,
                  &gEfiDns4ServiceBindingProtocolGuid,
                  (VOID **)&Sb,
                  NULL,
                  NULL,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = Sb->CreateChild (Sb, ChildHandle);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->OpenProtocol (
                  *ChildHandle,
                  &gEfiDns4ProtocolGuid,
                  (VOID **)Dns4,
                  NULL,
                  NULL,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );
  if (EFI_ERROR (Status)) {
    Sb->DestroyChild (Sb, *ChildHandle);
    *ChildHandle = NULL;
    return Status;
  }

  return EFI_SUCCESS;
}

/**
  Destroy a DNS4 child instance.
**/
STATIC
VOID
L7DestroyDnsChild (
  IN EFI_HANDLE          NicHandle,
  IN EFI_HANDLE          ChildHandle,
  IN EFI_DNS4_PROTOCOL   *Dns4
  )
{
  EFI_SERVICE_BINDING_PROTOCOL  *Sb;
  EFI_STATUS                    Status;

  if (Dns4 != NULL) {
    Dns4->Configure (Dns4, NULL);
  }

  Status = gBS->OpenProtocol (
                  NicHandle,
                  &gEfiDns4ServiceBindingProtocolGuid,
                  (VOID **)&Sb,
                  NULL,
                  NULL,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );
  if (!EFI_ERROR (Status) && ChildHandle != NULL) {
    Sb->DestroyChild (Sb, ChildHandle);
  }
}

//
// ============================================================
// HTTP helpers
// ============================================================
//

/**
  Create an HTTP child instance on the given NIC handle.
**/
STATIC
EFI_STATUS
L7CreateHttpChild (
  IN  EFI_HANDLE           NicHandle,
  OUT EFI_HANDLE           *ChildHandle,
  OUT EFI_HTTP_PROTOCOL    **Http
  )
{
  EFI_STATUS                    Status;
  EFI_SERVICE_BINDING_PROTOCOL  *Sb;

  *ChildHandle = NULL;
  *Http        = NULL;

  Status = gBS->OpenProtocol (
                  NicHandle,
                  &gEfiHttpServiceBindingProtocolGuid,
                  (VOID **)&Sb,
                  NULL,
                  NULL,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = Sb->CreateChild (Sb, ChildHandle);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->OpenProtocol (
                  *ChildHandle,
                  &gEfiHttpProtocolGuid,
                  (VOID **)Http,
                  NULL,
                  NULL,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );
  if (EFI_ERROR (Status)) {
    Sb->DestroyChild (Sb, *ChildHandle);
    *ChildHandle = NULL;
    return Status;
  }

  return EFI_SUCCESS;
}

/**
  Destroy an HTTP child instance.
**/
STATIC
VOID
L7DestroyHttpChild (
  IN EFI_HANDLE          NicHandle,
  IN EFI_HANDLE          ChildHandle,
  IN EFI_HTTP_PROTOCOL   *Http
  )
{
  EFI_SERVICE_BINDING_PROTOCOL  *Sb;
  EFI_STATUS                    Status;

  if (Http != NULL) {
    Http->Configure (Http, NULL);
  }

  Status = gBS->OpenProtocol (
                  NicHandle,
                  &gEfiHttpServiceBindingProtocolGuid,
                  (VOID **)&Sb,
                  NULL,
                  NULL,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );
  if (!EFI_ERROR (Status) && ChildHandle != NULL) {
    Sb->DestroyChild (Sb, ChildHandle);
  }
}

//
// ============================================================
// Test T7.1: DHCP Discover
// Send DHCP discover and check for offers.
//
// PASS: Received DHCP offer/ack, obtained IP address
// WARN: DHCP configured but no server responded
// FAIL: DHCP service unavailable or error
// ============================================================
//
EFI_STATUS
TestL7DhcpDiscover (
  IN  NIC_INFO         *Nic,
  IN  TEST_CONFIG      *Config,
  OUT TEST_RESULT_DATA *Result
  )
{
  EFI_STATUS             Status;
  EFI_HANDLE             ChildHandle;
  EFI_DHCP4_PROTOCOL     *Dhcp4;
  EFI_DHCP4_CONFIG_DATA  CfgData;
  EFI_DHCP4_MODE_DATA    ModeData;
  EFI_EVENT              DoneEvent;
  UINT32                 DiscoverTimeout;
  UINT32                 RequestTimeout;
  UINTN                  PollCount;
  UINT32                 TimeoutMs;

  ChildHandle = NULL;
  Dhcp4       = NULL;
  DoneEvent   = NULL;

  //
  // Create DHCP4 child
  //
  Status = L7CreateDhcpChild (Nic->Handle, &ChildHandle, &Dhcp4);
  if (EFI_ERROR (Status)) {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Cannot create DHCP4 child: %r", Status);
    UnicodeSPrint (Result->Suggestion, sizeof (Result->Suggestion),
                   L"Verify DHCP4 service binding is available on this NIC");
    return EFI_SUCCESS;
  }

  //
  // Configure DHCP4 for discovery
  //
  TimeoutMs = (Config->TimeoutMs > 0) ? Config->TimeoutMs : 10000;
  DiscoverTimeout = (TimeoutMs / 1000 > 0) ? (TimeoutMs / 1000) : 3;
  RequestTimeout  = DiscoverTimeout;

  ZeroMem (&CfgData, sizeof (CfgData));
  CfgData.DiscoverTryCount = 2;
  CfgData.DiscoverTimeout  = &DiscoverTimeout;
  CfgData.RequestTryCount  = 2;
  CfgData.RequestTimeout   = &RequestTimeout;
  //
  // ClientAddress = 0.0.0.0 → enters Dhcp4Init state
  //
  ZeroMem (&CfgData.ClientAddress, sizeof (EFI_IPv4_ADDRESS));
  CfgData.Dhcp4Callback  = NULL;
  CfgData.CallbackContext = NULL;
  CfgData.OptionCount    = 0;
  CfgData.OptionList     = NULL;

  Status = Dhcp4->Configure (Dhcp4, &CfgData);
  if (EFI_ERROR (Status)) {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"DHCP4 Configure failed: %r", Status);
    UnicodeSPrint (Result->Suggestion, sizeof (Result->Suggestion),
                   L"Another DHCP instance may already be active");
    L7DestroyDhcpChild (Nic->Handle, ChildHandle, Dhcp4);
    return EFI_SUCCESS;
  }

  //
  // Create completion event for async Start
  //
  Status = gBS->CreateEvent (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  L7NotifyStub,
                  NULL,
                  &DoneEvent
                  );
  if (EFI_ERROR (Status)) {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Cannot create event: %r", Status);
    L7DestroyDhcpChild (Nic->Handle, ChildHandle, Dhcp4);
    return EFI_SUCCESS;
  }

  //
  // Start DHCP process (async)
  //
  Status = Dhcp4->Start (Dhcp4, DoneEvent);
  if (EFI_ERROR (Status) && Status != EFI_ALREADY_STARTED) {
    //
    // Start may return synchronously on timeout or error
    //
    Dhcp4->GetModeData (Dhcp4, &ModeData);

    if (Status == EFI_TIMEOUT) {
      Result->StatusCode = TEST_RESULT_WARN;
      UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                     L"DHCP discover sent but no server responded");
      UnicodeSPrint (Result->Detail, sizeof (Result->Detail),
                     L"State: %d  No DHCP offers received within %d seconds",
                     ModeData.State, DiscoverTimeout);
      UnicodeSPrint (Result->Suggestion, sizeof (Result->Suggestion),
                     L"Ensure a DHCP server is running on the network");
    } else {
      Result->StatusCode = TEST_RESULT_FAIL;
      UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                     L"DHCP Start failed: %r", Status);
    }

    Result->PacketsSent = 1;
    gBS->CloseEvent (DoneEvent);
    L7DestroyDhcpChild (Nic->Handle, ChildHandle, Dhcp4);
    return EFI_SUCCESS;
  }

  //
  // Poll for completion if Start returned EFI_SUCCESS (async)
  //
  if (!EFI_ERROR (Status)) {
    PollCount = 0;
    while (PollCount < (TimeoutMs + 500)) {
      Dhcp4->GetModeData (Dhcp4, &ModeData);
      if (ModeData.State == Dhcp4Bound) {
        break;
      }
      gBS->Stall (1000);  // 1ms
      PollCount++;
    }
  }

  //
  // Check final state
  //
  Dhcp4->GetModeData (Dhcp4, &ModeData);
  Result->PacketsSent = 1;

  if (ModeData.State == Dhcp4Bound) {
    Result->StatusCode = TEST_RESULT_PASS;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"DHCP lease acquired: %d.%d.%d.%d",
                   ModeData.ClientAddress.Addr[0],
                   ModeData.ClientAddress.Addr[1],
                   ModeData.ClientAddress.Addr[2],
                   ModeData.ClientAddress.Addr[3]);
    UnicodeSPrint (Result->Detail, sizeof (Result->Detail),
                   L"Server: %d.%d.%d.%d  Mask: %d.%d.%d.%d  "
                   L"Router: %d.%d.%d.%d  Lease: %ds",
                   ModeData.ServerAddress.Addr[0],
                   ModeData.ServerAddress.Addr[1],
                   ModeData.ServerAddress.Addr[2],
                   ModeData.ServerAddress.Addr[3],
                   ModeData.SubnetMask.Addr[0],
                   ModeData.SubnetMask.Addr[1],
                   ModeData.SubnetMask.Addr[2],
                   ModeData.SubnetMask.Addr[3],
                   ModeData.RouterAddress.Addr[0],
                   ModeData.RouterAddress.Addr[1],
                   ModeData.RouterAddress.Addr[2],
                   ModeData.RouterAddress.Addr[3],
                   ModeData.LeaseTime);
    Result->PacketsReceived = 1;
  } else if (ModeData.State == Dhcp4Selecting || ModeData.State == Dhcp4Requesting) {
    Result->StatusCode = TEST_RESULT_WARN;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"DHCP in progress but did not complete (state=%d)",
                   ModeData.State);
    UnicodeSPrint (Result->Suggestion, sizeof (Result->Suggestion),
                   L"Increase timeout or check DHCP server");
  } else {
    Result->StatusCode = TEST_RESULT_WARN;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"DHCP discover sent, no lease obtained (state=%d)",
                   ModeData.State);
    UnicodeSPrint (Result->Suggestion, sizeof (Result->Suggestion),
                   L"Verify DHCP server is running on the network");
  }

  gBS->CloseEvent (DoneEvent);
  L7DestroyDhcpChild (Nic->Handle, ChildHandle, Dhcp4);
  return EFI_SUCCESS;
}

//
// ============================================================
// Test T7.2: DHCP Lease Verify
// Verify current DHCP lease is valid.
//
// PASS: NIC has valid DHCP lease (Dhcp4Bound state)
// WARN: DHCP available but not in Bound state
// FAIL: Cannot access DHCP service
// ============================================================
//
EFI_STATUS
TestL7DhcpLeaseVerify (
  IN  NIC_INFO         *Nic,
  IN  TEST_CONFIG      *Config,
  OUT TEST_RESULT_DATA *Result
  )
{
  EFI_STATUS           Status;
  EFI_HANDLE           ChildHandle;
  EFI_DHCP4_PROTOCOL   *Dhcp4;
  EFI_DHCP4_MODE_DATA  ModeData;

  ChildHandle = NULL;
  Dhcp4       = NULL;

  //
  // Create DHCP4 child
  //
  Status = L7CreateDhcpChild (Nic->Handle, &ChildHandle, &Dhcp4);
  if (EFI_ERROR (Status)) {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Cannot create DHCP4 child: %r", Status);
    return EFI_SUCCESS;
  }

  //
  // Query current DHCP state
  //
  Status = Dhcp4->GetModeData (Dhcp4, &ModeData);
  if (EFI_ERROR (Status)) {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"GetModeData failed: %r", Status);
    L7DestroyDhcpChild (Nic->Handle, ChildHandle, Dhcp4);
    return EFI_SUCCESS;
  }

  UnicodeSPrint (Result->Detail, sizeof (Result->Detail),
                 L"DHCP State: %d  Client: %d.%d.%d.%d  "
                 L"Server: %d.%d.%d.%d  Lease: %ds",
                 ModeData.State,
                 ModeData.ClientAddress.Addr[0],
                 ModeData.ClientAddress.Addr[1],
                 ModeData.ClientAddress.Addr[2],
                 ModeData.ClientAddress.Addr[3],
                 ModeData.ServerAddress.Addr[0],
                 ModeData.ServerAddress.Addr[1],
                 ModeData.ServerAddress.Addr[2],
                 ModeData.ServerAddress.Addr[3],
                 ModeData.LeaseTime);

  if (ModeData.State == Dhcp4Bound) {
    //
    // Check lease time validity
    //
    if (ModeData.LeaseTime == 0) {
      Result->StatusCode = TEST_RESULT_WARN;
      UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                     L"DHCP bound but lease time is 0 (expired?)");
      UnicodeSPrint (Result->Suggestion, sizeof (Result->Suggestion),
                     L"Renew DHCP lease");
    } else {
      BOOLEAN  AllZero = TRUE;
      UINTN    I;

      for (I = 0; I < 4; I++) {
        if (ModeData.ClientAddress.Addr[I] != 0) {
          AllZero = FALSE;
          break;
        }
      }

      if (AllZero) {
        Result->StatusCode = TEST_RESULT_WARN;
        UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                       L"DHCP bound but client address is 0.0.0.0");
      } else {
        Result->StatusCode = TEST_RESULT_PASS;
        UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                       L"Valid DHCP lease: %d.%d.%d.%d (lease=%ds)",
                       ModeData.ClientAddress.Addr[0],
                       ModeData.ClientAddress.Addr[1],
                       ModeData.ClientAddress.Addr[2],
                       ModeData.ClientAddress.Addr[3],
                       ModeData.LeaseTime);
      }
    }
  } else if (ModeData.State == Dhcp4Init || ModeData.State == Dhcp4InitReboot) {
    //
    // DHCP configured but not started
    //
    Result->StatusCode = TEST_RESULT_WARN;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"DHCP in init state (%d), no active lease",
                   ModeData.State);
    UnicodeSPrint (Result->Suggestion, sizeof (Result->Suggestion),
                   L"Run DHCP Discover test first to obtain a lease");
  } else if (ModeData.State == Dhcp4Renewing || ModeData.State == Dhcp4Rebinding) {
    Result->StatusCode = TEST_RESULT_WARN;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"DHCP lease is being renewed (state=%d)",
                   ModeData.State);
  } else if (ModeData.State == Dhcp4Stopped) {
    //
    // Check if NIC has a static IP configured instead
    //
    if (Nic->HasIpConfig) {
      Result->StatusCode = TEST_RESULT_PASS;
      UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                     L"No DHCP lease, using static IP: %d.%d.%d.%d",
                     Nic->Ipv4Address.Addr[0],
                     Nic->Ipv4Address.Addr[1],
                     Nic->Ipv4Address.Addr[2],
                     Nic->Ipv4Address.Addr[3]);
    } else {
      Result->StatusCode = TEST_RESULT_WARN;
      UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                     L"DHCP stopped, no active lease or static IP");
      UnicodeSPrint (Result->Suggestion, sizeof (Result->Suggestion),
                     L"Configure IP or run DHCP discover");
    }
  } else {
    Result->StatusCode = TEST_RESULT_WARN;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"DHCP in unexpected state: %d", ModeData.State);
  }

  L7DestroyDhcpChild (Nic->Handle, ChildHandle, Dhcp4);
  return EFI_SUCCESS;
}

//
// ============================================================
// Test T7.3: DNS Resolve
// Resolve a hostname via DNS query (forward lookup).
//
// PASS: Hostname resolved to at least one IP address
// WARN: DNS configured but no response
// FAIL: DNS service unavailable or error
// ============================================================
//
EFI_STATUS
TestL7DnsResolve (
  IN  NIC_INFO         *Nic,
  IN  TEST_CONFIG      *Config,
  OUT TEST_RESULT_DATA *Result
  )
{
  EFI_STATUS                  Status;
  EFI_HANDLE                  ChildHandle;
  EFI_DNS4_PROTOCOL           *Dns4;
  EFI_DNS4_CONFIG_DATA        DnsCfg;
  EFI_DNS4_COMPLETION_TOKEN   Token;
  EFI_IPv4_ADDRESS            DnsServer;
  UINT32                      TimeoutMs;
  UINTN                       PollCount;
  UINTN                       MaxPoll;

  ChildHandle = NULL;
  Dns4        = NULL;
  ZeroMem (&Token, sizeof (Token));

  //
  // Create DNS4 child
  //
  Status = L7CreateDnsChild (Nic->Handle, &ChildHandle, &Dns4);
  if (EFI_ERROR (Status)) {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Cannot create DNS4 child: %r", Status);
    UnicodeSPrint (Result->Suggestion, sizeof (Result->Suggestion),
                   L"Verify DNS4 service binding is available");
    return EFI_SUCCESS;
  }

  //
  // Use companion IP (gateway) as DNS server
  //
  if (Config->CompanionIp.Addr[0] != 0 || Config->CompanionIp.Addr[1] != 0 ||
      Config->CompanionIp.Addr[2] != 0 || Config->CompanionIp.Addr[3] != 0) {
    CopyMem (&DnsServer, &Config->CompanionIp, sizeof (EFI_IPv4_ADDRESS));
  } else if (Config->Gateway.Addr[0] != 0 || Config->Gateway.Addr[1] != 0 ||
             Config->Gateway.Addr[2] != 0 || Config->Gateway.Addr[3] != 0) {
    CopyMem (&DnsServer, &Config->Gateway, sizeof (EFI_IPv4_ADDRESS));
  } else {
    CopyMem (&DnsServer, &Nic->Gateway, sizeof (EFI_IPv4_ADDRESS));
  }

  //
  // Configure DNS4
  //
  ZeroMem (&DnsCfg, sizeof (DnsCfg));
  DnsCfg.DnsServerListCount = 1;
  DnsCfg.DnsServerList      = &DnsServer;
  DnsCfg.UseDefaultSetting  = FALSE;
  DnsCfg.EnableDnsCache     = TRUE;
  DnsCfg.Protocol           = IP_PROTO_UDP;
  CopyMem (&DnsCfg.StationIp, &Nic->Ipv4Address, sizeof (EFI_IPv4_ADDRESS));
  CopyMem (&DnsCfg.SubnetMask, &Nic->SubnetMask, sizeof (EFI_IPv4_ADDRESS));
  DnsCfg.LocalPort          = 0;
  DnsCfg.RetryCount         = 2;
  DnsCfg.RetryInterval      = 3;

  Status = Dns4->Configure (Dns4, &DnsCfg);
  if (EFI_ERROR (Status)) {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"DNS4 Configure failed: %r", Status);
    UnicodeSPrint (Result->Detail, sizeof (Result->Detail),
                   L"DNS Server: %d.%d.%d.%d  Station: %d.%d.%d.%d",
                   DnsServer.Addr[0], DnsServer.Addr[1],
                   DnsServer.Addr[2], DnsServer.Addr[3],
                   Nic->Ipv4Address.Addr[0], Nic->Ipv4Address.Addr[1],
                   Nic->Ipv4Address.Addr[2], Nic->Ipv4Address.Addr[3]);
    L7DestroyDnsChild (Nic->Handle, ChildHandle, Dns4);
    return EFI_SUCCESS;
  }

  //
  // Create completion event
  //
  Status = gBS->CreateEvent (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  L7NotifyStub,
                  NULL,
                  &Token.Event
                  );
  if (EFI_ERROR (Status)) {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Cannot create event: %r", Status);
    L7DestroyDnsChild (Nic->Handle, ChildHandle, Dns4);
    return EFI_SUCCESS;
  }

  Token.Status       = EFI_NOT_READY;
  Token.RetryCount   = 0;
  Token.RetryInterval = 0;
  Token.RspData.H2AData = NULL;

  //
  // Resolve "companion.test.ddtsoft.local"
  //
  Result->PacketsSent = 1;

  Status = Dns4->HostNameToIp (Dns4, L"companion.test.ddtsoft.local", &Token);
  if (EFI_ERROR (Status)) {
    //
    // Synchronous failure
    //
    if (Status == EFI_NOT_FOUND) {
      Result->StatusCode = TEST_RESULT_WARN;
      UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                     L"DNS query returned NOT_FOUND for test hostname");
      UnicodeSPrint (Result->Suggestion, sizeof (Result->Suggestion),
                     L"Ensure companion DNS server is running");
    } else {
      Result->StatusCode = TEST_RESULT_FAIL;
      UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                     L"DNS HostNameToIp failed: %r", Status);
    }

    gBS->CloseEvent (Token.Event);
    L7DestroyDnsChild (Nic->Handle, ChildHandle, Dns4);
    return EFI_SUCCESS;
  }

  //
  // Poll for completion
  //
  TimeoutMs = (Config->TimeoutMs > 0) ? Config->TimeoutMs : 5000;
  MaxPoll   = TimeoutMs;

  for (PollCount = 0; PollCount < MaxPoll; PollCount++) {
    if (Token.Status != EFI_NOT_READY) {
      break;
    }
    Dns4->Poll (Dns4);
    gBS->Stall (1000);  // 1ms
  }

  //
  // Cancel if still pending
  //
  if (Token.Status == EFI_NOT_READY) {
    Dns4->Cancel (Dns4, &Token);
    Result->StatusCode = TEST_RESULT_WARN;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"DNS query timed out (%dms)", TimeoutMs);
    UnicodeSPrint (Result->Suggestion, sizeof (Result->Suggestion),
                   L"Check DNS server at %d.%d.%d.%d",
                   DnsServer.Addr[0], DnsServer.Addr[1],
                   DnsServer.Addr[2], DnsServer.Addr[3]);
    gBS->CloseEvent (Token.Event);
    L7DestroyDnsChild (Nic->Handle, ChildHandle, Dns4);
    return EFI_SUCCESS;
  }

  //
  // Process result
  //
  if (!EFI_ERROR (Token.Status) && Token.RspData.H2AData != NULL) {
    DNS_HOST_TO_ADDR_DATA  *H2A = Token.RspData.H2AData;

    Result->PacketsReceived = 1;

    if (H2A->IpCount > 0 && H2A->IpList != NULL) {
      Result->StatusCode = TEST_RESULT_PASS;
      UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                     L"DNS resolved: %d.%d.%d.%d (%d addresses)",
                     H2A->IpList[0].Addr[0],
                     H2A->IpList[0].Addr[1],
                     H2A->IpList[0].Addr[2],
                     H2A->IpList[0].Addr[3],
                     H2A->IpCount);
      UnicodeSPrint (Result->Detail, sizeof (Result->Detail),
                     L"Hostname: companion.test.ddtsoft.local → %d.%d.%d.%d  "
                     L"DNS server: %d.%d.%d.%d",
                     H2A->IpList[0].Addr[0], H2A->IpList[0].Addr[1],
                     H2A->IpList[0].Addr[2], H2A->IpList[0].Addr[3],
                     DnsServer.Addr[0], DnsServer.Addr[1],
                     DnsServer.Addr[2], DnsServer.Addr[3]);

      FreePool (H2A->IpList);
    } else {
      Result->StatusCode = TEST_RESULT_WARN;
      UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                     L"DNS returned empty result (0 addresses)");
    }

    FreePool (H2A);
  } else if (Token.Status == EFI_NOT_FOUND) {
    Result->StatusCode = TEST_RESULT_WARN;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"DNS: hostname not found");
    UnicodeSPrint (Result->Suggestion, sizeof (Result->Suggestion),
                   L"Ensure DNS server has record for test hostname");
  } else {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"DNS query failed: %r", Token.Status);
  }

  gBS->CloseEvent (Token.Event);
  L7DestroyDnsChild (Nic->Handle, ChildHandle, Dns4);
  return EFI_SUCCESS;
}

//
// ============================================================
// Test T7.4: DNS Reverse
// Perform reverse DNS lookup on an IP address.
//
// PASS: Reverse lookup returned a hostname
// WARN: Lookup returned UNSUPPORTED or no result
// FAIL: DNS service error
// ============================================================
//
EFI_STATUS
TestL7DnsReverse (
  IN  NIC_INFO         *Nic,
  IN  TEST_CONFIG      *Config,
  OUT TEST_RESULT_DATA *Result
  )
{
  EFI_STATUS                  Status;
  EFI_HANDLE                  ChildHandle;
  EFI_DNS4_PROTOCOL           *Dns4;
  EFI_DNS4_CONFIG_DATA        DnsCfg;
  EFI_DNS4_COMPLETION_TOKEN   Token;
  EFI_IPv4_ADDRESS            DnsServer;
  EFI_IPv4_ADDRESS            LookupIp;
  UINT32                      TimeoutMs;
  UINTN                       PollCount;
  UINTN                       MaxPoll;

  ChildHandle = NULL;
  Dns4        = NULL;
  ZeroMem (&Token, sizeof (Token));

  //
  // Determine IP to reverse-lookup (use companion or gateway)
  //
  if (Config->CompanionIp.Addr[0] != 0 || Config->CompanionIp.Addr[1] != 0 ||
      Config->CompanionIp.Addr[2] != 0 || Config->CompanionIp.Addr[3] != 0) {
    CopyMem (&LookupIp, &Config->CompanionIp, sizeof (EFI_IPv4_ADDRESS));
  } else if (Config->Gateway.Addr[0] != 0 || Config->Gateway.Addr[1] != 0 ||
             Config->Gateway.Addr[2] != 0 || Config->Gateway.Addr[3] != 0) {
    CopyMem (&LookupIp, &Config->Gateway, sizeof (EFI_IPv4_ADDRESS));
  } else {
    CopyMem (&LookupIp, &Nic->Gateway, sizeof (EFI_IPv4_ADDRESS));
  }

  CopyMem (&DnsServer, &LookupIp, sizeof (EFI_IPv4_ADDRESS));

  //
  // Create DNS4 child
  //
  Status = L7CreateDnsChild (Nic->Handle, &ChildHandle, &Dns4);
  if (EFI_ERROR (Status)) {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Cannot create DNS4 child: %r", Status);
    return EFI_SUCCESS;
  }

  //
  // Configure DNS4
  //
  ZeroMem (&DnsCfg, sizeof (DnsCfg));
  DnsCfg.DnsServerListCount = 1;
  DnsCfg.DnsServerList      = &DnsServer;
  DnsCfg.UseDefaultSetting  = FALSE;
  DnsCfg.EnableDnsCache     = TRUE;
  DnsCfg.Protocol           = IP_PROTO_UDP;
  CopyMem (&DnsCfg.StationIp, &Nic->Ipv4Address, sizeof (EFI_IPv4_ADDRESS));
  CopyMem (&DnsCfg.SubnetMask, &Nic->SubnetMask, sizeof (EFI_IPv4_ADDRESS));
  DnsCfg.LocalPort          = 0;
  DnsCfg.RetryCount         = 2;
  DnsCfg.RetryInterval      = 3;

  Status = Dns4->Configure (Dns4, &DnsCfg);
  if (EFI_ERROR (Status)) {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"DNS4 Configure failed: %r", Status);
    L7DestroyDnsChild (Nic->Handle, ChildHandle, Dns4);
    return EFI_SUCCESS;
  }

  //
  // Create completion event
  //
  Status = gBS->CreateEvent (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  L7NotifyStub,
                  NULL,
                  &Token.Event
                  );
  if (EFI_ERROR (Status)) {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Cannot create event: %r", Status);
    L7DestroyDnsChild (Nic->Handle, ChildHandle, Dns4);
    return EFI_SUCCESS;
  }

  Token.Status        = EFI_NOT_READY;
  Token.RetryCount    = 0;
  Token.RetryInterval = 0;
  Token.RspData.A2HData = NULL;

  //
  // Perform reverse lookup
  //
  Result->PacketsSent = 1;

  Status = Dns4->IpToHostName (Dns4, LookupIp, &Token);
  if (EFI_ERROR (Status)) {
    if (Status == EFI_UNSUPPORTED) {
      Result->StatusCode = TEST_RESULT_WARN;
      UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                     L"Reverse DNS not supported by this DNS implementation");
    } else {
      Result->StatusCode = TEST_RESULT_FAIL;
      UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                     L"IpToHostName failed: %r", Status);
    }

    gBS->CloseEvent (Token.Event);
    L7DestroyDnsChild (Nic->Handle, ChildHandle, Dns4);
    return EFI_SUCCESS;
  }

  //
  // Poll for completion
  //
  TimeoutMs = (Config->TimeoutMs > 0) ? Config->TimeoutMs : 5000;
  MaxPoll   = TimeoutMs;

  for (PollCount = 0; PollCount < MaxPoll; PollCount++) {
    if (Token.Status != EFI_NOT_READY) {
      break;
    }
    Dns4->Poll (Dns4);
    gBS->Stall (1000);
  }

  //
  // Cancel if still pending
  //
  if (Token.Status == EFI_NOT_READY) {
    Dns4->Cancel (Dns4, &Token);
    Result->StatusCode = TEST_RESULT_WARN;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Reverse DNS timed out (%dms)", TimeoutMs);
    gBS->CloseEvent (Token.Event);
    L7DestroyDnsChild (Nic->Handle, ChildHandle, Dns4);
    return EFI_SUCCESS;
  }

  //
  // Process result
  //
  if (!EFI_ERROR (Token.Status) && Token.RspData.A2HData != NULL) {
    DNS_ADDR_TO_HOST_DATA  *A2H = Token.RspData.A2HData;

    Result->PacketsReceived = 1;

    if (A2H->HostName != NULL && A2H->HostName[0] != L'\0') {
      Result->StatusCode = TEST_RESULT_PASS;
      UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                     L"Reverse DNS: %d.%d.%d.%d → %.60s",
                     LookupIp.Addr[0], LookupIp.Addr[1],
                     LookupIp.Addr[2], LookupIp.Addr[3],
                     A2H->HostName);
      FreePool (A2H->HostName);
    } else {
      Result->StatusCode = TEST_RESULT_WARN;
      UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                     L"Reverse DNS returned empty hostname");
    }

    FreePool (A2H);
  } else if (Token.Status == EFI_NOT_FOUND) {
    Result->StatusCode = TEST_RESULT_WARN;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"No PTR record for %d.%d.%d.%d",
                   LookupIp.Addr[0], LookupIp.Addr[1],
                   LookupIp.Addr[2], LookupIp.Addr[3]);
    UnicodeSPrint (Result->Suggestion, sizeof (Result->Suggestion),
                   L"Ensure DNS server has reverse records");
  } else {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Reverse DNS failed: %r", Token.Status);
  }

  gBS->CloseEvent (Token.Event);
  L7DestroyDnsChild (Nic->Handle, ChildHandle, Dns4);
  return EFI_SUCCESS;
}

//
// ============================================================
// Test T7.5: HTTP GET
// Perform HTTP GET request to target.
//
// PASS: Received HTTP 200 OK response
// WARN: Received non-200 response
// FAIL: HTTP request failed or timed out
// ============================================================
//
EFI_STATUS
TestL7HttpGet (
  IN  NIC_INFO         *Nic,
  IN  TEST_CONFIG      *Config,
  OUT TEST_RESULT_DATA *Result
  )
{
  EFI_STATUS              Status;
  EFI_HANDLE              ChildHandle;
  EFI_HTTP_PROTOCOL       *Http;
  EFI_HTTP_CONFIG_DATA    HttpCfg;
  EFI_HTTPv4_ACCESS_POINT Ipv4Node;
  EFI_HTTP_TOKEN          ReqToken;
  EFI_HTTP_TOKEN          RspToken;
  EFI_HTTP_MESSAGE        ReqMsg;
  EFI_HTTP_MESSAGE        RspMsg;
  EFI_HTTP_REQUEST_DATA   ReqData;
  EFI_HTTP_RESPONSE_DATA  RspData;
  EFI_HTTP_HEADER         ReqHeaders[2];
  CHAR16                  UrlBuf[128];
  CHAR8                   HostBuf[32];
  UINT8                   BodyBuf[1024];
  UINT32                  TimeoutMs;
  UINTN                   PollCount;
  UINTN                   MaxPoll;
  UINT16                  Port;

  ChildHandle = NULL;
  Http        = NULL;
  ZeroMem (&ReqToken, sizeof (ReqToken));
  ZeroMem (&RspToken, sizeof (RspToken));

  //
  // Create HTTP child
  //
  Status = L7CreateHttpChild (Nic->Handle, &ChildHandle, &Http);
  if (EFI_ERROR (Status)) {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Cannot create HTTP child: %r", Status);
    UnicodeSPrint (Result->Suggestion, sizeof (Result->Suggestion),
                   L"Verify HTTP service binding is available (NetworkPkg)");
    return EFI_SUCCESS;
  }

  //
  // Configure HTTP
  //
  Port = (Config->TargetPort > 0) ? Config->TargetPort : 80;

  ZeroMem (&Ipv4Node, sizeof (Ipv4Node));
  Ipv4Node.UseDefaultAddress = FALSE;
  CopyMem (&Ipv4Node.LocalAddress, &Nic->Ipv4Address, sizeof (EFI_IPv4_ADDRESS));
  CopyMem (&Ipv4Node.LocalSubnet, &Nic->SubnetMask, sizeof (EFI_IPv4_ADDRESS));
  Ipv4Node.LocalPort = 0;

  ZeroMem (&HttpCfg, sizeof (HttpCfg));
  HttpCfg.HttpVersion         = HttpVersion11;
  HttpCfg.TimeOutMillisec     = 10000;
  HttpCfg.LocalAddressIsIPv6  = FALSE;
  HttpCfg.AccessPoint.IPv4Node = &Ipv4Node;

  Status = Http->Configure (Http, &HttpCfg);
  if (EFI_ERROR (Status)) {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"HTTP Configure failed: %r", Status);
    L7DestroyHttpChild (Nic->Handle, ChildHandle, Http);
    return EFI_SUCCESS;
  }

  //
  // Build URL: http://<TargetIp>:<Port>/
  //
  if (Port == 80) {
    UnicodeSPrint (UrlBuf, sizeof (UrlBuf),
                   L"http://%d.%d.%d.%d/",
                   Config->TargetIp.Addr[0], Config->TargetIp.Addr[1],
                   Config->TargetIp.Addr[2], Config->TargetIp.Addr[3]);
  } else {
    UnicodeSPrint (UrlBuf, sizeof (UrlBuf),
                   L"http://%d.%d.%d.%d:%d/",
                   Config->TargetIp.Addr[0], Config->TargetIp.Addr[1],
                   Config->TargetIp.Addr[2], Config->TargetIp.Addr[3],
                   Port);
  }

  //
  // Build Host header value (ASCII)
  //
  AsciiSPrint (HostBuf, sizeof (HostBuf),
               "%d.%d.%d.%d",
               Config->TargetIp.Addr[0], Config->TargetIp.Addr[1],
               Config->TargetIp.Addr[2], Config->TargetIp.Addr[3]);

  //
  // Set up request headers
  //
  ReqHeaders[0].FieldName  = "Host";
  ReqHeaders[0].FieldValue = HostBuf;
  ReqHeaders[1].FieldName  = "User-Agent";
  ReqHeaders[1].FieldValue = "DDTSoft/1.0";

  //
  // Set up request
  //
  ZeroMem (&ReqData, sizeof (ReqData));
  ReqData.Method = HttpMethodGet;
  ReqData.Url    = UrlBuf;

  ZeroMem (&ReqMsg, sizeof (ReqMsg));
  ReqMsg.Data.Request = &ReqData;
  ReqMsg.HeaderCount  = 2;
  ReqMsg.Headers      = ReqHeaders;
  ReqMsg.BodyLength   = 0;
  ReqMsg.Body         = NULL;

  //
  // Create request completion event
  //
  Status = gBS->CreateEvent (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  L7NotifyStub,
                  NULL,
                  &ReqToken.Event
                  );
  if (EFI_ERROR (Status)) {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Cannot create event: %r", Status);
    L7DestroyHttpChild (Nic->Handle, ChildHandle, Http);
    return EFI_SUCCESS;
  }

  ReqToken.Status  = EFI_NOT_READY;
  ReqToken.Message = &ReqMsg;

  //
  // Send HTTP request
  //
  Result->PacketsSent = 1;

  Status = Http->Request (Http, &ReqToken);
  if (EFI_ERROR (Status)) {
    if (Status == EFI_ACCESS_DENIED) {
      //
      // Some UEFI firmware HTTP drivers return Access Denied
      // due to platform security policy (Secure Boot restrictions,
      // TLS-only mode, etc). This is a firmware limitation, not a test failure.
      //
      Result->StatusCode = TEST_RESULT_WARN;
      UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                     L"HTTP blocked by firmware security policy");
      UnicodeSPrint (Result->Detail, sizeof (Result->Detail),
                     L"EFI_HTTP_PROTOCOL returned EFI_ACCESS_DENIED. "
                     L"This platform may require HTTPS or restrict HTTP.");
      UnicodeSPrint (Result->Suggestion, sizeof (Result->Suggestion),
                     L"Try TCP-level HTTP test instead (L4 TCP Connect port 80)");
    } else {
      Result->StatusCode = TEST_RESULT_FAIL;
      UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                     L"HTTP Request failed: %r", Status);
      UnicodeSPrint (Result->Suggestion, sizeof (Result->Suggestion),
                     L"Verify HTTP server is running at target IP");
    }
    gBS->CloseEvent (ReqToken.Event);
    L7DestroyHttpChild (Nic->Handle, ChildHandle, Http);
    return EFI_SUCCESS;
  }

  //
  // Poll for request completion
  //
  TimeoutMs = (Config->TimeoutMs > 0) ? Config->TimeoutMs : 10000;
  MaxPoll   = TimeoutMs;

  for (PollCount = 0; PollCount < MaxPoll; PollCount++) {
    if (ReqToken.Status != EFI_NOT_READY) {
      break;
    }
    Http->Poll (Http);
    gBS->Stall (1000);
  }

  if (ReqToken.Status == EFI_NOT_READY) {
    Http->Cancel (Http, &ReqToken);
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"HTTP request timed out (%dms)", TimeoutMs);
    gBS->CloseEvent (ReqToken.Event);
    L7DestroyHttpChild (Nic->Handle, ChildHandle, Http);
    return EFI_SUCCESS;
  }

  if (EFI_ERROR (ReqToken.Status)) {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"HTTP request error: %r", ReqToken.Status);
    gBS->CloseEvent (ReqToken.Event);
    L7DestroyHttpChild (Nic->Handle, ChildHandle, Http);
    return EFI_SUCCESS;
  }

  //
  // Now receive the response
  //
  Status = gBS->CreateEvent (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  L7NotifyStub,
                  NULL,
                  &RspToken.Event
                  );
  if (EFI_ERROR (Status)) {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Cannot create response event: %r", Status);
    gBS->CloseEvent (ReqToken.Event);
    L7DestroyHttpChild (Nic->Handle, ChildHandle, Http);
    return EFI_SUCCESS;
  }

  ZeroMem (&RspData, sizeof (RspData));
  ZeroMem (&RspMsg, sizeof (RspMsg));
  RspMsg.Data.Response = &RspData;
  RspMsg.HeaderCount   = 0;
  RspMsg.Headers       = NULL;
  RspMsg.BodyLength    = sizeof (BodyBuf);
  RspMsg.Body          = BodyBuf;

  RspToken.Status  = EFI_NOT_READY;
  RspToken.Message = &RspMsg;

  Status = Http->Response (Http, &RspToken);
  if (EFI_ERROR (Status)) {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"HTTP Response call failed: %r", Status);
    gBS->CloseEvent (ReqToken.Event);
    gBS->CloseEvent (RspToken.Event);
    L7DestroyHttpChild (Nic->Handle, ChildHandle, Http);
    return EFI_SUCCESS;
  }

  //
  // Poll for response
  //
  for (PollCount = 0; PollCount < MaxPoll; PollCount++) {
    if (RspToken.Status != EFI_NOT_READY) {
      break;
    }
    Http->Poll (Http);
    gBS->Stall (1000);
  }

  if (RspToken.Status == EFI_NOT_READY) {
    Http->Cancel (Http, &RspToken);
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"HTTP response timed out");
    gBS->CloseEvent (ReqToken.Event);
    gBS->CloseEvent (RspToken.Event);
    L7DestroyHttpChild (Nic->Handle, ChildHandle, Http);
    return EFI_SUCCESS;
  }

  //
  // Process response
  //
  Result->PacketsReceived = 1;
  Result->BytesReceived   = RspMsg.BodyLength;

  if (RspData.StatusCode == HTTP_STATUS_200_OK) {
    Result->StatusCode = TEST_RESULT_PASS;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"HTTP 200 OK (%d bytes body)", RspMsg.BodyLength);
    UnicodeSPrint (Result->Detail, sizeof (Result->Detail),
                   L"URL: %.80s  Headers: %d",
                   UrlBuf, RspMsg.HeaderCount);
  } else if (RspData.StatusCode >= HTTP_STATUS_200_OK &&
             RspData.StatusCode <= HTTP_STATUS_206_PARTIAL_CONTENT) {
    Result->StatusCode = TEST_RESULT_PASS;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"HTTP 2xx success (code=%d, %d bytes)",
                   RspData.StatusCode, RspMsg.BodyLength);
  } else if (RspData.StatusCode >= HTTP_STATUS_300_MULTIPLE_CHOICES &&
             RspData.StatusCode <= HTTP_STATUS_308_PERMANENT_REDIRECT) {
    Result->StatusCode = TEST_RESULT_WARN;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"HTTP 3xx redirect (code=%d)", RspData.StatusCode);
  } else if (RspData.StatusCode >= HTTP_STATUS_400_BAD_REQUEST) {
    Result->StatusCode = TEST_RESULT_WARN;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"HTTP error response (code=%d)", RspData.StatusCode);
  } else {
    Result->StatusCode = TEST_RESULT_WARN;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"HTTP status=%d, body=%d bytes",
                   RspData.StatusCode, RspMsg.BodyLength);
  }

  //
  // Free response headers if allocated by HTTP driver
  //
  if (RspMsg.Headers != NULL) {
    FreePool (RspMsg.Headers);
  }

  gBS->CloseEvent (ReqToken.Event);
  gBS->CloseEvent (RspToken.Event);
  L7DestroyHttpChild (Nic->Handle, ChildHandle, Http);
  return EFI_SUCCESS;
}

//
// ============================================================
// Test T7.6: HTTP Status Codes
// Test HTTP response status code handling for multiple paths.
//
// PASS: Server responds with correct status codes
// WARN: Some paths returned unexpected codes
// FAIL: HTTP service unavailable
// ============================================================
//
EFI_STATUS
TestL7HttpStatusCodes (
  IN  NIC_INFO         *Nic,
  IN  TEST_CONFIG      *Config,
  OUT TEST_RESULT_DATA *Result
  )
{
  EFI_STATUS              Status;
  EFI_HANDLE              ChildHandle;
  EFI_HTTP_PROTOCOL       *Http;
  EFI_HTTP_CONFIG_DATA    HttpCfg;
  EFI_HTTPv4_ACCESS_POINT Ipv4Node;
  EFI_HTTP_TOKEN          ReqToken;
  EFI_HTTP_TOKEN          RspToken;
  EFI_HTTP_MESSAGE        ReqMsg;
  EFI_HTTP_MESSAGE        RspMsg;
  EFI_HTTP_REQUEST_DATA   ReqData;
  EFI_HTTP_RESPONSE_DATA  RspData;
  EFI_HTTP_HEADER         ReqHeaders[2];
  CHAR16                  UrlBuf[128];
  CHAR8                   HostBuf[32];
  UINT8                   BodyBuf[512];
  UINT32                  TimeoutMs;
  UINTN                   PollCount;
  UINTN                   MaxPoll;
  UINT16                  Port;
  UINTN                   PathIdx;
  UINTN                   SuccessCount;
  UINTN                   TotalPaths;

  //
  // Test paths and expected status codes
  //
  CONST CHAR16  *TestPaths[]  = { L"/",        L"/nonexistent404", L"/status" };
  UINTN         ExpectedClass[] = { 2,           4,                  2 };  // 2xx, 4xx, 2xx
  UINTN         GotCodes[3];

  TotalPaths   = 3;
  SuccessCount = 0;
  ZeroMem (GotCodes, sizeof (GotCodes));

  ChildHandle = NULL;
  Http        = NULL;

  Port = (Config->TargetPort > 0) ? Config->TargetPort : 80;
  TimeoutMs = (Config->TimeoutMs > 0) ? Config->TimeoutMs : 10000;

  AsciiSPrint (HostBuf, sizeof (HostBuf),
               "%d.%d.%d.%d",
               Config->TargetIp.Addr[0], Config->TargetIp.Addr[1],
               Config->TargetIp.Addr[2], Config->TargetIp.Addr[3]);

  for (PathIdx = 0; PathIdx < TotalPaths; PathIdx++) {
    //
    // Create a fresh HTTP child for each request
    //
    Status = L7CreateHttpChild (Nic->Handle, &ChildHandle, &Http);
    if (EFI_ERROR (Status)) {
      continue;
    }

    //
    // Configure
    //
    ZeroMem (&Ipv4Node, sizeof (Ipv4Node));
    Ipv4Node.UseDefaultAddress = FALSE;
    CopyMem (&Ipv4Node.LocalAddress, &Nic->Ipv4Address, sizeof (EFI_IPv4_ADDRESS));
    CopyMem (&Ipv4Node.LocalSubnet, &Nic->SubnetMask, sizeof (EFI_IPv4_ADDRESS));
    Ipv4Node.LocalPort = 0;

    ZeroMem (&HttpCfg, sizeof (HttpCfg));
    HttpCfg.HttpVersion          = HttpVersion11;
    HttpCfg.TimeOutMillisec      = 10000;
    HttpCfg.LocalAddressIsIPv6   = FALSE;
    HttpCfg.AccessPoint.IPv4Node = &Ipv4Node;

    Status = Http->Configure (Http, &HttpCfg);
    if (EFI_ERROR (Status)) {
      L7DestroyHttpChild (Nic->Handle, ChildHandle, Http);
      continue;
    }

    //
    // Build URL
    //
    if (Port == 80) {
      UnicodeSPrint (UrlBuf, sizeof (UrlBuf),
                     L"http://%d.%d.%d.%d%s",
                     Config->TargetIp.Addr[0], Config->TargetIp.Addr[1],
                     Config->TargetIp.Addr[2], Config->TargetIp.Addr[3],
                     TestPaths[PathIdx]);
    } else {
      UnicodeSPrint (UrlBuf, sizeof (UrlBuf),
                     L"http://%d.%d.%d.%d:%d%s",
                     Config->TargetIp.Addr[0], Config->TargetIp.Addr[1],
                     Config->TargetIp.Addr[2], Config->TargetIp.Addr[3],
                     Port, TestPaths[PathIdx]);
    }

    //
    // Build and send request
    //
    ReqHeaders[0].FieldName  = "Host";
    ReqHeaders[0].FieldValue = HostBuf;
    ReqHeaders[1].FieldName  = "User-Agent";
    ReqHeaders[1].FieldValue = "DDTSoft/1.0";

    ZeroMem (&ReqData, sizeof (ReqData));
    ReqData.Method = HttpMethodGet;
    ReqData.Url    = UrlBuf;

    ZeroMem (&ReqMsg, sizeof (ReqMsg));
    ReqMsg.Data.Request = &ReqData;
    ReqMsg.HeaderCount  = 2;
    ReqMsg.Headers      = ReqHeaders;
    ReqMsg.BodyLength   = 0;
    ReqMsg.Body         = NULL;

    ZeroMem (&ReqToken, sizeof (ReqToken));
    Status = gBS->CreateEvent (
                    EVT_NOTIFY_SIGNAL,
                    TPL_CALLBACK,
                    L7NotifyStub,
                    NULL,
                    &ReqToken.Event
                    );
    if (EFI_ERROR (Status)) {
      L7DestroyHttpChild (Nic->Handle, ChildHandle, Http);
      continue;
    }

    ReqToken.Status  = EFI_NOT_READY;
    ReqToken.Message = &ReqMsg;

    Result->PacketsSent++;

    Status = Http->Request (Http, &ReqToken);
    if (EFI_ERROR (Status)) {
      if (Status == EFI_ACCESS_DENIED && PathIdx == 0) {
        //
        // Platform blocks HTTP — abort all paths, report as WARN
        //
        gBS->CloseEvent (ReqToken.Event);
        L7DestroyHttpChild (Nic->Handle, ChildHandle, Http);

        Result->StatusCode = TEST_RESULT_WARN;
        UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                       L"HTTP blocked by firmware security policy");
        UnicodeSPrint (Result->Detail, sizeof (Result->Detail),
                       L"EFI_HTTP_PROTOCOL returned EFI_ACCESS_DENIED. "
                       L"Platform may require HTTPS or restrict HTTP.");
        UnicodeSPrint (Result->Suggestion, sizeof (Result->Suggestion),
                       L"TCP connectivity to port 80 works (see L4 tests)");
        return EFI_SUCCESS;
      }

      gBS->CloseEvent (ReqToken.Event);
      L7DestroyHttpChild (Nic->Handle, ChildHandle, Http);
      continue;
    }

    //
    // Poll for request completion
    //
    MaxPoll = TimeoutMs;
    for (PollCount = 0; PollCount < MaxPoll; PollCount++) {
      if (ReqToken.Status != EFI_NOT_READY) {
        break;
      }
      Http->Poll (Http);
      gBS->Stall (1000);
    }

    if (ReqToken.Status == EFI_NOT_READY || EFI_ERROR (ReqToken.Status)) {
      if (ReqToken.Status == EFI_NOT_READY) {
        Http->Cancel (Http, &ReqToken);
      }
      gBS->CloseEvent (ReqToken.Event);
      L7DestroyHttpChild (Nic->Handle, ChildHandle, Http);
      continue;
    }

    //
    // Receive response
    //
    ZeroMem (&RspToken, sizeof (RspToken));
    Status = gBS->CreateEvent (
                    EVT_NOTIFY_SIGNAL,
                    TPL_CALLBACK,
                    L7NotifyStub,
                    NULL,
                    &RspToken.Event
                    );
    if (EFI_ERROR (Status)) {
      gBS->CloseEvent (ReqToken.Event);
      L7DestroyHttpChild (Nic->Handle, ChildHandle, Http);
      continue;
    }

    ZeroMem (&RspData, sizeof (RspData));
    ZeroMem (&RspMsg, sizeof (RspMsg));
    RspMsg.Data.Response = &RspData;
    RspMsg.HeaderCount   = 0;
    RspMsg.Headers       = NULL;
    RspMsg.BodyLength    = sizeof (BodyBuf);
    RspMsg.Body          = BodyBuf;

    RspToken.Status  = EFI_NOT_READY;
    RspToken.Message = &RspMsg;

    Status = Http->Response (Http, &RspToken);
    if (EFI_ERROR (Status)) {
      gBS->CloseEvent (ReqToken.Event);
      gBS->CloseEvent (RspToken.Event);
      L7DestroyHttpChild (Nic->Handle, ChildHandle, Http);
      continue;
    }

    for (PollCount = 0; PollCount < MaxPoll; PollCount++) {
      if (RspToken.Status != EFI_NOT_READY) {
        break;
      }
      Http->Poll (Http);
      gBS->Stall (1000);
    }

    if (RspToken.Status == EFI_NOT_READY) {
      Http->Cancel (Http, &RspToken);
    }

    if (!EFI_ERROR (RspToken.Status)) {
      Result->PacketsReceived++;
      Result->BytesReceived += RspMsg.BodyLength;

      GotCodes[PathIdx] = RspData.StatusCode;

      //
      // Check if response class matches expected
      //
      UINTN  GotClass = 0;
      if (RspData.StatusCode >= HTTP_STATUS_200_OK &&
          RspData.StatusCode <= HTTP_STATUS_206_PARTIAL_CONTENT) {
        GotClass = 2;
      } else if (RspData.StatusCode >= HTTP_STATUS_300_MULTIPLE_CHOICES &&
                 RspData.StatusCode <= HTTP_STATUS_308_PERMANENT_REDIRECT) {
        GotClass = 3;
      } else if (RspData.StatusCode >= HTTP_STATUS_400_BAD_REQUEST &&
                 RspData.StatusCode <= HTTP_STATUS_429_TOO_MANY_REQUESTS) {
        GotClass = 4;
      } else if (RspData.StatusCode >= HTTP_STATUS_500_INTERNAL_SERVER_ERROR) {
        GotClass = 5;
      }

      if (GotClass == ExpectedClass[PathIdx]) {
        SuccessCount++;
      }

      //
      // Free response headers
      //
      if (RspMsg.Headers != NULL) {
        FreePool (RspMsg.Headers);
      }
    }

    gBS->CloseEvent (ReqToken.Event);
    gBS->CloseEvent (RspToken.Event);
    L7DestroyHttpChild (Nic->Handle, ChildHandle, Http);

    //
    // Brief delay between requests
    //
    gBS->Stall (50000);  // 50ms
  }

  //
  // Evaluate results
  //
  if (SuccessCount == TotalPaths) {
    Result->StatusCode = TEST_RESULT_PASS;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"All %d HTTP paths returned expected status classes",
                   TotalPaths);
  } else if (Result->PacketsReceived > 0) {
    if (SuccessCount > 0) {
      Result->StatusCode = TEST_RESULT_WARN;
      UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                     L"%d/%d paths returned expected status codes",
                     SuccessCount, TotalPaths);
    } else {
      Result->StatusCode = TEST_RESULT_WARN;
      UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                     L"HTTP responses received but status codes differ from expected");
    }
  } else {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"No HTTP responses received from server");
    UnicodeSPrint (Result->Suggestion, sizeof (Result->Suggestion),
                   L"Verify HTTP server is running at target IP:%d",
                   Port);
  }

  UnicodeSPrint (Result->Detail, sizeof (Result->Detail),
                 L"/ → %d  /nonexistent404 → %d  /status → %d  "
                 L"Matched: %d/%d",
                 GotCodes[0], GotCodes[1], GotCodes[2],
                 SuccessCount, TotalPaths);

  return EFI_SUCCESS;
}
