/** @file
  Test case function prototypes for each OSI layer.
**/

#ifndef TEST_CASES_H_
#define TEST_CASES_H_

#include "OsiLayers.h"

//
// Layer 1 - Physical tests
//
EFI_STATUS TestL1NicStatus       (IN NIC_INFO *Nic, IN TEST_CONFIG *Config, OUT TEST_RESULT_DATA *Result);
EFI_STATUS TestL1LinkDetect      (IN NIC_INFO *Nic, IN TEST_CONFIG *Config, OUT TEST_RESULT_DATA *Result);
EFI_STATUS TestL1NicInitCycle    (IN NIC_INFO *Nic, IN TEST_CONFIG *Config, OUT TEST_RESULT_DATA *Result);
EFI_STATUS TestL1Loopback        (IN NIC_INFO *Nic, IN TEST_CONFIG *Config, OUT TEST_RESULT_DATA *Result);
EFI_STATUS TestL1LinkNegotiation (IN NIC_INFO *Nic, IN TEST_CONFIG *Config, OUT TEST_RESULT_DATA *Result);

//
// Layer 2 - Data Link tests
//
EFI_STATUS TestL2MacAddressValid  (IN NIC_INFO *Nic, IN TEST_CONFIG *Config, OUT TEST_RESULT_DATA *Result);
EFI_STATUS TestL2ArpRequestReply  (IN NIC_INFO *Nic, IN TEST_CONFIG *Config, OUT TEST_RESULT_DATA *Result);
EFI_STATUS TestL2ArpCache         (IN NIC_INFO *Nic, IN TEST_CONFIG *Config, OUT TEST_RESULT_DATA *Result);
EFI_STATUS TestL2BroadcastFrame   (IN NIC_INFO *Nic, IN TEST_CONFIG *Config, OUT TEST_RESULT_DATA *Result);
EFI_STATUS TestL2FrameTxRx        (IN NIC_INFO *Nic, IN TEST_CONFIG *Config, OUT TEST_RESULT_DATA *Result);
EFI_STATUS TestL2MtuDetection     (IN NIC_INFO *Nic, IN TEST_CONFIG *Config, OUT TEST_RESULT_DATA *Result);
EFI_STATUS TestL2ReceiveFilter    (IN NIC_INFO *Nic, IN TEST_CONFIG *Config, OUT TEST_RESULT_DATA *Result);

//
// Layer 3 - Network tests
//
EFI_STATUS TestL3IpConfigCheck    (IN NIC_INFO *Nic, IN TEST_CONFIG *Config, OUT TEST_RESULT_DATA *Result);
EFI_STATUS TestL3IcmpEcho         (IN NIC_INFO *Nic, IN TEST_CONFIG *Config, OUT TEST_RESULT_DATA *Result);
EFI_STATUS TestL3IcmpSweep        (IN NIC_INFO *Nic, IN TEST_CONFIG *Config, OUT TEST_RESULT_DATA *Result);
EFI_STATUS TestL3TtlHopDiscovery  (IN NIC_INFO *Nic, IN TEST_CONFIG *Config, OUT TEST_RESULT_DATA *Result);
EFI_STATUS TestL3MtuPathDiscovery (IN NIC_INFO *Nic, IN TEST_CONFIG *Config, OUT TEST_RESULT_DATA *Result);
EFI_STATUS TestL3IpFragmentation  (IN NIC_INFO *Nic, IN TEST_CONFIG *Config, OUT TEST_RESULT_DATA *Result);
EFI_STATUS TestL3Ipv6Nd           (IN NIC_INFO *Nic, IN TEST_CONFIG *Config, OUT TEST_RESULT_DATA *Result);
EFI_STATUS TestL3IpHeaderValid    (IN NIC_INFO *Nic, IN TEST_CONFIG *Config, OUT TEST_RESULT_DATA *Result);
EFI_STATUS TestL3RoutingTable     (IN NIC_INFO *Nic, IN TEST_CONFIG *Config, OUT TEST_RESULT_DATA *Result);
EFI_STATUS TestL3DuplicateIp      (IN NIC_INFO *Nic, IN TEST_CONFIG *Config, OUT TEST_RESULT_DATA *Result);

//
// Layer 4 - Transport tests
//
EFI_STATUS TestL4TcpConnect       (IN NIC_INFO *Nic, IN TEST_CONFIG *Config, OUT TEST_RESULT_DATA *Result);
EFI_STATUS TestL4TcpMultiPort     (IN NIC_INFO *Nic, IN TEST_CONFIG *Config, OUT TEST_RESULT_DATA *Result);
EFI_STATUS TestL4TcpDataTransfer  (IN NIC_INFO *Nic, IN TEST_CONFIG *Config, OUT TEST_RESULT_DATA *Result);
EFI_STATUS TestL4TcpClose         (IN NIC_INFO *Nic, IN TEST_CONFIG *Config, OUT TEST_RESULT_DATA *Result);
EFI_STATUS TestL4UdpSendReceive   (IN NIC_INFO *Nic, IN TEST_CONFIG *Config, OUT TEST_RESULT_DATA *Result);
EFI_STATUS TestL4UdpMultiPort     (IN NIC_INFO *Nic, IN TEST_CONFIG *Config, OUT TEST_RESULT_DATA *Result);
EFI_STATUS TestL4PortScan         (IN NIC_INFO *Nic, IN TEST_CONFIG *Config, OUT TEST_RESULT_DATA *Result);
EFI_STATUS TestL4TcpStress        (IN NIC_INFO *Nic, IN TEST_CONFIG *Config, OUT TEST_RESULT_DATA *Result);

//
// Layer 7 - Application tests
//
EFI_STATUS TestL7DhcpDiscover     (IN NIC_INFO *Nic, IN TEST_CONFIG *Config, OUT TEST_RESULT_DATA *Result);
EFI_STATUS TestL7DhcpLeaseVerify  (IN NIC_INFO *Nic, IN TEST_CONFIG *Config, OUT TEST_RESULT_DATA *Result);
EFI_STATUS TestL7DnsResolve       (IN NIC_INFO *Nic, IN TEST_CONFIG *Config, OUT TEST_RESULT_DATA *Result);
EFI_STATUS TestL7DnsReverse       (IN NIC_INFO *Nic, IN TEST_CONFIG *Config, OUT TEST_RESULT_DATA *Result);
EFI_STATUS TestL7HttpGet          (IN NIC_INFO *Nic, IN TEST_CONFIG *Config, OUT TEST_RESULT_DATA *Result);
EFI_STATUS TestL7HttpStatusCodes  (IN NIC_INFO *Nic, IN TEST_CONFIG *Config, OUT TEST_RESULT_DATA *Result);

#endif // TEST_CASES_H_
