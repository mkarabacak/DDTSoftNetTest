/** @file
  UI rendering functions for console output.
  Box drawing, color management, menus, progress bars.
**/

#ifndef UI_RENDERER_H_
#define UI_RENDERER_H_

#include <Uefi.h>
#include "DDTSoftNetTest.h"

//
// Box drawing characters (Unicode)
//
#define BOX_TL       L'\x2554'   // top-left corner
#define BOX_TR       L'\x2557'   // top-right corner
#define BOX_BL       L'\x255A'   // bottom-left corner
#define BOX_BR       L'\x255D'   // bottom-right corner
#define BOX_H        L'\x2550'   // horizontal line
#define BOX_V        L'\x2551'   // vertical line
#define BOX_LT       L'\x2560'   // left tee
#define BOX_RT       L'\x2563'   // right tee
#define BOX_CROSS    L'\x256C'   // cross

//
// Progress bar characters
//
#define PROGRESS_FILLED  L'\x2588'   // full block
#define PROGRESS_EMPTY   L'\x2591'   // light shade

//
// Console mode and dimensions
//
VOID
UiSetBestConsoleMode (
  VOID
  );

UINTN
UiGetScreenWidth (
  VOID
  );

UINTN
UiGetScreenHeight (
  VOID
  );

//
// Screen management
//
VOID
UiClearScreen (
  VOID
  );

VOID
UiHideCursor (
  VOID
  );

VOID
UiClearLines (
  IN UINTN  StartRow,
  IN UINTN  EndRow
  );

VOID
UiSetColor (
  IN UINTN  Foreground,
  IN UINTN  Background
  );

VOID
UiResetColor (
  VOID
  );

//
// Positioning and printing
//
VOID
EFIAPI
UiPrintAt (
  IN UINTN          Col,
  IN UINTN          Row,
  IN CONST CHAR16   *Fmt,
  ...
  );

//
// Box drawing
//
VOID
UiDrawBox (
  IN UINTN          Col,
  IN UINTN          Row,
  IN UINTN          Width,
  IN UINTN          Height,
  IN CONST CHAR16   *Title  OPTIONAL
  );

//
// Header and branding
//
VOID
UiDrawHeader (
  VOID
  );

//
// Menu rendering
//
VOID
UiDrawMenu (
  IN MENU_ITEM  *Items,
  IN UINTN      Count,
  IN UINTN      Selected
  );

//
// Progress bar
//
VOID
UiDrawProgress (
  IN UINTN          Col,
  IN UINTN          Row,
  IN UINTN          Width,
  IN UINTN          Percent,
  IN CONST CHAR16   *Label  OPTIONAL
  );

//
// Separator line
//
VOID
UiDrawSeparator (
  IN UINTN  Col,
  IN UINTN  Row,
  IN UINTN  Width
  );

//
// Status bar at bottom
//
VOID
UiDrawStatusBar (
  IN CONST CHAR16  *Message
  );

//
// Key input
//
EFI_INPUT_KEY
UiWaitKey (
  VOID
  );

//
// Message display helpers
//
VOID
UiShowComingSoon (
  IN CONST CHAR16  *FeatureName
  );

#endif // UI_RENDERER_H_
