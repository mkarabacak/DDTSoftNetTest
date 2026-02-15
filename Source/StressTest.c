/** @file
  Stress test engine.
  Provides network stress tests with live statistics and ASCII RTT graph.
  Tests: ICMP flood, UDP flood, raw frame flood, combined stress.
**/

#include <DDTSoftNetTest.h>
#include <OsiLayers.h>
#include <TestCases.h>
#include <PacketDefs.h>
#include <UiRenderer.h>

//
// ============================================================
// Constants
// ============================================================
//
#define STRESS_ICMP_ID          0xDD50
#define STRESS_MAX_RTT_SAMPLES  60
#define STRESS_UDP_PORT         5000
#define STRESS_RTT_GRAPH_WIDTH  50
#define STRESS_RTT_GRAPH_HEIGHT 8

//
// ============================================================
// Stress test mode
// ============================================================
//
typedef enum {
  StressModeIcmpFlood = 0,
  StressModeUdpFlood,
  StressModeRawFrameFlood,
  StressModeCombined,
  StressModeMax
} STRESS_MODE;

//
// ============================================================
// Live statistics
// ============================================================
//
typedef struct {
  UINT64    PacketsSent;
  UINT64    PacketsReceived;
  UINT64    BytesSent;
  UINT64    BytesReceived;
  UINT64    PacketsLost;
  UINT32    RttMinUs;
  UINT32    RttMaxUs;
  UINT64    RttTotalUs;
  UINT32    RttCount;
  UINT32    RttSamples[STRESS_MAX_RTT_SAMPLES];
  UINTN     RttSampleIdx;
  UINTN     RttSampleCount;
  UINT64    StartTimeS;
  UINT64    LastUpdateS;
  UINT64    ElapsedS;
  UINT64    PpsSent;
  UINT64    PpsRecv;
  UINT64    BpsSent;
} STRESS_STATS;

//
// ============================================================
// Static: initialize stats
// ============================================================
//
STATIC
VOID
StressInitStats (
  OUT STRESS_STATS  *Stats
  )
{
  ZeroMem (Stats, sizeof (STRESS_STATS));
  Stats->RttMinUs    = 0xFFFFFFFF;
  Stats->StartTimeS  = UtilGetTimestamp ();
  Stats->LastUpdateS = Stats->StartTimeS;
}

//
// ============================================================
// Static: record an RTT sample
// ============================================================
//
STATIC
VOID
StressRecordRtt (
  IN OUT STRESS_STATS  *Stats,
  IN     UINT32        RttUs
  )
{
  Stats->RttTotalUs += RttUs;
  Stats->RttCount++;

  if (RttUs < Stats->RttMinUs) {
    Stats->RttMinUs = RttUs;
  }
  if (RttUs > Stats->RttMaxUs) {
    Stats->RttMaxUs = RttUs;
  }

  Stats->RttSamples[Stats->RttSampleIdx] = RttUs;
  Stats->RttSampleIdx = (Stats->RttSampleIdx + 1) % STRESS_MAX_RTT_SAMPLES;
  if (Stats->RttSampleCount < STRESS_MAX_RTT_SAMPLES) {
    Stats->RttSampleCount++;
  }
}

//
// ============================================================
// Static: draw live statistics panel
// ============================================================
//
STATIC
VOID
StressDrawStats (
  IN STRESS_STATS  *Stats,
  IN STRESS_MODE   Mode,
  IN UINTN         Iteration,
  IN UINTN         TotalIterations
  )
{
  UINT32  RttAvg;
  UINT32  Jitter;
  UINTN   Percent;
  UINT64  Now;

  Now = UtilGetTimestamp ();
  Stats->ElapsedS = Now - Stats->StartTimeS;

  if (Stats->ElapsedS > 0) {
    Stats->PpsSent = Stats->PacketsSent / Stats->ElapsedS;
    Stats->PpsRecv = Stats->PacketsReceived / Stats->ElapsedS;
    Stats->BpsSent = Stats->BytesSent / Stats->ElapsedS;
  }

  RttAvg = (Stats->RttCount > 0) ? (UINT32)(Stats->RttTotalUs / Stats->RttCount) : 0;
  Jitter = (Stats->RttMaxUs > Stats->RttMinUs && Stats->RttMinUs != 0xFFFFFFFF)
           ? (Stats->RttMaxUs - Stats->RttMinUs) : 0;

  Percent = (TotalIterations > 0)
            ? (Iteration * 100 / TotalIterations) : 0;

  //
  // Mode label
  //
  CONST CHAR16  *ModeStr;
  switch (Mode) {
    case StressModeIcmpFlood:     ModeStr = L"ICMP Flood";      break;
    case StressModeUdpFlood:      ModeStr = L"UDP Flood";       break;
    case StressModeRawFrameFlood: ModeStr = L"Raw Frame Flood"; break;
    case StressModeCombined:      ModeStr = L"Combined Stress"; break;
    default:                      ModeStr = L"Stress Test";     break;
  }

  //
  // Stats panel
  //
  UiPrintAt (4, 5,
             L"  Mode: %-20s  Elapsed: %ds  Progress: %d/%d",
             ModeStr, (int)Stats->ElapsedS, (int)Iteration, (int)TotalIterations);

  UiDrawProgress (4, 6, 60, Percent, NULL);

  UiPrintAt (4, 8,
             L"  TX: %llu pkts  %llu bytes  (%llu pps, %llu Bps)    ",
             Stats->PacketsSent, Stats->BytesSent,
             Stats->PpsSent, Stats->BpsSent);

  UiPrintAt (4, 9,
             L"  RX: %llu pkts  %llu bytes  (%llu pps)              ",
             Stats->PacketsReceived, Stats->BytesReceived,
             Stats->PpsRecv);

  UINT64  LostPct = 0;
  if (Stats->PacketsSent > 0) {
    Stats->PacketsLost = Stats->PacketsSent - Stats->PacketsReceived;
    LostPct = (Stats->PacketsLost * 100) / Stats->PacketsSent;
  }

  UiPrintAt (4, 10,
             L"  Lost: %llu (%llu%%)                                ",
             Stats->PacketsLost, LostPct);

  if (Stats->RttCount > 0 && Stats->RttMinUs != 0xFFFFFFFF) {
    UiPrintAt (4, 12,
               L"  RTT min: %d us  avg: %d us  max: %d us  jitter: %d us    ",
               (int)Stats->RttMinUs, (int)RttAvg, (int)Stats->RttMaxUs, (int)Jitter);
  } else {
    UiPrintAt (4, 12,
               L"  RTT: (no data)                                          ");
  }
}

//
// ============================================================
// Static: draw ASCII RTT graph
// ============================================================
//
STATIC
VOID
StressDrawRttGraph (
  IN STRESS_STATS  *Stats
  )
{
  UINTN   I;
  UINTN   Row;
  UINT32  MaxRtt;
  UINT32  SampleVal;
  UINTN   BarHeight;
  UINTN   SampleCount;
  UINTN   StartIdx;

  if (Stats->RttSampleCount == 0) {
    UiPrintAt (4, 14, L"  RTT Graph: (waiting for data)");
    return;
  }

  //
  // Find max RTT in recent samples for scaling
  //
  SampleCount = Stats->RttSampleCount;
  if (SampleCount > STRESS_RTT_GRAPH_WIDTH) {
    SampleCount = STRESS_RTT_GRAPH_WIDTH;
  }

  MaxRtt = 0;
  if (Stats->RttSampleCount >= STRESS_MAX_RTT_SAMPLES) {
    StartIdx = Stats->RttSampleIdx;
  } else {
    StartIdx = 0;
  }

  for (I = 0; I < SampleCount; I++) {
    UINTN  Idx = (StartIdx + I) % STRESS_MAX_RTT_SAMPLES;
    if (Stats->RttSamples[Idx] > MaxRtt) {
      MaxRtt = Stats->RttSamples[Idx];
    }
  }

  if (MaxRtt == 0) {
    MaxRtt = 1;
  }

  //
  // Draw graph header
  //
  UiPrintAt (4, 14, L"  RTT Graph (last %d samples, max %d us):",
             (int)SampleCount, (int)MaxRtt);

  //
  // Draw rows top-to-bottom
  //
  for (Row = 0; Row < STRESS_RTT_GRAPH_HEIGHT; Row++) {
    UINT32  Threshold = (UINT32)((STRESS_RTT_GRAPH_HEIGHT - Row) * MaxRtt / STRESS_RTT_GRAPH_HEIGHT);

    UiPrintAt (4, 15 + Row, L"  %5d|", (int)Threshold);

    for (I = 0; I < SampleCount && I < STRESS_RTT_GRAPH_WIDTH; I++) {
      UINTN  Idx = (StartIdx + I) % STRESS_MAX_RTT_SAMPLES;
      SampleVal = Stats->RttSamples[Idx];

      BarHeight = (SampleVal * STRESS_RTT_GRAPH_HEIGHT) / MaxRtt;
      if (BarHeight > STRESS_RTT_GRAPH_HEIGHT) {
        BarHeight = STRESS_RTT_GRAPH_HEIGHT;
      }

      if (BarHeight >= (STRESS_RTT_GRAPH_HEIGHT - Row)) {
        //
        // Color based on RTT relative to max
        //
        if (SampleVal > (MaxRtt * 3 / 4)) {
          UiSetColor (COLOR_ERROR, COLOR_BG);
        } else if (SampleVal > (MaxRtt / 2)) {
          UiSetColor (COLOR_WARNING, COLOR_BG);
        } else {
          UiSetColor (COLOR_SUCCESS, COLOR_BG);
        }
        Print (L"%c", PROGRESS_FILLED);
        UiResetColor ();
      } else {
        Print (L" ");
      }
    }

    //
    // Pad remaining
    //
    for (; I < STRESS_RTT_GRAPH_WIDTH; I++) {
      Print (L" ");
    }
  }

  //
  // X-axis
  //
  UiPrintAt (4, 15 + STRESS_RTT_GRAPH_HEIGHT, L"       +");
  for (I = 0; I < STRESS_RTT_GRAPH_WIDTH; I++) {
    Print (L"-");
  }
}

//
// ============================================================
// Static: ARP resolve for stress tests
// ============================================================
//
STATIC
EFI_STATUS
StressResolveTargetMac (
  IN  EFI_SIMPLE_NETWORK_PROTOCOL  *Snp,
  IN  CONST UINT8                  *SrcIp,
  IN  CONST UINT8                  *TargetIp,
  OUT UINT8                        *TargetMac
  )
{
  UINT8       ArpFrame[64];
  UINTN       ArpSize;
  UINTN       I;
  EFI_STATUS  Status;
  UINT8       RxBuf[1518];
  UINTN       RxSize;
  VOID        *TxBuf;
  PARSED_PACKET  Parsed;

  ArpSize = PktBuildArpRequest (
              ArpFrame,
              (CONST UINT8 *)Snp->Mode->CurrentAddress.Addr,
              SrcIp,
              TargetIp
              );

  Status = Snp->Transmit (
             Snp,
             0,
             ArpSize,
             ArpFrame,
             NULL,
             NULL,
             NULL
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Wait for ARP reply (up to 3 seconds)
  //
  for (I = 0; I < 3000; I++) {
    //
    // Recycle TX buffers
    //
    TxBuf = NULL;
    Snp->GetStatus (Snp, NULL, &TxBuf);

    RxSize = sizeof (RxBuf);
    Status = Snp->Receive (Snp, NULL, &RxSize, RxBuf, NULL, NULL, NULL);
    if (!EFI_ERROR (Status) && RxSize >= ETHERNET_HEADER_SIZE + ARP_HEADER_SIZE) {
      PktParsePacket (RxBuf, RxSize, &Parsed);
      if (Parsed.HasArp && Parsed.Arp != NULL &&
          NTOHS (Parsed.Arp->Operation) == ARP_OP_REPLY &&
          CompareMem (Parsed.Arp->SenderIp, TargetIp, 4) == 0) {
        CopyMem (TargetMac, Parsed.Arp->SenderMac, 6);
        return EFI_SUCCESS;
      }
    }

    gBS->Stall (1000);
  }

  return EFI_TIMEOUT;
}

//
// ============================================================
// ICMP Flood stress test
// ============================================================
//
STATIC
EFI_STATUS
StressIcmpFlood (
  IN     NIC_INFO      *Nic,
  IN     TEST_CONFIG   *Config,
  IN OUT STRESS_STATS  *Stats
  )
{
  EFI_SIMPLE_NETWORK_PROTOCOL  *Snp;
  EFI_STATUS                   Status;
  UINT8                        Frame[128];
  UINTN                        FrameSize;
  UINT8                        RxBuf[1518];
  UINTN                        RxSize;
  UINT8                        TargetMac[6];
  UINT8                        Payload[56];
  UINTN                        I;
  UINTN                        Iterations;
  UINT16                       SeqNum;
  VOID                         *TxBuf;
  UINT64                       SendTime;
  UINT64                       RecvTime;
  PARSED_PACKET                Parsed;

  Snp = Nic->Snp;
  if (Snp == NULL || Snp->Mode->State != EfiSimpleNetworkInitialized) {
    return EFI_NOT_READY;
  }

  //
  // Resolve target MAC
  //
  Status = StressResolveTargetMac (
             Snp,
             Nic->Ipv4Address.Addr,
             Config->TargetIp.Addr,
             TargetMac
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Build payload pattern
  //
  for (I = 0; I < sizeof (Payload); I++) {
    Payload[I] = (UINT8)(I & 0xFF);
  }

  Iterations = (Config->Iterations > 0) ? Config->Iterations : 100;
  if (Iterations > 10000) {
    Iterations = 10000;
  }

  for (SeqNum = 0; SeqNum < Iterations; SeqNum++) {
    //
    // Build ICMP echo request
    //
    FrameSize = PktBuildIcmpEchoRequest (
                  Frame,
                  (CONST UINT8 *)Snp->Mode->CurrentAddress.Addr,
                  TargetMac,
                  Nic->Ipv4Address.Addr,
                  Config->TargetIp.Addr,
                  STRESS_ICMP_ID,
                  SeqNum,
                  Payload,
                  sizeof (Payload)
                  );

    SendTime = UtilGetTimestamp ();

    Status = Snp->Transmit (
               Snp,
               0,
               FrameSize,
               Frame,
               NULL,
               NULL,
               NULL
               );
    if (!EFI_ERROR (Status)) {
      Stats->PacketsSent++;
      Stats->BytesSent += FrameSize;
    } else {
      continue;
    }

    //
    // Try to receive reply (short timeout — 50ms for flood mode)
    //
    UINTN  RxAttempts;
    for (RxAttempts = 0; RxAttempts < 50; RxAttempts++) {
      TxBuf = NULL;
      Snp->GetStatus (Snp, NULL, &TxBuf);

      RxSize = sizeof (RxBuf);
      Status = Snp->Receive (Snp, NULL, &RxSize, RxBuf, NULL, NULL, NULL);
      if (!EFI_ERROR (Status)) {
        PktParsePacket (RxBuf, RxSize, &Parsed);
        if (Parsed.HasIcmp && Parsed.Icmp != NULL &&
            Parsed.Icmp->Type == ICMP_TYPE_ECHO_REPLY &&
            NTOHS (Parsed.Icmp->Identifier) == STRESS_ICMP_ID &&
            NTOHS (Parsed.Icmp->SequenceNumber) == SeqNum) {
          RecvTime = UtilGetTimestamp ();
          Stats->PacketsReceived++;
          Stats->BytesReceived += RxSize;

          //
          // Calculate RTT in microseconds
          // UtilGetTimestamp returns seconds, so we measure sub-second
          // with Stall count approximation
          //
          UINT32  RttUs = (UINT32)((RecvTime - SendTime) * 1000000);
          if (RttUs == 0) {
            RttUs = RxAttempts * 1000;  // Approximate from poll iterations
          }
          StressRecordRtt (Stats, RttUs);
          break;
        }
      }
      gBS->Stall (1000);  // 1ms
    }

    //
    // Update display every 10 packets
    //
    if ((SeqNum % 10) == 0) {
      StressDrawStats (Stats, StressModeIcmpFlood, SeqNum, Iterations);
      StressDrawRttGraph (Stats);
    }
  }

  return EFI_SUCCESS;
}

//
// ============================================================
// UDP Flood stress test
// ============================================================
//
STATIC
EFI_STATUS
StressUdpFlood (
  IN     NIC_INFO      *Nic,
  IN     TEST_CONFIG   *Config,
  IN OUT STRESS_STATS  *Stats
  )
{
  EFI_SIMPLE_NETWORK_PROTOCOL  *Snp;
  EFI_STATUS                   Status;
  UINT8                        Frame[1518];
  UINTN                        FrameSize;
  UINT8                        RxBuf[1518];
  UINTN                        RxSize;
  UINT8                        TargetMac[6];
  UINT8                        UdpPayload[512];
  UINTN                        I;
  UINTN                        Iterations;
  VOID                         *TxBuf;

  Snp = Nic->Snp;
  if (Snp == NULL || Snp->Mode->State != EfiSimpleNetworkInitialized) {
    return EFI_NOT_READY;
  }

  //
  // Resolve target MAC
  //
  Status = StressResolveTargetMac (
             Snp,
             Nic->Ipv4Address.Addr,
             Config->TargetIp.Addr,
             TargetMac
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Build UDP payload pattern
  //
  for (I = 0; I < sizeof (UdpPayload); I++) {
    UdpPayload[I] = (UINT8)(I & 0xFF);
  }

  Iterations = (Config->Iterations > 0) ? Config->Iterations : 200;
  if (Iterations > 10000) {
    Iterations = 10000;
  }

  for (I = 0; I < Iterations; I++) {
    //
    // Build UDP packet
    //
    UINT16  SrcPort = (UINT16)(10000 + (I % 1000));
    UINT16  DstPort = STRESS_UDP_PORT;

    FrameSize = PktBuildUdpPacket (
                  Frame,
                  (CONST UINT8 *)Snp->Mode->CurrentAddress.Addr,
                  TargetMac,
                  Nic->Ipv4Address.Addr,
                  Config->TargetIp.Addr,
                  SrcPort,
                  DstPort,
                  UdpPayload,
                  sizeof (UdpPayload)
                  );

    Status = Snp->Transmit (
               Snp,
               0,
               FrameSize,
               Frame,
               NULL,
               NULL,
               NULL
               );
    if (!EFI_ERROR (Status)) {
      Stats->PacketsSent++;
      Stats->BytesSent += FrameSize;
    }

    //
    // Poll for TX completion and any RX
    //
    TxBuf = NULL;
    Snp->GetStatus (Snp, NULL, &TxBuf);

    RxSize = sizeof (RxBuf);
    Status = Snp->Receive (Snp, NULL, &RxSize, RxBuf, NULL, NULL, NULL);
    if (!EFI_ERROR (Status)) {
      Stats->PacketsReceived++;
      Stats->BytesReceived += RxSize;
    }

    //
    // Brief delay to avoid overwhelming the NIC
    //
    if ((I % 4) == 0) {
      gBS->Stall (100);  // 0.1ms every 4 packets
    }

    //
    // Update display every 20 packets
    //
    if ((I % 20) == 0) {
      StressDrawStats (Stats, StressModeUdpFlood, I, Iterations);
    }
  }

  return EFI_SUCCESS;
}

//
// ============================================================
// Raw Frame Flood stress test
// Sends broadcast frames at maximum rate to measure PPS capacity.
// ============================================================
//
STATIC
EFI_STATUS
StressRawFrameFlood (
  IN     NIC_INFO      *Nic,
  IN     TEST_CONFIG   *Config,
  IN OUT STRESS_STATS  *Stats
  )
{
  EFI_SIMPLE_NETWORK_PROTOCOL  *Snp;
  EFI_STATUS                   Status;
  UINT8                        Frame[64];
  ETHERNET_HEADER              *Eth;
  UINTN                        I;
  UINTN                        Iterations;
  VOID                         *TxBuf;
  UINT8                        BroadcastMac[6] = ETHERNET_BROADCAST_MAC;

  Snp = Nic->Snp;
  if (Snp == NULL || Snp->Mode->State != EfiSimpleNetworkInitialized) {
    return EFI_NOT_READY;
  }

  //
  // Build minimal broadcast frame
  //
  ZeroMem (Frame, sizeof (Frame));
  Eth = (ETHERNET_HEADER *)Frame;
  CopyMem (Eth->DstMac, BroadcastMac, 6);
  CopyMem (Eth->SrcMac, Snp->Mode->CurrentAddress.Addr, 6);
  Eth->EtherType = HTONS (0x88B5);  // Local Experimental EtherType

  //
  // Fill with pattern
  //
  for (I = ETHERNET_HEADER_SIZE; I < sizeof (Frame); I++) {
    Frame[I] = (UINT8)(I & 0xFF);
  }

  Iterations = (Config->Iterations > 0) ? Config->Iterations : 500;
  if (Iterations > 50000) {
    Iterations = 50000;
  }

  for (I = 0; I < Iterations; I++) {
    Status = Snp->Transmit (
               Snp,
               0,
               sizeof (Frame),
               Frame,
               NULL,
               NULL,
               NULL
               );

    if (!EFI_ERROR (Status)) {
      Stats->PacketsSent++;
      Stats->BytesSent += sizeof (Frame);
    } else if (Status == EFI_NOT_READY) {
      //
      // TX queue full — wait for completion
      //
      UINTN  WaitCount;
      for (WaitCount = 0; WaitCount < 100; WaitCount++) {
        TxBuf = NULL;
        Snp->GetStatus (Snp, NULL, &TxBuf);
        if (TxBuf != NULL) {
          break;
        }
        gBS->Stall (100);
      }
      //
      // Retry
      //
      Status = Snp->Transmit (
                 Snp,
                 0,
                 sizeof (Frame),
                 Frame,
                 NULL,
                 NULL,
                 NULL
                 );
      if (!EFI_ERROR (Status)) {
        Stats->PacketsSent++;
        Stats->BytesSent += sizeof (Frame);
      }
    }

    //
    // Poll TX completion periodically
    //
    if ((I % 8) == 0) {
      TxBuf = NULL;
      Snp->GetStatus (Snp, NULL, &TxBuf);
    }

    //
    // Update display every 50 packets
    //
    if ((I % 50) == 0) {
      StressDrawStats (Stats, StressModeRawFrameFlood, I, Iterations);
    }
  }

  //
  // Drain remaining TX completions
  //
  for (I = 0; I < 100; I++) {
    TxBuf = NULL;
    Snp->GetStatus (Snp, NULL, &TxBuf);
    if (TxBuf == NULL) {
      break;
    }
    gBS->Stall (1000);
  }

  return EFI_SUCCESS;
}

//
// ============================================================
// Static: display final results
// ============================================================
//
STATIC
VOID
StressDisplayFinalResults (
  IN STRESS_STATS  *Stats,
  IN STRESS_MODE   Mode
  )
{
  UINT32  RttAvg;
  UINT32  Jitter;
  UINT64  LossPct;

  CONST CHAR16  *ModeStr;
  switch (Mode) {
    case StressModeIcmpFlood:     ModeStr = L"ICMP Flood";      break;
    case StressModeUdpFlood:      ModeStr = L"UDP Flood";       break;
    case StressModeRawFrameFlood: ModeStr = L"Raw Frame Flood"; break;
    case StressModeCombined:      ModeStr = L"Combined Stress"; break;
    default:                      ModeStr = L"Stress Test";     break;
  }

  RttAvg = (Stats->RttCount > 0) ? (UINT32)(Stats->RttTotalUs / Stats->RttCount) : 0;
  Jitter = (Stats->RttMaxUs > Stats->RttMinUs && Stats->RttMinUs != 0xFFFFFFFF)
           ? (Stats->RttMaxUs - Stats->RttMinUs) : 0;

  Stats->PacketsLost = (Stats->PacketsSent > Stats->PacketsReceived)
                       ? (Stats->PacketsSent - Stats->PacketsReceived) : 0;
  LossPct = (Stats->PacketsSent > 0)
            ? (Stats->PacketsLost * 100 / Stats->PacketsSent) : 0;

  UiClearScreen ();
  UiDrawHeader ();
  UiDrawBox (2, 3, 76, 21, L" Stress Test Results ");

  UiSetColor (COLOR_HEADER, COLOR_BG);
  UiPrintAt (4, 5, L"  Mode: %s", ModeStr);
  UiResetColor ();

  UiPrintAt (4, 6, L"  Duration: %d seconds", (int)Stats->ElapsedS);

  UiDrawSeparator (3, 7, 74);

  UiPrintAt (4, 8,  L"  Packets Sent:     %llu", Stats->PacketsSent);
  UiPrintAt (4, 9,  L"  Packets Received: %llu", Stats->PacketsReceived);
  UiPrintAt (4, 10, L"  Bytes Sent:       %llu", Stats->BytesSent);
  UiPrintAt (4, 11, L"  Bytes Received:   %llu", Stats->BytesReceived);

  UiPrintAt (4, 12, L"  Packet Loss:      ");
  if (LossPct > 10) {
    UiSetColor (COLOR_ERROR, COLOR_BG);
  } else if (LossPct > 0) {
    UiSetColor (COLOR_WARNING, COLOR_BG);
  } else {
    UiSetColor (COLOR_SUCCESS, COLOR_BG);
  }
  Print (L"%llu (%llu%%)", Stats->PacketsLost, LossPct);
  UiResetColor ();

  UiDrawSeparator (3, 13, 74);

  if (Stats->ElapsedS > 0) {
    UiPrintAt (4, 14, L"  Throughput TX:    %llu pps / %llu Bps",
               Stats->PpsSent, Stats->BpsSent);
    UiPrintAt (4, 15, L"  Throughput RX:    %llu pps",
               Stats->PpsRecv);
  }

  if (Stats->RttCount > 0 && Stats->RttMinUs != 0xFFFFFFFF) {
    UiDrawSeparator (3, 16, 74);
    UiPrintAt (4, 17, L"  RTT Min: %d us  Avg: %d us  Max: %d us",
               (int)Stats->RttMinUs, (int)RttAvg, (int)Stats->RttMaxUs);
    UiPrintAt (4, 18, L"  RTT Jitter: %d us  Samples: %d",
               (int)Jitter, (int)Stats->RttCount);
  }

  //
  // Overall verdict
  //
  UiDrawSeparator (3, 19, 74);
  UiPrintAt (4, 20, L"  Verdict: ");
  if (LossPct == 0 && Stats->PacketsSent > 0) {
    UiSetColor (COLOR_SUCCESS, COLOR_BG);
    Print (L"EXCELLENT - No packet loss detected");
  } else if (LossPct <= 1) {
    UiSetColor (COLOR_SUCCESS, COLOR_BG);
    Print (L"GOOD - Minimal packet loss (%llu%%)", LossPct);
  } else if (LossPct <= 5) {
    UiSetColor (COLOR_WARNING, COLOR_BG);
    Print (L"FAIR - Some packet loss (%llu%%)", LossPct);
  } else if (LossPct <= 20) {
    UiSetColor (COLOR_WARNING, COLOR_BG);
    Print (L"POOR - Significant packet loss (%llu%%)", LossPct);
  } else {
    UiSetColor (COLOR_ERROR, COLOR_BG);
    Print (L"CRITICAL - Severe packet loss (%llu%%)", LossPct);
  }
  UiResetColor ();

  UiDrawStatusBar (L"Press any key to return...");
}

//
// ============================================================
// Public: StressTestRun
// Main entry point for stress testing.
// Shows a mode selection menu, runs the selected test with
// live statistics and ASCII RTT graph, then shows final results.
//
// @param[in] Nic     Target NIC.
// @param[in] Config  Test configuration (target IP, iterations, etc).
//
// @retval EFI_SUCCESS  Test completed.
// ============================================================
//
EFI_STATUS
StressTestRun (
  IN NIC_INFO     *Nic,
  IN TEST_CONFIG  *Config
  )
{
  EFI_INPUT_KEY  Key;
  STRESS_MODE    Mode;
  STRESS_STATS   Stats;
  EFI_STATUS     Status;

  if (Nic == NULL || Config == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  //
  // Mode selection menu
  //
  UiClearScreen ();
  UiDrawHeader ();
  UiDrawBox (2, 3, 76, 14, L" Stress Test Mode ");

  UiPrintAt (6, 5, L"Select stress test mode:");
  UiPrintAt (6, 7,  L"[1] ICMP Flood     - Rapid ping with RTT measurement");
  UiPrintAt (6, 8,  L"[2] UDP Flood      - UDP packet flood with loss tracking");
  UiPrintAt (6, 9,  L"[3] Raw Frame Flood - Maximum PPS broadcast frames");
  UiPrintAt (6, 10, L"[4] Combined       - Run all stress tests sequentially");
  UiPrintAt (6, 12, L"[Q] Cancel");

  UiPrintAt (6, 14, L"Iterations: %d  Target: %d.%d.%d.%d",
             (int)((Config->Iterations > 0) ? Config->Iterations : 100),
             (int)Config->TargetIp.Addr[0], (int)Config->TargetIp.Addr[1],
             (int)Config->TargetIp.Addr[2], (int)Config->TargetIp.Addr[3]);

  UiDrawStatusBar (L"Press 1-4 to start, Q to cancel");

  Key = UiWaitKey ();

  switch (Key.UnicodeChar) {
    case L'1':
      Mode = StressModeIcmpFlood;
      break;
    case L'2':
      Mode = StressModeUdpFlood;
      break;
    case L'3':
      Mode = StressModeRawFrameFlood;
      break;
    case L'4':
      Mode = StressModeCombined;
      break;
    case L'q':
    case L'Q':
      return EFI_SUCCESS;
    default:
      return EFI_SUCCESS;
  }

  //
  // Set up and run
  //
  UiClearScreen ();
  UiDrawHeader ();
  UiDrawBox (2, 3, 76, 22, L" Stress Test Running ");

  StressInitStats (&Stats);

  switch (Mode) {
    case StressModeIcmpFlood:
      Status = StressIcmpFlood (Nic, Config, &Stats);
      break;

    case StressModeUdpFlood:
      Status = StressUdpFlood (Nic, Config, &Stats);
      break;

    case StressModeRawFrameFlood:
      Status = StressRawFrameFlood (Nic, Config, &Stats);
      break;

    case StressModeCombined:
      //
      // Run all three sequentially, accumulating stats
      //
      UiPrintAt (4, 4, L"  Phase 1/3: ICMP Flood...");
      Status = StressIcmpFlood (Nic, Config, &Stats);

      UiPrintAt (4, 4, L"  Phase 2/3: UDP Flood...   ");
      Status = StressUdpFlood (Nic, Config, &Stats);

      UiPrintAt (4, 4, L"  Phase 3/3: Raw Frame Flood...");
      Status = StressRawFrameFlood (Nic, Config, &Stats);
      break;

    default:
      Status = EFI_UNSUPPORTED;
      break;
  }

  //
  // Check if test had errors (unused beyond this point but prevents warning)
  //
  if (EFI_ERROR (Status)) {
    UiDrawStatusBar (L"Stress test encountered errors");
  }

  //
  // Final stats update
  //
  Stats.ElapsedS = UtilGetTimestamp () - Stats.StartTimeS;
  if (Stats.ElapsedS > 0) {
    Stats.PpsSent = Stats.PacketsSent / Stats.ElapsedS;
    Stats.PpsRecv = Stats.PacketsReceived / Stats.ElapsedS;
    Stats.BpsSent = Stats.BytesSent / Stats.ElapsedS;
  }

  //
  // Display final results
  //
  StressDisplayFinalResults (&Stats, Mode);

  //
  // Wait for key
  //
  UiWaitKey ();

  return EFI_SUCCESS;
}

//
// ============================================================
// Public: StressTestGetStats
// Run a stress test silently and return statistics.
// For programmatic use (reports).
//
// @param[in]  Nic          Target NIC.
// @param[in]  Config       Test configuration.
// @param[in]  Mode         Stress mode (0=ICMP, 1=UDP, 2=Raw).
// @param[out] Result       Test result data filled with stats.
//
// @retval EFI_SUCCESS  Test completed.
// ============================================================
//
EFI_STATUS
StressTestGetStats (
  IN  NIC_INFO         *Nic,
  IN  TEST_CONFIG      *Config,
  IN  UINT32           Mode,
  OUT TEST_RESULT_DATA *Result
  )
{
  STRESS_STATS  Stats;
  EFI_STATUS    Status;
  UINT32        RttAvg;
  UINT64        LossPct;

  if (Nic == NULL || Config == NULL || Result == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  ZeroMem (Result, sizeof (TEST_RESULT_DATA));
  StressInitStats (&Stats);

  switch (Mode) {
    case StressModeIcmpFlood:
      Status = StressIcmpFlood (Nic, Config, &Stats);
      break;
    case StressModeUdpFlood:
      Status = StressUdpFlood (Nic, Config, &Stats);
      break;
    case StressModeRawFrameFlood:
      Status = StressRawFrameFlood (Nic, Config, &Stats);
      break;
    default:
      Status = EFI_UNSUPPORTED;
      break;
  }

  if (EFI_ERROR (Status)) {
    Result->StatusCode = TEST_RESULT_ERROR;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Stress test failed: %r", Status);
    return Status;
  }

  //
  // Fill result
  //
  Stats.ElapsedS = UtilGetTimestamp () - Stats.StartTimeS;

  Result->PacketsSent     = Stats.PacketsSent;
  Result->PacketsReceived = Stats.PacketsReceived;
  Result->BytesSent       = Stats.BytesSent;
  Result->BytesReceived   = Stats.BytesReceived;
  Result->DurationMs      = Stats.ElapsedS * 1000;

  if (Stats.RttCount > 0 && Stats.RttMinUs != 0xFFFFFFFF) {
    Result->RttMinUs    = Stats.RttMinUs;
    RttAvg              = (UINT32)(Stats.RttTotalUs / Stats.RttCount);
    Result->RttAvgUs    = RttAvg;
    Result->RttMaxUs    = Stats.RttMaxUs;
    Result->RttJitterUs = Stats.RttMaxUs - Stats.RttMinUs;
  }

  Stats.PacketsLost = (Stats.PacketsSent > Stats.PacketsReceived)
                      ? (Stats.PacketsSent - Stats.PacketsReceived) : 0;
  LossPct = (Stats.PacketsSent > 0)
            ? (Stats.PacketsLost * 100 / Stats.PacketsSent) : 0;

  if (LossPct <= 1) {
    Result->StatusCode = TEST_RESULT_PASS;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Stress OK: %llu pkts, %llu%% loss",
                   Stats.PacketsSent, LossPct);
  } else if (LossPct <= 10) {
    Result->StatusCode = TEST_RESULT_WARN;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Stress: %llu%% packet loss (%llu/%llu)",
                   LossPct, Stats.PacketsLost, Stats.PacketsSent);
  } else {
    Result->StatusCode = TEST_RESULT_FAIL;
    UnicodeSPrint (Result->Summary, sizeof (Result->Summary),
                   L"Stress FAIL: %llu%% packet loss",
                   LossPct);
  }

  return EFI_SUCCESS;
}
