/** @file
  System Information View - 5-page system info display.
  Extracted from Main.c.
**/

#ifndef SYSTEM_INFO_VIEW_H_
#define SYSTEM_INFO_VIEW_H_

#include <Uefi.h>

/**
  Show 5-page System Information with left/right navigation.

  @retval EFI_SUCCESS  The view exited normally.
**/
EFI_STATUS
ShowSystemInfo (
  VOID
  );

#endif // SYSTEM_INFO_VIEW_H_
