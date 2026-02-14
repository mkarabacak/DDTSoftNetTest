/** @file
  SMBIOS table parser.
  Parses Type 0 (BIOS), Type 1 (System), Type 2 (Board),
  Type 4 (CPU), Type 17 (Memory Device) structures.
**/

#include <DDTSoftNetTest.h>
#include <SystemInfo.h>
#include <IndustryStandard/SmBios.h>
#include <Guid/SmBios.h>

//
// Module-level parsed data, filled by ParseSmbiosTables()
//
STATIC FIRMWARE_INFO  mFirmwareInfo;
STATIC SYSTEM_INFO    mSystemInfo;
STATIC CPU_INFO       mCpuInfo;
STATIC MEMORY_INFO    mMemoryInfo;
STATIC BOOLEAN        mSmbiosParsed = FALSE;

/**
  Get a string from the SMBIOS string table following the formatted structure.

  @param[in]  Header       Pointer to the SMBIOS structure header.
  @param[in]  StringIndex  1-based string index (0 means no string).

  @return  Pointer to the ASCII string, or "" if not found.
**/
STATIC
CONST CHAR8 *
SmbiosGetString (
  IN SMBIOS_STRUCTURE  *Header,
  IN UINT8             StringIndex
  )
{
  CONST CHAR8  *StringPtr;
  UINT8        Index;

  if (StringIndex == 0) {
    return "";
  }

  //
  // Strings start after the formatted portion of the structure
  //
  StringPtr = (CONST CHAR8 *)Header + Header->Length;

  for (Index = 1; Index < StringIndex; Index++) {
    while (*StringPtr != '\0') {
      StringPtr++;
    }
    StringPtr++;

    //
    // Double-null means end of string set
    //
    if (*StringPtr == '\0') {
      return "";
    }
  }

  return StringPtr;
}

/**
  Copy an SMBIOS string to a CHAR8 buffer with bounds checking.
**/
STATIC
VOID
SmbiosCopyString (
  OUT CHAR8            *Dest,
  IN  UINTN            DestSize,
  IN  SMBIOS_STRUCTURE *Header,
  IN  UINT8            StringIndex
  )
{
  CONST CHAR8  *Src;
  UINTN        Len;

  Src = SmbiosGetString (Header, StringIndex);
  Len = AsciiStrLen (Src);
  if (Len >= DestSize) {
    Len = DestSize - 1;
  }

  CopyMem (Dest, Src, Len);
  Dest[Len] = '\0';
}

/**
  Get the next SMBIOS structure, skipping past the string table.
**/
STATIC
SMBIOS_STRUCTURE *
SmbiosGetNext (
  IN SMBIOS_STRUCTURE  *Current
  )
{
  CONST CHAR8  *Ptr;

  Ptr = (CONST CHAR8 *)Current + Current->Length;

  //
  // Skip the string set (terminated by double-null)
  //
  while (!(Ptr[0] == '\0' && Ptr[1] == '\0')) {
    Ptr++;
  }

  return (SMBIOS_STRUCTURE *)(Ptr + 2);
}

/**
  Parse a Type 0 (BIOS Information) structure.
**/
STATIC
VOID
ParseType0 (
  IN SMBIOS_TABLE_TYPE0  *Type0
  )
{
  SmbiosCopyString (mFirmwareInfo.BiosVendor, sizeof (mFirmwareInfo.BiosVendor),
                    &Type0->Hdr, Type0->Vendor);
  SmbiosCopyString (mFirmwareInfo.BiosVersion, sizeof (mFirmwareInfo.BiosVersion),
                    &Type0->Hdr, Type0->BiosVersion);
  SmbiosCopyString (mFirmwareInfo.BiosReleaseDate, sizeof (mFirmwareInfo.BiosReleaseDate),
                    &Type0->Hdr, Type0->BiosReleaseDate);

  mFirmwareInfo.BiosMajorRelease = Type0->SystemBiosMajorRelease;
  mFirmwareInfo.BiosMinorRelease = Type0->SystemBiosMinorRelease;

  //
  // BiosSize field: (n+1) * 64K. If 0xFF, use ExtendedBiosSize.
  //
  if (Type0->BiosSize != 0xFF) {
    mFirmwareInfo.BiosRomSize = ((UINT64)Type0->BiosSize + 1) * 64 * 1024;
  } else {
    //
    // ExtendedBiosSize: bits 13:0 = size, bits 15:14 = unit (0=MB, 1=GB)
    //
    if (Type0->ExtendedBiosSize.Unit == 0) {
      mFirmwareInfo.BiosRomSize = (UINT64)Type0->ExtendedBiosSize.Size * 1024 * 1024;
    } else {
      mFirmwareInfo.BiosRomSize = (UINT64)Type0->ExtendedBiosSize.Size * 1024 * 1024 * 1024;
    }
  }
}

/**
  Parse a Type 1 (System Information) structure.
**/
STATIC
VOID
ParseType1 (
  IN SMBIOS_TABLE_TYPE1  *Type1
  )
{
  SmbiosCopyString (mSystemInfo.Manufacturer, sizeof (mSystemInfo.Manufacturer),
                    &Type1->Hdr, Type1->Manufacturer);
  SmbiosCopyString (mSystemInfo.ProductName, sizeof (mSystemInfo.ProductName),
                    &Type1->Hdr, Type1->ProductName);
  SmbiosCopyString (mSystemInfo.Version, sizeof (mSystemInfo.Version),
                    &Type1->Hdr, Type1->Version);
  SmbiosCopyString (mSystemInfo.SerialNumber, sizeof (mSystemInfo.SerialNumber),
                    &Type1->Hdr, Type1->SerialNumber);

  CopyMem (&mSystemInfo.SystemUuid, &Type1->Uuid, sizeof (EFI_GUID));
}

/**
  Parse a Type 2 (Base Board Information) structure.
**/
STATIC
VOID
ParseType2 (
  IN SMBIOS_TABLE_TYPE2  *Type2
  )
{
  SmbiosCopyString (mSystemInfo.BoardManufacturer, sizeof (mSystemInfo.BoardManufacturer),
                    &Type2->Hdr, Type2->Manufacturer);
  SmbiosCopyString (mSystemInfo.BoardProduct, sizeof (mSystemInfo.BoardProduct),
                    &Type2->Hdr, Type2->ProductName);
  SmbiosCopyString (mSystemInfo.BoardVersion, sizeof (mSystemInfo.BoardVersion),
                    &Type2->Hdr, Type2->Version);
  SmbiosCopyString (mSystemInfo.BoardSerial, sizeof (mSystemInfo.BoardSerial),
                    &Type2->Hdr, Type2->SerialNumber);
}

/**
  Parse a Type 4 (Processor Information) structure.
  Only the first processor is recorded.
**/
STATIC
VOID
ParseType4 (
  IN SMBIOS_TABLE_TYPE4  *Type4
  )
{
  static BOOLEAN  FirstCpu = TRUE;

  if (!FirstCpu) {
    return;
  }

  FirstCpu = FALSE;

  SmbiosCopyString (mCpuInfo.ProcessorName, sizeof (mCpuInfo.ProcessorName),
                    &Type4->Hdr, Type4->ProcessorVersion);
  SmbiosCopyString (mCpuInfo.SocketDesignation, sizeof (mCpuInfo.SocketDesignation),
                    &Type4->Hdr, Type4->Socket);

  mCpuInfo.MaxSpeed     = Type4->MaxSpeed;
  mCpuInfo.CurrentSpeed = Type4->CurrentSpeed;
  mCpuInfo.CoreCount    = Type4->CoreCount;
  mCpuInfo.ThreadCount  = Type4->ThreadCount;
}

/**
  Parse a Type 17 (Memory Device) structure.
**/
STATIC
VOID
ParseType17 (
  IN SMBIOS_TABLE_TYPE17  *Type17
  )
{
  MEMORY_SLOT_INFO  *Slot;
  UINT32            SizeMB;

  if (mMemoryInfo.TotalSlots >= 32) {
    return;
  }

  Slot = &mMemoryInfo.Slots[mMemoryInfo.TotalSlots];
  Slot->SlotIndex = mMemoryInfo.TotalSlots;

  SmbiosCopyString (Slot->DeviceLocator, sizeof (Slot->DeviceLocator),
                    &Type17->Hdr, Type17->DeviceLocator);
  SmbiosCopyString (Slot->Manufacturer, sizeof (Slot->Manufacturer),
                    &Type17->Hdr, Type17->Manufacturer);
  SmbiosCopyString (Slot->PartNumber, sizeof (Slot->PartNumber),
                    &Type17->Hdr, Type17->PartNumber);
  SmbiosCopyString (Slot->SerialNumber, sizeof (Slot->SerialNumber),
                    &Type17->Hdr, Type17->SerialNumber);

  //
  // Size: if bit 15 is set, value is in KB. Otherwise in MB.
  // 0 = not installed, 0x7FFF = use ExtendedSize, 0xFFFF = unknown.
  //
  if (Type17->Size == 0 || Type17->Size == 0xFFFF) {
    SizeMB = 0;
  } else if (Type17->Size == 0x7FFF) {
    SizeMB = Type17->ExtendedSize & 0x7FFFFFFF;
  } else if (Type17->Size & 0x8000) {
    SizeMB = (Type17->Size & 0x7FFF) / 1024;
  } else {
    SizeMB = Type17->Size;
  }

  Slot->SizeMB           = SizeMB;
  Slot->Speed            = Type17->Speed;
  Slot->ConfiguredSpeed  = Type17->ConfiguredMemoryClockSpeed;
  Slot->MemoryType       = Type17->MemoryType;
  Slot->FormFactor       = Type17->FormFactor;

  mMemoryInfo.TotalSlots++;

  if (SizeMB > 0) {
    mMemoryInfo.PopulatedSlots++;
    mMemoryInfo.TotalMemoryMB += SizeMB;
  }
}

/**
  Find the SMBIOS entry point from EFI configuration tables
  and parse all relevant structures.

  @retval EFI_SUCCESS    SMBIOS tables parsed successfully.
  @retval EFI_NOT_FOUND  SMBIOS entry point not found.
**/
EFI_STATUS
ParseSmbiosTables (
  VOID
  )
{
  UINTN               Index;
  VOID                *SmbiosTableAddress;
  SMBIOS_STRUCTURE    *Current;
  UINT8               *TableEnd;
  BOOLEAN             Found;

  SMBIOS_TABLE_ENTRY_POINT        *EntryPoint;
  SMBIOS_TABLE_3_0_ENTRY_POINT    *EntryPoint3;

  if (mSmbiosParsed) {
    return EFI_SUCCESS;
  }

  ZeroMem (&mFirmwareInfo, sizeof (mFirmwareInfo));
  ZeroMem (&mSystemInfo, sizeof (mSystemInfo));
  ZeroMem (&mCpuInfo, sizeof (mCpuInfo));
  ZeroMem (&mMemoryInfo, sizeof (mMemoryInfo));

  //
  // Collect UEFI firmware info from SystemTable
  //
  if (gST->FirmwareVendor != NULL) {
    StrnCpyS (mFirmwareInfo.FirmwareVendor, 128, gST->FirmwareVendor, 127);
  }

  mFirmwareInfo.FirmwareRevision = gST->FirmwareRevision;
  mFirmwareInfo.UefiSpecMajor    = (UINT16)(gST->Hdr.Revision >> 16);
  mFirmwareInfo.UefiSpecMinor    = (UINT16)(gST->Hdr.Revision & 0xFFFF);

  //
  // Try SMBIOS 3.0 64-bit entry point first, then fall back to 2.x
  //
  Found = FALSE;
  SmbiosTableAddress = NULL;

  for (Index = 0; Index < gST->NumberOfTableEntries; Index++) {
    if (CompareGuid (&gST->ConfigurationTable[Index].VendorGuid, &gEfiSmbios3TableGuid)) {
      EntryPoint3 = (SMBIOS_TABLE_3_0_ENTRY_POINT *)gST->ConfigurationTable[Index].VendorTable;
      SmbiosTableAddress = (VOID *)(UINTN)EntryPoint3->TableAddress;
      TableEnd = (UINT8 *)SmbiosTableAddress + EntryPoint3->TableMaximumSize;
      Found = TRUE;
      break;
    }
  }

  if (!Found) {
    for (Index = 0; Index < gST->NumberOfTableEntries; Index++) {
      if (CompareGuid (&gST->ConfigurationTable[Index].VendorGuid, &gEfiSmbiosTableGuid)) {
        EntryPoint = (SMBIOS_TABLE_ENTRY_POINT *)gST->ConfigurationTable[Index].VendorTable;
        SmbiosTableAddress = (VOID *)(UINTN)EntryPoint->TableAddress;
        TableEnd = (UINT8 *)SmbiosTableAddress + EntryPoint->TableLength;
        Found = TRUE;
        break;
      }
    }
  }

  if (!Found || SmbiosTableAddress == NULL) {
    return EFI_NOT_FOUND;
  }

  //
  // Walk SMBIOS structures
  //
  Current = (SMBIOS_STRUCTURE *)SmbiosTableAddress;

  while ((UINT8 *)Current < TableEnd && Current->Type != 127) {
    switch (Current->Type) {
      case 0:
        ParseType0 ((SMBIOS_TABLE_TYPE0 *)Current);
        break;
      case 1:
        ParseType1 ((SMBIOS_TABLE_TYPE1 *)Current);
        break;
      case 2:
        ParseType2 ((SMBIOS_TABLE_TYPE2 *)Current);
        break;
      case 4:
        ParseType4 ((SMBIOS_TABLE_TYPE4 *)Current);
        break;
      case 17:
        ParseType17 ((SMBIOS_TABLE_TYPE17 *)Current);
        break;
      default:
        break;
    }

    Current = SmbiosGetNext (Current);
  }

  mSmbiosParsed = TRUE;
  return EFI_SUCCESS;
}

//
// Accessor functions to copy parsed data
//

EFI_STATUS
CollectFirmwareInfo (
  OUT FIRMWARE_INFO  *Info
  )
{
  EFI_STATUS  Status;

  if (Info == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Status = ParseSmbiosTables ();
  if (EFI_ERROR (Status)) {
    //
    // Even if SMBIOS not found, we still have UEFI firmware info
    //
    CopyMem (Info, &mFirmwareInfo, sizeof (FIRMWARE_INFO));
    return EFI_SUCCESS;
  }

  CopyMem (Info, &mFirmwareInfo, sizeof (FIRMWARE_INFO));
  return EFI_SUCCESS;
}

EFI_STATUS
CollectSystemInfo (
  OUT SYSTEM_INFO  *Info
  )
{
  if (Info == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  ParseSmbiosTables ();
  CopyMem (Info, &mSystemInfo, sizeof (SYSTEM_INFO));
  return EFI_SUCCESS;
}

EFI_STATUS
CollectCpuInfo (
  OUT CPU_INFO  *Info
  )
{
  if (Info == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  ParseSmbiosTables ();
  CopyMem (Info, &mCpuInfo, sizeof (CPU_INFO));
  return EFI_SUCCESS;
}

EFI_STATUS
CollectMemoryInfo (
  OUT MEMORY_INFO  *Info
  )
{
  if (Info == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  ParseSmbiosTables ();
  CopyMem (Info, &mMemoryInfo, sizeof (MEMORY_INFO));
  return EFI_SUCCESS;
}
