# DDTSoftNetTest — EFI Network Test & OSI Layer Analyzer
## Tam Proje Spesifikasyonu (Claude Code Handoff Dokümanı)

---

## 1. PROJE GENEL BAKIŞ

### Amaç
EDK2 ile UEFI ortamında çalışan, OSI katmanlarını (Layer 1-7) test eden ve analiz eden bare-metal network diagnostik aracı. OS bağımsız, doğrudan firmware seviyesinde NIC erişimi sağlar. Karşı tarafta Linux üzerinde çalışan DDTSoft Companion uygulaması ile koordineli test yapar.

### Neden EFI Seviyesi
- OS kernel/driver katmanı araya girmiyor
- Raw donanım erişimi (SNP ile doğrudan NIC)
- Layer 1-2 testleri OS altında neredeyse imkansız, EFI'de doğrudan yapılabilir
- Firmware seviyesinde network stack doğrulama
- PXE boot öncesi diagnostik
- Bare-metal test senaryoları

### Branding
- Uygulama adı: **DDTSoft**
- Tüm ekranlarda "DDTSoft" logosu/ismi görünecek
- EFI App: DDTSoft EFI Network Test
- Companion: DDTSoft Test Companion
- Paket/Proje adı: DDTSoftNetTest

---

## 2. DİZİN YAPISI & BUILD SİSTEMİ

### Genel Dizin Yapısı — edk2 Dışında, PACKAGES_PATH ile

```
~/
├── edk2/                           ← Vanilla EDK2 (dokunulmaz)
│   ├── MdePkg/
│   ├── MdeModulePkg/
│   ├── NetworkPkg/
│   ├── ShellPkg/
│   ├── OvmfPkg/
│   ├── BaseTools/
│   └── ...
│
├── DDTSoft/                        ← Senin şirket/kişisel workspace'in
│   ├── DDTSoftNetTest/             ← BU PROJE
│   │   ├── DDTSoftNetTest.inf      # Module build tanımı
│   │   ├── DDTSoftNetTest.dsc      # Platform build tanımı
│   │   ├── DDTSoftNetTest.dec      # Package declaration
│   │   ├── Include/
│   │   │   ├── DDTSoftNetTest.h
│   │   │   ├── OsiLayers.h
│   │   │   ├── PacketDefs.h
│   │   │   ├── TestCases.h
│   │   │   ├── SystemInfo.h
│   │   │   ├── PciIds.h
│   │   │   └── UiRenderer.h
│   │   ├── Source/
│   │   │   ├── Main.c
│   │   │   ├── UiRenderer.c
│   │   │   ├── SystemInfo.c
│   │   │   ├── SmbiosParser.c
│   │   │   ├── PciEnumerator.c
│   │   │   ├── DriverEnumerator.c
│   │   │   ├── NicDiscovery.c
│   │   │   ├── CompanionLink.c
│   │   │   ├── TestRunner.c
│   │   │   ├── TestRegistry.c
│   │   │   ├── QuickScan.c
│   │   │   ├── Layer1Physical.c
│   │   │   ├── Layer2DataLink.c
│   │   │   ├── Layer3Network.c
│   │   │   ├── Layer4Transport.c
│   │   │   ├── Layer7Application.c
│   │   │   ├── StressTest.c
│   │   │   ├── PacketBuilder.c
│   │   │   ├── PacketParser.c
│   │   │   ├── OsiAnalyzer.c
│   │   │   ├── ReportExporter.c
│   │   │   └── Utils.c
│   │   ├── Companion/
│   │   │   ├── companion.py
│   │   │   ├── services/
│   │   │   │   ├── control_server.py
│   │   │   │   ├── link_control.py
│   │   │   │   ├── frame_generator.py
│   │   │   │   ├── arp_responder.py
│   │   │   │   ├── icmp_handler.py
│   │   │   │   ├── tcp_listener.py
│   │   │   │   ├── udp_echo.py
│   │   │   │   ├── dhcp_manager.py
│   │   │   │   ├── dns_manager.py
│   │   │   │   └── http_server.py
│   │   │   ├── capture/
│   │   │   │   ├── packet_capture.py
│   │   │   │   └── validator.py
│   │   │   ├── config/
│   │   │   │   └── default.conf
│   │   │   └── requirements.txt
│   │   ├── Scripts/
│   │   │   ├── build.sh
│   │   │   ├── build_and_run.sh
│   │   │   └── setup_tap.sh
│   │   ├── PROJECT_SPEC.md
│   │   └── CLAUDE.md
│   │
│   ├── DigerProjeA/                ← Senin diğer projelerin
│   ├── DigerProjeB/
│   └── ...
│
└── efi_disk/                       ← QEMU test disk
    └── DDTSoftNetTest.efi
```

### PACKAGES_PATH Mantığı

EDK2 `PACKAGES_PATH` environment variable'ı ile edk2 dışındaki dizinleri arayabilir. Bu sayede kendi package'larını edk2'den ayrı tutarsın:

```bash
# PACKAGES_PATH ayarı:
export PACKAGES_PATH=$HOME/edk2:$HOME/DDTSoft
```

Build sistemi sırasıyla şu dizinlerde arar:
1. `~/edk2/` — MdePkg, NetworkPkg vs. burada bulur
2. `~/DDTSoft/` — DDTSoftNetTest burada bulur

### Ortam Kurulumu

```bash
# 1. Gerekli paketler
sudo apt update
sudo apt install -y build-essential uuid-dev iasl git nasm python3 python3-pip qemu-system-x86 ovmf

# 2. EDK2 klonla (henüz yoksa)
cd ~
git clone https://github.com/tianocore/edk2.git
cd edk2
git submodule update --init --recursive
make -C BaseTools

# 3. DDTSoftNetTest dizinini oluştur
mkdir -p ~/DDTSoft/DDTSoftNetTest

# 4. Proje dosyalarını yerleştir (Claude Code oluşturacak)
```

### Build Komutları

```bash
# Ortamı hazırla (HER TERMİNAL AÇILIŞINDA)
cd ~/edk2
source edksetup.sh
export PACKAGES_PATH=$HOME/edk2:$HOME/DDTSoft

# DEBUG build
build -a X64 -t GCC5 -p DDTSoftNetTest/DDTSoftNetTest.dsc -b DEBUG

# RELEASE build
build -a X64 -t GCC5 -p DDTSoftNetTest/DDTSoftNetTest.dsc -b RELEASE

# Çıktı dosyası:
# ~/edk2/Build/DDTSoftNetTest/DEBUG_GCC5/X64/DDTSoftNetTest.efi
```

### DDTSoftNetTest.dsc — Platform Description File

```ini
[Defines]
  PLATFORM_NAME                  = DDTSoftNetTest
  PLATFORM_GUID                  = B1C2D3E4-F5A6-7890-BCDE-F12345678901
  PLATFORM_VERSION               = 1.0
  DSC_SPECIFICATION              = 0x00010006
  OUTPUT_DIRECTORY               = Build/DDTSoftNetTest
  SUPPORTED_ARCHITECTURES        = X64|IA32
  BUILD_TARGETS                  = DEBUG|RELEASE
  SKUID_IDENTIFIER               = DEFAULT

[LibraryClasses]
  UefiApplicationEntryPoint|MdePkg/Library/UefiApplicationEntryPoint/UefiApplicationEntryPoint.inf
  UefiLib|MdePkg/Library/UefiLib/UefiLib.inf
  UefiBootServicesTableLib|MdePkg/Library/UefiBootServicesTableLib/UefiBootServicesTableLib.inf
  UefiRuntimeServicesTableLib|MdePkg/Library/UefiRuntimeServicesTableLib/UefiRuntimeServicesTableLib.inf
  BaseLib|MdePkg/Library/BaseLib/BaseLib.inf
  BaseMemoryLib|MdePkg/Library/BaseMemoryLib/BaseMemoryLib.inf
  MemoryAllocationLib|MdePkg/Library/UefiMemoryAllocationLib/UefiMemoryAllocationLib.inf
  PrintLib|MdePkg/Library/BasePrintLib/BasePrintLib.inf
  DebugLib|MdePkg/Library/BaseDebugLibNull/BaseDebugLibNull.inf
  DebugPrintErrorLevelLib|MdePkg/Library/BaseDebugPrintErrorLevelLib/BaseDebugPrintErrorLevelLib.inf
  PcdLib|MdePkg/Library/BasePcdLibNull/BasePcdLibNull.inf
  DevicePathLib|MdePkg/Library/UefiDevicePathLib/UefiDevicePathLib.inf
  RegisterFilterLib|MdePkg/Library/RegisterFilterLibNull/RegisterFilterLibNull.inf
  NetLib|NetworkPkg/Library/DxeNetLib/DxeNetLib.inf
  FileHandleLib|MdePkg/Library/UefiFileHandleLib/UefiFileHandleLib.inf
  SortLib|MdeModulePkg/Library/UefiSortLib/UefiSortLib.inf
  HiiLib|MdeModulePkg/Library/UefiHiiLib/UefiHiiLib.inf
  UefiHiiServicesLib|MdeModulePkg/Library/UefiHiiServicesLib/UefiHiiServicesLib.inf
  ShellLib|ShellPkg/Library/UefiShellLib/UefiShellLib.inf
  ShellCommandLib|ShellPkg/Library/UefiShellCommandLib/UefiShellCommandLib.inf
  HandleParsingLib|ShellPkg/Library/UefiHandleParsingLib/UefiHandleParsingLib.inf
  ShellCEntryLib|ShellPkg/Library/UefiShellCEntryLib/UefiShellCEntryLib.inf

[Components]
  DDTSoftNetTest/DDTSoftNetTest.inf
```

### DDTSoftNetTest.dec — Package Declaration File

```ini
[Defines]
  DEC_SPECIFICATION              = 0x00010006
  PACKAGE_NAME                   = DDTSoftNetTest
  PACKAGE_GUID                   = C2D3E4F5-A6B7-8901-CDEF-234567890ABC
  PACKAGE_VERSION                = 1.0

[Includes]
  Include

[Guids]

[Protocols]

[PcdsFixedAtBuild]
```

### DDTSoftNetTest.inf — Module Build File

```ini
[Defines]
  INF_VERSION                    = 0x00010006
  BASE_NAME                      = DDTSoftNetTest
  FILE_GUID                      = A1B2C3D4-E5F6-7890-ABCD-EF1234567890
  MODULE_TYPE                    = UEFI_APPLICATION
  VERSION_STRING                 = 1.0
  ENTRY_POINT                    = DDTSoftNetTestMain

[Sources]
  Source/Main.c
  Source/UiRenderer.c
  Source/SystemInfo.c
  Source/SmbiosParser.c
  Source/PciEnumerator.c
  Source/DriverEnumerator.c
  Source/NicDiscovery.c
  Source/CompanionLink.c
  Source/TestRunner.c
  Source/TestRegistry.c
  Source/QuickScan.c
  Source/Layer1Physical.c
  Source/Layer2DataLink.c
  Source/Layer3Network.c
  Source/Layer4Transport.c
  Source/Layer7Application.c
  Source/StressTest.c
  Source/PacketBuilder.c
  Source/PacketParser.c
  Source/OsiAnalyzer.c
  Source/ReportExporter.c
  Source/Utils.c

[Packages]
  MdePkg/MdePkg.dec
  MdeModulePkg/MdeModulePkg.dec
  NetworkPkg/NetworkPkg.dec
  ShellPkg/ShellPkg.dec
  DDTSoftNetTest/DDTSoftNetTest.dec

[LibraryClasses]
  UefiApplicationEntryPoint
  UefiLib
  UefiBootServicesTableLib
  UefiRuntimeServicesTableLib
  BaseLib
  BaseMemoryLib
  MemoryAllocationLib
  PrintLib
  DebugLib
  NetLib
  ShellLib
  ShellCommandLib
  FileHandleLib
  DevicePathLib
  HiiLib
  SortLib
  HandleParsingLib

[Protocols]
  gEfiSimpleNetworkProtocolGuid
  gEfiManagedNetworkServiceBindingProtocolGuid
  gEfiManagedNetworkProtocolGuid
  gEfiArpServiceBindingProtocolGuid
  gEfiArpProtocolGuid
  gEfiIp4ServiceBindingProtocolGuid
  gEfiIp4ProtocolGuid
  gEfiIp4Config2ProtocolGuid
  gEfiIp6ServiceBindingProtocolGuid
  gEfiIp6ProtocolGuid
  gEfiIp6ConfigProtocolGuid
  gEfiUdp4ServiceBindingProtocolGuid
  gEfiUdp4ProtocolGuid
  gEfiTcp4ServiceBindingProtocolGuid
  gEfiTcp4ProtocolGuid
  gEfiUdp6ServiceBindingProtocolGuid
  gEfiUdp6ProtocolGuid
  gEfiTcp6ServiceBindingProtocolGuid
  gEfiTcp6ProtocolGuid
  gEfiDhcp4ServiceBindingProtocolGuid
  gEfiDhcp4ProtocolGuid
  gEfiDns4ServiceBindingProtocolGuid
  gEfiDns4ProtocolGuid
  gEfiHttpServiceBindingProtocolGuid
  gEfiHttpProtocolGuid
  gEfiTlsServiceBindingProtocolGuid
  gEfiTlsProtocolGuid
  gEfiPciIoProtocolGuid
  gEfiLoadedImageProtocolGuid
  gEfiComponentName2ProtocolGuid
  gEfiDevicePathProtocolGuid
  gEfiAdapterInformationProtocolGuid

[Guids]
  gEfiSmbios3TableGuid
  gEfiSmbiosTableGuid
  gEfiAcpi20TableGuid
  gEfiAcpi10TableGuid
  gEfiFileInfoGuid

[BuildOptions]
  GCC:*_*_*_CC_FLAGS = -Wno-unused-variable
  MSFT:*_*_*_CC_FLAGS = /W4
```

### Build & Test Script'leri

**Scripts/build.sh:**
```bash
#!/bin/bash
set -e
cd ~/edk2
source edksetup.sh
export PACKAGES_PATH=$HOME/edk2:$HOME/DDTSoft
echo "=== Building DDTSoftNetTest ==="
build -a X64 -t GCC5 -p DDTSoftNetTest/DDTSoftNetTest.dsc -b DEBUG
echo "=== Build OK ==="
echo "Output: Build/DDTSoftNetTest/DEBUG_GCC5/X64/DDTSoftNetTest.efi"
```

**Scripts/build_and_run.sh:**
```bash
#!/bin/bash
set -e
cd ~/edk2
source edksetup.sh
export PACKAGES_PATH=$HOME/edk2:$HOME/DDTSoft
echo "=== Building DDTSoftNetTest ==="
build -a X64 -t GCC5 -p DDTSoftNetTest/DDTSoftNetTest.dsc -b DEBUG
echo "=== Copying to EFI disk ==="
mkdir -p ~/efi_disk
cp Build/DDTSoftNetTest/DEBUG_GCC5/X64/DDTSoftNetTest.efi ~/efi_disk/
echo "=== Launching QEMU ==="
qemu-system-x86_64 \
  -bios /usr/share/OVMF/OVMF_CODE.fd \
  -drive format=raw,file=fat:rw:$HOME/efi_disk \
  -net nic,model=e1000 \
  -net user \
  -m 512M \
  -nographic
# OVMF build ettiysen bunun yerine:
# -bios Build/OvmfX64/DEBUG_GCC5/FV/OVMF.fd
```

**Scripts/setup_tap.sh:**
```bash
#!/bin/bash
# Companion testi için tap interface oluştur
set -e
sudo ip tuntap add dev tap0 mode tap user $USER
sudo ip addr add 192.168.100.1/24 dev tap0
sudo ip link set tap0 up
echo "tap0 ready: 192.168.100.1/24"
echo "QEMU komutu:"
echo "qemu-system-x86_64 -bios /usr/share/OVMF/OVMF_CODE.fd \\"
echo "  -drive format=raw,file=fat:rw:\$HOME/efi_disk \\"
echo "  -netdev tap,id=net0,ifname=tap0,script=no,downscript=no \\"
echo "  -device e1000,netdev=net0 -m 512M -nographic"
```

---

## 3. MİMARİ

### Katmanlı Modüler Yapı

```
┌─────────────────────────────────────────────┐
│         Main (Shell App / Menü)             │
├─────────────────────────────────────────────┤
│           Test Runner Engine                │
├──────────┬──────────┬──────────┬────────────┤
│ Layer 1  │ Layer 2  │ Layer 3  │ Layer 4    │
│ Physical │ DataLink │ Network  │ Transport  │
├──────────┴──────────┴──────────┴────────────┤
│         Layer 5-6-7 (Session/App)           │
├─────────────────────────────────────────────┤
│  PacketBuilder/Parser  │  Utils/Logger      │
├─────────────────────────────────────────────┤
│  SystemInfo │ NicDiscovery │ UiRenderer     │
└─────────────────────────────────────────────┘
```

### Fiziksel Test Topolojisi

```
┌─────────────────────┐                    ┌─────────────────────┐
│   DUT (Device       │   CAT6 Kablo      │   Test Server       │
│   Under Test)       │◄──────────────────►│   (Karşı Taraf)     │
│   UEFI Shell        │   1-1 Direkt      │   Linux             │
│   DDTSoftNetTest    │                    │   DDTSoft Companion │
│   NIC 0 ──────────────────────────────── NIC 0                │
│   NIC 1 ──────────────────────────────── NIC 1 (opsiyonel)    │
└─────────────────────┘                    └─────────────────────┘
```

### IP Adresleme Planı

```
Test Network      : 192.168.100.0/24
DUT (EFI)         : 192.168.100.10
Companion         : 192.168.100.1
DHCP Pool         : 192.168.100.100 - .200
DNS Domain        : test.ddtsoft.local
Kontrol Kanalı    : UDP 9999
Test Portları TCP : 80, 443, 8080, 22
Test Portları UDP : 5000, 5001, 5002
```

---

## 4. EFI PROTOKOL KULLANIMI

### Kullanılan Protokoller Tablosu

| Katman | Protokol GUID | Kullanım |
|--------|--------------|----------|
| L1 | `gEfiSimpleNetworkProtocolGuid` | NIC erişim, link durumu, initialize/start/stop |
| L2 | `gEfiManagedNetworkServiceBindingProtocolGuid` | Frame gönder/al, receive filter |
| L2 | `gEfiManagedNetworkProtocolGuid` | Managed network instance |
| L2-3 | `gEfiArpServiceBindingProtocolGuid` | ARP service |
| L2-3 | `gEfiArpProtocolGuid` | ARP resolve/cache |
| L3 | `gEfiIp4ServiceBindingProtocolGuid` | IPv4 service |
| L3 | `gEfiIp4ProtocolGuid` | IP gönder/al |
| L3 | `gEfiIp4Config2ProtocolGuid` | IP konfigürasyon (adres, mask, gw, dns) |
| L3 | `gEfiIp6ServiceBindingProtocolGuid` | IPv6 service |
| L3 | `gEfiIp6ProtocolGuid` | IPv6 gönder/al |
| L3 | `gEfiIp6ConfigProtocolGuid` | IPv6 konfigürasyon |
| L4 | `gEfiTcp4ServiceBindingProtocolGuid` | TCPv4 service |
| L4 | `gEfiTcp4ProtocolGuid` | TCP bağlantı |
| L4 | `gEfiUdp4ServiceBindingProtocolGuid` | UDPv4 service |
| L4 | `gEfiUdp4ProtocolGuid` | UDP paket |
| L4 | `gEfiTcp6ServiceBindingProtocolGuid` | TCPv6 service |
| L4 | `gEfiTcp6ProtocolGuid` | TCPv6 bağlantı |
| L4 | `gEfiUdp6ServiceBindingProtocolGuid` | UDPv6 service |
| L4 | `gEfiUdp6ProtocolGuid` | UDPv6 paket |
| L5-6 | `gEfiTlsServiceBindingProtocolGuid` | TLS service |
| L5-6 | `gEfiTlsProtocolGuid` | TLS handshake |
| L7 | `gEfiDhcp4ServiceBindingProtocolGuid` | DHCP service |
| L7 | `gEfiDhcp4ProtocolGuid` | DHCP discover/offer/request/ack |
| L7 | `gEfiDns4ServiceBindingProtocolGuid` | DNS service |
| L7 | `gEfiDns4ProtocolGuid` | DNS çözümleme |
| L7 | `gEfiHttpServiceBindingProtocolGuid` | HTTP service |
| L7 | `gEfiHttpProtocolGuid` | HTTP request/response |

### Sistem Bilgi Protokolleri

| Protokol | Kullanım |
|----------|----------|
| SMBIOS Config Table (`gEfiSmbios3TableGuid`/`gEfiSmbiosTableGuid`) | BIOS, sistem, CPU, RAM |
| `gEfiPciIoProtocolGuid` | PCI cihaz enumerate |
| `gEfiLoadedImageProtocolGuid` | Yüklü image bilgisi |
| `gEfiComponentName2ProtocolGuid` | Driver isim bilgisi |
| `gEfiDevicePathProtocolGuid` | Device path string |
| `gEfiAdapterInformationProtocolGuid` | NIC ek bilgi (varsa) |
| ACPI Config Table (`gEfiAcpi20TableGuid`/`gEfiAcpi10TableGuid`) | ACPI tabloları |

---

## 5. VERİ YAPILARI

### 5.1 NIC Bilgi Yapısı

```c
#define MAX_INTERFACES  8
#define MAC_ADDRESS_LENGTH 6

typedef struct {
  UINTN                          Index;
  EFI_HANDLE                     Handle;
  EFI_SIMPLE_NETWORK_PROTOCOL    *Snp;

  // Kimlik
  EFI_MAC_ADDRESS                CurrentMac;
  EFI_MAC_ADDRESS                PermanentMac;
  UINT8                          IfType;
  CHAR16                         Name[64];
  CHAR16                         DevicePath[256];

  // Fiziksel Durum
  UINT32                         State;
  BOOLEAN                        MediaPresent;
  BOOLEAN                        MediaDetectSupported;
  BOOLEAN                        MacChangeable;
  BOOLEAN                        MultipleTxSupported;

  // Kapasite
  UINT32                         MaxPacketSize;
  UINT32                         NvRamSize;
  UINT32                         MediaHeaderSize;
  UINT32                         ReceiveFilterMask;
  UINT32                         MaxMCastFilterCount;

  // IP Bilgileri (varsa)
  BOOLEAN                        HasIpConfig;
  EFI_IPv4_ADDRESS               Ipv4Address;
  EFI_IPv4_ADDRESS               SubnetMask;
  EFI_IPv4_ADDRESS               Gateway;

  // Üst Katman Protokol Desteği
  BOOLEAN                        HasMnp;
  BOOLEAN                        HasArp;
  BOOLEAN                        HasIp4;
  BOOLEAN                        HasIp6;
  BOOLEAN                        HasTcp4;
  BOOLEAN                        HasUdp4;
  BOOLEAN                        HasDhcp4;
  BOOLEAN                        HasDns4;
  BOOLEAN                        HasHttp;
  BOOLEAN                        HasTls;
} NIC_INFO;
```

### 5.2 Sistem Bilgi Yapıları

```c
typedef struct {
  CHAR16    FirmwareVendor[128];
  UINT32    FirmwareRevision;
  UINT16    UefiSpecMajor;
  UINT16    UefiSpecMinor;
  CHAR8     BiosVendor[64];
  CHAR8     BiosVersion[64];
  CHAR8     BiosReleaseDate[32];
  UINT16    BiosMajorRelease;
  UINT16    BiosMinorRelease;
  UINT64    BiosRomSize;
} FIRMWARE_INFO;

typedef struct {
  CHAR8     Manufacturer[64];
  CHAR8     ProductName[64];
  CHAR8     Version[64];
  CHAR8     SerialNumber[64];
  EFI_GUID  SystemUuid;
  CHAR8     BoardManufacturer[64];
  CHAR8     BoardProduct[64];
  CHAR8     BoardVersion[64];
  CHAR8     BoardSerial[64];
} SYSTEM_INFO;

typedef struct {
  CHAR8     ProcessorName[128];
  UINT16    MaxSpeed;
  UINT16    CurrentSpeed;
  UINT8     CoreCount;
  UINT8     ThreadCount;
  CHAR8     SocketDesignation[32];
} CPU_INFO;

typedef struct {
  UINT8     SlotIndex;
  CHAR8     DeviceLocator[32];
  UINT32    SizeMB;
  UINT16    Speed;
  UINT16    ConfiguredSpeed;
  UINT8     MemoryType;
  CHAR8     Manufacturer[32];
  CHAR8     PartNumber[32];
  CHAR8     SerialNumber[32];
  UINT8     FormFactor;
} MEMORY_SLOT_INFO;

typedef struct {
  UINT32             TotalMemoryMB;
  UINT8              PopulatedSlots;
  UINT8              TotalSlots;
  MEMORY_SLOT_INFO   Slots[32];
} MEMORY_INFO;

typedef struct {
  UINT8     Bus;
  UINT8     Device;
  UINT8     Function;
  UINT16    VendorId;
  UINT16    DeviceId;
  UINT16    SubsysVendorId;
  UINT16    SubsysDeviceId;
  UINT8     ClassCode;
  UINT8     SubClassCode;
  UINT8     ProgInterface;
  UINT8     RevisionId;
  CHAR16    VendorName[64];
  CHAR16    ClassName[32];
  CHAR16    DevicePath[256];
  BOOLEAN   IsNetworkDevice;
} PCI_DEVICE_INFO;

typedef struct {
  EFI_HANDLE Handle;
  CHAR16    Name[128];
  CHAR16    FilePath[256];
  UINT64    ImageBase;
  UINT64    ImageSize;
  UINT32    ImageCodeType;
  BOOLEAN   IsDriver;
} DRIVER_INFO;

typedef struct {
  UINT8     AcpiRevision;
  CHAR8     OemId[7];
  UINT32    XsdtTableCount;
  BOOLEAN   HasDsdt;
  BOOLEAN   HasFadt;
  BOOLEAN   HasMadt;
  BOOLEAN   HasMcfg;
} ACPI_BASIC_INFO;
```

### 5.3 Test Yapıları

```c
typedef enum {
  OsiLayerPhysical    = 1,
  OsiLayerDataLink    = 2,
  OsiLayerNetwork     = 3,
  OsiLayerTransport   = 4,
  OsiLayerSession     = 5,
  OsiLayerPresentation= 6,
  OsiLayerApplication = 7,
  OsiLayerAll         = 0xFF
} OSI_LAYER;

typedef enum {
  TestTypeDiscovery,
  TestTypeConnectivity,
  TestTypePerformance,
  TestTypeStress,
  TestTypeCompliance,
  TestTypePacketCapture,
  TestTypeSecurity,
  TestTypeFuzz,
  TestTypeAll
} TEST_TYPE;

#define TEST_RESULT_PASS  0
#define TEST_RESULT_FAIL  1
#define TEST_RESULT_SKIP  2
#define TEST_RESULT_WARN  3
#define TEST_RESULT_ERROR 4

typedef struct {
  EFI_IPv4_ADDRESS    TargetIp;
  EFI_IPv4_ADDRESS    LocalIp;
  EFI_IPv4_ADDRESS    SubnetMask;
  EFI_IPv4_ADDRESS    Gateway;
  UINT32              TimeoutMs;
  UINT32              Iterations;
  UINT16              TargetPort;
  BOOLEAN             UseCompanion;
  EFI_IPv4_ADDRESS    CompanionIp;
  UINT16              CompanionPort;
} TEST_CONFIG;

typedef struct {
  CHAR16            *Name;
  CHAR16            *Description;
  OSI_LAYER         Layer;
  TEST_TYPE         Type;
  UINT32            EstimatedTimeMs;
  BOOLEAN           RequiresTarget;
  BOOLEAN           RequiresIpv6;
  BOOLEAN           IsDestructive;
  BOOLEAN           NeedSnp;
  BOOLEAN           NeedMnp;
  BOOLEAN           NeedIp4;
  BOOLEAN           NeedTcp4;
  BOOLEAN           NeedUdp4;
  BOOLEAN           NeedDhcp4;
  EFI_STATUS        (*Execute)(
                      IN  NIC_INFO        *Nic,
                      IN  TEST_CONFIG     *Config,
                      OUT TEST_RESULT_DATA *Result
                    );
} TEST_DEFINITION;

typedef struct {
  UINT32            StatusCode;
  UINT64            DurationMs;
  CHAR16            Summary[128];
  CHAR16            Detail[512];
  CHAR16            FailReason[256];
  CHAR16            Suggestion[256];
  UINT64            PacketsSent;
  UINT64            PacketsReceived;
  UINT64            BytesSent;
  UINT64            BytesReceived;
  UINT32            RttMinUs;
  UINT32            RttAvgUs;
  UINT32            RttMaxUs;
  UINT32            RttJitterUs;
} TEST_RESULT_DATA;
```

---

## 6. UI TASARIMI

### 6.1 Renk Kodları

```c
#define COLOR_DEFAULT   EFI_WHITE
#define COLOR_SUCCESS   EFI_GREEN
#define COLOR_ERROR     EFI_RED
#define COLOR_WARNING   EFI_YELLOW
#define COLOR_INFO      EFI_CYAN
#define COLOR_HEADER    EFI_LIGHTBLUE
#define COLOR_LAYER1    EFI_LIGHTMAGENTA
#define COLOR_LAYER2    EFI_LIGHTCYAN
#define COLOR_LAYER3    EFI_LIGHTGREEN
#define COLOR_LAYER4    EFI_YELLOW
#define COLOR_LAYER7    EFI_LIGHTRED
```

### 6.2 Ana Menü

```
╔══════════════════════════════════════════════════════════════════╗
║  DDTSoft — EFI Network Test & OSI Analyzer v1.0.0              ║
╠══════════════════════════════════════════════════════════════════╣
║                                                                 ║
║   [S] System Information        Sistem ve donanım bilgileri    ║
║   [N] Network Interfaces        NIC listesi ve seçimi          ║
║   [T] Run Tests                 Test çalıştır                  ║
║   [C] Packet Capture            Paket yakalama & analiz        ║
║   [R] Reports                   Test sonuç raporları           ║
║   [Q] Quit                      Çıkış                         ║
║                                                                 ║
╚══════════════════════════════════════════════════════════════════╝
```

### 6.3 System Info Ekranları (5 sayfa, ←/→ ile gezinme)

**Sayfa 1/5: Firmware & Sistem** — UEFI Firmware (Vendor, Revision, Spec), BIOS (Vendor, Version, Date, ROM Size), System (Manufacturer, Product, Serial, UUID)

**Sayfa 2/5: CPU & Bellek** — İşlemci (Model, Socket, Hız, Core/Thread), Bellek slot tablosu (locator, boyut, hız, tip, üretici)

**Sayfa 3/5: PCI Cihazları** — Tüm PCI cihazlar tablosu (Bus:Dev.Fn, VendorID, DeviceID, Sınıf, Vendor Adı), Network ★ ile işaretli

**Sayfa 4/5: UEFI Drivers** — Yüklü driver listesi (İsim, Boyut, Yönettiği handle), [N] network filtre

**Sayfa 5/5: ACPI & Config Tables** — ACPI bilgi, EFI Configuration Tables listesi

### 6.4 NIC Listesi, NIC Detay, Test Ortamı Seçimi, Companion Bağlantı

(Önceki planlama dokümanlarındaki gibi — tüm ekranlarda "DDTSoft" markası)

### 6.5 Test Modu Seçimi

```
[1] Quick Scan     — Tüm katmanlar hızlıca, ~30sn
[2] Layer Select   — Tek katman detaylı
[3] Full Suite     — Tüm katmanlar tüm testler, ~5-10dk
[4] Custom         — Test seç
[5] Stress Test    — Yük altında performans
```

### 6.6 Test Çalışma & Sonuç Ekranları

- Canlı ilerleme çubuğu, test durumları (✓/✗/⚠/○/▶)
- Sonuç özeti: PASS/FAIL/WARN/SKIP sayıları, yüzde çubuğu
- Başarısız test detayı: beklenen, sonuç, olası sebepler, öneri
- Quick Scan otomatik teşhis karar ağacı
- Stress test: canlı istatistik + ASCII RTT grafiği
- Dışa aktarma: TXT, CSV, detaylı rapor, binary dump

---

## 7. COMPANION KOORDİNASYON PROTOKOLÜ

### Kontrol Kanalı: UDP 9999, text-based

```
Komutlar (EFI → Companion):
  HELLO / PREPARE <layer> <test> [args] / START / STOP / RESULT / DONE / GETREPORT

Yanıtlar (Companion → EFI):
  ACK / READY / ERROR / REPORT / CONFIRM
```

### Katman Bazlı Companion Görevleri

- **L1:** ethtool link control
- **L2:** raw socket frame, ARP responder
- **L3:** kernel ICMP reply, custom TTL paketleri
- **L4:** multi-port TCP listener, UDP echo server
- **L7:** dnsmasq (DHCP+DNS), mini HTTP server

---

## 8. PCI VENDOR/CLASS LOOKUP TABLOLARI

### PCI Class Codes
```
0x00=Unclassified  0x01=Storage  0x02=Network  0x03=Display
0x04=Multimedia  0x05=Memory  0x06=Bridge  0x07=Communication
0x08=System Peripheral  0x0C=Serial Bus  0x0D=Wireless
```

### Bilinen Vendor ID'ler
```
0x8086=Intel  0x10EC=Realtek  0x14E4=Broadcom  0x1969=Qualcomm Atheros
0x10DE=NVIDIA  0x1022=AMD  0x1002=AMD/ATI  0x15B3=Mellanox
0x1077=QLogic  0x19A2=Emulex  0x1137=Cisco  0x177D=Cavium  0x1D6A=Aquantia
```

---

## 9. TEST DETAYLARI

### Layer 1 — Physical (5 test)
NIC Status, Link Detect, NIC Initialize cycle, Loopback, Link Negotiation

### Layer 2 — Data Link (7 test)
MAC Address Valid, ARP Request/Reply, ARP Cache, Broadcast Frame, Frame TX/RX, MTU Detection, Receive Filter

### Layer 3 — Network (10 test)
IP Config Check, ICMP Echo (Ping), ICMP Sweep, TTL/Hop Discovery, MTU Path Discovery, IP Fragmentation, IPv6 ND, IP Header Validation, Routing Table, Duplicate IP Detection

### Layer 4 — Transport (8 test)
TCP Connect, TCP Multi-Port, TCP Data Transfer, TCP Close, UDP Send/Receive, UDP Multi-Port, Port Scan, TCP Stress

### Layer 5-6 — Session/Presentation (4 test)
TLS Handshake, TLS Cipher Suite, Session Establish, Certificate Check

### Layer 7 — Application (6 test)
DHCP Discover, DHCP Lease Verify, DNS Resolve, DNS Reverse, HTTP GET, HTTP Status Codes

---

## 10. QUICK SCAN OTOMATİK TEŞHİS KARAR AĞACI

```
L1 FAIL → "Fiziksel bağlantı yok. Kablo ve NIC kontrol edin."
L1 OK + L2 FAIL → "Link var ama frame iletişimi yok. Switch/VLAN kontrol."
L2 OK + L3 lokal FAIL → "Frame OK ama IP yapılandırması hatalı."
L3 lokal OK + L3 ext FAIL → "Lokal ağ OK ama dış ağa çıkamıyor. Gateway/routing."
L3 OK + L4 FAIL → "IP OK ama TCP/UDP bağlantı kurulamıyor. Firewall."
L4 OK + L7 DNS FAIL → "Transport OK ama DNS çalışmıyor."
Hepsi PASS → "Tüm katmanlar sağlıklı."
```

---

## 11. GELİŞTİRME FAZLARI

```
Faz 0:  Proje iskeleti — INF/DSC/DEC, tüm header'lar, UiRenderer, Utils, Main
Faz 1:  SystemInfo — SmbiosParser, PciEnumerator, DriverEnumerator, ACPI
Faz 2:  NIC Discovery — SNP enumeration, PCI eşleşme, IP config, NIC detay UI
Faz 3:  CompanionLink — handshake protokolü, kontrol kanalı
Faz 4:  PacketBuilder + PacketParser (Ethernet, IP, ICMP, TCP, UDP, ARP)
Faz 5:  TestRunner + TestRegistry altyapısı
Faz 6:  Layer 1-2 testleri
Faz 7:  Layer 3 testleri
Faz 8:  Layer 4 testleri
Faz 9:  Layer 7 testleri
Faz 10: QuickScan + otomatik teşhis karar ağacı
Faz 11: StressTest engine
Faz 12: ReportExporter (TXT, CSV, detaylı, binary)
Faz 13: Companion (Python, Linux) — paralel geliştirilebilir
```

---

## 12. KRİTİK UYARI VE KISITLAR

### EFI Ortamı
- Multithreading YOK — event-based (CreateEvent, WaitForEvent)
- Timeout yönetimi kritik — gBS->Stall(), gBS->SetTimer()
- Her protokolü LocateProtocol ile önce sorgula, yoksa SKIP
- SNP hemen her yerde var ama üst katmanlar firmware'e bağlı
- ServiceBinding ile child handle oluşturma gerekli

### NIC Paylaşımı
- Firmware NIC'i kullanıyor olabilir (PXE), SNP->Mode->State kontrol et
- Test sonrası orijinal duruma geri dön

### Paket İşleme
- Checksum: IP, TCP, UDP, ICMP doğru hesapla
- Endianness: HTONS(), NTOHS(), HTONL(), NTOHL()

---

## 13. CLAUDE CODE OTURUM YÖNETİMİ

### CLAUDE.md Kullanımı
Proje kök dizininde CLAUDE.md var. Claude Code her oturum başında bunu otomatik okur. Her faz bittiğinde checkbox güncellenir.

### Komutlar

```bash
# Proje dizininde başlat
cd ~/DDTSoft/DDTSoftNetTest
claude

# İlk oturum:
> PROJECT_SPEC.md oku ve Faz 0'dan başla.

# Sonraki oturumlar (console kapatıp açtıktan sonra):
> CLAUDE.md ve PROJECT_SPEC.md oku, kaldığımız fazdan devam et.

# Context doluyorsa:
> /compact

# Belirli faz:
> PROJECT_SPEC.md oku, Faz 3 (CompanionLink) kodla.

# Build hatası:
> Build hatası: [yapıştır]. Düzelt.
```

### İş Akışı

```
1. cd ~/DDTSoft/DDTSoftNetTest && claude
2. "CLAUDE.md ve PROJECT_SPEC.md oku, devam et"
3. Claude Code kodu yazar
4. Build: cd ~/edk2 && source edksetup.sh && export PACKAGES_PATH=$HOME/edk2:$HOME/DDTSoft && build -a X64 -t GCC5 -p DDTSoftNetTest/DDTSoftNetTest.dsc -b DEBUG
5. Hata varsa Claude Code'a göster
6. QEMU test: ~/DDTSoft/DDTSoftNetTest/Scripts/build_and_run.sh
7. Faz bitince: "CLAUDE.md güncelle, Faz X tamamlandı"
8. Console kapatıp açtığında 1'den başla
```

---

## 14. KOMUT — Claude Code'a Başlangıç Talimatı

Bu proje EDK2 tabanlı bir UEFI shell uygulamasıdır. edk2 dışında ayrı dizinde yaşar, PACKAGES_PATH ile build edilir. Faz 0'dan başla:

1. DDTSoftNetTest.inf, DDTSoftNetTest.dsc, DDTSoftNetTest.dec dosyalarını oluştur (bu dokümandaki içerikleri kullan)
2. Scripts/ altındaki build.sh, build_and_run.sh, setup_tap.sh dosyalarını oluştur
3. Include/ altındaki tüm header dosyalarını oluştur
4. Source/Main.c — entry point DDTSoftNetTestMain, ana menü
5. Source/UiRenderer.c — box drawing çerçeve, renk, sayfa navigasyonu, ilerleme çubuğu
6. Source/Utils.c — yardımcı fonksiyonlar

Tüm ekranlarda "DDTSoft" markası. Box drawing: ╔═╗║╚╝╠╣╬. EFI ConOut renkli text output.
Sonra sırayla Faz 1, 2, 3... devam et.
