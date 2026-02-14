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

#endif // PCI_IDS_H_
