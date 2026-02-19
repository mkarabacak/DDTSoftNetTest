/** @file
  Logger - Circular buffer log system.
  Records test execution, companion events, errors/warnings.
**/

#ifndef LOGGER_H_
#define LOGGER_H_

#include <Uefi.h>

//
// Severity levels
//
typedef enum {
  LOG_DEBUG,
  LOG_INFO,
  LOG_WARNING,
  LOG_ERROR,
  LOG_SUCCESS
} LOG_LEVEL;

//
// Single log entry
//
typedef struct {
  LOG_LEVEL  Level;
  UINT64     Timestamp;
  CHAR16     Message[128];
} LOG_ENTRY;

#define LOG_MAX_ENTRIES  128

/**
  Initialize the log system. Clears the buffer.
**/
VOID
LogInit (
  VOID
  );

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
  );

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
  );

/**
  Get the total number of log entries.

  @return  Number of entries currently stored.
**/
UINTN
LogGetCount (
  VOID
  );

/**
  Get a log entry by index.

  @param[in]  Index  Entry index (0 = oldest).

  @return  Pointer to the log entry, or NULL if out of range.
**/
LOG_ENTRY *
LogGetEntry (
  IN UINTN  Index
  );

/**
  Clear all log entries.
**/
VOID
LogClear (
  VOID
  );

#endif // LOGGER_H_
