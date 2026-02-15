/** @file
  Test registry - test case registration and lookup.
  Maintains a static array of all TEST_DEFINITION entries.
  Provides query functions for filtering by layer, type, etc.
**/

#include <DDTSoftNetTest.h>
#include <OsiLayers.h>
#include <TestCases.h>

//
// Static registry storage
//
STATIC TEST_DEFINITION  mRegistry[MAX_TESTS];
STATIC UINTN            mTestCount = 0;

/**
  Add a single test definition to the registry.
**/
STATIC
VOID
RegAdd (
  IN CHAR16       *Name,
  IN CHAR16       *Description,
  IN OSI_LAYER    Layer,
  IN TEST_TYPE    Type,
  IN UINT32       EstimatedTimeMs,
  IN BOOLEAN      RequiresTarget,
  IN BOOLEAN      NeedSnp,
  IN BOOLEAN      NeedIp4,
  IN BOOLEAN      NeedTcp4,
  IN BOOLEAN      NeedUdp4,
  IN BOOLEAN      NeedDhcp4,
  IN EFI_STATUS   (*Execute)(IN NIC_INFO *, IN TEST_CONFIG *, OUT TEST_RESULT_DATA *)
  )
{
  TEST_DEFINITION  *T;

  if (mTestCount >= MAX_TESTS) {
    return;
  }

  T = &mRegistry[mTestCount];
  T->Name            = Name;
  T->Description     = Description;
  T->Layer           = Layer;
  T->Type            = Type;
  T->EstimatedTimeMs = EstimatedTimeMs;
  T->RequiresTarget  = RequiresTarget;
  T->RequiresIpv6    = FALSE;
  T->IsDestructive   = FALSE;
  T->NeedSnp         = NeedSnp;
  T->NeedMnp         = FALSE;
  T->NeedIp4         = NeedIp4;
  T->NeedTcp4        = NeedTcp4;
  T->NeedUdp4        = NeedUdp4;
  T->NeedDhcp4       = NeedDhcp4;
  T->Execute         = Execute;

  mTestCount++;
}

/**
  Initialize the test registry with all test definitions.
  Called once at startup before any test execution.
**/
VOID
RegInitAllTests (
  VOID
  )
{
  if (mTestCount > 0) {
    return;  // Already initialized
  }

  //
  // ========== Layer 1: Physical (5 tests) ==========
  //
  RegAdd (
    L"NIC Status",
    L"Check NIC state, media presence, and basic readiness",
    OsiLayerPhysical, TestTypeDiscovery, 500,
    FALSE, TRUE, FALSE, FALSE, FALSE, FALSE,
    TestL1NicStatus
    );

  RegAdd (
    L"Link Detect",
    L"Verify physical link is up and media is connected",
    OsiLayerPhysical, TestTypeConnectivity, 1000,
    FALSE, TRUE, FALSE, FALSE, FALSE, FALSE,
    TestL1LinkDetect
    );

  RegAdd (
    L"NIC Init Cycle",
    L"Stop, start, and initialize the NIC to verify stability",
    OsiLayerPhysical, TestTypeCompliance, 3000,
    FALSE, TRUE, FALSE, FALSE, FALSE, FALSE,
    TestL1NicInitCycle
    );

  RegAdd (
    L"Loopback",
    L"Send and receive loopback frame through NIC",
    OsiLayerPhysical, TestTypeConnectivity, 2000,
    FALSE, TRUE, FALSE, FALSE, FALSE, FALSE,
    TestL1Loopback
    );

  RegAdd (
    L"Link Negotiation",
    L"Check auto-negotiation and link speed parameters",
    OsiLayerPhysical, TestTypeDiscovery, 1000,
    FALSE, TRUE, FALSE, FALSE, FALSE, FALSE,
    TestL1LinkNegotiation
    );

  //
  // ========== Layer 2: Data Link (7 tests) ==========
  //
  RegAdd (
    L"MAC Address Valid",
    L"Verify MAC address is valid and non-zero",
    OsiLayerDataLink, TestTypeCompliance, 500,
    FALSE, TRUE, FALSE, FALSE, FALSE, FALSE,
    TestL2MacAddressValid
    );

  RegAdd (
    L"ARP Request/Reply",
    L"Send ARP request and verify reply (gateway/target)",
    OsiLayerDataLink, TestTypeConnectivity, 7000,
    FALSE, TRUE, FALSE, FALSE, FALSE, FALSE,
    TestL2ArpRequestReply
    );

  RegAdd (
    L"ARP Cache",
    L"Check ARP protocol cache entries",
    OsiLayerDataLink, TestTypeDiscovery, 2000,
    FALSE, TRUE, FALSE, FALSE, FALSE, FALSE,
    TestL2ArpCache
    );

  RegAdd (
    L"Broadcast Frame",
    L"Send and verify broadcast Ethernet frame",
    OsiLayerDataLink, TestTypeConnectivity, 2000,
    FALSE, TRUE, FALSE, FALSE, FALSE, FALSE,
    TestL2BroadcastFrame
    );

  RegAdd (
    L"Frame TX/RX",
    L"Transmit and receive raw Ethernet frames",
    OsiLayerDataLink, TestTypeConnectivity, 3000,
    TRUE, TRUE, FALSE, FALSE, FALSE, FALSE,
    TestL2FrameTxRx
    );

  RegAdd (
    L"MTU Detection",
    L"Detect maximum transmission unit size",
    OsiLayerDataLink, TestTypePerformance, 5000,
    TRUE, TRUE, FALSE, FALSE, FALSE, FALSE,
    TestL2MtuDetection
    );

  RegAdd (
    L"Receive Filter",
    L"Test NIC receive filter modes (unicast, multicast, broadcast)",
    OsiLayerDataLink, TestTypeCompliance, 3000,
    FALSE, TRUE, FALSE, FALSE, FALSE, FALSE,
    TestL2ReceiveFilter
    );

  //
  // ========== Layer 3: Network (10 tests) ==========
  //
  RegAdd (
    L"IP Config Check",
    L"Verify IPv4 address, subnet mask, and gateway configuration",
    OsiLayerNetwork, TestTypeDiscovery, 500,
    FALSE, FALSE, TRUE, FALSE, FALSE, FALSE,
    TestL3IpConfigCheck
    );

  RegAdd (
    L"ICMP Echo (Ping)",
    L"Send ICMP echo request and measure round-trip time",
    OsiLayerNetwork, TestTypeConnectivity, 5000,
    TRUE, TRUE, FALSE, FALSE, FALSE, FALSE,
    TestL3IcmpEcho
    );

  RegAdd (
    L"ICMP Sweep",
    L"Ping sweep across subnet to discover live hosts",
    OsiLayerNetwork, TestTypeDiscovery, 30000,
    FALSE, TRUE, FALSE, FALSE, FALSE, FALSE,
    TestL3IcmpSweep
    );

  RegAdd (
    L"TTL/Hop Discovery",
    L"Trace route hops to target using incrementing TTL",
    OsiLayerNetwork, TestTypeDiscovery, 15000,
    TRUE, TRUE, FALSE, FALSE, FALSE, FALSE,
    TestL3TtlHopDiscovery
    );

  RegAdd (
    L"MTU Path Discovery",
    L"Discover path MTU using DF-bit and ICMP responses",
    OsiLayerNetwork, TestTypePerformance, 10000,
    TRUE, TRUE, FALSE, FALSE, FALSE, FALSE,
    TestL3MtuPathDiscovery
    );

  RegAdd (
    L"IP Fragmentation",
    L"Test IP fragmentation and reassembly",
    OsiLayerNetwork, TestTypeCompliance, 5000,
    TRUE, TRUE, FALSE, FALSE, FALSE, FALSE,
    TestL3IpFragmentation
    );

  RegAdd (
    L"IPv6 Neighbor Discovery",
    L"Test IPv6 neighbor discovery protocol",
    OsiLayerNetwork, TestTypeDiscovery, 5000,
    FALSE, TRUE, FALSE, FALSE, FALSE, FALSE,
    TestL3Ipv6Nd
    );

  RegAdd (
    L"IP Header Validation",
    L"Validate IP header fields for correctness",
    OsiLayerNetwork, TestTypeCompliance, 2000,
    FALSE, TRUE, FALSE, FALSE, FALSE, FALSE,
    TestL3IpHeaderValid
    );

  RegAdd (
    L"Routing Table",
    L"Check IP routing table entries",
    OsiLayerNetwork, TestTypeDiscovery, 2000,
    FALSE, FALSE, TRUE, FALSE, FALSE, FALSE,
    TestL3RoutingTable
    );

  RegAdd (
    L"Duplicate IP Detection",
    L"Check for duplicate IP addresses on the network",
    OsiLayerNetwork, TestTypeCompliance, 5000,
    FALSE, TRUE, FALSE, FALSE, FALSE, FALSE,
    TestL3DuplicateIp
    );

  //
  // ========== Layer 4: Transport (8 tests) ==========
  //
  RegAdd (
    L"TCP Connect",
    L"Establish TCP connection to target port",
    OsiLayerTransport, TestTypeConnectivity, 5000,
    TRUE, FALSE, FALSE, TRUE, FALSE, FALSE,
    TestL4TcpConnect
    );

  RegAdd (
    L"TCP Multi-Port",
    L"Test TCP connectivity on multiple ports",
    OsiLayerTransport, TestTypeConnectivity, 15000,
    TRUE, FALSE, FALSE, TRUE, FALSE, FALSE,
    TestL4TcpMultiPort
    );

  RegAdd (
    L"TCP Data Transfer",
    L"Send and receive data over TCP connection",
    OsiLayerTransport, TestTypePerformance, 10000,
    TRUE, FALSE, FALSE, TRUE, FALSE, FALSE,
    TestL4TcpDataTransfer
    );

  RegAdd (
    L"TCP Close",
    L"Test TCP connection graceful close (FIN handshake)",
    OsiLayerTransport, TestTypeCompliance, 5000,
    TRUE, FALSE, FALSE, TRUE, FALSE, FALSE,
    TestL4TcpClose
    );

  RegAdd (
    L"UDP Send/Receive",
    L"Send and receive UDP datagrams",
    OsiLayerTransport, TestTypeConnectivity, 5000,
    TRUE, FALSE, FALSE, FALSE, TRUE, FALSE,
    TestL4UdpSendReceive
    );

  RegAdd (
    L"UDP Multi-Port",
    L"Test UDP on multiple ports",
    OsiLayerTransport, TestTypeConnectivity, 10000,
    TRUE, FALSE, FALSE, FALSE, TRUE, FALSE,
    TestL4UdpMultiPort
    );

  RegAdd (
    L"Port Scan",
    L"Scan common TCP ports on target host",
    OsiLayerTransport, TestTypeDiscovery, 30000,
    TRUE, FALSE, FALSE, TRUE, FALSE, FALSE,
    TestL4PortScan
    );

  RegAdd (
    L"TCP Stress",
    L"Stress test TCP with rapid connect/disconnect cycles",
    OsiLayerTransport, TestTypeStress, 30000,
    TRUE, FALSE, FALSE, TRUE, FALSE, FALSE,
    TestL4TcpStress
    );

  //
  // ========== Layer 7: Application (6 tests) ==========
  //
  RegAdd (
    L"DHCP Discover",
    L"Send DHCP discover and check for offers",
    OsiLayerApplication, TestTypeDiscovery, 10000,
    FALSE, FALSE, FALSE, FALSE, FALSE, TRUE,
    TestL7DhcpDiscover
    );

  RegAdd (
    L"DHCP Lease Verify",
    L"Verify current DHCP lease is valid",
    OsiLayerApplication, TestTypeCompliance, 5000,
    FALSE, FALSE, FALSE, FALSE, FALSE, TRUE,
    TestL7DhcpLeaseVerify
    );

  RegAdd (
    L"DNS Resolve",
    L"Resolve a hostname via DNS query",
    OsiLayerApplication, TestTypeConnectivity, 5000,
    FALSE, FALSE, TRUE, FALSE, TRUE, FALSE,
    TestL7DnsResolve
    );

  RegAdd (
    L"DNS Reverse",
    L"Perform reverse DNS lookup on an IP address",
    OsiLayerApplication, TestTypeConnectivity, 5000,
    FALSE, FALSE, TRUE, FALSE, TRUE, FALSE,
    TestL7DnsReverse
    );

  RegAdd (
    L"HTTP GET",
    L"Perform HTTP GET request to target",
    OsiLayerApplication, TestTypeConnectivity, 10000,
    TRUE, FALSE, FALSE, TRUE, FALSE, FALSE,
    TestL7HttpGet
    );

  RegAdd (
    L"HTTP Status Codes",
    L"Test HTTP response status code handling",
    OsiLayerApplication, TestTypeCompliance, 10000,
    TRUE, FALSE, FALSE, TRUE, FALSE, FALSE,
    TestL7HttpStatusCodes
    );
}

/**
  Get the total number of registered tests.

  @return Number of registered tests.
**/
UINTN
RegGetTestCount (
  VOID
  )
{
  return mTestCount;
}

/**
  Get a test definition by its index in the registry.

  @param[in] Index  Zero-based index.

  @return Pointer to TEST_DEFINITION, or NULL if out of range.
**/
TEST_DEFINITION *
RegGetTest (
  IN UINTN  Index
  )
{
  if (Index >= mTestCount) {
    return NULL;
  }

  return &mRegistry[Index];
}

/**
  Get all tests matching a given OSI layer.
  Fills OutArray with pointers to matching TEST_DEFINITION entries.

  @param[in]  Layer     OSI layer to filter (OsiLayerAll for all).
  @param[out] OutArray  Array of TEST_DEFINITION pointers.
  @param[in]  MaxCount  Maximum entries in OutArray.

  @return Number of matching tests.
**/
UINTN
RegGetTestsByLayer (
  IN  OSI_LAYER         Layer,
  OUT TEST_DEFINITION   **OutArray,
  IN  UINTN             MaxCount
  )
{
  UINTN  I;
  UINTN  Count;

  Count = 0;
  for (I = 0; I < mTestCount && Count < MaxCount; I++) {
    if (Layer == OsiLayerAll || mRegistry[I].Layer == Layer) {
      OutArray[Count] = &mRegistry[I];
      Count++;
    }
  }

  return Count;
}

/**
  Get human-readable name for an OSI layer.

  @param[in] Layer  OSI layer enum value.

  @return Pointer to static string.
**/
CONST CHAR16 *
RegGetLayerName (
  IN OSI_LAYER  Layer
  )
{
  switch (Layer) {
    case OsiLayerPhysical:     return L"Layer 1 - Physical";
    case OsiLayerDataLink:     return L"Layer 2 - Data Link";
    case OsiLayerNetwork:      return L"Layer 3 - Network";
    case OsiLayerTransport:    return L"Layer 4 - Transport";
    case OsiLayerSession:      return L"Layer 5 - Session";
    case OsiLayerPresentation: return L"Layer 6 - Presentation";
    case OsiLayerApplication:  return L"Layer 7 - Application";
    case OsiLayerAll:          return L"All Layers";
    default:                   return L"Unknown";
  }
}

/**
  Get short layer name (e.g. "L1", "L2").

  @param[in] Layer  OSI layer enum value.

  @return Pointer to static string.
**/
CONST CHAR16 *
RegGetLayerShort (
  IN OSI_LAYER  Layer
  )
{
  switch (Layer) {
    case OsiLayerPhysical:     return L"L1";
    case OsiLayerDataLink:     return L"L2";
    case OsiLayerNetwork:      return L"L3";
    case OsiLayerTransport:    return L"L4";
    case OsiLayerSession:      return L"L5";
    case OsiLayerPresentation: return L"L6";
    case OsiLayerApplication:  return L"L7";
    case OsiLayerAll:          return L"ALL";
    default:                   return L"??";
  }
}

/**
  Get human-readable name for a test result status code.

  @param[in] StatusCode  Test result code.

  @return Pointer to static string.
**/
CONST CHAR16 *
RegGetResultName (
  IN UINT32  StatusCode
  )
{
  switch (StatusCode) {
    case TEST_RESULT_PASS:  return L"PASS";
    case TEST_RESULT_FAIL:  return L"FAIL";
    case TEST_RESULT_SKIP:  return L"SKIP";
    case TEST_RESULT_WARN:  return L"WARN";
    case TEST_RESULT_ERROR: return L"ERROR";
    default:                return L"???";
  }
}

/**
  Get human-readable name for a test type.

  @param[in] Type  Test type enum value.

  @return Pointer to static string.
**/
CONST CHAR16 *
RegGetTypeName (
  IN TEST_TYPE  Type
  )
{
  switch (Type) {
    case TestTypeDiscovery:     return L"Discovery";
    case TestTypeConnectivity:  return L"Connectivity";
    case TestTypePerformance:   return L"Performance";
    case TestTypeStress:        return L"Stress";
    case TestTypeCompliance:    return L"Compliance";
    case TestTypePacketCapture: return L"Capture";
    case TestTypeSecurity:      return L"Security";
    case TestTypeFuzz:          return L"Fuzz";
    case TestTypeAll:           return L"All";
    default:                    return L"Unknown";
  }
}
