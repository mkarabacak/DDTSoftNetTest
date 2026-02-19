/** @file
  DDTSoftNetTest - Main entry point and menu loop.
  EFI Network Test & OSI Layer Analyzer.
**/

#include <DDTSoftNetTest.h>
#include <UiRenderer.h>
#include <SystemInfo.h>
#include <SystemInfoView.h>
#include <PciIds.h>
#include <OsiLayers.h>
#include <PacketDefs.h>
#include <ProtocolProbe.h>

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
  Draw NIC list view with two sections: SNP and PCI NIC.
**/
STATIC
VOID
DrawNicList (
  IN NIC_INFO      *Nics,
  IN UINTN         NicCount,
  IN PCI_NIC_INFO  *PciNics,
  IN UINTN         PciNicCount,
  IN UINTN         Selected,
  IN UINTN         ScrollOffset
  )
{
  UINTN   I;
  UINTN   Row;
  UINTN   ScrH;
  UINTN   MaxRows;
  UINTN   TotalRows;
  UINTN   CurrentRow;
  CHAR16  MacStr[20];
  UINTN   BoxW;

  ScrH = UiGetScreenHeight ();
  BoxW = UiGetScreenWidth () - 2;
  if (BoxW < 76) BoxW = 76;

  //
  // Box header (rows 3-5)
  //
  UiSetColor (COLOR_HEADER, COLOR_BG);
  UiDrawBox (1, 3, BoxW, 3, L"Network Interfaces");

  UiSetColor (COLOR_INFO, COLOR_BG);
  UiPrintAt (3, 4, L"  SNP: %d  |  PCI NIC: %d", (int)NicCount, (int)PciNicCount);

  MaxRows = (ScrH > 10) ? (ScrH - 9) : 14;

  if (NicCount == 0 && PciNicCount == 0) {
    UiClearLines (6, 6 + MaxRows);
    UiSetColor (COLOR_WARNING, COLOR_BG);
    UiPrintAt (3, 7, L"  No network interfaces detected.");
    UiPrintAt (3, 9, L"  Make sure network drivers are loaded.");
    return;
  }

  TotalRows = 1 + NicCount * 2 + 1 + 1 + PciNicCount * 2;

  Row = 6;
  CurrentRow = 0;

  //
  // === SNP Section Header ===
  //
  if (CurrentRow >= ScrollOffset && (CurrentRow - ScrollOffset) < MaxRows) {
    UiSetColor (COLOR_LAYER2, COLOR_BG);
    UiPrintAt (2, Row, L" SNP Network Interfaces (%d)", (int)NicCount);
    Row++;
  }
  CurrentRow++;

  //
  // === SNP Entries (2 rows each) ===
  //
  for (I = 0; I < NicCount; I++) {
    UtilFormatMac (Nics[I].CurrentMac.Addr, MacStr);

    if (CurrentRow >= ScrollOffset && (CurrentRow - ScrollOffset) < MaxRows) {
      if (I == Selected) {
        UiSetColor (EFI_WHITE, EFI_BACKGROUND_BLUE);
      } else {
        UiSetColor (COLOR_DEFAULT, COLOR_BG);
      }
      UiPrintAt (2, Row, L"  [%d] %-24.24s %s",
                 (int)I, Nics[I].Name, MacStr);
      Row++;
    }
    CurrentRow++;

    if (CurrentRow >= ScrollOffset && (CurrentRow - ScrollOffset) < MaxRows) {
      if (I == Selected) {
        UiSetColor (EFI_LIGHTGRAY, EFI_BACKGROUND_BLUE);
      } else {
        UiSetColor (EFI_LIGHTGRAY, COLOR_BG);
      }
      UiPrintAt (2, Row, L"       %s | Link: %-4s | %04X:%04X",
                 GetSnpStateName (Nics[I].State),
                 Nics[I].MediaPresent ? L"UP" : L"DOWN",
                 Nics[I].HasPciInfo ? Nics[I].PciVendorId : 0,
                 Nics[I].HasPciInfo ? Nics[I].PciDeviceId : 0);
      Row++;
    }
    CurrentRow++;
  }

  //
  // === Blank separator ===
  //
  if (CurrentRow >= ScrollOffset && (CurrentRow - ScrollOffset) < MaxRows) {
    Row++;
  }
  CurrentRow++;

  //
  // === PCI NIC Section Header ===
  //
  if (CurrentRow >= ScrollOffset && (CurrentRow - ScrollOffset) < MaxRows) {
    UiSetColor (COLOR_LAYER3, COLOR_BG);
    UiPrintAt (2, Row, L" PCI Network Controllers (%d)", (int)PciNicCount);
    Row++;
  }
  CurrentRow++;

  //
  // === PCI NIC Entries (2 rows each) ===
  //
  for (I = 0; I < PciNicCount; I++) {
    if (CurrentRow >= ScrollOffset && (CurrentRow - ScrollOffset) < MaxRows) {
      if (Selected >= NicCount && I == Selected - NicCount) {
        UiSetColor (EFI_WHITE, EFI_BACKGROUND_BLUE);
      } else {
        if (!PciNics[I].HasDriver) {
          UiSetColor (COLOR_ERROR, COLOR_BG);          // No driver: RED
        } else if (PciNics[I].HasMac && PciNics[I].MediaPresent) {
          UiSetColor (COLOR_SUCCESS, COLOR_BG);        // Driver + Link UP: GREEN
        } else if (PciNics[I].HasMac && !PciNics[I].MediaPresent) {
          UiSetColor (COLOR_WARNING, COLOR_BG);        // Driver + Link DOWN: YELLOW
        } else {
          UiSetColor (COLOR_INFO, COLOR_BG);           // Driver, no MAC info: CYAN
        }
      }
      UiPrintAt (2, Row, L"  [%d] %-20.20s %02X:%02X.%X  %04X:%04X  %s %s",
                 (int)I,
                 PciNics[I].DeviceModel,
                 PciNics[I].Bus, PciNics[I].Dev, PciNics[I].Func,
                 PciNics[I].VendorId, PciNics[I].DeviceId,
                 PciNics[I].HasDriver ? L"[DRV OK]" : L"[NO DRV]",
                 PciNics[I].HasMac ? (PciNics[I].MediaPresent ? L"Link:UP" : L"Link:DN") : L"Link:--");
      Row++;
    }
    CurrentRow++;

    if (CurrentRow >= ScrollOffset && (CurrentRow - ScrollOffset) < MaxRows) {
      if (Selected >= NicCount && I == Selected - NicCount) {
        UiSetColor (EFI_LIGHTGRAY, EFI_BACKGROUND_BLUE);
      } else if (PciNics[I].HasMac) {
        UiSetColor (PciNics[I].MediaPresent ? COLOR_INFO : EFI_LIGHTGRAY, COLOR_BG);
      } else {
        UiSetColor (EFI_LIGHTGRAY, COLOR_BG);
      }

      if (PciNics[I].HasMac) {
        UiPrintAt (2, Row,
                   L"       MAC: %02X:%02X:%02X:%02X:%02X:%02X  Link: %-4s",
                   PciNics[I].MacAddress[0], PciNics[I].MacAddress[1],
                   PciNics[I].MacAddress[2], PciNics[I].MacAddress[3],
                   PciNics[I].MacAddress[4], PciNics[I].MacAddress[5],
                   PciNics[I].MediaPresent ? L"UP" : L"DOWN");
      } else {
        UiPrintAt (2, Row, L"       MAC: N/A (no driver)");
      }
      Row++;
    }
    CurrentRow++;
  }

  //
  // Clear only unused trailing rows (avoids full-area clear flicker).
  //
  if (Row <= 6 + MaxRows) {
    UiClearLines (Row, 6 + MaxRows);
  }

  //
  // Scroll indicator
  //
  if (TotalRows > MaxRows) {
    UiSetColor (EFI_LIGHTGRAY, COLOR_BG);
    UiPrintAt (2, 6 + MaxRows, L"  [Up/Down/PgUp/PgDn] scroll (%d/%d)",
               (int)(ScrollOffset + 1), (int)TotalRows);
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
      UiSetColor (EFI_LIGHTGRAY, COLOR_BG);
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
    // Protocol Stack — selectable list with echo probe support
    //
    Row = 21;
    UiSetColor (COLOR_HEADER, COLOR_BG);
    UiDrawBox (1, Row, BoxW, 10, L"Protocol Stack");

    Row++;
    {
      UINTN  ProtoIdx;
      struct {
        CHAR16   Key;
        CHAR16   *Name;
        BOOLEAN  Available;
        BOOLEAN  CanProbe;
      } ProtoList[] = {
        { L'1', L"ARP",   Nic->HasArp  || (Nic->Snp != NULL), ProbeIsAvailable (Nic, ProbeArp)  },
        { L'2', L"ICMP",  Nic->HasIp4,                         ProbeIsAvailable (Nic, ProbeIcmp) },
        { L'3', L"UDP4",  Nic->HasUdp4,                        ProbeIsAvailable (Nic, ProbeUdp)  },
        { L'4', L"TCP4",  Nic->HasTcp4,                        ProbeIsAvailable (Nic, ProbeTcp)  },
        { L'5', L"DHCP4", Nic->HasDhcp4,                       FALSE },
        { L'6', L"DNS4",  Nic->HasDns4,                        FALSE },
        { L'7', L"HTTP",  Nic->HasHttp,                        FALSE },
      };
      UINTN  ProtoCount;

      ProtoCount = sizeof (ProtoList) / sizeof (ProtoList[0]);

      for (ProtoIdx = 0; ProtoIdx < ProtoCount; ProtoIdx++) {
        if (ProtoList[ProtoIdx].Available) {
          UiSetColor (COLOR_SUCCESS, COLOR_BG);
          if (ProtoList[ProtoIdx].CanProbe) {
            UiPrintAt (3, Row, L"  [%c] %-6s  Available (Echo Test)",
                       ProtoList[ProtoIdx].Key, ProtoList[ProtoIdx].Name);
          } else {
            UiPrintAt (3, Row, L"  [%c] %-6s  Available",
                       ProtoList[ProtoIdx].Key, ProtoList[ProtoIdx].Name);
          }
        } else {
          UiSetColor (EFI_DARKGRAY, COLOR_BG);
          UiPrintAt (3, Row, L"  [%c] %-6s  N/A",
                     ProtoList[ProtoIdx].Key, ProtoList[ProtoIdx].Name);
        }
        Row++;
      }
    }

    //
    // Extra protocol indicators (MNP, IP6, TLS — no echo test)
    //
    Row++;
    UiSetColor (EFI_LIGHTGRAY, COLOR_BG);
    UiPrintAt (3, Row, L"  MNP:%s  IP6:%s  TLS:%s",
               Nic->HasMnp ? L"+" : L"-",
               Nic->HasIp6 ? L"+" : L"-",
               Nic->HasTls ? L"+" : L"-");

    //
    // Device path
    //
    Row += 2;
    UiSetColor (EFI_LIGHTGRAY, COLOR_BG);
    UiPrintAt (2, Row, L"  Path: %.70s", Nic->DevicePath);
  }
}

/**
  Draw PCI NIC detail view.
**/
STATIC
VOID
DrawPciNicDetail (
  IN PCI_NIC_INFO  *Pci
  )
{
  UINTN   Row;
  UINTN   BoxW;

  BoxW = UiGetScreenWidth () - 2;
  if (BoxW < 76) BoxW = 76;

  UiClearLines (3, UiGetScreenHeight () - 2);

  //
  // PCI Hardware box
  //
  UiSetColor (COLOR_HEADER, COLOR_BG);
  UiDrawBox (1, 3, BoxW, 10, L"PCI NIC Hardware");

  UiSetColor (COLOR_INFO, COLOR_BG);
  UiPrintAt (3, 4,  L"  Vendor       : %s", Pci->VendorName);
  UiPrintAt (3, 5,  L"  Model        : %s", Pci->DeviceModel);
  UiPrintAt (3, 6,  L"  PCI IDs      : %04X:%04X", Pci->VendorId, Pci->DeviceId);
  UiPrintAt (3, 7,  L"  PCI Location : Bus %02X  Dev %02X  Func %X",
             Pci->Bus, Pci->Dev, Pci->Func);

  if (Pci->HasDriver) {
    UiSetColor (COLOR_SUCCESS, COLOR_BG);
    UiPrintAt (3, 9, L"  Driver       : Loaded (SNP active)");
  } else {
    UiSetColor (COLOR_ERROR, COLOR_BG);
    UiPrintAt (3, 9, L"  Driver       : NOT LOADED");
  }

  if (Pci->HasMac) {
    UiSetColor (COLOR_INFO, COLOR_BG);
    UiPrintAt (3, 10, L"  MAC Address  : %02X:%02X:%02X:%02X:%02X:%02X",
               Pci->MacAddress[0], Pci->MacAddress[1],
               Pci->MacAddress[2], Pci->MacAddress[3],
               Pci->MacAddress[4], Pci->MacAddress[5]);
    UiPrintAt (3, 11, L"  Link Status  : %s",
               Pci->MediaPresent ? L"UP (connected)" : L"DOWN (no link)");
  } else {
    UiSetColor (EFI_LIGHTGRAY, COLOR_BG);
    UiPrintAt (3, 10, L"  MAC Address  : N/A (no driver)");
    UiPrintAt (3, 11, L"  Link Status  : N/A (no driver)");
  }

  //
  // SNP Match info
  //
  Row = 14;
  UiSetColor (COLOR_HEADER, COLOR_BG);
  UiDrawBox (1, Row, BoxW, 5, L"SNP Association");
  Row++;

  if (Pci->MatchedSnp) {
    UiSetColor (COLOR_SUCCESS, COLOR_BG);
    UiPrintAt (3, Row, L"  Matched to SNP NIC index: %d", (int)Pci->SnpIndex);
    Row++;
    UiSetColor (COLOR_INFO, COLOR_BG);
    UiPrintAt (3, Row, L"  Use the SNP NIC detail for full protocol info");
  } else {
    UiSetColor (COLOR_WARNING, COLOR_BG);
    UiPrintAt (3, Row, L"  No SNP driver bound to this PCI device");
    Row++;
    if (!Pci->HasDriver) {
      UiSetColor (EFI_LIGHTGRAY, COLOR_BG);
      UiPrintAt (3, Row, L"  Load a network driver to enable this NIC");
    }
  }
}

/**
  Run periodic protocol echo test.
  Sends probe with SeqId every ~1 second, shows live stats.

  @param[in] Nic       NIC to test on.
  @param[in] Protocol  Protocol to probe (ProbeArp, ProbeIcmp, ProbeUdp, ProbeTcp).
  @param[in] TargetIp  Target IP for probes.
**/
STATIC
VOID
RunProtocolEchoTest (
  IN NIC_INFO          *Nic,
  IN PROBE_PROTOCOL    Protocol,
  IN EFI_IPv4_ADDRESS  *TargetIp
  )
{
  PROBE_STATS     Stats;
  EFI_INPUT_KEY   Key;
  UINTN           BoxW;
  UINTN           Row;
  UINTN           I;
  CHAR16          IpStr[20];
  CONST CHAR16    *ProtoName;

  ProtoName = ProbeGetName (Protocol);
  ProbeInit (&Stats, Protocol);

  UiClearScreen ();
  UiDrawHeader ();

  BoxW = UiGetScreenWidth () - 2;
  if (BoxW < 66) BoxW = 66;

  //
  // Title
  //
  UtilFormatIpv4 (TargetIp->Addr, IpStr);
  UiSetColor (COLOR_HEADER, COLOR_BG);

  {
    CHAR16  Title[80];
    UnicodeSPrint (Title, sizeof (Title), L"%s Echo Test", ProtoName);
    UiDrawBox (1, 3, BoxW, 20, Title);
  }

  //
  // Static info
  //
  UiSetColor (COLOR_INFO, COLOR_BG);
  UiPrintAt (3, 4, L"  NIC    : %s", Nic->Name);
  UiPrintAt (3, 5, L"  Target : %s", IpStr);
  if (Protocol == ProbeUdp) {
    UiPrintAt (3, 6, L"  Port   : %d (echo)", (int)PROBE_UDP_PORT);
  } else if (Protocol == ProbeTcp) {
    UiPrintAt (3, 6, L"  Port   : %d", (int)PROBE_TCP_PORT);
  }

  UiDrawStatusBar (L"[ESC] Stop echo test");

  //
  // Probe loop — 1 probe per second
  //
  for (;;) {
    //
    // Execute one probe
    //
    UiSetColor (COLOR_WARNING, COLOR_BG);
    UiPrintAt (3, 8, L"  Probing #%04d ...", (int)Stats.NextSeqId);

    ProbeExecuteOnce (Nic, TargetIp, &Stats);

    //
    // Update stats display
    //
    UiClearLines (8, 21);

    UiSetColor (COLOR_HEADER, COLOR_BG);
    UiDrawSeparator (1, 8, BoxW);

    //
    // Summary line
    //
    Row = 9;
    {
      UINT32  LossPct;

      if (Stats.Sent > 0) {
        LossPct = (Stats.Lost * 100) / Stats.Sent;
      } else {
        LossPct = 0;
      }

      UiSetColor (COLOR_INFO, COLOR_BG);
      UiPrintAt (3, Row, L"  Sent: %d   Recv: %d   Lost: %d (%d%%)",
                 (int)Stats.Sent, (int)Stats.Received, (int)Stats.Lost, (int)LossPct);
    }

    Row++;
    if (Stats.Received > 0) {
      UiSetColor (COLOR_SUCCESS, COLOR_BG);
      UiPrintAt (3, Row, L"  RTT:  Last=%dms  Avg=%dms  Min=%dms  Max=%dms",
                 (int)(Stats.RttLastUs / 1000),
                 (int)(Stats.RttAvgUs / 1000),
                 (int)(Stats.RttMinUs / 1000),
                 (int)(Stats.RttMaxUs / 1000));
    } else {
      UiSetColor (EFI_DARKGRAY, COLOR_BG);
      UiPrintAt (3, Row, L"  RTT:  (no successful probes yet)");
    }

    //
    // History — last PROBE_HISTORY_SIZE results
    //
    Row += 2;
    UiSetColor (COLOR_HEADER, COLOR_BG);
    UiDrawSeparator (1, Row, BoxW);
    Row++;

    {
      UINTN  Count;
      UINTN  Idx;
      UINTN  DisplayCount;

      Count = Stats.Sent;
      if (Count > PROBE_HISTORY_SIZE) {
        Count = PROBE_HISTORY_SIZE;
      }

      DisplayCount = 0;

      for (I = 0; I < Count && Row < 22; I++) {
        //
        // Read from ring buffer, newest first
        //
        if (Stats.HistoryHead == 0) {
          Idx = PROBE_HISTORY_SIZE - 1 - I;
        } else {
          Idx = (Stats.HistoryHead - 1 - I + PROBE_HISTORY_SIZE) % PROBE_HISTORY_SIZE;
        }

        switch (Stats.History[Idx].Status) {
          case PROBE_STATUS_PASS:
            UiSetColor (COLOR_SUCCESS, COLOR_BG);
            UiPrintAt (3, Row, L"  #%04d  PASS   RTT=%dms",
                       (int)Stats.History[Idx].SeqId,
                       (int)(Stats.History[Idx].RttUs / 1000));
            break;

          case PROBE_STATUS_FAIL:
            UiSetColor (COLOR_ERROR, COLOR_BG);
            UiPrintAt (3, Row, L"  #%04d  FAIL   error",
                       (int)Stats.History[Idx].SeqId);
            break;

          case PROBE_STATUS_TIMEOUT:
            UiSetColor (COLOR_WARNING, COLOR_BG);
            UiPrintAt (3, Row, L"  #%04d  TIMEOUT",
                       (int)Stats.History[Idx].SeqId);
            break;

          default:
            UiSetColor (EFI_DARKGRAY, COLOR_BG);
            UiPrintAt (3, Row, L"  #%04d  ...",
                       (int)Stats.History[Idx].SeqId);
            break;
        }

        Row++;
        DisplayCount++;
      }
    }

    UiDrawStatusBar (L"[ESC] Stop echo test");

    //
    // Wait ~1 second for next probe, but check for ESC
    //
    if (UiWaitKeyTimeout (1000, &Key)) {
      if (Key.ScanCode == SCAN_ESC ||
          Key.UnicodeChar == L'q' || Key.UnicodeChar == L'Q') {
        break;
      }
    }
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

  //
  // Companion test uses a dedicated point-to-point link:
  //   EFI  = 192.168.100.10
  //   Comp = 192.168.100.1
  // Do NOT override these from DHCP — companion expects this exact subnet.
  // UDP4 Configure with UseDefaultAddress=FALSE creates its own IP4 child
  // with the manual address, independent of whatever DHCP configured.
  //
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
  UiPrintAt (3, Row, L"  [2/3] Sending HELLO (3 attempts, SNP direct rx)...");
  Row++;

  Status = CompanionConnect (&Link);

  if (EFI_ERROR (Status)) {
    UiSetColor (COLOR_ERROR, COLOR_BG);
    UiPrintAt (3, Row, L"  FAILED: %s (status=%r)", Link.StatusMsg, Status);
    Row++;
    if (Status == EFI_TIMEOUT) {
      UiPrintAt (3, Row, L"  No companion found. Is it running on %d.%d.%d.%d:%d?",
                 (int)CompIp.Addr[0], (int)CompIp.Addr[1],
                 (int)CompIp.Addr[2], (int)CompIp.Addr[3],
                 (int)CONTROL_CHANNEL_PORT);
      Row++;
      UiPrintAt (3, Row, L"  Run: sudo python3 companion.py -i <iface> --ip %d.%d.%d.%d",
                 (int)CompIp.Addr[0], (int)CompIp.Addr[1],
                 (int)CompIp.Addr[2], (int)CompIp.Addr[3]);
    } else if (Status == EFI_NO_MAPPING) {
      UiPrintAt (3, Row, L"  ARP failed — companion unreachable. Check cable & IPs.");
    } else {
      UiPrintAt (3, Row, L"  Check network link and companion configuration.");
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
  PCI_NIC_INFO    *PciNics;
  UINTN           NicCount;
  UINTN           PciNicCount;
  EFI_INPUT_KEY   Key;
  BOOLEAN         Running;
  UINTN           Selected;
  UINTN           ScrollOffset;
  BOOLEAN         DetailView;
  BOOLEAN         NeedFullClear;

  Nics = AllocateZeroPool (MAX_INTERFACES * sizeof (NIC_INFO));
  if (Nics == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  PciNics = AllocateZeroPool (MAX_PCI_NICS * sizeof (PCI_NIC_INFO));
  if (PciNics == NULL) {
    FreePool (Nics);
    return EFI_OUT_OF_RESOURCES;
  }

  //
  // Show loading indicator before NIC discovery
  //
  UiClearScreen ();
  UiDrawHeader ();
  UiSetColor (COLOR_INFO, COLOR_BG);
  UiPrintAt (3, 6, L"  Discovering network interfaces...");

  NicCount = MAX_INTERFACES;
  DiscoverNics (Nics, &NicCount);

  PciNicCount = MAX_PCI_NICS;
  DiscoverPciNics (PciNics, &PciNicCount, Nics, NicCount);

  Selected      = 0;
  ScrollOffset  = 0;
  DetailView    = FALSE;
  NeedFullClear = TRUE;
  Running       = TRUE;

  for (;;) {
    //
    // === REDRAW ===
    //
    if (NeedFullClear) {
      UiClearScreen ();
      UiDrawHeader ();
      NeedFullClear = FALSE;
    }

    if (DetailView) {
      if (Selected < NicCount) {
        DrawNicDetail (&Nics[Selected]);
        UiDrawStatusBar (L"[1-4] Echo Test  [C] Companion  [ESC] Back");
      } else {
        DrawPciNicDetail (&PciNics[Selected - NicCount]);
        UiDrawStatusBar (L"[ESC] Back to list");
      }
    } else {
      DrawNicList (Nics, NicCount, PciNics, PciNicCount, Selected, ScrollOffset);
      UiDrawStatusBar (L"[Up/Down] Select  [Enter] Detail  [ESC] Back");
    }

    //
    // === INPUT LOOP ===
    //
    for (;;) {
      if (UiWaitKeyTimeout (2000, &Key)) {
        break;
      }

      //
      // Timeout — refresh media status (slow HW query, only on idle)
      //
      if (DetailView && Selected < NicCount) {
        BOOLEAN  OldMedia;
        OldMedia = Nics[Selected].MediaPresent;
        NicRefreshMedia (&Nics[Selected]);
        if (Nics[Selected].MediaPresent != OldMedia) {
          UiSetColor (COLOR_INFO, COLOR_BG);
          UiPrintAt (3, 12, L"  Media        : %-14s",
                     Nics[Selected].MediaPresent ? L"Connected" : L"Disconnected");
          UiResetColor ();
        }
      } else if (!DetailView) {
        //
        // List view: refresh all NICs on timeout, then redraw
        //
        UINTN  NicIdx;
        for (NicIdx = 0; NicIdx < NicCount; NicIdx++) {
          NicRefreshMedia (&Nics[NicIdx]);
        }

        DrawNicList (Nics, NicCount, PciNics, PciNicCount, Selected, ScrollOffset);
        UiDrawStatusBar (L"[Up/Down] Select  [Enter] Detail  [ESC] Back  (auto-refresh)");
      }
    }

    //
    // === KEY HANDLING ===
    //
    if (DetailView) {
      if (Key.ScanCode == SCAN_ESC ||
          Key.UnicodeChar == L'q' || Key.UnicodeChar == L'Q') {
        DetailView    = FALSE;
        NeedFullClear = TRUE;
      } else if ((Key.UnicodeChar == L'c' || Key.UnicodeChar == L'C') &&
                 Selected < NicCount) {
        TestCompanionConnection (&Nics[Selected]);
        NeedFullClear = TRUE;
      } else if (Key.UnicodeChar >= L'1' && Key.UnicodeChar <= L'4' &&
                 Selected < NicCount) {
        //
        // Protocol echo test: 1=ARP, 2=ICMP, 3=UDP, 4=TCP
        //
        PROBE_PROTOCOL  ProbeProto;
        EFI_IPv4_ADDRESS  ProbeTarget;

        ProbeProto = (PROBE_PROTOCOL)(Key.UnicodeChar - L'1');

        if (ProbeIsAvailable (&Nics[Selected], ProbeProto)) {
          //
          // Use default companion IP as target
          //
          ProbeTarget = (EFI_IPv4_ADDRESS)DEFAULT_COMPANION_IP;
          RunProtocolEchoTest (&Nics[Selected], ProbeProto, &ProbeTarget);
        }
        NeedFullClear = TRUE;
      }
    } else {
      if (Key.ScanCode == SCAN_ESC ||
          Key.UnicodeChar == L'q' || Key.UnicodeChar == L'Q') {
        Running = FALSE;
        break;
      } else if (Key.ScanCode == SCAN_DOWN) {
        {
          UINTN  TotalItems;

          TotalItems = NicCount + PciNicCount;
          if (TotalItems > 0 && Selected < TotalItems - 1) {
            Selected++;

            //
            // Auto-scroll: compute visual row for the selected item.
            // SNP: header(1) + i*2
            // PCI: header(1) + NicCount*2 + blank(1) + pci_header(1) + j*2
            //
            {
              UINTN  ScrH;
              UINTN  MaxRows;
              UINTN  SelVisRow;

              ScrH    = UiGetScreenHeight ();
              MaxRows = (ScrH > 10) ? (ScrH - 9) : 14;

              if (Selected < NicCount) {
                SelVisRow = 1 + Selected * 2;
              } else {
                SelVisRow = 1 + NicCount * 2 + 1 + 1 + (Selected - NicCount) * 2;
              }

              if (SelVisRow + 1 >= ScrollOffset + MaxRows) {
                ScrollOffset = SelVisRow + 2 - MaxRows;
              }
            }
          }
        }
      } else if (Key.ScanCode == SCAN_UP) {
        if (Selected > 0) {
          Selected--;
          {
            UINTN  SelVisRow;

            if (Selected < NicCount) {
              SelVisRow = 1 + Selected * 2;
            } else {
              SelVisRow = 1 + NicCount * 2 + 1 + 1 + (Selected - NicCount) * 2;
            }

            if (SelVisRow < ScrollOffset) {
              ScrollOffset = SelVisRow;
            }
          }
        }
      } else if (Key.UnicodeChar == CHAR_CARRIAGE_RETURN) {
        if (NicCount + PciNicCount > 0) {
          DetailView    = TRUE;
          NeedFullClear = TRUE;
        }
      } else if (Key.ScanCode == SCAN_PAGE_DOWN) {
        //
        // Scroll down (PCI section may be off-screen)
        //
        {
          UINTN  TotalRows;
          UINTN  ScrH;
          UINTN  MaxRows;

          TotalRows = 1 + NicCount * 2 + 1 + 1 + PciNicCount * 2;
          ScrH      = UiGetScreenHeight ();
          MaxRows   = (ScrH > 10) ? (ScrH - 9) : 14;

          if (ScrollOffset + MaxRows < TotalRows) {
            ScrollOffset += MaxRows / 2;
            if (ScrollOffset + MaxRows > TotalRows) {
              ScrollOffset = (TotalRows > MaxRows) ? TotalRows - MaxRows : 0;
            }
          }
        }
      } else if (Key.ScanCode == SCAN_PAGE_UP) {
        {
          UINTN  ScrH;
          UINTN  MaxRows;

          ScrH    = UiGetScreenHeight ();
          MaxRows = (ScrH > 10) ? (ScrH - 9) : 14;

          if (ScrollOffset > MaxRows / 2) {
            ScrollOffset -= MaxRows / 2;
          } else {
            ScrollOffset = 0;
          }
        }
      }
    }

    if (!Running) {
      break;
    }
  }

  FreePool (PciNics);
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
  UiSetColor (EFI_LIGHTGRAY, COLOR_BG);
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
      case TEST_RESULT_SKIP:  UiSetColor (EFI_LIGHTGRAY, COLOR_BG);  break;
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
    UiSetColor (EFI_LIGHTGRAY, COLOR_BG);
    UiPrintAt (2, 8 + (int)MaxRows, L"  [Up/Down] scroll (%d-%d of %d)",
               (int)(ScrollOffset + 1),
               (int)((ScrollOffset + MaxRows < Count) ? ScrollOffset + MaxRows : Count),
               (int)Count);
  }
}

/**
  ARP warm-up completion callback.
  Sets the BOOLEAN flag pointed to by Context to TRUE.
**/
STATIC
VOID
EFIAPI
ArpWarmupNotify (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  if (Context != NULL) {
    *((BOOLEAN *)Context) = TRUE;
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

  //
  // Refresh media status early — needed for accurate report header and
  // for the SNP warm-up to check MediaPresent before sending frames.
  //
  NicRefreshMedia (Nic);

  //
  // Phase 1: Send ARP request via raw SNP to detect link + prime network.
  // Some NICs (Intel I219-LM) don't update Mode->MediaPresent via GetStatus
  // but Transmit works fine when the link is actually up.
  // Sending a proper ARP request (instead of a nonsense probe) ensures:
  //  - Link detection (Transmit succeeds → link is up)
  //  - Gateway sees our MAC/IP mapping (learns our address)
  //  - If reply comes back via MNP timer, ARP cache gets populated
  //
  if (Config->TargetIp.Addr[0] != 0 && Nic->Snp != NULL &&
      Nic->Snp->Mode != NULL &&
      Nic->Snp->Mode->State == EfiSimpleNetworkInitialized) {
    UINT8  ArpFrame[64];
    UINTN  ArpLen;
    UINT8  *SrcIpPtr;

    UiSetColor (EFI_LIGHTGRAY, COLOR_BG);
    UiPrintAt (3, 7, L"  Network warm-up: detecting link...");

    SrcIpPtr = (Nic->HasIpConfig && Nic->Ipv4Address.Addr[0] != 0)
               ? Nic->Ipv4Address.Addr
               : Config->LocalIp.Addr;

    ArpLen = PktBuildArpRequest (
               ArpFrame,
               Nic->Snp->Mode->CurrentAddress.Addr,
               SrcIpPtr,
               Config->TargetIp.Addr
               );

    if (!EFI_ERROR (Nic->Snp->Transmit (Nic->Snp, 0, ArpLen,
                                          ArpFrame, NULL, NULL, NULL))) {
      Nic->MediaPresent = TRUE;
    }

    {
      UINT32  Tmp = 0;
      VOID    *Rb = NULL;
      Nic->Snp->GetStatus (Nic->Snp, &Tmp, &Rb);  // Drain TX
    }

    //
    // Also send ARP for gateway if different from target
    //
    if (Config->Gateway.Addr[0] != 0 &&
        CompareMem (&Config->Gateway, &Config->TargetIp, 4) != 0) {
      ArpLen = PktBuildArpRequest (
                 ArpFrame,
                 Nic->Snp->Mode->CurrentAddress.Addr,
                 SrcIpPtr,
                 Config->Gateway.Addr
                 );
      Nic->Snp->Transmit (Nic->Snp, 0, ArpLen, ArpFrame, NULL, NULL, NULL);
      {
        UINT32  Tmp2 = 0;
        VOID    *Rb2 = NULL;
        Nic->Snp->GetStatus (Nic->Snp, &Tmp2, &Rb2);
      }
    }
  }

  //
  // Phase 2: Non-blocking ARP warm-up via EFI_ARP_PROTOCOL.
  // KEY INSIGHT: Blocking Arp->Request(NULL) raises TPL to TPL_CALLBACK,
  // which prevents MNP timer events (also TPL_CALLBACK) from firing.
  // MNP can't poll SNP for incoming frames, so ARP replies are never
  // processed — the blocking call always times out.
  //
  // FIX: Use non-blocking Arp->Request(Event) which returns immediately,
  // then poll at TPL_APPLICATION via gBS->Stall(). At TPL_APPLICATION,
  // MNP's 10ms timer can fire, poll SNP->Receive, deliver ARP replies
  // to the ARP module, and signal our completion event.
  //
  if (Config->TargetIp.Addr[0] != 0 && Nic->HasArp) {
    EFI_SERVICE_BINDING_PROTOCOL  *ArpSb;
    EFI_ARP_PROTOCOL              *Arp;
    EFI_HANDLE                    ArpChild;
    EFI_ARP_CONFIG_DATA           ArpCfg;
    EFI_IPv4_ADDRESS              StaAddr;
    EFI_MAC_ADDRESS               Resolved;
    UINT8                         *SrcIp;
    BOOLEAN                       ArpDone;
    EFI_EVENT                     ArpEvent;

    UiPrintAt (3, 7, L"  Network warm-up: resolving ARP...");

    ArpSb    = NULL;
    Arp      = NULL;
    ArpChild = NULL;
    ArpEvent = NULL;

    SrcIp = (Nic->HasIpConfig && Nic->Ipv4Address.Addr[0] != 0)
            ? Nic->Ipv4Address.Addr
            : Config->LocalIp.Addr;

    if (!EFI_ERROR (gBS->OpenProtocol (Nic->Handle,
          &gEfiArpServiceBindingProtocolGuid, (VOID **)&ArpSb,
          gImageHandle, Nic->Handle, EFI_OPEN_PROTOCOL_GET_PROTOCOL)) &&
        !EFI_ERROR (ArpSb->CreateChild (ArpSb, &ArpChild)) &&
        !EFI_ERROR (gBS->OpenProtocol (ArpChild,
          &gEfiArpProtocolGuid, (VOID **)&Arp,
          gImageHandle, Nic->Handle, EFI_OPEN_PROTOCOL_GET_PROTOCOL))) {

      CopyMem (&StaAddr, SrcIp, 4);
      ZeroMem (&ArpCfg, sizeof (ArpCfg));
      ArpCfg.SwAddressType   = 0x0800;
      ArpCfg.SwAddressLength = 4;
      ArpCfg.StationAddress  = &StaAddr;
      ArpCfg.EntryTimeOut    = 0;          // No cache expiry
      ArpCfg.RetryCount      = 10;         // 10 retries = 10 seconds
      ArpCfg.RetryTimeOut    = 10000000;   // 1 second per retry

      if (!EFI_ERROR (Arp->Configure (Arp, &ArpCfg))) {
        //
        // Non-blocking ARP request for target IP.
        // Create event + callback, Arp->Request returns immediately,
        // then we poll at TPL_APPLICATION so MNP timer can process replies.
        //
        ArpDone = FALSE;
        if (!EFI_ERROR (gBS->CreateEvent (EVT_NOTIFY_SIGNAL, TPL_CALLBACK,
              ArpWarmupNotify, &ArpDone, &ArpEvent))) {
          ZeroMem (&Resolved, sizeof (Resolved));
          if (!EFI_ERROR (Arp->Request (Arp, &Config->TargetIp, ArpEvent, &Resolved))) {
            //
            // Poll at TPL_APPLICATION — MNP's 10ms timer fires between
            // Stall calls, receives ARP reply from SNP, delivers to ARP
            // module, which signals our ArpEvent → ArpDone = TRUE.
            // Wait up to 10 seconds.
            //
            for (I = 0; I < 10000 && !ArpDone; I++) {
              gBS->Stall (1000);  // 1ms
            }
          }

          gBS->CloseEvent (ArpEvent);
          ArpEvent = NULL;
        }

        //
        // Also resolve gateway if different (non-blocking)
        //
        if (Config->Gateway.Addr[0] != 0 &&
            CompareMem (&Config->Gateway, &Config->TargetIp, 4) != 0) {
          ArpDone = FALSE;
          if (!EFI_ERROR (gBS->CreateEvent (EVT_NOTIFY_SIGNAL, TPL_CALLBACK,
                ArpWarmupNotify, &ArpDone, &ArpEvent))) {
            ZeroMem (&Resolved, sizeof (Resolved));
            if (!EFI_ERROR (Arp->Request (Arp, &Config->Gateway, ArpEvent, &Resolved))) {
              for (I = 0; I < 5000 && !ArpDone; I++) {
                gBS->Stall (1000);  // 1ms, 5s max for gateway
              }
            }

            gBS->CloseEvent (ArpEvent);
            ArpEvent = NULL;
          }
        }

        Arp->Configure (Arp, NULL);
      }
    }

    if (ArpChild != NULL && ArpSb != NULL) {
      ArpSb->DestroyChild (ArpSb, ArpChild);
    }

    UiPrintAt (3, 7, L"  Network warm-up complete.             ");
  }

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

    UiSetColor (EFI_LIGHTGRAY, COLOR_BG);
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
    Config.TargetPort    = 0;
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

      UiSetColor (EFI_LIGHTGRAY, COLOR_BG);
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

      //
      // Update Config from NIC's actual IP configuration (DHCP/static).
      // Hardcoded defaults (192.168.100.x) won't work if NIC is on
      // a different subnet.
      //
      if (Nics[SelectedNic].HasIpConfig) {
        CopyMem (&Config.LocalIp, &Nics[SelectedNic].Ipv4Address, sizeof (EFI_IPv4_ADDRESS));
        CopyMem (&Config.SubnetMask, &Nics[SelectedNic].SubnetMask, sizeof (EFI_IPv4_ADDRESS));
        if (Nics[SelectedNic].Gateway.Addr[0] != 0 ||
            Nics[SelectedNic].Gateway.Addr[1] != 0 ||
            Nics[SelectedNic].Gateway.Addr[2] != 0 ||
            Nics[SelectedNic].Gateway.Addr[3] != 0) {
          CopyMem (&Config.Gateway, &Nics[SelectedNic].Gateway, sizeof (EFI_IPv4_ADDRESS));
          //
          // If no explicit target was set, use gateway as default target
          // (most likely to respond to ARP/ICMP)
          //
          if (Config.TargetIp.Addr[0] == 192 && Config.TargetIp.Addr[1] == 168 &&
              Config.TargetIp.Addr[2] == 100 && Config.TargetIp.Addr[3] == 1) {
            CopyMem (&Config.TargetIp, &Nics[SelectedNic].Gateway, sizeof (EFI_IPv4_ADDRESS));
          }
        }
      }

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
      UiDrawStatusBar (L"[Up/Down] Scroll  [E] Export  [R] Run again  [ESC] Back");

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
      } else if (Key.UnicodeChar == L'e' || Key.UnicodeChar == L'E') {
        //
        // Export current results to file
        //
        ExportTestResults (
          &Nics[SelectedNic],
          &Config,
          TestPtrs,
          Results,
          ResultCount,
          SelectedLayer
          );

        //
        // Redraw after export screen
        //
        UiClearScreen ();
        UiDrawHeader ();
      } else if (Key.UnicodeChar == L'r' || Key.UnicodeChar == L'R') {
        //
        // Re-run the same tests
        //
        ResultCount  = 0;
        ResultScroll = 0;

        //
        // Refresh Config from NIC (IP may have changed via DHCP renewal)
        //
        if (Nics[SelectedNic].HasIpConfig) {
          CopyMem (&Config.LocalIp, &Nics[SelectedNic].Ipv4Address, sizeof (EFI_IPv4_ADDRESS));
          CopyMem (&Config.SubnetMask, &Nics[SelectedNic].SubnetMask, sizeof (EFI_IPv4_ADDRESS));
          if (Nics[SelectedNic].Gateway.Addr[0] != 0 ||
              Nics[SelectedNic].Gateway.Addr[1] != 0 ||
              Nics[SelectedNic].Gateway.Addr[2] != 0 ||
              Nics[SelectedNic].Gateway.Addr[3] != 0) {
            CopyMem (&Config.Gateway, &Nics[SelectedNic].Gateway, sizeof (EFI_IPv4_ADDRESS));
          }
        }

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
