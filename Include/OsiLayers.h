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

#endif // OSI_LAYERS_H_
