/** @file
  Protocol probe declarations.
  Periodic echo test for ARP, ICMP, UDP, TCP protocols.
  Sends messages with sequence IDs, expects echo back.
**/

#ifndef PROTOCOL_PROBE_H_
#define PROTOCOL_PROBE_H_

#include <DDTSoftNetTest.h>

//
// Probe protocol types
//
typedef enum {
  ProbeArp,
  ProbeIcmp,
  ProbeUdp,
  ProbeTcp,
  ProbeMax
} PROBE_PROTOCOL;

//
// Single probe result
//
#define PROBE_STATUS_PENDING  0
#define PROBE_STATUS_PASS     1
#define PROBE_STATUS_FAIL     2
#define PROBE_STATUS_TIMEOUT  3

#define PROBE_HISTORY_SIZE    12
#define PROBE_PAYLOAD_SIZE    32
#define PROBE_UDP_PORT        5000
#define PROBE_TCP_PORT        22
#define PROBE_TIMEOUT_MS      2000

typedef struct {
  UINT32    SeqId;
  UINT32    Status;       // PROBE_STATUS_*
  UINT32    RttUs;        // Round-trip time in microseconds
} PROBE_ENTRY;

//
// Aggregate probe statistics
//
typedef struct {
  PROBE_PROTOCOL   Protocol;
  UINT32           Sent;
  UINT32           Received;
  UINT32           Lost;
  UINT32           RttMinUs;
  UINT32           RttMaxUs;
  UINT32           RttAvgUs;
  UINT32           RttLastUs;
  UINT64           RttTotalUs;     // For computing average
  UINT32           NextSeqId;
  PROBE_ENTRY      History[PROBE_HISTORY_SIZE];
  UINTN            HistoryHead;    // Ring buffer write index
} PROBE_STATS;

//
// Protocol probe functions
//

/**
  Initialize probe stats for a given protocol.
**/
VOID
ProbeInit (
  OUT PROBE_STATS     *Stats,
  IN  PROBE_PROTOCOL  Protocol
  );

/**
  Execute a single probe round-trip.
  Sends a message with SeqId, waits for echo, records result.

  @param[in]      Nic       NIC to use.
  @param[in]      TargetIp  Target IP for ICMP/UDP/TCP probes.
  @param[in,out]  Stats     Probe statistics (updated with result).

  @retval EFI_SUCCESS  Probe executed (check Stats->History for result).
**/
EFI_STATUS
ProbeExecuteOnce (
  IN     NIC_INFO      *Nic,
  IN     EFI_IPv4_ADDRESS  *TargetIp,
  IN OUT PROBE_STATS   *Stats
  );

/**
  Get human-readable protocol name.

  @param[in] Protocol  Probe protocol enum.

  @return Static string.
**/
CONST CHAR16 *
ProbeGetName (
  IN PROBE_PROTOCOL  Protocol
  );

/**
  Check if a NIC supports the given probe protocol.

  @param[in] Nic       NIC info.
  @param[in] Protocol  Protocol to check.

  @retval TRUE   Protocol available.
  @retval FALSE  Not available.
**/
BOOLEAN
ProbeIsAvailable (
  IN NIC_INFO        *Nic,
  IN PROBE_PROTOCOL  Protocol
  );

#endif // PROTOCOL_PROBE_H_
