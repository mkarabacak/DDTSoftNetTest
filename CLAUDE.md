# DDTSoftNetTest — Claude Code Proje Bağlamı

## Proje Nedir
EDK2 tabanlı UEFI shell uygulaması. OSI katman network test ve analiz aracı.
Tam spesifikasyon: PROJECT_SPEC.md dosyasını oku.

## Dizin Yapısı
- Proje: ~/DDTSoft/DDTSoftNetTest/
- EDK2: ~/edk2/ (ayrı, dokunulmaz)
- Build: PACKAGES_PATH=$HOME/edk2:$HOME/DDTSoft

## Mevcut Durum
- [x] Faz 0: Proje iskeleti — INF/DSC/DEC, header'lar, UiRenderer, Utils, Main
- [x] Faz 1: SystemInfo — SmbiosParser, PciEnumerator, DriverEnumerator, ACPI
- [x] Faz 2: NIC Discovery — SNP enum, PCI eşleşme, IP config, NIC detay UI
- [x] Faz 3: CompanionLink — handshake, kontrol kanalı
- [x] Faz 4: PacketBuilder + PacketParser
- [x] Faz 5: TestRunner + TestRegistry
- [x] Faz 6: Layer 1-2 testleri
- [x] Faz 7: Layer 3 testleri
- [x] Faz 8: Layer 4 testleri
- [x] Faz 9: Layer 7 testleri
- [x] Faz 10: QuickScan + otomatik teşhis
- [x] Faz 11: StressTest
- [x] Faz 12: ReportExporter
- [x] Faz 13: Companion (Python)

## Build Komutu
```bash
cd ~/edk2 && source edksetup.sh && export PACKAGES_PATH=$HOME/edk2:$HOME/DDTSoft && build -a X64 -t GCC5 -p DDTSoftNetTest/DDTSoftNetTest.dsc -b DEBUG
```

## Test Komutu
```bash
chmod +x ~/DDTSoft/DDTSoftNetTest/Scripts/*.sh
~/DDTSoft/DDTSoftNetTest/Scripts/build_and_run.sh
```

## Önemli Kurallar
- PROJECT_SPEC.md ana referans doküman, her zaman ona uy
- Tüm UI ekranlarında "DDTSoft" markası olacak
- Her faz bittiğinde bu dosyadaki checkbox'ı [x] güncelle
- Build hataları varsa önce çöz, sonra faza devam et
- Box drawing karakterleri kullan (╔═╗║╚╝╠╣╬)
- EFI ConOut üzerinden renkli text output
- Entry point: DDTSoftNetTestMain
