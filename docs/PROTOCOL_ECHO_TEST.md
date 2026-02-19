# Protocol Echo Test — Teknik Dokumantasyon

EFI uygulamasindan companion'a periyodik echo probe gondererek canli baglanti takibi yapan ozelligin teknik analizi.

## Genel Bakis

Protocol Echo Test, NIC detay ekranindan secilen bir protokol uzerinden her saniye bir probe mesaji gonderip ayni mesaji geri bekleyen mekanizmadir. Round-trip time (RTT), kayip orani ve gecmis sonuclari canli olarak gosterir.

```
         EFI (DUT)                          Companion (PC)
     ┌──────────────┐                   ┌──────────────────┐
     │ ProbeExecute │  DDTECHO|ID=0001  │                  │
     │    Once()    │──────────────────►│  Echo Service    │
     │              │  DDTECHO|ID=0001  │  (ARP/ICMP/UDP/  │
     │  RttUs=3ms   │◄──────────────────│   TCP)           │
     │  PASS        │                   │                  │
     └──────────────┘                   └──────────────────┘
           │
     ┌─────▼──────┐
     │ PROBE_STATS│  Sent=15, Recv=14, Lost=1
     │ History[12]│  RTT: Min=1ms, Avg=3ms, Max=8ms
     └────────────┘
```

## Protokol Detaylari

### 1. ARP Probe

**Akis:**
1. EFI: `Arp->Delete()` ile cache'den siler (her seferinde taze exchange icin)
2. EFI: `Arp->Request(TargetIp)` ile ARP Request gondererir
3. Companion: `arp_responder` ARP Reply gondererir (otomatik, raw socket)
4. EFI: Reply gelince RTT hesaplar

**Fallback:** EFI_ARP_PROTOCOL yoksa raw SNP ile `PktBuildArpRequest()` + `Snp->Transmit/Receive`

**Companion tarafinda:**
- `ArpResponder._respond_loop()` — AF_PACKET raw socket, ETH_P_ARP filtresi
- Hedef IP eslesirse ARP Reply frame olusturup gonderir
- Her reply loglanir: `ARP REPLY to x.x.x.x (mac) - total: N`

**Not:** ARP probe mesaj payload tasimaz. Basari kriteri: ARP Reply'in gelmesi.

### 2. ICMP Probe

**Akis:**
1. EFI: IP4 Service Binding → child olustur → Configure (ICMP, TTL=64)
2. EFI: ICMP Echo Request olustur:
   - Type=8, Code=0
   - Identifier=0xDD50 (DDTECHO imzasi)
   - SequenceNumber=SeqId
   - Payload: `DDTECHO|ID=xxxx|TS=xxxxxxxx` (28 byte)
   - Checksum hesapla
3. EFI: `Ip4->Transmit()` ile gonder (ARP priming icin 3 deneme)
4. Companion: Linux kernel otomatik Echo Reply gondererir
5. Companion: `IcmpHandler._monitor_loop()` ICMP_ECHO_REQUEST yakalar
   - Identifier=0xDD50 kontrolu
   - Payload'dan DDTECHO mesajini parse eder
6. EFI: `Ip4->Receive()` ile reply al, ReplyType==0 (Echo Reply) kontrol

**IP4 Configure fallback:**
```
UseDefaultAddress=FALSE + StationAddress → basarisiz ise
UseDefaultAddress=TRUE (DHCP varsa otomatik)
```

**ARP Retry mekanizmasi:**
- TX 3 kere denenir (500ms arayla)
- Ilk denemede ARP cache bos olabilir → IP4 dahili ARP resolver calisir
- Ikinci denemede ARP cache prime olmus olur → TX basarili

### 3. UDP Probe

**Akis:**
1. EFI: UDP4 Service Binding → child olustur → Configure
   - StationPort=5001, RemotePort=5000
   - StationAddress=NIC IP, RemoteAddress=Target IP
2. EFI: Payload olustur: `DDTECHO|ID=xxxx|TS=xxxxxxxx`
3. EFI: `Udp4->Transmit()` → timer-based TX bekleme
4. Companion: `UdpEcho._echo_loop()` alir, verbatim geri gonderir
   - DDTECHO prefix tespit eder, ID parse eder
   - `UDP PROBE #xxxx from x.x.x.x:5001 (total: N)` loglar
5. EFI: `Udp4->Receive()` → timer-based RX bekleme
6. EFI: Alinan payload'in `DDTECHO` ile basladigini dogrular

**Dogrulama:** Alinan verinin ilk 7 byte'i `DDTECHO` olmali

### 4. TCP Probe

**Akis:**
1. EFI: TCP4 Service Binding → child → Configure (ActiveFlag=TRUE)
   - StationPort=0 (ephemeral), RemotePort=22
2. EFI: `Tcp4->Connect()` → 3-way handshake (timer-based bekleme)
3. Companion: `TcpListener._accept_loop()` baglanti kabul eder
4. EFI: `Tcp4->Transmit()` → Push=TRUE ile DDTECHO payload gonder
5. Companion: `_handle_client()` veriyi alir, DDTECHO parse eder, echo geri gonderir
   - `TCP PROBE #xxxx from x.x.x.x:port port 22 (total: N)` loglar
6. EFI: `Tcp4->Receive()` → payload'i dogrular
7. EFI: `Tcp4->Close()` → graceful FIN handshake (2s timeout)
8. EFI: TCP child destroy

**Her probe yeni baglanti:** TCP probe her seferinde connect-send-recv-close-destroy yapar. Bu, baglanti kurulumu dahil toplam RTT olcer.

## Veri Yapilari

### PROBE_STATS

```c
typedef struct {
  PROBE_PROTOCOL   Protocol;     // ProbeArp, ProbeIcmp, ProbeUdp, ProbeTcp
  UINT32           Sent;         // Toplam gonderilen
  UINT32           Received;     // Basarili alinan
  UINT32           Lost;         // Kayip (timeout + fail)
  UINT32           RttMinUs;     // Minimum RTT (mikrosaniye)
  UINT32           RttMaxUs;     // Maksimum RTT
  UINT32           RttAvgUs;     // Ortalama RTT (RttTotalUs / Received)
  UINT32           RttLastUs;    // Son probe RTT
  UINT64           RttTotalUs;   // Toplam RTT (ortalama hesabi icin)
  UINT32           NextSeqId;    // Sonraki sequence ID
  PROBE_ENTRY      History[12];  // Ring buffer: son 12 sonuc
  UINTN            HistoryHead;  // Ring buffer yazma indeksi
} PROBE_STATS;
```

### PROBE_ENTRY

```c
typedef struct {
  UINT32    SeqId;     // Sequence numarasi
  UINT32    Status;    // PROBE_STATUS_PASS / FAIL / TIMEOUT
  UINT32    RttUs;     // RTT mikrosaniye (0 = fail/timeout)
} PROBE_ENTRY;
```

### Ring Buffer Mantigi

History ring buffer olarak calisir. `HistoryHead` her probe sonrasi `(Head + 1) % 12` ile ilerler. Gosterimde en yeni sonuc ustte, en eski altta listelenir:

```
Idx = (HistoryHead - 1 - I + 12) % 12   // I=0 en yeni, I=11 en eski
```

## Probe Payload Formati

```
Position:  0         7 8       10 11     14 15 16      18 19         26 27
Content:   D D T E C H O | I D = x x x x | T S = x x x x x x x x \0
           ─────────────   ─────────────   ─────────────────────────
           Prefix (7)      SeqID (4 digit)  Timestamp (8 hex)
```

Toplam: 28 byte ASCII + 4 byte padding = 32 byte (`PROBE_PAYLOAD_SIZE`)

## Zamanlama

| Parametre | Deger | Aciklama |
|-----------|-------|----------|
| Probe araligi | ~1 saniye | `UiWaitKeyTimeout(1000)` ile bekleme |
| Probe timeout | 2 saniye | `PROBE_TIMEOUT_MS=2000` |
| ARP retry | 3 deneme | `Arp->Request` + poll |
| ICMP TX retry | 3 deneme | ARP priming icin |
| TCP connect timeout | 2 saniye | Timer-based |
| TCP close timeout | 2 saniye | Graceful FIN |

## RTT Hesaplama

```c
StartTick = UtilGetTimestamp();   // Gonderim oncesi
// ... send + receive ...
EndTick = UtilGetTimestamp();
RttUs = (EndTick - StartTick) * 1000000;  // Saniye → mikrosaniye
```

`UtilGetTimestamp()` EFI `GetTime()` veya `Stall`-based tick counter kullanir.

## Hata Durumlari

| Durum | Sonuc | Ekran Gosterimi |
|-------|-------|-----------------|
| Reply geldi, payload dogru | PASS | `#0015  PASS   RTT=3ms` (yesil) |
| Reply geldi, payload yanlis | FAIL | `#0015  FAIL   error` (kirmizi) |
| Timeout (2s icerisinde cevap yok) | TIMEOUT | `#0015  TIMEOUT` (sari) |
| Protokol mevcut degil | Test baslamaz | `[3] UDP4  N/A` (gri) |
| NIC'de IP config yok | ARP/SNP fallback | ARP calisir, diger protokoller basarisiz |

## UI Akisi

```
Ana Menu → [N] Network Interfaces → NIC Sec → [Enter] Detay
                                                    │
                                Protocol Stack      │
                                [1] ARP   Available  │
                                [2] ICMP  Available  ├──[1-4]──► RunProtocolEchoTest()
                                [3] UDP4  Available  │              │
                                [4] TCP4  Available  │              ├─ ProbeInit()
                                [5] DHCP4 Available  │              ├─ loop:
                                [6] DNS4  N/A        │              │    ProbeExecuteOnce()
                                [7] HTTP  N/A        │              │    Update stats display
                                                     │              │    UiWaitKeyTimeout(1000)
                                [C] Companion Test   │              │    ESC? → break
                                [ESC] Back           │              └─ return to detail
```

## Companion Tarafinda Loglama

Companion `-v` (verbose) flagi olmadan bile probe mesajlari INFO seviyesinde loglanir:

```bash
sudo python3 companion.py -i eth0 --ip 192.168.100.1

# Cikti:
13:45:01 [INFO   ] arp              : ARP REPLY to 192.168.100.10 (52:54:00:12:34:56) - total: 1
13:45:02 [INFO   ] icmp             : ICMP PROBE seq=1 from 192.168.100.10 (total: 1) [DDTECHO|ID=0001|TS=001A2B3C]
13:45:03 [INFO   ] udp_echo         : UDP PROBE #0001 from 192.168.100.10:5001 (total: 1)
13:45:04 [INFO   ] tcp              : TCP PROBE #0001 from 192.168.100.10:49152 port 22 (total: 1)
```

## Kaynak Dosyalari

| Dosya | Rol |
|-------|-----|
| `Include/ProtocolProbe.h` | Tip tanimlari, sabitler, fonksiyon prototipleri |
| `Source/ProtocolProbe.c` | 4 protokol icin probe implementasyonu |
| `Source/Main.c` | DrawNicDetail (protokol listesi), RunProtocolEchoTest (UI) |
| `Companion/services/arp_responder.py` | ARP reply + probe tracking |
| `Companion/services/icmp_handler.py` | ICMP monitoring + DDTECHO ID tespiti |
| `Companion/services/udp_echo.py` | UDP echo + DDTECHO payload parse |
| `Companion/services/tcp_listener.py` | TCP echo + DDTECHO payload parse |
