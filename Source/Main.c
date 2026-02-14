/** @file
  DDTSoftNetTest - Main entry point and menu loop.
  EFI Network Test & OSI Layer Analyzer.
**/

#include <DDTSoftNetTest.h>
#include <UiRenderer.h>
#include <SystemInfo.h>
#include <PciIds.h>

//
// Main menu items
//
STATIC MENU_ITEM  mMainMenu[] = {
  { L'S', L"System Information",   L"Sistem ve donanim bilgileri"   },
  { L'N', L"Network Interfaces",   L"NIC listesi ve secimi"        },
  { L'T', L"Run Tests",            L"Test calistir"                },
  { L'C', L"Packet Capture",       L"Paket yakalama & analiz"      },
  { L'R', L"Reports",              L"Test sonuc raporlari"         },
  { L'Q', L"Quit",                 L"Cikis"                        },
};

#define MAIN_MENU_COUNT  (sizeof (mMainMenu) / sizeof (mMainMenu[0]))

//
// SystemInfo constants
//
#define SYSINFO_TOTAL_PAGES  5
#define MAX_PCI_DEVICES      128
#define MAX_DRIVERS          256

//
// Memory type name lookup
//
STATIC CONST CHAR16 *
GetMemoryTypeName (
  IN UINT8  MemType
  )
{
  switch (MemType) {
    case 0x01: return L"Other";
    case 0x02: return L"Unknown";
    case 0x03: return L"DRAM";
    case 0x04: return L"EDRAM";
    case 0x05: return L"VRAM";
    case 0x06: return L"SRAM";
    case 0x07: return L"RAM";
    case 0x08: return L"ROM";
    case 0x09: return L"FLASH";
    case 0x0A: return L"EEPROM";
    case 0x0B: return L"FEPROM";
    case 0x0C: return L"EPROM";
    case 0x0D: return L"CDRAM";
    case 0x0E: return L"3DRAM";
    case 0x0F: return L"SDRAM";
    case 0x10: return L"SGRAM";
    case 0x11: return L"RDRAM";
    case 0x12: return L"DDR";
    case 0x13: return L"DDR2";
    case 0x14: return L"DDR2 FB";
    case 0x18: return L"DDR3";
    case 0x1A: return L"DDR4";
    case 0x1B: return L"LPDDR";
    case 0x1C: return L"LPDDR2";
    case 0x1D: return L"LPDDR3";
    case 0x1E: return L"LPDDR4";
    case 0x20: return L"HBM";
    case 0x21: return L"HBM2";
    case 0x22: return L"DDR5";
    case 0x23: return L"LPDDR5";
    default:   return L"N/A";
  }
}

/**
  Draw Page 1/5: Firmware & System Information.
**/
STATIC
VOID
DrawSysInfoPage1 (
  IN FIRMWARE_INFO  *Fw,
  IN SYSTEM_INFO    *Sys
  )
{
  CHAR16  TmpBuf[128];

  UiSetColor (COLOR_HEADER, COLOR_BG);
  UiDrawBox (1, 3, 76, 10, L"UEFI Firmware");

  UiSetColor (COLOR_INFO, COLOR_BG);
  UiPrintAt (3, 4, L"  Firmware Vendor  : %s", Fw->FirmwareVendor);
  UiPrintAt (3, 5, L"  Firmware Rev     : 0x%08X", Fw->FirmwareRevision);
  UiPrintAt (3, 6, L"  UEFI Spec        : %d.%d", Fw->UefiSpecMajor, Fw->UefiSpecMinor);

  UtilAsciiToUnicode (Fw->BiosVendor, TmpBuf, 128);
  UiPrintAt (3, 7, L"  BIOS Vendor      : %s", TmpBuf);
  UtilAsciiToUnicode (Fw->BiosVersion, TmpBuf, 128);
  UiPrintAt (3, 8, L"  BIOS Version     : %s", TmpBuf);
  UtilAsciiToUnicode (Fw->BiosReleaseDate, TmpBuf, 128);
  UiPrintAt (3, 9, L"  BIOS Date        : %s", TmpBuf);
  UiPrintAt (3, 10, L"  BIOS Release     : %d.%d", Fw->BiosMajorRelease, Fw->BiosMinorRelease);
  UiPrintAt (3, 11, L"  BIOS ROM Size    : %ld KB", Fw->BiosRomSize / 1024);

  UiSetColor (COLOR_HEADER, COLOR_BG);
  UiDrawBox (1, 13, 76, 10, L"System Information");

  UiSetColor (COLOR_INFO, COLOR_BG);
  UtilAsciiToUnicode (Sys->Manufacturer, TmpBuf, 128);
  UiPrintAt (3, 14, L"  Manufacturer     : %s", TmpBuf);
  UtilAsciiToUnicode (Sys->ProductName, TmpBuf, 128);
  UiPrintAt (3, 15, L"  Product          : %s", TmpBuf);
  UtilAsciiToUnicode (Sys->Version, TmpBuf, 128);
  UiPrintAt (3, 16, L"  Version          : %s", TmpBuf);
  UtilAsciiToUnicode (Sys->SerialNumber, TmpBuf, 128);
  UiPrintAt (3, 17, L"  Serial           : %s", TmpBuf);
  UiPrintAt (3, 18, L"  UUID             : %g", &Sys->SystemUuid);

  UtilAsciiToUnicode (Sys->BoardManufacturer, TmpBuf, 128);
  UiPrintAt (3, 19, L"  Board Mfg        : %s", TmpBuf);
  UtilAsciiToUnicode (Sys->BoardProduct, TmpBuf, 128);
  UiPrintAt (3, 20, L"  Board Product    : %s", TmpBuf);
  UtilAsciiToUnicode (Sys->BoardSerial, TmpBuf, 128);
  UiPrintAt (3, 21, L"  Board Serial     : %s", TmpBuf);
}

/**
  Draw Page 2/5: CPU & Memory.
**/
STATIC
VOID
DrawSysInfoPage2 (
  IN CPU_INFO     *Cpu,
  IN MEMORY_INFO  *Mem
  )
{
  CHAR16  TmpBuf[128];
  UINTN   I;
  UINTN   Row;

  UiSetColor (COLOR_HEADER, COLOR_BG);
  UiDrawBox (1, 3, 76, 7, L"Processor");

  UiSetColor (COLOR_INFO, COLOR_BG);
  UtilAsciiToUnicode (Cpu->ProcessorName, TmpBuf, 128);
  UiPrintAt (3, 4, L"  Processor   : %s", TmpBuf);
  UtilAsciiToUnicode (Cpu->SocketDesignation, TmpBuf, 128);
  UiPrintAt (3, 5, L"  Socket      : %s", TmpBuf);
  UiPrintAt (3, 6, L"  Max Speed   : %d MHz", Cpu->MaxSpeed);
  UiPrintAt (3, 7, L"  Cur Speed   : %d MHz", Cpu->CurrentSpeed);
  UiPrintAt (3, 8, L"  Cores       : %d     Threads: %d", Cpu->CoreCount, Cpu->ThreadCount);

  UiSetColor (COLOR_HEADER, COLOR_BG);
  UiDrawBox (1, 10, 76, 3, L"Memory");

  UiSetColor (COLOR_INFO, COLOR_BG);
  UiPrintAt (3, 11, L"  Total: %d MB   Slots: %d/%d populated",
             Mem->TotalMemoryMB, Mem->PopulatedSlots, Mem->TotalSlots);

  //
  // Memory slot table header
  //
  Row = 13;
  UiSetColor (COLOR_WARNING, COLOR_BG);
  UiPrintAt (2, Row, L" %-14s %7s %6s %6s %-6s %-16s",
             L"Locator", L"Size", L"Speed", L"Conf", L"Type", L"Manufacturer");
  UiSetColor (EFI_LIGHTGRAY, COLOR_BG);
  Row++;

  for (I = 0; I < Mem->TotalSlots && Row < 23; I++) {
    UtilAsciiToUnicode (Mem->Slots[I].DeviceLocator, TmpBuf, 32);

    if (Mem->Slots[I].SizeMB == 0) {
      UiSetColor (EFI_DARKGRAY, COLOR_BG);
      UiPrintAt (2, Row, L" %-14s %7s %6s %6s %-6s %-16s",
                 TmpBuf, L"Empty", L"-", L"-", L"-", L"-");
    } else {
      CHAR16  MfgBuf[32];
      UiSetColor (COLOR_DEFAULT, COLOR_BG);
      UtilAsciiToUnicode (Mem->Slots[I].Manufacturer, MfgBuf, 32);
      UiPrintAt (2, Row, L" %-14s %5d MB %4d  %4d  %-6s %-16s",
                 TmpBuf,
                 Mem->Slots[I].SizeMB,
                 Mem->Slots[I].Speed,
                 Mem->Slots[I].ConfiguredSpeed,
                 GetMemoryTypeName (Mem->Slots[I].MemoryType),
                 MfgBuf);
    }

    Row++;
  }
}

/**
  Draw Page 3/5: PCI Devices.
**/
STATIC
VOID
DrawSysInfoPage3 (
  IN PCI_DEVICE_INFO  *Devices,
  IN UINTN            DeviceCount,
  IN UINTN            ScrollOffset
  )
{
  UINTN  I;
  UINTN  Row;
  UINTN  MaxRows;

  UiSetColor (COLOR_HEADER, COLOR_BG);
  UiDrawBox (1, 3, 76, 3, L"PCI Devices");

  UiSetColor (COLOR_INFO, COLOR_BG);
  UiPrintAt (3, 4, L"  Total: %d devices", DeviceCount);

  //
  // Table header
  //
  Row = 6;
  UiSetColor (COLOR_WARNING, COLOR_BG);
  UiPrintAt (2, Row, L"   Bus:D.F  VenID DevID Class        Vendor");
  Row++;

  UiSetColor (EFI_LIGHTGRAY, COLOR_BG);
  MaxRows = 16;

  for (I = ScrollOffset; I < DeviceCount && (I - ScrollOffset) < MaxRows; I++) {
    Row = 7 + (I - ScrollOffset);

    if (Devices[I].IsNetworkDevice) {
      UiSetColor (COLOR_LAYER3, COLOR_BG);
      UiPrintAt (2, Row, L"%c", L'\x2605');  // star
    } else {
      UiSetColor (COLOR_DEFAULT, COLOR_BG);
      UiPrintAt (2, Row, L" ");
    }

    UiSetColor (Devices[I].IsNetworkDevice ? COLOR_LAYER3 : COLOR_DEFAULT, COLOR_BG);
    Print (L" %02X:%02X.%X  %04X  %04X  %-12s %s",
           Devices[I].Bus, Devices[I].Device, Devices[I].Function,
           Devices[I].VendorId, Devices[I].DeviceId,
           Devices[I].ClassName, Devices[I].VendorName);
  }

  if (DeviceCount > MaxRows) {
    UiSetColor (EFI_DARKGRAY, COLOR_BG);
    UiPrintAt (2, Row + 2, L"  [Up/Down] to scroll (%d-%d of %d)",
               ScrollOffset + 1, I, DeviceCount);
  }
}

/**
  Draw Page 4/5: UEFI Drivers.
**/
STATIC
VOID
DrawSysInfoPage4 (
  IN DRIVER_INFO  *Drivers,
  IN UINTN        DriverCount,
  IN UINTN        ScrollOffset,
  IN BOOLEAN      FilterNetwork
  )
{
  UINTN  I;
  UINTN  Row;
  UINTN  MaxRows;
  UINTN  Displayed;

  UiSetColor (COLOR_HEADER, COLOR_BG);
  UiDrawBox (1, 3, 76, 3, L"UEFI Loaded Images");

  UiSetColor (COLOR_INFO, COLOR_BG);
  UiPrintAt (3, 4, L"  Total: %d images", DriverCount);

  //
  // Table header
  //
  Row = 6;
  UiSetColor (COLOR_WARNING, COLOR_BG);
  UiPrintAt (2, Row, L" %-3s %-38s %8s %s", L"#", L"Name", L"Size", L"Type");
  Row++;

  MaxRows = 15;
  Displayed = 0;

  for (I = 0; I < DriverCount && Displayed < MaxRows; I++) {
    if (Displayed < ScrollOffset) {
      Displayed++;
      continue;
    }

    Row = 7 + (Displayed - ScrollOffset);
    if (Row >= 22) {
      break;
    }

    UiSetColor (Drivers[I].IsDriver ? COLOR_INFO : EFI_LIGHTGRAY, COLOR_BG);
    UiPrintAt (2, Row, L" %3d %-38.38s %6ld KB %s",
               I + 1,
               Drivers[I].Name,
               Drivers[I].ImageSize / 1024,
               Drivers[I].IsDriver ? L"Driver" : L"App");

    Displayed++;
  }

  UiSetColor (EFI_DARKGRAY, COLOR_BG);
  UiPrintAt (2, 23, L"  [Up/Down] scroll");
}

/**
  Draw Page 5/5: ACPI & Configuration Tables.
**/
STATIC
VOID
DrawSysInfoPage5 (
  IN ACPI_BASIC_INFO  *Acpi
  )
{
  CHAR16  OemBuf[8];
  UINTN   I;
  UINTN   Row;

  UiSetColor (COLOR_HEADER, COLOR_BG);
  UiDrawBox (1, 3, 76, 9, L"ACPI Information");

  UiSetColor (COLOR_INFO, COLOR_BG);
  UiPrintAt (3, 4, L"  ACPI Revision    : %d", Acpi->AcpiRevision);
  UtilAsciiToUnicode (Acpi->OemId, OemBuf, 8);
  UiPrintAt (3, 5, L"  OEM ID           : %s", OemBuf);
  UiPrintAt (3, 6, L"  XSDT Tables      : %d", Acpi->XsdtTableCount);
  UiPrintAt (3, 7, L"  DSDT             : %s", Acpi->HasDsdt ? L"Present" : L"Not found");
  UiPrintAt (3, 8, L"  FADT             : %s", Acpi->HasFadt ? L"Present" : L"Not found");
  UiPrintAt (3, 9, L"  MADT (APIC)      : %s", Acpi->HasMadt ? L"Present" : L"Not found");
  UiPrintAt (3, 10, L"  MCFG (PCIe)      : %s", Acpi->HasMcfg ? L"Present" : L"Not found");

  //
  // EFI Configuration Tables
  //
  UiSetColor (COLOR_HEADER, COLOR_BG);
  UiDrawBox (1, 12, 76, 3, L"EFI Configuration Tables");

  UiSetColor (COLOR_INFO, COLOR_BG);
  UiPrintAt (3, 13, L"  Count: %d tables", gST->NumberOfTableEntries);

  Row = 15;
  UiSetColor (COLOR_WARNING, COLOR_BG);
  UiPrintAt (2, Row, L" %-3s %-38s", L"#", L"GUID");
  Row++;

  UiSetColor (EFI_LIGHTGRAY, COLOR_BG);
  for (I = 0; I < gST->NumberOfTableEntries && Row < 23; I++) {
    UiPrintAt (2, Row, L" %3d %g", I + 1,
               &gST->ConfigurationTable[I].VendorGuid);
    Row++;
  }
}

/**
  Show 5-page System Information with left/right navigation.
**/
EFI_STATUS
ShowSystemInfo (
  VOID
  )
{
  UINTN           CurrentPage;
  EFI_INPUT_KEY   Key;
  BOOLEAN         Running;
  FIRMWARE_INFO   FwInfo;
  SYSTEM_INFO     SysInfo;
  CPU_INFO        CpuInfo;
  MEMORY_INFO     MemInfo;
  ACPI_BASIC_INFO AcpiInfo;
  PCI_DEVICE_INFO *PciDevices;
  UINTN           PciCount;
  DRIVER_INFO     *DriverList;
  UINTN           DrvCount;
  UINTN           PciScroll;
  UINTN           DrvScroll;

  //
  // Collect all data
  //
  CollectFirmwareInfo (&FwInfo);
  CollectSystemInfo (&SysInfo);
  CollectCpuInfo (&CpuInfo);
  CollectMemoryInfo (&MemInfo);
  CollectAcpiInfo (&AcpiInfo);

  //
  // Allocate and enumerate PCI devices
  //
  PciDevices = AllocateZeroPool (MAX_PCI_DEVICES * sizeof (PCI_DEVICE_INFO));
  PciCount = MAX_PCI_DEVICES;
  if (PciDevices != NULL) {
    EnumeratePciDevices (PciDevices, &PciCount);
  } else {
    PciCount = 0;
  }

  //
  // Allocate and enumerate drivers
  //
  DriverList = AllocateZeroPool (MAX_DRIVERS * sizeof (DRIVER_INFO));
  DrvCount = MAX_DRIVERS;
  if (DriverList != NULL) {
    EnumerateDrivers (DriverList, &DrvCount);
  } else {
    DrvCount = 0;
  }

  CurrentPage = 1;
  PciScroll = 0;
  DrvScroll = 0;
  Running = TRUE;

  while (Running) {
    UiClearScreen ();
    UiDrawHeader ();

    //
    // Page indicator
    //
    UiSetColor (COLOR_WARNING, COLOR_BG);
    UiPrintAt (55, 1, L"Page %d/%d", CurrentPage, SYSINFO_TOTAL_PAGES);

    //
    // Draw current page
    //
    switch (CurrentPage) {
      case 1:
        DrawSysInfoPage1 (&FwInfo, &SysInfo);
        break;
      case 2:
        DrawSysInfoPage2 (&CpuInfo, &MemInfo);
        break;
      case 3:
        DrawSysInfoPage3 (PciDevices, PciCount, PciScroll);
        break;
      case 4:
        DrawSysInfoPage4 (DriverList, DrvCount, DrvScroll, FALSE);
        break;
      case 5:
        DrawSysInfoPage5 (&AcpiInfo);
        break;
    }

    UiDrawStatusBar (L"[<-/->] Page  [Up/Down] Scroll  [ESC] Back");

    Key = UiWaitKey ();

    if (Key.ScanCode == SCAN_RIGHT) {
      if (CurrentPage < SYSINFO_TOTAL_PAGES) {
        CurrentPage++;
        PciScroll = 0;
        DrvScroll = 0;
      }
    } else if (Key.ScanCode == SCAN_LEFT) {
      if (CurrentPage > 1) {
        CurrentPage--;
        PciScroll = 0;
        DrvScroll = 0;
      }
    } else if (Key.ScanCode == SCAN_DOWN) {
      if (CurrentPage == 3 && PciScroll + 16 < PciCount) {
        PciScroll++;
      } else if (CurrentPage == 4 && DrvScroll + 15 < DrvCount) {
        DrvScroll++;
      }
    } else if (Key.ScanCode == SCAN_UP) {
      if (CurrentPage == 3 && PciScroll > 0) {
        PciScroll--;
      } else if (CurrentPage == 4 && DrvScroll > 0) {
        DrvScroll--;
      }
    } else if (Key.ScanCode == SCAN_ESC) {
      Running = FALSE;
    } else if (Key.UnicodeChar == L'q' || Key.UnicodeChar == L'Q') {
      Running = FALSE;
    }
  }

  //
  // Free allocated memory
  //
  if (PciDevices != NULL) {
    FreePool (PciDevices);
  }

  if (DriverList != NULL) {
    FreePool (DriverList);
  }

  return EFI_SUCCESS;
}

/**
  Handle main menu key selection.

  @param[in]  Key  The pressed key.

  @retval TRUE   Continue the menu loop.
  @retval FALSE  Exit the application.
**/
STATIC
BOOLEAN
HandleMainMenuKey (
  IN EFI_INPUT_KEY  Key
  )
{
  CHAR16  Ch;

  Ch = Key.UnicodeChar;

  if (Ch >= L'a' && Ch <= L'z') {
    Ch = Ch - L'a' + L'A';
  }

  switch (Ch) {
    case L'S':
      ShowSystemInfo ();
      break;

    case L'N':
      ShowNetworkInterfaces ();
      break;

    case L'T':
      ShowTestMenu ();
      break;

    case L'C':
      ShowPacketCapture ();
      break;

    case L'R':
      ShowReports ();
      break;

    case L'Q':
      return FALSE;

    default:
      break;
  }

  return TRUE;
}

/**
  Application entry point.

  @param[in]  ImageHandle  The firmware allocated handle for the EFI image.
  @param[in]  SystemTable  A pointer to the EFI System Table.

  @retval EFI_SUCCESS  The application exited normally.
**/
EFI_STATUS
EFIAPI
DDTSoftNetTestMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_INPUT_KEY  Key;
  BOOLEAN        Running;

  //
  // Clear screen and show splash
  //
  UiClearScreen ();
  UiDrawHeader ();

  UiSetColor (COLOR_INFO, COLOR_BG);
  UiPrintAt (2, 5, L"  Initializing...");
  gBS->Stall (500000);

  //
  // Main menu loop
  //
  Running = TRUE;
  while (Running) {
    UiClearScreen ();
    UiDrawHeader ();
    UiDrawMenu (mMainMenu, MAIN_MENU_COUNT, 0);
    UiDrawStatusBar (L"Select an option [S/N/T/C/R/Q]");

    Key = UiWaitKey ();
    Running = HandleMainMenuKey (Key);
  }

  //
  // Exit
  //
  UiClearScreen ();
  UiSetColor (COLOR_SUCCESS, COLOR_BG);
  Print (L"\n  DDTSoft - Goodbye!\n\n");
  UiResetColor ();

  return EFI_SUCCESS;
}

//
// Stub implementations for remaining menu items
//

EFI_STATUS
ShowNetworkInterfaces (
  VOID
  )
{
  UiShowComingSoon (L"Network Interfaces");
  return EFI_SUCCESS;
}

EFI_STATUS
ShowTestMenu (
  VOID
  )
{
  UiShowComingSoon (L"Run Tests");
  return EFI_SUCCESS;
}

EFI_STATUS
ShowPacketCapture (
  VOID
  )
{
  UiShowComingSoon (L"Packet Capture");
  return EFI_SUCCESS;
}

EFI_STATUS
ShowReports (
  VOID
  )
{
  UiShowComingSoon (L"Reports");
  return EFI_SUCCESS;
}
