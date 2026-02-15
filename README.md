# DDTSoft — EFI Network Test & OSI Layer Analyzer

EDK2 tabanli bare-metal UEFI shell uygulamasi. OSI katmanlarini (Layer 1-7) firmware seviyesinde test eden ve analiz eden network diagnostik araci.

## Neden EFI Seviyesi?

- OS kernel/driver katmani araya girmiyor — dogrudan NIC donanim erisimi
- Layer 1-2 testleri (fiziksel link, raw Ethernet frame) OS altinda neredeyse imkansiz
- Firmware seviyesinde network stack dogrulama
- PXE boot oncesi diagnostik
- SNP (Simple Network Protocol) ile raw frame TX/RX

## Ozellikler

- **Sistem Bilgisi**: SMBIOS, PCI, UEFI Driver, ACPI tablo analizi
- **NIC Kesfi**: SNP enumeration, PCI eslestirme, vendor/device lookup, IP konfigurasyonu
- **OSI Layer 1-7 Testleri**: 36 farkli test (asagida detayli)
- **QuickScan**: Otomatik teshis karar agaci ile hizli tarama
- **Stress Test**: Throughput, latency, packet loss olcumu
- **Rapor**: TXT, CSV, detayli rapor, binary dump formatlari
- **Companion**: Linux uzerinde calisan Python uygulama ile koordineli test

## Mimari

```
┌─────────────────────────────────────────────┐
│          Main (Shell App / Menu)            │
├─────────────────────────────────────────────┤
│           Test Runner Engine                │
├──────────┬──────────┬──────────┬────────────┤
│ Layer 1  │ Layer 2  │ Layer 3  │ Layer 4    │
│ Physical │ DataLink │ Network  │ Transport  │
├──────────┴──────────┴──────────┴────────────┤
│          Layer 7 (Application)              │
├─────────────────────────────────────────────┤
│  PacketBuilder/Parser  │  StressTest        │
├─────────────────────────────────────────────┤
│  QuickScan │ ReportExporter │ CompanionLink │
├─────────────────────────────────────────────┤
│  SystemInfo │ NicDiscovery  │ UiRenderer    │
└─────────────────────────────────────────────┘
```

### Test Topolojisi

```
┌─────────────────────┐                    ┌─────────────────────┐
│   DUT (Device       │   Ethernet Cable   │   Test Server       │
│   Under Test)       │◄──────────────────►│   (Companion)       │
│   UEFI Shell        │   Direct / Switch  │   Linux PC          │
│   DDTSoftNetTest    │                    │   DDTSoft Companion │
└─────────────────────┘                    └─────────────────────┘
```

## Dizin Yapisi

```
DDTSoftNetTest/
├── DDTSoftNetTest.inf      # EDK2 module tanimlari
├── DDTSoftNetTest.dsc      # Platform build tanimlari
├── DDTSoftNetTest.dec      # Package declaration
├── Include/
│   ├── DDTSoftNetTest.h    # Ana header, NIC_INFO, global'ler
│   ├── OsiLayers.h         # OSI layer enum, TEST_CONFIG, TEST_RESULT_DATA
│   ├── PacketDefs.h        # Ethernet/IP/ICMP/TCP/UDP/ARP header struct'lari
│   ├── TestCases.h         # Test fonksiyon prototipleri
│   ├── SystemInfo.h        # SMBIOS, PCI, Driver, ACPI veri yapilari
│   ├── PciIds.h            # PCI vendor/device ID lookup tablolari
│   └── UiRenderer.h        # UI fonksiyon prototipleri
├── Source/
│   ├── Main.c              # Entry point, ana menu, UI akisi
│   ├── UiRenderer.c        # Box drawing, renk, ilerleme cubugu
│   ├── SystemInfo.c        # Firmware/sistem bilgisi toplama
│   ├── SmbiosParser.c      # SMBIOS tablo parse (BIOS, CPU, RAM)
│   ├── PciEnumerator.c     # PCI cihaz tarama, vendor/class lookup
│   ├── DriverEnumerator.c  # UEFI driver listesi
│   ├── NicDiscovery.c      # NIC bulma, SNP/PCI eslestirme, IP config
│   ├── CompanionLink.c     # Companion handshake, kontrol kanali
│   ├── TestRunner.c        # Test calistirma motoru
│   ├── TestRegistry.c      # Test kayit sistemi, filtreleme
│   ├── QuickScan.c         # Otomatik teshis karar agaci
│   ├── Layer1Physical.c    # Fiziksel katman testleri (5 test)
│   ├── Layer2DataLink.c    # Veri baglantisi testleri (7 test)
│   ├── Layer3Network.c     # Ag katmani testleri (10 test)
│   ├── Layer4Transport.c   # Tasima katmani testleri (8 test)
│   ├── Layer7Application.c # Uygulama katmani testleri (6 test)
│   ├── StressTest.c        # Yuk testi motoru
│   ├── PacketBuilder.c     # Paket olusturma (Ethernet, IP, ICMP, TCP, UDP, ARP)
│   ├── PacketParser.c      # Paket ayristirma
│   ├── OsiAnalyzer.c       # OSI katman analizi
│   ├── ReportExporter.c    # Rapor disa aktarma
│   └── Utils.c             # Yardimci fonksiyonlar
├── Companion/
│   ├── companion.py        # Ana companion uygulamasi
│   ├── services/           # Servis modulleri
│   └── requirements.txt
├── Scripts/
│   ├── build.sh            # Sadece build
│   ├── build_and_run.sh    # Build + QEMU
│   ├── run_with_tap.sh     # TAP networking ile QEMU
│   └── setup_tap.sh        # TAP interface kurulumu
└── Release/                # Dagitim dosyalari
    ├── DDTSoftNetTest.efi
    └── Companion/
```

## Test Detaylari

### Layer 1 — Physical (5 test)

Fiziksel katman testleri dogrudan `EFI_SIMPLE_NETWORK_PROTOCOL` ile NIC donanim durumunu kontrol eder.

| # | Test | Aciklama |
|---|------|----------|
| 1 | **NIC Status** | NIC durumunu kontrol eder: `Snp->Mode->State` degerinin `EfiSimpleNetworkInitialized` olup olmadigini, `MediaPresent` flagini ve `MaxPacketSize` degerinin gecerli olup olmadigini dogrular. |
| 2 | **Link Detect** | Fiziksel linkin aktif olup olmadigini iki yontemle kontrol eder: (1) `MediaPresent` flagi, (2) gercek bir minimal frame TX deneyerek linkin canliligi dogrulanir. Kablonun takili olup olmadigini belirler. |
| 3 | **NIC Init Cycle** | NIC'i `Shutdown → Stop → Start → Initialize` dongusunden gecirerek kararliligi test eder. Her adimda `EFI_STATUS` kontrol edilir. NIC'in reset sonrasi duzgun calisip calismadigini dogrular. |
| 4 | **Loopback** | NIC uzerinden broadcast frame gonderip kendi gonderdigini geri alip alamadigini test eder. `ReceiveFilters` ile promiscuous mod aktif edilir, frame gonderilir, 500ms icerisinde ayni frame'in donmesi beklenir. |
| 5 | **Link Negotiation** | `Snp->Mode` uzerinden IfType (Ethernet/WiFi/Fiber vb.), `MediaHeaderSize`, `MaxPacketSize` ve receive filter yeteneklerini (`UNICAST`, `MULTICAST`, `BROADCAST`, `PROMISCUOUS`) sorgular. |

### Layer 2 — Data Link (7 test)

Veri baglantisi katmani Ethernet frame duzeninde calisir. ARP cozumleme, broadcast ve raw frame TX/RX testleri yapar.

| # | Test | Aciklama |
|---|------|----------|
| 1 | **MAC Address Valid** | NIC'in MAC adresinin gecerli oldugunu dogrular: sifir olmamasi (00:00:00:00:00:00), broadcast olmamasi (FF:FF:FF:FF:FF:FF) ve multicast bit'inin kaynakta set edilmemis olmasi. |
| 2 | **ARP Request/Reply** | ARP request paketi olusturup hedefe (gateway veya target IP) gonderir, ARP reply bekler. Hedef MAC adresinin cozumlenip cozumlenemedigini test eder. Birden fazla hedef denenir. |
| 3 | **ARP Cache** | EFI ARP protokolunun cache mekanizmasini kontrol eder. `EFI_ARP_PROTOCOL` uzerinden `Find()` ile cache entry varligini sorgular. |
| 4 | **Broadcast Frame** | Broadcast MAC adresine (FF:FF:FF:FF:FF:FF) bir Ethernet frame gonderir ve TX tamamlanmasini dogrular. Switch/hub uzerinden broadcast iletisimini test eder. |
| 5 | **Frame TX/RX** | Hedefe raw Ethernet frame gonderip cevap bekler. Companion gerektirir. Gonderilen frame'in karsi tarafta alinip cevaplanmasiyla full-duplex frame iletisimini dogrular. |
| 6 | **MTU Detection** | Artan boyutlarda frame gondererek desteklenen maksimum MTU degerini belirler. 64 byte'tan baslayip `MaxPacketSize` limitine kadar test eder. Companion gerektirir. |
| 7 | **Receive Filter** | NIC'in receive filter modlarini test eder: unicast, multicast ve broadcast filtrelerini sirayla aktif/deaktif eder, `ReceiveFilters()` donus degerlerini kontrol eder. |

### Layer 3 — Network (10 test)

Ag katmani IP yapilandirmasi, ICMP ping, traceroute, MTU path discovery ve IP fragmentation testlerini kapsar.

| # | Test | Aciklama |
|---|------|----------|
| 1 | **IP Config Check** | IPv4 adres, subnet mask ve gateway yapilandirmasinin gecerligini kontrol eder. `EFI_IP4_CONFIG2_PROTOCOL` uzerinden okunan degerlerin sifir veya gecersiz olmamasini dogrular. |
| 2 | **ICMP Echo (Ping)** | Hedefe ICMP Echo Request gonderip Echo Reply bekler. Round-trip time (RTT) olcer. Temel ag baglantisini dogrular. Companion gerektirir. |
| 3 | **ICMP Sweep** | Subnet uzerinde IP araligi tarayarak aktif hostlari bulur. Her IP'ye ICMP ping gonderir ve cevap veren adesleri listeler. |
| 4 | **TTL/Hop Discovery** | TTL degerini 1'den baslayarak artirip hedefe gonderir. Her hop'ta ICMP Time Exceeded cevabi ile router IP'lerini kesfeder (traceroute benzeri). Companion gerektirir. |
| 5 | **MTU Path Discovery** | Don't Fragment (DF) bit'i set edilmis paketlerle artan boyutlarda gonderim yapar. ICMP Fragmentation Needed cevaplariyla yol uzerindeki minimum MTU'yu belirler. Companion gerektirir. |
| 6 | **IP Fragmentation** | MTU'dan buyuk IP paketleri gondererek fragmentation ve reassembly mekanizmasini test eder. Companion gerektirir. |
| 7 | **IPv6 Neighbor Discovery** | IPv6 Neighbor Discovery protokolunu test eder. NDP mesajlari gondererek IPv6 destegini dogrular. |
| 8 | **IP Header Validation** | Gonderilen ve alinan IP header alanlarinin (version, IHL, checksum, TTL vb.) RFC uyumlulugunun dogrulanmasi. |
| 9 | **Routing Table** | IP routing tablosundaki entry'leri kontrol eder. Default gateway ve subnet route'larin varligini dogrular. |
| 10 | **Duplicate IP Detection** | Ag uzerinde ayni IP adresini kullanan baska bir cihaz olup olmadigini gratuitous ARP ile tespit eder. |

### Layer 4 — Transport (8 test)

Tasima katmani TCP ve UDP protokollerini `EFI_TCP4_PROTOCOL` ve `EFI_UDP4_PROTOCOL` uzerinden test eder.

| # | Test | Aciklama |
|---|------|----------|
| 1 | **TCP Connect** | Hedefe TCP baglantisi kurar (3-way handshake). `Configure → Connect → Close` dongusunu test eder. Companion gerektirir. |
| 2 | **TCP Multi-Port** | Birden fazla TCP portuna (80, 443, 8080, 22 vb.) sirali baglanti denemesi yapar. Her portun acik/kapali durumunu raporlar. Companion gerektirir. |
| 3 | **TCP Data Transfer** | TCP baglantisi uzerinden veri gonderip alir. Gonderilen ve alinan byte sayilarini karsilastirir, throughput olcer. Companion gerektirir. |
| 4 | **TCP Close** | TCP baglantisinin duzgun kapatilmasini (FIN handshake) test eder. Graceful close sonrasi durumu kontrol eder. Companion gerektirir. |
| 5 | **UDP Send/Receive** | UDP datagram gonderip cevap bekler. UDP echo mekanizmasini test eder. Companion gerektirir. |
| 6 | **UDP Multi-Port** | Birden fazla UDP portuna datagram gonderip cevap bekler. Companion gerektirir. |
| 7 | **Port Scan** | Hedef host uzerinde yaygin TCP portlarini tarar. Acik, kapali ve filtrelenmis portlari raporlar. Companion gerektirir. |
| 8 | **TCP Stress** | Hizli TCP connect/disconnect dongusu ile stres testi yapar. Cok sayida baglanti acip kapatarak kararliligi olcer. Companion gerektirir. |

### Layer 7 — Application (6 test)

Uygulama katmani DHCP, DNS ve HTTP protokollerini EFI protokol stack'i uzerinden test eder.

| # | Test | Aciklama |
|---|------|----------|
| 1 | **DHCP Discover** | DHCP Discover paketi gonderip Offer cevabini bekler. DHCP sunucusunun varligi ve teklif edilen IP/mask/gateway bilgisini raporlar. |
| 2 | **DHCP Lease Verify** | Mevcut DHCP lease'in gecerliligini kontrol eder. Lease durumu ve suresini dogrular. |
| 3 | **DNS Resolve** | Bir hostname'i DNS sorgusu ile cozer. DNS sunucusuna UDP query gonderir, cevaptaki IP adresini dogrular. |
| 4 | **DNS Reverse** | Bir IP adresi icin ters DNS sorgusu yapar (PTR record). |
| 5 | **HTTP GET** | Hedefe HTTP GET istegi gonderir ve cevabi dogrular. HTTP protokol destegini test eder. Companion gerektirir. |
| 6 | **HTTP Status Codes** | Farkli HTTP durum kodlarini (200, 404, 500 vb.) test eder. Companion gerektirir. |

### Stress Test

Ag performansini yuk altinda olcer:

- **ICMP Flood**: Hizli ICMP paket gonderimi, yanit orani ve latency olcumu
- **UDP Flood**: Yuksek hacimli UDP datagram gonderimi, throughput hesaplama
- **Raw Frame Flood**: SNP uzerinden maximum hizda raw frame gonderimi
- **Canli istatistik**: Gonderilen/alinan paket sayisi, kayip orani, ortalama RTT
- **ASCII RTT grafigi**: Terminal uzerinde gercek zamanli latency grafigi

### QuickScan — Otomatik Teshis

Her katmandan hizli testler calistirip otomatik teshis karar agaci uygular:

```
L1 FAIL → "Fiziksel baglanti yok. Kablo ve NIC kontrol edin."
L1 OK + L2 FAIL → "Link var ama frame iletisimi yok. Switch/VLAN kontrol."
L2 OK + L3 lokal FAIL → "Frame OK ama IP yapilandirmasi hatali."
L3 lokal OK + L3 ext FAIL → "Lokal ag OK ama dis aga cikamiyor. Gateway/routing."
L3 OK + L4 FAIL → "IP OK ama TCP/UDP baglanti kurulamiyor. Firewall."
L4 OK + L7 DNS FAIL → "Transport OK ama DNS calismiyor."
Hepsi PASS → "Tum katmanlar saglikli."
```

## Build

### Gereksinimler

- EDK2 (https://github.com/tianocore/edk2)
- GCC 5+ toolchain
- NASM assembler
- Python 3
- QEMU + OVMF (test icin)

### Build Komutu

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

### Companion ile QEMU Testi (TAP Networking)

```bash
# Terminal 1: TAP interface kur
sudo ~/DDTSoft/DDTSoftNetTest/Scripts/setup_tap.sh

# Terminal 2: Companion baslat
cd ~/DDTSoft/DDTSoftNetTest/Release/Companion
sudo python3 companion.py -i tap0 --ip 192.168.100.1

# Terminal 3: QEMU baslat
~/DDTSoft/DDTSoftNetTest/Scripts/run_with_tap.sh
```

## Gercek PC'de Test

### Gerekli Donanim

- **DUT (Test edilecek PC)**: UEFI destekli, Ethernet portu olan PC
- **Companion PC**: Linux (Ubuntu/Debian), Ethernet portu olan PC
- **Ethernet kablosu**: Direkt veya switch uzerinden baglanti
- **USB bellek**: FAT32 formatli

### Adim 1: USB Boot Diski Hazirla

```bash
# USB bellegi FAT32 formatlayip EFI binary kopyala
sudo mkfs.vfat -F 32 /dev/sdX1
sudo mkdir -p /mnt/usb && sudo mount /dev/sdX1 /mnt/usb
sudo mkdir -p /mnt/usb/EFI/BOOT
sudo cp Release/DDTSoftNetTest.efi /mnt/usb/EFI/BOOT/BOOTX64.EFI
sudo umount /mnt/usb
```

### Adim 2: DUT PC'yi USB'den Boot Et

1. USB'yi DUT PC'ye tak
2. BIOS/UEFI ayarlarina gir (`F2`, `F12`, `Del`)
3. **Secure Boot'u kapat** (imzali degil)
4. USB'den boot et
5. EFI Shell acilirsa: `fs0:\> DDTSoftNetTest.efi`

### Adim 3: Companion Olmadan Test

Companion baglamadan kullanilabilecek ozellikler:
- **[S] System Information** — SMBIOS, PCI, driver bilgileri
- **[N] Network Interfaces** — NIC kesfi, MAC/PCI bilgileri
- **Layer 1-2 testlerinin cogu** — Fiziksel ve frame duzeyi

### Adim 4: Companion ile Tam Test

```bash
# Companion PC'de IP ayarla
sudo ip addr add 192.168.100.1/24 dev enp3s0
sudo ip link set enp3s0 up

# Companion'i baslat
cd Release/Companion
sudo python3 companion.py -i enp3s0 --ip 192.168.100.1
```

DUT'ta: `[N]` ile NIC sec → `[T]` ile test calistir

### IP Adresleme

```
Test Network : 192.168.100.0/24
DUT (EFI)    : 192.168.100.10
Companion    : 192.168.100.1
Kontrol      : UDP 9999
```

## Companion Koordinasyon Protokolu

Kontrol kanali: UDP port 9999, text-based mesajlar.

```
EFI → Companion:  HELLO / PREPARE <layer> <test> / START / STOP / RESULT / DONE
Companion → EFI:  ACK / READY / ERROR / REPORT / CONFIRM
```

Companion katman bazli gorevleri:
- **L1**: ethtool ile link kontrolu
- **L2**: Raw socket frame, ARP responder
- **L3**: ICMP reply, TTL paketleri
- **L4**: TCP listener, UDP echo server
- **L7**: DHCP + DNS (dnsmasq), HTTP server

## Sorun Giderme

| Sorun | Cozum |
|-------|-------|
| USB'den boot olmuyor | Secure Boot kapali mi? UEFI modda mi? |
| NIC bulunamiyor | Ethernet takili mi? NIC'in UEFI driver'i var mi? |
| Companion baglanmiyor | IP dogru mu? Firewall kapali mi? |
| TX Invalid Parameter | Frame pre-built ise `HeaderSize=0` olmali |
| Ekran titremesi | `UiClearScreen()` yerine `UiClearLines()` kullanin |
| Layer testleri SKIP | Ilgili EFI protokol firmware'de mevcut degil |

## Lisans

DDTSoft (c) 2026
