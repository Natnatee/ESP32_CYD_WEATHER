# ESP32 CYD Weather

แดชบอร์ดตั้งโต๊ะสำหรับ ESP32-2432S028 / HW-458 จอ ILI9341 แนวนอน 320x240 **เชื่อมข้อมูลอากาศและราคา API จริงแล้ว** ไม่ต้องใช้ API key; firmware build ผ่าน แต่ UI/ทัช/TLS บนบอร์ดรอบใหม่นี้ยังต้องทดสอบจริง

## Features
- ครั้งแรกหรือไม่มี calibration ที่ใช้ได้: พื้นดำ วงกลม 4 มุมพร้อมจุดกลาง/ลูกศรเล็ก ข้อความ `Click center of circles` Nunito Bold26px; บันทึกเมื่อครบ4มุมและfitผ่าน บูตครั้งต่อไปโหลดจาก NVS แล้วข้ามหน้าคาลิเบรต
- แตะ **จุดกลางวงกลม ไม่ใช่ลูกศร** มุมไหนก่อนก็ได้ วงกลมเป็นเขียวเมื่อรับค่าทัชที่นิ่งและปล่อยนิ้วแล้ว ครบ 4 มุมและตรวจ geometry ผ่านจะแสดงเขียว 600 ms ก่อนเข้า WiFi (ไม่บล็อก)
- หน้า WiFi แบบเรียบง่าย: SSID, Password แสดงข้อความจริงตามคำขอ และ Connect/Cancel ไม่มีไอคอน/คำอธิบายจิ๋ว ฟอนต์ Nunito Bold 18px
- คีย์บอร์ดแยกตัวอักษร QWERTY 3 แถว กับตัวเลข/สัญลักษณ์ 2 ชุด ปุ่มอักษรสูง 29px (เดิม 21px) พร้อม Space/Del
- บูตลอง WiFi ที่เคยเชื่อมสำเร็จอัตโนมัติ มี spinner/Cancel/timeout20s เดิม ถ้าล้มเหลวกลับฟอร์มที่แก้ SSID/Password ได้ ไม่เขียนทับข้อมูลที่บันทึกไว้
- เปิดแดชบอร์ดเฉพาะเมื่อ `WL_CONNECTED`; Wi-Fi หลุดกลับหน้าเชื่อมต่อ
- แดชบอร์ด **LVGL 8.3.11** เต็มจอ 5 หน้า midnight/neon ไม่มี navbar: หุ้นไทย → ทองไทย → น้ำมันไทย → อากาศกรุงเทพ → นาฬิกา cyberpunk
- **ปัดซ้าย/ขวาเปลี่ยนหน้า** แตะเลือกหุ้น SET / PTT / PTG / AOT / ADVANC / CPALL หรือสถานี PTT / PT (PTG) / Bangchak; แตะพื้นหลังไม่เปลี่ยนหน้า ต้องปล่อย Connect ก่อนใช้งาน ไม่เลือก tab หลัง swipe/กดค้าง
- หุ้นราคาล่าสุด/กราฟ observations จริงจาก Yahoo พร้อมวันที่; **หน้าทองตัดกราฟออกแล้ว** ใช้ราคา96.5% ซื้อ/ขายตัวเลขใหญ่44px ภาพทองแท่ง และเวลาปรับราคาที่เด่นขึ้น พร้อมวันที่/สถานะcached
- น้ำมัน Gasohol95/91/E20/DieselB7 แยก12ราคาของ3สถานี ตัวเลขแดงแบบปั๊ม7segment มีตราบนหน้าปั๊ม: PTTเปลวน้ำเงิน/แกนแดง, PTตัวอักษรแดงเอียงในวงแหวนเขียวสองสี, Bangchakตราleaf-loop/gray sweep/orange capพร้อมคำ `bangchak` ใช้ภาพRGB565 antialias60×56px ไม่ใช่เส้นprimitiveหนาแบบเดิม (PTT/PTเป็นreproductions; บางจากใช้SVGทางการจัดวางใหม่)
- อากาศมีแอนิเมชันตามWMOจริง: รัศมีดวงอาทิตย์หมุน เมฆลอย ฝน/หิมะตก หมอก ฟ้าแลบ/ลูกเห็บตามชนิด ตั้งเป้า25fps/40ms คำนวณจากelapsed millisเฉพาะกรอบภาพเมื่อเปิดหน้าอากาศ ไม่ขยับเมื่อข้อมูลขาดหรือWMOไม่รู้จัก
- HTTPS worker แยกจาก main UI พร้อม timeout/trustedCA/snapshot เก็บค่าจริงล่าสุดใน RAM; ไม่มีข้อมูลใช้ -- ไม่ยืมสินค้าหรือสถานีอื่น Fetchพลาดแสดง CACHED พร้อม source timestamp สีแดง/ไม่มีค่าดีใช้ UNAVAILABLE/error; STALE เมื่อfetchสำเร็จล่าสุดเกิน2intervals
- เวลา Bangkok UTC+7 จาก NTP; แสดง `--:--` จนเวลาถูกต้อง
- Serial115200: `n` หน้าถัดไป, `c` ลบ calibration ที่บันทึกแล้วคาลิเบรตใหม่ (ไม่ลบ WiFi), `w` ลบ WiFi ที่บันทึกและกลับฟอร์ม, `t` touch diagnostics, `h` heap/stack; ไม่ log credentials
- SSID/Password บันทึกใน ESP32 NVS เฉพาะการเชื่อมสำเร็จ และไม่เขียนซ้ำเมื่อข้อมูลเดิมไม่เปลี่ยน ดูข้อควรระวังด้านความเป็นส่วนตัวด้านล่าง

## Hardware / Pin Map
มองจากด้านหลัง พอร์ต USB อยู่ด้านบน รูปแสดงตำแหน่งส่วนประกอบโดยประมาณ ไม่ใช่ลำดับขาของคอนเนกเตอร์

```text
       Micro USB     USB-C
    +--------------------------+
    |      ESP32-2432S028       |
    |                          |-- SPEAK (ไม่ใช้)
    | microSD                  |
    | (ไม่ใช้)   TFT ภายใน     |
    |          CLK14 MOSI13    |
    |          MISO12 CS15 DC2 |
    |          BL21 RST=-1     |
    | RGB LED                  |
    | (ไม่ใช้) Touch ภายใน     |
    |          CLK25 MOSI32    |
    |          MISO39 CS33     |
    |          IRQ36          |
    |       ESP32-WROOM   BOOT |
    |       เสาอากาศ      RESET|
    +--------------------------+
```

จอ ILI9341 ยืนยันแล้ว; ขาทัชยัง provisional ต้องยืนยันกับบอร์ดจริง Firmware อ่าน pressure โดยไม่อาศัย IRQ36 ไม่ต้องต่อสายภายนอก

## Wiring Notes / Safety
ใช้สาย USB ที่ส่งข้อมูลได้ จ่ายไฟผ่าน USB พอร์ตเดียว ห้ามป้อน 5V เข้า GPIO การ upload จะเขียนทับ firmware เดิม ไม่มี backup ตามคำขอ

## Getting Started
```bash
pio run -e cyd_ili9341
pio device list
pio run -e cyd_ili9341 -t upload --upload-port COM_NUMBER
pio device monitor --port COM_NUMBER -b 115200
```
แทน COM_NUMBER ด้วยพอร์ตจริง ผู้ใช้ยืนยัน ILI9341 สี/สัดส่วนแล้ว ST7789 จอขาว คง default cyd_ili9341 คำสั่ง upload/monitor ด้านบนให้ผู้ใช้รันเอง ไม่ได้รันในรอบนี้

## Test Procedure
1. เมื่อไม่มี calibration ที่ใช้ได้ หรือสั่ง `c` ต้องเห็นพื้นดำ วงกลมขาวพร้อมจุดกลาง/ลูกศรเล็กที่ (25,25)/(294,25)/(294,214)/(25,214) ข้อความเดียว `Click center of circles` แตะจุดกลาง ไม่ใช่ลูกศร มุมไหนก่อนก็ได้ นิ่งประมาณ80msแล้วปล่อย; ผ่านเป็นเขียว ครบและ geometryผ่านรอ600msก่อนWiFi ไม่บล็อก
   - แตะกลาง/มุมซ้ำ/ลากหรือสัญญาณกระโดดไม่เพิ่มมุม ถ้าfitไม่ผ่านกลับขาวทั้งหมด ดูเหตุผลSerial; `c` ลบเฉพาะ calibration แล้วเริ่มใหม่ได้ทุกหน้า; ถ้าเขียน NVS ไม่สำเร็จ Serialแจ้งและใช้ผลfitในบูตนี้ได้
2. หน้า WiFi เดิม กรอก SSID/Password เครือข่าย **2.4GHz** รหัสว่างสำหรับopen network **รหัสผ่านยังมองเห็นบนจอ ระวังผู้อื่น/ภาพถ่าย** ไม่log บันทึกลงNVSเฉพาะเมื่อเชื่อมสำเร็จ
3. QWERTY3แถว `ABC`/`abc` shift; `123` แยกตัวเลข/symbols `#+=`/`123` สลับ2ชุด `ABC`คืนletters `Space`/`Del` ครอบคลุมASCII printableทั้งหมด
4. Connectขวาบนเปลี่ยนเป็นCancelระหว่างเชื่อม มีspinner/timeout20s/Retry ลองรหัสผิด/Cancel ไม่เข้าdashboardก่อนWL_CONNECTED
5. เริ่มอากาศกรุงเทพ รอNTPก่อนHTTPS **ปล่อยConnectก่อนปัดซ้าย/ขวา** หรือSerial `n`; แตะหุ้น/สถานีเท่านั้นไม่เปลี่ยนหน้า ลองกดค้าง/noise1–2polls/ปัดแนวตั้ง/swipeบนtabsต้องไม่เลือกผิด
6. ปิดrouterกลับWiFi gateและแก้ข้อมูลได้ ไม่ลบค่าที่บันทึก; Rebootต้องข้ามcalibrationและลองWiFiเดิมอัตโนมัติเมื่อrecordใช้ได้ ลองรหัสผิด/Cancel/timeoutแล้วrebootต้องยังใช้ค่าดีเดิม; LVGLไม่เขียนทับsetup
7. ปุ่มริมซ้าย/ขวา/Delต้องตรงhitbox ถ้าแตะไม่ตรงเปิด `t` แตะปุ่มทดสอบที่ไม่ใช่credentials ตรวจraw/z/pressure_ok/mapped/xy/hit แล้ว `c` คาลิเบรตใหม่
8. ตรวจกราฟเวลา/source update/missingproducts/CACHED/STALE/NTP/429 และSerial `h` ระหว่างโหลดหลายรอบ ยังไม่ถือว่าheap/TLSผ่านบนฮาร์ดแวร์

Mappingใช้affineจากศูนย์กลางวงกลมเดียวกับวาด extrapolateถึงขอบ clamp x0..319/y0..239 ไม่ทิ้งvalidADCเพียงเพราะพิกัดเลยขอบ ดู host regression `python tools/test_touch_mapping.py`

**ข้อจำกัด:** WiFi keyboard/dashboardใช้ASCII ไม่มีฟอนต์ไทย; SSIDสูงสุด32bytes/passphrase8–63หรือว่าง ไม่มีrawPSK64hex/enterprise/captiveportal/scan คาลิเบรตอิงorientation rotation1 rawxเพิ่มขวา/rawyเพิ่มลง ช่วง200..3900 บริเวณนอก25%ทั้ง2แกน ไม่อิงลำดับแตะ ถ้าแกนกลับ/สลับต้องยืนยันhardwareก่อนปรับ ไม่สามารถinferorientationจากunordered4cornersล้วน ๆ

## Storage / ความเป็นส่วนตัว
ใช้ Arduino Preferences namespace `cyd_settings` แยก blob `cal`/`wifi` มี version1/ขนาด/magic/CRC32 ตรวจ profileจอ/ทัชและgeometryของcalibration และความยาว/ASCIIของcredentials ไม่ serialize raw struct/padding; บันทึก1blobต่อrecord ไม่แยกSSIDกับPassword ข้อมูลขาด/เสีย/ต่างversionถูกปฏิเสธ ไม่ลบทิ้งเองตอนบูต

`WiFi.persistent(false)` ปิดการเขียน configโดยSDK ใช้NVSที่แอปควบคุมเท่านั้น Credentialsเก็บจากruntime ไม่มีsecretฝังในsource บันทึกหลัง `WL_CONNECTED` และSSIDตรงกับattempt; ordinaryboot/failed attempt/Cancel/WiFiหลุดไม่ลบหรือเปลี่ยนค่าที่ดี

**NVSนี้เป็น plaintext ไม่ได้เปิด hardware flash/NVS encryption** ผู้ที่อ่านflashได้อาจกู้Passwordได้ `w` เป็นการลบkeyตามปกติ ไม่ใช่secure forensic wipe; Passwordยังแสดงบนจอตามคำขอ `c` ไม่ลบWiFi และจะลองค่าที่บันทึกหลังคาลิเบรตเสร็จ ถ้าสั่ง `w` ระหว่างcalibrationที่ยังไม่ผ่าน จะลบWiFiแต่ต้องทำวงกลมให้เสร็จก่อนใช้ฟอร์ม ทดสอบpower-cut/NVSจริงบนบอร์ดยังจำเป็น Hosttestsจำลองtruncated/corruptrecordsเท่านั้น

## APIs / ขอบเขตข้อมูล
ตรวจ2026-10-14 ทุกแหล่งด้านล่าง HTTP200/schemaตรงด้วยtrustedCAชุดเดียวกับfirmware:

| แหล่ง | ข้อมูลบนจอ | Refresh |
|---|---|---|
| `https://api.open-meteo.com/v1/forecast` | Bangkok13.7563,100.5018: temperature/feels/RH/WMO/wind km/h | 10 นาที |
| `https://api.chnwt.dev/thai-gold-api/latest` | response.price.gold_bar.buy/sell ทองแท่งไทย96.5% THB/บาทน้ำหนัก พร้อมวัน/เวลาปรับราคา | 15 นาที |
| `https://api.chnwt.dev/thai-oil-api/latest` | response.stations.ptt/pt/bcp Gasohol95/91/E20/DieselB7 Bangkok/vicinities THB/ลิตร | 15 นาที |
| `https://query1.finance.yahoo.com/v8/finance/chart/{symbol}?interval=1d&range=1mo` | ^SET.BK / PTT.BK / PTG.BK / AOT.BK / ADVANC.BK / CPALL.BK latest + timestamp/quote.close | 15 นาทีต่อสัญลักษณ์ |

Yahoo **unofficial/delayed ไม่มี SLA** ล่าสุดจาก meta.regularMarketPrice/regularMarketTime ไม่ใช้ previous close แทน; SETเป็นpoints หุ้นเป็นTHB instrumentType INDEX/EQUITY ไม่มีหุ้นสหรัฐหรือfutures/FXconversionแล้ว ใช้ User-Agent Mozilla/5.0 และเมื่อ429 cooldownทั้งYahoo15นาที ไม่ retryรัว

**ข้อจำกัดกราฟ:** ตรวจ2026-10-14 Yahooส่งSET1จุด แม้ขอ1mo หุ้นอีก5ตัวมี22จุด; เมื่อYahooให้<2จุดจะเก็บ latest ที่มี market timestamp เพิ่มขึ้นจริงในRAM ใช้ป้าย Observed / since boot และ 1 real sample เมื่อมีจุดเดียว ไม่เติมเส้นย้อนหลังเอง เมื่อได้ dailyhistory>=2จุดจะใช้APIhistoryแทน กราฟเชื่อมจุดจริง/แกนเวลาตาม timestamp ไม่ใช่ข้อมูลทุกนาทีหรือadjustedclose

ทองendpointที่ใช้มีlatestเท่านั้น **หน้าทองไม่แสดงกราฟแล้ว** ขยายราคาBUY/SELLและเวลาUPDATEDจากsource ค่าไม่มีใช้-- ยังแสดงfullsource date/timeและCACHED/STALE/error Collectorเดิมยังเก็บobservedSELLในRAM32จุดทุกsuccessfulfetch15นาทีโดยไม่แต่งข้อมูล แต่UIไม่ได้ใช้ประวัตินี้ ไม่เปลี่ยนAPI/refresh/NVS

น้ำมันตรวจครบ12สินค้าจริง API keys Gasohol95/91/E20 = gasohol_95/gasohol_91/gasohol_e20; PTT/PTดีเซลใช้ diesel แต่ **บางจากใช้ disel** ตามschemaจริง ตรวจชื่อสินค้าด้วย: แก๊สโซฮอล์95/91/E20 และดีเซลB7 ไม่มีpremium/เบนซิน95/ปั๊มอื่นมาทดแทน หากไม่มี/null/ชื่อไม่ตรงแสดง -- แม้ข้อมูลสินค้าอื่นสำเร็จ ตราบนปั๊มเป็นRGB565จากSVGsupersampling: PTT/PTวาดreproductionตามreference, Bangchakใช้path/gradientและlowercasewordจากpublicSVGทางการแล้วcrop/recompose ดู `assets/logos/README.md` เครื่องหมายเป็นของเจ้าของแบรนด์ ไม่อ้างการรับรองหรือlicenseแบบunrestricted

Open-Meteo เป็นข้อมูลจากแบบจำลอง ไม่ใช่เซนเซอร์หน้างาน/พยากรณ์โอกาสฝน; แสดงเวลาข้อมูล Bangkok ส่วน Yahoo timestamp แปลงเป็น Bangkok, ทองไทยคงวันที่ พ.ศ. ของ source, น้ำมันไทยแปลงเดือนภาษาไทย/พ.ศ. เป็นวันที่ ASCII ค.ศ. โดยไม่แทนด้วยเวลา fetch

HTTPS ใช้ root CA จริง (ISRG Root X1, GTS Root R4, DigiCert Global Root G2) ใน `src/api_roots.h`; ไม่มี `setInsecure` ต้องมี NTP ก่อนโหลด ถ้า CA เปลี่ยนต้องอัปเดต trust anchors; ไม่ติดตาม redirect ไป host อื่นอัตโนมัติ มี timeout TCP4s/TLS5s/read3s/body8s และ body cap24KiB; DNS/HTTP headers มีขอบเขตตาม framework ไม่ใช่ hard real-time deadline ต่อ request

ข้อมูลล่าสุดอยู่ RAM เท่านั้น: fetch ผิดพลาดยังเก็บค่าดีเดิมพร้อม error; ถ้าไม่มีค่าดีใช้ `--` ไม่สร้างราคาเอง `STALE` หมายถึงไม่ได้ fetch สำเร็จ >=2 refresh intervals ไม่ได้ใช้ตัดสินว่าตลาดเปิดอยู่หรือไม่ ค่า source timestamp ช่วยบอกอายุราคาจริง API ส่งคำขอโดยตรงจาก IP เครือข่ายผู้ใช้ ไม่ส่ง credentials WiFi; ต้องใช้อินเทอร์เน็ต และใช้งานตามเงื่อนไข/rate limits ของผู้ให้บริการ (Open-Meteo free tier สำหรับ non-commercial)

## Validation / ทดสอบเพิ่มเติม
```bash
pio run -e cyd_ili9341
python tools/test_touch_mapping.py
python tools/test_live_data.py
python tools/test_saved_settings.py
python tools/test_dashboard_render.py
python tools/check_stack_frames.py --self-test
python tools/verify_apis.py
```
Testsบนhostใช้Python/g++; render testใช้gccเพิ่มและLVGLpinnedจริง ทดสอบfontwidths/partialflushbounds/tabselection/swipeไม่เลือก/setupTFTownership มีpreviewsใน .pio/host_ui (Pillowoptional) ทุกpreviewเป็น **synthetic test fixtures ไม่ใช่ราคาlive** parser testsใช้ArduinoJsonจากbuild ส่วน verify_apis.py ใช้requests/rootPEMของfirmwareและnetwork ไม่ควรรันซ้ำถี่

บนบอร์ดยังต้องตรวจ5หน้า/สี/ทัช/swipe/Connect hold/Cancel/WiFiหลุดกลับgate/NTP/HTTPSพร้อมUI/cached/429 ใช้Serial hวัดfree/minheap/largestblock/stackระหว่างโหลดหลายรอบ ไม่ถือว่าhostrenderหรือstaticRAMยืนยันTLSheapจริง

## Firmware Status / Memory
Build cyd_ili9341 ใช้app partitionเดิม1310720bytes **ไม่เปลี่ยนpartition**; ตัวเลขล่าสุดดู hardware_lib.md ค่าบอร์ด esp32dev4MB เป็นconfiguration ไม่ใช่หลักฐานขนาดflashที่ติดตั้งจริง ยังไม่อ่านflashจากบอร์ดและไม่flash/monitor/commit/vault

LVGLใช้single partialbuffer8rows RGB565=5120bytes + staticpool24KiB ไม่มีclocksprite84KBเดิม ไม่มีPSRAMrequirement ใช้Montserrat18/26เท่านั้น TFTsetupยังใช้Nunito GFXเดิม RuntimeยังมีTLS/body<=24KiB/filteredJSON6KiB/filter1.5KiB/workerstack12KiB/snapshot copies ต้องตรวจheap fragmentationและstackบนบอร์ด หากจะเปลี่ยนpartitionในอนาคตต้องยืนยันcapacityจริงก่อน ไม่จำเป็นสำหรับbuildนี้

### แก้ crash หลัง Connect / saved-config boot
พบ `Stack canary watchpoint triggered (loopTask)` ในรุ่น ELF SHAเริ่ม `9779515cb32b5bee` ยืนยันSHAและdecodeก่อนbuild: loop→dashboard::tick→LVGL refresh→draw→box→lv_draw_rect→draw_bg คือตอนวาดหน้าจอปกติ แต่loop/tickจองstack3440+3248bytesจากSnapshot3216bytesที่คืนby-value แม้ไม่ได้กดh ไม่พบหลักฐานNVSเสีย จึง **ไม่ต้องลบWiFi/calibrationเพื่อแก้บั๊กนี้**

แก้เป็นcopyตรงลงSnapshotของUIในstaticRAMใต้lock และSerialhอ่านmetadataเล็ก ไม่มีSnapshottemporaryใหญ่บนloop stack ลดcompilerframe loopเหลือ240bytes tick/show32bytes **ไม่เพิ่มloopstack8192bytes** RAMstaticไม่เพิ่ม เก็บELFเดิม/decodedtraceใน .pio/crashes/9779515cb32b5bee/ รายละเอียดPCและSHAเต็มอยู่ project.md

Buildมี -fstack-usage/-Wframe-larger-than=2048 และpost-linkguard UIframe<=1024bytes; standalonecheckด้านบนตรวจreportsจริงจากXtensa ไม่ใช่hostABI ต่อให้hosttestsผ่านยังต้องทดสอบบนบอร์ด: rebootด้วยsavedconfig เข้า5หน้า ปัด/แตะ ใช้Serialhหลังrender/TLS/reconnectหลายรอบ ดู loop-stack-min-free/API-stack-min-free (bytes, minimumตั้งแต่taskเริ่ม ไม่ใช่heap; API0=ยังไม่วัด) รุ่นแก้ยังไม่ได้flash/erase/monitorให้ผู้ใช้

## Roadmap
ยืนยันUI/pressure/swipe/TLSและheapบนฮาร์ดแวร์; ติดตามSETcoverage/CArotation/API429 ไม่เพิ่มข้อมูลย้อนหลังปลอมหรือAPIแบบมีkey

### Weather animation / Brand references
Serial h เพิ่ม scheduled และ presented-roi FPS จากหน้าต่างล่าสุดอย่างน้อย2s (window=0 ยังwarming, active=0 หยุดแล้ว): presented-roiนับเฉพาะrefreshที่ส่งSPIครบchunkสุดท้ายและทุกchunkอยู่ในweather dirty ROI ไม่ใช่LCDscanrate; mixedคือfull/data/statusrefreshที่ไม่นับรวมROI FPS ส่วน skippedคือdeadlineที่พลาดเมื่อloopช้า วาดภาพล่าสุดครั้งเดียวไม่catch-up burst การออกจากหน้า/ข้อมูลinvalidหยุดscheduler ไม่มีper-frameallocationหรือSnapshotบนstackเพิ่ม

WMO0/1/2/3แยกclear/mainlyclear/partlycloudy/overcast; 45/48หมอก; 51/53/55/56/57drizzle; 61/63/65/66/67/80/81/82rain; 71/73/75/77/85/86snow; 95/96/99thunder (96/99เพิ่มลูกเห็บ) ไม่ใช้ช่วงกว้างเดาcodeที่ไม่มีในschema ข้อมูลcachedใช้WMOล่าสุดที่ดีพร้อมสถานะ/timeเดิม ไอคอนsunเป็นสัญลักษณ์condition ไม่ใช่ตำแหน่งดวงอาทิตย์หรือday/night prediction

แอนิเมชันตั้งเป้า30fps ด้วย rational accumulator จังหวะ34/33/33ms ไม่สะสม timing drift; เมื่อถึงกำหนด ready เฉพาะ display refresh timer ก่อน lv_timer_handler ครั้งเดียว แก้การปัดจังหวะตาม LVGL20ms โดยไม่ลด global refresh period ผ่านmain tick คำนวณphaseจากเวลาที่ผ่านไปจริงไม่ใช่นับframe จึงไม่เปลี่ยนความเร็วเมื่อframeช้า เมฆdrift/bobมากขึ้นแบบsinusoidalแยกphase พร้อมsquash/stretchและlobeสามส่วนหายใจต่างphase3/4/5s ทำให้silhouetteเปลี่ยนจริง รัศมีหมุน/pulseต่อเนื่อง ฝนมีopacity-gradedtrails/phaseแยก ฟ้าแลบ2pulseสั้นใน6s ไม่มีdelay/สร้างtimerหรือobjectใหม่ทุกframe animation-only invalidateเฉพาะกรอบ107x84px (LVGL8.3เพิ่มsafety margin5pxต่อด้าน; การเปลี่ยนหน้า/ข้อมูล/สถานะยังวาดเต็มจอได้) particles/rays/เมฆclipอยู่ในกรอบ ไม่ทับตัวเลข/ข้อความ Fastpathวาดเฉพาะcard/artโดยไม่formatตัวเลข/dateหรือglyphข้อความนอกกรอบซ้ำต่อanimation-onlyframe ยังคงSnapshotstatic/copy_snapshot ไม่คืนby-value และlv_timer_handlerเรียกครั้งเดียว ไม่เพิ่มstack/bufferใหญ่ NativeLVGLtestsตรวจpixelmovement/dirtyarea/visibility/poolไม่รั่ว; FPS/ทัช/heapจริงบนบอร์ดยังไม่ยืนยัน

โลโก้: PTT/PTเป็นBezierreproductionsตามreferenceผู้ใช้ ไม่ใช่ไฟล์ทางการ โดยPTใช้P/Tแดงเอียงซ้อนเฉียงและวงแหวนlime/darkgreenมีช่องNE/SW; Bangchakใช้ [public official SVG](https://www.bangchak.co.th/assets/images/logo.svg) ที่ตรวจHTTP200/HTTPSแล้ว ตรา/คำbangchakยังเป็นoriginalpathsแต่จัดcropบนwhite/greenroundedplateใหม่ ไม่ใช่officiallockupเดิม ไม่มีruntimeassetdownload/SVGdecoderบนESP32 [ที่มา/ข้อจำกัดสิทธิ์](assets/logos/README.md)

Assetsมี3ภาพ60×56RGB565รวม20,160bytesในflash ไม่กินframebufferใหม่ สร้างออฟไลน์8×ด้วยresvg-py+Pillow/Lanczos ผ่าน `tools/generate_station_logos.py` / `--check`; ลงdependencyเฉพาะdeveloperด้วย `python -m pip install -r tools/requirements-assets.txt` Buildfirmwareปกติใช้generatedsrc/station_logos.h/.cppไม่ต้องมีPythonrenderer NativeLVGLtestsตรวจbytes/ตำแหน่งภาพและhoseไม่ทับletteringด้วย Target30fpsไม่ใช่FPSที่วัดบนบอร์ด ยังต้องตรวจSPI27MHz/ทัช/TLS/heapจริง

## Fonts / License
UI คาลิเบรตและ WiFi ใช้ **Nunito Bold** (rounded sans) จาก Google Fonts, SIL Open Font License 1.1 ดู `assets/fonts/OFL.txt` และต้นฉบับ `assets/fonts/Nunito.ttf` ฝัง ASCII bitmap ขนาด 18/26px ใน `src/ui_fonts.h` (~4.8 KB bitmap) ไม่ต้องดาวน์โหลดฟอนต์ขณะรัน

หากต้อง regenerate: `python tools/generate_ui_fonts.py` ต้องมี Pillow เฉพาะเครื่องมือสร้างฟอนต์; build firmware ใช้ header ที่มีอยู่แล้ว ไม่ต้องติดตั้งเพิ่ม

License ส่วนอื่นของโปรเจค: TBD

LVGL library MIT; built-in Montserrat font OFL1.1 ตามlicensesที่มากับdependency ส่วนทอง/ปั๊ม/7segment/weatherเป็นprocedural artworkใน src/dashboard.cpp ตราแบรนด์เป็นPTT/PTreproductionsและBangchakofficialSVGที่crop/recomposeเป็นRGB565 ดูattribution/brandrightsในassets/logos/README.md ไม่อ้างว่าofficialassetเหล่านี้มีlicenseใช้งานได้อย่างเสรี

### Display orientation
จอทุกหน้าใช้ TFT rotation3 (หมุน180°จากเดิม) ขนาด320×240เท่าเดิม ทัชอ่านcanonical rotation1และกลับพิกัดก่อนส่งUI จึงใช้calibration/WiFiที่บันทึกไว้ได้โดยไม่ลบNVS คาลิเบรตใหม่ยังรองรับตามปกติ

### IDLE0 watchdog / API diagnostics
เพิ่ม cooperative TLS polling ให้IDLE0มีโอกาสรันระหว่างHTTPheader/Streamรอข้อมูล ไม่ปิดwatchdog คงCAverified มีrequest budget20sที่ตรวจเมื่อกลับเข้าtransport poll (ไม่รับรองharddeadlineของDNS/individualframeworkcalls) body8s/24KiB และrejectchunkedที่ไม่คาดหมาย ดูSerial API[index] phase=request/headers/body/done พร้อมelapsed/status/error/stack-min โดยไม่logcredentials รายละเอียดELFและข้อจำกัดในproject.md ทดสอบhostเพิ่ม python tools/test_cooperative_tls.py; ยังต้องยืนยันwatchdog/TLSstackบนบอร์ดจริง

รอบแก้watchdogซ้ำ: APIworkerเปลี่ยนเป็นpriority0ให้IDLE0แบ่งtime-sliceได้แม้ภายในconnect/TLSไม่มีpoll เพิ่มlogdnsและtcp-tlsstart/doneเพื่อscopeก่อนheaders API[0]คือOpen-Meteo รอบก่อนpolling-onlyยังไม่เพียงพอ ข้อมูลล้มเหลวใช้lastgoodจริงในRAMพร้อมCACHED/เวลาเดิม หากไม่เคยโหลดได้ใช้--/UNAVAILABLE ไม่มีAPIทดแทนที่ไม่ตรวจสอบ Retryแยกแหล่ง30s/60s แล้วพักcircuit>=5min สูงสุด15min สำเร็จreset Yahoo429ยังพักทั้งhost15min แหล่งหนึ่งfailแล้วไปแหล่งถัดไปหลังrequestคืนการควบคุม ไม่ลบWiFi/calibration DNSมีblockingwaitframeworkได้16+15s; 20sเป็นpollbudgetไม่ใช่harddeadlineบังคับตัดopaqueTLScall ต้องทดสอบบนhardware ดูรายละเอียดfollow-upในproject.md

TLSfollow-up: weather/ทอง/น้ำมันตั้งhandshake20sและcooperativebudget35s เฉพาะ3แหล่งนี้ Yahooยัง5s/20sเดิม (ไม่ใช่watchdogtimeout) ค่า-1ยังไม่ฟันธงbadCA/timeout เพิ่มTLSerrorcode/hex/textและepoch/freeheap/largestblockก่อนต่อเพื่อแยกสาเหตุ SNIใช้hostnameเดิมถูกต้อง ไม่เปลี่ยนCA: 2026-10-22ตรวจ9APIHTTP200และdirectTLS1.2ด้วยPEMเฟิร์มแวร์ผ่านแล้ว รัน python tools/verify_tls_hosts.py เพื่อตรวจhosttrustแบบไม่มีproxyได้ (network3connections ไม่ใช่ESP32performance test) บนบอร์ดยังต้องยืนยันweather/oilกลับมา ไม่มีinsecurefallback
