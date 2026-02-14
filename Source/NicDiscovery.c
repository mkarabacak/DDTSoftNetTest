/** @file
  NIC discovery and enumeration.
  Discovers NICs via SimpleNetwork Protocol, gets IP config, checks upper-layer protocols.
**/

#include <DDTSoftNetTest.h>
#include <SystemInfo.h>

//
// Forward declarations
//
STATIC
VOID
GetNicName (
  IN  EFI_HANDLE  Handle,
  OUT CHAR16      *Name,
  IN  UINTN       NameSize
  );

STATIC
BOOLEAN
HasProtocol (
  IN EFI_HANDLE  Handle,
  IN EFI_GUID    *Protocol
  );

STATIC
VOID
CheckUpperLayerProtocols (
  IN  EFI_HANDLE  Handle,
  OUT NIC_INFO    *Nic
  );

STATIC
VOID
GetIpConfig (
  IN  EFI_HANDLE  Handle,
  OUT NIC_INFO    *Nic
  );

/**
  Discover all NICs in the system via EFI_SIMPLE_NETWORK_PROTOCOL.

  @param[out]     Nics   Array to receive NIC information.
  @param[in,out]  Count  On input, max entries. On output, actual count.

  @retval EFI_SUCCESS           NICs discovered successfully.
  @retval EFI_INVALID_PARAMETER Nics or Count is NULL.
  @retval EFI_NOT_FOUND         No NICs found.
**/
EFI_STATUS
DiscoverNics (
  OUT NIC_INFO  *Nics,
  IN OUT UINTN  *Count
  )
{
  EFI_STATUS                    Status;
  EFI_HANDLE                    *HandleBuffer;
  UINTN                         HandleCount;
  UINTN                         Index;
  UINTN                         MaxNics;
  EFI_SIMPLE_NETWORK_PROTOCOL   *Snp;
  EFI_DEVICE_PATH_PROTOCOL      *DevPath;
  CHAR16                        *DevPathStr;

  if (Nics == NULL || Count == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  MaxNics = *Count;
  *Count = 0;

  //
  // Find all SNP handles
  //
  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiSimpleNetworkProtocolGuid,
                  NULL,
                  &HandleCount,
                  &HandleBuffer
                  );
  if (EFI_ERROR (Status)) {
    return EFI_NOT_FOUND;
  }

  for (Index = 0; Index < HandleCount && *Count < MaxNics; Index++) {
    //
    // Open SNP on this handle
    //
    Status = gBS->HandleProtocol (
                    HandleBuffer[Index],
                    &gEfiSimpleNetworkProtocolGuid,
                    (VOID **)&Snp
                    );
    if (EFI_ERROR (Status) || Snp == NULL) {
      continue;
    }

    ZeroMem (&Nics[*Count], sizeof (NIC_INFO));

    Nics[*Count].Index  = *Count;
    Nics[*Count].Handle = HandleBuffer[Index];
    Nics[*Count].Snp    = Snp;

    //
    // Read SNP Mode data
    //
    if (Snp->Mode != NULL) {
      CopyMem (&Nics[*Count].CurrentMac, &Snp->Mode->CurrentAddress, sizeof (EFI_MAC_ADDRESS));
      CopyMem (&Nics[*Count].PermanentMac, &Snp->Mode->PermanentAddress, sizeof (EFI_MAC_ADDRESS));
      Nics[*Count].IfType              = (UINT8)Snp->Mode->IfType;
      Nics[*Count].State               = Snp->Mode->State;
      Nics[*Count].MediaPresent        = Snp->Mode->MediaPresent;
      Nics[*Count].MediaDetectSupported = Snp->Mode->MediaPresentSupported;
      Nics[*Count].MacChangeable       = Snp->Mode->MacAddressChangeable;
      Nics[*Count].MultipleTxSupported = Snp->Mode->MultipleTxSupported;
      Nics[*Count].MaxPacketSize       = Snp->Mode->MaxPacketSize;
      Nics[*Count].NvRamSize           = Snp->Mode->NvRamSize;
      Nics[*Count].MediaHeaderSize     = Snp->Mode->MediaHeaderSize;
      Nics[*Count].ReceiveFilterMask   = Snp->Mode->ReceiveFilterMask;
      Nics[*Count].MaxMCastFilterCount = Snp->Mode->MaxMCastFilterCount;
    }

    //
    // Get device name via ComponentName2
    //
    GetNicName (HandleBuffer[Index], Nics[*Count].Name, sizeof (Nics[*Count].Name) / sizeof (CHAR16));

    //
    // Get device path string
    //
    Status = gBS->HandleProtocol (
                    HandleBuffer[Index],
                    &gEfiDevicePathProtocolGuid,
                    (VOID **)&DevPath
                    );
    if (!EFI_ERROR (Status) && DevPath != NULL) {
      DevPathStr = ConvertDevicePathToText (DevPath, FALSE, FALSE);
      if (DevPathStr != NULL) {
        UtilSafeStrCpy (Nics[*Count].DevicePath, DevPathStr, 256);
        FreePool (DevPathStr);
      }
    }

    //
    // Check upper-layer protocol support
    //
    CheckUpperLayerProtocols (HandleBuffer[Index], &Nics[*Count]);

    //
    // Get IP configuration
    //
    GetIpConfig (HandleBuffer[Index], &Nics[*Count]);

    (*Count)++;
  }

  FreePool (HandleBuffer);

  return (*Count > 0) ? EFI_SUCCESS : EFI_NOT_FOUND;
}

/**
  Get NIC name via ComponentName2 protocol.
  Iterates driver handles to find one that can name this controller.

  @param[in]   Handle    The NIC handle.
  @param[out]  Name      Buffer to receive the name.
  @param[in]   NameSize  Maximum characters in Name.
**/
STATIC
VOID
GetNicName (
  IN  EFI_HANDLE  Handle,
  OUT CHAR16      *Name,
  IN  UINTN       NameSize
  )
{
  EFI_STATUS                       Status;
  EFI_COMPONENT_NAME2_PROTOCOL     *CompName2;
  CHAR16                           *ControllerName;
  EFI_HANDLE                       *DriverHandles;
  UINTN                            DriverCount;
  UINTN                            I;

  UtilSafeStrCpy (Name, L"Unknown NIC", NameSize);

  //
  // Find all handles with ComponentName2
  //
  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiComponentName2ProtocolGuid,
                  NULL,
                  &DriverCount,
                  &DriverHandles
                  );
  if (EFI_ERROR (Status)) {
    return;
  }

  for (I = 0; I < DriverCount; I++) {
    Status = gBS->HandleProtocol (
                    DriverHandles[I],
                    &gEfiComponentName2ProtocolGuid,
                    (VOID **)&CompName2
                    );
    if (EFI_ERROR (Status)) {
      continue;
    }

    //
    // Try to get the controller name from this driver
    //
    Status = CompName2->GetControllerName (
                          CompName2,
                          Handle,
                          NULL,
                          "en",
                          &ControllerName
                          );
    if (!EFI_ERROR (Status) && ControllerName != NULL) {
      UtilSafeStrCpy (Name, ControllerName, NameSize);
      FreePool (DriverHandles);
      return;
    }
  }

  FreePool (DriverHandles);
}

/**
  Check if a protocol exists on the given handle.

  @param[in]  Handle    The handle to check.
  @param[in]  Protocol  The protocol GUID to look for.

  @retval TRUE   Protocol is present.
  @retval FALSE  Protocol is not present.
**/
STATIC
BOOLEAN
HasProtocol (
  IN EFI_HANDLE  Handle,
  IN EFI_GUID    *Protocol
  )
{
  EFI_STATUS  Status;
  VOID        *Interface;

  Status = gBS->HandleProtocol (Handle, Protocol, &Interface);
  return !EFI_ERROR (Status);
}

/**
  Check which upper-layer network protocols are available on the NIC handle.
  Checks for service binding protocols indicating stack availability.

  @param[in]   Handle  The NIC handle.
  @param[out]  Nic     NIC_INFO to populate protocol flags.
**/
STATIC
VOID
CheckUpperLayerProtocols (
  IN  EFI_HANDLE  Handle,
  OUT NIC_INFO    *Nic
  )
{
  Nic->HasMnp   = HasProtocol (Handle, &gEfiManagedNetworkServiceBindingProtocolGuid);
  Nic->HasArp   = HasProtocol (Handle, &gEfiArpServiceBindingProtocolGuid);
  Nic->HasIp4   = HasProtocol (Handle, &gEfiIp4ServiceBindingProtocolGuid);
  Nic->HasIp6   = HasProtocol (Handle, &gEfiIp6ServiceBindingProtocolGuid);
  Nic->HasTcp4  = HasProtocol (Handle, &gEfiTcp4ServiceBindingProtocolGuid);
  Nic->HasUdp4  = HasProtocol (Handle, &gEfiUdp4ServiceBindingProtocolGuid);
  Nic->HasDhcp4 = HasProtocol (Handle, &gEfiDhcp4ServiceBindingProtocolGuid);
  Nic->HasDns4  = HasProtocol (Handle, &gEfiDns4ServiceBindingProtocolGuid);
  Nic->HasHttp  = HasProtocol (Handle, &gEfiHttpServiceBindingProtocolGuid);
  Nic->HasTls   = HasProtocol (Handle, &gEfiTlsServiceBindingProtocolGuid);
}

/**
  Get IPv4 configuration for the NIC via IP4Config2 protocol.

  @param[in]   Handle  The NIC handle.
  @param[out]  Nic     NIC_INFO to populate IP fields.
**/
STATIC
VOID
GetIpConfig (
  IN  EFI_HANDLE  Handle,
  OUT NIC_INFO    *Nic
  )
{
  EFI_STATUS                       Status;
  EFI_IP4_CONFIG2_PROTOCOL         *Ip4Config2;
  EFI_IP4_CONFIG2_INTERFACE_INFO   *IfInfo;
  UINTN                            DataSize;
  EFI_IPv4_ADDRESS                 *GwList;

  Nic->HasIpConfig = FALSE;

  Status = gBS->HandleProtocol (
                  Handle,
                  &gEfiIp4Config2ProtocolGuid,
                  (VOID **)&Ip4Config2
                  );
  if (EFI_ERROR (Status) || Ip4Config2 == NULL) {
    return;
  }

  //
  // Query interface info size first
  //
  DataSize = 0;
  Status = Ip4Config2->GetData (
                         Ip4Config2,
                         Ip4Config2DataTypeInterfaceInfo,
                         &DataSize,
                         NULL
                         );
  if (Status != EFI_BUFFER_TOO_SMALL || DataSize == 0) {
    return;
  }

  IfInfo = AllocatePool (DataSize);
  if (IfInfo == NULL) {
    return;
  }

  Status = Ip4Config2->GetData (
                         Ip4Config2,
                         Ip4Config2DataTypeInterfaceInfo,
                         &DataSize,
                         IfInfo
                         );
  if (!EFI_ERROR (Status)) {
    CopyMem (&Nic->Ipv4Address, &IfInfo->StationAddress, sizeof (EFI_IPv4_ADDRESS));
    CopyMem (&Nic->SubnetMask, &IfInfo->SubnetMask, sizeof (EFI_IPv4_ADDRESS));

    //
    // Check if IP is configured (non-zero address)
    //
    if (IfInfo->StationAddress.Addr[0] != 0 ||
        IfInfo->StationAddress.Addr[1] != 0 ||
        IfInfo->StationAddress.Addr[2] != 0 ||
        IfInfo->StationAddress.Addr[3] != 0) {
      Nic->HasIpConfig = TRUE;
    }
  }

  FreePool (IfInfo);

  //
  // Try to get gateway address
  //
  if (Nic->HasIpConfig) {
    DataSize = 0;
    Status = Ip4Config2->GetData (
                           Ip4Config2,
                           Ip4Config2DataTypeGateway,
                           &DataSize,
                           NULL
                           );
    if (Status == EFI_BUFFER_TOO_SMALL && DataSize >= sizeof (EFI_IPv4_ADDRESS)) {
      GwList = AllocatePool (DataSize);
      if (GwList != NULL) {
        Status = Ip4Config2->GetData (
                               Ip4Config2,
                               Ip4Config2DataTypeGateway,
                               &DataSize,
                               GwList
                               );
        if (!EFI_ERROR (Status)) {
          CopyMem (&Nic->Gateway, &GwList[0], sizeof (EFI_IPv4_ADDRESS));
        }

        FreePool (GwList);
      }
    }
  }
}
