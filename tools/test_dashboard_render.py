"""Build actual pinned LVGL/dashboard on host, test layout/selection/ownership.
Requires gcc/g++ and optional Pillow for .pio/host_ui/fixture-*.png previews.
All displayed prices are SYNTHETIC TEST FIXTURES, not downloaded market data.
No board connection. Generated/cache files are confined to ignored .pio/host_ui.
"""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
LVGL = ROOT / ".pio/libdeps/cyd_ili9341/lvgl"
OUT = ROOT / ".pio/host_ui"
assert (LVGL / "lvgl.h").exists(), "Run pio run -e cyd_ili9341 first"
OUT.mkdir(parents=True, exist_ok=True)


def write_if_changed(path, text):
    if not path.exists() or path.read_text(encoding="utf-8") != text:
        path.write_text(text, encoding="utf-8")


write_if_changed(OUT / "Arduino.h", """#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#ifdef _WIN32
inline tm *localtime_r(const time_t *value, tm *out) {
    const tm *result=localtime(value); if(!result)return nullptr;
    *out=*result; return out;
}
#endif
extern uint32_t host_millis;
inline uint32_t millis() { return host_millis; }
""")
write_if_changed(OUT / "TFT_eSPI.h", """#pragma once
#include <cstdint>
#include <cassert>
struct TFT_eSPI {
    uint16_t framebuffer[320*240]{};
    int x=0,y=0,w=0,h=0,flushes=0,total_pixels=0;
    int min_x=320,min_y=240,max_x=-1,max_y=-1;
    void reset_capture(){total_pixels=0;min_x=320;min_y=240;max_x=max_y=-1;}
    void startWrite() {} void endWrite() {}
    void setAddrWindow(int ax,int ay,int aw,int ah) {
        assert(ax>=0 && ay>=0 && ax+aw<=320 && ay+ah<=240);
        assert(aw*ah<=320*8); // Same bounded partial buffer as firmware.
        x=ax;y=ay;w=aw;h=ah;
    }
    void pushColors(uint16_t *pixels,int count,bool) {
        assert(count==w*h); ++flushes;total_pixels+=count;
        if(x<min_x)min_x=x;if(y<min_y)min_y=y;
        if(x+w-1>max_x)max_x=x+w-1;if(y+h-1>max_y)max_y=y+h-1;
        for(int i=0;i<count;++i) framebuffer[(y+i/w)*320+x+i%w]=pixels[i];
    }
};
""")
harness = r'''
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <cstdarg>
int formatted_values=0;
int host_snprintf(char *out,size_t size,const char *format,...) {
    ++formatted_values;va_list args;va_start(args,format);
    const int result=vsnprintf(out,size,format,args);va_end(args);return result;
}
#define snprintf host_snprintf
#include "dashboard.cpp"
#undef snprintf
uint32_t host_millis=0;
live_data::Snapshot fixture;
namespace live_data {
void copy_snapshot(Snapshot &destination) { destination = fixture; }
bool stale(const Reading &r,Source,uint32_t) { return r.failed; }
}
void fits(const char *text,int width,bool large=false) {
    auto font=large?&lv_font_montserrat_26:&lv_font_montserrat_18;
    int actual=lv_txt_get_width(text,strlen(text),font,0,LV_TEXT_FLAG_NONE);
    if(actual>width) { fprintf(stderr,"LAYOUT OVERFLOW: %s (%d > %d)\n",text,actual,width); }
    assert(actual<=width);
}
void frame(TFT_eSPI &tft,int page,const char *variant="data") {
    dashboard::show(page);
    host_millis+=25; dashboard::tick(host_millis);
    std::string name="fixture-"+std::to_string(page)+"-"+variant+".ppm";
    auto file=fopen(name.c_str(),"wb"); assert(file);
    fprintf(file,"P6\n320 240\n255\n");
    for(auto pixel:tft.framebuffer) {
        uint8_t rgb[]={uint8_t(((pixel>>11)&31)*255/31),uint8_t(((pixel>>5)&63)*255/63),uint8_t((pixel&31)*255/31)};
        fwrite(rgb,1,3,file);
    }
    fclose(file);
}
uint32_t image_hash(const TFT_eSPI &tft) {
    uint32_t hash=2166136261U;
    for(uint16_t pixel:tft.framebuffer)hash=(hash^pixel)*16777619U;
    return hash;
}
int logo_pixels(const TFT_eSPI &tft,uint32_t rgb) {
    const auto color=lv_color_hex(rgb).full;int count=0;
    for(int y=91;y<150;++y)for(int x=22;x<82;++x)count+=tft.framebuffer[y*320+x]==color;
    return count;
}
int logo_hue_pixels(const TFT_eSPI &tft,bool orange) {
    int count=0;
    for(int y=91;y<147;++y)for(int x=22;x<82;++x) {
        const auto pixel=tft.framebuffer[y*320+x];
        const int r=((pixel>>11)&31)*8,g=((pixel>>5)&63)*4,b=(pixel&31)*8;
        count+=orange ? (r>180 && g>65 && g<210 && b<80) : (g>90 && g>r+25 && g>b+25);
    }
    return count;
}
void test_motion_time(TFT_eSPI &tft) {
    using namespace dashboard;
    sample_weather(1000);const float x0=weather_pose.cloud_x;
    sample_weather(1040);assert(fabsf(weather_pose.cloud_x-x0)>0 && fabsf(weather_pose.cloud_x-x0)<0.2f);
    assert(weather_pose.flash==0);
    sample_weather(1840);assert(weather_pose.flash>100);
    sample_weather(2050);assert(weather_pose.flash==0);
    sample_weather(2240);assert(weather_pose.flash>40);
    sample_weather(2400);assert(weather_pose.flash==0);
    fixture.readings[live_data::WEATHER].valid=true;
    fixture.readings[live_data::WEATHER].values[3]=61;
    frame(tft,3,"motion-clock-test");
    weather_elapsed_ms=0;sample_weather(0);
    for(int i=0;i<10;++i){host_millis+=40;tick(host_millis);}
    const float regular=weather_pose.cloud_x;assert(weather_elapsed_ms==400);
    weather_elapsed_ms=0;sample_weather(0);
    host_millis+=400;tick(host_millis);
    assert(weather_elapsed_ms==400 && fabsf(weather_pose.cloud_x-regular)<0.00001f); // Not frame-count speed.
    weather_cadence.reset();tft.reset_capture();host_millis+=20;tick(host_millis);assert(tft.total_pixels==0);
    // Unsigned elapsed arithmetic remains correct across millis() wrap.
    last_tick=UINT32_MAX-15;weather_cadence.reset();host_millis=24;
    weather_elapsed_ms=0;tick(host_millis);assert(weather_elapsed_ms==40);
    // Restore a modest time so unrelated status-age refresh does not affect ROI checks.
    host_millis=1000;last_tick=host_millis;last_age_refresh=host_millis;
    set_weather_running(false,host_millis);
}
void test_cadence_and_presented_fps(TFT_eSPI &tft) {
    WeatherCadence cadence;
    uint32_t frames=0,previous=0;
    for(uint32_t ms=1;ms<=10000;++ms) {
        const auto slots=cadence.advance(1);
        if(slots) {
            assert(slots==1);++frames;
            assert(ms==(frames*1000+29)/30); // Exact ceil(n * 1000 / 30), not 20ms timer quantization.
            assert(ms-previous==33 || ms-previous==34);previous=ms;
        }
    }
    assert(frames==300 && cadence.credit==0);
    assert(cadence.advance(500)==15); // One render with 14 skipped slots, not a catch-up burst.
    cadence.reset();uint32_t sum=0,slots=0;
    for(int i=0;sum<10000;++i) {
        uint32_t delta=(i%3==0?7:i%3==1?29:83);if(delta>10000-sum)delta=10000-sum;
        sum+=delta;slots+=cadence.advance(delta);
    }
    assert(slots==300 && cadence.credit==0);
    using namespace dashboard;
    fixture.readings[live_data::WEATHER].valid=true;fixture.readings[live_data::WEATHER].values[3]=3;
    frame(tft,3,"cadence-lvgl");
    set_weather_running(false,host_millis);set_weather_running(true,host_millis);
    weather_elapsed_ms=0;sample_weather(0);const auto started=host_millis;
    uint32_t completions=0;
    for(uint32_t ms=1;ms<=2000;++ms) {
        const int before=tft.flushes;host_millis=started+ms;tick(host_millis);
        if(tft.flushes!=before) {
            ++completions;assert(ms==(completions*1000+29)/30);
            assert(!refresh_started); // Last SPI chunk, not one count per partial buffer.
        }
    }
    const auto stats=weather_stats();
    assert(completions==60 && stats.window_ms==2000 && stats.scheduled_fps_x10==300);
    assert(stats.presented_roi_fps_x10==300 && !stats.skipped_slots && !stats.mixed_refreshes);
    host_millis+=500;const int before=tft.flushes;tick(host_millis);
    assert(tft.flushes>before && scheduled_frames==1 && skipped_slots==14); // Still just one fresh refresh.
    const uint32_t count=presented_frames;
    lv_obj_invalidate(root);host_millis+=34;tick(host_millis);
    assert(mixed_refreshes==1 && presented_frames==count); // Full data refresh excluded from ROI FPS.
    hide();assert(!weather_stats().active);
}
void test_cloud_morph(TFT_eSPI &tft) {
    using namespace dashboard;
    fixture.readings[live_data::WEATHER].values[3]=3;frame(tft,3,"morph-check");
    uint32_t first=0;
    for(int i=0;i<2;++i) {
        sample_weather(i?2100:0);
        weather_pose.cloud_x=weather_pose.cloud_y=weather_pose.back_x=weather_pose.back_y=0;
        weather_cadence.reset();
        lv_obj_invalidate_area(root,&WEATHER_ART_AREA);lv_timer_ready(_lv_disp_get_refr_timer(display));
        host_millis+=1;tick(host_millis);
        if(!i)first=image_hash(tft);else assert(image_hash(tft)!=first); // Shape changes with translation frozen.
    }
}
void test_weather(TFT_eSPI &tft) {
    using dashboard::WeatherScene;
    for(auto pair:{std::pair<int,WeatherScene>(0,WeatherScene::CLEAR),{1,WeatherScene::MAINLY_CLEAR},
                  {2,WeatherScene::PARTLY_CLOUDY},{3,WeatherScene::OVERCAST},{45,WeatherScene::FOG},
                  {48,WeatherScene::FOG},{51,WeatherScene::DRIZZLE},{57,WeatherScene::DRIZZLE},
                  {61,WeatherScene::RAIN},{67,WeatherScene::RAIN},{82,WeatherScene::RAIN},
                  {71,WeatherScene::SNOW},{77,WeatherScene::SNOW},{86,WeatherScene::SNOW},
                  {95,WeatherScene::THUNDER},{96,WeatherScene::THUNDER},{99,WeatherScene::THUNDER}})
        assert(dashboard::weather_scene(pair.first)==pair.second);
    for(int code:{-1,4,44,52,60,70,78,83,94,97,100})assert(dashboard::weather_scene(code)==WeatherScene::UNKNOWN);
    for(const char *text:{"Freezing drizzle","Freezing rain","Rain showers","Snow showers","Hail / thunder","Unknown WMO","Thunderstorm"})fits(text,164);
    static uint16_t previous[320*240];
    auto &weather=fixture.readings[live_data::WEATHER];
    for(int code:{0,1,2,3,45,51,61,71,95,99}) {
        weather.valid=true;weather.values[3]=code;++fixture.revision;
        const std::string variant="wmo-"+std::to_string(code);
        frame(tft,3,variant.c_str());
        memcpy(previous,tft.framebuffer,sizeof(previous));
        const uint32_t first=image_hash(tft);
        bool moved=false;
        for(int i=0;i<90;++i) {
            const int formats_before=formatted_values;
            tft.reset_capture();host_millis+=(i%3==0?34:33);dashboard::tick(host_millis);
            assert(formatted_values==formats_before); // Animation path must not reformat prices/weather text.
            // LVGL 8.3 public invalidation adds a 5px transform-safety margin on each side.
            if(tft.total_pixels<=0 || tft.total_pixels>117*94)
                fprintf(stderr,"Animation code=%d step=%d frame=%u pixels=%d bounds=%d,%d..%d,%d revision=%u\n",code,i,unsigned(dashboard::weather_elapsed_ms),tft.total_pixels,tft.min_x,tft.min_y,tft.max_x,tft.max_y,dashboard::data.revision);
            assert(tft.total_pixels>0 && tft.total_pixels<=117*94);
            assert(tft.min_x>=191 && tft.max_x<=307 && tft.min_y>=43 && tft.max_y<=136);
            moved=moved || image_hash(tft)!=first;
            for(int y=0;y<240;++y)for(int x=0;x<320;++x)
                if(x<196 || x>302 || y<48 || y>131)assert(tft.framebuffer[y*320+x]==previous[y*320+x]);
        }
        assert(moved); // Allow quantized cloud drift to pause briefly at each turning point.
    }
    // Steady frames do not leak LVGL pool allocations.
    lv_mem_monitor_t before{},after{};lv_mem_monitor(&before);
    for(int i=0;i<200;++i){host_millis+=40;dashboard::tick(host_millis);}
    lv_mem_monitor(&after);assert(after.used_cnt==before.used_cnt && after.free_size==before.free_size);
    weather.values[3]=97;frame(tft,3,"unknown-wmo");tft.reset_capture();
    const auto frozen=dashboard::weather_elapsed_ms;
    host_millis+=400;dashboard::tick(host_millis);assert(tft.total_pixels==0 && dashboard::weather_elapsed_ms==frozen);
    weather.valid=false;frame(tft,3,"no-weather");tft.reset_capture();
    host_millis+=400;dashboard::tick(host_millis);assert(tft.total_pixels==0 && dashboard::weather_elapsed_ms==frozen);
    weather.valid=true;weather.values[3]=61;
    frame(tft,0,"stocks-animation-paused");tft.reset_capture();
    host_millis+=400;dashboard::tick(host_millis);assert(tft.total_pixels==0 && dashboard::weather_elapsed_ms==frozen);
    frame(tft,3,"rain-visible");
    dashboard::hide();tft.reset_capture();host_millis+=1000;dashboard::tick(host_millis);
    assert(tft.total_pixels==0); // Setup owns TFT; animation must stop completely.
}
void tap(int x,int y) {
    dashboard::touch(true,x,y);host_millis+=100;dashboard::touch(false,x,y);
}
int main() {
    fixture.worker_ok=true;
    for(int i=0;i<live_data::COUNT;++i) {
        auto &r=fixture.readings[i];r.valid=true;r.source_epoch=time(nullptr);r.available=0xFFF;
        strcpy(r.source_time,"14/10/2569 15:46");
        for(int j=0;j<12;++j)r.values[j]=41.44f+j;
        for(int j=0;j<22;++j)r.history[j]={r.source_epoch-(21-j)*86400,100.0f+j+(j%3)*3};
        r.history_count=22;
    }
    auto &weather=fixture.readings[live_data::WEATHER];
    weather.values[0]=31.5;weather.values[1]=72;weather.values[2]=36.0;weather.values[3]=3;weather.values[4]=12.0;
    fixture.readings[live_data::SET].values[0]=1575.88;
    auto &gold=fixture.readings[live_data::THAI_GOLD];gold.values[0]=65950;gold.values[1]=65750;gold.source_epoch=0;
    for(int i=0;i<22;++i)gold.history[i]={time(nullptr)-(21-i)*900,65000.0f+i*50};
    fixture.readings[live_data::THAI_OIL].source_epoch=0;
    strcpy(fixture.readings[live_data::THAI_OIL].source_time,"As of 14/10/2026");
    TFT_eSPI tft;assert(dashboard::begin(tft));
    fits("THAI MARKET",296,true);fits("THAI GOLD 96.5%",296,true);
    fits("FUEL / THB PER L",296,true);fits("BANGKOK / UTC+7",296,true);
    for(auto text:{"SET","PTT","PTG","AOT","ADVANC","CPALL","PT / PTG","Bangchak"})fits(text,98);
    for(auto text:{"Gasohol 95","Gasohol 91","Diesel B7"})fits(text,109);
    for(auto text:{"CACHED 14/10/2569 15:46","CACHED As of 14/10/2026","UNAVAILABLE / error -24"})fits(text,296);
    fits("14/10 23:59",136);fits("99999",126,true);fits("1575.88",170,true);
    for(int i=0;i<5;++i)frame(tft,i);
    fits("/ baht wt.",99);fits("UPDATED",98);fits("15:46",96,true);fits("PT",46,true);
    gold.history_count=0;frame(tft,1,"no-gold-history");const auto no_history=image_hash(tft);
    gold.history_count=32;frame(tft,1,"unused-gold-history");assert(image_hash(tft)==no_history);
    for(int i=0;i<3;++i) {
        dashboard::station=i;const std::string name="logo-"+std::to_string(i);frame(tft,2,name.c_str());
        if(i==0)assert(logo_pixels(tft,0x00AFDE)>40 && logo_pixels(tft,0xED1C24)>40);
        else if(i==1)assert(logo_pixels(tft,0xE41B23)>60 && logo_hue_pixels(tft,false)>20); // Red italic PT, not white letters.
        else assert(logo_hue_pixels(tft,false)>80 && logo_hue_pixels(tft,true)>10);
        const auto &image=station_logos::images[i];
        assert(image.header.w==60 && image.header.h==56 && image.data_size==6720);
        // Asset occupies the exact intended pump rectangle: hose must not overwrite its lettering.
        for(int y=0;y<56;++y)for(int x=0;x<60;++x) {
            const int offset=(y*60+x)*2;
            const uint16_t expected=image.data[offset] | (uint16_t(image.data[offset+1])<<8);
            assert(tft.framebuffer[(y+91)*320+x+22]==expected);
        }
    }
    test_motion_time(tft);
    test_cadence_and_presented_fps(tft);
    test_cloud_morph(tft);
    test_weather(tft);
    dashboard::show(0);tap(252,83);assert(dashboard::stock==5 && dashboard::page==0);
    tap(109,55);tap(55,69);assert(dashboard::stock==5); // Tab gaps are not hits.
    dashboard::touch(true,108,55);host_millis+=100;dashboard::touch(true,113,55);
    dashboard::touch(false,113,55);assert(dashboard::stock==5); // Crossing a tab boundary isn't a selection.
    for(int i=0;i<6;++i)assert(dashboard::selected_tab(15+(i%3)*101,45+(i/3)*31,false)==i);
    tap(80,140);assert(dashboard::page==0); // Background never navigates.
    dashboard::touch(true,280,55);host_millis+=60;dashboard::touch(true,100,55);
    dashboard::touch(false,100,55);assert(dashboard::page==1 && dashboard::stock==5);
    dashboard::show(2);tap(252,55);assert(dashboard::station==2);
    tap(109,55);assert(dashboard::station==2);
    for(int i=0;i<3;++i)assert(dashboard::selected_tab(15+i*101,45,true)==i);
    frame(tft,2);
    // Gold graph is absent regardless of collector state; preserve cached/source timestamps.
    fixture.readings[live_data::THAI_GOLD].history_count=1;fixture.readings[live_data::THAI_GOLD].failed=true;
    frame(tft,1,"cached-single");
    fixture.readings[live_data::THAI_OIL].available=1<<8;frame(tft,2,"missing-products");
    for(auto &r:fixture.readings)r={};++fixture.revision;
    for(int i=0;i<5;++i)frame(tft,i,"unavailable");
    int before=tft.flushes;dashboard::hide();host_millis+=1000;dashboard::tick(host_millis);
    assert(tft.flushes==before); // Custom WiFi/calibration exclusively own TFT while hidden.
    assert(lv_mem_test()==LV_RES_OK);
    lv_mem_monitor_t monitor;lv_mem_monitor(&monitor);
    printf("PASS: actual LVGL 8.3 logos on pump, gold graph absent, WMO scenes, 30fps rational cadence + actual LVGL completion counters, cloud morph/clip/visibility/no-pool-leak, font widths/tab/swipe/setup ownership; LVGL pool max used %u bytes. Preview fixtures are not real prices.\n",unsigned(monitor.max_used));
}
'''
write_if_changed(OUT / "harness.cpp", harness)
includes = ["-I", str(OUT), "-I", str(ROOT / "src"), "-I", str(LVGL), "-DLV_CONF_INCLUDE_SIMPLE=1"]
config_time = (ROOT / "src/lv_conf.h").stat().st_mtime
sources = list((LVGL / "src").rglob("*.c"))


def compile_c(source):
    name = "_".join(source.relative_to(LVGL / "src").parts) + ".o"
    output = OUT / name
    if not output.exists() or output.stat().st_mtime < max(source.stat().st_mtime, config_time):
        subprocess.run(["gcc", "-std=c99", "-O1", *includes, "-c", str(source), "-o", str(output)], check=True, timeout=30)
    return output


with ThreadPoolExecutor(max_workers=8) as pool:
    objects = list(pool.map(compile_c, sources))
subprocess.run(["g++", "-std=c++11", "-O1", *includes, str(OUT / "harness.cpp"), str(ROOT / "src/station_logos.cpp"),
                *map(str, objects), "-o", str(OUT / "test.exe")], check=True, timeout=60)
subprocess.run([str(OUT / "test.exe")], cwd=OUT, check=True, timeout=15)
try:
    from PIL import Image
    for ppm in OUT.glob("fixture-*.ppm"):
        with Image.open(ppm) as image:
            image.save(ppm.with_suffix(".png"))
except ImportError:
    print("Pillow not installed: previews remain PPM (tests already passed).")
