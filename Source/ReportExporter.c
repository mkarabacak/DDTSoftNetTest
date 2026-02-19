/** @file
  Report exporter (TXT, CSV, detailed, binary).
  Provides test result export to files in multiple formats.
  Uses EFI_SIMPLE_FILE_SYSTEM_PROTOCOL for direct file I/O.
**/

#include <DDTSoftNetTest.h>
#include <OsiLayers.h>
#include <UiRenderer.h>
#include <SystemInfo.h>
#include <Guid/FileInfo.h>

//
// ============================================================
// Constants
// ============================================================
//
#define REPORT_LINE_MAX      512
#define REPORT_BUFFER_SIZE   (64 * 1024)
#define REPORT_MAX_FILENAME  64

//
// ============================================================
// Report format enumeration
// ============================================================
//
typedef enum {
  ReportFormatTxt = 0,
  ReportFormatCsv,
  ReportFormatDetailed,
  ReportFormatBinary,
  ReportFormatMax
} REPORT_FORMAT;

//
// ============================================================
// Report data container
// ============================================================
//
typedef struct {
  NIC_INFO          *Nic;
  TEST_CONFIG       *Config;
  TEST_DEFINITION   **TestDefs;
  TEST_RESULT_DATA  *Results;
  UINTN             ResultCount;
  OSI_LAYER         Layer;
  CHAR16            Timestamp[32];
  EFI_TIME          Time;
} REPORT_CONTEXT;

//
// ============================================================
// Static: get current time string
// ============================================================
//
STATIC
VOID
ReportGetTimestamp (
  OUT CHAR16   *OutStr,
  IN  UINTN    MaxLen,
  OUT EFI_TIME *OutTime
  )
{
  EFI_STATUS  Status;
  EFI_TIME    Time;

  Status = gRT->GetTime (&Time, NULL);
  if (EFI_ERROR (Status)) {
    UnicodeSPrint (OutStr, MaxLen * sizeof (CHAR16), L"Unknown");
    ZeroMem (OutTime, sizeof (EFI_TIME));
    return;
  }

  CopyMem (OutTime, &Time, sizeof (EFI_TIME));
  UnicodeSPrint (
    OutStr,
    MaxLen * sizeof (CHAR16),
    L"%04d-%02d-%02d %02d:%02d:%02d",
    Time.Year, Time.Month, Time.Day,
    Time.Hour, Time.Minute, Time.Second
    );
}

//
// ============================================================
// Static: build filename from format and time
// ============================================================
//
STATIC
VOID
ReportBuildFilename (
  IN  REPORT_FORMAT  Format,
  IN  EFI_TIME       *Time,
  OUT CHAR16         *OutName,
  IN  UINTN          MaxLen
  )
{
  CONST CHAR16  *Extension;

  switch (Format) {
    case ReportFormatTxt:      Extension = L"txt";  break;
    case ReportFormatCsv:      Extension = L"csv";  break;
    case ReportFormatDetailed: Extension = L"txt";  break;
    case ReportFormatBinary:   Extension = L"bin";  break;
    default:                   Extension = L"dat";  break;
  }

  UnicodeSPrint (
    OutName,
    MaxLen * sizeof (CHAR16),
    L"DDTSoft_%04d%02d%02d_%02d%02d%02d.%s",
    Time->Year, Time->Month, Time->Day,
    Time->Hour, Time->Minute, Time->Second,
    Extension
    );
}

//
// ============================================================
// Static: open a file on the boot device using EFI_FILE_PROTOCOL
// ============================================================
//
STATIC
EFI_STATUS
ReportOpenFile (
  IN  CONST CHAR16       *Filename,
  OUT EFI_FILE_PROTOCOL  **OutFile
  )
{
  EFI_STATUS                       Status;
  EFI_LOADED_IMAGE_PROTOCOL        *LoadedImage;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Fs;
  EFI_FILE_PROTOCOL                *Root;

  *OutFile = NULL;

  //
  // Get the device we booted from via LoadedImage
  //
  Status = gBS->HandleProtocol (
                  gImageHandle,
                  &gEfiLoadedImageProtocolGuid,
                  (VOID **)&LoadedImage
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Open SimpleFileSystem on the boot device
  //
  Status = gBS->HandleProtocol (
                  LoadedImage->DeviceHandle,
                  &gEfiSimpleFileSystemProtocolGuid,
                  (VOID **)&Fs
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Open the root volume
  //
  Status = Fs->OpenVolume (Fs, &Root);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Create/open the file
  //
  Status = Root->Open (
                   Root,
                   OutFile,
                   (CHAR16 *)Filename,
                   EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
                   0
                   );

  Root->Close (Root);
  return Status;
}

//
// ============================================================
// Static: write a Unicode string to file as ASCII
// ============================================================
//
STATIC
EFI_STATUS
ReportWriteLine (
  IN EFI_FILE_PROTOCOL  *FileHandle,
  IN CONST CHAR16       *Line
  )
{
  CHAR8       AsciiBuf[REPORT_LINE_MAX];
  UINTN       I;
  UINTN       Len;
  UINTN       WriteSize;
  EFI_STATUS  Status;

  //
  // Convert CHAR16 to ASCII
  //
  Len = StrLen (Line);
  if (Len >= REPORT_LINE_MAX - 2) {
    Len = REPORT_LINE_MAX - 3;
  }

  for (I = 0; I < Len; I++) {
    AsciiBuf[I] = (CHAR8)((Line[I] < 0x80) ? Line[I] : '?');
  }

  AsciiBuf[Len]     = '\r';
  AsciiBuf[Len + 1] = '\n';
  WriteSize = Len + 2;

  Status = FileHandle->Write (FileHandle, &WriteSize, AsciiBuf);
  return Status;
}

//
// ============================================================
// Static: write raw bytes to file
// ============================================================
//
STATIC
EFI_STATUS
ReportWriteRaw (
  IN EFI_FILE_PROTOCOL  *FileHandle,
  IN VOID               *Data,
  IN UINTN              DataSize
  )
{
  UINTN  WriteSize;

  WriteSize = DataSize;
  return FileHandle->Write (FileHandle, &WriteSize, Data);
}

//
// ============================================================
// Static: get result status string
// ============================================================
//
STATIC
CONST CHAR16 *
ReportResultStr (
  IN UINT32  StatusCode
  )
{
  switch (StatusCode) {
    case TEST_RESULT_PASS:  return L"PASS";
    case TEST_RESULT_FAIL:  return L"FAIL";
    case TEST_RESULT_SKIP:  return L"SKIP";
    case TEST_RESULT_WARN:  return L"WARN";
    case TEST_RESULT_ERROR: return L"ERROR";
    default:                return L"???";
  }
}

//
// ============================================================
// Static: count results by status
// ============================================================
//
STATIC
VOID
ReportCountResults (
  IN  TEST_RESULT_DATA  *Results,
  IN  UINTN             Count,
  OUT UINTN             *PassCount,
  OUT UINTN             *FailCount,
  OUT UINTN             *WarnCount,
  OUT UINTN             *SkipCount,
  OUT UINTN             *ErrCount
  )
{
  UINTN  I;

  *PassCount = 0;
  *FailCount = 0;
  *WarnCount = 0;
  *SkipCount = 0;
  *ErrCount  = 0;

  for (I = 0; I < Count; I++) {
    switch (Results[I].StatusCode) {
      case TEST_RESULT_PASS:  (*PassCount)++; break;
      case TEST_RESULT_FAIL:  (*FailCount)++; break;
      case TEST_RESULT_WARN:  (*WarnCount)++; break;
      case TEST_RESULT_SKIP:  (*SkipCount)++; break;
      case TEST_RESULT_ERROR: (*ErrCount)++;  break;
    }
  }
}

//
// ============================================================
// Static: get memory type name from SMBIOS type code
// ============================================================
//
STATIC
CONST CHAR16 *
ReportMemTypeName (
  IN UINT8  MemoryType
  )
{
  switch (MemoryType) {
    case 0x12: return L"DDR";
    case 0x13: return L"DDR2";
    case 0x18: return L"DDR3";
    case 0x1A: return L"DDR4";
    case 0x1B: return L"LPDDR4";
    case 0x1C: return L"LPDDR3";
    case 0x22: return L"DDR5";
    case 0x23: return L"LPDDR5";
    default:   return L"Unknown";
  }
}

//
// ============================================================
// TXT Report Export
// ============================================================
//
STATIC
EFI_STATUS
ReportExportTxt (
  IN REPORT_CONTEXT  *Ctx,
  IN CONST CHAR16    *Filename
  )
{
  EFI_FILE_PROTOCOL  *FileHandle;
  EFI_STATUS         Status;
  CHAR16             Line[REPORT_LINE_MAX];
  CHAR16             MacStr[20];
  CHAR16             IpStr[20];
  UINTN              I;
  UINTN              PassCnt, FailCnt, WarnCnt, SkipCnt, ErrCnt;

  Status = ReportOpenFile (Filename, &FileHandle);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Header
  //
  ReportWriteLine (FileHandle, L"================================================================");
  ReportWriteLine (FileHandle, L"  DDTSoft - Network Test Report (TXT)");
  UnicodeSPrint (Line, sizeof (Line), L"  Date: %s", Ctx->Timestamp);
  ReportWriteLine (FileHandle, Line);
  UnicodeSPrint (Line, sizeof (Line), L"  Version: %s", APP_VERSION_STRING);
  ReportWriteLine (FileHandle, Line);
  ReportWriteLine (FileHandle, L"================================================================");
  ReportWriteLine (FileHandle, L"");

  //
  // System Information (from SMBIOS)
  //
  {
    FIRMWARE_INFO  FwInfo;
    SYSTEM_INFO    SysInfo;
    CPU_INFO       CpuInf;
    MEMORY_INFO    MemInfo;
    UINTN          J;

    CollectFirmwareInfo (&FwInfo);
    CollectSystemInfo (&SysInfo);
    CollectCpuInfo (&CpuInf);
    CollectMemoryInfo (&MemInfo);

    ReportWriteLine (FileHandle, L"--- System Information ---");
    UnicodeSPrint (Line, sizeof (Line), L"  Manufacturer : %a", SysInfo.Manufacturer);
    ReportWriteLine (FileHandle, Line);
    UnicodeSPrint (Line, sizeof (Line), L"  Product      : %a", SysInfo.ProductName);
    ReportWriteLine (FileHandle, Line);
    UnicodeSPrint (Line, sizeof (Line), L"  Version      : %a", SysInfo.Version);
    ReportWriteLine (FileHandle, Line);
    UnicodeSPrint (Line, sizeof (Line), L"  Serial No    : %a", SysInfo.SerialNumber);
    ReportWriteLine (FileHandle, Line);
    UnicodeSPrint (Line, sizeof (Line),
                   L"  UUID         : %08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
                   SysInfo.SystemUuid.Data1, SysInfo.SystemUuid.Data2,
                   SysInfo.SystemUuid.Data3,
                   SysInfo.SystemUuid.Data4[0], SysInfo.SystemUuid.Data4[1],
                   SysInfo.SystemUuid.Data4[2], SysInfo.SystemUuid.Data4[3],
                   SysInfo.SystemUuid.Data4[4], SysInfo.SystemUuid.Data4[5],
                   SysInfo.SystemUuid.Data4[6], SysInfo.SystemUuid.Data4[7]);
    ReportWriteLine (FileHandle, Line);
    ReportWriteLine (FileHandle, L"");

    ReportWriteLine (FileHandle, L"--- Board Information ---");
    UnicodeSPrint (Line, sizeof (Line), L"  Manufacturer : %a", SysInfo.BoardManufacturer);
    ReportWriteLine (FileHandle, Line);
    UnicodeSPrint (Line, sizeof (Line), L"  Product      : %a", SysInfo.BoardProduct);
    ReportWriteLine (FileHandle, Line);
    UnicodeSPrint (Line, sizeof (Line), L"  Version      : %a", SysInfo.BoardVersion);
    ReportWriteLine (FileHandle, Line);
    UnicodeSPrint (Line, sizeof (Line), L"  Serial No    : %a", SysInfo.BoardSerial);
    ReportWriteLine (FileHandle, Line);
    ReportWriteLine (FileHandle, L"");

    ReportWriteLine (FileHandle, L"--- Firmware Information ---");
    UnicodeSPrint (Line, sizeof (Line), L"  UEFI Vendor  : %s", FwInfo.FirmwareVendor);
    ReportWriteLine (FileHandle, Line);
    UnicodeSPrint (Line, sizeof (Line), L"  UEFI Spec    : %d.%d",
                   (int)FwInfo.UefiSpecMajor, (int)FwInfo.UefiSpecMinor);
    ReportWriteLine (FileHandle, Line);
    UnicodeSPrint (Line, sizeof (Line), L"  FW Revision  : 0x%08X", FwInfo.FirmwareRevision);
    ReportWriteLine (FileHandle, Line);
    UnicodeSPrint (Line, sizeof (Line), L"  BIOS Vendor  : %a", FwInfo.BiosVendor);
    ReportWriteLine (FileHandle, Line);
    UnicodeSPrint (Line, sizeof (Line), L"  BIOS Version : %a", FwInfo.BiosVersion);
    ReportWriteLine (FileHandle, Line);
    UnicodeSPrint (Line, sizeof (Line), L"  BIOS Date    : %a", FwInfo.BiosReleaseDate);
    ReportWriteLine (FileHandle, Line);
    UnicodeSPrint (Line, sizeof (Line), L"  BIOS Release : %d.%d",
                   (int)FwInfo.BiosMajorRelease, (int)FwInfo.BiosMinorRelease);
    ReportWriteLine (FileHandle, Line);
    UnicodeSPrint (Line, sizeof (Line), L"  BIOS ROM     : %llu KB",
                   FwInfo.BiosRomSize / 1024);
    ReportWriteLine (FileHandle, Line);
    ReportWriteLine (FileHandle, L"");

    ReportWriteLine (FileHandle, L"--- CPU Information ---");
    UnicodeSPrint (Line, sizeof (Line), L"  Processor    : %a", CpuInf.ProcessorName);
    ReportWriteLine (FileHandle, Line);
    UnicodeSPrint (Line, sizeof (Line), L"  Socket       : %a", CpuInf.SocketDesignation);
    ReportWriteLine (FileHandle, Line);
    UnicodeSPrint (Line, sizeof (Line), L"  Max Speed    : %d MHz", (int)CpuInf.MaxSpeed);
    ReportWriteLine (FileHandle, Line);
    UnicodeSPrint (Line, sizeof (Line), L"  Current Speed: %d MHz", (int)CpuInf.CurrentSpeed);
    ReportWriteLine (FileHandle, Line);
    UnicodeSPrint (Line, sizeof (Line), L"  Cores        : %d", (int)CpuInf.CoreCount);
    ReportWriteLine (FileHandle, Line);
    UnicodeSPrint (Line, sizeof (Line), L"  Threads      : %d", (int)CpuInf.ThreadCount);
    ReportWriteLine (FileHandle, Line);
    ReportWriteLine (FileHandle, L"");

    ReportWriteLine (FileHandle, L"--- Memory Information ---");
    UnicodeSPrint (Line, sizeof (Line), L"  Total Memory : %d MB (%d GB)",
                   (int)MemInfo.TotalMemoryMB, (int)(MemInfo.TotalMemoryMB / 1024));
    ReportWriteLine (FileHandle, Line);
    UnicodeSPrint (Line, sizeof (Line), L"  Slots        : %d populated / %d total",
                   (int)MemInfo.PopulatedSlots, (int)MemInfo.TotalSlots);
    ReportWriteLine (FileHandle, Line);

    for (J = 0; J < MemInfo.TotalSlots; J++) {
      if (MemInfo.Slots[J].SizeMB > 0) {
        UnicodeSPrint (Line, sizeof (Line),
                       L"  [%a] %d MB %s @ %d MHz  %a %a",
                       MemInfo.Slots[J].DeviceLocator,
                       (int)MemInfo.Slots[J].SizeMB,
                       ReportMemTypeName (MemInfo.Slots[J].MemoryType),
                       (int)MemInfo.Slots[J].ConfiguredSpeed,
                       MemInfo.Slots[J].Manufacturer,
                       MemInfo.Slots[J].PartNumber);
        ReportWriteLine (FileHandle, Line);
      }
    }

    ReportWriteLine (FileHandle, L"");
  }

  //
  // NIC info
  //
  ReportWriteLine (FileHandle, L"--- NIC Information ---");
  UnicodeSPrint (Line, sizeof (Line), L"  Name: %s", Ctx->Nic->Name);
  ReportWriteLine (FileHandle, Line);

  UtilFormatMac (Ctx->Nic->CurrentMac.Addr, MacStr);
  UnicodeSPrint (Line, sizeof (Line), L"  MAC:  %s", MacStr);
  ReportWriteLine (FileHandle, Line);

  if (Ctx->Nic->HasIpConfig) {
    UtilFormatIpv4 (Ctx->Nic->Ipv4Address.Addr, IpStr);
    UnicodeSPrint (Line, sizeof (Line), L"  IP:   %s", IpStr);
    ReportWriteLine (FileHandle, Line);
  }

  UnicodeSPrint (Line, sizeof (Line), L"  Media: %s",
                 Ctx->Nic->MediaPresent ? L"Connected" : L"Disconnected");
  ReportWriteLine (FileHandle, Line);
  ReportWriteLine (FileHandle, L"");

  //
  // Test target
  //
  ReportWriteLine (FileHandle, L"--- Test Configuration ---");
  UtilFormatIpv4 (Ctx->Config->TargetIp.Addr, IpStr);
  UnicodeSPrint (Line, sizeof (Line), L"  Target IP: %s", IpStr);
  ReportWriteLine (FileHandle, Line);
  UnicodeSPrint (Line, sizeof (Line), L"  Timeout:   %d ms", (int)Ctx->Config->TimeoutMs);
  ReportWriteLine (FileHandle, Line);
  UnicodeSPrint (Line, sizeof (Line), L"  Layer:     %s", RegGetLayerName (Ctx->Layer));
  ReportWriteLine (FileHandle, Line);
  ReportWriteLine (FileHandle, L"");

  //
  // Summary
  //
  ReportCountResults (Ctx->Results, Ctx->ResultCount,
                      &PassCnt, &FailCnt, &WarnCnt, &SkipCnt, &ErrCnt);

  ReportWriteLine (FileHandle, L"--- Results Summary ---");
  UnicodeSPrint (Line, sizeof (Line),
                 L"  Total: %d  Pass: %d  Fail: %d  Warn: %d  Skip: %d  Error: %d",
                 (int)Ctx->ResultCount, (int)PassCnt, (int)FailCnt, (int)WarnCnt, (int)SkipCnt, (int)ErrCnt);
  ReportWriteLine (FileHandle, Line);
  ReportWriteLine (FileHandle, L"");

  //
  // Individual results
  //
  ReportWriteLine (FileHandle, L"--- Test Results ---");
  ReportWriteLine (FileHandle, L"  #   Layer  Result  Duration  Test Name");
  ReportWriteLine (FileHandle, L"  --- -----  ------  --------  ---------");

  for (I = 0; I < Ctx->ResultCount; I++) {
    UnicodeSPrint (
      Line, sizeof (Line),
      L"  %2d  %-5s  %-6s  %5llu ms  %s",
      (int)(I + 1),
      RegGetLayerShort (Ctx->TestDefs[I]->Layer),
      ReportResultStr (Ctx->Results[I].StatusCode),
      Ctx->Results[I].DurationMs,
      Ctx->TestDefs[I]->Name
      );
    ReportWriteLine (FileHandle, Line);

    if (Ctx->Results[I].Summary[0] != L'\0') {
      UnicodeSPrint (Line, sizeof (Line), L"        Summary: %s",
                     Ctx->Results[I].Summary);
      ReportWriteLine (FileHandle, Line);
    }

    if (Ctx->Results[I].StatusCode == TEST_RESULT_FAIL &&
        Ctx->Results[I].FailReason[0] != L'\0') {
      UnicodeSPrint (Line, sizeof (Line), L"        Reason:  %s",
                     Ctx->Results[I].FailReason);
      ReportWriteLine (FileHandle, Line);
      if (Ctx->Results[I].Suggestion[0] != L'\0') {
        UnicodeSPrint (Line, sizeof (Line), L"        Suggest: %s",
                       Ctx->Results[I].Suggestion);
        ReportWriteLine (FileHandle, Line);
      }
    }
  }

  ReportWriteLine (FileHandle, L"");
  ReportWriteLine (FileHandle, L"================================================================");
  ReportWriteLine (FileHandle, L"  Report generated by DDTSoft Network Test & OSI Analyzer");
  ReportWriteLine (FileHandle, L"================================================================");

  FileHandle->Close (FileHandle);
  return EFI_SUCCESS;
}

//
// ============================================================
// CSV Report Export
// ============================================================
//
STATIC
EFI_STATUS
ReportExportCsv (
  IN REPORT_CONTEXT  *Ctx,
  IN CONST CHAR16    *Filename
  )
{
  EFI_FILE_PROTOCOL  *FileHandle;
  EFI_STATUS         Status;
  CHAR16             Line[REPORT_LINE_MAX];
  UINTN              I;

  Status = ReportOpenFile (Filename, &FileHandle);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // CSV header row
  //
  ReportWriteLine (FileHandle,
    L"\"#\",\"Test Name\",\"Layer\",\"Type\",\"Result\",\"Duration(ms)\","
    L"\"Summary\",\"PktSent\",\"PktRecv\",\"BytesSent\",\"BytesRecv\","
    L"\"RTT Min(us)\",\"RTT Avg(us)\",\"RTT Max(us)\",\"RTT Jitter(us)\"");

  //
  // Data rows
  //
  for (I = 0; I < Ctx->ResultCount; I++) {
    UnicodeSPrint (
      Line, sizeof (Line),
      L"%d,\"%s\",\"%s\",\"%s\",\"%s\",%llu,"
      L"\"%s\",%llu,%llu,%llu,%llu,"
      L"%d,%d,%d,%d",
      (int)(I + 1),
      Ctx->TestDefs[I]->Name,
      RegGetLayerShort (Ctx->TestDefs[I]->Layer),
      RegGetTypeName (Ctx->TestDefs[I]->Type),
      ReportResultStr (Ctx->Results[I].StatusCode),
      Ctx->Results[I].DurationMs,
      Ctx->Results[I].Summary,
      Ctx->Results[I].PacketsSent,
      Ctx->Results[I].PacketsReceived,
      Ctx->Results[I].BytesSent,
      Ctx->Results[I].BytesReceived,
      (int)Ctx->Results[I].RttMinUs,
      (int)Ctx->Results[I].RttAvgUs,
      (int)Ctx->Results[I].RttMaxUs,
      (int)Ctx->Results[I].RttJitterUs
      );
    ReportWriteLine (FileHandle, Line);
  }

  FileHandle->Close (FileHandle);
  return EFI_SUCCESS;
}

//
// ============================================================
// Detailed Report Export
// Verbose report with full diagnostics and analysis.
// ============================================================
//
STATIC
EFI_STATUS
ReportExportDetailed (
  IN REPORT_CONTEXT  *Ctx,
  IN CONST CHAR16    *Filename
  )
{
  EFI_FILE_PROTOCOL  *FileHandle;
  EFI_STATUS         Status;
  CHAR16             Line[REPORT_LINE_MAX];
  CHAR16             MacStr[20];
  CHAR16             IpStr[20];
  UINTN              I;
  UINTN              PassCnt, FailCnt, WarnCnt, SkipCnt, ErrCnt;
  UINT64             TotalDurationMs;
  UINT64             TotalPktSent, TotalPktRecv;

  Status = ReportOpenFile (Filename, &FileHandle);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // ── Title ──
  //
  ReportWriteLine (FileHandle, L"################################################################");
  ReportWriteLine (FileHandle, L"##                                                            ##");
  ReportWriteLine (FileHandle, L"##    DDTSoft - Detailed Network Test Report                  ##");
  ReportWriteLine (FileHandle, L"##    EFI Network Test & OSI Layer Analyzer                   ##");
  ReportWriteLine (FileHandle, L"##                                                            ##");
  ReportWriteLine (FileHandle, L"################################################################");
  ReportWriteLine (FileHandle, L"");

  UnicodeSPrint (Line, sizeof (Line), L"Report Date    : %s", Ctx->Timestamp);
  ReportWriteLine (FileHandle, Line);
  UnicodeSPrint (Line, sizeof (Line), L"Application    : %s v%s", APP_FULL_NAME, APP_VERSION_STRING);
  ReportWriteLine (FileHandle, Line);
  UnicodeSPrint (Line, sizeof (Line), L"Test Scope     : %s", RegGetLayerName (Ctx->Layer));
  ReportWriteLine (FileHandle, Line);
  UnicodeSPrint (Line, sizeof (Line), L"Total Tests    : %d", (int)Ctx->ResultCount);
  ReportWriteLine (FileHandle, Line);
  ReportWriteLine (FileHandle, L"");

  //
  // ── Section 1: System Information ──
  //
  {
    FIRMWARE_INFO  FwInfo;
    SYSTEM_INFO    SysInfo;
    CPU_INFO       CpuInf;
    MEMORY_INFO    MemInfo;
    UINTN          Sl;

    CollectFirmwareInfo (&FwInfo);
    CollectSystemInfo (&SysInfo);
    CollectCpuInfo (&CpuInf);
    CollectMemoryInfo (&MemInfo);

    ReportWriteLine (FileHandle, L"================================================================");
    ReportWriteLine (FileHandle, L"  SECTION 1: SYSTEM INFORMATION");
    ReportWriteLine (FileHandle, L"================================================================");
    ReportWriteLine (FileHandle, L"");

    ReportWriteLine (FileHandle, L"  -- System --");
    UnicodeSPrint (Line, sizeof (Line), L"  Manufacturer    : %a", SysInfo.Manufacturer);
    ReportWriteLine (FileHandle, Line);
    UnicodeSPrint (Line, sizeof (Line), L"  Product Name    : %a", SysInfo.ProductName);
    ReportWriteLine (FileHandle, Line);
    UnicodeSPrint (Line, sizeof (Line), L"  Version         : %a", SysInfo.Version);
    ReportWriteLine (FileHandle, Line);
    UnicodeSPrint (Line, sizeof (Line), L"  Serial Number   : %a", SysInfo.SerialNumber);
    ReportWriteLine (FileHandle, Line);
    UnicodeSPrint (Line, sizeof (Line),
                   L"  UUID            : %08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
                   SysInfo.SystemUuid.Data1, SysInfo.SystemUuid.Data2,
                   SysInfo.SystemUuid.Data3,
                   SysInfo.SystemUuid.Data4[0], SysInfo.SystemUuid.Data4[1],
                   SysInfo.SystemUuid.Data4[2], SysInfo.SystemUuid.Data4[3],
                   SysInfo.SystemUuid.Data4[4], SysInfo.SystemUuid.Data4[5],
                   SysInfo.SystemUuid.Data4[6], SysInfo.SystemUuid.Data4[7]);
    ReportWriteLine (FileHandle, Line);
    ReportWriteLine (FileHandle, L"");

    ReportWriteLine (FileHandle, L"  -- Baseboard --");
    UnicodeSPrint (Line, sizeof (Line), L"  Board Mfr       : %a", SysInfo.BoardManufacturer);
    ReportWriteLine (FileHandle, Line);
    UnicodeSPrint (Line, sizeof (Line), L"  Board Product   : %a", SysInfo.BoardProduct);
    ReportWriteLine (FileHandle, Line);
    UnicodeSPrint (Line, sizeof (Line), L"  Board Version   : %a", SysInfo.BoardVersion);
    ReportWriteLine (FileHandle, Line);
    UnicodeSPrint (Line, sizeof (Line), L"  Board Serial    : %a", SysInfo.BoardSerial);
    ReportWriteLine (FileHandle, Line);
    ReportWriteLine (FileHandle, L"");

    ReportWriteLine (FileHandle, L"  -- Firmware --");
    UnicodeSPrint (Line, sizeof (Line), L"  UEFI Vendor     : %s", FwInfo.FirmwareVendor);
    ReportWriteLine (FileHandle, Line);
    UnicodeSPrint (Line, sizeof (Line), L"  UEFI Spec       : %d.%d",
                   (int)FwInfo.UefiSpecMajor, (int)FwInfo.UefiSpecMinor);
    ReportWriteLine (FileHandle, Line);
    UnicodeSPrint (Line, sizeof (Line), L"  FW Revision     : 0x%08X", FwInfo.FirmwareRevision);
    ReportWriteLine (FileHandle, Line);
    UnicodeSPrint (Line, sizeof (Line), L"  BIOS Vendor     : %a", FwInfo.BiosVendor);
    ReportWriteLine (FileHandle, Line);
    UnicodeSPrint (Line, sizeof (Line), L"  BIOS Version    : %a", FwInfo.BiosVersion);
    ReportWriteLine (FileHandle, Line);
    UnicodeSPrint (Line, sizeof (Line), L"  BIOS Date       : %a", FwInfo.BiosReleaseDate);
    ReportWriteLine (FileHandle, Line);
    UnicodeSPrint (Line, sizeof (Line), L"  BIOS Release    : %d.%d",
                   (int)FwInfo.BiosMajorRelease, (int)FwInfo.BiosMinorRelease);
    ReportWriteLine (FileHandle, Line);
    UnicodeSPrint (Line, sizeof (Line), L"  BIOS ROM Size   : %llu KB",
                   FwInfo.BiosRomSize / 1024);
    ReportWriteLine (FileHandle, Line);
    ReportWriteLine (FileHandle, L"");

    ReportWriteLine (FileHandle, L"  -- Processor --");
    UnicodeSPrint (Line, sizeof (Line), L"  Processor       : %a", CpuInf.ProcessorName);
    ReportWriteLine (FileHandle, Line);
    UnicodeSPrint (Line, sizeof (Line), L"  Socket          : %a", CpuInf.SocketDesignation);
    ReportWriteLine (FileHandle, Line);
    UnicodeSPrint (Line, sizeof (Line), L"  Max Speed       : %d MHz", (int)CpuInf.MaxSpeed);
    ReportWriteLine (FileHandle, Line);
    UnicodeSPrint (Line, sizeof (Line), L"  Current Speed   : %d MHz", (int)CpuInf.CurrentSpeed);
    ReportWriteLine (FileHandle, Line);
    UnicodeSPrint (Line, sizeof (Line), L"  Cores / Threads : %d / %d",
                   (int)CpuInf.CoreCount, (int)CpuInf.ThreadCount);
    ReportWriteLine (FileHandle, Line);
    ReportWriteLine (FileHandle, L"");

    ReportWriteLine (FileHandle, L"  -- Memory --");
    UnicodeSPrint (Line, sizeof (Line), L"  Total Memory    : %d MB (%d GB)",
                   (int)MemInfo.TotalMemoryMB, (int)(MemInfo.TotalMemoryMB / 1024));
    ReportWriteLine (FileHandle, Line);
    UnicodeSPrint (Line, sizeof (Line), L"  Populated Slots : %d / %d",
                   (int)MemInfo.PopulatedSlots, (int)MemInfo.TotalSlots);
    ReportWriteLine (FileHandle, Line);
    ReportWriteLine (FileHandle, L"");

    for (Sl = 0; Sl < MemInfo.TotalSlots; Sl++) {
      if (MemInfo.Slots[Sl].SizeMB > 0) {
        UnicodeSPrint (Line, sizeof (Line),
                       L"  Slot %-2d [%a]",
                       (int)MemInfo.Slots[Sl].SlotIndex,
                       MemInfo.Slots[Sl].DeviceLocator);
        ReportWriteLine (FileHandle, Line);
        UnicodeSPrint (Line, sizeof (Line),
                       L"    Size: %d MB  Type: %s  Speed: %d/%d MHz",
                       (int)MemInfo.Slots[Sl].SizeMB,
                       ReportMemTypeName (MemInfo.Slots[Sl].MemoryType),
                       (int)MemInfo.Slots[Sl].ConfiguredSpeed,
                       (int)MemInfo.Slots[Sl].Speed);
        ReportWriteLine (FileHandle, Line);
        UnicodeSPrint (Line, sizeof (Line),
                       L"    Mfr: %a  P/N: %a  S/N: %a",
                       MemInfo.Slots[Sl].Manufacturer,
                       MemInfo.Slots[Sl].PartNumber,
                       MemInfo.Slots[Sl].SerialNumber);
        ReportWriteLine (FileHandle, Line);
      }
    }

    ReportWriteLine (FileHandle, L"");

    //
    // PCI Network Controllers
    //
    {
      NIC_INFO      AllNics[MAX_INTERFACES];
      PCI_NIC_INFO  PciNicArr[MAX_PCI_NICS];
      UINTN         AllNicCnt;
      UINTN         PciNicCnt;
      UINTN         P;
      CHAR16        PciMacStr[20];

      AllNicCnt = MAX_INTERFACES;
      DiscoverNics (AllNics, &AllNicCnt);

      PciNicCnt = MAX_PCI_NICS;
      DiscoverPciNics (PciNicArr, &PciNicCnt, AllNics, AllNicCnt);

      ReportWriteLine (FileHandle, L"  -- PCI Network Controllers --");
      UnicodeSPrint (Line, sizeof (Line), L"  Found: %d PCI NIC(s), %d with SNP driver",
                     (int)PciNicCnt, (int)AllNicCnt);
      ReportWriteLine (FileHandle, Line);
      ReportWriteLine (FileHandle, L"");

      for (P = 0; P < PciNicCnt; P++) {
        UnicodeSPrint (Line, sizeof (Line),
                       L"  NIC %d: %s %s",
                       (int)(P + 1),
                       PciNicArr[P].VendorName,
                       PciNicArr[P].DeviceModel);
        ReportWriteLine (FileHandle, Line);
        UnicodeSPrint (Line, sizeof (Line),
                       L"    PCI BDF: %02X:%02X.%X  VID:DID: %04X:%04X  Driver: %s",
                       (int)PciNicArr[P].Bus, (int)PciNicArr[P].Dev,
                       (int)PciNicArr[P].Func,
                       PciNicArr[P].VendorId, PciNicArr[P].DeviceId,
                       PciNicArr[P].HasDriver ? L"Loaded" : L"NOT LOADED");
        ReportWriteLine (FileHandle, Line);

        if (PciNicArr[P].HasMac) {
          UtilFormatMac (PciNicArr[P].MacAddress, PciMacStr);
          UnicodeSPrint (Line, sizeof (Line),
                         L"    MAC: %s  Link: %s",
                         PciMacStr,
                         PciNicArr[P].MediaPresent ? L"Up" : L"Down");
          ReportWriteLine (FileHandle, Line);
        } else {
          ReportWriteLine (FileHandle, L"    MAC: N/A (no driver)");
        }

        if (PciNicArr[P].MatchedSnp) {
          UnicodeSPrint (Line, sizeof (Line),
                         L"    SNP Match: Yes (NIC index %d)",
                         (int)PciNicArr[P].SnpIndex);
          ReportWriteLine (FileHandle, Line);
        }
      }

      ReportWriteLine (FileHandle, L"");
    }
  }

  //
  // ── Section 2: NIC Details ──
  //
  ReportWriteLine (FileHandle, L"================================================================");
  ReportWriteLine (FileHandle, L"  SECTION 2: NETWORK INTERFACE");
  ReportWriteLine (FileHandle, L"================================================================");
  ReportWriteLine (FileHandle, L"");

  UnicodeSPrint (Line, sizeof (Line), L"  Name            : %s", Ctx->Nic->Name);
  ReportWriteLine (FileHandle, Line);

  UtilFormatMac (Ctx->Nic->CurrentMac.Addr, MacStr);
  UnicodeSPrint (Line, sizeof (Line), L"  MAC Address     : %s", MacStr);
  ReportWriteLine (FileHandle, Line);

  UtilFormatMac (Ctx->Nic->PermanentMac.Addr, MacStr);
  UnicodeSPrint (Line, sizeof (Line), L"  Permanent MAC   : %s", MacStr);
  ReportWriteLine (FileHandle, Line);

  UnicodeSPrint (Line, sizeof (Line), L"  Link Status     : %s",
                 Ctx->Nic->MediaPresent ? L"Connected" : L"Disconnected");
  ReportWriteLine (FileHandle, Line);

  UnicodeSPrint (Line, sizeof (Line), L"  Max Packet Size : %d bytes",
                 (int)Ctx->Nic->MaxPacketSize);
  ReportWriteLine (FileHandle, Line);

  if (Ctx->Nic->HasIpConfig) {
    ReportWriteLine (FileHandle, L"");
    UtilFormatIpv4 (Ctx->Nic->Ipv4Address.Addr, IpStr);
    UnicodeSPrint (Line, sizeof (Line), L"  IPv4 Address    : %s", IpStr);
    ReportWriteLine (FileHandle, Line);
    UtilFormatIpv4 (Ctx->Nic->SubnetMask.Addr, IpStr);
    UnicodeSPrint (Line, sizeof (Line), L"  Subnet Mask     : %s", IpStr);
    ReportWriteLine (FileHandle, Line);
    UtilFormatIpv4 (Ctx->Nic->Gateway.Addr, IpStr);
    UnicodeSPrint (Line, sizeof (Line), L"  Default Gateway : %s", IpStr);
    ReportWriteLine (FileHandle, Line);
  } else {
    ReportWriteLine (FileHandle, L"  IPv4 Config     : Not configured");
  }

  ReportWriteLine (FileHandle, L"");
  UnicodeSPrint (Line, sizeof (Line),
                 L"  Protocol Support: MNP=%s ARP=%s IP4=%s TCP4=%s UDP4=%s",
                 Ctx->Nic->HasMnp  ? L"Yes" : L"No",
                 Ctx->Nic->HasArp  ? L"Yes" : L"No",
                 Ctx->Nic->HasIp4  ? L"Yes" : L"No",
                 Ctx->Nic->HasTcp4 ? L"Yes" : L"No",
                 Ctx->Nic->HasUdp4 ? L"Yes" : L"No");
  ReportWriteLine (FileHandle, Line);
  UnicodeSPrint (Line, sizeof (Line),
                 L"                    DHCP4=%s DNS4=%s HTTP=%s TLS=%s",
                 Ctx->Nic->HasDhcp4 ? L"Yes" : L"No",
                 Ctx->Nic->HasDns4  ? L"Yes" : L"No",
                 Ctx->Nic->HasHttp  ? L"Yes" : L"No",
                 Ctx->Nic->HasTls   ? L"Yes" : L"No");
  ReportWriteLine (FileHandle, Line);
  ReportWriteLine (FileHandle, L"");

  UnicodeSPrint (Line, sizeof (Line), L"  Device Path: %s", Ctx->Nic->DevicePath);
  ReportWriteLine (FileHandle, Line);
  ReportWriteLine (FileHandle, L"");

  //
  // ── Section 3: Test Configuration ──
  //
  ReportWriteLine (FileHandle, L"================================================================");
  ReportWriteLine (FileHandle, L"  SECTION 3: TEST CONFIGURATION");
  ReportWriteLine (FileHandle, L"================================================================");
  ReportWriteLine (FileHandle, L"");

  UtilFormatIpv4 (Ctx->Config->TargetIp.Addr, IpStr);
  UnicodeSPrint (Line, sizeof (Line), L"  Target IP       : %s", IpStr);
  ReportWriteLine (FileHandle, Line);
  UnicodeSPrint (Line, sizeof (Line), L"  Target Port     : %d", (int)Ctx->Config->TargetPort);
  ReportWriteLine (FileHandle, Line);
  UnicodeSPrint (Line, sizeof (Line), L"  Timeout         : %d ms", (int)Ctx->Config->TimeoutMs);
  ReportWriteLine (FileHandle, Line);
  UnicodeSPrint (Line, sizeof (Line), L"  Iterations      : %d", (int)Ctx->Config->Iterations);
  ReportWriteLine (FileHandle, Line);
  UnicodeSPrint (Line, sizeof (Line), L"  Use Companion   : %s",
                 Ctx->Config->UseCompanion ? L"Yes" : L"No");
  ReportWriteLine (FileHandle, Line);

  if (Ctx->Config->UseCompanion) {
    UtilFormatIpv4 (Ctx->Config->CompanionIp.Addr, IpStr);
    UnicodeSPrint (Line, sizeof (Line), L"  Companion IP    : %s:%d",
                   IpStr, (int)Ctx->Config->CompanionPort);
    ReportWriteLine (FileHandle, Line);
  }

  ReportWriteLine (FileHandle, L"");

  //
  // ── Section 4: Results Summary ──
  //
  ReportWriteLine (FileHandle, L"================================================================");
  ReportWriteLine (FileHandle, L"  SECTION 4: RESULTS SUMMARY");
  ReportWriteLine (FileHandle, L"================================================================");
  ReportWriteLine (FileHandle, L"");

  ReportCountResults (Ctx->Results, Ctx->ResultCount,
                      &PassCnt, &FailCnt, &WarnCnt, &SkipCnt, &ErrCnt);

  TotalDurationMs = 0;
  TotalPktSent    = 0;
  TotalPktRecv    = 0;
  for (I = 0; I < Ctx->ResultCount; I++) {
    TotalDurationMs += Ctx->Results[I].DurationMs;
    TotalPktSent    += Ctx->Results[I].PacketsSent;
    TotalPktRecv    += Ctx->Results[I].PacketsReceived;
  }

  UnicodeSPrint (Line, sizeof (Line), L"  Total Tests     : %d", (int)Ctx->ResultCount);
  ReportWriteLine (FileHandle, Line);
  UnicodeSPrint (Line, sizeof (Line), L"  Passed          : %d", (int)PassCnt);
  ReportWriteLine (FileHandle, Line);
  UnicodeSPrint (Line, sizeof (Line), L"  Failed          : %d", (int)FailCnt);
  ReportWriteLine (FileHandle, Line);
  UnicodeSPrint (Line, sizeof (Line), L"  Warnings        : %d", (int)WarnCnt);
  ReportWriteLine (FileHandle, Line);
  UnicodeSPrint (Line, sizeof (Line), L"  Skipped         : %d", (int)SkipCnt);
  ReportWriteLine (FileHandle, Line);
  UnicodeSPrint (Line, sizeof (Line), L"  Errors          : %d", (int)ErrCnt);
  ReportWriteLine (FileHandle, Line);
  UnicodeSPrint (Line, sizeof (Line), L"  Total Duration  : %llu ms", TotalDurationMs);
  ReportWriteLine (FileHandle, Line);
  UnicodeSPrint (Line, sizeof (Line), L"  Total Pkts Sent : %llu", TotalPktSent);
  ReportWriteLine (FileHandle, Line);
  UnicodeSPrint (Line, sizeof (Line), L"  Total Pkts Recv : %llu", TotalPktRecv);
  ReportWriteLine (FileHandle, Line);

  //
  // Pass rate
  //
  if (Ctx->ResultCount > 0) {
    UINTN  PassRate = (PassCnt * 100) / Ctx->ResultCount;
    UnicodeSPrint (Line, sizeof (Line), L"  Pass Rate       : %d%%", (int)PassRate);
    ReportWriteLine (FileHandle, Line);
  }

  ReportWriteLine (FileHandle, L"");

  //
  // ── Section 5: Detailed Per-Test Results ──
  //
  ReportWriteLine (FileHandle, L"================================================================");
  ReportWriteLine (FileHandle, L"  SECTION 5: DETAILED TEST RESULTS");
  ReportWriteLine (FileHandle, L"================================================================");

  for (I = 0; I < Ctx->ResultCount; I++) {
    ReportWriteLine (FileHandle, L"");
    ReportWriteLine (FileHandle, L"  ------------------------------------------------");

    UnicodeSPrint (Line, sizeof (Line), L"  Test #%d: %s",
                   (int)(I + 1), Ctx->TestDefs[I]->Name);
    ReportWriteLine (FileHandle, Line);

    UnicodeSPrint (Line, sizeof (Line), L"  Description : %s",
                   Ctx->TestDefs[I]->Description);
    ReportWriteLine (FileHandle, Line);

    UnicodeSPrint (Line, sizeof (Line), L"  Layer       : %s  (%s)",
                   RegGetLayerName (Ctx->TestDefs[I]->Layer),
                   RegGetLayerShort (Ctx->TestDefs[I]->Layer));
    ReportWriteLine (FileHandle, Line);

    UnicodeSPrint (Line, sizeof (Line), L"  Type        : %s",
                   RegGetTypeName (Ctx->TestDefs[I]->Type));
    ReportWriteLine (FileHandle, Line);

    UnicodeSPrint (Line, sizeof (Line), L"  Result      : %s",
                   ReportResultStr (Ctx->Results[I].StatusCode));
    ReportWriteLine (FileHandle, Line);

    UnicodeSPrint (Line, sizeof (Line), L"  Duration    : %llu ms",
                   Ctx->Results[I].DurationMs);
    ReportWriteLine (FileHandle, Line);

    //
    // Summary and detail
    //
    if (Ctx->Results[I].Summary[0] != L'\0') {
      UnicodeSPrint (Line, sizeof (Line), L"  Summary     : %s",
                     Ctx->Results[I].Summary);
      ReportWriteLine (FileHandle, Line);
    }

    if (Ctx->Results[I].Detail[0] != L'\0') {
      UnicodeSPrint (Line, sizeof (Line), L"  Detail      : %s",
                     Ctx->Results[I].Detail);
      ReportWriteLine (FileHandle, Line);
    }

    //
    // Failure info
    //
    if (Ctx->Results[I].StatusCode == TEST_RESULT_FAIL ||
        Ctx->Results[I].StatusCode == TEST_RESULT_ERROR) {
      if (Ctx->Results[I].FailReason[0] != L'\0') {
        UnicodeSPrint (Line, sizeof (Line), L"  Fail Reason : %s",
                       Ctx->Results[I].FailReason);
        ReportWriteLine (FileHandle, Line);
      }
      if (Ctx->Results[I].Suggestion[0] != L'\0') {
        UnicodeSPrint (Line, sizeof (Line), L"  Suggestion  : %s",
                       Ctx->Results[I].Suggestion);
        ReportWriteLine (FileHandle, Line);
      }
    }

    //
    // Packet statistics (only if non-zero)
    //
    if (Ctx->Results[I].PacketsSent > 0 || Ctx->Results[I].PacketsReceived > 0) {
      ReportWriteLine (FileHandle, L"");
      UnicodeSPrint (Line, sizeof (Line),
                     L"  Packets     : Sent=%llu  Recv=%llu",
                     Ctx->Results[I].PacketsSent,
                     Ctx->Results[I].PacketsReceived);
      ReportWriteLine (FileHandle, Line);
      UnicodeSPrint (Line, sizeof (Line),
                     L"  Bytes       : Sent=%llu  Recv=%llu",
                     Ctx->Results[I].BytesSent,
                     Ctx->Results[I].BytesReceived);
      ReportWriteLine (FileHandle, Line);
    }

    //
    // RTT statistics (only if measured)
    //
    if (Ctx->Results[I].RttAvgUs > 0) {
      UnicodeSPrint (Line, sizeof (Line),
                     L"  RTT (us)    : Min=%d  Avg=%d  Max=%d  Jitter=%d",
                     (int)Ctx->Results[I].RttMinUs,
                     (int)Ctx->Results[I].RttAvgUs,
                     (int)Ctx->Results[I].RttMaxUs,
                     (int)Ctx->Results[I].RttJitterUs);
      ReportWriteLine (FileHandle, Line);
    }
  }

  ReportWriteLine (FileHandle, L"");

  //
  // ── Section 6: Summary Diagnosis ──
  //
  ReportWriteLine (FileHandle, L"================================================================");
  ReportWriteLine (FileHandle, L"  SECTION 6: SUMMARY DIAGNOSIS");
  ReportWriteLine (FileHandle, L"================================================================");
  ReportWriteLine (FileHandle, L"");

  //
  // Generate diagnosis from existing results (no re-running tests)
  //
  {
    UINTN  K;

    if (FailCnt == 0 && ErrCnt == 0) {
      if (WarnCnt > 0) {
        ReportWriteLine (FileHandle, L"  Diagnosis: MOSTLY OK - All tests passed with some warnings.");
        UnicodeSPrint (Line, sizeof (Line),
                       L"  Detail:    %d warnings detected. Review WARN results above.", (int)WarnCnt);
        ReportWriteLine (FileHandle, Line);
      } else {
        ReportWriteLine (FileHandle, L"  Diagnosis: ALL PASS - Network stack is fully functional.");
      }
    } else {
      UnicodeSPrint (Line, sizeof (Line),
                     L"  Diagnosis: %d FAIL, %d ERROR detected in %d tests.",
                     (int)FailCnt, (int)ErrCnt, (int)Ctx->ResultCount);
      ReportWriteLine (FileHandle, Line);

      //
      // List failed tests
      //
      ReportWriteLine (FileHandle, L"");
      ReportWriteLine (FileHandle, L"  Failed tests:");
      for (K = 0; K < Ctx->ResultCount; K++) {
        if (Ctx->Results[K].StatusCode == TEST_RESULT_FAIL ||
            Ctx->Results[K].StatusCode == TEST_RESULT_ERROR) {
          UnicodeSPrint (Line, sizeof (Line), L"    - %s: %s",
                         Ctx->TestDefs[K]->Name, Ctx->Results[K].Summary);
          ReportWriteLine (FileHandle, Line);
        }
      }
    }
  }

  ReportWriteLine (FileHandle, L"");

  //
  // ── Footer ──
  //
  ReportWriteLine (FileHandle, L"################################################################");
  ReportWriteLine (FileHandle, L"##  End of Report                                             ##");
  ReportWriteLine (FileHandle, L"##  Generated by DDTSoft - EFI Network Test & OSI Analyzer    ##");
  ReportWriteLine (FileHandle, L"################################################################");

  FileHandle->Close (FileHandle);
  return EFI_SUCCESS;
}

//
// ============================================================
// Binary Report Export
// Raw binary dump of test result structures.
// ============================================================
//
STATIC
EFI_STATUS
ReportExportBinary (
  IN REPORT_CONTEXT  *Ctx,
  IN CONST CHAR16    *Filename
  )
{
  EFI_FILE_PROTOCOL  *FileHandle;
  EFI_STATUS         Status;
  UINT32             Magic;
  UINT32             Version;
  UINT32             Count;

  Status = ReportOpenFile (Filename, &FileHandle);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Binary header: magic (4 bytes) + version (4 bytes) + count (4 bytes)
  //
  Magic   = 0x44445453;  // "DDTS" in little-endian
  Version = 0x00010000;  // 1.0
  Count   = (UINT32)Ctx->ResultCount;

  ReportWriteRaw (FileHandle, &Magic,   sizeof (UINT32));
  ReportWriteRaw (FileHandle, &Version, sizeof (UINT32));
  ReportWriteRaw (FileHandle, &Count,   sizeof (UINT32));

  //
  // Timestamp
  //
  ReportWriteRaw (FileHandle, &Ctx->Time, sizeof (EFI_TIME));

  //
  // NIC MAC address (6 bytes) + IP (4 bytes)
  //
  ReportWriteRaw (FileHandle, Ctx->Nic->CurrentMac.Addr, 6);
  ReportWriteRaw (FileHandle, Ctx->Nic->Ipv4Address.Addr, 4);

  //
  // All TEST_RESULT_DATA structures
  //
  if (Ctx->ResultCount > 0) {
    ReportWriteRaw (
      FileHandle,
      Ctx->Results,
      Ctx->ResultCount * sizeof (TEST_RESULT_DATA)
      );
  }

  FileHandle->Close (FileHandle);
  return EFI_SUCCESS;
}

//
// ============================================================
// Static: run all tests and collect results for reporting
// ============================================================
//
STATIC
EFI_STATUS
ReportRunTests (
  IN  NIC_INFO          *Nic,
  IN  TEST_CONFIG       *Config,
  IN  OSI_LAYER         Layer,
  OUT TEST_DEFINITION   **OutTestDefs,
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
  EFI_INPUT_KEY    Key;
  EFI_STATUS       KeyStatus;
  TEST_CONFIG      ReportConfig;

  TestCount = RegGetTestsByLayer (Layer, Tests, MAX_TESTS);
  *OutCount = 0;

  //
  // Use reduced timeouts for report mode to avoid long hangs.
  // Tests that would take very long are skipped automatically.
  //
  CopyMem (&ReportConfig, Config, sizeof (TEST_CONFIG));
  if (ReportConfig.TimeoutMs > 1500) {
    ReportConfig.TimeoutMs = 1500;
  }
  ReportConfig.Iterations = 1;

  BoxW = UiGetScreenWidth () - 2;
  if (BoxW < 76) {
    BoxW = 76;
  }
  BarW = BoxW - 8;

  //
  // Draw the static frame once before the loop
  //
  UiClearScreen ();
  UiDrawHeader ();
  UiDrawBox (1, 3, BoxW, 11, L" Generating Report ");

  UiSetColor (COLOR_INFO, COLOR_BG);
  UiPrintAt (3, 5, L"Running tests for report export...");
  UiPrintAt (3, 6, L"NIC: %s", Nic->Name);

  UiSetColor (EFI_LIGHTGRAY, COLOR_BG);
  UiDrawStatusBar (L"Press [ESC] to cancel report generation");

  for (I = 0; I < TestCount; I++) {
    Percent = (TestCount > 0) ? ((I * 100) / TestCount) : 0;

    //
    // Check for ESC key (non-blocking) to allow user to cancel
    //
    KeyStatus = gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
    if (!EFI_ERROR (KeyStatus) && Key.ScanCode == SCAN_ESC) {
      //
      // User cancelled — mark remaining tests as SKIP
      //
      break;
    }

    //
    // Update only the changing lines (test info + progress)
    //
    UiClearLines (8, 11);

    UiSetColor (COLOR_DEFAULT, COLOR_BG);
    UiPrintAt (3, 8, L"Test %d/%d: %-50s", (int)(I + 1), (int)TestCount, Tests[I]->Name);
    UiPrintAt (3, 9, L"Layer: %s  Est: %d ms       ",
               RegGetLayerShort (Tests[I]->Layer), (int)Tests[I]->EstimatedTimeMs);

    UiDrawProgress (4, 11, BarW, Percent, L"Progress");

    //
    // SAFETY: In report mode, only run tests that are purely informational
    // (read SNP Mode data, check NIC_INFO fields, read IP config).
    // Skip ALL tests that perform hardware I/O (Transmit, Receive,
    // Start/Stop/Initialize, ReceiveFilters) — these can freeze real PCs
    // by deadlocking NIC drivers when upper protocol stacks are active.
    //
    // Safe criteria: EstimatedTime <= 1000ms AND !RequiresTarget AND !Stress
    // This allows: NicStatus, LinkDetect, LinkNegotiation, MacAddressValid,
    //              IpConfigCheck — all read-only operations.
    //
    if (Tests[I]->RequiresTarget ||
        Tests[I]->EstimatedTimeMs > 1000 ||
        Tests[I]->Type == TestTypeStress ||
        Tests[I]->Type == TestTypePerformance) {
      ZeroMem (&OutResults[I], sizeof (TEST_RESULT_DATA));
      OutResults[I].StatusCode = TEST_RESULT_SKIP;
      UnicodeSPrint (
        OutResults[I].Summary,
        sizeof (OutResults[I].Summary),
        L"Skipped in report mode — run from [T] Run Tests for full results"
        );
      OutTestDefs[I] = Tests[I];
      (*OutCount)++;
      continue;
    }

    //
    // Run safe (read-only) test with reduced timeout config
    //
    RunSingleTest (Tests[I], Nic, &ReportConfig, &OutResults[I]);
    OutTestDefs[I] = Tests[I];
    (*OutCount)++;
  }

  return EFI_SUCCESS;
}

//
// ============================================================
// Static: show export format selection and export
// ============================================================
//
STATIC
EFI_STATUS
ReportDoExport (
  IN REPORT_CONTEXT  *Ctx
  )
{
  EFI_INPUT_KEY   Key;
  REPORT_FORMAT   Format;
  CHAR16          Filename[REPORT_MAX_FILENAME];
  EFI_STATUS      Status;
  UINTN           BoxW;

  BoxW = UiGetScreenWidth () - 2;
  if (BoxW < 76) {
    BoxW = 76;
  }

  //
  // Format selection menu
  //
  UiClearScreen ();
  UiDrawHeader ();
  UiDrawBox (1, 3, BoxW, 12, L" Export Format ");

  UiSetColor (COLOR_INFO, COLOR_BG);
  UiPrintAt (5, 5, L"Tests completed: %d results ready to export", (int)Ctx->ResultCount);

  UiSetColor (COLOR_DEFAULT, COLOR_BG);
  UiPrintAt (5, 7,  L"[1] TXT        - Plain text summary report");
  UiPrintAt (5, 8,  L"[2] CSV        - Spreadsheet-compatible data");
  UiPrintAt (5, 9,  L"[3] Detailed   - Verbose report with full diagnostics");
  UiPrintAt (5, 10, L"[4] Binary     - Raw binary data dump");

  UiPrintAt (5, 12, L"[Q] Cancel");

  UiDrawStatusBar (L"Select format [1-4] or [Q] to cancel");

  Key = UiWaitKey ();

  switch (Key.UnicodeChar) {
    case L'1':  Format = ReportFormatTxt;      break;
    case L'2':  Format = ReportFormatCsv;      break;
    case L'3':  Format = ReportFormatDetailed; break;
    case L'4':  Format = ReportFormatBinary;   break;
    default:    return EFI_ABORTED;
  }

  //
  // Build filename
  //
  ReportBuildFilename (Format, &Ctx->Time, Filename, REPORT_MAX_FILENAME);

  //
  // Show "exporting..." message
  //
  UiClearScreen ();
  UiDrawHeader ();
  UiDrawBox (1, 3, BoxW, 8, L" Exporting Report ");

  UiSetColor (COLOR_INFO, COLOR_BG);
  UiPrintAt (3, 5, L"  Writing: %s", Filename);
  UiPrintAt (3, 6, L"  Please wait...");

  //
  // Export
  //
  switch (Format) {
    case ReportFormatTxt:
      Status = ReportExportTxt (Ctx, Filename);
      break;
    case ReportFormatCsv:
      Status = ReportExportCsv (Ctx, Filename);
      break;
    case ReportFormatDetailed:
      Status = ReportExportDetailed (Ctx, Filename);
      break;
    case ReportFormatBinary:
      Status = ReportExportBinary (Ctx, Filename);
      break;
    default:
      Status = EFI_UNSUPPORTED;
      break;
  }

  //
  // Show result
  //
  if (!EFI_ERROR (Status)) {
    UiSetColor (COLOR_SUCCESS, COLOR_BG);
    UiPrintAt (3, 8, L"  Report saved successfully: %s", Filename);
  } else {
    UiSetColor (COLOR_ERROR, COLOR_BG);
    UiPrintAt (3, 8, L"  Failed to save report: %r", Status);
  }

  UiDrawStatusBar (L"Press any key to continue...");
  UiWaitKey ();

  return Status;
}

//
// ============================================================
// Public: ShowReports
// Main entry point for the Reports menu.
// Discovers NIC, runs tests, and exports results in chosen format.
// ============================================================
//
EFI_STATUS
ShowReports (
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
  TEST_DEFINITION   **TestDefs;
  UINTN             ResultCount;
  REPORT_CONTEXT    Ctx;
  CHAR16            IpStr[20];
  UINTN             BoxW;

  //
  // Initialize test registry
  //
  RegInitAllTests ();

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
    UiPrintAt (3, 7, L"  Cannot generate reports without a NIC.");
    UiDrawStatusBar (L"Press any key to return");
    UiWaitKey ();
    FreePool (Nics);
    return EFI_NOT_FOUND;
  }

  //
  // Allocate results
  //
  Results  = AllocateZeroPool (MAX_TESTS * sizeof (TEST_RESULT_DATA));
  TestDefs = AllocateZeroPool (MAX_TESTS * sizeof (TEST_DEFINITION *));
  if (Results == NULL || TestDefs == NULL) {
    FreePool (Nics);
    if (Results != NULL) {
      FreePool (Results);
    }
    if (TestDefs != NULL) {
      FreePool (TestDefs);
    }
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
  Running       = TRUE;

  BoxW = UiGetScreenWidth () - 2;
  if (BoxW < 76) {
    BoxW = 76;
  }

  //
  // Initial full draw
  //
  UiClearScreen ();
  UiDrawHeader ();

  while (Running) {
    //
    // Report menu — clear only content area, keep header stable
    //
    UiClearLines (3, UiGetScreenHeight () - 2);

    UiSetColor (COLOR_HEADER, COLOR_BG);
    UiDrawBox (1, 3, BoxW, 5, L"Reports");

    UiSetColor (COLOR_INFO, COLOR_BG);
    UiPrintAt (3, 4, L"  NIC       : [%d] %s",
               (int)(SelectedNic + 1), Nics[SelectedNic].Name);

    if (Nics[SelectedNic].HasIpConfig) {
      UtilFormatIpv4 (Nics[SelectedNic].Ipv4Address.Addr, IpStr);
    } else {
      UtilSafeStrCpy (IpStr, L"(not configured)", 20);
    }
    UiPrintAt (3, 5, L"  IP        : %s", IpStr);

    UtilFormatIpv4 (Config.TargetIp.Addr, IpStr);
    UiPrintAt (3, 6, L"  Target IP : %s", IpStr);
    UiPrintAt (3, 7, L"  Tests     : %d registered", (int)RegGetTestCount ());

    //
    // Report type selection
    //
    UiSetColor (COLOR_HEADER, COLOR_BG);
    UiDrawBox (1, 9, BoxW, 13, L"Select Report Type");

    UiSetColor (COLOR_DEFAULT, COLOR_BG);
    UiPrintAt (5, 10, L"[1] Quick Scan Report   - Run QuickScan + export results");
    UiPrintAt (5, 11, L"[2] Layer 1 Report      - Physical layer tests");
    UiPrintAt (5, 12, L"[3] Layer 2 Report      - Data Link layer tests");
    UiPrintAt (5, 13, L"[4] Layer 3 Report      - Network layer tests");
    UiPrintAt (5, 14, L"[5] Layer 4 Report      - Transport layer tests");
    UiPrintAt (5, 15, L"[6] Layer 7 Report      - Application layer tests");
    UiPrintAt (5, 16, L"[7] Full Report         - All layers (%d tests)",
               (int)RegGetTestCount ());

    UiSetColor (EFI_LIGHTGRAY, COLOR_BG);
    UiPrintAt (5, 18, L"[N] Change NIC  [T] Change Target IP");
    UiPrintAt (5, 19, L"[ESC] Back to main menu");

    UiDrawStatusBar (L"Select report [1-7] or [N]IC [T]arget [ESC]");

    Key = UiWaitKey ();

    switch (Key.UnicodeChar) {
      case L'1':
        SelectedLayer = OsiLayerAll;
        break;
      case L'2':
        SelectedLayer = OsiLayerPhysical;
        break;
      case L'3':
        SelectedLayer = OsiLayerDataLink;
        break;
      case L'4':
        SelectedLayer = OsiLayerNetwork;
        break;
      case L'5':
        SelectedLayer = OsiLayerTransport;
        break;
      case L'6':
        SelectedLayer = OsiLayerApplication;
        break;
      case L'7':
        SelectedLayer = OsiLayerAll;
        break;
      case L'n': case L'N':
        SelectedNic = (SelectedNic + 1) % NicCount;
        continue;
      case L't': case L'T':
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
    // For option [1] (Quick Scan), run QuickScan-specific subset
    // For options [2-7], run layer-specific tests
    //
    if (Key.UnicodeChar == L'1') {
      //
      // QuickScan: run all tests (QuickScan picks its subset internally)
      // We still run the full layer set so the report has data
      //
      SelectedLayer = OsiLayerAll;
    }

    //
    // Run the tests
    //
    ResultCount = 0;
    ReportRunTests (
      &Nics[SelectedNic],
      &Config,
      SelectedLayer,
      TestDefs,
      Results,
      &ResultCount
      );

    if (ResultCount == 0) {
      UiClearScreen ();
      UiDrawHeader ();
      UiSetColor (COLOR_WARNING, COLOR_BG);
      UiPrintAt (3, 5, L"  No test results to export.");
      UiDrawStatusBar (L"Press any key to return");
      UiWaitKey ();
      continue;
    }

    //
    // Build context and export
    //
    ZeroMem (&Ctx, sizeof (REPORT_CONTEXT));
    Ctx.Nic         = &Nics[SelectedNic];
    Ctx.Config       = &Config;
    Ctx.TestDefs     = TestDefs;
    Ctx.Results      = Results;
    Ctx.ResultCount  = ResultCount;
    Ctx.Layer        = SelectedLayer;

    ReportGetTimestamp (Ctx.Timestamp, 32, &Ctx.Time);

    ReportDoExport (&Ctx);
  }

  FreePool (TestDefs);
  FreePool (Results);
  FreePool (Nics);
  return EFI_SUCCESS;
}

//
// ============================================================
// Public: ExportTestResults
// Allows the Run Tests menu to export its results directly
// without re-running tests.
// ============================================================
//
EFI_STATUS
ExportTestResults (
  IN NIC_INFO          *Nic,
  IN TEST_CONFIG       *Config,
  IN TEST_DEFINITION   **TestDefs,
  IN TEST_RESULT_DATA  *Results,
  IN UINTN             ResultCount,
  IN OSI_LAYER         Layer
  )
{
  REPORT_CONTEXT  Ctx;

  if (Nic == NULL || Results == NULL || ResultCount == 0) {
    return EFI_INVALID_PARAMETER;
  }

  ZeroMem (&Ctx, sizeof (REPORT_CONTEXT));
  Ctx.Nic         = Nic;
  Ctx.Config       = Config;
  Ctx.TestDefs     = TestDefs;
  Ctx.Results      = Results;
  Ctx.ResultCount  = ResultCount;
  Ctx.Layer        = Layer;

  ReportGetTimestamp (Ctx.Timestamp, 32, &Ctx.Time);

  return ReportDoExport (&Ctx);
}
