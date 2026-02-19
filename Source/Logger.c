/** @file
  Logger - Circular buffer log system.
  Records test execution, companion events, errors/warnings.
**/

#include <DDTSoftNetTest.h>
#include <UiRenderer.h>
#include <Logger.h>

//
// Circular buffer storage
//
STATIC LOG_ENTRY  mLogEntries[LOG_MAX_ENTRIES];
STATIC UINTN      mLogHead;
STATIC UINTN      mLogCount;

/**
  Initialize the log system. Clears the buffer.
**/
VOID
LogInit (
  VOID
  )
{
  ZeroMem (mLogEntries, sizeof (mLogEntries));
  mLogHead  = 0;
  mLogCount = 0;
}

/**
  Add a log message with format string support.

  @param[in]  Level  Severity level.
  @param[in]  Fmt    Format string (CHAR16).
  @param[in]  ...    Variable arguments.
**/
VOID
EFIAPI
LogAdd (
  IN LOG_LEVEL        Level,
  IN CONST CHAR16     *Fmt,
  ...
  )
{
  VA_LIST     Args;
  LOG_ENTRY   *Entry;

  Entry = &mLogEntries[mLogHead];
  Entry->Level     = Level;
  Entry->Timestamp = UtilGetTimestamp ();

  VA_START (Args, Fmt);
  UnicodeVSPrint (Entry->Message, sizeof (Entry->Message), Fmt, Args);
  VA_END (Args);

  mLogHead = (mLogHead + 1) % LOG_MAX_ENTRIES;
  if (mLogCount < LOG_MAX_ENTRIES) {
    mLogCount++;
  }
}

/**
  Get the color attribute for a log level.
**/
STATIC
UINTN
LogLevelColor (
  IN LOG_LEVEL  Level
  )
{
  switch (Level) {
    case LOG_DEBUG:   return EFI_LIGHTGRAY;
    case LOG_INFO:    return EFI_WHITE;
    case LOG_WARNING: return EFI_YELLOW;
    case LOG_ERROR:   return EFI_RED;
    case LOG_SUCCESS: return EFI_GREEN;
    default:          return EFI_WHITE;
  }
}

/**
  Get the level prefix tag.
**/
STATIC
CONST CHAR16 *
LogLevelTag (
  IN LOG_LEVEL  Level
  )
{
  switch (Level) {
    case LOG_DEBUG:   return L"DBG";
    case LOG_INFO:    return L"INF";
    case LOG_WARNING: return L"WRN";
    case LOG_ERROR:   return L"ERR";
    case LOG_SUCCESS: return L"OK ";
    default:          return L"???";
  }
}

/**
  Draw a log panel showing the most recent entries that fit.

  @param[in]  Col     Starting column.
  @param[in]  Row     Starting row.
  @param[in]  Width   Panel width in characters.
  @param[in]  Height  Panel height in rows.
**/
VOID
LogDrawPanel (
  IN UINTN  Col,
  IN UINTN  Row,
  IN UINTN  Width,
  IN UINTN  Height
  )
{
  UINTN       DrawCount;
  UINTN       StartIdx;
  UINTN       I;
  UINTN       CurRow;
  UINTN       MsgWidth;
  LOG_ENTRY   *Entry;

  if (mLogCount == 0 || Height == 0) {
    return;
  }

  //
  // Show as many recent entries as fit in Height rows
  //
  DrawCount = (mLogCount < Height) ? mLogCount : Height;
  StartIdx  = mLogCount - DrawCount;

  //
  // Max message width = panel width minus "[TAG] " prefix (6 chars)
  //
  MsgWidth = (Width > 6) ? (Width - 6) : 1;

  CurRow = Row;
  for (I = 0; I < DrawCount; I++) {
    Entry = LogGetEntry (StartIdx + I);
    if (Entry == NULL) {
      continue;
    }

    UiSetColor (LogLevelColor (Entry->Level), COLOR_BG);
    UiPrintAt (Col, CurRow, L"[%s] %-*.*s",
               LogLevelTag (Entry->Level),
               (int)MsgWidth, (int)MsgWidth,
               Entry->Message);
    CurRow++;
  }
}

/**
  Get the total number of log entries.

  @return  Number of entries currently stored.
**/
UINTN
LogGetCount (
  VOID
  )
{
  return mLogCount;
}

/**
  Get a log entry by index.

  @param[in]  Index  Entry index (0 = oldest).

  @return  Pointer to the log entry, or NULL if out of range.
**/
LOG_ENTRY *
LogGetEntry (
  IN UINTN  Index
  )
{
  UINTN  Pos;

  if (Index >= mLogCount) {
    return NULL;
  }

  //
  // mLogHead points to the next write slot.
  // Oldest entry is at (mLogHead - mLogCount) mod size.
  // Entry[Index] = (oldest + Index) mod size.
  //
  Pos = (mLogHead + LOG_MAX_ENTRIES - mLogCount + Index) % LOG_MAX_ENTRIES;
  return &mLogEntries[Pos];
}

/**
  Clear all log entries.
**/
VOID
LogClear (
  VOID
  )
{
  mLogHead  = 0;
  mLogCount = 0;
}
