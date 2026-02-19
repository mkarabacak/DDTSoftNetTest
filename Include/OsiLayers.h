/** @file
  OSI Layer definitions, test types, and test structures.
**/

#ifndef OSI_LAYERS_H_
#define OSI_LAYERS_H_

#include <Uefi.h>
#include "DDTSoftNetTest.h"

//
// OSI Layer enumeration
//
typedef enum {
  OsiLayerPhysical     = 1,
  OsiLayerDataLink     = 2,
  OsiLayerNetwork      = 3,
  OsiLayerTransport    = 4,
  OsiLayerSession      = 5,
  OsiLayerPresentation = 6,
  OsiLayerApplication  = 7,
  OsiLayerAll          = 0xFF
} OSI_LAYER;

//
// Test type enumeration
//
typedef enum {
  TestTypeDiscovery,
  TestTypeConnectivity,
  TestTypePerformance,
  TestTypeStress,
  TestTypeCompliance,
  TestTypePacketCapture,
  TestTypeSecurity,
  TestTypeFuzz,
  TestTypeAll
} TEST_TYPE;

//
// Test result codes
//
#define TEST_RESULT_PASS   0
#define TEST_RESULT_FAIL   1
#define TEST_RESULT_SKIP   2
#define TEST_RESULT_WARN   3
#define TEST_RESULT_ERROR  4

//
// Test configuration
//
typedef struct {
  EFI_IPv4_ADDRESS    TargetIp;
  EFI_IPv4_ADDRESS    LocalIp;
  EFI_IPv4_ADDRESS    SubnetMask;
  EFI_IPv4_ADDRESS    Gateway;
  UINT32              TimeoutMs;
  UINT32              Iterations;
  UINT16              TargetPort;
  BOOLEAN             UseCompanion;
  EFI_IPv4_ADDRESS    CompanionIp;
  UINT16              CompanionPort;
} TEST_CONFIG;

//
// Test result data
//
typedef struct {
  UINT32    StatusCode;
  UINT64    DurationMs;
  CHAR16    Summary[128];
  CHAR16    Detail[512];
  CHAR16    FailReason[256];
  CHAR16    Suggestion[256];
  UINT64    PacketsSent;
  UINT64    PacketsReceived;
  UINT64    BytesSent;
  UINT64    BytesReceived;
  UINT32    RttMinUs;
  UINT32    RttAvgUs;
  UINT32    RttMaxUs;
  UINT32    RttJitterUs;
} TEST_RESULT_DATA;

//
// Test definition
//
typedef struct {
  CHAR16        *Name;
  CHAR16        *Description;
  OSI_LAYER     Layer;
  TEST_TYPE     Type;
  UINT32        EstimatedTimeMs;
  BOOLEAN       RequiresTarget;
  BOOLEAN       RequiresIpv6;
  BOOLEAN       IsDestructive;
  BOOLEAN       NeedSnp;
  BOOLEAN       NeedMnp;
  BOOLEAN       NeedIp4;
  BOOLEAN       NeedTcp4;
  BOOLEAN       NeedUdp4;
  BOOLEAN       NeedDhcp4;
  EFI_STATUS    (*Execute)(
                  IN  NIC_INFO         *Nic,
                  IN  TEST_CONFIG      *Config,
                  OUT TEST_RESULT_DATA *Result
                );
} TEST_DEFINITION;

//
// Maximum registered tests
//
#define MAX_TESTS  64

//
// ============================================================
// TestRegistry functions (TestRegistry.c)
// ============================================================
//

//
// Initialize the test registry with all test definitions
//
VOID   RegInitAllTests   (VOID);

//
// Query functions
//
UINTN            RegGetTestCount    (VOID);
TEST_DEFINITION *RegGetTest         (IN UINTN Index);
UINTN            RegGetTestsByLayer (IN OSI_LAYER Layer, OUT TEST_DEFINITION **OutArray, IN UINTN MaxCount);

//
// Name helpers
//
CONST CHAR16 *RegGetLayerName  (IN OSI_LAYER Layer);
CONST CHAR16 *RegGetLayerShort (IN OSI_LAYER Layer);
CONST CHAR16 *RegGetResultName (IN UINT32 StatusCode);
CONST CHAR16 *RegGetTypeName   (IN TEST_TYPE Type);

//
// ============================================================
// TestRunner functions (TestRunner.c)
// ============================================================
//

//
// Run a single test with timing and result capture
//
EFI_STATUS RunSingleTest (
  IN  TEST_DEFINITION   *Test,
  IN  NIC_INFO          *Nic,
  IN  TEST_CONFIG       *Config,
  OUT TEST_RESULT_DATA  *Result
  );

//
// Run all tests for a given OSI layer
//
EFI_STATUS RunTestsByLayer (
  IN  OSI_LAYER         Layer,
  IN  NIC_INFO          *Nic,
  IN  TEST_CONFIG       *Config,
  OUT TEST_RESULT_DATA  *Results,
  IN  UINTN             MaxResults,
  OUT UINTN             *ResultCount
  );

//
// Run all registered tests
//
EFI_STATUS RunAllTests (
  IN  NIC_INFO          *Nic,
  IN  TEST_CONFIG       *Config,
  OUT TEST_RESULT_DATA  *Results,
  IN  UINTN             MaxResults,
  OUT UINTN             *ResultCount
  );

//
// Check if NIC meets test prerequisites
//
BOOLEAN RunCheckPrerequisites (
  IN TEST_DEFINITION  *Test,
  IN NIC_INFO         *Nic
  );

//
// ============================================================
// QuickScan functions (QuickScan.c)
// ============================================================
//

//
// Run quick scan with UI (progress, results, diagnosis)
//
EFI_STATUS QuickScanRun (
  IN NIC_INFO     *Nic,
  IN TEST_CONFIG  *Config
  );

//
// Run quick scan silently and return diagnosis string
//
EFI_STATUS QuickScanGetDiagnosis (
  IN  NIC_INFO     *Nic,
  IN  TEST_CONFIG  *Config,
  OUT CHAR16       *Diagnosis,
  IN  UINTN        DiagSize,
  OUT CHAR16       *Detail     OPTIONAL,
  IN  UINTN        DetailSize
  );

//
// ============================================================
// StressTest functions (StressTest.c)
// ============================================================
//

//
// Run stress test with UI (mode selection, live stats, RTT graph)
//
EFI_STATUS StressTestRun (
  IN NIC_INFO     *Nic,
  IN TEST_CONFIG  *Config
  );

//
// Run stress test silently and return statistics
//
EFI_STATUS StressTestGetStats (
  IN  NIC_INFO         *Nic,
  IN  TEST_CONFIG      *Config,
  IN  UINT32           Mode,
  OUT TEST_RESULT_DATA *Result
  );

//
// ============================================================
// ReportExporter public functions
// ============================================================
//

//
// Export existing test results (called from Run Tests menu)
//
EFI_STATUS ExportTestResults (
  IN NIC_INFO          *Nic,
  IN TEST_CONFIG       *Config,
  IN TEST_DEFINITION   **TestDefs,
  IN TEST_RESULT_DATA  *Results,
  IN UINTN             ResultCount,
  IN OSI_LAYER         Layer
  );

#endif // OSI_LAYERS_H_
