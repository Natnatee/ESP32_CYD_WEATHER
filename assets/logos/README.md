# Pump logo assets / ที่มาและข้อจำกัด

ใช้เพื่อระบุสถานีบนจอเท่านั้น เครื่องหมาย/ลิขสิทธิ์เป็นของเจ้าของแบรนด์ ไม่ใช่ endorsement และไม่ได้อ้างว่าได้รับสิทธิ์ใช้ตราอย่างเสรี

- `ptt_reproduced.svg`: วาด Bezier ใหม่ตามลักษณะอ้างอิงของผู้ใช้และตรา PTT: cyan flame สองยอด, navy inner teardrop, red disc, thin pale boundaries บนขาว **เป็น reproduction ไม่ใช่ไฟล์ทางการ** อ้างอิงแบรนด์: https://www.pttplc.com/
- `pt_reproduced.svg`: วาดใหม่ตาม reference ที่ผู้ใช้อธิบาย: red italic overlapping P/T, white interior, lime upper-left/dark-green lower-right split ring มีช่องว่าง NE/SW **เป็น approximation/reproduction ไม่ใช่ไฟล์ทางการ** ไม่ใช่ตัว PT ขาวแบบเดิม
- `bangchak_official.svg`: ดาวน์โหลดจาก public brand site ผ่าน HTTPS วันที่ 2026-10-15: https://www.bangchak.co.th/assets/images/logo.svg (HTTP200) SHA256 `52ace08c56d04a6c1c34220355a4816f458cb8e6ad40bc4a8488560e176ac56e` ตราและ lowercase `bangchak` ใช้ path/gradient จากไฟล์นี้จริง แต่ **crop/recompose** บน rounded white/green plate สำหรับหน้าปั๊ม ไม่ใช่ official lockup/export เดิม การเปิดไฟล์สาธารณะไม่ใช่การให้ license แบบ unrestricted; ตรวจ brand usage terms ก่อนเผยแพร่/ใช้เชิงพาณิชย์

ไม่มี JavaScript, external image/font, DOCTYPE หรือ external SVG references ใน sources ที่ใช้ ตัว generator ตรวจอีกครั้งและไม่ดาวน์โหลดข้อมูลขณะสร้าง/build/ทำงานบนบอร์ด

## Generate / reproduce

```sh
python -m pip install -r tools/requirements-assets.txt
python tools/generate_station_logos.py
python tools/generate_station_logos.py --check
```

ใช้ `resvg-py` SVG path renderer เฉพาะเครื่องมือพัฒนา ไม่ใช้ Cairo DLL หรือ rasterizer บน ESP32 และ Pillow/Lanczos ลดจาก 8x supersampling เป็น RGB565 little-endian **60×56 pixels ต่อโลโก้** 6,720 bytes ต่อรูป รวม **20,160 bytes** ใน flash บวก descriptors เล็กน้อย ไม่ใช้ runtime PNG/SVG decoder หรือ framebuffer ของโลโก้ใหม่

ไฟล์ generated ที่ใช้ build ปกติ: `src/station_logos.h/.cpp` (ไม่ต้องมี Python/resvg/Pillow เพื่อ build firmware) ไม่แก้ bytes ด้วยมือ Previews ใน ignored `.pio/host_ui/logo-*-60x56.png` ไม่มีการเขียนทับรูป hardware ของผู้ใช้ใน `assets/debug/`
