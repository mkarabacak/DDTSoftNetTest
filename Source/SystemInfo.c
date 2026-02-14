/** @file
  System information collection.
  The actual collection functions are implemented in:
    - SmbiosParser.c (CollectFirmwareInfo, CollectSystemInfo, CollectCpuInfo, CollectMemoryInfo)
    - PciEnumerator.c (EnumeratePciDevices)
    - DriverEnumerator.c (EnumerateDrivers, CollectAcpiInfo)
  This file is kept for organizational purposes.
**/

#include <DDTSoftNetTest.h>
#include <SystemInfo.h>
