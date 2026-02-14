/** @file
  UEFI driver enumeration and ACPI info collection.
**/

#include <DDTSoftNetTest.h>
#include <SystemInfo.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/ComponentName2.h>
#include <IndustryStandard/Acpi20.h>
#include <Guid/Acpi.h>

#define MAX_DRIVERS  256

//
// ACPI table signature constants
//
#define ACPI_SIG_FADT  SIGNATURE_32('F','A','C','P')
#define ACPI_SIG_DSDT  SIGNATURE_32('D','S','D','T')
#define ACPI_SIG_MADT  SIGNATURE_32('A','P','I','C')
#define ACPI_SIG_MCFG  SIGNATURE_32('M','C','F','G')

/**
  Enumerate loaded UEFI images/drivers.

  @param[out]     Drivers  Array to fill with driver info.
  @param[in,out]  Count    On entry, max array size. On exit, number found.

  @retval EFI_SUCCESS     Enumeration complete.
  @retval EFI_NOT_FOUND   No loaded image handles found.
**/
EFI_STATUS
EnumerateDrivers (
  OUT DRIVER_INFO  *Drivers,
  IN OUT UINTN     *Count
  )
{
  EFI_STATUS                  Status;
  EFI_HANDLE                  *HandleBuffer;
  UINTN                       HandleCount;
  UINTN                       Index;
  UINTN                       MaxDrivers;
  UINTN                       DriverCount;
  EFI_LOADED_IMAGE_PROTOCOL   *LoadedImage;
  EFI_COMPONENT_NAME2_PROTOCOL *CompName2;
  CHAR16                      *DriverName;
  EFI_DEVICE_PATH_PROTOCOL    *DevPath;
  CHAR16                      *DevPathStr;

  if (Drivers == NULL || Count == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  MaxDrivers = *Count;
  *Count = 0;
  DriverCount = 0;

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiLoadedImageProtocolGuid,
                  NULL,
                  &HandleCount,
                  &HandleBuffer
                  );
  if (EFI_ERROR (Status)) {
    return EFI_NOT_FOUND;
  }

  for (Index = 0; Index < HandleCount && DriverCount < MaxDrivers; Index++) {
    Status = gBS->HandleProtocol (
                    HandleBuffer[Index],
                    &gEfiLoadedImageProtocolGuid,
                    (VOID **)&LoadedImage
                    );
    if (EFI_ERROR (Status)) {
      continue;
    }

    ZeroMem (&Drivers[DriverCount], sizeof (DRIVER_INFO));

    Drivers[DriverCount].Handle    = HandleBuffer[Index];
    Drivers[DriverCount].ImageBase = (UINT64)(UINTN)LoadedImage->ImageBase;
    Drivers[DriverCount].ImageSize = (UINT64)LoadedImage->ImageSize;
    Drivers[DriverCount].ImageCodeType = (UINT32)LoadedImage->ImageCodeType;

    //
    // Determine if this is a driver or application
    //
    Drivers[DriverCount].IsDriver =
      (LoadedImage->ImageCodeType == EfiBootServicesCode ||
       LoadedImage->ImageCodeType == EfiRuntimeServicesCode) ? TRUE : FALSE;

    //
    // Try to get driver name via ComponentName2
    //
    DriverName = NULL;
    Status = gBS->HandleProtocol (
                    HandleBuffer[Index],
                    &gEfiComponentName2ProtocolGuid,
                    (VOID **)&CompName2
                    );
    if (!EFI_ERROR (Status)) {
      Status = CompName2->GetDriverName (CompName2, "en", &DriverName);
      if (EFI_ERROR (Status)) {
        CompName2->GetDriverName (CompName2, "eng", &DriverName);
      }
    }

    if (DriverName != NULL) {
      StrnCpyS (Drivers[DriverCount].Name, 128, DriverName, 127);
    } else {
      UnicodeSPrint (Drivers[DriverCount].Name, sizeof (Drivers[DriverCount].Name),
                     L"Image @0x%lX", Drivers[DriverCount].ImageBase);
    }

    //
    // Get file path
    //
    DevPath = LoadedImage->FilePath;
    if (DevPath != NULL) {
      DevPathStr = ConvertDevicePathToText (DevPath, FALSE, FALSE);
      if (DevPathStr != NULL) {
        StrnCpyS (Drivers[DriverCount].FilePath, 256, DevPathStr, 255);
        FreePool (DevPathStr);
      }
    }

    DriverCount++;
  }

  FreePool (HandleBuffer);
  *Count = DriverCount;

  return EFI_SUCCESS;
}

/**
  Collect basic ACPI information from configuration tables.

  @param[out]  Info  ACPI info to fill.

  @retval EFI_SUCCESS     ACPI info collected.
  @retval EFI_NOT_FOUND   ACPI RSDP not found.
**/
EFI_STATUS
CollectAcpiInfo (
  OUT ACPI_BASIC_INFO  *Info
  )
{
  UINTN                                     Index;
  EFI_ACPI_2_0_ROOT_SYSTEM_DESCRIPTION_POINTER  *Rsdp;
  EFI_ACPI_DESCRIPTION_HEADER               *Xsdt;
  UINT64                                    *XsdtEntries;
  UINTN                                     EntryCount;
  EFI_ACPI_DESCRIPTION_HEADER               *Table;
  BOOLEAN                                   Found;

  if (Info == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  ZeroMem (Info, sizeof (ACPI_BASIC_INFO));

  //
  // Find RSDP from EFI configuration tables
  //
  Found = FALSE;
  Rsdp = NULL;

  for (Index = 0; Index < gST->NumberOfTableEntries; Index++) {
    if (CompareGuid (&gST->ConfigurationTable[Index].VendorGuid, &gEfiAcpi20TableGuid)) {
      Rsdp = (EFI_ACPI_2_0_ROOT_SYSTEM_DESCRIPTION_POINTER *)gST->ConfigurationTable[Index].VendorTable;
      Found = TRUE;
      break;
    }
  }

  if (!Found) {
    for (Index = 0; Index < gST->NumberOfTableEntries; Index++) {
      if (CompareGuid (&gST->ConfigurationTable[Index].VendorGuid, &gEfiAcpi10TableGuid)) {
        Rsdp = (EFI_ACPI_2_0_ROOT_SYSTEM_DESCRIPTION_POINTER *)gST->ConfigurationTable[Index].VendorTable;
        Found = TRUE;
        break;
      }
    }
  }

  if (!Found || Rsdp == NULL) {
    return EFI_NOT_FOUND;
  }

  Info->AcpiRevision = Rsdp->Revision;
  CopyMem (Info->OemId, Rsdp->OemId, 6);
  Info->OemId[6] = '\0';

  //
  // Try XSDT first (ACPI 2.0+)
  //
  if (Rsdp->Revision >= 2 && Rsdp->XsdtAddress != 0) {
    Xsdt = (EFI_ACPI_DESCRIPTION_HEADER *)(UINTN)Rsdp->XsdtAddress;
    EntryCount = (Xsdt->Length - sizeof (EFI_ACPI_DESCRIPTION_HEADER)) / sizeof (UINT64);
    Info->XsdtTableCount = (UINT32)EntryCount;

    XsdtEntries = (UINT64 *)((UINT8 *)Xsdt + sizeof (EFI_ACPI_DESCRIPTION_HEADER));

    for (Index = 0; Index < EntryCount; Index++) {
      Table = (EFI_ACPI_DESCRIPTION_HEADER *)(UINTN)XsdtEntries[Index];
      if (Table == NULL) {
        continue;
      }

      if (Table->Signature == ACPI_SIG_FADT) {
        Info->HasFadt = TRUE;
        //
        // FADT contains DSDT pointer
        //
        if (Table->Length >= 148) {
          //
          // X_Dsdt at offset 140 in FADT (ACPI 2.0+)
          //
          UINT64 DsdtAddr = *(UINT64 *)((UINT8 *)Table + 140);
          if (DsdtAddr != 0) {
            Info->HasDsdt = TRUE;
          }
        }

        if (!Info->HasDsdt && Table->Length >= 44) {
          //
          // Legacy Dsdt at offset 40
          //
          UINT32 DsdtAddr32 = *(UINT32 *)((UINT8 *)Table + 40);
          if (DsdtAddr32 != 0) {
            Info->HasDsdt = TRUE;
          }
        }
      } else if (Table->Signature == ACPI_SIG_MADT) {
        Info->HasMadt = TRUE;
      } else if (Table->Signature == ACPI_SIG_MCFG) {
        Info->HasMcfg = TRUE;
      } else if (Table->Signature == ACPI_SIG_DSDT) {
        Info->HasDsdt = TRUE;
      }
    }
  }

  return EFI_SUCCESS;
}
