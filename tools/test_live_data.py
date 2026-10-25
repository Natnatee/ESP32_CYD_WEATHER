"""Host tests of actual firmware parsers, observed history, gestures and touch gate.
Synthetic fixtures only, never firmware prices. Requires g++ and a PlatformIO build.
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
DATA = (ROOT / "src/live_data.cpp").read_text(encoding="utf-8")
MAIN = (ROOT / "src/main.cpp").read_text(encoding="utf-8")


def function(source, name):
    match = re.search(r"^(?:bool|void|Diagnostics) " + name + r"\([^\n]*\) \{", source, re.M)
    assert match, name
    start = source.index("{", match.start())
    end, depth = start + 1, 1
    while depth:
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
        end += 1
    return source[match.start():end]


header = re.sub(r"^#.*$", "", (ROOT / "src/live_data.h").read_text(encoding="utf-8"), flags=re.M)
harness = r'''
#include <ArduinoJson.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <string>
#include <ctime>
#include <mutex>
#include <thread>
#include <type_traits>
#include "dashboard_gesture.h"
using String = std::string;
using std::isfinite;
using std::min;
size_t strlcpy(char *out, const char *in, size_t size) {
    snprintf(out,size,"%s",in); return strlen(in);
}
'''
harness += header + "\nnamespace live_data {\n"
harness += r'''
Snapshot shared;
std::mutex snapshot_mux;
#define portENTER_CRITICAL(mux) (mux)->lock()
#define portEXIT_CRITICAL(mux) (mux)->unlock()
'''
harness += "\n".join(function(DATA, name) for name in ("parse_price", "number", "price", "decode", "stale", "append_observation", "copy_snapshot", "diagnostics", "mark_failure"))
harness += r'''
void check_decode() {
    float value=0;
    for (const char *p : {"65,950.00", "42.94", "100", "1,234,567.89"}) assert(parse_price(p,value));
    for (const char *p : {"", "--", "null", "nan", "inf", "-1", "0", "1x", ".2", "1.", "12,34", "1234,567", "1,,000", "1,000.2,3"}) assert(!parse_price(p,value));
    assert(!parse_price(nullptr,value));
    Reading r;
    assert(decode(WEATHER,R"({"current":{"time":"2026-10-14T22:45","temperature_2m":26.3,"relative_humidity_2m":91,"apparent_temperature":32.5,"weather_code":3,"wind_speed_10m":3.1}})",r));
    assert(r.values[0]>26 && !strcmp(r.source_time,"10-14 22:45"));
    assert(!decode(WEATHER,R"({"current":{"temperature_2m":null}})",r));
    assert(!decode(WEATHER,"not json",r) && r.error==-20);
    assert(decode(THAI_GOLD,R"({"status":"success","response":{"update_date":"14/10/2569","update_time":"เวลา 15:46 น.","price":{"gold_bar":{"buy":"65,750.00","sell":"65,950.00"}}}})",r));
    assert(r.values[0]==65950 && r.values[1]==65750 && !strcmp(r.source_time,"14/10/2569 15:46"));
    assert(!decode(THAI_GOLD,R"({"status":"success","response":{"price":{"gold_bar":{"sell":"--","buy":"100"}}}})",r));
    assert(decode(THAI_OIL,R"({"status":"success","response":{"date":"14 ตุลาคม 2569","stations":{"ptt":{"diesel":{"name":"ดีเซล B7","price":"42.94"}},"pt":{"gasohol_95":{"name":"แก๊สโซฮอล์ 95","price":"41.44"}},"bcp":{"disel":{"name":"ดีเซล B7","price":"43.00"},"diesel":{"name":"ดีเซล B7","price":"99.00"},"gasohol_91":{"name":"แก๊สโซฮอล์ 91","price":null},"gasohol_e20":{"name":"เบนซิน 95","price":"12"}}}}})",r));
    assert(!strcmp(r.source_time,"As of 14/10/2026"));
    assert(r.available==((1<<3)|(1<<4)|(1<<11)));
    assert(r.values[3]==42.94f && r.values[4]==41.44f && r.values[11]==43.0f);
    assert(r.values[0]==0 && r.values[8]==0 && r.values[9]==0 && r.values[10]==0);
    assert(!decode(THAI_OIL,R"({"status":"success","response":{"stations":{"ptt":{"diesel":{"name":"ดีเซล B7","price":null}}}}})",r));
    assert(!decode(THAI_OIL,R"({"status":"success","response":{"stations":{"bcp":{"diesel":{"name":"ดีเซล B7","price":"40"}}}}})",r)); // No guessed alternate keys.
    for(int i=SET;i<COUNT;++i) {
        std::string json="{\"chart\":{\"error\":null,\"result\":[{\"meta\":{\"regularMarketPrice\":123.45,\"regularMarketTime\":1791992891,\"symbol\":\"";
        json+=STOCK_SYMBOLS[i-SET]; json+="\",\"currency\":\"THB\",\"instrumentType\":\"";
        json+=i==SET?"INDEX":"EQUITY";
        json+="\"},\"timestamp\":[1791700000,1791800000,1791900000,1792000000],\"indicators\":{\"quote\":[{\"close\":[120.5,null,123.0,124.0]}]}}]}}";
        r={}; assert(decode(static_cast<Source>(i),json,r));
        assert(r.source_epoch==1791992891 && std::abs(r.values[0]-123.45)<0.01);
        assert(r.history_count==2 && r.history[0].epoch==1791700000 && r.history[1].price==123.0f);
        assert(!decode(static_cast<Source>(i==SET?PTT:SET),json,r));
        auto malformed=json; malformed.replace(malformed.find("123.45"),6,"null");
        assert(!decode(static_cast<Source>(i),malformed,r));
        auto currency=json; currency.replace(currency.find("THB"),3,"USD");
        assert(!decode(static_cast<Source>(i),currency,r));
    }
    // Maximum filtered history size fits the firmware JSON allocation.
    std::string full=R"({"chart":{"error":null,"result":[{"meta":{"symbol":"PTT.BK","currency":"THB","instrumentType":"EQUITY","regularMarketPrice":120,"regularMarketTime":1791992891},"timestamp":[)";
    for(int i=0;i<40;++i) { if(i)full+=","; full+=std::to_string(1790000000+i*1000); }
    full+=R"(],"indicators":{"quote":[{"close":[)";
    for(int i=0;i<40;++i) { if(i)full+=","; full+="123.5"; }
    full+="]}]}}]}}";
    r={}; assert(decode(PTT,full,r) && r.history_count==32);
    assert(r.history[0].epoch==1790008000 && r.history[31].epoch==1790039000);
    assert(!decode(SET,R"({"chart":{"error":{"code":"Not Found"},"result":null}})",r));
    r={}; r.valid=true; r.received_ms=100;
    assert(!stale(r,WEATHER,WEATHER_INTERVAL+100));
    assert(stale(r,WEATHER,2*WEATHER_INTERVAL+100));
    r.failed=true; assert(stale(r,WEATHER,101));
    r.failed=false; r.received_ms=UINT32_MAX-99;
    assert(!stale(r,WEATHER,100));
    r={};
    for(int i=0;i<40;++i) append_observation(r,1790000000+i*900,65000+i);
    assert(r.history_count==32 && r.history[0].price==65008 && r.history[31].price==65039);
    append_observation(r,1790000000,999); append_observation(r,1790035100,999);
    assert(r.history_count==32 && r.history[31].price==65039); // Duplicate/reversed timestamp rejected.
    append_observation(r,1790040000,-1); assert(r.history_count==32);
}
void check_fallback() {
    shared={};
    auto &weather=shared.readings[WEATHER];
    weather.valid=true;weather.values[0]=29.5f;weather.received_ms=123;
    strcpy(weather.source_time,"original stamp");
    weather.history_count=1;weather.history[0]={1704067200,29.5f};
    mark_failure(WEATHER,-25);
    assert(weather.valid && weather.failed && weather.values[0]==29.5f && weather.received_ms==123);
    assert(!strcmp(weather.source_time,"original stamp") && weather.history_count==1);
    assert(!shared.readings[THAI_OIL].failed); // One failed endpoint cannot poison another.
    mark_failure(THAI_GOLD,-1);
    assert(!shared.readings[THAI_GOLD].valid && shared.readings[THAI_GOLD].failed);
    mark_failure(PTT,429);
    for(int i=SET;i<COUNT;++i) assert(shared.readings[i].failed && shared.readings[i].error==429);
    shared={};
}
void check_snapshot() {
    static_assert(std::is_same<decltype(&copy_snapshot),void (*)(Snapshot &)>::value,
                  "Snapshot API must copy into caller storage, never return a large value");
    static_assert(sizeof(Diagnostics)<=16,"Diagnostics must not contain history arrays");
    static Snapshot destination;
    std::thread publisher([] {
        for(uint32_t revision=1;revision<=2000;++revision) {
            portENTER_CRITICAL(&snapshot_mux);
            shared.revision=revision;shared.worker_ok=true;shared.worker_stack_free_bytes=1000+revision;
            for(auto &r:shared.readings) {
                r.valid=true;r.error=revision;r.history_count=HISTORY_CAPACITY;
                for(float &value:r.values)value=revision;
                for(int j=0;j<HISTORY_CAPACITY;++j)r.history[j]={time_t(1704067200+revision+j),float(revision+j)};
            }
            portEXIT_CRITICAL(&snapshot_mux);
            std::this_thread::yield();
        }
    });
    for(int i=0;i<3000;++i) {
        copy_snapshot(destination);
        const auto revision=destination.revision;
        if(!revision)continue;
        assert(destination.worker_ok && destination.worker_stack_free_bytes==1000+revision);
        for(const auto &r:destination.readings) {
            assert(r.valid && r.error==int(revision) && r.history_count==HISTORY_CAPACITY);
            for(float value:r.values)assert(value==revision);
            for(int j=0;j<HISTORY_CAPACITY;++j)assert(r.history[j].price==revision+j && r.history[j].epoch==1704067200+revision+j);
        }
        const auto small=diagnostics();
        assert(small.worker_ok && small.worker_stack_free_bytes==1000+small.revision);
    }
    publisher.join();copy_snapshot(destination);assert(destination.revision==2000);
    destination.readings[0].values[0]=-1;
    copy_snapshot(destination);assert(destination.readings[0].values[0]==2000); // UI owns its independent copy.
    puts("PASS: actual copy_snapshot/diagnostics, coherent concurrent history+metadata under mock portMUX, no by-value Snapshot API.");
}
} // namespace live_data

struct TS_Point {int x=160,y=120,z=0;};
struct Touch { TS_Point point; TS_Point getPoint(){return point;} } touch;
struct SerialStub {template<typename... A> void printf(const char *,A...){} } Serial;
enum class Screen {CALIBRATION,WIFI,DASHBOARD};
Screen screen=Screen::WIFI;
uint32_t last_touch=0,last_touch_diagnostic=0,tick=0;
bool touch_diagnostics=false,pressed_before=false,dashboard_touch_armed=false;
int turns=0,keys=0,taps=0;
bool map_touch(int x,int y,int &outx,int &outy){outx=x-1;outy=y-1;return true;}
void poll_calibration(TS_Point,bool,uint32_t){}
void wifi_tap(int,int){++keys;}
namespace dashboard {
DashboardGesture gesture;
void touch(bool pressed,int x,int y) {
    auto action=gesture.update(pressed,x,y,tick);
    if(action==DashboardGesture::LEFT)++turns;
    else if(action==DashboardGesture::RIGHT)--turns;
    else if(action==DashboardGesture::TAP)++taps;
}
}
'''
harness += function(MAIN, "poll_touch")
harness += r'''
void poll(bool down,int count=1){touch.point.z=down?400:0;for(int i=0;i<count;++i)poll_touch(tick+=20);}
int main(){
    live_data::check_decode();
    live_data::check_fallback();
    live_data::check_snapshot();
    poll(true,2); assert(keys==1);
    screen=Screen::DASHBOARD;dashboard_touch_armed=false;
    poll(true,10);assert(turns==0 && taps==0);
    poll(false,1);poll(true,3);assert(turns==0 && taps==0);
    poll(false,3);assert(dashboard_touch_armed && taps==0);
    touch.point.x=300;touch.point.y=60;poll(true,2);
    touch.point.x=220;poll(true);touch.point.x=150;poll(true);
    poll(false,2);assert(turns==0);poll(true);poll(false,3);assert(turns==1 && taps==0);
    touch.point.x=25;touch.point.y=100;poll(true,2);
    touch.point.x=110;poll(true);touch.point.x=200;poll(true);poll(false,3);assert(turns==0);
    touch.point.x=320;touch.point.y=1;poll(true,2);poll(false,3);assert(taps==1 && turns==0);
    poll(true,100);poll(false,3);assert(taps==1); // Long hold is not a selection.
    touch.point.x=100;touch.point.y=50;poll(true,2);touch.point.y=160;poll(true);poll(false,3);assert(taps==1);
    screen=Screen::WIFI;dashboard_touch_armed=false;poll(true,2);assert(keys==2);
    puts("PASS: actual parsers, missing station/product, exact BCP disel, dated history/null/bounds, observed ring, stale/rollover, calibrated gesture gate/hold/noise/swipes/taps.");
}
'''
assert "setInsecure" not in DATA
assert "setCACert(API_ROOTS)" in DATA
assert "millis() - started < BODY_TIMEOUT" in DATA
assert "body.length() + count > MAX_BODY" in DATA
assert "xTaskCreatePinnedToCore" in DATA
assert "portENTER_CRITICAL(&snapshot_mux)" in DATA
assert "Snapshot snapshot()" not in DATA
assert "live_data::snapshot()" not in MAIN
assert "TFT_eSprite" not in MAIN
assert "dashboard::hide();" in MAIN
with tempfile.TemporaryDirectory(prefix="cyd-data-test-") as folder:
    source = Path(folder) / "test.cpp"
    executable = Path(folder) / "test.exe"
    source.write_text(harness, encoding="utf-8")
    include = ROOT / ".pio/libdeps/cyd_ili9341/ArduinoJson/src"
    assert (include / "ArduinoJson.h").exists(), "Run pio run -e cyd_ili9341 first"
    subprocess.run(["g++", "-std=c++11", "-Wall", "-Wextra", "-pthread", "-I", str(include), "-I", str(ROOT / "src"), str(source), "-o", str(executable)], check=True, timeout=45)
    subprocess.run([str(executable)], check=True, timeout=10)
