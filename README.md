# DDTSoftNetTest

EDK2 tabanli UEFI shell uygulamasi. OSI katman network test ve analiz araci.

## Ozellikler

- Sistem bilgisi toplama (SMBIOS, PCI, Driver, ACPI)
- NIC kesfi ve detayli bilgi (SNP, PCI eslestirme)
- OSI Layer 1-7 network testleri
- Paket yakalama ve analiz
- Stress test (throughput, latency, packet loss)
- QuickScan otomatik teshis
- Rapor disa aktarma (TXT, CSV, Detailed, Binary)
- Companion (Python) ile tam test otomasyonu

## Dizin Yapisi

```
DDTSoftNetTest/
├── Include/            Header dosyalari
├── Source/             C kaynak kodlari
├── Scripts/            Build ve calistirma scriptleri
├── Companion/          Python companion uygulamasi
└── Release/            Calistirilabilir dosyalar
    ├── DDTSoftNetTest.efi
    ├── build.sh
    ├── build_and_run.sh
    ├── run_with_tap.sh
    ├── setup_tap.sh
    └── Companion/
```

## Build

```bash
cd ~/edk2 && source edksetup.sh
export PACKAGES_PATH=$HOME/edk2:$HOME/DDTSoft
build -a X64 -t GCC5 -p DDTSoftNetTest/DDTSoftNetTest.dsc -b DEBUG
```

Cikti: `~/edk2/Build/DDTSoftNetTest/DEBUG_GCC5/X64/DDTSoftNetTest.efi`

## QEMU ile Test

```bash
chmod +x ~/DDTSoft/DDTSoftNetTest/Scripts/*.sh
~/DDTSoft/DDTSoftNetTest/Scripts/build_and_run.sh
```

Script otomatik olarak build eder, FAT32 disk image olusturur ve QEMU baslatir.
Uygulama `EFI/BOOT/BOOTX64.EFI` olarak yuklendiginden otomatik acilir.
Cikis: `Ctrl+A, X`

## Gercek PC'de Test

### Gerekli Donanim

- **DUT (Test edilecek PC)**: UEFI destekli, Ethernet portu olan PC
- **Companion PC**: Linux (Ubuntu/Debian), Ethernet portu olan PC
- **Ethernet kablosu**: Iki PC'yi birbirine baglamak icin (direkt veya switch uzerinden)
- **USB bellek**: FAT32 formatli, EFI binary'yi tasimak icin

### Adim 1: USB Boot Diski Hazirla

```bash
# USB bellegi tak, hangi cihaz oldugunu bul
lsblk

# Ornek: /dev/sdb ise (DIKKAT: dogru diski sec!)
sudo mkfs.vfat -F 32 /dev/sdb1

# Mount et
sudo mkdir -p /mnt/usb
sudo mount /dev/sdb1 /mnt/usb

# EFI dizin yapisini olustur ve binary'yi kopyala
sudo mkdir -p /mnt/usb/EFI/BOOT
sudo cp Release/DDTSoftNetTest.efi /mnt/usb/EFI/BOOT/BOOTX64.EFI
sudo cp Release/DDTSoftNetTest.efi /mnt/usb/

sudo umount /mnt/usb
```

### Adim 2: DUT PC'yi USB'den Boot Et

1. USB'yi DUT PC'ye tak
2. PC'yi ac, BIOS/UEFI ayarlarina gir (genelde `F2`, `F12`, `Del`)
3. **Secure Boot'u kapat** (DDTSoftNetTest imzali degil)
4. Boot sirasini USB'ye ayarla veya boot menusunden USB'yi sec
5. Iki senaryo:
   - **Otomatik**: `EFI/BOOT/BOOTX64.EFI` olarak kopyaladiysan direkt acilir
   - **EFI Shell**: Shell acilirsa:
     ```
     Shell> fs0:
     FS0:\> DDTSoftNetTest.efi
     ```
     `fs0` calismazsa `fs1`, `fs2` dene veya `map -r` ile disk listesine bak

### Adim 3: Sadece DUT Testi (Companion Olmadan)

Companion baglamadan sunlari test edebilirsin:

- **[S] System Information** -- SMBIOS, PCI, driver bilgileri
- **[N] Network Interfaces** -- NIC kesfi, MAC/PCI bilgileri
- **[Q] Quit** -- Cikis

Network testleri (`[T]`, `[C]`, `[R]`) companion gerektirir.

### Adim 4: Companion ile Tam Test

#### 4a. Fiziksel Baglanti

```
DUT PC ---- Ethernet kablosu ---- Companion PC
```

Direkt baglanti veya ayni switch uzerinden.

#### 4b. Companion PC'de IP Ayarla

```bash
# Ethernet arayuzunu bul
ip link show
# Ornek: eth0, enp3s0, eno1 vb.

# Statik IP ata (DUT 192.168.100.10 bekler)
sudo ip addr flush dev enp3s0
sudo ip addr add 192.168.100.1/24 dev enp3s0
sudo ip link set enp3s0 up

# Dogrula
ip addr show enp3s0
```

#### 4c. Companion'i Baslat

```bash
cd Release/Companion

# Bagimliliklari kur (ilk seferde)
pip3 install -r requirements.txt

# Companion'i baslat (kendi ethernet arayuz adinla)
sudo python3 companion.py -i enp3s0 --ip 192.168.100.1
```

#### 4d. DUT'ta Test Calistir

DUT PC'de uygulama acikken:

1. **[N] Network Interfaces** -- NIC'i sec
2. **[T] Run Tests** -- Katman sec:
   - Layer 1: Link durumu, hiz
   - Layer 2: ARP cozumleme, Ethernet frame
   - Layer 3: ICMP ping, TTL
   - Layer 4: TCP baglanti, UDP echo
   - Layer 7: DHCP, DNS, HTTP
3. **[C] Packet Capture** -- Paket yakalama ve analiz
4. **[R] Reports** -- Test sonuclarini TXT/CSV/Binary olarak disa aktar

### Sorun Giderme

| Sorun | Cozum |
|-------|-------|
| USB'den boot olmuyor | Secure Boot kapali mi? UEFI modda mi? |
| EFI Shell `fs0` bulunamiyor | `map -r` yaz, USB'nin hangi `fsX` oldugunu bul |
| NIC bulunamiyor | PC'nin Ethernet'i takili mi? Bazi NIC'ler UEFI driver istiyor |
| Companion baglanmiyor | IP dogru mu? `ping 192.168.100.1` dene, firewall kapali mi? |
| Testler basarisiz | Ethernet kablosu takili mi? Companion calisiyor mu? |

## Companion ile QEMU Testi (TAP Networking)

QEMU uzerinden companion testi icin:

```bash
# Terminal 1: TAP interface kur
sudo ~/DDTSoft/DDTSoftNetTest/Scripts/setup_tap.sh

# Terminal 2: Companion baslat
cd ~/DDTSoft/DDTSoftNetTest/Release/Companion
sudo python3 companion.py -i tap0 --ip 192.168.100.1

# Terminal 3: QEMU baslat
~/DDTSoft/DDTSoftNetTest/Scripts/run_with_tap.sh
```

## Lisans

DDTSoft (c) 2026
