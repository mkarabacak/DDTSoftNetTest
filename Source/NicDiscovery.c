/** @file
  NIC discovery and enumeration.
  Discovers NICs via SimpleNetwork Protocol, gets IP config, checks upper-layer protocols.
**/

#include <DDTSoftNetTest.h>
#include <SystemInfo.h>
#include <PciIds.h>
#include <Protocol/PciIo.h>

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

STATIC
VOID
GetPciInfo (
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
    // Ensure SNP is initialized before reading Mode data.
    // MediaPresent is only valid after Initialize() per UEFI spec.
    // SNP states: Stopped -> Started -> Initialized
    //
    if (Snp->Mode != NULL) {
      if (Snp->Mode->State == EfiSimpleNetworkStopped) {
        Status = Snp->Start (Snp);
        if (!EFI_ERROR (Status)) {
          Snp->Initialize (Snp, 0, 0);
        }
      } else if (Snp->Mode->State == EfiSimpleNetworkStarted) {
        Snp->Initialize (Snp, 0, 0);
      }
    }

    //
    // Force MediaPresent update via GetStatus().
    // UEFI spec: Mode->MediaPresent is NOT updated automatically.
    // GetStatus() is the ONLY way to refresh media detection state.
    // Also allow time for link negotiation after Initialize.
    //
    if (Snp->Mode != NULL && Snp->Mode->State == EfiSimpleNetworkInitialized) {
      UINT32  IntStatus;
      VOID    *RecycleBuf;
      UINTN   MediaRetry;

      //
      // Quick media check: 3 tries x 100ms = 300ms max.
      // Keeps startup fast. Auto-refresh will catch later link-up.
      //
      for (MediaRetry = 0; MediaRetry < 3; MediaRetry++) {
        IntStatus  = 0;
        RecycleBuf = NULL;
        Snp->GetStatus (Snp, &IntStatus, &RecycleBuf);

        if (Snp->Mode->MediaPresent) {
          break;  // Link is up
        }

        gBS->Stall (100000);  // 100ms
      }
    }

    //
    // Read SNP Mode data (now valid after Initialize + GetStatus)
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

    //
    // Get PCI device info (vendor, device ID, BDF)
    //
    GetPciInfo (HandleBuffer[Index], &Nics[*Count]);

    //
    // If ComponentName2 failed (Name is still "Unknown NIC"),
    // build a descriptive name from PCI info and MAC address.
    //
    if (StrCmp (Nics[*Count].Name, L"Unknown NIC") == 0) {
      if (Nics[*Count].HasPciInfo) {
        UnicodeSPrint (
          Nics[*Count].Name, sizeof (Nics[*Count].Name),
          L"%s %s [%02X:%02X.%X]",
          Nics[*Count].VendorName,
          Nics[*Count].DeviceModel,
          Nics[*Count].PciBus,
          Nics[*Count].PciDev,
          Nics[*Count].PciFunc
          );
      } else {
        //
        // No PCI info either — use MAC address
        //
        UnicodeSPrint (
          Nics[*Count].Name, sizeof (Nics[*Count].Name),
          L"NIC %02X:%02X:%02X:%02X:%02X:%02X",
          Nics[*Count].CurrentMac.Addr[0],
          Nics[*Count].CurrentMac.Addr[1],
          Nics[*Count].CurrentMac.Addr[2],
          Nics[*Count].CurrentMac.Addr[3],
          Nics[*Count].CurrentMac.Addr[4],
          Nics[*Count].CurrentMac.Addr[5]
          );
      }
    }

    (*Count)++;
  }

  FreePool (HandleBuffer);

  //
  // ========== Deduplicate SNP entries with same MAC ==========
  //
  // UEFI network stack creates multiple child handles (MNP, IP4, ARP)
  // on a single physical NIC. Each child also exposes SNP, resulting
  // in 2-4 entries with identical MAC/PCI for one physical NIC.
  //
  // Strategy: for each group of entries sharing the same MAC, keep
  // the one with the most upper-layer protocols (richest handle).
  //
  {
    UINTN  I, J;
    UINTN  FinalCount;

    for (I = 0; I < *Count; I++) {
      if (Nics[I].Handle == NULL) {
        continue;  // Already marked for removal
      }

      for (J = I + 1; J < *Count; J++) {
        if (Nics[J].Handle == NULL) {
          continue;
        }

        //
        // Compare MAC addresses (6 bytes)
        //
        if (CompareMem (Nics[I].CurrentMac.Addr, Nics[J].CurrentMac.Addr, 6) == 0) {
          //
          // Same MAC — count protocols to decide which to keep
          //
          UINTN  ProtoI, ProtoJ;

          ProtoI = (Nics[I].HasMnp ? 1 : 0) + (Nics[I].HasArp ? 1 : 0) +
                   (Nics[I].HasIp4 ? 1 : 0) + (Nics[I].HasIp6 ? 1 : 0) +
                   (Nics[I].HasTcp4 ? 1 : 0) + (Nics[I].HasUdp4 ? 1 : 0) +
                   (Nics[I].HasDhcp4 ? 1 : 0) + (Nics[I].HasDns4 ? 1 : 0) +
                   (Nics[I].HasHttp ? 1 : 0) + (Nics[I].HasTls ? 1 : 0);

          ProtoJ = (Nics[J].HasMnp ? 1 : 0) + (Nics[J].HasArp ? 1 : 0) +
                   (Nics[J].HasIp4 ? 1 : 0) + (Nics[J].HasIp6 ? 1 : 0) +
                   (Nics[J].HasTcp4 ? 1 : 0) + (Nics[J].HasUdp4 ? 1 : 0) +
                   (Nics[J].HasDhcp4 ? 1 : 0) + (Nics[J].HasDns4 ? 1 : 0) +
                   (Nics[J].HasHttp ? 1 : 0) + (Nics[J].HasTls ? 1 : 0);

          if (ProtoJ > ProtoI) {
            //
            // J is richer — remove I, keep J
            //
            Nics[I].Handle = NULL;
            break;  // I is removed, no need to check more
          } else {
            //
            // I is richer or equal — remove J
            //
            Nics[J].Handle = NULL;
          }
        }
      }
    }

    //
    // Compact array: remove NULL entries and re-index
    //
    FinalCount = 0;
    for (I = 0; I < *Count; I++) {
      if (Nics[I].Handle != NULL) {
        if (FinalCount != I) {
          CopyMem (&Nics[FinalCount], &Nics[I], sizeof (NIC_INFO));
        }
        Nics[FinalCount].Index = FinalCount;
        FinalCount++;
      }
    }
    *Count = FinalCount;
  }

  return (*Count > 0) ? EFI_SUCCESS : EFI_NOT_FOUND;
}

/**
  Discover PCI network controllers (class 0x02).
  Scans all PCI IO handles, checks for network class, reads vendor/device IDs,
  detects driver presence via OpenProtocolInformation, and tries to match
  each PCI NIC to an existing SNP NIC for MAC/media info.

  Adapted from SelfDestroySystem CollectNetworkInfo.

  @param[out]     PciNics   Array to receive PCI NIC information.
  @param[in,out]  PciCount  On input, max entries. On output, actual count.
  @param[in]      SnpNics   Array of SNP-discovered NICs for cross-reference.
  @param[in]      SnpCount  Number of SNP NICs.

  @retval EFI_SUCCESS    PCI NICs discovered.
  @retval EFI_NOT_FOUND  No PCI network controllers found.
**/
EFI_STATUS
DiscoverPciNics (
  OUT PCI_NIC_INFO  *PciNics,
  IN OUT UINTN      *PciCount,
  IN  NIC_INFO      *SnpNics,
  IN  UINTN         SnpCount
  )
{
  EFI_STATUS              Status;
  EFI_HANDLE              *Handles;
  UINTN                   HandleCount;
  UINTN                   I;
  UINTN                   MaxNics;
  EFI_PCI_IO_PROTOCOL     *PciIo;
  UINT8                   ClassCode[3];
  UINT16                  VendorId;
  UINT16                  DeviceId;
  UINTN                   Seg, Bus, Dev, Func;
  CONST CHAR16            *VendorName;
  CONST CHAR16            *DeviceName;

  if (PciNics == NULL || PciCount == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  MaxNics = *PciCount;
  *PciCount = 0;

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiPciIoProtocolGuid,
                  NULL,
                  &HandleCount,
                  &Handles
                  );
  if (EFI_ERROR (Status)) {
    return EFI_NOT_FOUND;
  }

  for (I = 0; I < HandleCount && *PciCount < MaxNics; I++) {
    Status = gBS->HandleProtocol (
                    Handles[I],
                    &gEfiPciIoProtocolGuid,
                    (VOID **)&PciIo
                    );
    if (EFI_ERROR (Status) || PciIo == NULL) {
      continue;
    }

    //
    // Read class code — only interested in Network Controllers (0x02)
    //
    Status = PciIo->Pci.Read (PciIo, EfiPciIoWidthUint8, 0x09, 3, ClassCode);
    if (EFI_ERROR (Status) || ClassCode[2] != 0x02) {
      continue;
    }

    //
    // Read Vendor/Device ID
    //
    Status = PciIo->Pci.Read (PciIo, EfiPciIoWidthUint16, 0x00, 1, &VendorId);
    if (EFI_ERROR (Status) || VendorId == 0xFFFF) {
      continue;
    }

    PciIo->Pci.Read (PciIo, EfiPciIoWidthUint16, 0x02, 1, &DeviceId);
    PciIo->GetLocation (PciIo, &Seg, &Bus, &Dev, &Func);

    //
    // Fill PCI NIC info
    //
    ZeroMem (&PciNics[*PciCount], sizeof (PCI_NIC_INFO));
    PciNics[*PciCount].Index    = *PciCount;
    PciNics[*PciCount].Handle   = Handles[I];
    PciNics[*PciCount].VendorId = VendorId;
    PciNics[*PciCount].DeviceId = DeviceId;
    PciNics[*PciCount].Bus      = (UINT8)Bus;
    PciNics[*PciCount].Dev      = (UINT8)Dev;
    PciNics[*PciCount].Func     = (UINT8)Func;

    VendorName = PciLookupVendorName (VendorId);
    UtilSafeStrCpy (PciNics[*PciCount].VendorName, VendorName, 32);

    DeviceName = PciLookupNicDeviceName (VendorId, DeviceId);
    if (DeviceName != NULL) {
      UtilSafeStrCpy (PciNics[*PciCount].DeviceModel, DeviceName, 48);
    } else {
      UnicodeSPrint (PciNics[*PciCount].DeviceModel,
                     sizeof (PciNics[*PciCount].DeviceModel),
                     L"Device %04X", DeviceId);
    }

    //
    // Check if a driver is attached (OpenProtocolInformation method from SelfDestroySystem)
    //
    {
      EFI_OPEN_PROTOCOL_INFORMATION_ENTRY  *OpenInfo;
      UINTN                                OpenInfoCount;
      UINTN                                J;

      OpenInfo      = NULL;
      OpenInfoCount = 0;
      PciNics[*PciCount].HasDriver = FALSE;

      Status = gBS->OpenProtocolInformation (
                      Handles[I],
                      &gEfiPciIoProtocolGuid,
                      &OpenInfo,
                      &OpenInfoCount
                      );
      if (!EFI_ERROR (Status) && OpenInfo != NULL) {
        for (J = 0; J < OpenInfoCount; J++) {
          if (OpenInfo[J].Attributes & EFI_OPEN_PROTOCOL_BY_DRIVER) {
            PciNics[*PciCount].HasDriver = TRUE;
            break;
          }
        }

        FreePool (OpenInfo);
      }
    }

    //
    // Try to match with an SNP NIC by PCI Bus/Dev/Func
    //
    {
      UINTN  S;

      PciNics[*PciCount].MatchedSnp = FALSE;
      for (S = 0; S < SnpCount; S++) {
        if (SnpNics[S].HasPciInfo &&
            SnpNics[S].PciBus  == (UINT8)Bus &&
            SnpNics[S].PciDev  == (UINT8)Dev &&
            SnpNics[S].PciFunc == (UINT8)Func) {
          PciNics[*PciCount].MatchedSnp    = TRUE;
          PciNics[*PciCount].SnpIndex      = S;
          PciNics[*PciCount].HasMac        = TRUE;
          PciNics[*PciCount].MediaPresent  = SnpNics[S].MediaPresent;
          CopyMem (PciNics[*PciCount].MacAddress,
                   SnpNics[S].CurrentMac.Addr, 6);
          break;
        }
      }

      //
      // If no BDF match, try matching by PCI VendorId:DeviceId
      // (some platforms have different BDF for SNP child vs PCI parent)
      //
      if (!PciNics[*PciCount].MatchedSnp) {
        for (S = 0; S < SnpCount; S++) {
          if (SnpNics[S].HasPciInfo &&
              SnpNics[S].PciVendorId == VendorId &&
              SnpNics[S].PciDeviceId == DeviceId) {
            PciNics[*PciCount].MatchedSnp    = TRUE;
            PciNics[*PciCount].SnpIndex      = S;
            PciNics[*PciCount].HasMac        = TRUE;
            PciNics[*PciCount].MediaPresent  = SnpNics[S].MediaPresent;
            CopyMem (PciNics[*PciCount].MacAddress,
                     SnpNics[S].CurrentMac.Addr, 6);
            break;
          }
        }
      }

      //
      // If still no match, try to find SNP child on this PCI device's path
      // (SelfDestroySystem approach: compare device path prefix)
      //
      if (!PciNics[*PciCount].MatchedSnp && !PciNics[*PciCount].HasMac) {
        EFI_DEVICE_PATH_PROTOCOL  *PciDevPath;

        Status = gBS->HandleProtocol (
                        Handles[I],
                        &gEfiDevicePathProtocolGuid,
                        (VOID **)&PciDevPath
                        );
        if (!EFI_ERROR (Status) && PciDevPath != NULL) {
          UINTN       PciPathSize;
          EFI_HANDLE  *SnpHandles;
          UINTN       SnpHandleCount;

          PciPathSize = GetDevicePathSize (PciDevPath) - sizeof (EFI_DEVICE_PATH_PROTOCOL);

          Status = gBS->LocateHandleBuffer (
                          ByProtocol,
                          &gEfiSimpleNetworkProtocolGuid,
                          NULL,
                          &SnpHandleCount,
                          &SnpHandles
                          );
          if (!EFI_ERROR (Status)) {
            UINTN  K;

            for (K = 0; K < SnpHandleCount; K++) {
              EFI_DEVICE_PATH_PROTOCOL      *SnpPath;
              EFI_SIMPLE_NETWORK_PROTOCOL   *ChildSnp;

              Status = gBS->HandleProtocol (
                              SnpHandles[K],
                              &gEfiDevicePathProtocolGuid,
                              (VOID **)&SnpPath
                              );
              if (EFI_ERROR (Status) || SnpPath == NULL) {
                continue;
              }

              if (PciPathSize > 0 &&
                  CompareMem (PciDevPath, SnpPath, PciPathSize) == 0) {
                Status = gBS->HandleProtocol (
                                SnpHandles[K],
                                &gEfiSimpleNetworkProtocolGuid,
                                (VOID **)&ChildSnp
                                );
                if (!EFI_ERROR (Status) && ChildSnp != NULL &&
                    ChildSnp->Mode != NULL) {
                  CopyMem (PciNics[*PciCount].MacAddress,
                           ChildSnp->Mode->CurrentAddress.Addr, 6);
                  PciNics[*PciCount].HasMac       = TRUE;
                  PciNics[*PciCount].MediaPresent  = ChildSnp->Mode->MediaPresent;
                }

                break;
              }
            }

            FreePool (SnpHandles);
          }
        }
      }
    }

    (*PciCount)++;
  }

  FreePool (Handles);
  return (*PciCount > 0) ? EFI_SUCCESS : EFI_NOT_FOUND;
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
  Get PCI device information for a NIC handle.
  Uses LocateDevicePath to find the parent PCI IO handle, then reads config space.

  @param[in]   Handle  The NIC handle.
  @param[out]  Nic     NIC_INFO to populate PCI fields.
**/
STATIC
VOID
GetPciInfo (
  IN  EFI_HANDLE  Handle,
  OUT NIC_INFO    *Nic
  )
{
  EFI_STATUS                Status;
  EFI_DEVICE_PATH_PROTOCOL  *DevPath;
  EFI_DEVICE_PATH_PROTOCOL  *DevPathCopy;
  EFI_HANDLE                PciHandle;
  EFI_PCI_IO_PROTOCOL       *PciIo;
  UINTN                     Seg, Bus, Dev, Func;
  UINT16                    VendorId;
  UINT16                    DeviceId;
  UINT16                    SubVendorId;
  UINT16                    SubDeviceId;
  UINT8                     ClassCode[3];
  CONST CHAR16              *VendorName;
  CONST CHAR16              *DeviceName;

  Nic->HasPciInfo = FALSE;

  //
  // Get device path from the NIC handle
  //
  Status = gBS->HandleProtocol (
                  Handle,
                  &gEfiDevicePathProtocolGuid,
                  (VOID **)&DevPath
                  );
  if (EFI_ERROR (Status) || DevPath == NULL) {
    return;
  }

  //
  // LocateDevicePath modifies the path pointer, so duplicate it
  //
  DevPathCopy = DuplicateDevicePath (DevPath);
  if (DevPathCopy == NULL) {
    return;
  }

  //
  // Walk up the device path to find the nearest handle with PCI IO
  //
  DevPath = DevPathCopy;
  Status = gBS->LocateDevicePath (
                  &gEfiPciIoProtocolGuid,
                  &DevPath,
                  &PciHandle
                  );
  FreePool (DevPathCopy);

  if (EFI_ERROR (Status)) {
    return;
  }

  //
  // Open PCI IO on the parent handle
  //
  Status = gBS->HandleProtocol (
                  PciHandle,
                  &gEfiPciIoProtocolGuid,
                  (VOID **)&PciIo
                  );
  if (EFI_ERROR (Status) || PciIo == NULL) {
    return;
  }

  //
  // Read PCI location
  //
  Status = PciIo->GetLocation (PciIo, &Seg, &Bus, &Dev, &Func);
  if (EFI_ERROR (Status)) {
    return;
  }

  //
  // Read config space identifiers
  //
  Status = PciIo->Pci.Read (PciIo, EfiPciIoWidthUint16, 0x00, 1, &VendorId);
  if (EFI_ERROR (Status) || VendorId == 0xFFFF) {
    return;
  }

  PciIo->Pci.Read (PciIo, EfiPciIoWidthUint16, 0x02, 1, &DeviceId);
  PciIo->Pci.Read (PciIo, EfiPciIoWidthUint16, 0x2C, 1, &SubVendorId);
  PciIo->Pci.Read (PciIo, EfiPciIoWidthUint16, 0x2E, 1, &SubDeviceId);
  PciIo->Pci.Read (PciIo, EfiPciIoWidthUint8,  0x09, 3, ClassCode);

  //
  // Fill NIC_INFO PCI fields
  //
  Nic->PciVendorId       = VendorId;
  Nic->PciDeviceId       = DeviceId;
  Nic->PciSubsysVendorId = SubVendorId;
  Nic->PciSubsysDeviceId = SubDeviceId;
  Nic->PciBus            = (UINT8)Bus;
  Nic->PciDev            = (UINT8)Dev;
  Nic->PciFunc           = (UINT8)Func;
  Nic->PciClassCode      = ClassCode[2];

  //
  // Lookup vendor name
  //
  VendorName = PciLookupVendorName (VendorId);
  UtilSafeStrCpy (Nic->VendorName, VendorName, 32);

  //
  // Lookup device model name
  //
  DeviceName = PciLookupNicDeviceName (VendorId, DeviceId);
  if (DeviceName != NULL) {
    UtilSafeStrCpy (Nic->DeviceModel, DeviceName, 48);
  } else {
    UnicodeSPrint (Nic->DeviceModel, sizeof (Nic->DeviceModel),
                   L"Device %04X", DeviceId);
  }

  Nic->HasPciInfo = TRUE;
}

/**
  Configure a static IPv4 address on the NIC via IP4Config2 protocol.
  Sets policy to Static, then writes manual address and gateway.

  @param[in]  Handle   The NIC handle.
  @param[in]  Ip       Static IPv4 address to set.
  @param[in]  Mask     Subnet mask.
  @param[in]  Gateway  Default gateway.

  @retval EFI_SUCCESS    IP configured successfully.
  @retval other          Configuration failed.
**/
STATIC
EFI_STATUS
ConfigureStaticIp (
  IN EFI_HANDLE            Handle,
  IN CONST EFI_IPv4_ADDRESS  *Ip,
  IN CONST EFI_IPv4_ADDRESS  *Mask,
  IN CONST EFI_IPv4_ADDRESS  *Gateway
  )
{
  EFI_STATUS                        Status;
  EFI_IP4_CONFIG2_PROTOCOL          *Ip4Config2;
  EFI_IP4_CONFIG2_POLICY            Policy;
  EFI_IP4_CONFIG2_MANUAL_ADDRESS    ManualAddr;
  UINTN                             DataSize;

  Status = gBS->HandleProtocol (
                  Handle,
                  &gEfiIp4Config2ProtocolGuid,
                  (VOID **)&Ip4Config2
                  );
  if (EFI_ERROR (Status) || Ip4Config2 == NULL) {
    return Status;
  }

  //
  // Step 1: Set policy to Static
  //
  Policy = Ip4Config2PolicyStatic;
  DataSize = sizeof (EFI_IP4_CONFIG2_POLICY);
  Status = Ip4Config2->SetData (
                         Ip4Config2,
                         Ip4Config2DataTypePolicy,
                         DataSize,
                         &Policy
                         );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Small delay for policy change to take effect
  //
  gBS->Stall (100000);  // 100ms

  //
  // Step 2: Set manual address (IP + subnet mask)
  //
  CopyMem (&ManualAddr.Address, Ip, sizeof (EFI_IPv4_ADDRESS));
  CopyMem (&ManualAddr.SubnetMask, Mask, sizeof (EFI_IPv4_ADDRESS));

  DataSize = sizeof (EFI_IP4_CONFIG2_MANUAL_ADDRESS);
  Status = Ip4Config2->SetData (
                         Ip4Config2,
                         Ip4Config2DataTypeManualAddress,
                         DataSize,
                         &ManualAddr
                         );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Small delay for address to be applied
  //
  gBS->Stall (100000);  // 100ms

  //
  // Step 3: Set gateway
  //
  if (Gateway->Addr[0] != 0 || Gateway->Addr[1] != 0 ||
      Gateway->Addr[2] != 0 || Gateway->Addr[3] != 0) {
    EFI_IPv4_ADDRESS  GwAddr;

    CopyMem (&GwAddr, Gateway, sizeof (EFI_IPv4_ADDRESS));
    DataSize = sizeof (EFI_IPv4_ADDRESS);
    Ip4Config2->SetData (
                  Ip4Config2,
                  Ip4Config2DataTypeGateway,
                  DataSize,
                  &GwAddr
                  );
    //
    // Gateway failure is non-fatal
    //
  }

  //
  // Allow IP stack to settle
  //
  gBS->Stall (200000);  // 200ms

  return EFI_SUCCESS;
}

/**
  Get IPv4 configuration for the NIC via IP4Config2 protocol.
  If no IP is configured, automatically assigns a static IP
  based on DEFAULT_LOCAL_IP.

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
  // If no IP configured, assign static IP automatically
  //
  if (!Nic->HasIpConfig) {
    EFI_IPv4_ADDRESS  DefaultIp   = DEFAULT_LOCAL_IP;
    EFI_IPv4_ADDRESS  DefaultMask = DEFAULT_SUBNET_MASK;
    EFI_IPv4_ADDRESS  DefaultGw   = DEFAULT_GATEWAY;

    Status = ConfigureStaticIp (Handle, &DefaultIp, &DefaultMask, &DefaultGw);
    if (!EFI_ERROR (Status)) {
      //
      // Re-read the IP config after setting it
      //
      DataSize = 0;
      Status = Ip4Config2->GetData (
                             Ip4Config2,
                             Ip4Config2DataTypeInterfaceInfo,
                             &DataSize,
                             NULL
                             );
      if (Status == EFI_BUFFER_TOO_SMALL && DataSize > 0) {
        IfInfo = AllocatePool (DataSize);
        if (IfInfo != NULL) {
          Status = Ip4Config2->GetData (
                                 Ip4Config2,
                                 Ip4Config2DataTypeInterfaceInfo,
                                 &DataSize,
                                 IfInfo
                                 );
          if (!EFI_ERROR (Status)) {
            CopyMem (&Nic->Ipv4Address, &IfInfo->StationAddress, sizeof (EFI_IPv4_ADDRESS));
            CopyMem (&Nic->SubnetMask, &IfInfo->SubnetMask, sizeof (EFI_IPv4_ADDRESS));

            if (IfInfo->StationAddress.Addr[0] != 0 ||
                IfInfo->StationAddress.Addr[1] != 0 ||
                IfInfo->StationAddress.Addr[2] != 0 ||
                IfInfo->StationAddress.Addr[3] != 0) {
              Nic->HasIpConfig = TRUE;
            }
          }

          FreePool (IfInfo);
        }
      }

      //
      // If re-read didn't work, just fill from defaults
      //
      if (!Nic->HasIpConfig) {
        CopyMem (&Nic->Ipv4Address, &DefaultIp, sizeof (EFI_IPv4_ADDRESS));
        CopyMem (&Nic->SubnetMask, &DefaultMask, sizeof (EFI_IPv4_ADDRESS));
        CopyMem (&Nic->Gateway, &DefaultGw, sizeof (EFI_IPv4_ADDRESS));
        Nic->HasIpConfig = TRUE;
      }
    }
  }

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

/**
  Refresh media status for a single NIC via GetStatus().
  Updates MediaPresent in the NIC_INFO structure.
  Call this periodically for real-time cable plug/unplug detection.

  Uses double-read debouncing: two GetStatus() calls 10ms apart must
  agree before the displayed state changes. This prevents flickering
  caused by SNP drivers that return inconsistent MediaPresent values
  on rapid polling.

  @param[in,out]  Nic  NIC info structure with valid Snp pointer.

  @return Current MediaPresent state (TRUE = cable connected).
**/
BOOLEAN
NicRefreshMedia (
  IN OUT NIC_INFO  *Nic
  )
{
  UINT32   IntStatus;
  VOID     *RecycleBuf;
  BOOLEAN  Reading1;
  BOOLEAN  Reading2;

  if (Nic == NULL || Nic->Snp == NULL || Nic->Snp->Mode == NULL) {
    return FALSE;
  }

  if (Nic->Snp->Mode->State != EfiSimpleNetworkInitialized) {
    return Nic->MediaPresent;
  }

  //
  // Aggressive media detection: try up to 10 times with 100ms gaps.
  // If ANY read returns TRUE, set MediaPresent=TRUE immediately.
  // The Intel I219-LM SNP driver can be slow to update MediaPresent
  // after Initialize+GetStatus; a single 3ms debounce is not enough.
  //
  {
    UINTN  Retry;
    UINTN  MaxRetries;

    //
    // If already TRUE, just do a quick 2-read debounce
    //
    if (Nic->MediaPresent) {
      MaxRetries = 2;
    } else {
      MaxRetries = 10;
    }

    for (Retry = 0; Retry < MaxRetries; Retry++) {
      IntStatus  = 0;
      RecycleBuf = NULL;
      Nic->Snp->GetStatus (Nic->Snp, &IntStatus, &RecycleBuf);

      if (Nic->Snp->Mode->MediaPresent) {
        Nic->MediaPresent = TRUE;
        return TRUE;
      }

      gBS->Stall (100000);  // 100ms
    }

    //
    // All reads returned FALSE — cable truly disconnected
    //
    Nic->MediaPresent = FALSE;
  }

  return Nic->MediaPresent;
}
