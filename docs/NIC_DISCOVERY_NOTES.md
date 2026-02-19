# NIC Discovery — Teknik Notlar

## SNP Duplicate Filtreleme

### Problem

UEFI firmware'de bir fiziksel NIC icin birden fazla `EFI_SIMPLE_NETWORK_PROTOCOL` handle'i olusturulur. Bunun nedeni UEFI network stack mimarisidir:

```
Fiziksel NIC (PCI Device)
    └── SNP Driver
        ├── SNP Handle #1 (ana)
        │   └── MNP Service Binding
        │       ├── MNP Child #1 → ARP, IP4, TCP4, UDP4...
        │       └── MNP Child #2 → IP4Config2...
        ├── SNP Handle #2 (MNP child — ayni MAC)
        └── SNP Handle #3 (IP4 child — ayni MAC)
```

Her MNP/IP4 child handle'i da SNP expose eder. Sonuc: **1 fiziksel NIC = 2-4 SNP handle, hepsi ayni MAC adresi**.

### Cozum (NicDiscovery.c)

Discovery sonrasi MAC adresi bazli duplicate tespit ve filtreleme:

1. Tum SNP handle'lari toplanir
2. MAC adresleri pairwise karsilastirilir
3. Ayni MAC'e sahip handle'lardan **en fazla protokol destegi olan** tutulur
4. Diger duplicateler `Handle = NULL` yapilir
5. Array compact edilir, indeksler yenilenir

**Protokol sayma:**
```c
Score = HasMnp + HasArp + HasIp4 + HasIp6 + HasTcp4 + HasUdp4 +
        HasDhcp4 + HasDns4 + HasHttp + HasTls;
```

En yuksek Score'a sahip handle tutulur — bu handle tum upper-layer protokollere erisim saglar.

### Neden Score Bazli?

UEFI'de farkli child handle'lar farkli protokol alt kumelerine sahip olabilir:
- Handle A: SNP + MNP + ARP (Score=3)
- Handle B: SNP + MNP + ARP + IP4 + TCP4 + UDP4 (Score=6)

Handle B tutulursa tum protokollere erisim korunur.

## PCI NIC Renk Kodlamasi

NIC listesinde PCI NIC'ler durum bazli renklendirilir:

| Durum | Renk | Anlami |
|-------|------|--------|
| Driver yok | Kirmizi | PCI cihaz var ama UEFI driver yuklu degil |
| Driver + Link UP | Yesil | Calisiyor, kablo bagli |
| Driver + Link DOWN | Sari | Driver var ama kablo bagli degil |
| Driver + MAC bilgisi yok | Cyan | Driver var ama SNP eslesmesi yapilamamis |

## SNP vs PCI NIC Farki

| Ozellik | SNP Network Interface | PCI Network Controller |
|---------|----------------------|----------------------|
| Kaynak | `EFI_SIMPLE_NETWORK_PROTOCOL` handle | `EFI_PCI_IO_PROTOCOL` handle (Class=0x02) |
| Ne ifade eder | Yazilim protokol instance'i | Fiziksel donanim |
| Sayisi | 1 NIC = 2-4 handle (filtreleme oncesi) | 1 NIC = 1 handle |
| Protokol erisimi | SNP, MNP, ARP, IP4, TCP4, UDP4... | Sadece PCI IO |
| Driver gerekli mi | Evet (SNP driver yuklenmis) | Hayir (PCI her zaman gorunur) |
| Kullanim | Test ve iletisim icin | Donanim tespiti icin |

## IP Konfigurasyonu

NIC discovery sirasinda IP bilgisi `EFI_IP4_CONFIG2_PROTOCOL` uzerinden okunur:

```
IP4Config2 → GetData(Ip4Config2DataTypeInterfaceInfo) → {
  StationAddress,
  SubnetMask
}
IP4Config2 → GetData(Ip4Config2DataTypeGateway) → {
  Gateway
}
```

DHCP aktif degilse veya IP4Config2 mevcut degilse `HasIpConfig = FALSE`.
Bu durumda echo probe testlerinde CompanionLink'in statik IP'leri (192.168.100.10/1) kullanilir.
