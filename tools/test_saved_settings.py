"""Host regression for actual NVS codec and extracted firmware startup/save flows.
Preferences/WiFi mocks only: no real secrets, network or board. Requires Python/g++.
Power-loss tests simulate absent/truncated/corrupted blobs, not physical ESP32 flash.
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MAIN = (ROOT / "src/main.cpp").read_text(encoding="utf-8")


def function(name):
    match = re.search(r"^(?:bool|void|uint32_t) " + name + r"\([^\n]*\) \{", MAIN, re.M)
    assert match, name
    end = MAIN.index("{", match.start()) + 1
    depth = 1
    while depth:
        depth += (MAIN[end] == "{") - (MAIN[end] == "}")
        end += 1
    return MAIN[match.start():end]


preferences = r'''
#pragma once
#include <map>
#include <string>
#include <vector>
#include <cstring>
#include <cassert>
namespace mock_nvs {
std::map<std::string,std::vector<uint8_t>> records;
bool exists=false,fail_open=false,fail_write=false,fail_remove=false;
int writes=0,removes=0;
}
class Preferences {
    bool readonly=true;
public:
    bool begin(const char *name,bool read_only=false) {
        assert(!strcmp(name,"cyd_settings"));readonly=read_only;
        if(mock_nvs::fail_open || (readonly && !mock_nvs::exists))return false;
        if(!readonly)mock_nvs::exists=true;
        return true;
    }
    void end() {}
    size_t getBytesLength(const char *key) {
        auto found=mock_nvs::records.find(key);
        return found==mock_nvs::records.end()?0:found->second.size();
    }
    size_t getBytes(const char *key,void *out,size_t size) {
        auto found=mock_nvs::records.find(key);
        if(found==mock_nvs::records.end() || found->second.size()!=size)return 0;
        memcpy(out,found->second.data(),size);return size;
    }
    size_t putBytes(const char *key,const void *in,size_t size) {
        assert(!readonly);if(mock_nvs::fail_write)return 0;
        const auto *bytes=static_cast<const uint8_t *>(in);
        mock_nvs::records[key]=std::vector<uint8_t>(bytes,bytes+size);++mock_nvs::writes;return size;
    }
    bool isKey(const char *key) { return mock_nvs::records.count(key)!=0; }
    bool remove(const char *key) {
        assert(!readonly);if(mock_nvs::fail_remove)return false;
        ++mock_nvs::removes;return mock_nvs::records.erase(key)!=0;
    }
};
'''
harness = r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include "saved_settings.cpp"
#include "dashboard_gesture.h"
using std::min;
using std::max;
using std::isfinite;
struct String : std::string {
    using std::string::string;
    using std::string::operator=;
    bool isEmpty() const {return empty();}
};
uint32_t tick=0;
uint32_t millis() {return tick;}
struct SerialStub {
    std::string messages;
    void println(const char *text) {messages+=text;messages+='\n';}
    template<typename... A> void printf(const char *format,A... args) {
        char text[512];snprintf(text,sizeof(text),format,args...);messages+=text;
    }
} Serial;
constexpr int WL_CONNECTED=3,WL_DISCONNECTED=6;
using wl_status_t=int;
struct WiFiStub {
    int state=WL_DISCONNECTED,attempts=0;
    String current,pending;
    void disconnect(bool,bool) {state=WL_DISCONNECTED;current="";}
    void begin(const char *ssid,const char *) {pending=ssid;++attempts;}
    int status() const {return state;}
    String SSID() const {return current;}
    void confirm() {state=WL_CONNECTED;current=pending;}
} WiFi;
struct TFTStub {void fillRect(int,int,int,int,int){} void fillCircle(int,int,int,int){} } tft;
namespace dashboard {
int hides=0,taps=0,swipes=0;
DashboardGesture gesture;
void hide(){++hides;gesture={};}
void touch(bool pressed,int x,int y){
    const auto action=gesture.update(pressed,x,y,tick);
    if(action==DashboardGesture::TAP)++taps;
    if(action==DashboardGesture::LEFT || action==DashboardGesture::RIGHT)++swipes;
}
}
struct TS_Point {int x=2000,y=2000,z=0;};
struct TouchStub {TS_Point point;TS_Point getPoint(){return point;}} touch;
bool touch_diagnostics=false;
uint32_t last_touch=0,last_touch_diagnostic=0;
void poll_calibration(const TS_Point &,bool,uint32_t){}
void wifi_tap(int,int){}
template<typename T> T constrain(T value,T lo,T hi){return std::max(lo,std::min(value,hi));}
constexpr float PI=3.14159265f;
constexpr uint16_t WIFI_BG=0,WIFI_MUTED=1,WIFI_KEY=2,TFT_WHITE=3;
constexpr uint32_t CONNECT_TIMEOUT=20000;
constexpr int TOUCH_CS=33,TOUCH_RAW_MIN=200,TOUCH_RAW_MAX=3900;
constexpr int CORNER_X[]={25,294,294,25},CORNER_Y[]={25,25,214,214};
constexpr int TARGET_SPAN_X=269,TARGET_SPAN_Y=189;
enum class Screen {CALIBRATION,WIFI,DASHBOARD};
Screen screen=Screen::CALIBRATION;
struct Calibration {float ox,oy,ux,uy,vx,vy;bool valid;} calibration{};
bool calibration_complete=false,calibration_ignore_hold=false,pressed_before=false;
bool dashboard_touch_armed=false,connecting=false;
bool corner_done[4]{};
float corner_raw_x[4]{},corner_raw_y[4]{};
uint32_t calibration_completed_at=0,connect_started=0,last_animation=0;
int sample_corner=-1,sample_count=0,release_samples=0;
long sample_sum_x=0,sample_sum_y=0;
int sample_min_x=4095,sample_max_x=0,sample_min_y=4095,sample_max_y=0;
bool sample_rejected=false;
uint8_t page=0,spinner_phase=0;
String ssid,password;
const char *wifi_message="";
int calibration_draws=0,wifi_draws=0,ntp_calls=0;
void draw_calibration(){++calibration_draws;}
void draw_corner_target(int){}
void draw_wifi(){++wifi_draws;}
void draw_wifi_status(){}
void configTime(int,int,const char *,const char *) {++ntp_calls;}
void draw_dashboard() {assert(WiFi.status()==WL_CONNECTED);screen=Screen::DASHBOARD;}
'''
for name in ("calibration_profile", "reset_corner_sample", "start_calibration", "fit_calibration", "accept_corner",
             "show_gate", "begin_connect", "enter_wifi", "restore_startup", "force_recalibration", "forget_saved_wifi", "update_network", "map_touch", "poll_touch"):
    harness += "\n" + function(name) + "\n"
harness += r'''
void resign(std::vector<uint8_t> &bytes) {
    saved_settings::put_u32(bytes.data()+bytes.size()-4,saved_settings::checksum(bytes.data(),bytes.size()-4));
}
void corner(int index,int x,int y) {
    sample_corner=index;sample_count=4;sample_sum_x=x*4;sample_sum_y=y*4;sample_rejected=false;
    sample_min_x=sample_max_x=x;sample_min_y=sample_max_y=y;
    accept_corner(tick+=100);
}
void four_good_corners() {
    corner(0,500,500);corner(1,3200,500);corner(2,3200,3200);corner(3,500,3200);
    assert(calibration_complete && calibration.valid);
}
void test_codec() {
    using namespace saved_settings;
    using namespace mock_nvs;
    Affine affine{},out{};Credentials credentials;
    assert(!load_calibration(out,1) && !load_wifi(credentials) && writes==0);
    assert(valid_credentials("Test AP","fixture-pass"));
    assert(valid_credentials("Open AP",""));
    for(const char *bad:{"short","1234567","bad\npassword"})assert(!valid_credentials("Test AP",bad));
    assert(!valid_credentials("","fixture-pass") && !valid_credentials(nullptr,"fixture-pass"));
    assert(!valid_credentials("AP",nullptr) && !valid_credentials("AP\n", "fixture-pass"));
    assert(!save_calibration(affine,1) && !save_wifi("","fixture-pass") && writes==0);
    affine={500,500,2700,0,0,2700};
    assert(save_calibration(affine,123) && writes==1);
    const auto good_cal=records["cal"];
    assert(good_cal.size()==40 && get_u32(good_cal.data()+12)==0x43FA0000); // 500 binary32 LE.
    assert(load_calibration(out,123) && out.ux==2700 && out.oy==500);
    assert(save_calibration(affine,123) && writes==1);
    assert(!load_calibration(out,124));
    for(size_t i=0;i<good_cal.size();++i) {
        records["cal"]=good_cal;records["cal"][i]^=1;assert(!load_calibration(out,123));
    }
    for(int field:{4,5,6,7}) {
        records["cal"]=good_cal;records["cal"][field]=99;resign(records["cal"]);
        assert(!load_calibration(out,123));
    }
    for(uint32_t bits:{0x7FC00000U,0x7F800000U,0x00000000U,0xC3FA0000U}) {
        records["cal"]=good_cal;put_u32(records["cal"].data()+20,bits);resign(records["cal"]);
        assert(!load_calibration(out,123));
    }
    records["cal"]=good_cal;records["cal"].resize(20);assert(!load_calibration(out,123));
    records["cal"]=good_cal;
    assert(save_wifi("Test AP","fixture-pass") && writes==2);
    auto good_wifi=records["wifi"];
    assert(load_wifi(credentials) && !strcmp(credentials.ssid,"Test AP") && !strcmp(credentials.password,"fixture-pass"));
    assert(save_wifi("Test AP","fixture-pass") && writes==2);
    for(size_t i=0;i<good_wifi.size();++i) {
        records["wifi"]=good_wifi;records["wifi"][i]^=1;assert(!load_wifi(credentials));
    }
    for(int field:{4,5,6,7,8}) {
        records["wifi"]=good_wifi;records["wifi"][field]=field==7?1:0;resign(records["wifi"]);
        assert(!load_wifi(credentials));
    }
    records["wifi"]=good_wifi;records["wifi"].resize(10);assert(!load_wifi(credentials));
    records["wifi"]=good_wifi;
    fail_write=true;assert(!save_wifi("Different AP","different-fixture"));assert(records["wifi"]==good_wifi);
    fail_write=false;fail_open=true;assert(!load_wifi(credentials) && !save_calibration(affine,1));fail_open=false;
    std::string max_ssid(32,'s'),max_pass(63,'p');
    assert(save_wifi(max_ssid.c_str(),max_pass.c_str()) && records["wifi"].size()==107);
    assert(load_wifi(credentials) && strlen(credentials.ssid)==32 && strlen(credentials.password)==63);
    max_ssid+='s';assert(!save_wifi(max_ssid.c_str(),max_pass.c_str()));
    max_pass+='p';assert(!save_wifi("AP",max_pass.c_str()));
    assert(forget_wifi() && !load_wifi(credentials) && load_calibration(out,123));
    assert(forget_wifi() && forget_calibration() && !load_calibration(out,123));
    records.clear();exists=false;writes=removes=0;
}
void test_startup() {
    using namespace mock_nvs;
    saved_settings::Credentials stored;
    restore_startup();assert(screen==Screen::CALIBRATION && calibration_draws && writes==0);
    corner(0,500,500);corner(1,3200,500);corner(2,3200,3200);assert(writes==0);
    corner(3,500,3500);assert(!calibration.valid && !calibration_complete && writes==0); // Failed fit never saved.
    four_good_corners();assert(writes==1);
    auto good_cal=records["cal"];
    update_network(tick+599);assert(screen==Screen::CALIBRATION);
    tick+=600;update_network(tick);assert(screen==Screen::WIFI && !connecting && writes==1);
    ssid="Test AP";password="fixture-pass";begin_connect();assert(connecting && writes==1);
    WiFi.state=WL_CONNECTED;WiFi.current="Wrong AP";update_network(tick+1);
    assert(connecting && writes==1); // Connected status must belong to attempted SSID.
    WiFi.confirm();dashboard_touch_armed=true;update_network(tick+2);
    assert(screen==Screen::DASHBOARD && !connecting && !dashboard_touch_armed && writes==2 && ntp_calls==1);
    auto good_wifi=records["wifi"];
    ssid="Edited AP";password="edited-fixture";calibration.valid=false;restore_startup();
    assert(calibration.valid && screen==Screen::WIFI && connecting && ssid=="Test AP" && password=="fixture-pass");
    assert(writes==2 && records["cal"]==good_cal && records["wifi"]==good_wifi);
    WiFi.confirm();update_network(tick+3);assert(screen==Screen::DASHBOARD && writes==2); // Boot success: unchanged, no write.
    WiFi.disconnect(false,false);update_network(tick+4);
    assert(screen==Screen::WIFI && !connecting && records["wifi"]==good_wifi);
    password="failed-fixture";begin_connect();tick+=CONNECT_TIMEOUT;update_network(tick);
    assert(screen==Screen::WIFI && !connecting && password=="failed-fixture" && records["wifi"]==good_wifi && writes==2);
    restore_startup();assert(connecting && password=="fixture-pass");
    forget_saved_wifi();assert(screen==Screen::WIFI && !connecting && ssid.empty() && password.empty());
    assert(!saved_settings::load_wifi(stored) && records["cal"]==good_cal);
    restore_startup();assert(calibration.valid && screen==Screen::WIFI && !connecting);
    ssid="Open AP";password="";begin_connect();WiFi.confirm();update_network(tick+5);
    assert(saved_settings::load_wifi(stored) && !strcmp(stored.ssid,"Open AP") && !stored.password[0]);
    auto open_wifi=records["wifi"];
    force_recalibration();assert(screen==Screen::CALIBRATION && !records.count("cal") && records["wifi"]==open_wifi);
    four_good_corners();tick+=600;update_network(tick);
    assert(screen==Screen::WIFI && connecting && ssid=="Open AP" && password.empty());
    auto new_cal=records["cal"];
    records["cal"].resize(18);restore_startup();assert(screen==Screen::CALIBRATION && !calibration.valid);
    assert(records["wifi"]==open_wifi); // Interrupted/corrupt calibration doesn't erase credentials.
    records["cal"]=new_cal;records["wifi"].resize(10);restore_startup();
    assert(calibration.valid && screen==Screen::WIFI && !connecting); // No partial credentials auto attempt.
    records["wifi"]=open_wifi;
    restore_startup();WiFi.confirm();update_network(tick+6);
    show_gate("");ssid="New AP";password="new-fixture";begin_connect();
    fail_write=true;WiFi.confirm();update_network(tick+7);fail_write=false;
    assert(screen==Screen::DASHBOARD && records["wifi"]==open_wifi); // Save failure doesn't break usable connection.
    fail_remove=true;forget_saved_wifi();assert(records["wifi"]==open_wifi && !strcmp(wifi_message,"NVS erase failed"));
    force_recalibration();assert(screen==Screen::CALIBRATION && records.count("cal"));fail_remove=false;
    forget_saved_wifi();assert(screen==Screen::CALIBRATION && !records.count("wifi")); // Can't use form before valid touch map.
    assert(Serial.messages.find("fixture-pass")==std::string::npos && Serial.messages.find("Open AP")==std::string::npos);
}
void poll(bool down,int count=1) {
    touch.point.z=down?400:0;
    for(int i=0;i<count;++i)poll_touch(tick+=20);
}
void test_restored_touch_gate() {
    const saved_settings::Affine good={500,500,2700,0,0,2700};
    assert(saved_settings::save_calibration(good,calibration_profile()));
    assert(saved_settings::save_wifi("Test AP","fixture-pass"));
    calibration.valid=false;pressed_before=false;dashboard_touch_armed=false;
    restore_startup();assert(calibration.valid && connecting && screen==Screen::WIFI);
    int x=0,y=0;assert(map_touch(2000,2000,x,y) && x>100 && y>100);
    poll(true,2);WiFi.confirm();update_network(tick);
    assert(screen==Screen::DASHBOARD && !dashboard_touch_armed);
    poll(true,10);assert(dashboard::taps==0 && dashboard::swipes==0);
    poll(false,1);poll(true,3);assert(!dashboard_touch_armed && dashboard::taps==0);
    poll(false,3);assert(dashboard_touch_armed && dashboard::taps==0);
    poll(true,2);poll(false,3);assert(dashboard::taps==1 && dashboard::swipes==0);
}
int main() {
    test_codec();test_startup();test_restored_touch_gate();
    puts("PASS: actual Preferences codec + firmware fit/save/startup/auto-connect/timeout/loss/forget flows, versions/CRC/geometry/lengths/truncation, no repeated/failed-attempt writes, open/max-length credentials, save/erase failures, no secret logs.");
}
'''
assert 'WiFi.persistent(false)' in MAIN
assert "command == 'c') force_recalibration()" in MAIN
assert "command == 'w') forget_saved_wifi()" in MAIN
assert "saved_settings::save_wifi" not in function("begin_connect")
assert "saved_settings::save_calibration" not in function("fit_calibration")
with tempfile.TemporaryDirectory(prefix="cyd-nvs-test-") as folder:
    folder = Path(folder)
    (folder / "Preferences.h").write_text(preferences, encoding="utf-8")
    (folder / "test.cpp").write_text(harness, encoding="utf-8")
    executable = folder / "test.exe"
    subprocess.run(["g++", "-std=c++11", "-Wall", "-Wextra", "-I", str(folder), "-I", str(ROOT / "src"),
                    str(folder / "test.cpp"), "-o", str(executable)], check=True, timeout=45)
    subprocess.run([str(executable)], check=True, timeout=10)
