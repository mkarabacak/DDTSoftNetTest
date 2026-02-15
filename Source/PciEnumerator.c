/** @file
  PCI device enumeration via EFI_PCI_IO_PROTOCOL.
  Reads config space, fills PCI_DEVICE_INFO array.
**/

#include <DDTSoftNetTest.h>
#include <SystemInfo.h>
#include <PciIds.h>
#include <Protocol/PciIo.h>
#include <IndustryStandard/Pci.h>

#define MAX_PCI_DEVICES  128

/**
  Enumerate all PCI devices via EFI_PCI_IO_PROTOCOL handles.

  @param[out]     Devices  Array to fill with PCI device info.
  @param[in,out]  Count    On entry, max array size. On exit, number found.

  @retval EFI_SUCCESS     Enumeration complete.
  @retval EFI_NOT_FOUND   No PCI IO protocol handles found.
**/
EFI_STATUS
EnumeratePciDevices (
  OUT PCI_DEVICE_INFO  *Devices,
  IN OUT UINTN         *Count
  )
{
  EFI_STATUS           Status;
  EFI_HANDLE           *HandleBuffer;
  UINTN                HandleCount;
  UINTN                Index;
  UINTN                MaxDevices;
  UINTN                DeviceCount;
  EFI_PCI_IO_PROTOCOL  *PciIo;
  UINTN                Seg, Bus, Dev, Func;
  UINT16               VendorId;
  UINT16               DeviceId;
  UINT16               SubVendorId;
  UINT16               SubDeviceId;
  UINT8                ClassCode[3];
  UINT8                RevisionId;
  CONST CHAR16         *VendorName;
  CONST CHAR16         *ClassName;
  EFI_DEVICE_PATH_PROTOCOL  *DevPath;
  CHAR16                    *DevPathStr;

  if (Devices == NULL || Count == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  MaxDevices = *Count;
  *Count = 0;
  DeviceCount = 0;

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiPciIoProtocolGuid,
                  NULL,
                  &HandleCount,
                  &HandleBuffer
                  );
  if (EFI_ERROR (Status)) {
    return EFI_NOT_FOUND;
  }

  for (Index = 0; Index < HandleCount && DeviceCount < MaxDevices; Index++) {
    Status = gBS->HandleProtocol (
                    HandleBuffer[Index],
                    &gEfiPciIoProtocolGuid,
                    (VOID **)&PciIo
                    );
    if (EFI_ERROR (Status)) {
      continue;
    }

    //
    // Get PCI location (segment, bus, device, function)
    //
    Status = PciIo->GetLocation (PciIo, &Seg, &Bus, &Dev, &Func);
    if (EFI_ERROR (Status)) {
      continue;
    }

    //
    // Read config space
    //
    Status = PciIo->Pci.Read (PciIo, EfiPciIoWidthUint16, 0x00, 1, &VendorId);
    if (EFI_ERROR (Status) || VendorId == 0xFFFF) {
      continue;
    }

    PciIo->Pci.Read (PciIo, EfiPciIoWidthUint16, 0x02, 1, &DeviceId);
    PciIo->Pci.Read (PciIo, EfiPciIoWidthUint8,  0x09, 3, ClassCode);
    PciIo->Pci.Read (PciIo, EfiPciIoWidthUint8,  0x08, 1, &RevisionId);
    PciIo->Pci.Read (PciIo, EfiPciIoWidthUint16, 0x2C, 1, &SubVendorId);
    PciIo->Pci.Read (PciIo, EfiPciIoWidthUint16, 0x2E, 1, &SubDeviceId);

    //
    // Fill the device info entry
    //
    ZeroMem (&Devices[DeviceCount], sizeof (PCI_DEVICE_INFO));

    Devices[DeviceCount].Bus             = (UINT8)Bus;
    Devices[DeviceCount].Device          = (UINT8)Dev;
    Devices[DeviceCount].Function        = (UINT8)Func;
    Devices[DeviceCount].VendorId        = VendorId;
    Devices[DeviceCount].DeviceId        = DeviceId;
    Devices[DeviceCount].SubsysVendorId  = SubVendorId;
    Devices[DeviceCount].SubsysDeviceId  = SubDeviceId;
    Devices[DeviceCount].ClassCode       = ClassCode[2];
    Devices[DeviceCount].SubClassCode    = ClassCode[1];
    Devices[DeviceCount].ProgInterface   = ClassCode[0];
    Devices[DeviceCount].RevisionId      = RevisionId;
    Devices[DeviceCount].IsNetworkDevice = (ClassCode[2] == 0x02) ? TRUE : FALSE;

    //
    // Lookup vendor name
    //
    VendorName = PciLookupVendorName (VendorId);
    StrnCpyS (Devices[DeviceCount].VendorName, 64, VendorName, 63);

    //
    // Lookup class name
    //
    ClassName = PciLookupClassName (ClassCode[2]);
    StrnCpyS (Devices[DeviceCount].ClassName, 32, ClassName, 31);

    //
    // Get device path string
    //
    Status = gBS->HandleProtocol (
                    HandleBuffer[Index],
                    &gEfiDevicePathProtocolGuid,
                    (VOID **)&DevPath
                    );
    if (!EFI_ERROR (Status) && DevPath != NULL) {
      DevPathStr = ConvertDevicePathToText (DevPath, FALSE, FALSE);
      if (DevPathStr != NULL) {
        StrnCpyS (Devices[DeviceCount].DevicePath, 256, DevPathStr, 255);
        FreePool (DevPathStr);
      }
    }

    DeviceCount++;
  }

  FreePool (HandleBuffer);
  *Count = DeviceCount;

  return EFI_SUCCESS;
}

CONST CHAR16 *
PciLookupVendorName (
  IN UINT16  VendorId
  )
{
  UINTN  I;

  for (I = 0; gPciVendorTable[I].Name != NULL; I++) {
    if (gPciVendorTable[I].VendorId == VendorId) {
      return gPciVendorTable[I].Name;
    }
  }

  return L"Unknown";
}

CONST CHAR16 *
PciLookupClassName (
  IN UINT8  ClassCode
  )
{
  UINTN  I;

  for (I = 0; gPciClassTable[I].Name != NULL; I++) {
    if (gPciClassTable[I].ClassCode == ClassCode) {
      return gPciClassTable[I].Name;
    }
  }

  return L"Unknown";
}

CONST CHAR16 *
PciLookupNicDeviceName (
  IN UINT16  VendorId,
  IN UINT16  DeviceId
  )
{
  UINTN  I;

  for (I = 0; gPciNicDeviceTable[I].Name != NULL; I++) {
    if (gPciNicDeviceTable[I].VendorId == VendorId &&
        gPciNicDeviceTable[I].DeviceId == DeviceId) {
      return gPciNicDeviceTable[I].Name;
    }
  }

  return NULL;
}
