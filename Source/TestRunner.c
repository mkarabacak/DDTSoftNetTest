/** @file
  Test runner engine.
  Executes registered tests with timing, prerequisite checking,
  and result capture.
**/

#include <DDTSoftNetTest.h>
#include <OsiLayers.h>

/**
  Check if a NIC meets the prerequisites for a test.

  @param[in] Test  Test definition to check.
  @param[in] Nic   NIC information.

  @retval TRUE   NIC meets all prerequisites.
  @retval FALSE  NIC is missing a required capability.
**/
BOOLEAN
RunCheckPrerequisites (
  IN TEST_DEFINITION  *Test,
  IN NIC_INFO         *Nic
  )
{
  if (Test == NULL || Nic == NULL) {
    return FALSE;
  }

  if (Test->NeedSnp && Nic->Snp == NULL) {
    return FALSE;
  }

  if (Test->NeedIp4 && !Nic->HasIp4) {
    return FALSE;
  }

  if (Test->NeedTcp4 && !Nic->HasTcp4) {
    return FALSE;
  }

  if (Test->NeedUdp4 && !Nic->HasUdp4) {
    return FALSE;
  }

  if (Test->NeedDhcp4 && !Nic->HasDhcp4) {
    return FALSE;
  }

  if (Test->NeedMnp && !Nic->HasMnp) {
    return FALSE;
  }

  return TRUE;
}

/**
  Run a single test with timing and result capture.
  Checks prerequisites first, skips if not met.

  @param[in]  Test    Test definition to execute.
  @param[in]  Nic     Target NIC.
  @param[in]  Config  Test configuration.
  @param[out] Result  Test result data.

  @retval EFI_SUCCESS            Test executed (check Result->StatusCode).
  @retval EFI_INVALID_PARAMETER  NULL parameter.
**/
EFI_STATUS
RunSingleTest (
  IN  TEST_DEFINITION   *Test,
  IN  NIC_INFO          *Nic,
  IN  TEST_CONFIG       *Config,
  OUT TEST_RESULT_DATA  *Result
  )
{
  EFI_STATUS  Status;
  UINT64      StartTime;
  UINT64      EndTime;

  if (Test == NULL || Nic == NULL || Config == NULL || Result == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  ZeroMem (Result, sizeof (TEST_RESULT_DATA));

  //
  // Check prerequisites
  //
  if (!RunCheckPrerequisites (Test, Nic)) {
    Result->StatusCode = TEST_RESULT_SKIP;
    UnicodeSPrint (
      Result->Summary,
      sizeof (Result->Summary),
      L"Skipped: NIC missing required protocol"
      );
    UnicodeSPrint (
      Result->FailReason,
      sizeof (Result->FailReason),
      L"NIC does not support required protocol stack"
      );
    UnicodeSPrint (
      Result->Suggestion,
      sizeof (Result->Suggestion),
      L"Use a NIC with the required protocol support"
      );
    return EFI_SUCCESS;
  }

  //
  // Check if test requires a target and no target IP is set
  //
  if (Test->RequiresTarget) {
    BOOLEAN  AllZero;
    UINTN    I;

    AllZero = TRUE;
    for (I = 0; I < 4; I++) {
      if (Config->TargetIp.Addr[I] != 0) {
        AllZero = FALSE;
        break;
      }
    }

    if (AllZero) {
      Result->StatusCode = TEST_RESULT_SKIP;
      UnicodeSPrint (
        Result->Summary,
        sizeof (Result->Summary),
        L"Skipped: Target IP required but not configured"
        );
      UnicodeSPrint (
        Result->Suggestion,
        sizeof (Result->Suggestion),
        L"Configure a target IP address in test settings"
        );
      return EFI_SUCCESS;
    }
  }

  //
  // Execute the test with timing
  //
  StartTime = UtilGetTimestamp ();

  if (Test->Execute != NULL) {
    Status = Test->Execute (Nic, Config, Result);

    if (EFI_ERROR (Status) && Result->StatusCode == 0) {
      //
      // Test function returned error but didn't set StatusCode
      //
      Result->StatusCode = TEST_RESULT_ERROR;
      UnicodeSPrint (
        Result->Summary,
        sizeof (Result->Summary),
        L"Test returned error: %r",
        Status
        );
    }
  } else {
    Result->StatusCode = TEST_RESULT_SKIP;
    UnicodeSPrint (
      Result->Summary,
      sizeof (Result->Summary),
      L"Skipped: Test not yet implemented"
      );
  }

  EndTime = UtilGetTimestamp ();

  //
  // Calculate duration (UtilGetTimestamp returns seconds)
  //
  if (EndTime >= StartTime) {
    Result->DurationMs = (EndTime - StartTime) * 1000;
  }

  //
  // If no summary was set, generate a default one
  //
  if (Result->Summary[0] == L'\0') {
    switch (Result->StatusCode) {
      case TEST_RESULT_PASS:
        UnicodeSPrint (Result->Summary, sizeof (Result->Summary), L"Test passed");
        break;
      case TEST_RESULT_FAIL:
        UnicodeSPrint (Result->Summary, sizeof (Result->Summary), L"Test failed");
        break;
      case TEST_RESULT_WARN:
        UnicodeSPrint (Result->Summary, sizeof (Result->Summary), L"Test completed with warnings");
        break;
      default:
        UnicodeSPrint (Result->Summary, sizeof (Result->Summary), L"Test completed");
        break;
    }
  }

  return EFI_SUCCESS;
}

/**
  Run all tests for a given OSI layer.

  @param[in]  Layer       OSI layer (OsiLayerAll for all).
  @param[in]  Nic         Target NIC.
  @param[in]  Config      Test configuration.
  @param[out] Results     Array to receive results.
  @param[in]  MaxResults  Maximum results array size.
  @param[out] ResultCount Number of tests executed.

  @retval EFI_SUCCESS  Tests executed.
**/
EFI_STATUS
RunTestsByLayer (
  IN  OSI_LAYER         Layer,
  IN  NIC_INFO          *Nic,
  IN  TEST_CONFIG       *Config,
  OUT TEST_RESULT_DATA  *Results,
  IN  UINTN             MaxResults,
  OUT UINTN             *ResultCount
  )
{
  TEST_DEFINITION  *Tests[MAX_TESTS];
  UINTN            TestCount;
  UINTN            I;

  if (Nic == NULL || Config == NULL || Results == NULL || ResultCount == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  *ResultCount = 0;

  TestCount = RegGetTestsByLayer (Layer, Tests, MAX_TESTS);

  for (I = 0; I < TestCount && I < MaxResults; I++) {
    RunSingleTest (Tests[I], Nic, Config, &Results[I]);
    (*ResultCount)++;
  }

  return EFI_SUCCESS;
}

/**
  Run all registered tests.

  @param[in]  Nic         Target NIC.
  @param[in]  Config      Test configuration.
  @param[out] Results     Array to receive results.
  @param[in]  MaxResults  Maximum results array size.
  @param[out] ResultCount Number of tests executed.

  @retval EFI_SUCCESS  Tests executed.
**/
EFI_STATUS
RunAllTests (
  IN  NIC_INFO          *Nic,
  IN  TEST_CONFIG       *Config,
  OUT TEST_RESULT_DATA  *Results,
  IN  UINTN             MaxResults,
  OUT UINTN             *ResultCount
  )
{
  return RunTestsByLayer (OsiLayerAll, Nic, Config, Results, MaxResults, ResultCount);
}
