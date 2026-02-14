/** @file
  DDTSoftNetTest - EFI Network Test & OSI Layer Analyzer
  Main application header.
**/

#ifndef DDTSOFT_NET_TEST_H_
#define DDTSOFT_NET_TEST_H_

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Protocol/SimpleNetwork.h>
#include <Protocol/ManagedNetwork.h>
#include <Protocol/Arp.h>
#include <Protocol/Ip4.h>
#include <Protocol/Ip4Config2.h>
#include <Protocol/Udp4.h>
#include <Protocol/Tcp4.h>
#include <Protocol/PciIo.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/ComponentName2.h>
#include <Protocol/DevicePath.h>
#include <Protocol/AdapterInformation.h>

//
// Application version
//
#define APP_VERSION_MAJOR  1
#define APP_VERSION_MINOR  0
#define APP_VERSION_PATCH  0
#define APP_VERSION_STRING L"1.0.0"
#define APP_NAME           L"DDTSoft"
#define APP_FULL_NAME      L"DDTSoft - EFI Network Test & OSI Analyzer"

//
// Network defaults
//
#define DEFAULT_LOCAL_IP       { 192, 168, 100, 10 }
#define DEFAULT_COMPANION_IP   { 192, 168, 100, 1 }
#define DEFAULT_SUBNET_MASK    { 255, 255, 255, 0 }
#define DEFAULT_GATEWAY        { 192, 168, 100, 1 }
#define CONTROL_CHANNEL_PORT   9999
#define MAX_INTERFACES         8
#define MAC_ADDRESS_LENGTH     6

//
// UI Color definitions
//
#define COLOR_DEFAULT    EFI_WHITE
#define COLOR_SUCCESS    EFI_GREEN
#define COLOR_ERROR      EFI_RED
#define COLOR_WARNING    EFI_YELLOW
#define COLOR_INFO       EFI_CYAN
#define COLOR_HEADER     EFI_LIGHTBLUE
#define COLOR_LAYER1     EFI_LIGHTMAGENTA
#define COLOR_LAYER2     EFI_LIGHTCYAN
#define COLOR_LAYER3     EFI_LIGHTGREEN
#define COLOR_LAYER4     EFI_YELLOW
#define COLOR_LAYER7     EFI_LIGHTRED
#define COLOR_BG         EFI_BACKGROUND_BLACK

//
// UI dimensions
//
#define UI_BOX_WIDTH     66
#define UI_MENU_START_ROW  8

//
// NIC information structure
//
typedef struct {
  UINTN                          Index;
  EFI_HANDLE                     Handle;
  EFI_SIMPLE_NETWORK_PROTOCOL    *Snp;

  // Identity
  EFI_MAC_ADDRESS                CurrentMac;
  EFI_MAC_ADDRESS                PermanentMac;
  UINT8                          IfType;
  CHAR16                         Name[64];
  CHAR16                         DevicePath[256];

  // Physical state
  UINT32                         State;
  BOOLEAN                        MediaPresent;
  BOOLEAN                        MediaDetectSupported;
  BOOLEAN                        MacChangeable;
  BOOLEAN                        MultipleTxSupported;

  // Capacity
  UINT32                         MaxPacketSize;
  UINT32                         NvRamSize;
  UINT32                         MediaHeaderSize;
  UINT32                         ReceiveFilterMask;
  UINT32                         MaxMCastFilterCount;

  // IP configuration
  BOOLEAN                        HasIpConfig;
  EFI_IPv4_ADDRESS               Ipv4Address;
  EFI_IPv4_ADDRESS               SubnetMask;
  EFI_IPv4_ADDRESS               Gateway;

  // Upper-layer protocol support
  BOOLEAN                        HasMnp;
  BOOLEAN                        HasArp;
  BOOLEAN                        HasIp4;
  BOOLEAN                        HasIp6;
  BOOLEAN                        HasTcp4;
  BOOLEAN                        HasUdp4;
  BOOLEAN                        HasDhcp4;
  BOOLEAN                        HasDns4;
  BOOLEAN                        HasHttp;
  BOOLEAN                        HasTls;
} NIC_INFO;

//
// Menu item structure
//
typedef struct {
  CHAR16    Key;
  CHAR16    *Label;
  CHAR16    *Description;
} MENU_ITEM;

//
// Forward declarations - Main
//
EFI_STATUS
EFIAPI
DDTSoftNetTestMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  );

//
// Forward declarations - Modules
//
EFI_STATUS ShowSystemInfo (VOID);
EFI_STATUS ShowNetworkInterfaces (VOID);
EFI_STATUS ShowTestMenu (VOID);
EFI_STATUS ShowPacketCapture (VOID);
EFI_STATUS ShowReports (VOID);

//
// Utility functions (Utils.c)
//
VOID   UtilFormatMac       (IN CONST UINT8 *MacAddr, OUT CHAR16 *OutStr);
VOID   UtilFormatIpv4      (IN CONST UINT8 *Ip, OUT CHAR16 *OutStr);
VOID   UtilStallMs         (IN UINTN Ms);
UINT64 UtilGetTimestamp     (VOID);
VOID   UtilAsciiToUnicode  (IN CONST CHAR8 *Ascii, OUT CHAR16 *Unicode, IN UINTN MaxLen);
VOID   UtilSafeStrCpy      (OUT CHAR16 *Dest, IN CONST CHAR16 *Src, IN UINTN MaxLen);

#endif // DDTSOFT_NET_TEST_H_
