# ESP32 CYD Weather

## Overview
แดชบอร์ดตั้งโต๊ะตลาดไทย: หุ้นไทย ทองแท่งไทย น้ำมันหน้าปั๊มไทย พร้อมอากาศกรุงเทพและนาฬิกา ยอมรับข้อมูลล่าช้าได้

## Goals
LVGL 8.3.11 dashboard เต็มจอ 5 หน้า ปัดซ้าย/ขวาเปลี่ยนหน้า ไม่มี navbar; แตะเลือกหุ้น/สถานี คง circle calibration และ TFT_eSPI WiFi gate/keyboard เดิม

## Hardware
ESP32-2432S028 / HW-458 สองพอร์ต USB, ESP32-WROOM-32E, จอ 240x320 ใช้แนวนอน

## Pin Map
จอ ILI9341 และขาจอยืนยันแล้ว; ขาทัชยัง provisional ต้องยืนยันบนบอร์ดจริง ดูรูปใน README.md

| Function | Pin | Device | Direction | Notes |
|---|---|---|---|---|
| TFT SCLK/MOSI/MISO | 14/13/12 | TFT | out/out/in | SPI |
| TFT CS/DC | 15/2 | TFT | out | Reset ใช้ -1 |
| Backlight | 21 | TFT | out | HIGH เปิด |
| Touch CLK/MOSI/MISO | 25/32/39 | XPT2046 | out/out/in | SPI แยกจากจอ |
| Touch CS/IRQ | 33/36 | XPT2046 | out/in | GPIO36 ไม่มี pull-up ภายใน |

## Electrical / Safety Notes
จ่ายไฟผ่าน USB เพียงพอร์ตเดียว GPIO เป็น 3.3V ไม่ต่อสายเพิ่มในการทดสอบนี้

## Firmware Architecture
Arduino + TFT_eSPI + XPT2046; state machine CALIBRATION (เมื่อไม่มีrecordที่ใช้ได้/สั่งc) → WIFI → DASHBOARD ที่ TFT rotation3 (touch/affine canonical rotation1) ใช้ Arduino Preferences/NVS บันทึก calibration และ WiFiที่เชื่อมสำเร็จ
- Calibration UI: พื้นดำ วงกลมรัศมี9px มีจุดกลางรัศมี2px สีขาว→TFT_GREEN เฉพาะมุมที่รับสำเร็จ ลูกศรมนขนาดเล็กแยกอยู่นอกวงกลม ข้อความเดียว `Click center of circles` สองบรรทัด Nunito Bold26px กลางจอ ไม่มี label/progress สีอื่น
- ระบุมุมจาก raw extrema quadrants นอก 25% ของช่วง 200..3900 (low<=1125/high>=2975) ภายใต้ orientation raw x/right y/down ไม่ใช้ลำดับแตะ รับมุมใดก่อนก็ได้; แตะกลาง/ซ้ำ/เปลี่ยน quadrant ระหว่างกดถูกปฏิเสธ
- อ่านทุก 20 ms รับอย่างน้อย 4 samples (~80 ms) และ release 3 samples; เก็บ average สูงสุด 12 samples ตรวจ spread<=100 raw ตลอดการกด ไม่ต้อง long hold
- fit affine จาก averaged opposing edges ทั้ง 4 มุม target ตรงกับศูนย์กลางวงกลม (25,25)/(294,25)/(294,214)/(25,214) ใช้ CORNER_X/Y และ TARGET_SPAN_X/Y ร่วมกันทั้ง fit/map/draw ไม่มีเลขพิกัดซ้ำ hardcode; determinant>=1e6, edge length>=1800 raw, closure<=180 raw, residual<=6 pixels ถ้า fit ไม่ผ่านรีเซ็ตวงกลมทั้งหมดเป็นขาว ไม่แสดงครบเขียวผิด ๆ
- เมื่อครบ4มุมและfitผ่าน บันทึก6affine fields ลงNVS แล้วแสดงเขียว600msด้วยmillisและเข้าWiFi; บูตโหลดrecordที่validข้ามcalibration Serial cลบเฉพาะcalibrationและตัดWiFiก่อนเริ่มวงกลมใหม่ หากsave/eraseล้มเหลวแจ้งSerialแบบไม่มีsecret ผลfitยังใช้ในRAMได้
- Affine รองรับ skew/cross-axis correction แต่การระบุชื่อมุมต้องอาศัย orientation ที่ทราบ ไม่อ้างว่า auto-detect swapped/inverted hardware จาก unordered taps ได้ ถ้า orientation ต่างต้องปรับ classifier หลังยืนยันบอร์ด
- ทัชหน้า WiFi/dashboard ใช้ affine ที่ fit แล้ว extrapolate จากวงกลม inset ไปขอบจอและ clamp0..319/0..239 ไม่ reject valid ADC เมื่อ mapped เลยขอบ (เดิม reject นอก +-12px); คง pressure350 และ debounce เดิม (กดนิ่ง2/ปล่อย3) ยังไม่มีหลักฐานให้ลด threshold
- Serial t toggle diagnostics rate<=10Hz raw/x/y/z/pressure_ok/mapped/down + hit/no-hit ไม่ log key characters หรือ strings; fit log coefficients/determinant/closure/residual และ rejected sample count/spread เพื่อ isolate calibration vs pressure/hitbox
- tools/test_touch_mapping.py ทดสอบ C++ fit/map/keyboard helpers ที่ extract จาก firmware จริงด้วย g++: synthetic skew model, targets, screen edges, out-of-bounds clamp, invalid ADC/fit และ hitbox 94 ปุ่ม ไม่ใช่ hardware measurement
- Wi-Fi UI เรียบง่ายดำ/ขาว/เทา ฟอนต์ Nunito Bold 18px: หัวข้อ WiFi, ช่อง SSID/Password และ Connect ขวาบน (Cancel เมื่อเชื่อม) ไม่มีไอคอน/helper/tiny text แสดง status สั้นเฉพาะจำเป็น
- Keyboard letters-only QWERTY 3 แถว 10/9/7 ตัว; numbers+symbols แยก mode มี 2 ชุด ครอบคลุม ASCII printable+space ทั้งหมด ปุ่มสูง 29px กว้าง 29–44px แถวสั้นจัดกลาง; bottom controls สูง26px shift/page, 123/ABC, Space, Del
- keyboard_key() คำนวณ rect เดียวกันทั้งวาด/ตรวจแตะ ป้องกัน hitbox ไม่ตรง หลังเปลี่ยนหน้าแป้นล้างเฉพาะ keyboard area; field truncate ตามความกว้างฟอนต์จริง ไม่ตามจำนวน chars
- Password แสดงข้อความจริงตามคำขอ (เสี่ยง shoulder-surfing/ภาพหน้าจอ), 2.4GHz station; SSID/passphrase NVSเฉพาะconfirmed successful attempt และ `WiFi.persistent(false)` ปิดSDKpersistence ไม่ log credentials
- เชื่อมด้วยWiFi.begin/millis timeout20s spinner/Cancel/Retry เดิม; bootและหลังcalibrationลองstored valid WiFiอัตโนมัติหนึ่งattempt ถ้าfailกลับeditableform ไม่overwriteNVS ปิดSDKauto reconnect RuntimeWiFilossกลับgateโดยไม่ลบrecord
- Dashboard เปิดเฉพาะ WL_CONNECTED; loss กลับ gate เก็บข้อมูลใน RAM เพื่อแก้/ลองใหม่; Serial n ผ่าน gate เช่นกัน
- Dashboard: src/dashboard.cpp ใช้ LVGL 8.3.11 custom draw event บน root เดียว สี midnight/neon/cards; Montserrat18/26 ASCII เท่านั้น ไม่ฝังฟอนต์ไทยชุดใหญ่ Nunito GFX เดิมใช้เฉพาะ setup
- ลำดับปัดซ้าย stocks→gold→oil→weather→clock→stocks ปัดขวาย้อนกลับ เริ่ม weather หลัง Connect; Serial n หน้าถัดไป ไม่มี navbar ไม่มี tap-anywhere cycling
- Input driver LVGL รับพิกัด affine/debounceจาก main; src/dashboard_gesture.h แยก contact เป็น TAP/LEFT/RIGHT เมื่อ release3polls เท่านั้น dx>=55px และแนวนอน>2เท่าแนวตั้ง; tap travel<=12px และ<800ms ต้องเริ่ม/จบในปุ่มเดียวกัน ช่องว่างระหว่างปุ่มไม่เลือก คง pressure350 กด2samples สำหรับ dashboard ไม่ใช้ stationary filter ที่ขัดขวาง swipe
- ต้องปล่อย Connect ก่อน arm gesture; WiFi keyboard ยังใช้ stationary2/release3 เดิม Calibration/WiFi TFT custom และ dashboard LVGL ไม่เขียน TFT พร้อมกัน dashboard::hide ก่อน gate/calibration; lv_timer_handler เฉพาะ main และเฉพาะ dashboard worker ไม่มี LVGL/TFT
- LVGL initialize หลังผ่าน gate ครั้งแรก ใช้ RGB565 single partial buffer320x8=5120bytes static + LVGL pool24KiB static ไม่มี full-screen buffer/clock sprite84KB ไม่มี PSRAM requirement Config src/lv_conf.h เปิดเพียง Montserrat18/26 ไม่เปิด examples/demos/themes
- หุ้น SET/PTT/PTG/AOT/ADVANC/CPALL แตะปุ่ม3x2 ล่าสุดจาก meta.regularMarketPrice พร้อม source time Bangkok; Yahoo interval1d range1mo ใช้ timestamp+indicators.quote[0].close จริง เก็บสูงสุด32จุด ตัดnull/invalid/future/เวลาย้อน ไม่ใช้ previous close แทนราคาล่าสุด ไม่ปรับ split/FX ไม่อ้าง realtime
- Yahoo ตรวจจริง SET ให้เพียง1จุด (แม้ขอ1mo); จอแสดง 1 real sample ไม่แต่งกราฟ เมื่อ<2 daily points จะสะสม latest ที่มี regularMarketTime เพิ่มขึ้นจริงในRAM พร้อม Observed / since boot; เมื่อ API กลับมามี>=2 daily points ใช้ประวัติ API แทน หุ้นอีก5ตัวให้22จุดในการตรวจ2026-10-14
- ทอง96.5% BUY/SELL THB/บาทน้ำหนัก **ไม่แสดงกราฟแล้ว** ตามคำขอล่าสุด reclaimพื้นที่เป็น2cardsราคา7segment44px/ภาพทองแท่ง/UPDATED26px พร้อมfullsource dateพ.ศ.+time/สถานะ18pxด้านล่าง ไม่มีข้อมูลใช้-- CollectorobservedSELLเดิมยังอยู่ในworker/RAM32จุด ไม่แต่งย้อนหลังแต่UIไม่อ่านhistory ไม่แก้API/NVS
- น้ำมัน PTT / PT (บริษัทPTG) / Bangchak แตะ3tabs ใช้ stations.ptt/pt/bcp ตามAPI Gasohol95/91/E20/DieselB7 Bangkok/vicinities THB/L 12สินค้าแยกavailability bits ตรวจทั้งnameและprice; bcp dieselใช้คีย์ disel ที่APIสะกดผิดตามจริง ไม่มีfallbackไปdiesel/ปั๊มอื่น/premium ถ้าไม่มีหรือชื่อไม่ตรงใช้-- แม้สินค้าอื่นสำเร็จ
- station_logoใช้60×56antialiasedRGB565flashassets20,160bytesแทนprimitiveหนา PTT/PTเป็นBezierreproductionsตามuserreference (PTแดงเอียงซ้อนเฉียง/whiteinside/brokenlime+darkgreenring) ไม่อ้างofficialfiles Bangchakใช้publicofficialSVG paths/gradients/correctlowercasewordที่crop/recomposeบนgreen/whiteroundedplate ไม่ใช่officiallockupเดิม รายละเอียดURL/hash/rightsที่assets/logos/README.md Offlinegenerate8×resvg-py/Pillow/Lanczosเป็นsrc/station_logos.h/.cpp ไม่มีruntimeSVGdecoder/download/newframebuffer Hoseย้ายออกนอกimageareaไม่ทับword ไม่มีBCPคำย่อแทนbangchakบนpumpแล้ว ราคาสินค้า/tab/hitboxesเดิม
- WeatherคงOpen-Meteo Bangkok temperature/feels/RH/WMO/wind เพิ่มanimatedweather_artตามexplicitWMOscene: 0clear/1mainlyclear/2partlycloudy/3overcast,45/48fog,51/53/55/56/57drizzle,61/63/65/66/67/80/81/82rain,71/73/75/77/85/86snow,95/96/99thunder+hail96/99 Captionแยกfreezing/showers ไม่inferunsupportedcode ไอคอนsunเป็นconditionไม่ใช่day/night/solarposition
- WeatherSceneUNKNOWN/readinginvalid/hiddenไม่animate cachedใช้lastgoodWMO/source stamp Target30fps rational34/33/33ms ไม่drift; ready display refresh timerก่อนsinglehandlerเพื่อไม่quantizeตามLVGL20ms Late loopวาดล่าสุดครั้งเดียว/นับskipped ไม่มีcatchup burst uint32elapsedvisible-only mod120s WeatherPose48bytesstatic cloud drift±8/6.5px bob±2.6/2.3px + squash6s/independentlobes3/4/5s ทุกpartialchunkใช้poseเดียวกัน sun/rain/snow/lightningคงelapsed/WMO ไม่มีper-frameallocationหรือSnapshottemporary Serialhแยกscheduled/presented-roiFPSในwindow>=2s: นับSPIlastchunkเฉพาะROIครบทั้งrefresh แยกmixedfullrefresh ไม่ใช่LCDscanrate/ไม่ยืนยันhardwareFPS
- WEATHER_ART_AREA=(196,48)..(302,131)107x84px animation-only invalidateareaเท่านั้น (LVGL8.3publicAPIเพิ่ม5pxmarginจึงanimationflushสูงสุด117x94; page/data/statuschangesยังfullinvalidateได้) drawctxclipintersectกับpartialbufferareaแล้วrestorepointerทุกครั้ง particlesไม่paintเหนือlabel/ราคา FastpathในdrawตรวจanimationROIแล้ววาดเฉพาะcard/art ไม่formatfloat/dateหรือglyphข้อความนอกกรอบทุกframe Nativeformattercounterยืนยันไม่มีappsnprintfในanimation-onlyflush Snapshotstatic/copy_snapshotเดิม ไม่เพิ่มstack/framebufferใหญ่ (logo20,160flashbytes) clockยัง1Hzและfinanceupdateเมื่อrevision/60s ไม่มีrebuildobjecttree
- src/live_data.cpp worker core0 priority0 (tskIDLE_PRIORITY) stack12288 sequential verified HTTPS; Snapshot POD portMUX copy/publish ไม่ถือlockขณะI/O Weather10min Thai gold/oil/Yahoo15min Yahoo429 cooldownทั้งhost15min ไม่burst/retryรัว
- HTTP TCP4s/TLS5s/read3s/body wall8s cap24KiB JSON filtered6144bytes/filter1536 depth16 DNS/headerขึ้นกับframework ไม่อ้าง harddeadline; trust roots ISRG X1/GTS R4/DigiCert G2 src/api_roots.h ไม่มี insecure/redirect follow ต้องNTPvalidก่อนHTTPS
- Fetchผิดพลาดเก็บlastgoodและtimestampเดิมพร้อม CACHED สีแดง; ถ้ายังไม่มีใช้--/UNAVAILABLE/error ถ้าอายุfetch>=2intervalsใช้STALEพร้อมsource stamp (ไม่ใช่market-open detector) Missingoilpartialresponseล้างavailabilityเฉพาะสินค้าที่ไม่มี ไม่ยืมราคาสถานีอื่น
- Serial h: free/minheap/largest8bit block/UIstack high-water/APIstack high-water bytes หลังfetch; 0API=ยังไม่วัด ไม่logcredentials ไม่รันmonitorอัตโนมัติ

## Persistent Settings
src/saved_settings.h/.cpp ใช้Preferences namespace cyd_settings แยกkey cal/wifiเป็นcommitted single blobs; VERSION1/magic/length/CRC32 (ตรวจcorruption ไม่ใช่encryption) SingleSSID/passwordblobป้องกันpairคนละattempt ไม่มีmulti-key partial update ไม่มีPreferences.clear/autoeraseบนordinaryboot
Cal40bytes: CYDC/version/rotation/reserved/profile + IEEE754binary32 LE6fields + CRC32 ไม่มีcompilerpadding บูตตรวจfinite/ADCcornerbounds/axispositive/determinant>=1e6/edge>=1800 และprofilefingerprintจากresolution/targetcoordinates/touchpins/rawrange/rotation ไม่เขียนตอนfithelperหรือload เขียนจากaccept_cornerหลังfinalfitผ่านเท่านั้น
WiFi13..107bytes: CYDW/version/ssidlength/passwordlength/reserved + SSID/passwordASCIIbytes + CRC32; SSID1..32 password0หรือ8..63 ตรวจembeddedNUL/control chars/size/version ก่อนใช้ SaveเฉพาะconnectingและWL_CONNECTEDที่SSIDตรงattempt ขณะconnectingแก้fieldsไม่ได้ compareserializedblobก่อนputBytesเพื่อไม่writeunchanged success; failed/Cancel/lossไม่write
Serial cforgetcalibrationอย่างเดียวแล้วทำworkflow; Serial wforgetWiFiอย่างเดียว disconnect/clearRAMfields/returnformเมื่อcalibrationvalid มิฉะนั้นคงcircleจนfitผ่าน Save/erasefailแจ้งgenericSerial ไม่มีcredentials ไม่ลบdataอัตโนมัติ
NVSplaintext ไม่มีhardware flash/NVS encryption ผู้เข้าถึงflashอาจกู้passwordได้ wlogicalkeydeleteไม่ใช่securewipe; passwordvisibleตามคำขอ SavedcredentialsโหลดจากruntimeNVSเท่านั้นไม่ฝังsource/ไม่มีsecretfiles APIhistoryยังRAMเท่านั้น
Host tools/test_saved_settings.py compileactualcodec+extractedmainfit/save/startup/timeout/loss/forgetflowsกับPreferences/WiFi mocks; corruption/truncation/versions/profile/length/open/maxcredentials/nochangedwrites/nofailedwrites/saveerasefail และnosecretlogs ไม่ใช่physicalpower-cut/NVStest

## Libraries
LVGL 8.3.11, TFT_eSPI 2.5.43, XPT2046_Touchscreen 1.4, ArduinoJson 6.21.5;  เปิด LOAD_GFXFF เพื่อใช้ Nunito Bold 18/26px จาก Google Fonts (OFL 1.1) ASCII bitmap src/ui_fonts.h (~4.8KB) สำหรับ setup ไม่ต้องโหลดฟอนต์ผ่านเครือข่าย; LVGL เป็น runtime dependency ใหม่ของ dashboard
ต้นฉบับ assets/fonts/Nunito.ttf + OFL.txt; tools/generate_ui_fonts.py ใช้ Python/Pillow เฉพาะ regeneration (ไม่ใช่ build requirement)

## Build & Upload
ดู README.md ค่า default cyd_ili9341; cyd_st7789 เก็บไว้สำหรับ build compatibility เท่านั้น (บอร์ดจริงจอขาว) ไม่สำรอง firmware ตามคำขอผู้ใช้

## Current State
LVGL UI build ผ่านบน cyd_ili9341: flash1,180,409bytes90.1% / staticRAM85,812bytes26.2% ในapp partitionเดิม1310720bytes ไม่แก้partition/flash capacity ค่า esp32dev4MB เป็นconfigเท่านั้น ยังไม่อ่านขนาดflashจริงจากบอร์ด ไม่ต้องเลือกnoOTAเพื่อให้buildนี้ผ่าน
ผลตรวจHTTPS2026-10-14: 9URLs HTTP200+embeddedCA+schemas ผ่าน 12oilproductsครบ; SET history1จุด หุ้นอีก5ตัว22จุด
Host tests: tools/test_touch_mapping.py actual affine/94keyboardrects; tools/test_live_data.py actual C++ parsers/historyring/stale/debounce/gesturegate; tools/test_dashboard_render.py actual pinnedLVGL native render/partialflushbounds/fontwidths/tab/swipe/setupownership ผ่าน previews .pio/host_ui เป็น synthetic fixtures ไม่ใช่ live prices
ยังไม่flash/monitor/commit/แก้vault UI/pressure/swipe speed/สี RGB565/heap fragmentation/stack/TLS ต้องยืนยันบนบอร์ด ดูhardware_lib/READMEสำหรับตัวเลขbuildล่าสุดและขั้นตอนตรวจ

## loopTask stack-canary incident (2026-10-15)
ผู้ใช้พบ Guru Meditation: Stack canary watchpoint triggered (loopTask) หลัง Connect/บูตด้วยsaved config ตรวจและdecodeก่อนrebuild ยืนยัน SHA prefixตรงกับที่ผู้ใช้รายงาน; local ELF SHA256เต็ม: `9779515cb32b5beeb3c9df6774bf07dee994c0907e139d9684d236b74c16e916` ไม่ใช้ELFใหม่ตีความaddressเก่า เก็บoriginal ELF+decodedtraceในignored `.pio/crashes/9779515cb32b5bee/`

Backtrace top→caller จาก xtensa-esp32-elf-addr2line -a -f -C -i -p:

| PC | Function / original source location |
|---|---|
| 0x400eaadd | lv_area_copy inlined into draw_bg, lv_draw_sw_rect.c:105 (lv_area.h:113) |
| 0x400ebf1d | lv_draw_sw_rect, lv_draw_sw_rect.c:75 |
| 0x4018905d | lv_draw_rect, lv_draw_rect.c:66 |
| 0x400d290d | dashboard::box, dashboard.cpp:51 |
| 0x400d2fb1 | dashboard::draw, dashboard.cpp:250 |
| 0x400dba46 | event_send_core, lv_event.c:467 |
| 0x400dbaa2 | lv_event_send, lv_event.c:75 |
| 0x400e194d | lv_obj_redraw, lv_refr.c:148 |
| 0x400e1a19 | refr_obj, lv_refr.c:974/968 |
| 0x400e1d52 | refr_obj_and_children, lv_refr.c:856 |
| 0x400e1e83 | refr_area_part, lv_refr.c:791 |
| 0x400e2a77 | refr_area → refr_invalid_areas → _lv_disp_refr_timer, lv_refr.c:680/618/325 |
| 0x400e6b18 | lv_timer_exec → lv_timer_handler, lv_timer.c:313/109 |
| 0x400d3a03 | dashboard::tick, dashboard.cpp:325 |
| 0x400d7069 | loop, main.cpp:554 |
| 0x400f757d | loopTask, Arduino main.cpp:50 |

หลักฐาน root cause: Xtensa objdump original `loop` entry a1,0xd70=3440bytes และ `dashboard::tick`/`show` entry a1,0xcb0=3248bytes; Snapshot symbol size0xc90=3216bytes ค่าชั่วคราวจาก return-by-value + localstatsในbranch h ทำให้จองframeตลอดloop แม้ไม่กดh ขณะrenderมีloop+tickอยู่พร้อมกัน6688bytes ยังไม่รวมLVGLchain บนArduino defaultloop stack8192bytes UserA1span0x3ffb2290−0x3ffb02f0=8096bytes สอดคล้องstackหมด จุดlv_area_copyเป็นที่ตรวจพบ ไม่ใช่หลักฐานว่าNVSเสีย
LVGL event/render chainนี้เป็นการวาดปกติ ไม่พบcallbackrender recursionผิดปกติ: drawเรียกprimitive ไม่เรียกlv_timer_handler/lv_event_sendซ้ำ ไม่มีเหตุให้ตัดUIหรือเพิ่มstackเป็นวิธีหลัก

แก้ด้วย `copy_snapshot(Snapshot&)` copyตรงลงstatic UI-owned dataใต้portMUX ไม่มีSnapshotby-value/local/temporariesอีก workerยังเป็นผู้เขียนsharedคนเดียว UIเป็นผู้เขียนdisplaydataคนเดียว และไม่มีlockระหว่างnetwork/render Serialhอ่านDiagnosticsขนาด12bytesใต้lock ไม่อ่านhistoryทั้งหมด ยังคงloop/APIstack high-water bytes ไม่logcredentials ไม่แตะsaved_settings/schema/NVS/partition

| Compiled Xtensa frame | Original | Fixed |
|---|---:|---:|
| loop | 3440 | 240 |
| dashboard::tick | 3248 | 32 |
| dashboard::show | 3248 | 32 |
| copy_snapshot | — | 32 |
| diagnostics | — | 48 |

Stack-fix-only revision ELF SHA256 (ก่อนUIpolish) `8d0c0911813448a5d1e46793c29f98823caab4ddbe2ebb12fb04e11fb6c47054` Stack allocationยัง8192bytes ไม่มีheap/staticbufferเพิ่ม RAM85,708bytesเท่าเดิม Workerstackยัง12288bytes (compilerworkerframe2848bytes ไม่ใช่UI; peakTLSยังต้องวัดด้วยh)
Buildเปิด -fstack-usage / -Wframe-larger-than=2048 และpost-link tools/check_stack_frames.py ตรวจทุกmain/dashboardframe<=1024 copy/diagnostics<=256 ตรวจrequiredsymbols+missingreportsแล้วfailbuild ไม่มีการลดthresholdเพื่อซ่อนregression Self-testปฏิเสธoriginal3440byteframe Hosttest_live_dataเพิ่มactualcopy/diagnosticsและconcurrentpublish/copyผ่านmockportMUX พร้อมAPItypeguard ไม่ใช่หลักฐานruntime8KBstackผ่านจริง
ต้องให้ผู้ใช้ทดสอบsavedboot/manualConnect/all5pages/Serialh/TLS/reconnectจริงหลังติดตั้งรุ่นแก้ ตรวจloop-stack-min-free/API-stack-min-freeเป็นค่าต่ำสุดตั้งแต่taskเริ่ม (API0ยังไม่วัด) ไม่ใช่freeheap รอบแก้ไม่flash/erase/monitor/commit/vaultและไม่ลบsavedconfig

## Open Questions
Flash capacityจริง TBD ไม่ถือว่าconfig4MBเป็นhardware evidence; touchorientationและpressureยังต้องยืนยันบนบอร์ดจริง คงpinmap/rotation/calibrationเดิม
APIunofficial/delayed ไม่มีSLA อาจเปลี่ยนschema/root/429; goldไม่มีhistoricalAPIในendpointที่ใช้อยู่ SETcoverageจำกัดตามการตรวจจริง ไม่มีราคาตลาดโลก/การแปลงหน่วย/ข้อมูลจำลองในfirmware

## Issue Log
2026-10-22: หมุนทั้งแอป180°ด้วยTFTrotation3 คงtouchrotation1/NVSprofile/version/fitเดิม กลับfinalmappedxyและตำแหน่งcalibrationtargets ไม่ลบsavedsettings ไม่เปลี่ยนLVGLresolution/swipe semantics
2026-10-15: Weather-only30fps rationalcadence/displaytimer sync, larger cloud drift/bob/squash/breathinglobes และSerialh scheduled-vs-SPIcompletedROI telemetry NativeLVGLทดสอบ60refresh/2sตรงdeadline/late-skip/mixedexclude/morph/clip/poolผ่าน LatestXtensaframes loop256 tick48 show32bytes UImax256 ไม่เพิ่มloopstack/เปลี่ยนlogos/settings/API HardwareFPSยังไม่วัด
2026-10-15: Reference-logo/smoothmotionรอบใหม่: offlineSVG→8×antialiasRGB56560×56 flash20,160bytes PTT/PTreproductions(PTตัวแดง) Bangchakofficialpaths+wordcrop/recompose เปลี่ยนweatherframecounterเป็นelapsedtime/25fps-target/subtlecloudbob/trails/lightningenvelopes NativeLVGL/assetregen/frameguardผ่าน ไม่flash/erase/commit/vault ไม่ยืนยันhardwareFPS
2026-10-15: UIpolishที่อนุมัติ: ตราวาดย่อPTT/PT/Bangchakบนpump, weatheranimation100msตามexplicitWMO/clip/visibility, ตัดgoldgraphเพิ่มราคา44px/เวลาUPDATED Collector/API/NVS/touchไม่แก้ NativeLVGLpixel/dirtyregion/pool/font testsและXtensaframeguardผ่าน ภาพpreviewเป็นsyntheticfixtures ไม่ยืนยันhardwareFPS
2026-10-15: Decode SHA-matched loopTask canary → oversizedby-valueSnapshot frames แก้copyintoUIstatic+smallDiagnostics คง8KBloopstack/NVS เพิ่มXtensaframeguardและconcurrentsnapshottests Build/hosttestsผ่าน ต้องยืนยันfixบนhardware
2026-10-14: คำขอล่าสุดเพิ่มPreferences/NVS calibration+successfulWiFi versioned/validated blobs ข้ามcalibrationและauto-connectเมื่อrecordvalid เพิ่มcลบcal/wลบWiFiและhoststorageflowtests Build/4hosttestsผ่าน ไม่flash/monitor/commit/vault (supersedesRAM-only/every-boot notesด้านล่าง)
2026-10-14: ST7789 จอขาว; ILI9341 แสดงผล สีและสัดส่วนถูกต้องตามผู้ใช้ ตั้ง default เป็น cyd_ili9341
2026-10-14: เพิ่ม Neon Desk UI, Wi-Fi gate/keyboard และ Bangkok NTP; ไม่เพิ่ม dependency ไม่บันทึก credentials และไม่แก้ vault
2026-10-14: เคยเปลี่ยนเป็น WiFi-first ตามคำขอก่อนหน้า; คำขอล่าสุดเปลี่ยนเป็นคาลิเบรตทุกบูตแบบดำ/ขาว ข้อความเดียว ลูกศรเขียวตามมุม raw ที่รับสำเร็จ แล้ว WiFi หลัง 600 ms ไม่ใช้ Preferences และคง UI WiFi/dashboard เดิมในรอบนั้น
2026-10-14: ปรับลูกศร rounded และ Nunito Bold; ลด WiFi ให้เหลือ fields/buttons/status จำเป็น พร้อมแยก letters/numbers+symbols keyboard ปุ่มใหญ่ขึ้น ไม่เปลี่ยน dashboard/calibration logic
2026-10-14: ผู้ใช้รายงานปุ่มริมจอกดยาก ตรวจโค้ดพบ hitbox ใช้ helper เดียวกับวาด แต่เป้าลูกศรใหญ่/พิกัด fit เป็นปลายทำให้ตำแหน่งแตะกำกวม (สาเหตุจริงบนบอร์ดยังเป็น inference) และ map reject นอก+-12px เปลี่ยนเป็นวงกลมศูนย์กลางชัดเจน inset25px, shared target constants, edge extrapolation/clamp และ diagnostics/regression; แสดง password จริงตามคำขอ

2026-10-14: Major UI ที่ผู้ใช้อนุมัติ: LVGL8.3 swipe/tap dashboard ตลาดไทย/กราฟจริง/ทองobservedhistory/12oilproducts+BCPdisel คงsetupและpartition เพิ่มhostLVGLrender tests ไม่flash/monitor/commit/vault

## IDLE0 task watchdog incident (2026-10-22)
ELF SHA256 da0341f55e4d989f7dd55159600f82866c3f614574f9b185b7ab1fe8d992448f matched before rebuild; preserved .pio/crashes/da0341f55e4d989f/firmware.elf and decoded.txt. 0x401011d4=task_wdt_isr (task_wdt.c:176); 0x400838dd=panic_abort (panic.c:408). Log proves IDLE0 missed watchdog while api_worker ran CPU0; corrupted/unusable caller trace does not prove worker PC or stack corruption. Different from prior loopTask canary.
Inspected installed Arduino framework: Stream.cpp timedRead/timedPeek (31/45) busy-spin without sleep; readStringUntil used by HTTPClient header parsing invokes timedRead and has per-byte rather than whole-response deadline. App body loop already delayed; TLS handshake/write WANT loops already vTaskDelay(2). Therefore do not claim every TLS/body loop was missing a yield. Slow/dripping header retries are an evidenced starvation risk, not a hardware-confirmed exact crash location.
Fix: src/cooperative_tls.h overrides virtual read/peek/available/connected/write, sleeps >=1 FreeRTOS tick after >=2ms work (not taskYIELD/delay0), including Stream retries dispatched through Client pointer. 20s elapsed request budget stops transport on next poll; connect TCP4s/handshake5s/socket3s and Stream100ms timeouts remain finite. DNS/framework connect and individual mbedTLS calls are not preempted by this cooperative deadline, so it is not a hard real-time20s guarantee. No new supervising task/stack/watchdog reset/timeout increase. WiFiClientSecure.setTimeout units corrected to3 seconds; HTTPClient uses3000ms. Body uses available-bounded bulk read, cap24KiB/wall8s; JSON parses the completed String, never chunked Stream. HTTP1.0 requested and unexpected nonidentity transfer encoding rejected (-26), request expiry -25.
Serial API[index] request/headers/body/done timings identify last reached phase, status/error/bytes/stack-min only; indexes0weather1gold2oil3SET4PTT5PTG6AOT7ADVANC8CPALL. No credentials/response bodies/URLs logged. Sleeps/logs/network remain outside portMUX. Worker compiler frame2896 bytes on unchanged12288 stack; this does not prove TLS call-depth headroom. UIloop256/tick48/show32 guard unchanged. Runtime Serialh/endpoint done stack-min required. Host transport mock verifies virtual dispatch/sleep/deadline/wrap, not actual TLS or hardware IDLE scheduling.

### Follow-up: watchdog repeats before headers (2026-10-22)
รอบก่อนแก้ไม่ครอบคลุม: ผู้ใช้รายงานAPI[0]requestแล้วIDLE0watchdogซ้ำ ELF SHA256 2b5b431dcd7b2fa9b7b7c1ed66977b608bf25629d649890a3b4597d3e43b9661 ตรงก่อนbuild เก็บELF/decodedใน.pio/crashes/2b5b431dcd7b2fa9/ PC0x401016fc=task_wdt_isr:176 ไม่มีworkerPCที่ระบุconnectหรือheaderได้ชัด เพราะGETรวมหลายphase
Frameworkจริง HTTPClient.cpp:1162 เรียกvirtual connect(host,port,int32 timeout); CooperativeTLSรอบก่อนoverrideเฉพาะI/O ไม่overrideconnect WiFiClientSecure.cpp:120ส่งต่อconnect→hostByName→start_ssl_client ไม่มีการผ่านpollระหว่างภายในconnect WiFiGeneric.cpp:1566 DNSรอeventได้16sserialize+15slookup (เป็นblocking wait ไม่ใช่busyspin จึงไม่กล่าวโทษDNSว่าแย่งIDLE0); ssl_client.cpp TCPselect4s handshakeWANTloopsพัก2ticks แต่crypto/entropyเป็นopaque synchronous calls ไม่ทราบจุดที่กินCPUจากlogเดิม
แก้schedulingจริง: api_worker priority0เท่าIDLE0 คงcore0/12KiB มีstatic_assertconfigUSE_PREEMPTION/configUSE_TIME_SLICING=1 SDKที่ติดตั้งconfigIDLE_SHOULD_YIELD=0 IDLE0จึงได้time-sliceพร้อมidlehooksแม้APIติดอยู่ในlibraryCPUloopที่interruptยังทำงาน ไม่ปิด/เลี้ยงแทน/ยืดwatchdog ไม่เพิ่มtask supervisory ไม่taskdeleteTLSขณะถือresource หากlibraryปิดinterruptหรือcriticalนานวิธีนี้ไม่รับรองและต้องเก็บPCจริงเพิ่มเติม
เพิ่มoverrideconnectทั้งhost2/3args HTTPClientdispatchตรง: phase=dns start/done แล้วphase=tcp-tls start/done (TCPและTLSยังรวมเพราะframeworkstart_ssl_clientรวมกัน) resolveครั้งเดียวแล้วต่อIPพร้อมoriginalhostname/CAเพื่อคงSNI/hostnameverification TCP4s handshake5s socket3s Stream100ms body8s/24KiB cooperativepollbudget20sเดิม ไม่มีการเพิ่มtimeout DNSframeworkไม่มีcaller-timeoutargument; DNSอาจนานกว่า20sแต่เมื่อคืนมาหากหมดbudgetไม่เริ่มTLS ไม่มีharddeadlineตัดopaqueTLS/entropycall
Fallback/APIcircuit: ApiRetryแยก9แหล่ง failretry30s/60s จากfailure3เปิดcircuitอย่างน้อย5min cap15min successresetและกลับweather10min/finance15min Yahoo429ยังcooldownทั้งhost15min แหล่งfailไม่continueวนretryค้างก่อนแหล่งถัดไป doneระบุretry-ms/failures/fallback การfailคงlastgoodค่า/source timestamp/historyเดิมในRAMพร้อมCACHED ถ้าไม่เคยมีข้อมูลแสดง--/UNAVAILABLE ไม่แต่งข้อมูลหรือเปลี่ยนปั๊ม/ราคา/endpoint ไม่มีNVSlastgoodwritesหรือAPIalternatehostที่ไม่verify Clock/gesture/UIคนละcoreยังทำงานระหว่างรอ API0คือOpen-MeteoBangkok
Hosttestscompileactualconnectoverrideผ่านbasepointer DNSfail/TLSfail/circuitcap/recovery/wrap/sourceisolation + actualmark_failureเก็บoldtimestamp/history/availabilityและYahoo429 ผ่าน Mockไม่รับรองIDLE0runtime/TLSบนบอร์ด Workercompilerframe2960bytes/12KiB UIloop256/tick48/show32 ผ่านguard ต้องวัดh/stack-minจริง ไม่มีการเปลี่ยนorientation/calibration/WiFi/CA

### TLS follow-up: weather/gold/oil unavailable, Yahoo works (2026-10-22)
Userhardwarelogรายงานไม่มีwatchdogแล้ว: API0DNSsuccess/TCP-TLSfail~12196ms API1/2fail~5755ms start_ssl_client=-1 ขณะที่Yahoo3..8HTTP200+parseok~3s/stackmin4068 เป็นหลักฐานschedulingดีขึ้น แต่ไม่ยืนยันfailureเป็นCAหรือSNI
ตรวจinstalledArduino2.0.17จริง: sixargumentconnect(IP,port,hostname,CA,cert,key)ส่งhostnameตรงเข้าstart_ssl_client และmbedtls_ssl_set_hostname ไม่ใช่IPonlyoverload ไม่มีSNIomissionให้แก้ จึงคงresolveครั้งเดียวและexacthostnameเดิม Regressionmockบังคับhostname/CAตรงและdeleteIPonlyoverload ส่วนค่า-1มีหลายreturnpathรวมTCP/handshaketimeout ไม่ใช่รหัสX509เฉพาะ ไม่กล่าวโทษCA/APIจาก-1
Hostverified2026-10-22: verify_apis.pyทั้ง9URLsHTTP200/embeddedCA/schemaผ่าน oilครบ12products; SET1historypoint หุ้น5ตัว21points DirectTLS1.2 (ไม่มีproxy) จากverify_tls_hosts.py ใช้เฉพาะPEMในapi_roots.h พร้อมhostnameverificationสำเร็จทั้ง3hosts OpenMeteoissuerLetsEncryptYE2 leafvalid2026-10-19..2027-01-17/chainISRGX1, CHNWTissuerGTSWE1 valid2026-09-04..12-03/chainGTSR4, YahooDigiCertGlobalG2TLSRSASHA2562020CA1 valid2026-09-29..11-18 cipherECDHE-ECDSA-AES256-GCM-SHA384สำหรับweatherและAES128-GCM-SHA256อีก2hosts ไม่เปลี่ยนCA/ไม่insecure/ไม่downgradeHTTP HostOpenSSLผลผ่านไม่รับรองembeddedmbedTLSเวลาเครื่องหรือperformance
เวลาล้มเหลว~5.7sสอดคล้องhandshakelimit5sเดิม (weatherบางครั้งopaquecryptoทำให้คืนมาช้า12s) เป็นtimeout/slow-chainhypothesisไม่ใช่hardwareproof ปรับเฉพาะAPI0..2handshaketimeout20sและcooperativebudget35s เพื่อให้20shandshake+TCP4sมีheadroom Yahooคง5s/20s TCP4s/socket3s/body8s/24KiB unchanged DNSมี16+15sframeworkeventwaitและopaqueTLSไม่preemptdeadline จึงไม่รับรอง35shardwall นี่ไม่ใช่เพิ่มwatchdogtimeout Workerยังpriority0/12KiB/timeslicingและretrycircuitเดิม
เพิ่มlogก่อนhandshake: epochUnix/free8bitheap/largestblock/handshake-s/budget-ms หลังfailใช้WiFiClientSecure.lastErrorทั้งsignedcode/hex/mbedTLStextก่อนobjectถูกทำลาย ไม่มีcredentials/bodies/privatekeys Generic-1ยังเป็นgenericไม่labelbadCA Newnative testsยืนยันerror-9984และ-1ไม่ถูกแปลง, SNI+CA, per-sourcebudgets/deadline/expiry/wrapผ่าน Workercompilerframe2976บน12KiBและUIloop256/tick48/show32guardผ่าน RuntimeTLSheadroomต้องวัดhจริง คงcalibration/NVS/rotation3/ราคา/history
