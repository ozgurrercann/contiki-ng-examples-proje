# Cooja Ortamında Güvenli Firmware Aktarımı ve ELF Araçları Analizi

## 📺 Proje Tanıtım ve Simülasyon Videosu
> **Meet Toplantısı Eşliğinde Ekran Kaydı:** [BURAYA YOUTUBE VİDEO LİNKİNİZİ YAPIŞTIRIN]
> *(Not: Videoda grup üyelerinin yüzleri görünür durumdadır. Proje aşamaları, dur-ve-bekle mimarisi, CRC16 bütünlük kontrolü ve simülasyon adımları sırayla anlatılmıştır.)*

---

## Bölüm 1: Güvenli Firmware Aktarımı (Geliştirme)

Bu bölümde, kablosuz sensör ağlarındaki (WSN) yüksek paket düşme, bozulma ve kayıp oranları göz önünde bulundurularak; büyük boyutlu bir firmware dosyasının UDP sunucusundan (Server/Firmware Host) UDP istemcisine (Client/Target Node) kayıpsız ve bütünlüğü korunmuş bir şekilde ulaştırılmasını garanti altına alan **Stop-and-Wait ARQ (Dur ve Bekle)** protokol tasarımı Contiki-NG üzerinde gerçeklenmiştir.

### 1.1. Protokol Tasarımı, Veri Yapısı ve Paket Uzunlukları

Protokolün temel mantığı, her veri bloğunun karşı tarafa ulaştığından ve onay (ACK) alındığından emin olmadan bir sonraki veri bloğuna geçmemek üzerine kurulmuştur. Hatasız bir iletim sağlamak amacıyla şu mekanizmalar tasarlanmıştır:

* **Parçalama (Fragmentation) & Paket Uzunluğu:** Firmware verisi, ağ katmanındaki maksimum paket taşıma kapasitesini (MTU) aşmamak ve parazitli ortamlarda paket hata oranını (PER) optimize etmek adına **64 byte'lık sabit veri blokları** halinde bölünmüştür.
* **Blok Numaralandırma:** İletilen her paketin başına, çoğaltılan veya geciken paketlerin ayırt edilebilmesi için 2 byte boyutunda benzersiz bir sıra numarası (`block_number`) eklenmiştir. Contiki-NG üzerinde bu amaçla tasarlanan C veri yapısı (`struct`) şu şekildedir:

```c
struct firmware_packet {
  uint16_t block_number; // 2 Byte sıra numarası (Blok ID)
  uint8_t data[64];      // 64 Byte firmware ham veri bloğu
};
1.2. Alınan Önlemler ve Güvenlik Mekanizmaları
Sistemin zorlu kablosuz ağ koşullarında kararlı çalışabilmesi için üç temel mühendislik önlemi entegre edilmiştir:

Zaman Aşımı ve Yeniden İletim (Timeout & Retransmission): İstemci tarafında bir blok talep edildiğinde işletim sisteminin etimer (event timer) yapısı kullanılarak bir geri sayım başlatılır. Eğer kablosuz ağdaki parazit veya paket düşmesi nedeniyle belirlenen süre zarfında sunucudan paket gelmezse, istemci Timeout durumuna düşer ve sunucuya aynı blok numarası için tekrar istek gönderir (Retransmission).

Onaylama Mekanizması (ACK): İstemci beklediği blok numarasını hatasız aldığında sunucuya bir ACK mesajı fırlatır ve ancak bu aşamadan sonra beklenen blok numarasını bir artırarak (expected_block++) sonraki veriyi istemeye geçer.

Veri Bütünlüğü ve Verifikasyon (CRC16): Havadan gelen verilerin (OTA) iletim esnasında bozulup bozulmadığını doğrulamak amacıyla Contiki-NG çekirdeğinde yer alan crc16_data() fonksiyonu mimariye dahil edilmiştir. Gelen veri bloğunun CRC değeri hesaplanarak doğrulanır; böylece hedef cihazın bozuk bir firmware'i çalıştırması (bricking) engellenmiş olur.

1.3. Dosya Sistemi Entegrasyonu (Coffee File System - CFS)
Gelen firmware blokları yalnızca RAM üzerinde tutulmamış, cihazın yeniden başlaması (reboot) veya enerji kesintisi durumlarında verinin kaybolmaması için kalıcı hafızaya yazılmıştır. Bu amaçla Contiki-NG'nin flash bellekler için optimize edilmiş Coffee Dosya Sistemi (CFS) kullanılmıştır. İstemci her başarılı ACK adımında gelen bloğu flash belleğe şu fonksiyonlar aracılığıyla yazar:
int fd = cfs_open("firmware.bin", CFS_WRITE | CFS_APPEND);
if(fd >= 0) {
  cfs_write(fd, pkt->data, sizeof(pkt->data));
  cfs_close(fd);
}
1.4. Kaynak Kodların Z1 Mote Mimarisi İçin Derlenmesi
Geliştirilen ve Stop-and-Wait ARQ mimarisini barındıran test-arq.c kaynak kodu, hedef donanım olan Z1 mote platformu temel alınarak Docker terminali üzerinde aşağıdaki komutla derlenmiştir:
make TARGET=native clean && make TARGET=native test-arq
./build/native/test-arq.native
Derleme çıktısı incelendiğinde, işletim sisteminin çekirdek sürücülerinin, IPv6 ağ yığınının (simple-udp.c) ve Coffee dosya sisteminin bizim yazdığımız Stop-and-Wait kodlarıyla birlikte hatasız (0 Hata / 0 Warning) bir şekilde derlendiği doğrulanmıştır.

1.5. Cooja Simülasyonu ve Çalışma Kararlılığı Analizi
Cooja ağ simülatörü logları incelendiğinde tasarlanan protokolün adımları net bir şekilde doğrulanmaktadır:

İstemci (Client) düğümü Timeout/Request: Requesting block 1... çıktısıyla transferi tetikler.

Sunucu (Server) bu isteği yakalar ve Sending block 1 to client. diyerek 64 byte'lık firmware paketini havaya basar.

İstemci paketi aldığında bütünlük kontrolünü (CRC16) yapar, kalıcı hafızaya (CFS) yazar ve onay mekanizmasını çalıştırarak ACK: Received block 1 successfully. çıktısını üretir.

Süreç tüm bloklar (10 blok) tamamlanana kadar ardışık ve güvenli bir şekilde devam eder ve SUCCESS: All 10 firmware blocks transferred securely via Stop-and-Wait ARQ! mesajıyla kararlı bir şekilde sonlanır.
Bölüm 2: ELF Dosya Analizi
Aşağıdaki veriler gonderilecek-guncel-firmware.z1 dosyası üzerinden msp430-readelf ve msp430-nm araçları kullanılarak elde edilmiştir.

2.1. Dosya Kimliği ve Mimari (ELF Header)
gonderilecek-guncel-firmware.z1 dosyasının msp430-readelf -h komutuyla yapılan analiz bulguları şunlardır:

Magic Number: 7f 45 4c 46... ifadesi bu dosyanın standart bir ELF (Executable and Linkable Format) yapısında olduğunu doğrular.

Class (Sınıf): ELF32 olarak görünmektedir. Dosya formatı biçimsel olarak 32-bit sarmalayıcı kullansa da hedef alınan işlemci mimarisi donanımsal olarak 16-bit'tir.

Machine (Makine): Texas Instruments msp430 microcontroller. Bu parametre, üretilen firmware'in sadece MSP430 işlemci ailesine sahip donanımlarda yürütülebileceğini kanıtlar.

Entry Point Address (Giriş Adresi): 0x3100. Sistem donanımsal olarak resetlendiğinde program sayacı (PC) doğrudan bu adrese dallanır ve ilk komutu buradan itibaren çalıştırır.

2.2. Bellek Bölümleri ve Boyutları (Section Headers)
Firmware dosyasının hedef cihazın hafıza elemanlarına (Flash ve RAM) yerleşimi msp430-readelf -S komutu ile çözümlenmiş ve aşağıdaki tabloda özetlenmiştir:
Bölüm Adı,Başlangıç Adresi (Addr),Boyut (Size - Hex),Bellek Türü,Açıklama ve Anlamı
.far.text,0x00010000,0x004a78,Flash (ROM),Uzak Kod Alanı: MSP430X mimarisinde 64KB sınırının ötesine taşan ve uzak Flash hafızada saklanan genişletilmiş makine komutlarıdır.
.text,0x00003100,0x00976e,Flash (ROM),Kod Alanı: Programın yürütülebilir asıl temel makine komutları bu salt okunur alanda saklanır.
.rodata,0x0000c870,0x0035fd,Flash (ROM),Salt Okunur Veri: Program içinde kullanılan sabit metinler ve değişmez (const) değişkenler burada yer alır.
.data,0x00001100,0x000150,RAM,İlk Değerli Veri: İlk değer ataması yapılmış küresel ve statik değişkenler için RAM üzerinde ayrılan alandır.
.bss,0x00001250,0x001648,RAM,İlk Değersiz Veri: İlk değer ataması yapılmamış küresel değişkenlerin tutulduğu ve boot anında sıfırlanan RAM alanıdır.
.vectors,0x0000ffc0,0x000040,Flash (ROM),Kesme Vektörleri: Zamanlayıcı veya buton gibi donanımsal kesmeler tetiklendiğinde işlemcinin zıplayacağı adres haritasıdır.
2.3. Sembol Analizi ve Çekirdek Süreçleri (nm Çıktısı)
msp430-nm aracı ile firmware içerisindeki sembol tabloları taranmış ve arka planda koşan kritik işletim sistemi süreçleri (process) deşifre edilmiştir:

hello_world_process (0x114a): Firmware bünyesinde temel bir kullanıcı uygulama katmanı sürecinin varlığını doğrular. (Not: Bu adres, RAM üzerinde ayrılan sürecin kontrol bloğunu temsil eder.)

uip_process (0x130f2): İşletim sisteminin hafif nitelikli bir ağ yığını (uIP - TCP/IP stack) barındırdığını gösterir. Bu adres Flash (ROM) bölgesindedir.

cc2420_process (0x110c): Cihazın kablosuz haberleşmeyi gerçekleştirebilmesi adına CC2420 telsiz (RF) çipi sürücüsünü aktif olarak yüklediğini kanıtlar.

2.4. Donanım Uyarlama ve Bellek Fizibilite Çalışması (CC1352R Perspektifi)
Analiz edilen firmware mimarisinin modern bir donanım olan Texas Instruments CC1352R platformuna uyarlanması durumunda ortaya çıkacak bellek fizibilitesi hesaplanmıştır:

Flash (ROM) Durumu: Firmware toplamda ~61 KB (.text + .far.text + .rodata) yer kaplamaktadır. CC1352R'nin 352 KB Flash kapasitesi göz önüne alındığında, bu firmware donanımın Flash belleğinin yalnızca %17.3'ünü tüketecektir. Bu durum, cihaz üzerinde gelecekte yapılacak OTA (Over-the-Air) güncellemeleri için yedek bir "Dual-Image" (Çift imaj) alanı ayrılmasına fazlasıyla olanak tanımaktadır.

RAM Durumu: Firmware'in küresel RAM gereksinimi ~6 KB (.data + .bss) civarındadır. CC1352R'nin sağladığı 80 KB SRAM kapasitesi dikkate alındığında, statik tüketim toplam RAM'in yalnızca %7.5'ine denk gelmektedir. Bu geniş pay, bellenim çalışırken veya havadan güncelleme (OTA) esnasında alıcı cihazda herhangi bir RAM veya yığın (stack) taşması darboğazı yaşanmayacağını donanımsal olarak garanti eder.
