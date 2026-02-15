/** @file
  PCI vendor and class code lookup tables.
**/

#ifndef PCI_IDS_H_
#define PCI_IDS_H_

#include <Uefi.h>

//
// PCI Vendor ID lookup entry
//
typedef struct {
  UINT16          VendorId;
  CONST CHAR16    *Name;
} PCI_VENDOR_ENTRY;

//
// PCI Class Code lookup entry
//
typedef struct {
  UINT8           ClassCode;
  CONST CHAR16    *Name;
} PCI_CLASS_ENTRY;

//
// Known vendor table
//
STATIC CONST PCI_VENDOR_ENTRY  gPciVendorTable[] = {
  { 0x8086, L"Intel" },
  { 0x10EC, L"Realtek" },
  { 0x14E4, L"Broadcom" },
  { 0x1969, L"Qualcomm Atheros" },
  { 0x10DE, L"NVIDIA" },
  { 0x1022, L"AMD" },
  { 0x1002, L"AMD/ATI" },
  { 0x15B3, L"Mellanox" },
  { 0x1077, L"QLogic" },
  { 0x19A2, L"Emulex" },
  { 0x1137, L"Cisco" },
  { 0x177D, L"Cavium" },
  { 0x1D6A, L"Aquantia" },
  { 0x0000, NULL }
};

//
// PCI class code table
//
STATIC CONST PCI_CLASS_ENTRY  gPciClassTable[] = {
  { 0x00, L"Unclassified" },
  { 0x01, L"Storage" },
  { 0x02, L"Network" },
  { 0x03, L"Display" },
  { 0x04, L"Multimedia" },
  { 0x05, L"Memory" },
  { 0x06, L"Bridge" },
  { 0x07, L"Communication" },
  { 0x08, L"System Peripheral" },
  { 0x09, L"Input Device" },
  { 0x0A, L"Docking Station" },
  { 0x0B, L"Processor" },
  { 0x0C, L"Serial Bus" },
  { 0x0D, L"Wireless" },
  { 0xFF, NULL }
};

//
// PCI NIC Device ID lookup (Vendor:Device -> model name)
//
typedef struct {
  UINT16          VendorId;
  UINT16          DeviceId;
  CONST CHAR16    *Name;
} PCI_NIC_DEVICE_ENTRY;

STATIC CONST PCI_NIC_DEVICE_ENTRY  gPciNicDeviceTable[] = {
  // Intel
  { 0x8086, 0x100E, L"PRO/1000 MT (82540EM)" },
  { 0x8086, 0x100F, L"PRO/1000 MT (82545EM)" },
  { 0x8086, 0x10D3, L"82574L GbE" },
  { 0x8086, 0x10EA, L"82577LM GbE" },
  { 0x8086, 0x1502, L"82579LM GbE" },
  { 0x8086, 0x1503, L"82579V GbE" },
  { 0x8086, 0x150C, L"82583V GbE" },
  { 0x8086, 0x1533, L"I210 GbE" },
  { 0x8086, 0x1539, L"I211 GbE" },
  { 0x8086, 0x15B7, L"I219-LM" },
  { 0x8086, 0x15B8, L"I219-V" },
  { 0x8086, 0x15BB, L"I219-LM (Cannon Point)" },
  { 0x8086, 0x15BC, L"I219-V (Cannon Point)" },
  { 0x8086, 0x15BD, L"I219-LM (Cannon Point)" },
  { 0x8086, 0x15BE, L"I219-V (Cannon Point)" },
  { 0x8086, 0x15D7, L"I219-LM (Comet Lake)" },
  { 0x8086, 0x15D8, L"I219-V (Comet Lake)" },
  { 0x8086, 0x15E3, L"I219-LM (Tiger Lake)" },
  { 0x8086, 0x15F9, L"I219-LM" },
  { 0x8086, 0x15FA, L"I219-V" },
  { 0x8086, 0x15FB, L"I219-LM" },
  { 0x8086, 0x15FC, L"I219-V" },
  { 0x8086, 0x0D4F, L"I219-LM (Alder Lake)" },
  { 0x8086, 0x0D4E, L"I219-V (Alder Lake)" },
  { 0x8086, 0x1A1E, L"I219-LM (Raptor Lake)" },
  { 0x8086, 0x1A1F, L"I219-V (Raptor Lake)" },
  { 0x8086, 0x10FB, L"X520 10GbE SFP+" },
  { 0x8086, 0x1528, L"X540-AT2 10GbE" },
  { 0x8086, 0x15C8, L"I350 GbE" },
  { 0x8086, 0x1521, L"I350 GbE" },
  { 0x8086, 0x1572, L"X710 10GbE SFP+" },
  { 0x8086, 0x158B, L"XXV710 25GbE SFP28" },
  { 0x8086, 0x37D2, L"X722 10GbE" },
  // Realtek
  { 0x10EC, 0x8139, L"RTL8139 100M" },
  { 0x10EC, 0x8168, L"RTL8111/8168 GbE" },
  { 0x10EC, 0x8169, L"RTL8169 GbE" },
  { 0x10EC, 0x8125, L"RTL8125 2.5GbE" },
  { 0x10EC, 0x2600, L"RTL8125B 2.5GbE" },
  // Broadcom
  { 0x14E4, 0x1657, L"BCM5719 GbE" },
  { 0x14E4, 0x165F, L"BCM5720 GbE" },
  { 0x14E4, 0x1682, L"BCM57762 GbE" },
  { 0x14E4, 0x16B5, L"BCM57311 10GbE" },
  { 0x14E4, 0x16B6, L"BCM57312 10GbE" },
  // Qualcomm Atheros
  { 0x1969, 0x1091, L"AR8161 GbE" },
  { 0x1969, 0xE0A1, L"Killer E2500 GbE" },
  { 0x1969, 0x10A1, L"QCA8171 GbE" },
  // Mellanox
  { 0x15B3, 0x1013, L"ConnectX-4 25GbE" },
  { 0x15B3, 0x1015, L"ConnectX-4 Lx 25GbE" },
  { 0x15B3, 0x1017, L"ConnectX-5 100GbE" },
  { 0x15B3, 0x101B, L"ConnectX-6 100GbE" },
  { 0x0000, 0x0000, NULL }
};

//
// Lookup functions
//
CONST CHAR16 *
PciLookupVendorName (
  IN UINT16  VendorId
  );

CONST CHAR16 *
PciLookupClassName (
  IN UINT8  ClassCode
  );

CONST CHAR16 *
PciLookupNicDeviceName (
  IN UINT16  VendorId,
  IN UINT16  DeviceId
  );

#endif // PCI_IDS_H_
