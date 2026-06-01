# 📑 TI CC1352R OTA GENİŞLETME TEKNİK KATALOĞU
**Saha Elemanları ve Sistem Entegratörleri İçin Devreye Alma Rehberi** *Sürüm: v1.0.4 | Hedef Platform: Texas Instruments CC1352R SimpleLink™ SoC*

---

## 1. SİSTEME GENEL BAKIŞ
Bu katalog, sahada pille çalışan **Texas Instruments CC1352R** kablosuz sensör düğümlerinin (Node), merkezi bir UDP Sunucusu üzerinden havadan güvenli bellenim güncelleme (OTA Update) işlemlerini gerçekleştirmek amacıyla hazırlanmıştır. 

Sistem, endüstriyel ortamlardaki yüksek elektromanyetik parazit ve paket düşme oranlarına karşı **Stop-and-Wait ARQ** mimarisi ve donanımsal **CRC16 hata doğrulama** algoritmaları ile donatılmış olup verinin sıfır hata ile kalıcı hafızaya (Flash) yazılmasını garanti eder.

---

## 2. DONANIM VE MİMARİ ÖZELLİKLERİ
Sahada entegrasyonu yapılacak CC1352R mikrodenetleyici platformunun teknik kısıtları ve bellek haritası parametreleri aşağıdaki gibidir:

* **Ana İşlemci:** 32-bit ARM® Cortex®-M4F (48 MHz)
* **Fiziksel Flash Depolama:** 352 KB
* **Sistem SRAM Kapasitesi:** 80 KB
* **Telsiz Frekansı:** Sub-1 GHz ve 2.4 GHz Çoklu Protokol (IEEE 802.15.4)

### 📊 Bellenim (Firmware) Bellek Dağılım Tablosu
Havadan gelecek olan güncel bellenim imajının (`new-firmware.bin`) cihaz üzerindeki sektörel doluluk oranları ve fiziksel adres mimarisi şu şekildedir:

| Katman / Kesit (Section) | Fiziksel Boyut | Donanım Bellek Türü | Standart Adres Aralığı | Sektörel Doluluk Oranı |
| :--- | :--- | :--- | :--- | :--- |
| **`.text` (Sistem Makine Kodları)** | 38.766 Byte | Dahili Flash (ROM) | `0x00000000 - 0x0000976D` | ~%11.0 (Güvenli Sınırda) |
| **`.rodata` (Sabit/Değişmez Veriler)**| 13.821 Byte | Dahili Flash (ROM) | `0x0000976E - 0x0000CD6B` | ~%3.9 (Güvenli Sınırda) |
| **`.vectors` (Kesme Vektör Tablosu)**| 64 Byte | Dahili Flash (ROM) | `0x00057FC0 - 0x00058000` | ~%0.01 (M4 Özel Alanı) |
| **`.data` + `.bss` (Değişkenler)** | 6.040 Byte | Sistem SRAM (RAM) | `0x20000000 - 0x20001797` | ~%7.5 (Yüksek RAM Toleransı)|

> ⚠️ **Saha Notu:** Yeni yüklenecek firmware imajı toplamda ~52.5 KB yer kaplamaktadır. CC1352R'nin 352 KB'lık geniş kapasitesi sayesinde, cihazda **Dual-Image (Çift İmaj)** yapısı desteklenir. Yani eski yazılım çalışmaya devam ederken, yeni yazılım yan taraftaki boş sektöre güvenle indirilebilir.

---

## 3. SAHA KURULUMU VE YAPILANDIRMA (CONFIG)

### 3.1. Ağ Paket Yapısı (Struct Layout)
Saha elemanlarının ağ paketlerini osiloskop veya sniffer (Wireshark) ile incelerken hatasız çözümleme yapabilmesi için tanımlanan paket yapısı aşagıdaki gibidir:

* **Header (2 Byte):** `block_number` (0 - 65535 arası blok sıra numarası)
* **Payload (64 Byte):** `data` (Firmware binary parçası)
* **Checksum (2 Byte):** `checksum` (64 Byte'lık verinin CRC16 değeri)
* **Toplam Paket Boyutu:** **68 Byte** (IEEE 802.15.4 standartlarındaki 127 Byte'lık MTU sınırının altında kalacak şekilde optimize edilmiştir, havada parçalanmaz).

### 3.2. Flash Bellek Sektör Konfigürasyonu
CC1352R donanımında yazma işlemi mantıksal sayfalarla, silme işlemi ise **4 KB'lık (4096 Byte) fiziksel sektörlerle** yapılır. Sistem devreye alınırken `cfs-coffee-arch.h` mimari dosyası altındaki parametrelerin saha ayarları şu şekilde kalibre edilmelidir:

```c
#define COFFEE_PAGE_SIZE      256   // Mantıksal sayfa boyutu (256 Byte)
#define COFFEE_SECTOR_SIZE   4096   // Fiziksel sektör silme boyutu (4 KB)
```
Bu ayar, ağdan gelen 64 Byte'lık her blok yazıldığında flash belleğin gereksiz yere silinmesini (Page Erase) engeller ve donanımın yıpranma ömrünü (Wear-Leveling) maksimuma çıkarır.
## 4. GÜNCELLEME AKIŞI VE ÇALIŞMA PRENSİBİ
Saha uygulamalarında güncelleme işlemi başladığı andan itibaren arka planda şu 4 aşamalı otomasyon süreci koşturulur:
[1. ARQ Transfer] ---> [2. CRC16 Kontrol] ---> [3. Coffee CFS Kayıt] ---> [4. NVIC_SystemReset]
1. İstek ve Blok Transferi (Stop-and-Wait): İstemci cihaz sunucudan 0. bloğu ister. Blok gelip onaylanmadan (ACK fırlatılmadan) bir sonraki bloğa geçilmez. Paket yolda düşerse 2 saniyelik etimer zaman aşımı tetiklenir ve istemci aynı bloğu tekrar talep eder.

2. Bütünlük Doğrulama (CRC16): Alınan her bloğun verisi lib/crc16.h ile taranır. Eğer havada bir bozulma varsa paket sessizce düşürülür, kayıt engellenir.

3. Kalıcı Depolama (Coffee CFS): Doğrulanan paket verileri RAM'de biriktirilmeden, cfs_open() ve cfs_write() fonksiyonları aracılığıyla doğrudan Flash bellekteki new-firmware.bin dosyasına ardışık yazılır.

4. Donanımsal Reset ve Bootloader Dallanması: Toplam 10 blok kayıpsız tamamlandığında istemci düğüm içindeki eski süreçler sonlandırılır ve NVIC_SystemReset() fonksiyonu çağrılarak 32-bit ARM Cortex çekirdeğine donanımsal reset atılır. Cihaz uyanırken Flash belleğin son sektöründeki CCFG (Customer Configuration) tablosunu okur ve ROM tabanlı entegre Bootloader program sayacını (PC) yeni bellenimin giriş adresine (0x3100) yönlendirerek sistemi yeni kodla ayağa kaldırır.
## 5. ENERJİ YÖNETİMİ VE SAHA OPTİMİZASYONU
Endüstriyel pille çalışan düğümler için OTA güncelleme süreçleri yüksek enerji tüketir. CC1352R donanımına özel olarak tasarlanan bu yazılım mimarisinde pil ömrünü korumak adına şu önlemler alınmıştır:

Donanımsal Radyo İşlemcisi (RF Core): Simülasyondaki yazılımsal telsiz emülasyonları yerine, CC1352R'nin kendi içindeki akıllı Proprietary RF Core donanımı kullanılır. Sinyal kalitesini otomatik ayarlayarak çarpışmaları (Collision) azaltır ve yeniden iletim (Retransmission) sayılarını düşürür.

Dinamik Düşük Güç Modu (LPM / Energest): Contiki-NG işletim sisteminin Energest güç yönetim modülü protokolümüzle entegre çalışır. İstemci sunucudan paket beklediği veya zaman aşımı saydığı pasif bekleme sürelerinde telsiz donanımını açık tutmaz; telsizi otomatik olarak Standby/Low Power Mode (LPM) seviyesine çeker. Telsiz sadece milisaniyelik paket alma ve ACK gönderme pencerelerinde aktif moda (Active Mode) geçer. Bu sayede saha elemanlarının pil değişimi operasyon maliyetleri minimuma indirgenmiş olur.
