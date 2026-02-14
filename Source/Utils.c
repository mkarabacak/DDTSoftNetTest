/** @file
  Utility functions.
  String formatting, timer helpers, conversion utilities.
**/

#include <DDTSoftNetTest.h>

/**
  Format a MAC address into a CHAR16 string.

  @param[in]   MacAddr  Pointer to the MAC address (6 bytes).
  @param[out]  OutStr   Output buffer (at least 18 CHAR16).
**/
VOID
UtilFormatMac (
  IN  CONST UINT8   *MacAddr,
  OUT CHAR16        *OutStr
  )
{
  UnicodeSPrint (
    OutStr,
    18 * sizeof (CHAR16),
    L"%02X:%02X:%02X:%02X:%02X:%02X",
    MacAddr[0], MacAddr[1], MacAddr[2],
    MacAddr[3], MacAddr[4], MacAddr[5]
    );
}

/**
  Format an IPv4 address into a CHAR16 string.

  @param[in]   Ip      Pointer to the IPv4 address (4 bytes).
  @param[out]  OutStr  Output buffer (at least 16 CHAR16).
**/
VOID
UtilFormatIpv4 (
  IN  CONST UINT8   *Ip,
  OUT CHAR16        *OutStr
  )
{
  UnicodeSPrint (
    OutStr,
    16 * sizeof (CHAR16),
    L"%d.%d.%d.%d",
    Ip[0], Ip[1], Ip[2], Ip[3]
    );
}

/**
  Stall for the specified number of milliseconds.

  @param[in]  Ms  Milliseconds to wait.
**/
VOID
UtilStallMs (
  IN UINTN  Ms
  )
{
  gBS->Stall (Ms * 1000);
}

/**
  Get a timestamp from the UEFI runtime clock.

  @return  Timestamp in seconds since midnight, or 0 on failure.
**/
UINT64
UtilGetTimestamp (
  VOID
  )
{
  EFI_STATUS  Status;
  EFI_TIME    Time;

  Status = gRT->GetTime (&Time, NULL);
  if (EFI_ERROR (Status)) {
    return 0;
  }

  return (UINT64)Time.Hour * 3600 +
         (UINT64)Time.Minute * 60 +
         (UINT64)Time.Second;
}

/**
  Convert an ASCII string to a Unicode (CHAR16) string.

  @param[in]   Ascii    Source ASCII string.
  @param[out]  Unicode  Destination CHAR16 buffer.
  @param[in]   MaxLen   Maximum characters to copy (including NUL).
**/
VOID
UtilAsciiToUnicode (
  IN  CONST CHAR8   *Ascii,
  OUT CHAR16        *Unicode,
  IN  UINTN         MaxLen
  )
{
  UINTN  I;

  if (Ascii == NULL || Unicode == NULL || MaxLen == 0) {
    return;
  }

  for (I = 0; I < MaxLen - 1 && Ascii[I] != '\0'; I++) {
    Unicode[I] = (CHAR16)Ascii[I];
  }

  Unicode[I] = L'\0';
}

/**
  Safe string copy for CHAR16 strings.

  @param[out]  Dest    Destination buffer.
  @param[in]   Src     Source string.
  @param[in]   MaxLen  Maximum characters including NUL.
**/
VOID
UtilSafeStrCpy (
  OUT CHAR16        *Dest,
  IN  CONST CHAR16  *Src,
  IN  UINTN         MaxLen
  )
{
  UINTN  I;

  if (Dest == NULL || Src == NULL || MaxLen == 0) {
    return;
  }

  for (I = 0; I < MaxLen - 1 && Src[I] != L'\0'; I++) {
    Dest[I] = Src[I];
  }

  Dest[I] = L'\0';
}
