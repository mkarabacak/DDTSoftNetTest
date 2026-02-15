/** @file
  DDTSoftNetTest - Main entry point and menu loop.
  EFI Network Test & OSI Layer Analyzer.
**/

#include <DDTSoftNetTest.h>
#include <UiRenderer.h>
#include <SystemInfo.h>
#include <PciIds.h>
#include <OsiLayers.h>

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
  UiPrintAt (3, 6, L"  UEFI Spec        : %d.%d", (int)Fw->UefiSpecMajor, (int)Fw->UefiSpecMinor);

  UtilAsciiToUnicode (Fw->BiosVendor, TmpBuf, 128);
  UiPrintAt (3, 7, L"  BIOS Vendor      : %s", TmpBuf);
  UtilAsciiToUnicode (Fw->BiosVersion, TmpBuf, 128);
  UiPrintAt (3, 8, L"  BIOS Version     : %s", TmpBuf);
  UtilAsciiToUnicode (Fw->BiosReleaseDate, TmpBuf, 128);
  UiPrintAt (3, 9, L"  BIOS Date        : %s", TmpBuf);
  UiPrintAt (3, 10, L"  BIOS Release     : %d.%d", (int)Fw->BiosMajorRelease, (int)Fw->BiosMinorRelease);
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
  UiPrintAt (3, 6, L"  Max Speed   : %d MHz", (int)Cpu->MaxSpeed);
  UiPrintAt (3, 7, L"  Cur Speed   : %d MHz", (int)Cpu->CurrentSpeed);
  UiPrintAt (3, 8, L"  Cores       : %d     Threads: %d", (int)Cpu->CoreCount, (int)Cpu->ThreadCount);

  UiSetColor (COLOR_HEADER, COLOR_BG);
  UiDrawBox (1, 10, 76, 3, L"Memory");

  UiSetColor (COLOR_INFO, COLOR_BG);
  UiPrintAt (3, 11, L"  Total: %d MB   Slots: %d/%d populated",
             (int)Mem->TotalMemoryMB, (int)Mem->PopulatedSlots, (int)Mem->TotalSlots);

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
                 (int)Mem->Slots[I].SizeMB,
                 (int)Mem->Slots[I].Speed,
                 (int)Mem->Slots[I].ConfiguredSpeed,
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
  UiPrintAt (3, 4, L"  Total: %d devices", (int)DeviceCount);

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
               (int)(ScrollOffset + 1), (int)I, (int)DeviceCount);
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
  UiPrintAt (3, 4, L"  Total: %d images", (int)DriverCount);

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
               (int)(I + 1),
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
  UiPrintAt (3, 4, L"  ACPI Revision    : %d", (int)Acpi->AcpiRevision);
  UtilAsciiToUnicode (Acpi->OemId, OemBuf, 8);
  UiPrintAt (3, 5, L"  OEM ID           : %s", OemBuf);
  UiPrintAt (3, 6, L"  XSDT Tables      : %d", (int)Acpi->XsdtTableCount);
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
  UiPrintAt (3, 13, L"  Count: %d tables", (int)gST->NumberOfTableEntries);

  Row = 15;
  UiSetColor (COLOR_WARNING, COLOR_BG);
  UiPrintAt (2, Row, L" %-3s %-38s", L"#", L"GUID");
  Row++;

  UiSetColor (EFI_LIGHTGRAY, COLOR_BG);
  for (I = 0; I < gST->NumberOfTableEntries && Row < 23; I++) {
    UiPrintAt (2, Row, L" %3d %g", (int)(I + 1),
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

  //
  // Initial full draw
  //
  UiClearScreen ();
  UiDrawHeader ();

  while (Running) {
    //
    // Clear only the content area (rows 1 and 3+), not the whole screen.
    // This avoids the blank-screen flash on every keypress.
    //
    UiClearLines (1, 1);
    UiClearLines (3, UiGetScreenHeight () - 2);

    //
    // Page indicator
    //
    UiSetColor (COLOR_WARNING, COLOR_BG);
    UiPrintAt (55, 1, L"Page %d/%d", (int)CurrentPage, (int)SYSINFO_TOTAL_PAGES);

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
  // Disable watchdog timer — UEFI sets a 5-min watchdog that reboots
  // the system if the app doesn't return or disable it in time.
  //
  gBS->SetWatchdogTimer (0, 0, 0, NULL);

  //
  // Try to set a higher resolution console mode (wider screen)
  //
  UiSetBestConsoleMode ();
  UiHideCursor ();

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

/**
  Get SNP state name as string.
**/
STATIC CONST CHAR16 *
GetSnpStateName (
  IN UINT32  State
  )
{
  switch (State) {
    case EfiSimpleNetworkStopped:     return L"Stopped";
    case EfiSimpleNetworkStarted:     return L"Started";
    case EfiSimpleNetworkInitialized: return L"Initialized";
    default:                          return L"Unknown";
  }
}

/**
  Draw NIC list view with selection highlight.
**/
STATIC
VOID
DrawNicList (
  IN NIC_INFO  *Nics,
  IN UINTN     NicCount,
  IN UINTN     Selected
  )
{
  UINTN   I;
  UINTN   Row;
  CHAR16  MacStr[20];
  CHAR16  IpStr[20];

  {
    UINTN  BoxW;
    BoxW = UiGetScreenWidth () - 2;
    if (BoxW < 76) BoxW = 76;

    UiSetColor (COLOR_HEADER, COLOR_BG);
    UiDrawBox (1, 3, BoxW, 3, L"Network Interfaces");
  }

  UiSetColor (COLOR_INFO, COLOR_BG);
  UiPrintAt (3, 4, L"  Found: %d interface(s)", (int)NicCount);

  if (NicCount == 0) {
    UiSetColor (COLOR_WARNING, COLOR_BG);
    UiPrintAt (3, 7, L"  No network interfaces detected.");
    UiPrintAt (3, 9, L"  Make sure network drivers are loaded.");
    return;
  }

  //
  // Table header
  //
  Row = 6;
  UiSetColor (COLOR_WARNING, COLOR_BG);
  UiPrintAt (2, Row, L" # %-12s %-26s %-18s %-7s %-15s",
             L"Vendor", L"Model", L"MAC Address", L"Media", L"Address");
  Row++;

  for (I = 0; I < NicCount && Row < 22; I++) {
    UtilFormatMac (Nics[I].CurrentMac.Addr, MacStr);

    if (Nics[I].HasIpConfig) {
      UtilFormatIpv4 (Nics[I].Ipv4Address.Addr, IpStr);
    } else {
      UtilSafeStrCpy (IpStr, L"--", 20);
    }

    if (I == Selected) {
      UiSetColor (EFI_WHITE, EFI_BACKGROUND_BLUE);
    } else {
      UiSetColor (COLOR_DEFAULT, COLOR_BG);
    }

    UiPrintAt (2, Row, L" %d %-12s %-26.26s %-18s %-7s %-15s",
               (int)(I + 1),
               Nics[I].HasPciInfo ? Nics[I].VendorName : L"Unknown",
               Nics[I].HasPciInfo ? Nics[I].DeviceModel : L"--",
               MacStr,
               Nics[I].MediaPresent ? L"Up" : L"Down",
               IpStr);
    Row++;
  }
}

/**
  Draw NIC detail view with full information.
**/
STATIC
VOID
DrawNicDetail (
  IN NIC_INFO  *Nic
  )
{
  CHAR16  MacStr[20];
  CHAR16  IpStr[20];
  UINTN   Row;

  {
    UINTN  BoxW;
    BoxW = UiGetScreenWidth () - 2;
    if (BoxW < 76) BoxW = 76;

    //
    // Hardware Identity box
    //
    UiSetColor (COLOR_HEADER, COLOR_BG);
    UiDrawBox (1, 3, BoxW, 12, L"NIC Hardware");

    UiSetColor (COLOR_INFO, COLOR_BG);
    UiPrintAt (3, 4, L"  Name         : %s", Nic->Name);

    if (Nic->HasPciInfo) {
      UiPrintAt (3, 5, L"  Vendor       : %s", Nic->VendorName);
      UiPrintAt (3, 6, L"  Model        : %s", Nic->DeviceModel);
      UiPrintAt (3, 7, L"  PCI IDs      : %04X:%04X (Sub %04X:%04X)",
                 Nic->PciVendorId, Nic->PciDeviceId,
                 Nic->PciSubsysVendorId, Nic->PciSubsysDeviceId);
      UiPrintAt (3, 8, L"  PCI Location : Bus %02X  Dev %02X  Func %X",
                 Nic->PciBus, Nic->PciDev, Nic->PciFunc);
    } else {
      UiSetColor (EFI_DARKGRAY, COLOR_BG);
      UiPrintAt (3, 5, L"  Vendor       : (PCI info not available)");
      UiSetColor (COLOR_INFO, COLOR_BG);
    }

    UtilFormatMac (Nic->CurrentMac.Addr, MacStr);
    UiPrintAt (3, 9,  L"  Current MAC  : %s", MacStr);

    UtilFormatMac (Nic->PermanentMac.Addr, MacStr);
    UiPrintAt (3, 10, L"  Permanent MAC: %s", MacStr);

    UiPrintAt (3, 11, L"  State        : %s", GetSnpStateName (Nic->State));
    UiPrintAt (3, 12, L"  Media        : %s", Nic->MediaPresent ? L"Connected" : L"Disconnected");
    UiPrintAt (3, 13, L"  Max Packet   : %d bytes   Header: %d bytes",
               (int)Nic->MaxPacketSize, (int)Nic->MediaHeaderSize);

    //
    // IP Configuration box
    //
    UiSetColor (COLOR_HEADER, COLOR_BG);
    UiDrawBox (1, 15, BoxW, 6, L"IP Configuration");

    UiSetColor (COLOR_INFO, COLOR_BG);
    if (Nic->HasIpConfig) {
      UtilFormatIpv4 (Nic->Ipv4Address.Addr, IpStr);
      UiPrintAt (3, 16, L"  IPv4 Address : %s", IpStr);
      UtilFormatIpv4 (Nic->SubnetMask.Addr, IpStr);
      UiPrintAt (3, 17, L"  Subnet Mask  : %s", IpStr);
      UtilFormatIpv4 (Nic->Gateway.Addr, IpStr);
      UiPrintAt (3, 18, L"  Gateway      : %s", IpStr);
    } else {
      UiSetColor (COLOR_WARNING, COLOR_BG);
      UiPrintAt (3, 16, L"  No IPv4 configuration available");
    }

    UiPrintAt (3, 19, L"  MAC Changeable: %s   Multi TX: %s",
               Nic->MacChangeable ? L"Yes" : L"No",
               Nic->MultipleTxSupported ? L"Yes" : L"No");

    //
    // Protocol stack box
    //
    Row = 21;
    UiSetColor (COLOR_HEADER, COLOR_BG);
    UiDrawBox (1, Row, BoxW, 5, L"Protocol Stack");

    Row++;
    UiSetColor (COLOR_LAYER3, COLOR_BG);
    UiPrintAt (3, Row, L"  MNP:%s  ARP:%s  IP4:%s  IP6:%s  TCP4:%s",
               Nic->HasMnp  ? L"+" : L"-",
               Nic->HasArp  ? L"+" : L"-",
               Nic->HasIp4  ? L"+" : L"-",
               Nic->HasIp6  ? L"+" : L"-",
               Nic->HasTcp4 ? L"+" : L"-");
    Row++;
    UiPrintAt (3, Row, L"  UDP4:%s  DHCP4:%s  DNS4:%s  HTTP:%s  TLS:%s",
               Nic->HasUdp4  ? L"+" : L"-",
               Nic->HasDhcp4 ? L"+" : L"-",
               Nic->HasDns4  ? L"+" : L"-",
               Nic->HasHttp  ? L"+" : L"-",
               Nic->HasTls   ? L"+" : L"-");

    //
    // Device path
    //
    Row += 2;
    UiSetColor (EFI_DARKGRAY, COLOR_BG);
    UiPrintAt (2, Row, L"  Path: %.70s", Nic->DevicePath);
  }
}

/**
  Test companion connectivity on the selected NIC.
  Initializes UDP4, sends HELLO, waits for ACK, and displays result.
**/
STATIC
VOID
TestCompanionConnection (
  IN NIC_INFO  *Nic
  )
{
  COMPANION_LINK  Link;
  EFI_STATUS      Status;
  EFI_IPv4_ADDRESS LocalIp  = DEFAULT_LOCAL_IP;
  EFI_IPv4_ADDRESS CompIp   = DEFAULT_COMPANION_IP;
  EFI_IPv4_ADDRESS Mask     = DEFAULT_SUBNET_MASK;
  CHAR16          IpStr[20];
  UINTN           Row;

  UiClearScreen ();
  UiDrawHeader ();

  UiSetColor (COLOR_HEADER, COLOR_BG);
  UiDrawBox (1, 3, 76, 20, L"Companion Link Test");

  //
  // Show configuration
  //
  Row = 4;
  UiSetColor (COLOR_INFO, COLOR_BG);
  UiPrintAt (3, Row, L"  NIC          : %s", Nic->Name);
  Row++;

  UtilFormatIpv4 (LocalIp.Addr, IpStr);
  UiPrintAt (3, Row, L"  Local IP     : %s", IpStr);
  Row++;

  UtilFormatIpv4 (CompIp.Addr, IpStr);
  UiPrintAt (3, Row, L"  Companion IP : %s", IpStr);
  Row++;

  UiPrintAt (3, Row, L"  Port         : %d", (int)CONTROL_CHANNEL_PORT);
  Row += 2;

  //
  // Step 1: Initialize
  //
  UiSetColor (COLOR_WARNING, COLOR_BG);
  UiPrintAt (3, Row, L"  [1/3] Initializing UDP4 channel...");

  Status = CompanionInit (&Link, Nic->Handle, &LocalIp, &CompIp, &Mask);
  Row++;

  if (EFI_ERROR (Status)) {
    UiSetColor (COLOR_ERROR, COLOR_BG);
    UiPrintAt (3, Row, L"  FAILED: %s", Link.StatusMsg);
    Row++;
    UiPrintAt (3, Row, L"  EFI_STATUS = %r", Status);
    goto Done;
  }

  UiSetColor (COLOR_SUCCESS, COLOR_BG);
  UiPrintAt (3, Row, L"  OK: %s", Link.StatusMsg);
  Row += 2;

  //
  // Step 2: Connect (HELLO handshake)
  //
  UiSetColor (COLOR_WARNING, COLOR_BG);
  UiPrintAt (3, Row, L"  [2/3] Sending HELLO to companion...");
  Row++;

  Status = CompanionConnect (&Link);

  if (EFI_ERROR (Status)) {
    UiSetColor (COLOR_ERROR, COLOR_BG);
    UiPrintAt (3, Row, L"  FAILED: %s", Link.StatusMsg);
    Row++;
    if (Status == EFI_TIMEOUT) {
      UiPrintAt (3, Row, L"  No companion found. Is it running?");
    } else {
      UiPrintAt (3, Row, L"  EFI_STATUS = %r", Status);
    }
    CompanionDestroy (&Link);
    goto Done;
  }

  UiSetColor (COLOR_SUCCESS, COLOR_BG);
  UiPrintAt (3, Row, L"  OK: %s", Link.StatusMsg);
  Row += 2;

  //
  // Step 3: Disconnect
  //
  UiSetColor (COLOR_WARNING, COLOR_BG);
  UiPrintAt (3, Row, L"  [3/3] Disconnecting...");
  Row++;

  CompanionDisconnect (&Link);

  UiSetColor (COLOR_SUCCESS, COLOR_BG);
  UiPrintAt (3, Row, L"  OK: %s", Link.StatusMsg);
  Row += 2;

  //
  // Summary
  //
  UiSetColor (COLOR_HEADER, COLOR_BG);
  UiDrawSeparator (1, Row, 76);
  Row++;
  UiSetColor (COLOR_SUCCESS, COLOR_BG);
  UiPrintAt (3, Row, L"  Companion link test PASSED");

  CompanionDestroy (&Link);

Done:
  UiDrawStatusBar (L"Press any key to return");
  UiWaitKey ();
}

/**
  Show Network Interfaces UI with list and detail views.
**/
EFI_STATUS
ShowNetworkInterfaces (
  VOID
  )
{
  NIC_INFO        *Nics;
  UINTN           NicCount;
  EFI_INPUT_KEY   Key;
  BOOLEAN         Running;
  UINTN           Selected;
  BOOLEAN         DetailView;

  Nics = AllocateZeroPool (MAX_INTERFACES * sizeof (NIC_INFO));
  if (Nics == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  NicCount = MAX_INTERFACES;
  DiscoverNics (Nics, &NicCount);

  Selected = 0;
  DetailView = FALSE;
  Running = TRUE;

  //
  // Initial full draw
  //
  UiClearScreen ();
  UiDrawHeader ();

  while (Running) {
    //
    // Clear only the content area below the header
    //
    UiClearLines (3, UiGetScreenHeight () - 2);

    if (DetailView) {
      DrawNicDetail (&Nics[Selected]);
      UiDrawStatusBar (L"[C] Companion Test  [ESC] Back to list");
    } else {
      DrawNicList (Nics, NicCount, Selected);
      UiDrawStatusBar (L"[Up/Down] Select  [Enter] Detail  [ESC] Back");
    }

    Key = UiWaitKey ();

    if (DetailView) {
      if (Key.ScanCode == SCAN_ESC ||
          Key.UnicodeChar == L'q' || Key.UnicodeChar == L'Q') {
        DetailView = FALSE;
      } else if (Key.UnicodeChar == L'c' || Key.UnicodeChar == L'C') {
        TestCompanionConnection (&Nics[Selected]);
      }
    } else {
      if (Key.ScanCode == SCAN_ESC ||
          Key.UnicodeChar == L'q' || Key.UnicodeChar == L'Q') {
        Running = FALSE;
      } else if (Key.ScanCode == SCAN_DOWN) {
        if (NicCount > 0 && Selected < NicCount - 1) {
          Selected++;
        }
      } else if (Key.ScanCode == SCAN_UP) {
        if (Selected > 0) {
          Selected--;
        }
      } else if (Key.UnicodeChar == CHAR_CARRIAGE_RETURN) {
        if (NicCount > 0) {
          DetailView = TRUE;
        }
      }
    }
  }

  FreePool (Nics);
  return EFI_SUCCESS;
}

/**
  Draw test results summary screen.
**/
STATIC
VOID
DrawTestResults (
  IN TEST_DEFINITION   **Tests,
  IN TEST_RESULT_DATA  *Results,
  IN UINTN             Count,
  IN OSI_LAYER         Layer,
  IN UINTN             ScrollOffset
  )
{
  UINTN   I;
  UINTN   Row;
  UINTN   MaxRows;
  UINTN   PassCount;
  UINTN   FailCount;
  UINTN   SkipCount;
  UINTN   WarnCount;
  UINTN   ErrCount;
  UINTN   BoxW;
  UINTN   ScrH;
  UINTN   SumW;

  BoxW = UiGetScreenWidth () - 2;
  if (BoxW < 76) {
    BoxW = 76;
  }
  ScrH = UiGetScreenHeight ();

  //
  // Summary column width = total width minus fixed columns (# + Lyr + Name + Result + spacing)
  // Fixed: " %2d  %-4s %-22.22s %-6s " = ~40 chars
  //
  SumW = BoxW - 40;
  if (SumW < 20) {
    SumW = 20;
  }

  PassCount = 0;
  FailCount = 0;
  SkipCount = 0;
  WarnCount = 0;
  ErrCount  = 0;

  for (I = 0; I < Count; I++) {
    switch (Results[I].StatusCode) {
      case TEST_RESULT_PASS:  PassCount++; break;
      case TEST_RESULT_FAIL:  FailCount++; break;
      case TEST_RESULT_SKIP:  SkipCount++; break;
      case TEST_RESULT_WARN:  WarnCount++; break;
      case TEST_RESULT_ERROR: ErrCount++;  break;
    }
  }

  //
  // Summary box
  //
  UiSetColor (COLOR_HEADER, COLOR_BG);
  UiDrawBox (1, 3, BoxW, 4, L"Test Results");

  UiSetColor (COLOR_INFO, COLOR_BG);
  UiPrintAt (3, 4, L"  %s  |  Total: %d", RegGetLayerName (Layer), (int)Count);

  UiSetColor (COLOR_SUCCESS, COLOR_BG);
  UiPrintAt (3, 5, L"  PASS:%d", (int)PassCount);
  UiSetColor (COLOR_ERROR, COLOR_BG);
  Print (L"  FAIL:%d", (int)FailCount);
  UiSetColor (COLOR_WARNING, COLOR_BG);
  Print (L"  WARN:%d", (int)WarnCount);
  UiSetColor (EFI_DARKGRAY, COLOR_BG);
  Print (L"  SKIP:%d", (int)SkipCount);
  UiSetColor (COLOR_ERROR, COLOR_BG);
  Print (L"  ERR:%d", (int)ErrCount);

  //
  // Result table header
  //
  Row = 7;
  UiSetColor (COLOR_WARNING, COLOR_BG);
  UiPrintAt (2, Row, L" %-3s %-4s %-22s %-6s %-*s",
             L"#", L"Lyr", L"Test Name", L"Result", (int)SumW, L"Summary");
  Row++;

  //
  // MaxRows adapts to screen height: leave room for header(3) + summary box(4) + table header(1) + footer(2)
  //
  MaxRows = (ScrH > 12) ? (ScrH - 10) : 14;

  for (I = ScrollOffset; I < Count && (I - ScrollOffset) < MaxRows; I++) {
    Row = 8 + (I - ScrollOffset);

    switch (Results[I].StatusCode) {
      case TEST_RESULT_PASS:  UiSetColor (COLOR_SUCCESS, COLOR_BG); break;
      case TEST_RESULT_FAIL:  UiSetColor (COLOR_ERROR, COLOR_BG);   break;
      case TEST_RESULT_WARN:  UiSetColor (COLOR_WARNING, COLOR_BG); break;
      case TEST_RESULT_SKIP:  UiSetColor (EFI_DARKGRAY, COLOR_BG);  break;
      case TEST_RESULT_ERROR: UiSetColor (COLOR_ERROR, COLOR_BG);   break;
      default:                UiSetColor (COLOR_DEFAULT, COLOR_BG);  break;
    }

    UiPrintAt (2, Row, L" %2d  %-4s %-22.22s %-6s %-*.*s",
               (int)(I + 1),
               RegGetLayerShort (Tests[I]->Layer),
               Tests[I]->Name,
               RegGetResultName (Results[I].StatusCode),
               (int)SumW, (int)SumW,
               Results[I].Summary);
  }

  if (Count > MaxRows) {
    UiSetColor (EFI_DARKGRAY, COLOR_BG);
    UiPrintAt (2, 8 + (int)MaxRows, L"  [Up/Down] scroll (%d-%d of %d)",
               (int)(ScrollOffset + 1),
               (int)((ScrollOffset + MaxRows < Count) ? ScrollOffset + MaxRows : Count),
               (int)Count);
  }
}

/**
  Execute tests for a given layer on a NIC with live progress display.
**/
STATIC
EFI_STATUS
ExecuteTestsWithProgress (
  IN  OSI_LAYER         Layer,
  IN  NIC_INFO          *Nic,
  IN  TEST_CONFIG       *Config,
  OUT TEST_DEFINITION   **OutTests,
  OUT TEST_RESULT_DATA  *OutResults,
  OUT UINTN             *OutCount
  )
{
  TEST_DEFINITION  *Tests[MAX_TESTS];
  UINTN            TestCount;
  UINTN            I;
  UINTN            Percent;
  UINTN            BoxW;
  UINTN            BarW;

  TestCount = RegGetTestsByLayer (Layer, Tests, MAX_TESTS);
  *OutCount = 0;

  BoxW = UiGetScreenWidth () - 2;
  if (BoxW < 76) {
    BoxW = 76;
  }
  BarW = BoxW - 6;

  //
  // Draw the static frame once before the loop
  //
  UiClearScreen ();
  UiDrawHeader ();

  UiSetColor (COLOR_HEADER, COLOR_BG);
  UiDrawBox (1, 3, BoxW, 10, L"Running Tests");

  UiSetColor (COLOR_INFO, COLOR_BG);
  UiPrintAt (3, 4, L"  %s", RegGetLayerName (Layer));
  UiPrintAt (3, 5, L"  NIC: %s", Nic->Name);

  for (I = 0; I < TestCount; I++) {
    Percent = (TestCount > 0) ? ((I * 100) / TestCount) : 0;

    //
    // Update only the changing lines (test name, description, progress, type)
    //
    UiClearLines (7, 12);

    UiSetColor (COLOR_INFO, COLOR_BG);
    UiPrintAt (3, 7, L"  Test %d/%d: %s", (int)(I + 1), (int)TestCount, Tests[I]->Name);
    UiPrintAt (3, 8, L"  %s", Tests[I]->Description);

    UiDrawProgress (3, 10, BarW, Percent, L"Progress");

    UiSetColor (EFI_DARKGRAY, COLOR_BG);
    UiPrintAt (3, 12, L"  Type: %-12s |  Target: %s",
               RegGetTypeName (Tests[I]->Type),
               Tests[I]->RequiresTarget ? L"Required    " : L"Not needed  ");

    //
    // Run the test
    //
    RunSingleTest (Tests[I], Nic, Config, &OutResults[I]);
    OutTests[I] = Tests[I];
    (*OutCount)++;
  }

  return EFI_SUCCESS;
}

/**
  Show the Run Tests menu.
  Allows NIC selection, layer selection, test execution, and results viewing.
**/
EFI_STATUS
ShowTestMenu (
  VOID
  )
{
  NIC_INFO          *Nics;
  UINTN             NicCount;
  EFI_INPUT_KEY     Key;
  BOOLEAN           Running;
  UINTN             SelectedNic;
  OSI_LAYER         SelectedLayer;
  TEST_CONFIG       Config;
  TEST_RESULT_DATA  *Results;
  TEST_DEFINITION   **TestPtrs;
  UINTN             ResultCount;
  UINTN             ResultScroll;
  BOOLEAN           HasResults;
  CHAR16            IpStr[20];
  UINTN             BoxW;

  //
  // Initialize test registry
  //
  RegInitAllTests ();

  BoxW = UiGetScreenWidth () - 2;
  if (BoxW < 76) {
    BoxW = 76;
  }

  //
  // Discover NICs
  //
  Nics = AllocateZeroPool (MAX_INTERFACES * sizeof (NIC_INFO));
  if (Nics == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  NicCount = MAX_INTERFACES;
  DiscoverNics (Nics, &NicCount);

  if (NicCount == 0) {
    UiClearScreen ();
    UiDrawHeader ();
    UiSetColor (COLOR_WARNING, COLOR_BG);
    UiPrintAt (3, 5, L"  No network interfaces found.");
    UiPrintAt (3, 7, L"  Cannot run tests without a NIC.");
    UiDrawStatusBar (L"Press any key to return");
    UiWaitKey ();
    FreePool (Nics);
    return EFI_NOT_FOUND;
  }

  //
  // Allocate results
  //
  Results  = AllocateZeroPool (MAX_TESTS * sizeof (TEST_RESULT_DATA));
  TestPtrs = AllocateZeroPool (MAX_TESTS * sizeof (TEST_DEFINITION *));
  if (Results == NULL || TestPtrs == NULL) {
    FreePool (Nics);
    if (Results != NULL) FreePool (Results);
    if (TestPtrs != NULL) FreePool (TestPtrs);
    return EFI_OUT_OF_RESOURCES;
  }

  //
  // Default config
  //
  ZeroMem (&Config, sizeof (TEST_CONFIG));
  {
    EFI_IPv4_ADDRESS TmpLocal = DEFAULT_LOCAL_IP;
    EFI_IPv4_ADDRESS TmpMask  = DEFAULT_SUBNET_MASK;
    EFI_IPv4_ADDRESS TmpGw    = DEFAULT_GATEWAY;
    EFI_IPv4_ADDRESS TmpComp  = DEFAULT_COMPANION_IP;
    CopyMem (&Config.LocalIp, &TmpLocal, sizeof (EFI_IPv4_ADDRESS));
    CopyMem (&Config.SubnetMask, &TmpMask, sizeof (EFI_IPv4_ADDRESS));
    CopyMem (&Config.Gateway, &TmpGw, sizeof (EFI_IPv4_ADDRESS));
    CopyMem (&Config.TargetIp, &TmpComp, sizeof (EFI_IPv4_ADDRESS));
    Config.TimeoutMs     = 3000;
    Config.Iterations    = 1;
    Config.TargetPort    = 80;
    Config.CompanionPort = CONTROL_CHANNEL_PORT;
  }

  SelectedNic   = 0;
  SelectedLayer = OsiLayerAll;
  ResultCount   = 0;
  ResultScroll  = 0;
  HasResults    = FALSE;
  Running       = TRUE;

  while (Running) {
    if (!HasResults) {
      //
      // Layer selection menu
      //
      UiClearScreen ();
      UiDrawHeader ();

      UiSetColor (COLOR_HEADER, COLOR_BG);
      UiDrawBox (1, 3, BoxW, 5, L"Run Tests");

      UiSetColor (COLOR_INFO, COLOR_BG);
      UiPrintAt (3, 4, L"  NIC       : [%d] %s", (int)(SelectedNic + 1), Nics[SelectedNic].Name);

      if (Nics[SelectedNic].HasIpConfig) {
        UtilFormatIpv4 (Nics[SelectedNic].Ipv4Address.Addr, IpStr);
      } else {
        UtilSafeStrCpy (IpStr, L"(not configured)", 20);
      }
      UiPrintAt (3, 5, L"  IP        : %s", IpStr);

      UtilFormatIpv4 (Config.TargetIp.Addr, IpStr);
      UiPrintAt (3, 6, L"  Target IP : %s", IpStr);

      UiSetColor (COLOR_INFO, COLOR_BG);
      UiPrintAt (3, 7, L"  Tests     : %d registered", (int)RegGetTestCount ());

      //
      // Layer selection
      //
      UiSetColor (COLOR_HEADER, COLOR_BG);
      UiDrawBox (1, 9, BoxW, 12, L"Select Test Layer");

      UiSetColor (COLOR_LAYER1, COLOR_BG);
      UiPrintAt (5, 10, L"[1] Layer 1 - Physical        (5 tests)");
      UiSetColor (COLOR_LAYER2, COLOR_BG);
      UiPrintAt (5, 11, L"[2] Layer 2 - Data Link       (7 tests)");
      UiSetColor (COLOR_LAYER3, COLOR_BG);
      UiPrintAt (5, 12, L"[3] Layer 3 - Network        (10 tests)");
      UiSetColor (COLOR_LAYER4, COLOR_BG);
      UiPrintAt (5, 13, L"[4] Layer 4 - Transport       (8 tests)");
      UiSetColor (COLOR_LAYER7, COLOR_BG);
      UiPrintAt (5, 14, L"[7] Layer 7 - Application     (6 tests)");
      UiSetColor (COLOR_DEFAULT, COLOR_BG);
      UiPrintAt (5, 16, L"[A] All Layers               (36 tests)");

      UiSetColor (EFI_DARKGRAY, COLOR_BG);
      UiPrintAt (5, 18, L"[N] Change NIC  [T] Change Target IP");
      UiPrintAt (5, 19, L"[ESC] Back to main menu");

      UiDrawStatusBar (L"Select layer [1/2/3/4/7/A] or [N]IC [T]arget [ESC]");

      Key = UiWaitKey ();

      switch (Key.UnicodeChar) {
        case L'1':
          SelectedLayer = OsiLayerPhysical;
          break;
        case L'2':
          SelectedLayer = OsiLayerDataLink;
          break;
        case L'3':
          SelectedLayer = OsiLayerNetwork;
          break;
        case L'4':
          SelectedLayer = OsiLayerTransport;
          break;
        case L'7':
          SelectedLayer = OsiLayerApplication;
          break;
        case L'a': case L'A':
          SelectedLayer = OsiLayerAll;
          break;
        case L'n': case L'N':
          //
          // Cycle to next NIC
          //
          SelectedNic = (SelectedNic + 1) % NicCount;
          continue;
        case L't': case L'T':
          //
          // Simple target IP toggle: cycle between default companion and 0.0.0.0
          //
          if (Config.TargetIp.Addr[0] != 0) {
            ZeroMem (&Config.TargetIp, sizeof (EFI_IPv4_ADDRESS));
          } else {
            EFI_IPv4_ADDRESS TmpTarget = DEFAULT_COMPANION_IP;
            CopyMem (&Config.TargetIp, &TmpTarget, sizeof (EFI_IPv4_ADDRESS));
          }
          continue;
        default:
          if (Key.ScanCode == SCAN_ESC || Key.UnicodeChar == L'q' || Key.UnicodeChar == L'Q') {
            Running = FALSE;
          }
          continue;
      }

      //
      // Execute tests
      //
      ResultCount  = 0;
      ResultScroll = 0;

      ExecuteTestsWithProgress (
        SelectedLayer,
        &Nics[SelectedNic],
        &Config,
        TestPtrs,
        Results,
        &ResultCount
        );

      HasResults = TRUE;

      //
      // Redraw header after test execution cleared the screen
      //
      UiClearScreen ();
      UiDrawHeader ();

    } else {
      //
      // Results display — clear content area only, keep header stable
      //
      UiClearLines (3, UiGetScreenHeight () - 2);
      DrawTestResults (TestPtrs, Results, ResultCount, SelectedLayer, ResultScroll);
      UiDrawStatusBar (L"[Up/Down] Scroll  [R] Run again  [ESC] Back to menu");

      Key = UiWaitKey ();

      if (Key.ScanCode == SCAN_ESC || Key.UnicodeChar == L'q' || Key.UnicodeChar == L'Q') {
        HasResults = FALSE;
      } else if (Key.ScanCode == SCAN_DOWN) {
        {
          UINTN  VisRows;
          VisRows = (UiGetScreenHeight () > 12) ? (UiGetScreenHeight () - 10) : 14;
          if (ResultCount > VisRows && ResultScroll + VisRows < ResultCount) {
            ResultScroll++;
          }
        }
      } else if (Key.ScanCode == SCAN_UP) {
        if (ResultScroll > 0) {
          ResultScroll--;
        }
      } else if (Key.UnicodeChar == L'r' || Key.UnicodeChar == L'R') {
        //
        // Re-run the same tests
        //
        ResultCount  = 0;
        ResultScroll = 0;

        ExecuteTestsWithProgress (
          SelectedLayer,
          &Nics[SelectedNic],
          &Config,
          TestPtrs,
          Results,
          &ResultCount
          );

        //
        // Redraw header after test execution cleared the screen
        //
        UiClearScreen ();
        UiDrawHeader ();
      }
    }
  }

  FreePool (TestPtrs);
  FreePool (Results);
  FreePool (Nics);
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

//
// ShowReports is implemented in ReportExporter.c
//
