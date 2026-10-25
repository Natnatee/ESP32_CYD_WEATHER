# Hardware & Library Specs

## Hardware
| Part | Model | Interface | Notes |
|---|---|---|---|
| MCU | ESP32-WROOM-32E | USB UART | บอร์ด HW-458 |
| Display | ILI9341 (ยืนยันแล้ว) | HSPI 14/13/12 CS15 DC2 | landscape 320x240 display rotation 3 (180° from previous), BL21 |
| Touch | XPT2046 (pin map provisional) | VSPI CLK25/MISO39/MOSI32 CS33 | IRQ36 ไม่ใช้เป็น gate; อ่าน pressure polling |

## Libraries
| Purpose | Library | Status | Notes |
|---|---|---|---|
| TFT/UI | TFT_eSPI 2.5.43 | implemented | setup custom UI + LVGL flush, ILI9341 default, LOAD_GFXFF |
| Dashboard | LVGL 8.3.11 | pinned/implemented | main-only, 8-row partial RGB565, Montserrat18/26, static pool24KiB |
| UI font | Nunito Bold (Google Fonts) | embedded ASCII | rounded sans 18/26px, OFL 1.1 assets/fonts/OFL.txt, src/ui_fonts.h |
| Touch | XPT2046_Touchscreen git v1.4 | hardware validation pending | VSPI แยกจาก TFT, validated affine calibration ในNVS ข้ามเมื่อบูต |
| Wi-Fi | Arduino ESP32 WiFi | implemented | station2.4GHz nonblocking/bootautoattempt, successful credentialsในNVS; SDKpersistent(false) |
| Storage | Arduino Preferences 2.0.0 (framework) | implemented | cyd_settings namespace, versioned/CRC single blobs, plaintextไม่encrypted |
| Time | ESP32 SNTP / time.h | implemented | Bangkok UTC+7, pool.ntp.org / time.google.com; valid clock required before HTTPS |
| JSON | ArduinoJson 6.21.5 | pinned/implemented | filtered 6KiB document + filter1.5KiB, strict missing/schema validation |
| HTTPS | framework HTTPClient / WiFiClientSecure | implemented | verified PEM roots src/api_roots.h; no insecure mode |
| Network worker | framework FreeRTOS | implemented | core0 stack12KB priority0 (idle/time-sliced), POD snapshot portMUX; TFT/touch UI thread only |

## Datasheet / References
- https://github.com/Bodmer/TFT_eSPI
- https://github.com/PaulStoffregen/XPT2046_Touchscreen
- https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display

## Hardware Issues
ผู้ใช้ยืนยัน ILI9341 สี/สัดส่วนถูกต้อง ST7789 จอขาว จึงคง default ILI9341 ไม่มี auto-detect ขาและแกนทัชยังต้องทดสอบกับบอร์ด; touch.begin() ไม่ใช่การตรวจพบชิป

## Test Notes
เมื่อไม่มีcalibrationที่ใช้ได้หรือสั่งSerial c แสดงคาลิเบรตพื้นดำ วงกลมขาวรัศมี9px มีจุดกลางที่ (25,25)/(294,25)/(294,214)/(25,214) ลูกศรมนเล็กชี้จากนอกวงกลม ข้อความเดียว `Click center of circles` Nunito Bold26px สองบรรทัด แตะจุดกลางวงกลม (ไม่ใช่ลูกศร) ประมาณ80ms แล้วปล่อย มุมใดก่อนก็ได้ มุมผ่านเป็นเขียว; ครบและ affine fit ผ่านรอ 600 ms แบบไม่บล็อกแล้วเข้า WiFi Serial `c` ลบcalibrationแล้วเริ่มใหม่ บันทึกNVSหลังครบ4มุม/fitผ่าน บูตครั้งต่อไปโหลดrecordvalidatedแล้วข้าม; WiFirecordยังอยู่ ไม่มีcredentialsในlog

WiFi ใช้ Nunito Bold 18px fields/keys/status; Password แสดงข้อความจริงตามคำขอ ไม่log แต่persistNVSเฉพาะเมื่อเชื่อมสำเร็จ;  keyboard 3 แถวตัวอักษรแยกจาก numbers+symbols สองชุด ปุ่ม29–44x29px พร้อม controls26px ไม่มีไอคอน/helper จิ๋ว ไม่เพิ่ม runtime library; regenerate font ด้วย Python/Pillow ผ่าน tools/generate_ui_fonts.py เท่านั้น

Corner classifier อิง rotation1 raw x เพิ่มขวา/y เพิ่มลงล่าง และ raw 200..3900: ต้องอยู่ภายนอก 25% ทั้งสองแกน (<=1125 หรือ >=2975) ตรวจ 4 samples/spread<=100 และ release 3 polls ก่อนรับ; ไม่ให้ repeated corner เติมมุมอื่น ตรวจ closure/determinant/fit residual เมื่อครบก่อนแสดงเขียวทั้งหมด หากผิด reset เป็นขาว

Fit/map/draw ใช้ constants ศูนย์กลางวงกลมชุดเดียวกัน, affine extrapolate ถึงขอบแล้ว clamp ไม่ทิ้ง valid ADC เพียงเพราะ mapped เกินขอบ Pressure>=350/debounceคงเดิม; Serial `t` raw/pressure/mapped/hit-only diagnostics ไม่มี characters/credentials, Serial `c` ทำ calibration ใหม่ Logs fit coefficients/residual ช่วยแยกปัญหาเป้าแตะ/แกน/pressure
Regression `python tools/test_touch_mapping.py` ใช้ g++ กับฟังก์ชัน firmware จริงบน synthetic ADC; ยังไม่ยืนยันการกดริมจอจริง

ไม่สามารถ infer orientation ที่สลับ/กลับแกนจาก unordered corners ล้วน ๆ ได้ ต้องยืนยัน orientation บอร์ดก่อน; affine แก้ skew/cross-axis หลังระบุชื่อมุมถูกแล้ว

Dashboard LVGL8.3.11 ตลาดไทย5หน้า ปัดซ้าย/ขวาไม่มีnavbar แตะเลือก6หุ้น/3สถานี Inputaffine/main20ms/pressure350/debounceกด2ปล่อย3 ไม่ใช้stationaryfilterบนdashboardเพื่อให้swipeเริ่มได้ คงWiFi/calibrationเดิม ต้องreleaseConnectก่อนarm เปลี่ยนหน้าเมื่อreleaseและไม่ให้swipeกลายเป็นtabtap

SingleRGB565320x8buffer5120bytes + LVGL24KiBpool static ไม่มีfullscreensprite/PSRAM ไม่ใช้GFXfontเป็นLVGLfont Montserrat18/26builtinเท่านั้น initializeหลังgate mainเป็นผู้เรียกLVGL/TFTทั้งหมด hideก่อนsetup หยุดtimer/flushขณะsetup Workerไม่มีจอ/ทัช

Latest build cyd_ili9341: flash1,180,409/1,310,720bytes (90.1%), staticRAM85,812/327,680bytes (26.2%); ไม่รวมTLS/body24KiB/JSON6KiB/filter1.5KiB/workerstack12KiBและruntimeheap ไม่เคยตรวจflashcapacityจริง ค่าesp32dev4MBเป็นconfig ไม่เปลี่ยนpartition/ไม่เลือกnoOTA/ไม่flash

APIverified2026-10-14: 9URLs HTTP200withfirmwareCA YahooSET1historypoint vsอีก5หุ้น22points; oil12productsครบ keysptt/pt/bcp BCPdiesel=disel ไม่ทดแทนmissing; goldobservedSELL collector RAM32points/15minยังอยู่ แต่หน้าทองตัดกราฟออกแล้ว ใช้ราคา7segments44px/UPDATED26pxและfullsource stamp18px StocksTHB/SETpoints goldTHB/bahtweight oilTHB/L ไม่มีราคาตลาดโลกแล้ว อ่านREADME/projectสำหรับcoverageและrate limits

Serial hอ่านmetadata12bytesเท่านั้น ให้free/minheap/largest8bit/loop-stack-min-free/API-stack-min-free (high-waterเป็นminimumตั้งแต่taskเริ่มในหน่วยbytes; 0APIยังไม่วัด) ให้ผู้ใช้ตรวจระหว่างTLS/reconnect/runtimeหลายรอบ ไม่เปิดmonitorให้เอง HostactualLVGLrenderpool/partialbounds/fontmetrics testsไม่ใช่hardwareheapmeasurement

Preferencesstorageใช้cal40bytes/wifi13..107bytes magic/version1/CRC32/profile/geometry/ASCIIlength validation explicitLEfieldsไม่มีrawstructpadding Singleblobต่อrecord writeเฉพาะsuccessที่ข้อมูลเปลี่ยน `WiFi.persistent(false)` ไม่มีSDKduplicatewrites Bootautoattempt spinner/20stimeout ถ้าfailformeditable ไม่ลบlastgood `w`ลบเฉพาะWiFi `c`ลบเฉพาะcalibration ถ้าcalยังinvalidต้องทำวงกลมก่อนใช้ฟอร์ม
NVSเป็นplaintext ไม่เปิดflash/NVSencryption Physicalflashaccessอาจrecoverpasswordได้ wไม่ใช่securewipe Hoststoragetestsผ่านกับmocksแต่ยังต้องทดสอบrealpowercut/NVS/reboot/autoconnect/releasegateบนบอร์ด

2026-10-15: ผู้ใช้รายงานloopTaskcanaryหลังConnect/savedboot ELFprefix9779515cb32b5beeยืนยันตรงก่อนdecode จุดเกิดLVGLdraw_bg แต่rootcauseเป็นloop3440+tick3248bytesจากSnapshot3216byteby-value บนstack8192 แก้copy_snapshotตรงลงUIstatic+smallDiagnostics ไม่เพิ่มstack/heap/staticRAM ไม่แตะNVS/partition ค่าXtensaframeใหม่loop240 tick/show32 copy32 diagnostics48bytes BuildpostlinkguardUI<=1024/copymetadata<=256 +framewarning2048 รายละเอียดchain/PC/SHAในproject.md Workercompilerframe2848บน12KBstackยังต้องวัดpeakTLSจริง

2026-10-15 UIpolish: ตราPTTflameblue/red/PTgreencircle/Bangchakgreenorangeleafเป็นLVGLhand-drawn approximationsบนpump ไม่ฝังbitmapasset/officialSVG ตรวจvisualrefsPTT/Bangchakผ่านHTTPS PTGSSL/TLSล้มเหลวจึงใช้userbriefโดยไม่bypassverify Weather100msเฉพาะpage3visible/knownvalidWMO: suns/clouds/rain/snow/fog/lightning/hail clip107x84 animation-onlydirtyflushรวมLVGL5pxmargin117x94สูงสุด (data/page/statuschangesยังfullredrawได้) ไม่paintบนlabels fastpathcard/artข้ามfloat/date/textformatต่อanimation-onlyframe (nativecounterทดสอบ) ไม่มีper-frameappheap/Snapshottemporary/handlerreentry ใช้Montserrat18/26เดิม Buffers/stack/NVSเดิมเพิ่มเพียง8staticbytesสำหรับanimationstate NativeLVGLmotion/dirtypixel/pool/font testsผ่าน ไม่ใช่hardwareFPS/heapmeasurement

2026-10-15 latest: small-logoassets3×60×56RGB565LE=20,160flashbytes (PTT/PTBezierreproductions, PTตัวแดงเอียงวงแหวน2สี; BangchakofficialpublicSVGcrop/recomposeพร้อมbangchakword) Antialias8×/Lanczos offline tools/generate_station_logos.py dependencyresvg-py/Pillowเฉพาะdeveloper ไม่มีruntimeSVGdecoder/textureframebuffer/Hoseทับword ภาพและdescriptorsconstflash ไม่คัดลงRAM
Weather latest target30fps rational34/33/33ms ไม่มีdrift displaytimerreadyก่อนsinglehandler แก้LVGL20msquantizationโดยไม่เปลี่ยนglobalperiod Cloud drift±8/6.5px bob±2.6/2.3px squash/stretch6s independentlobes3/4/5s WeatherPose48bytesstatic ไม่มีallocationต่อframe Serialhแสดงscheduled/presented-roi FPS window>=2s นับlastSPIchunkเฉพาะrefreshภายใน117x94ทั้งหมด แยกmixed/skipped ไม่ใช่physicalLCDscanrate Warmupwindow0 hiddenactive0 NativeLVGL60completions/2sตรงdeadline/late14skips/morph/clip/poolผ่าน Latestloop256 tick48 show32bytes UImax256/8192loopstackเดิม SPI27MHz/bufferเดิม

ยังไม่ได้ทดสอบรุ่นโลโก้ใหม่/30fps-target/แก้stackบนบอร์ดจริง หากไม่มีtouchresponseให้ตรวจVSPIpinmapก่อน หากแตะไม่ตรงให้วัดraw/ตรวจแกนแล้วคาลิเบรตใหม่ คงGPIO36ไม่มีinternalpull-up ไม่มีการแก้pinmap/pressureแบบเดา

Display orientation: TFT rotation3 สำหรับทุกหน้า; XPT2046 rotation1 และ NVS affine/profile/versionเดิมยังเป็นcanonical rotation1 แปลงพิกัดหลังmap/clampเป็น(319-x,239-y)ก่อนUI เป้าคาลิเบรตใหม่วาดที่ตำแหน่งกลับด้านตามcanonicalcorner ไม่เปลี่ยนrawclassifier/fit ไม่ต้องลบหรือคาลิเบรตค่าเก่าใหม่ Swipeใช้พิกัดหน้าจอปัจจุบัน

2026-10-22 IDLE0 watchdog: SHA-matched ELF preserved/decoded (project.md). CooperativeTLS blocks at least1tick every>=2ms polling work across HTTP Stream header retries; 20s request poll deadline plus bounded body8s/24KiB, socket3s/Stream100ms, rejects unexpected chunked response instead of parsing framing. TLS CA unchanged. api_worker priority1/core0/12KiB unchanged; compilerframe2896bytes runtimeTLSheadroomunknown. Perendpoint APIphase logs reveal stalls without secrets. Root candidate evidenced framework timedRead busyspin; exact hardware PC unknown. No watchdogdisable/reset/timeoutincrease or orientation/NVS edits.

2026-10-22 watchdogfollow-up supersedespriorpolling-onlyfix: workerpriority0เท่าIDLE0/SDKpreemption+timeslicing1/IDLE_SHOULD_YIELD0 ไม่เปลี่ยนwatchdog พร้อมoverrideHTTPconnecthost2/3args แยกDNSและTCP-TLSphase OriginalSNI/CAคงเดิม DNSframeworkมี16+15sblockingwaitไม่มีtimeoutargument (ไม่รับรอง20sharddeadline) Workerframe2960bytes stack12KiBต้องวัดruntime Retryรายsource30/60s then circuit>=5min cap15min successreset; failคงrealcachedRAMหรือ-- ไม่มีfallbackendpoint/ราคาทดแทน/เพิ่มNVSwrite UI/rotation3/touchcanonical1เดิม

2026-10-22 TLSfollow-up: userlogไม่มีwatchdog/Yahooผ่าน แต่weather/gold/oilstart_ssl_client=-1 DNSผ่าน sixargumentconnectส่งhostname/SNIถูกอยู่แล้ว ไม่เปลี่ยนCA HostexactPEM+directTLS1.2และ9HTTPschemasผ่าน TargetedAPI0..2handshake20s/budget35s (เดิม5/20s); Yahoo5/20เดิม TCP4s/socket3s/body8sเดิม Generic-1ยังไม่ยืนยันcause เพิ่มlastErrorcode/hex/text+epoch+free/largestheapก่อนhandshake NativeSNI/CA/error/budgettestsผ่าน Workerframe2976/12KiB ไม่เพิ่มstack UIloop256/tick48/show32 Nativeผลไม่รับรองhardwareTLSต้องทดสอบต่อ ไม่มีflash/erase
