/** @file
  NIC discovery and enumeration.
  Stub - to be implemented in Phase 2.
**/

#include <DDTSoftNetTest.h>
#include <SystemInfo.h>

EFI_STATUS
DiscoverNics (
  OUT NIC_INFO  *Nics,
  IN OUT UINTN  *Count
  )
{
  if (Nics == NULL || Count == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  *Count = 0;
  return EFI_UNSUPPORTED;
}
