/** @file
  Quick Scan — automated rapid diagnostic across all OSI layers.
  Runs representative tests from each layer, collects per-layer
  pass/fail status, and applies a diagnostic decision tree to
  produce a human-readable network health summary.
**/

#include <DDTSoftNetTest.h>
#include <OsiLayers.h>
#include <TestCases.h>
#include <UiRenderer.h>

//
// ============================================================
// Quick Scan layer status
// ============================================================
//

typedef enum {
  QuickLayerNotTested = 0,
  QuickLayerPass,
  QuickLayerWarn,
  QuickLayerFail
} QUICK_LAYER_STATUS;

//
// Per-layer summary collected during quick scan
//
typedef struct {
  OSI_LAYER           Layer;
  QUICK_LAYER_STATUS  Status;
  UINTN               TestsRun;
  UINTN               TestsPassed;
  UINTN               TestsFailed;
  UINTN               TestsWarned;
  UINTN               TestsSkipped;
  CHAR16              BestSummary[128];     // Summary from first PASS test
  CHAR16              WorstSummary[128];    // Summary from first FAIL test
  CHAR16              WorstSuggestion[128]; // Suggestion from first FAIL test
} QUICK_LAYER_RESULT;

//
// Number of layers we scan (L1, L2, L3, L4, L7)
//
#define QUICK_SCAN_LAYERS  5

//
// Overall scan result
//
typedef struct {
  QUICK_LAYER_RESULT  Layers[QUICK_SCAN_LAYERS];
  UINTN               TotalTests;
  UINTN               TotalPassed;
  UINTN               TotalFailed;
  UINTN               TotalWarned;
  UINTN               TotalSkipped;
  CHAR16              Diagnosis[256];
  CHAR16              DiagnosisDetail[512];
} QUICK_SCAN_RESULT;

//
// ============================================================
// Static: pick representative tests for quick scan
// ============================================================
//
// For each layer, we pick a small subset of fast, essential tests.
// The goal is a ~30 second total scan.
//

//
// L1 quick tests: NIC Status, Link Detect
//
STATIC CONST CHAR16  *mQuickL1Tests[] = {
  L"NIC Status",
  L"Link Detect"
};
#define QUICK_L1_COUNT  2

//
// L2 quick tests: MAC Address Valid, ARP Request/Reply
//
STATIC CONST CHAR16  *mQuickL2Tests[] = {
  L"MAC Address Valid",
  L"ARP Request/Reply"
};
#define QUICK_L2_COUNT  2

//
// L3 quick tests: IP Config Check, ICMP Echo (Ping)
//
STATIC CONST CHAR16  *mQuickL3Tests[] = {
  L"IP Config Check",
  L"ICMP Echo (Ping)"
};
#define QUICK_L3_COUNT  2

//
// L4 quick tests: TCP Connect, UDP Send/Receive
//
STATIC CONST CHAR16  *mQuickL4Tests[] = {
  L"TCP Connect",
  L"UDP Send/Receive"
};
#define QUICK_L4_COUNT  2

//
// L7 quick tests: DNS Resolve, DHCP Lease Verify
//
STATIC CONST CHAR16  *mQuickL7Tests[] = {
  L"DNS Resolve",
  L"DHCP Lease Verify"
};
#define QUICK_L7_COUNT  2

//
// ============================================================
// Static: find a test by name in the registry
// ============================================================
//
STATIC
TEST_DEFINITION *
QuickFindTest (
  IN CONST CHAR16  *Name
  )
{
  UINTN            I;
  UINTN            Count;
  TEST_DEFINITION  *Test;

  Count = RegGetTestCount ();
  for (I = 0; I < Count; I++) {
    Test = RegGetTest (I);
    if (Test != NULL && StrCmp (Test->Name, Name) == 0) {
      return Test;
    }
  }

  return NULL;
}

//
// ============================================================
// Static: determine overall layer status from individual results
// ============================================================
//
STATIC
QUICK_LAYER_STATUS
QuickDetermineLayerStatus (
  IN QUICK_LAYER_RESULT  *Layer
  )
{
  if (Layer->TestsRun == 0) {
    return QuickLayerNotTested;
  }

  if (Layer->TestsFailed > 0) {
    return QuickLayerFail;
  }

  if (Layer->TestsWarned > 0) {
    return QuickLayerWarn;
  }

  if (Layer->TestsPassed > 0) {
    return QuickLayerPass;
  }

  //
  // All skipped
  //
  return QuickLayerNotTested;
}

//
// ============================================================
// Static: run a set of named tests for one layer
// ============================================================
//
STATIC
VOID
QuickRunLayerTests (
  IN     CONST CHAR16       **TestNames,
  IN     UINTN              TestCount,
  IN     NIC_INFO           *Nic,
  IN     TEST_CONFIG        *Config,
  IN OUT QUICK_LAYER_RESULT *LayerResult,
  IN     UINTN              LayerIndex,
  IN     UINTN              TotalQuickTests,
  IN OUT UINTN              *RunningTotal
  )
{
  UINTN            I;
  TEST_DEFINITION  *Test;
  TEST_RESULT_DATA TestResult;
  UINTN            Percent;

  for (I = 0; I < TestCount; I++) {
    Test = QuickFindTest (TestNames[I]);
    if (Test == NULL) {
      continue;
    }

    //
    // Show progress
    //
    (*RunningTotal)++;
    Percent = (*RunningTotal * 100) / TotalQuickTests;

    UiPrintAt (4, 6 + LayerIndex,
               L"  %s %-30s  [%s]  ",
               RegGetLayerShort (LayerResult->Layer),
               TestNames[I],
               L"Running...");
    UiDrawProgress (4, 18, 60, Percent, L"Quick Scan Progress");

    //
    // Run the test
    //
    ZeroMem (&TestResult, sizeof (TestResult));
    RunSingleTest (Test, Nic, Config, &TestResult);

    LayerResult->TestsRun++;

    switch (TestResult.StatusCode) {
      case TEST_RESULT_PASS:
        LayerResult->TestsPassed++;
        if (LayerResult->BestSummary[0] == L'\0') {
          UtilSafeStrCpy (LayerResult->BestSummary, TestResult.Summary, 128);
        }
        break;

      case TEST_RESULT_FAIL:
      case TEST_RESULT_ERROR:
        LayerResult->TestsFailed++;
        if (LayerResult->WorstSummary[0] == L'\0') {
          UtilSafeStrCpy (LayerResult->WorstSummary, TestResult.Summary, 128);
          UtilSafeStrCpy (LayerResult->WorstSuggestion, TestResult.Suggestion, 128);
        }
        break;

      case TEST_RESULT_WARN:
        LayerResult->TestsWarned++;
        if (LayerResult->WorstSummary[0] == L'\0') {
          UtilSafeStrCpy (LayerResult->WorstSummary, TestResult.Summary, 128);
          UtilSafeStrCpy (LayerResult->WorstSuggestion, TestResult.Suggestion, 128);
        }
        break;

      case TEST_RESULT_SKIP:
        LayerResult->TestsSkipped++;
        break;

      default:
        LayerResult->TestsSkipped++;
        break;
    }

    //
    // Update line with result
    //
    CONST CHAR16  *StatusIcon;
    UINTN         StatusColor;

    switch (TestResult.StatusCode) {
      case TEST_RESULT_PASS:
        StatusIcon  = L"\x2713 PASS";
        StatusColor = COLOR_SUCCESS;
        break;
      case TEST_RESULT_FAIL:
      case TEST_RESULT_ERROR:
        StatusIcon  = L"\x2717 FAIL";
        StatusColor = COLOR_ERROR;
        break;
      case TEST_RESULT_WARN:
        StatusIcon  = L"\x26A0 WARN";
        StatusColor = COLOR_WARNING;
        break;
      default:
        StatusIcon  = L"\x25CB SKIP";
        StatusColor = COLOR_INFO;
        break;
    }

    UiPrintAt (4, 6 + LayerIndex,
               L"  %s %-30s  ",
               RegGetLayerShort (LayerResult->Layer),
               TestNames[I]);
    UiSetColor (StatusColor, COLOR_BG);
    Print (L"[%s]", StatusIcon);
    UiResetColor ();
    Print (L"  ");
  }
}

//
// ============================================================
// Static: apply diagnostic decision tree
// ============================================================
//
STATIC
VOID
QuickApplyDiagnosis (
  IN OUT QUICK_SCAN_RESULT  *ScanResult
  )
{
  QUICK_LAYER_STATUS  L1 = ScanResult->Layers[0].Status;
  QUICK_LAYER_STATUS  L2 = ScanResult->Layers[1].Status;
  QUICK_LAYER_STATUS  L3 = ScanResult->Layers[2].Status;
  QUICK_LAYER_STATUS  L4 = ScanResult->Layers[3].Status;
  QUICK_LAYER_STATUS  L7 = ScanResult->Layers[4].Status;

  //
  // Decision tree per PROJECT_SPEC.md Section 10
  //
  if (L1 == QuickLayerFail) {
    UnicodeSPrint (ScanResult->Diagnosis, sizeof (ScanResult->Diagnosis),
                   L"Fiziksel baglanti yok. Kablo ve NIC kontrol edin.");
    UnicodeSPrint (ScanResult->DiagnosisDetail, sizeof (ScanResult->DiagnosisDetail),
                   L"Layer 1 (Physical) testleri basarisiz. NIC durumu ve "
                   L"kablo baglantisini kontrol edin. NIC surucusunun yuklendigi dogrulayin.");
  } else if (L1 != QuickLayerFail && L2 == QuickLayerFail) {
    UnicodeSPrint (ScanResult->Diagnosis, sizeof (ScanResult->Diagnosis),
                   L"Link var ama frame iletisimi yok. Switch/VLAN kontrol.");
    UnicodeSPrint (ScanResult->DiagnosisDetail, sizeof (ScanResult->DiagnosisDetail),
                   L"Layer 1 OK ama Layer 2 (Data Link) basarisiz. MAC adresi, "
                   L"ARP cevaplari veya switch port/VLAN yapilandirmasini kontrol edin.");
  } else if (L2 != QuickLayerFail && L3 == QuickLayerFail) {
    //
    // Check if it's local L3 (IP config) or external L3 (ping)
    // We differentiate by checking which specific tests failed
    //
    if (ScanResult->Layers[2].TestsPassed == 0) {
      UnicodeSPrint (ScanResult->Diagnosis, sizeof (ScanResult->Diagnosis),
                     L"Frame OK ama IP yapilandirmasi hatali.");
      UnicodeSPrint (ScanResult->DiagnosisDetail, sizeof (ScanResult->DiagnosisDetail),
                     L"Layer 2 OK ama IP konfigurasyonu basarisiz. IPv4 adresi, "
                     L"subnet mask ve gateway ayarlarini kontrol edin.");
    } else {
      UnicodeSPrint (ScanResult->Diagnosis, sizeof (ScanResult->Diagnosis),
                     L"Lokal ag OK ama dis aga cikamiyor. Gateway/routing.");
      UnicodeSPrint (ScanResult->DiagnosisDetail, sizeof (ScanResult->DiagnosisDetail),
                     L"IP konfigurasyonu mevcut ama uzak host'a ulasim yok. "
                     L"Gateway ayarini ve routing tablosunu kontrol edin.");
    }
  } else if (L3 != QuickLayerFail && L4 == QuickLayerFail) {
    UnicodeSPrint (ScanResult->Diagnosis, sizeof (ScanResult->Diagnosis),
                   L"IP OK ama TCP/UDP baglanti kurulam\x0131yor. Firewall.");
    UnicodeSPrint (ScanResult->DiagnosisDetail, sizeof (ScanResult->DiagnosisDetail),
                   L"Layer 3 (Network) OK ama Layer 4 (Transport) basarisiz. "
                   L"Hedef host uzerinde firewall kurallari veya port "
                   L"yapilandirmasini kontrol edin.");
  } else if (L4 != QuickLayerFail && L7 == QuickLayerFail) {
    UnicodeSPrint (ScanResult->Diagnosis, sizeof (ScanResult->Diagnosis),
                   L"Transport OK ama DNS/DHCP/HTTP calismiyor.");
    UnicodeSPrint (ScanResult->DiagnosisDetail, sizeof (ScanResult->DiagnosisDetail),
                   L"Layer 4 OK ama Layer 7 (Application) servisleri basarisiz. "
                   L"DNS sunucusu, DHCP servisi veya HTTP sunucusunu kontrol edin.");
  } else if (L1 == QuickLayerPass && L2 == QuickLayerPass &&
             L3 == QuickLayerPass && L4 == QuickLayerPass &&
             L7 == QuickLayerPass) {
    UnicodeSPrint (ScanResult->Diagnosis, sizeof (ScanResult->Diagnosis),
                   L"Tum katmanlar saglikli.");
    UnicodeSPrint (ScanResult->DiagnosisDetail, sizeof (ScanResult->DiagnosisDetail),
                   L"Tum OSI katmanlarindaki testler basarili. "
                   L"Ag baglantisi tam fonksiyonel gorunuyor.");
  } else {
    //
    // Mixed results — some warnings but no outright failures
    //
    UINTN  WarnCount = 0;
    UINTN  I;

    for (I = 0; I < QUICK_SCAN_LAYERS; I++) {
      if (ScanResult->Layers[I].Status == QuickLayerWarn) {
        WarnCount++;
      }
    }

    if (WarnCount > 0) {
      UnicodeSPrint (ScanResult->Diagnosis, sizeof (ScanResult->Diagnosis),
                     L"Ag calisiyor ama %d katmanda uyarilar var.",
                     WarnCount);
      UnicodeSPrint (ScanResult->DiagnosisDetail, sizeof (ScanResult->DiagnosisDetail),
                     L"Kritik hata yok ama bazi testler uyari verdi. "
                     L"Detayli test ile sorunlar arastirilabilir.");
    } else {
      UnicodeSPrint (ScanResult->Diagnosis, sizeof (ScanResult->Diagnosis),
                     L"Tarama tamamlandi, bazi testler atildi.");
      UnicodeSPrint (ScanResult->DiagnosisDetail, sizeof (ScanResult->DiagnosisDetail),
                     L"Bazi testler gerekli protokol destegi olmadigindan atlandi. "
                     L"Mevcut NIC yapilandirmasini kontrol edin.");
    }
  }
}

//
// ============================================================
// Static: display scan results
// ============================================================
//
STATIC
VOID
QuickDisplayResults (
  IN QUICK_SCAN_RESULT  *ScanResult
  )
{
  UINTN  I;
  UINTN  Row;

  //
  // Clear and draw results box
  //
  UiClearScreen ();
  UiDrawHeader ();
  UiDrawBox (2, 3, 76, 22, L" Quick Scan Results ");

  //
  // Layer-by-layer results table
  //
  Row = 5;
  UiSetColor (COLOR_HEADER, COLOR_BG);
  UiPrintAt (4, Row,
             L"  Layer                   Tests  Pass  Fail  Warn  Skip  Status");
  UiResetColor ();

  UiDrawSeparator (3, Row + 1, 74);

  Row = 7;
  for (I = 0; I < QUICK_SCAN_LAYERS; I++) {
    QUICK_LAYER_RESULT  *LR = &ScanResult->Layers[I];
    CONST CHAR16        *StatusStr;
    UINTN               StatusColor;

    switch (LR->Status) {
      case QuickLayerPass:
        StatusStr   = L"  PASS  ";
        StatusColor = COLOR_SUCCESS;
        break;
      case QuickLayerFail:
        StatusStr   = L"  FAIL  ";
        StatusColor = COLOR_ERROR;
        break;
      case QuickLayerWarn:
        StatusStr   = L"  WARN  ";
        StatusColor = COLOR_WARNING;
        break;
      default:
        StatusStr   = L"  N/A   ";
        StatusColor = COLOR_INFO;
        break;
    }

    UiPrintAt (4, Row + I,
               L"  %-24s %3d   %3d   %3d   %3d   %3d  ",
               RegGetLayerName (LR->Layer),
               LR->TestsRun,
               LR->TestsPassed,
               LR->TestsFailed,
               LR->TestsWarned,
               LR->TestsSkipped);

    UiSetColor (StatusColor, COLOR_BG);
    Print (L"%s", StatusStr);
    UiResetColor ();
  }

  //
  // Summary bar
  //
  Row = 13;
  UiDrawSeparator (3, Row, 74);

  Row = 14;
  UiPrintAt (4, Row,
             L"  Total: %d tests | ",
             ScanResult->TotalTests);
  UiSetColor (COLOR_SUCCESS, COLOR_BG);
  Print (L"%d PASS", ScanResult->TotalPassed);
  UiResetColor ();
  Print (L" | ");
  UiSetColor (COLOR_ERROR, COLOR_BG);
  Print (L"%d FAIL", ScanResult->TotalFailed);
  UiResetColor ();
  Print (L" | ");
  UiSetColor (COLOR_WARNING, COLOR_BG);
  Print (L"%d WARN", ScanResult->TotalWarned);
  UiResetColor ();
  Print (L" | %d SKIP", ScanResult->TotalSkipped);

  //
  // Percentage bar
  //
  if (ScanResult->TotalTests > 0) {
    UINTN  PassPercent = (ScanResult->TotalPassed * 100) /
                         (ScanResult->TotalTests - ScanResult->TotalSkipped > 0 ?
                          ScanResult->TotalTests - ScanResult->TotalSkipped : 1);
    UiDrawProgress (4, Row + 1, 60, PassPercent, L"Health Score");
  }

  //
  // Diagnosis box
  //
  Row = 17;
  UiDrawSeparator (3, Row, 74);

  Row = 18;
  UiSetColor (COLOR_HEADER, COLOR_BG);
  UiPrintAt (4, Row, L"  Diagnostic:");
  UiResetColor ();

  //
  // Color the diagnosis based on severity
  //
  Row = 19;
  BOOLEAN  HasFail = FALSE;
  for (I = 0; I < QUICK_SCAN_LAYERS; I++) {
    if (ScanResult->Layers[I].Status == QuickLayerFail) {
      HasFail = TRUE;
      break;
    }
  }

  if (HasFail) {
    UiSetColor (COLOR_ERROR, COLOR_BG);
  } else if (ScanResult->TotalWarned > 0) {
    UiSetColor (COLOR_WARNING, COLOR_BG);
  } else {
    UiSetColor (COLOR_SUCCESS, COLOR_BG);
  }

  UiPrintAt (4, Row, L"  %s", ScanResult->Diagnosis);
  UiResetColor ();

  //
  // Detail (wrapped if needed)
  //
  Row = 21;
  UiPrintAt (4, Row, L"  %.70s", ScanResult->DiagnosisDetail);
  if (StrLen (ScanResult->DiagnosisDetail) > 70) {
    UiPrintAt (4, Row + 1, L"  %.70s", ScanResult->DiagnosisDetail + 70);
  }

  //
  // Footer
  //
  UiDrawStatusBar (L"Press any key to return...");
}

//
// ============================================================
// Public: QuickScanRun
// Main entry point for quick scan.
//
// Runs representative tests from each OSI layer, collects
// results, applies diagnostic decision tree, and displays
// a summary with health assessment.
//
// @param[in]  Nic     Target NIC to test.
// @param[in]  Config  Test configuration (IPs, timeouts, etc).
//
// @retval EFI_SUCCESS  Scan completed.
// ============================================================
//
EFI_STATUS
QuickScanRun (
  IN NIC_INFO     *Nic,
  IN TEST_CONFIG  *Config
  )
{
  QUICK_SCAN_RESULT  ScanResult;
  UINTN              RunningTotal;
  UINTN              TotalQuickTests;
  UINTN              I;

  if (Nic == NULL || Config == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  //
  // Initialize
  //
  ZeroMem (&ScanResult, sizeof (ScanResult));

  ScanResult.Layers[0].Layer = OsiLayerPhysical;
  ScanResult.Layers[1].Layer = OsiLayerDataLink;
  ScanResult.Layers[2].Layer = OsiLayerNetwork;
  ScanResult.Layers[3].Layer = OsiLayerTransport;
  ScanResult.Layers[4].Layer = OsiLayerApplication;

  TotalQuickTests = QUICK_L1_COUNT + QUICK_L2_COUNT + QUICK_L3_COUNT +
                    QUICK_L4_COUNT + QUICK_L7_COUNT;
  RunningTotal = 0;

  //
  // Ensure registry is initialized
  //
  RegInitAllTests ();

  //
  // Draw scan UI
  //
  UiClearScreen ();
  UiDrawHeader ();
  UiDrawBox (2, 3, 76, 18, L" Quick Scan ");

  UiSetColor (COLOR_INFO, COLOR_BG);
  UiPrintAt (4, 4, L"  Scanning all OSI layers... (%d tests)", TotalQuickTests);
  UiResetColor ();

  //
  // Run L1 tests
  //
  QuickRunLayerTests (
    mQuickL1Tests, QUICK_L1_COUNT,
    Nic, Config,
    &ScanResult.Layers[0], 0,
    TotalQuickTests, &RunningTotal
    );
  ScanResult.Layers[0].Status = QuickDetermineLayerStatus (&ScanResult.Layers[0]);

  //
  // Run L2 tests (skip if L1 failed hard — no link)
  //
  if (ScanResult.Layers[0].Status != QuickLayerFail) {
    QuickRunLayerTests (
      mQuickL2Tests, QUICK_L2_COUNT,
      Nic, Config,
      &ScanResult.Layers[1], 1,
      TotalQuickTests, &RunningTotal
      );
  } else {
    RunningTotal += QUICK_L2_COUNT;
    ScanResult.Layers[1].TestsSkipped = QUICK_L2_COUNT;
    UiPrintAt (4, 7,
               L"  L2 %-30s  [%s]  ",
               L"(Skipped - L1 failed)",
               L"\x25CB SKIP");
  }
  ScanResult.Layers[1].Status = QuickDetermineLayerStatus (&ScanResult.Layers[1]);

  //
  // Run L3 tests
  //
  if (ScanResult.Layers[1].Status != QuickLayerFail) {
    QuickRunLayerTests (
      mQuickL3Tests, QUICK_L3_COUNT,
      Nic, Config,
      &ScanResult.Layers[2], 2,
      TotalQuickTests, &RunningTotal
      );
  } else {
    RunningTotal += QUICK_L3_COUNT;
    ScanResult.Layers[2].TestsSkipped = QUICK_L3_COUNT;
    UiPrintAt (4, 8,
               L"  L3 %-30s  [%s]  ",
               L"(Skipped - L2 failed)",
               L"\x25CB SKIP");
  }
  ScanResult.Layers[2].Status = QuickDetermineLayerStatus (&ScanResult.Layers[2]);

  //
  // Run L4 tests
  //
  if (ScanResult.Layers[2].Status != QuickLayerFail) {
    QuickRunLayerTests (
      mQuickL4Tests, QUICK_L4_COUNT,
      Nic, Config,
      &ScanResult.Layers[3], 3,
      TotalQuickTests, &RunningTotal
      );
  } else {
    RunningTotal += QUICK_L4_COUNT;
    ScanResult.Layers[3].TestsSkipped = QUICK_L4_COUNT;
    UiPrintAt (4, 9,
               L"  L4 %-30s  [%s]  ",
               L"(Skipped - L3 failed)",
               L"\x25CB SKIP");
  }
  ScanResult.Layers[3].Status = QuickDetermineLayerStatus (&ScanResult.Layers[3]);

  //
  // Run L7 tests (always attempt — DHCP/DNS may work even if L4 target tests fail)
  //
  QuickRunLayerTests (
    mQuickL7Tests, QUICK_L7_COUNT,
    Nic, Config,
    &ScanResult.Layers[4], 4,
    TotalQuickTests, &RunningTotal
    );
  ScanResult.Layers[4].Status = QuickDetermineLayerStatus (&ScanResult.Layers[4]);

  //
  // Update progress to 100%
  //
  UiDrawProgress (4, 18, 60, 100, L"Quick Scan Complete");

  //
  // Aggregate totals
  //
  for (I = 0; I < QUICK_SCAN_LAYERS; I++) {
    ScanResult.TotalTests   += ScanResult.Layers[I].TestsRun +
                               ScanResult.Layers[I].TestsSkipped;
    ScanResult.TotalPassed  += ScanResult.Layers[I].TestsPassed;
    ScanResult.TotalFailed  += ScanResult.Layers[I].TestsFailed;
    ScanResult.TotalWarned  += ScanResult.Layers[I].TestsWarned;
    ScanResult.TotalSkipped += ScanResult.Layers[I].TestsSkipped;
  }

  //
  // Apply diagnostic decision tree
  //
  QuickApplyDiagnosis (&ScanResult);

  //
  // Brief pause to show 100% progress
  //
  gBS->Stall (500000);  // 500ms

  //
  // Display results
  //
  QuickDisplayResults (&ScanResult);

  //
  // Wait for key
  //
  UiWaitKey ();

  return EFI_SUCCESS;
}

//
// ============================================================
// Public: QuickScanGetDiagnosis
// Run quick scan and return diagnosis string without UI.
// Useful for programmatic use (e.g., report generation).
//
// @param[in]  Nic         Target NIC.
// @param[in]  Config      Test configuration.
// @param[out] Diagnosis   Buffer for diagnosis string.
// @param[in]  DiagSize    Size of Diagnosis buffer in bytes.
// @param[out] Detail      Buffer for detail string (optional).
// @param[in]  DetailSize  Size of Detail buffer in bytes.
//
// @retval EFI_SUCCESS  Scan completed.
// ============================================================
//
EFI_STATUS
QuickScanGetDiagnosis (
  IN  NIC_INFO     *Nic,
  IN  TEST_CONFIG  *Config,
  OUT CHAR16       *Diagnosis,
  IN  UINTN        DiagSize,
  OUT CHAR16       *Detail     OPTIONAL,
  IN  UINTN        DetailSize
  )
{
  QUICK_SCAN_RESULT  ScanResult;
  TEST_DEFINITION    *Test;
  TEST_RESULT_DATA   TestResult;
  UINTN              I;
  UINTN              J;

  //
  // Quick scan test sets per layer
  //
  CONST CHAR16  **LayerTests[QUICK_SCAN_LAYERS] = {
    mQuickL1Tests, mQuickL2Tests, mQuickL3Tests,
    mQuickL4Tests, mQuickL7Tests
  };
  UINTN  LayerTestCounts[QUICK_SCAN_LAYERS] = {
    QUICK_L1_COUNT, QUICK_L2_COUNT, QUICK_L3_COUNT,
    QUICK_L4_COUNT, QUICK_L7_COUNT
  };

  if (Nic == NULL || Config == NULL || Diagnosis == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  ZeroMem (&ScanResult, sizeof (ScanResult));

  ScanResult.Layers[0].Layer = OsiLayerPhysical;
  ScanResult.Layers[1].Layer = OsiLayerDataLink;
  ScanResult.Layers[2].Layer = OsiLayerNetwork;
  ScanResult.Layers[3].Layer = OsiLayerTransport;
  ScanResult.Layers[4].Layer = OsiLayerApplication;

  RegInitAllTests ();

  //
  // Run all layer tests silently (no UI)
  //
  for (I = 0; I < QUICK_SCAN_LAYERS; I++) {
    //
    // Skip dependent layers if previous failed
    //
    if (I > 0 && I < 4 && ScanResult.Layers[I - 1].Status == QuickLayerFail) {
      ScanResult.Layers[I].TestsSkipped = LayerTestCounts[I];
      ScanResult.Layers[I].Status = QuickLayerNotTested;
      continue;
    }

    for (J = 0; J < LayerTestCounts[I]; J++) {
      Test = QuickFindTest (LayerTests[I][J]);
      if (Test == NULL) {
        continue;
      }

      ZeroMem (&TestResult, sizeof (TestResult));
      RunSingleTest (Test, Nic, Config, &TestResult);

      ScanResult.Layers[I].TestsRun++;

      switch (TestResult.StatusCode) {
        case TEST_RESULT_PASS:
          ScanResult.Layers[I].TestsPassed++;
          break;
        case TEST_RESULT_FAIL:
        case TEST_RESULT_ERROR:
          ScanResult.Layers[I].TestsFailed++;
          break;
        case TEST_RESULT_WARN:
          ScanResult.Layers[I].TestsWarned++;
          break;
        default:
          ScanResult.Layers[I].TestsSkipped++;
          break;
      }
    }

    ScanResult.Layers[I].Status = QuickDetermineLayerStatus (&ScanResult.Layers[I]);
  }

  //
  // Apply diagnosis
  //
  QuickApplyDiagnosis (&ScanResult);

  //
  // Copy results out
  //
  UtilSafeStrCpy (Diagnosis, ScanResult.Diagnosis,
                  DiagSize / sizeof (CHAR16));

  if (Detail != NULL && DetailSize > 0) {
    UtilSafeStrCpy (Detail, ScanResult.DiagnosisDetail,
                    DetailSize / sizeof (CHAR16));
  }

  return EFI_SUCCESS;
}
