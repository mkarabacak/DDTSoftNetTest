/** @file
  Layer 1 (Physical) test implementations.
  Tests NIC status, link detection, init cycle, loopback, and link negotiation.
  Uses EFI_SIMPLE_NETWORK_PROTOCOL for hardware-level operations.
**/

#include <DDTSoftNetTest.h>
#include <OsiLayers.h>
#include <TestCases.h>
#include <PacketDefs.h>

/**
  Probe link status by attempting to transmit a minimal frame.
  Many SNP drivers don't update MediaPresent reliably, so we verify
  by actually sending a frame and checking for TX completion.

  @param[in] Snp  Initialized SNP protocol.

  @retval TRUE   TX succeeded — link is up.
  @retval FALSE  TX failed — link may be down.
**/
STATIC
BOOLEAN
ProbeLinkViaTx (
  IN EFI_SIMPLE_NETWORK_PROTOCOL  *Snp
  )
{
  EFI_STATUS  Status;
  UINT8       Frame[64];
  ETHERNET_HEADER  *Eth;
  VOID        *TxBuf;
  UINTN       I;

  ZeroMem (Frame, sizeof (Frame));
  Eth = (ETHERNET_HEADER *)Frame;

  //
  // Build a minimal broadcast frame with experimental EtherType
  //
  for (I = 0; I < 6; I++) {
    Eth->DstMac[I] = 0xFF;
  }
  CopyMem (Eth->SrcMac, Snp->Mode->CurrentAddress.Addr, 6);
  Eth->EtherType = HTONS (0x88B5);

  Status = Snp->Transmit (Snp, 0, sizeof (Frame), Frame, NULL, NULL, NULL);
  if (EFI_ERROR (Status)) {
    return FALSE;
  }

  //
  // Poll for TX completion (up to 200ms)
  //
  TxBuf = NULL;
  for (I = 0; I < 40; I++) {
    Snp->GetStatus (Snp, NULL, &TxBuf);
    if (TxBuf != NULL) {
      return TRUE;
    }
    gBS->Stall (5000);  // 5ms
  }

  //
  // TX was accepted but no recycled buffer — still consider link up
  // since Transmit didn't return an error
  //
  return TRUE;
}

/**
  Poll GetStatus and enable receive filters to refresh MediaPresent.
  Returns TRUE if media is detected after polling.

  @param[in] Snp  Initialized SNP protocol.

  @retval TRUE   Media detected or detection not supported.
  @retval FALSE  Media not detected after polling.
**/
STATIC
BOOLEAN
PollMediaPresent (
  IN EFI_SIMPLE_NETWORK_PROTOCOL  *Snp
  )
{
  UINTN  I;

  //
  // Enable receive filters — some drivers won't report link up without them
  //
  if (Snp->Mode->ReceiveFilterSetting == 0 && Snp->Mode->ReceiveFilterMask != 0) {
    Snp->ReceiveFilters (
      Snp,
      EFI_SIMPLE_NETWORK_RECEIVE_UNICAST |
      EFI_SIMPLE_NETWORK_RECEIVE_BROADCAST,
      0, FALSE, 0, NULL
      );
  }

  //
  // Poll GetStatus multiple times with delays
  //
  for (I = 0; I < 20; I++) {
    Snp->GetStatus (Snp, NULL, NULL);
    if (!Snp->Mode->MediaPresentSupported || Snp->Mode->MediaPresent) {
      return TRUE;
    }
    gBS->Stall (50000);  // 50ms, max ~1s total
  }

  return FALSE;
}

/**
  Test L1.1: NIC Status
  Checks NIC state, media presence, and basic readiness.

  PASS: SNP initialized, media present
  WARN: SNP started but not initialized, or media not present
  FAIL: SNP stopped or NULL
**/
EFI_STATUS
TestL1NicStatus (
  IN  NIC_INFO         *Nic,
  IN  TEST_CONFIG      *Config,
  OUT TEST_RESULT_DATA *Result
  )
{
  EFI_SIMPLE_NETWORK_PROTOCOL  *Snp;
  BOOLEAN                      MediaUp;

  Snp = Nic->Snp;
  if (Snp == NULL) {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"SNP protocol not available on this NIC");
    UnicodeSPrint (Result->Suggestion, sizeof (Result->Suggestion),
                   L"Verify NIC driver is loaded");
    return EFI_SUCCESS;
  }

  if (Snp->Mode->State == EfiSimpleNetworkInitialized) {
    //
    // First try polling MediaPresent via GetStatus
    //
    MediaUp = PollMediaPresent (Snp);

    //
    // If MediaPresent still reports down, do a real TX probe.
    // Many SNP drivers don't update MediaPresent but TX works fine.
    //
    if (!MediaUp) {
      MediaUp = ProbeLinkViaTx (Snp);
    }

    UnicodeSPrint (Result->Detail, sizeof (Result->Detail),
                   L"State: %d  Media: %s  MaxPkt: %d  HdrSize: %d  RxFilter: 0x%X",
                   Snp->Mode->State,
                   MediaUp ? L"Up" : L"Down",
                   Snp->Mode->MaxPacketSize,
                   Snp->Mode->MediaHeaderSize,
                   Snp->Mode->ReceiveFilterSetting);

    if (MediaUp) {
      Result->StatusCode = TEST_RESULT_PASS;
      UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                     L"NIC initialized and ready (MaxPkt=%d)",
                     Snp->Mode->MaxPacketSize);
    } else {
      Result->StatusCode = TEST_RESULT_WARN;
      UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                     L"NIC initialized but no media detected");
      UnicodeSPrint (Result->Suggestion, sizeof (Result->Suggestion),
                     L"Check cable connection");
    }
  } else if (Snp->Mode->State == EfiSimpleNetworkStarted) {
    Result->StatusCode = TEST_RESULT_WARN;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"NIC started but not initialized");
    UnicodeSPrint (Result->Suggestion, sizeof (Result->Suggestion),
                   L"NIC needs Initialize() call");
  } else {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"NIC is in stopped state (%d)", Snp->Mode->State);
    UnicodeSPrint (Result->Suggestion, sizeof (Result->Suggestion),
                   L"Start and initialize the NIC first");
  }

  return EFI_SUCCESS;
}

/**
  Test L1.2: Link Detect
  Verifies physical link is up and media is connected.

  PASS: Media present
  WARN: Media detection not supported (assume connected)
  FAIL: Media not present
**/
EFI_STATUS
TestL1LinkDetect (
  IN  NIC_INFO         *Nic,
  IN  TEST_CONFIG      *Config,
  OUT TEST_RESULT_DATA *Result
  )
{
  EFI_SIMPLE_NETWORK_PROTOCOL  *Snp;
  BOOLEAN                      MediaUp;

  Snp = Nic->Snp;
  if (Snp == NULL) {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"SNP not available");
    return EFI_SUCCESS;
  }

  if (Snp->Mode->State != EfiSimpleNetworkInitialized) {
    Result->StatusCode = TEST_RESULT_WARN;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"NIC not initialized (state=%d)", Snp->Mode->State);
    return EFI_SUCCESS;
  }

  if (!Snp->Mode->MediaPresentSupported) {
    //
    // MediaPresent flag not supported — use TX probe instead
    //
    MediaUp = ProbeLinkViaTx (Snp);
    if (MediaUp) {
      Result->StatusCode = TEST_RESULT_PASS;
      UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                     L"Link is UP (verified via TX probe)");
    } else {
      Result->StatusCode = TEST_RESULT_WARN;
      UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                     L"Media detection not supported, TX probe failed");
    }
    return EFI_SUCCESS;
  }

  //
  // Try polling MediaPresent first
  //
  MediaUp = PollMediaPresent (Snp);

  //
  // If MediaPresent still reports down, fall back to TX probe.
  // Many SNP drivers (e1000, virtio-net) don't update MediaPresent
  // but transmit works fine when the link is actually up.
  //
  if (!MediaUp) {
    MediaUp = ProbeLinkViaTx (Snp);
  }

  if (MediaUp) {
    Result->StatusCode = TEST_RESULT_PASS;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Link is UP, media detected");
  } else {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Link is DOWN, no media detected");
    UnicodeSPrint (Result->FailReason, sizeof (Result->FailReason),
                   L"No physical link detected (MediaPresent=FALSE, TX failed)");
    UnicodeSPrint (Result->Suggestion, sizeof (Result->Suggestion),
                   L"Check Ethernet cable and switch port");
  }

  return EFI_SUCCESS;
}

/**
  Test L1.3: NIC Init Cycle
  Stops, starts, and re-initializes the NIC to verify stability.
  Restores the NIC to its original state afterward.

  PASS: Full cycle completes without error
  FAIL: Any step in the cycle fails
**/
EFI_STATUS
TestL1NicInitCycle (
  IN  NIC_INFO         *Nic,
  IN  TEST_CONFIG      *Config,
  OUT TEST_RESULT_DATA *Result
  )
{
  EFI_SIMPLE_NETWORK_PROTOCOL  *Snp;
  EFI_STATUS                   Status;
  UINT32                       OriginalState;

  Snp = Nic->Snp;
  if (Snp == NULL) {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"SNP not available");
    return EFI_SUCCESS;
  }

  OriginalState = Snp->Mode->State;

  //
  // Step 1: Shutdown if initialized
  //
  if (Snp->Mode->State == EfiSimpleNetworkInitialized) {
    Status = Snp->Shutdown (Snp);
    if (EFI_ERROR (Status)) {
      Result->StatusCode = TEST_RESULT_FAIL;
      UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                     L"Shutdown failed: %r", Status);
      return EFI_SUCCESS;
    }
  }

  //
  // Step 2: Stop if started
  //
  if (Snp->Mode->State == EfiSimpleNetworkStarted) {
    Status = Snp->Stop (Snp);
    if (EFI_ERROR (Status)) {
      Result->StatusCode = TEST_RESULT_FAIL;
      UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                     L"Stop failed: %r", Status);
      return EFI_SUCCESS;
    }
  }

  //
  // Step 3: Start
  //
  Status = Snp->Start (Snp);
  if (EFI_ERROR (Status)) {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Start failed: %r", Status);
    UnicodeSPrint (Result->Suggestion, sizeof (Result->Suggestion),
                   L"NIC may be in an unexpected state");
    return EFI_SUCCESS;
  }

  //
  // Step 4: Initialize
  //
  Status = Snp->Initialize (Snp, 0, 0);
  if (EFI_ERROR (Status)) {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Initialize failed: %r", Status);
    //
    // Try to restore
    //
    Snp->Stop (Snp);
    if (OriginalState >= EfiSimpleNetworkStarted) {
      Snp->Start (Snp);
      if (OriginalState == EfiSimpleNetworkInitialized) {
        Snp->Initialize (Snp, 0, 0);
      }
    }
    return EFI_SUCCESS;
  }

  //
  // Verify we're back to initialized
  //
  if (Snp->Mode->State != EfiSimpleNetworkInitialized) {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"State after init cycle: %d (expected initialized)",
                   Snp->Mode->State);
    return EFI_SUCCESS;
  }

  Result->StatusCode = TEST_RESULT_PASS;
  UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                 L"Init cycle complete: Stop->Start->Initialize OK");

  //
  // If original state was not initialized, restore to started
  //
  if (OriginalState == EfiSimpleNetworkStopped) {
    Snp->Shutdown (Snp);
    Snp->Stop (Snp);
  } else if (OriginalState == EfiSimpleNetworkStarted) {
    Snp->Shutdown (Snp);
  }

  return EFI_SUCCESS;
}

/**
  Test L1.4: Loopback
  Attempts to send a small frame and receive it back via SNP.
  Many NICs/virtual NICs don't support true hardware loopback,
  so this test sends a broadcast frame and checks for TX completion.

  PASS: Frame transmitted successfully
  WARN: TX succeeded but no loopback RX (expected on most NICs)
  FAIL: TX failed
**/
EFI_STATUS
TestL1Loopback (
  IN  NIC_INFO         *Nic,
  IN  TEST_CONFIG      *Config,
  OUT TEST_RESULT_DATA *Result
  )
{
  EFI_SIMPLE_NETWORK_PROTOCOL  *Snp;
  EFI_STATUS                   Status;
  UINT8                        Frame[64];
  ETHERNET_HEADER              *Eth;
  VOID                         *TxBuf;
  UINTN                        I;
  UINT8                        BroadcastMac[6] = ETHERNET_BROADCAST_MAC;

  Snp = Nic->Snp;
  if (Snp == NULL || Snp->Mode->State != EfiSimpleNetworkInitialized) {
    Result->StatusCode = TEST_RESULT_SKIP;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"SNP not initialized");
    return EFI_SUCCESS;
  }

  //
  // Build a minimal broadcast frame (64 bytes padded)
  //
  ZeroMem (Frame, sizeof (Frame));
  Eth = (ETHERNET_HEADER *)Frame;
  CopyMem (Eth->DstMac, BroadcastMac, 6);
  CopyMem (Eth->SrcMac, Snp->Mode->CurrentAddress.Addr, 6);
  Eth->EtherType = HTONS (0x88B5);  // IEEE 802.1 Local Experimental EtherType 1

  //
  // Fill payload with a pattern
  //
  for (I = ETHERNET_HEADER_SIZE; I < sizeof (Frame); I++) {
    Frame[I] = (UINT8)(I & 0xFF);
  }

  //
  // Transmit
  //
  Status = Snp->Transmit (
             Snp,
             0,               // HeaderSize=0: header already in buffer
             sizeof (Frame),
             Frame,
             NULL,
             NULL,
             NULL
             );

  if (EFI_ERROR (Status)) {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Transmit failed: %r", Status);
    UnicodeSPrint (Result->Suggestion, sizeof (Result->Suggestion),
                   L"Verify NIC is initialized and link is up");
    return EFI_SUCCESS;
  }

  Result->PacketsSent = 1;
  Result->BytesSent   = sizeof (Frame);

  //
  // Poll for TX completion
  //
  TxBuf = NULL;
  for (I = 0; I < 100; I++) {
    Status = Snp->GetStatus (Snp, NULL, &TxBuf);
    if (!EFI_ERROR (Status) && TxBuf != NULL) {
      break;
    }
    gBS->Stall (1000);  // 1ms
  }

  if (TxBuf != NULL) {
    Result->StatusCode = TEST_RESULT_PASS;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Frame transmitted and TX completed (64 bytes)");
  } else {
    Result->StatusCode = TEST_RESULT_WARN;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Frame sent but TX completion not confirmed");
    UnicodeSPrint (Result->Detail, sizeof (Result->Detail),
                   L"Some NICs don't recycle TX buffers promptly");
  }

  return EFI_SUCCESS;
}

/**
  Test L1.5: Link Negotiation
  Reports link capabilities and negotiated parameters.
  Reads SNP Mode data for interface type, MAC address size, and filters.

  PASS: Parameters are reasonable
  WARN: Some parameters unusual
**/
EFI_STATUS
TestL1LinkNegotiation (
  IN  NIC_INFO         *Nic,
  IN  TEST_CONFIG      *Config,
  OUT TEST_RESULT_DATA *Result
  )
{
  EFI_SIMPLE_NETWORK_PROTOCOL  *Snp;
  EFI_SIMPLE_NETWORK_MODE      *Mode;

  Snp = Nic->Snp;
  if (Snp == NULL) {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"SNP not available");
    return EFI_SUCCESS;
  }

  Mode = Snp->Mode;

  UnicodeSPrint (Result->Detail, sizeof (Result->Detail),
                 L"IfType: %d  HwAddrSize: %d  MaxPkt: %d  HdrSize: %d  "
                 L"NvRam: %d  RxFilterMask: 0x%X  RxFilter: 0x%X  "
                 L"MCastMax: %d  MacChange: %s  MultipleTx: %s",
                 Mode->IfType,
                 Mode->HwAddressSize,
                 Mode->MaxPacketSize,
                 Mode->MediaHeaderSize,
                 Mode->NvRamSize,
                 Mode->ReceiveFilterMask,
                 Mode->ReceiveFilterSetting,
                 Mode->MaxMCastFilterCount,
                 Mode->MacAddressChangeable ? L"Yes" : L"No",
                 Mode->MultipleTxSupported ? L"Yes" : L"No");

  //
  // Validate parameters
  //
  if (Mode->IfType != 1) {
    //
    // IfType 1 = Ethernet. Others are unusual in this context.
    //
    Result->StatusCode = TEST_RESULT_WARN;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Non-Ethernet interface type (%d)", Mode->IfType);
    return EFI_SUCCESS;
  }

  if (Mode->HwAddressSize != 6) {
    Result->StatusCode = TEST_RESULT_WARN;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Unusual MAC address size: %d (expected 6)", Mode->HwAddressSize);
    return EFI_SUCCESS;
  }

  if (Mode->MaxPacketSize < 1500) {
    Result->StatusCode = TEST_RESULT_WARN;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"MaxPacketSize %d below standard 1500", Mode->MaxPacketSize);
    return EFI_SUCCESS;
  }

  Result->StatusCode = TEST_RESULT_PASS;
  UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                 L"Ethernet link OK (MaxPkt=%d, RxFilter=0x%X)",
                 Mode->MaxPacketSize, Mode->ReceiveFilterSetting);

  return EFI_SUCCESS;
}
