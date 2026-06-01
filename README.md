
# Contiki-NG ile Havadan Güvenli Bellenim Güncellemesi (OTA Update) Projesi

Bu proje, Kablosuz Sensör Ağlarında (WSN) yüksek paket düşme ve kayıp oranları göz önünde bulundurularak, büyük boyutlu bir bellenim (firmware) imajının bir UDP Sunucusundan (Server) UDP İstemcisine (Client) kayıpsız, güvenli ve bütünlüğü doğrulanmış bir şekilde havadan iletilmesini (Over-the-Air Update) gerçekleştirmektedir. 

Proje, Contiki-NG işletim sistemi üzerinde **Stop-and-Wait ARQ** protokolü, **CRC16 Bütünlük Kontrolü** ve **Coffee Dosya Sistemi (CFS)** entegrasyonu ile kurgulanmıştır.

---

## 📺 Proje Tanıtım ve Cooja Simülasyon Videosu

Projenin Cooja simülatör ortamında çalıştırılması, protokolün mantığı, kod yapısı ve teorik açıklamaları içeren yüksek çözünürlüklü anlatım videosuna aşağıdaki bağlantıdan ulaşabilirsiniz:

▶️ **[YOUTUBE VİDEO LİNKİNİZİ BURAYA YAPIŞTIRIN]**

---

## 🛠️ Bölüm 1: Protokol Tasarımı, Veri Yapısı ve Hata Yönetimi

### 1.1. Güvenli Aktarım Yöntemi (Stop-and-Wait ARQ)
Kablosuz sensör ağlarındaki paket kayıplarını önlemek adına **Dur-ve-Bekle (Stop-and-Wait ARQ)** protokolü tasarlanmıştır. Bu protokolün temel mantığı, istemci tarafından talep edilen bir veri bloğu sunucudan başarıyla gelip onaylanmadan (ACK) bir sonraki bloğa geçilmemesidir.

* **Parçalama (Fragmentation):** İletilecek bellenim verisi, ağ katmanındaki Maksimum Taşıma Birimi (MTU) sınırlarını aşmamak ve bellek taşmalarını önlemek adına **64 Byte'lık sabit bloklara** bölünmüştür.
* **Blok Numaralandırma:** İletilen her paketin başına, paketlerin sırasını takip edebilmek ve mükerrer paketleri ayırt edebilmek amacıyla 2 Byte boyutunda benzersiz bir sıra numarası (`block_number`) eklenmiştir.

### 1.2. Ağ Veri Yapısı (Struct) ve Paket Uzunluğu
Ağ üzerinden gönderilen paketlerin donanımsal hizalamaya uygun olması için aşağıdaki C veri yapısı tanımlanmıştır:

```c
struct firmware_packet {
  uint16_t block_number; // 2 Byte - Sıra Numarası
  uint8_t data[64];      // 64 Byte - Firmware Ham Verisi
  uint16_t checksum;     // 2 Byte - CRC16 Bütünlük Kontrol Değeri
};
```
Toplam Paket Boyutu: 2 Byte + 64 Byte + 2 Byte = 68 Byte düzeyindedir. Bu optimize edilmiş boyut, IEEE 802.15.4 standartlarındaki MTU (127 Byte) sınırının oldukça altındadır ve paketlerin havada parçalanmasını (fragmentation) engeller.

### 1.3. Alınan Önlemler ve Güvenlik Mekanizmaları
#### A. Veri Bütünlüğü Kontrolü (CRC16)
Paketlerin havada bozulma riskine karşı lib/crc16.h kütüphanesi kullanılarak CRC16 doğrulama algoritması entegre edilmiştir. Sunucu, paketi göndermeden önce 64 Byte'lık verinin CRC16 değerini hesaplar ve checksum alanına yazar. İstemci paketi aldığında aynı hesaplamayı yapar; eğer değerler eşleşmiyorsa paket bozulmuş demektir ve sessizce düşürülerek ARQ mekanizması tetiklenir.

// Sunucu Tarafı CRC16 Hesaplama Örneği
```c
pkt.block_number = current_block;
memcpy(pkt.data, local_firmware_buffer, 64);
pkt.checksum = crc16_data(pkt.data, 64, 0); // Bütünlük koruma önlemi
```
#### B. Zaman Aşımı ve Yeniden İletim (Timeout & Retransmission)
Ağda bir paket veya ACK mesajı düştüğünde sistemin kilitlenmesini önlemek amacıyla istemci tarafında etimer (event timer) yapısı ile zaman aşımı mekanizması kurulmuştur. Eğer belirlenen süre (örneğin 2 saniye) içinde beklenen blok numarasına sahip paket gelmezse, istemci sunucuya aynı blok için yeniden istek (Request) fırlatır.

#### C. Kalıcı Depolama (Coffee Dosya Sistemi - CFS)
Gelen bellenim blokları RAM üzerinde tutulamaz çünkü RAM geçicidir ve boyutu sınırlıdır. Alınan önlem olarak, istemci düğüm gelen her doğrulanmış bloğu Contiki-NG'nin Coffee Dosya Sistemi (CFS) kütüphanelerini kullanarak flash bellekte açılan new-firmware.bin dosyasına ardışık olarak yazar.

// İstemci Tarafı Kalıcı Hafızaya Yazma Örneği
```c
int fd = cfs_open("new-firmware.bin", CFS_WRITE | CFS_APPEND);
if(fd >= 0) {
  cfs_write(fd, pkt.data, 64);
  cfs_close(fd);
}
```
💻 Proje Klasör Yapısı ve Derleme
Proje kaynak kodları Contiki-NG/examples/proje/ dizini altında yer almaktadır:

udp-client.c: Stop-and-Wait isteklerini yapan, CRC kontrolü gerçekleştiren ve CFS ile hafızaya yazan istemci kodu.

udp-server.c: İstemciden gelen isteklere göre bellenim bloklarını CRC16 imzalayarak gönderen sunucu kodu.

Makefile: Contiki-NG derleme yapılandırma dosyası.

Derleme Komutu (Z1 Mote Target):
make TARGET=z1 udp-client udp-server

## 📊 Bölüm 2: Araştırma İş Parçacığı (ELF ve Donanım Araç Zinciri Analizi)
Bu bölümde, derleme sonucunda üretilen ikili (binary) bellenim imajlarının iç yapısı, GNU binutils araçları (readelf, nm) vasıtasıyla çözümlenmiş ve hedef platform mimarisine göre haritalandırılmıştır.

### 2.1. Dosya Kimliği ve Mimari (ELF Header)
msp430-readelf -h komutu ile .z1 uzantılı bellenim dosyasının başlık verileri çözümlenmiştir. Çıktılardan elde edilen verilere göre dosyanın Sihirli Sayıları (Magic Numbers) incelendiğinde, bu dosyanın doğrudan donanıma yazılmaya uygun bir ham binary (raw binary) olmadığı; sembol tablolarını (.symtab), adres haritalarını ve debug verilerini içeren ELF (Executable and Linkable Format) biçiminde olduğu kanıtlanmıştır.

Dosyanın gerçek donanıma (örneğin CC1352R veya Z1 mote) yüklenebilmesi için öncelikle msp430-objcopy veya arm-none-eabi-objcopy gibi araçlar kullanılarak meta verilerinden arındırılması ve sadece makine kodlarını içeren saf .bin veya .hex formatına dönüştürülmesi gerekmektedir. Dosyanın giriş noktası (Entry Point) ise 0x3100 mimari adresi olarak tespit edilmiştir.

### 2.2. Bellek ve Disk Uzayı Analizi (readelf -S Çıktısı)
readelf -S komutu ile elde edilen bellenim kesit (section) boyutları, ödev şartnamesinde belirtilen kural uyarınca hedef donanım olan Texas Instruments CC1352R SoC (352 KB Flash / 80 KB SRAM) fiziksel bellek sınırlarına haritalandırılmış ve aşağıda görselleştirilmiştir:

| Bellenim Bölümü (Section) | Analiz Boyutu (Byte) | CC1352R Fiziksel Bellek Türü | CC1352R Standart Adres Aralığı | CC1352R Bellek Doluluk Oranı (%) |
| :--- | :--- | :--- | :--- | :--- |
| **`.text` (Kod Alanı)** | 38.766 Byte | Flash Bellek (ROM) | `0x00000000 - 0x0000976D` | ~%11.0 (352 KB Sınırı İçinde) |
| **`.rodata` (Sabit Veriler)** | 13.821 Byte | Flash Bellek (ROM) | `0x0000976E - 0x0000CD6B` | ~%3.9 (352 KB Sınırı İçinde) |
| **`.vectors` (Kesme Vektörleri)** | 64 Byte | Flash Bellek (ROM) | `0x00057FC0 - 0x00058000` | ~%0.01 (M4 Çekirdek Alanı) |
| **`.data` + `.bss` (Değişkenler)** | 6.040 Byte | SRAM (Sistem RAM) | `0x20000000 - 0x20001797` | ~%7.5 (80 KB Sınırı İçinde) |

Teknik Değerlendirme: İncelenen bellenimin toplam Flash depolama ihtiyacı (.text + .rodata) yaklaşık 52.5 KB düzeyindedir. Hedef platform CC1352R donanımı bünyesinde 352 KB Flash barındırdığından, ağ üzerinden ilettiğimiz bu bellenim imajı, cihazın toplam kalıcı depolama alanının sadece %15'ini işgal edecektir. Bu durum, donanım üzerinde aynı anda hem mevcut çalışan bellenimin hem de yeni gelen bellenimin saklanabileceği Çift İmaj (Dual-Image / Staging Area) mimarisinin kurulması için fazlasıyla yeterli alan olduğunu kanıtlar. Ayrıca, bellenimin 6 KB olan RAM gereksinimi, CC1352R'nin 80 KB'lık SRAM kapasitesinin yalnızca %7.5'ini tüketecektir. Bu sayede, havadan güncelleme (OTA) esnasında alıcı cihazda herhangi bir RAM veya yığın (stack) taşması darboğazı yaşanmayacaktır.

### 2.3. Sembol Analizi (nm Çıktısı)
msp430-nm aracı ile bellenim içerisindeki sembol tabloları incelenmiş, sistemin arka planda hangi süreçleri (process) koşturduğu analiz edilmiştir:

hello_world_process (Adres: 0x114a): Uygulama katmanındaki temel süreci temsil eder.

uip_process (Adres: 0x130f2): İşletim sisteminin hafif nitelikli bir ağ yığını (uIP - TCP/IP stack) barındırdığını doğrular.

cc2420_process (Adres: 0x110c): Kablosuz haberleşme için CC2420 telsiz çipi sürücüsünün aktif yüklendiğini kanıtlar.

Ayrıca projenin 1. aşamasında koda entegre ettiğimiz bütünlük kontrolünü sağlayan crc16_data fonksiyonu ve flash bellek yönetimini üstlenen cfs_open, cfs_write, cfs_close kütüphane sembolleri de derleme sonrası başarıyla tabloya dahil edilerek işlevselliğin sembolik seviyede kazanıldığı doğrulanmıştır.

## Bölüm 3: CC1352R Gerçekleme ve Donanım Uyarlama Analizi
Bu bölüm, simülasyon ortamında test edilen sistemin gerçek hayattaki modern bir IoT platformu olan Texas Instruments CC1352R SimpleLink SoC donanımına taşınması durumunda yapılması gereken mimari ve donanımsal uyarlamaları analiz etmektedir.

### 3.1. Mimari Geçiş ve Derleme Araç Zinciri (Toolchain) Değişimi
Z1 mote platformu 16-bit MSP430 tabanlı eski bir mikrodenetleyiciye sahipken, Texas Instruments CC1352R platformu 32-bit ARM Cortex-M4F ana işlemci çekirdeğine sahiptir.

Derleyici Değişimi: Z1 platformu için kullanılan msp430-gcc araç zinciri, yerini ARM tabanlı arm-none-eabi-gcc derleyicisine bırakacaktır.

Contiki-NG Derleme Komutu: Gerçek donanım uyarlaması için projenin derleme komutu şu şekilde revize edilmelidir:
make TARGET=cc13xx-cc26xx BOARD=launchpad/cc1352r1 udp-client udp-server
Bellek Hizalaması (Padding Prevention): 32-bit mimaride pointer ve int yapıları 4 byte boyutuna yükselir. Ağ paket yapımızın (struct firmware_packet) elemanları arasında derleyicinin boşluk bırakmasını (padding) engellemek adına yapı sonuna __attribute__((packed)) makrosu eklenerek sıkıştırma sağlanmalıdır.

### 3.2. Flash Sektör Yapısı ve CFS Yapılandırması
CC1352R donanımında dahili Flash bellek 4 KB'lık (4096 Byte) sabit sektörlere bölünmüştür. Bir sektöre yeni veri yazılmadan önce, o sektörün donanımsal olarak tamamen silinmesi (Page Erase) gerekir.

Alınan Önlem: Geliştirilen bellenim aktarım kodunun CC1352R üzerinde flash belleğe zarar vermemesi ve yüksek verimle çalışması için cfs-coffee-arch.h dosyası üzerinden Coffee'nin sayfa boyutu (COFFEE_PAGE_SIZE) 256 Byte, sektör boyutu (COFFEE_SECTOR_SIZE) ise donanıma tam uyumlu olarak 4096 Byte (4 KB) olarak re-konfigüre edilmelidir. Böylece flash belleğin yıpranma ömrü (wear-leveling) korunmuş olur.

### 3.3. Donanım Seviyesinde Yeniden Başlatma (Reboot) ve Bootloader Mantığı
10 bloğun tamamı başarıyla indirilip Coffee dosya sistemine kaydedildikten sonra cihazın yeni bellenimle açılması süreci gerçek donanımda şu adımlarla tetiklenir:

Bellenim Taşıma (Flipping): Coffee içerisindeki binary imaj okunarak cihazın aktif bellenim çalışma alanına (Internal Flash Active Region) kopyalanır.

Yazılımsal Reset (Software Reset): Kopyalama bittikten sonra ARM Cortex-M çekirdeğine özel olan NVIC_SystemReset() fonksiyonu çağrılarak donanımsal reset tetiklenir.

CCFG Alanının Rolü: Cihaz yeniden başladığında, ROM tabanlı gömülü Bootloader mimarisi devreye girer. İşlemci, Flash belleğin en son sektöründe yer alan ve cihazın açılış parametrelerini saklayan CCFG (Customer Configuration) bölgesini okur. Bootloader, CCFG'deki yönergelere bakarak yeni yüklenen bellenimin giriş adresine (Entry Point) program sayacını (PC) dallandırır ve cihaz güncel kodla uyanır.

### 3.4. Güç Yönetimi (Energest Analizi) ve Telsiz Sürücüleri
Sürücü Değişimi: Simülasyondaki eski CC2420 telsiz sürücüsü, yerini CC1352R'nin yerleşik, yüksek performanslı Proprietary RF / IEEE 802.15.4 radyo alt sistemine (RF Core) bırakır.

Enerji Tasarrufu: CC1352R gelişmiş donanımsal uyku modlarına (Standby/Idle) sahiptir. Tasarladığımız Stop-and-Wait ARQ protokolünün timeout süreleri boyunca ve pasif paket bekleme pencerelerinde Contiki-NG'nin güç yönetim mekanizması telsizi otomatik olarak Düşük Güç Moduna (LPM) çeker. Telsiz sadece paketin havadan geldiği mikro saniyelik anlarda tam güce geçer, bu da pil ömrünü dramatik ölçüde uzatır.
