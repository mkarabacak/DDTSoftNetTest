/** @file
  UI rendering functions.
  Box drawing, color management, menus, progress bars, status bar.
**/

#include <DDTSoftNetTest.h>
#include <UiRenderer.h>

/**
  Try to set the best (widest) console mode available.
  Iterates all modes and picks the one with the most columns.
**/
VOID
UiSetBestConsoleMode (
  VOID
  )
{
  EFI_STATUS  Status;
  UINTN       MaxMode;
  UINTN       Mode;
  UINTN       Cols;
  UINTN       Rows;
  UINTN       BestMode;
  UINTN       BestCols;
  UINTN       BestRows;

  MaxMode  = (UINTN)gST->ConOut->Mode->MaxMode;
  BestMode = (UINTN)gST->ConOut->Mode->Mode;
  BestCols = 80;
  BestRows = 25;

  for (Mode = 0; Mode < MaxMode; Mode++) {
    Status = gST->ConOut->QueryMode (gST->ConOut, Mode, &Cols, &Rows);
    if (!EFI_ERROR (Status)) {
      //
      // Prefer wider modes; among equal width, prefer taller
      //
      if (Cols > BestCols || (Cols == BestCols && Rows > BestRows)) {
        BestMode = Mode;
        BestCols = Cols;
        BestRows = Rows;
      }
    }
  }

  if (BestMode != (UINTN)gST->ConOut->Mode->Mode) {
    gST->ConOut->SetMode (gST->ConOut, BestMode);
  }
}

/**
  Get the current screen width (columns).

  @return  Number of columns.
**/
UINTN
UiGetScreenWidth (
  VOID
  )
{
  UINTN  Cols;
  UINTN  Rows;

  gST->ConOut->QueryMode (gST->ConOut, gST->ConOut->Mode->Mode, &Cols, &Rows);
  return Cols;
}

/**
  Get the current screen height (rows).

  @return  Number of rows.
**/
UINTN
UiGetScreenHeight (
  VOID
  )
{
  UINTN  Cols;
  UINTN  Rows;

  gST->ConOut->QueryMode (gST->ConOut, gST->ConOut->Mode->Mode, &Cols, &Rows);
  return Rows;
}

/**
  Hide the text cursor to reduce visual noise during screen updates.
**/
VOID
UiHideCursor (
  VOID
  )
{
  gST->ConOut->EnableCursor (gST->ConOut, FALSE);
}

/**
  Clear the screen and set background color.
**/
VOID
UiClearScreen (
  VOID
  )
{
  gST->ConOut->SetAttribute (gST->ConOut, EFI_TEXT_ATTR (COLOR_DEFAULT, COLOR_BG));
  gST->ConOut->ClearScreen (gST->ConOut);
}

/**
  Clear specific rows by overwriting with spaces.
  This avoids the full-screen flash that ClearScreen causes.

  @param[in]  StartRow  First row to clear (inclusive).
  @param[in]  EndRow    Last row to clear (inclusive).
**/
VOID
UiClearLines (
  IN UINTN  StartRow,
  IN UINTN  EndRow
  )
{
  UINTN   Cols;
  UINTN   Rows;
  UINTN   Row;
  UINTN   I;
  CHAR16  BlankBuf[256];

  gST->ConOut->QueryMode (gST->ConOut, gST->ConOut->Mode->Mode, &Cols, &Rows);

  if (EndRow >= Rows) {
    EndRow = Rows - 1;
  }

  //
  // Build a blank line buffer (up to 254 columns).
  // Use Cols-1 to avoid cursor wrap at the right edge.
  //
  if (Cols > 255) {
    Cols = 255;
  }
  if (Cols > 0) {
    Cols--;
  }
  for (I = 0; I < Cols; I++) {
    BlankBuf[I] = L' ';
  }
  BlankBuf[Cols] = L'\0';

  gST->ConOut->SetAttribute (gST->ConOut, EFI_TEXT_ATTR (COLOR_DEFAULT, COLOR_BG));

  for (Row = StartRow; Row <= EndRow; Row++) {
    gST->ConOut->SetCursorPosition (gST->ConOut, 0, Row);
    Print (L"%s", BlankBuf);
  }
}

/**
  Set foreground and background color.

  @param[in]  Foreground  Foreground color.
  @param[in]  Background  Background color.
**/
VOID
UiSetColor (
  IN UINTN  Foreground,
  IN UINTN  Background
  )
{
  gST->ConOut->SetAttribute (gST->ConOut, EFI_TEXT_ATTR (Foreground, Background));
}

/**
  Reset color to default (white on black).
**/
VOID
UiResetColor (
  VOID
  )
{
  gST->ConOut->SetAttribute (gST->ConOut, EFI_TEXT_ATTR (COLOR_DEFAULT, COLOR_BG));
}

/**
  Print formatted text at a specific position.

  @param[in]  Col  Column (0-based).
  @param[in]  Row  Row (0-based).
  @param[in]  Fmt  Format string.
  @param[in]  ...  Variable arguments.
**/
VOID
EFIAPI
UiPrintAt (
  IN UINTN          Col,
  IN UINTN          Row,
  IN CONST CHAR16   *Fmt,
  ...
  )
{
  VA_LIST  Args;
  CHAR16   Buffer[256];
  UINTN    Cols;
  UINTN    Rows;
  UINTN    MaxLen;

  gST->ConOut->SetCursorPosition (gST->ConOut, Col, Row);

  VA_START (Args, Fmt);
  UnicodeVSPrint (Buffer, sizeof (Buffer), Fmt, Args);
  VA_END (Args);

  //
  // Clip output to prevent line wrapping and screen scrolling.
  // Leave 1 column margin to avoid cursor-advance wrap on some firmware.
  //
  gST->ConOut->QueryMode (gST->ConOut, gST->ConOut->Mode->Mode, &Cols, &Rows);
  if (Col < Cols) {
    MaxLen = Cols - Col - 1;
    if (StrLen (Buffer) > MaxLen) {
      Buffer[MaxLen] = L'\0';
    }
  }

  Print (L"%s", Buffer);
}

/**
  Draw a box with optional title using box-drawing characters.

  @param[in]  Col     Left column.
  @param[in]  Row     Top row.
  @param[in]  Width   Width including borders.
  @param[in]  Height  Height including borders.
  @param[in]  Title   Optional title string for top border.
**/
VOID
UiDrawBox (
  IN UINTN          Col,
  IN UINTN          Row,
  IN UINTN          Width,
  IN UINTN          Height,
  IN CONST CHAR16   *Title  OPTIONAL
  )
{
  UINTN   I;
  UINTN   TitleLen;
  UINTN   PadBefore;
  UINTN   PadAfter;

  //
  // Top border
  //
  gST->ConOut->SetCursorPosition (gST->ConOut, Col, Row);
  Print (L"%c", BOX_TL);

  if (Title != NULL) {
    TitleLen = StrLen (Title);
    if (TitleLen > Width - 4) {
      TitleLen = Width - 4;
    }
    PadBefore = 1;
    PadAfter  = Width - 2 - PadBefore - TitleLen - 2;

    for (I = 0; I < PadBefore; I++) {
      Print (L"%c", BOX_H);
    }
    Print (L" %.*s ", (UINTN)TitleLen, Title);
    for (I = 0; I < PadAfter; I++) {
      Print (L"%c", BOX_H);
    }
  } else {
    for (I = 0; I < Width - 2; I++) {
      Print (L"%c", BOX_H);
    }
  }

  Print (L"%c", BOX_TR);

  //
  // Side borders
  //
  for (I = 1; I < Height - 1; I++) {
    gST->ConOut->SetCursorPosition (gST->ConOut, Col, Row + I);
    Print (L"%c", BOX_V);
    gST->ConOut->SetCursorPosition (gST->ConOut, Col + Width - 1, Row + I);
    Print (L"%c", BOX_V);
  }

  //
  // Bottom border
  //
  gST->ConOut->SetCursorPosition (gST->ConOut, Col, Row + Height - 1);
  Print (L"%c", BOX_BL);
  for (I = 0; I < Width - 2; I++) {
    Print (L"%c", BOX_H);
  }
  Print (L"%c", BOX_BR);
}

/**
  Draw the DDTSoft header banner.
**/
VOID
UiDrawHeader (
  VOID
  )
{
  UINTN  Width;

  Width = UiGetScreenWidth () - 2;
  if (Width < 60) {
    Width = 60;
  }

  UiSetColor (COLOR_HEADER, COLOR_BG);
  UiDrawBox (1, 0, Width, 3, NULL);
  UiPrintAt (3, 1, L" DDTSoft - EFI Network Test & OSI Analyzer v1.0.0");
  UiResetColor ();
}

/**
  Draw the main menu with items.

  @param[in]  Items     Array of menu items.
  @param[in]  Count     Number of items.
  @param[in]  Selected  Currently selected index (unused for now).
**/
VOID
UiDrawMenu (
  IN MENU_ITEM  *Items,
  IN UINTN      Count,
  IN UINTN      Selected
  )
{
  UINTN  I;
  UINTN  StartRow;
  UINTN  Width;

  StartRow = 4;
  Width = UiGetScreenWidth () - 2;
  if (Width < 60) {
    Width = 60;
  }

  UiSetColor (COLOR_HEADER, COLOR_BG);
  UiDrawBox (1, StartRow - 1, Width, Count + 4, NULL);

  //
  // Draw separator after header box connects to menu box
  //
  UiDrawSeparator (1, StartRow - 1, Width);

  for (I = 0; I < Count; I++) {
    gST->ConOut->SetCursorPosition (gST->ConOut, 3, StartRow + I + 1);

    UiSetColor (COLOR_INFO, COLOR_BG);
    Print (L"   [");
    UiSetColor (COLOR_WARNING, COLOR_BG);
    Print (L"%c", Items[I].Key);
    UiSetColor (COLOR_INFO, COLOR_BG);
    Print (L"] ");

    UiSetColor (COLOR_DEFAULT, COLOR_BG);
    Print (L"%-22s", Items[I].Label);

    UiSetColor (EFI_LIGHTGRAY, COLOR_BG);
    Print (L" %s", Items[I].Description);
  }

  UiResetColor ();
}

/**
  Draw a progress bar.

  @param[in]  Col      Left column.
  @param[in]  Row      Row.
  @param[in]  Width    Total width of the progress bar area.
  @param[in]  Percent  Completion percentage (0-100).
  @param[in]  Label    Optional label displayed before the bar.
**/
VOID
UiDrawProgress (
  IN UINTN          Col,
  IN UINTN          Row,
  IN UINTN          Width,
  IN UINTN          Percent,
  IN CONST CHAR16   *Label  OPTIONAL
  )
{
  UINTN  BarWidth;
  UINTN  Filled;
  UINTN  I;
  UINTN  LabelLen;

  LabelLen = 0;
  if (Label != NULL) {
    gST->ConOut->SetCursorPosition (gST->ConOut, Col, Row);
    UiSetColor (COLOR_DEFAULT, COLOR_BG);
    Print (L"%s ", Label);
    LabelLen = StrLen (Label) + 1;
  }

  //
  // Reserve space for percentage display " 100%"
  //
  BarWidth = Width - LabelLen - 5;
  if (Percent > 100) {
    Percent = 100;
  }
  Filled = (BarWidth * Percent) / 100;

  gST->ConOut->SetCursorPosition (gST->ConOut, Col + LabelLen, Row);
  Print (L"[");

  UiSetColor (COLOR_SUCCESS, COLOR_BG);
  for (I = 0; I < Filled; I++) {
    Print (L"%c", PROGRESS_FILLED);
  }

  UiSetColor (EFI_LIGHTGRAY, COLOR_BG);
  for (I = Filled; I < BarWidth; I++) {
    Print (L"%c", PROGRESS_EMPTY);
  }

  UiSetColor (COLOR_DEFAULT, COLOR_BG);
  Print (L"] %3d%%", Percent);
  UiResetColor ();
}

/**
  Draw a horizontal separator line.

  @param[in]  Col    Left column.
  @param[in]  Row    Row.
  @param[in]  Width  Width of the separator.
**/
VOID
UiDrawSeparator (
  IN UINTN  Col,
  IN UINTN  Row,
  IN UINTN  Width
  )
{
  UINTN  I;

  gST->ConOut->SetCursorPosition (gST->ConOut, Col, Row);
  Print (L"%c", BOX_LT);
  for (I = 0; I < Width - 2; I++) {
    Print (L"%c", BOX_H);
  }
  Print (L"%c", BOX_RT);
}

/**
  Draw a status bar at the bottom of the screen.

  @param[in]  Message  Status message to display.
**/
VOID
UiDrawStatusBar (
  IN CONST CHAR16  *Message
  )
{
  UINTN  Rows;
  UINTN  Cols;
  UINTN  PadWidth;

  gST->ConOut->QueryMode (gST->ConOut, gST->ConOut->Mode->Mode, &Cols, &Rows);

  //
  // On the last row, total output must be < Cols to avoid cursor wrap
  // which scrolls the entire screen up. Use Cols-2 padding so total
  // output = 1 (space) + (Cols-2) = Cols-1 characters.
  //
  PadWidth = (Cols > 2) ? (Cols - 2) : 1;

  gST->ConOut->SetCursorPosition (gST->ConOut, 0, Rows - 1);
  UiSetColor (EFI_BLACK, EFI_LIGHTGRAY);
  Print (L" %-*s", PadWidth, Message);
  UiResetColor ();
}

/**
  Wait for a key press and return the key.

  @return  The pressed key.
**/
EFI_INPUT_KEY
UiWaitKey (
  VOID
  )
{
  EFI_INPUT_KEY  Key;
  UINTN          EventIndex;

  gBS->WaitForEvent (1, &gST->ConIn->WaitForKey, &EventIndex);
  gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);

  return Key;
}

/**
  Wait for a key press with timeout.
  Returns TRUE if a key was pressed, FALSE if timed out.
  On timeout, the caller can redraw the screen and call again.

  @param[in]  TimeoutMs  Timeout in milliseconds.
  @param[out] Key        The pressed key (valid only if TRUE returned).

  @retval TRUE   Key was pressed.
  @retval FALSE  Timeout expired, no key pressed.
**/
BOOLEAN
UiWaitKeyTimeout (
  IN  UINT32          TimeoutMs,
  OUT EFI_INPUT_KEY   *Key
  )
{
  EFI_STATUS  Status;
  EFI_EVENT   TimerEvent;
  EFI_EVENT   Events[2];
  UINTN       EventIndex;

  ZeroMem (Key, sizeof (EFI_INPUT_KEY));

  //
  // Create one-shot timer event
  //
  Status = gBS->CreateEvent (EVT_TIMER, 0, NULL, NULL, &TimerEvent);
  if (EFI_ERROR (Status)) {
    //
    // Fallback to blocking wait if timer creation fails
    //
    *Key = UiWaitKey ();
    return TRUE;
  }

  //
  // Set timer: TimeoutMs * 10000 converts ms to 100ns units
  //
  gBS->SetTimer (TimerEvent, TimerRelative, (UINT64)TimeoutMs * 10000);

  Events[0] = gST->ConIn->WaitForKey;
  Events[1] = TimerEvent;

  gBS->WaitForEvent (2, Events, &EventIndex);

  if (EventIndex == 0) {
    //
    // Key was pressed
    //
    gST->ConIn->ReadKeyStroke (gST->ConIn, Key);
    gBS->CloseEvent (TimerEvent);
    return TRUE;
  }

  //
  // Timer expired — no key pressed
  //
  gBS->CloseEvent (TimerEvent);
  return FALSE;
}

/**
  Show a "Coming Soon" message for an unimplemented feature.

  @param[in]  FeatureName  Name of the feature.
**/
VOID
UiShowComingSoon (
  IN CONST CHAR16  *FeatureName
  )
{
  UiClearScreen ();
  UiDrawHeader ();

  UiSetColor (COLOR_WARNING, COLOR_BG);
  UiDrawBox (5, 6, 56, 7, FeatureName);

  UiPrintAt (8, 8,  L"Bu ozellik henuz gelistirme asamasinda.");
  UiPrintAt (8, 9,  L"Sonraki fazlarda implement edilecek.");

  UiSetColor (EFI_LIGHTGRAY, COLOR_BG);
  UiPrintAt (8, 11, L"Devam etmek icin herhangi bir tusa basin...");
  UiResetColor ();

  UiWaitKey ();
}
