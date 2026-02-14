/** @file
  System information data structures.
  Firmware, system, CPU, memory, PCI, driver, and ACPI info.
**/

#ifndef SYSTEM_INFO_H_
#define SYSTEM_INFO_H_

#include <Uefi.h>

//
// Firmware information (UEFI + BIOS)
//
typedef struct {
  CHAR16    FirmwareVendor[128];
  UINT32    FirmwareRevision;
  UINT16    UefiSpecMajor;
  UINT16    UefiSpecMinor;
  CHAR8     BiosVendor[64];
  CHAR8     BiosVersion[64];
  CHAR8     BiosReleaseDate[32];
  UINT16    BiosMajorRelease;
  UINT16    BiosMinorRelease;
  UINT64    BiosRomSize;
} FIRMWARE_INFO;

//
// System information
//
typedef struct {
  CHAR8     Manufacturer[64];
  CHAR8     ProductName[64];
  CHAR8     Version[64];
  CHAR8     SerialNumber[64];
  EFI_GUID  SystemUuid;
  CHAR8     BoardManufacturer[64];
  CHAR8     BoardProduct[64];
  CHAR8     BoardVersion[64];
  CHAR8     BoardSerial[64];
} SYSTEM_INFO;

//
// CPU information
//
typedef struct {
  CHAR8     ProcessorName[128];
  UINT16    MaxSpeed;
  UINT16    CurrentSpeed;
  UINT8     CoreCount;
  UINT8     ThreadCount;
  CHAR8     SocketDesignation[32];
} CPU_INFO;

//
// Memory slot information
//
typedef struct {
  UINT8     SlotIndex;
  CHAR8     DeviceLocator[32];
  UINT32    SizeMB;
  UINT16    Speed;
  UINT16    ConfiguredSpeed;
  UINT8     MemoryType;
  CHAR8     Manufacturer[32];
  CHAR8     PartNumber[32];
  CHAR8     SerialNumber[32];
  UINT8     FormFactor;
} MEMORY_SLOT_INFO;

//
// Memory overview
//
typedef struct {
  UINT32             TotalMemoryMB;
  UINT8              PopulatedSlots;
  UINT8              TotalSlots;
  MEMORY_SLOT_INFO   Slots[32];
} MEMORY_INFO;

//
// PCI device information
//
typedef struct {
  UINT8     Bus;
  UINT8     Device;
  UINT8     Function;
  UINT16    VendorId;
  UINT16    DeviceId;
  UINT16    SubsysVendorId;
  UINT16    SubsysDeviceId;
  UINT8     ClassCode;
  UINT8     SubClassCode;
  UINT8     ProgInterface;
  UINT8     RevisionId;
  CHAR16    VendorName[64];
  CHAR16    ClassName[32];
  CHAR16    DevicePath[256];
  BOOLEAN   IsNetworkDevice;
} PCI_DEVICE_INFO;

//
// Driver information
//
typedef struct {
  EFI_HANDLE Handle;
  CHAR16    Name[128];
  CHAR16    FilePath[256];
  UINT64    ImageBase;
  UINT64    ImageSize;
  UINT32    ImageCodeType;
  BOOLEAN   IsDriver;
} DRIVER_INFO;

//
// ACPI basic information
//
typedef struct {
  UINT8     AcpiRevision;
  CHAR8     OemId[7];
  UINT32    XsdtTableCount;
  BOOLEAN   HasDsdt;
  BOOLEAN   HasFadt;
  BOOLEAN   HasMadt;
  BOOLEAN   HasMcfg;
} ACPI_BASIC_INFO;

//
// Module function prototypes
//
EFI_STATUS CollectSystemInfo     (OUT SYSTEM_INFO *Info);
EFI_STATUS CollectFirmwareInfo   (OUT FIRMWARE_INFO *Info);
EFI_STATUS CollectCpuInfo        (OUT CPU_INFO *Info);
EFI_STATUS CollectMemoryInfo     (OUT MEMORY_INFO *Info);

EFI_STATUS ParseSmbiosTables     (VOID);
EFI_STATUS EnumeratePciDevices   (OUT PCI_DEVICE_INFO *Devices, IN OUT UINTN *Count);
EFI_STATUS EnumerateDrivers      (OUT DRIVER_INFO *Drivers, IN OUT UINTN *Count);
EFI_STATUS CollectAcpiInfo       (OUT ACPI_BASIC_INFO *Info);

EFI_STATUS DiscoverNics          (OUT NIC_INFO *Nics, IN OUT UINTN *Count);

#endif // SYSTEM_INFO_H_
