"""Compile actual transport against deterministic mocks; no network/hardware proof."""
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
mock = r'''
#pragma once
#include <cstdint>
#include <cstddef>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <string>
inline uint32_t clock_ms=0, sleeps=0, stops=0;
inline uint32_t millis() { return clock_ms++; }
inline void vTaskDelay(unsigned ticks) { assert(ticks>=1); ++sleeps; clock_ms+=ticks; }
struct IPAddress {};
inline unsigned dns_calls=0, tls_calls=0;
inline bool dns_ok=true, tls_ok=true;
inline int tls_error=-9984;
struct WiFiMock { int hostByName(const char*, IPAddress&) { ++dns_calls; return dns_ok; } };
inline WiFiMock WiFi;
inline std::string logs;
struct SerialMock { template<class... T> void printf(const char* fmt,T... args) {
 char text[512]; std::snprintf(text,sizeof(text),fmt,args...); logs+=text;
} };
inline SerialMock Serial;
class WiFiClientSecure {
protected:
 int _timeout=0;
 const char *_CA_cert="exact embedded CA", *_cert=nullptr, *_private_key=nullptr;
 unsigned handshake_s=0;
public:
 virtual int connect(const char*,uint16_t){return 0;}
 virtual int connect(const char*,uint16_t,int32_t){return 0;}
 int connect(IPAddress,uint16_t,const char* host,const char* ca,const char*,const char*) {
   assert(host && std::strcmp(host,"test.invalid")==0); // Exact DNS name, never resolved IP as SNI.
   assert(ca && std::strcmp(ca,"exact embedded CA")==0);
   assert(handshake_s==20 || handshake_s==5);
   assert(_timeout>0 && _timeout<=4000); ++tls_calls; return tls_ok;
 }
 int connect(IPAddress,uint16_t,const char*,const char*,const char*)=delete; // IP-only loses SNI.
 void setHandshakeTimeout(unsigned seconds) {handshake_s=seconds;}
 unsigned handshakeTimeout()const{return handshake_s;}
 int lastError(char* out,size_t size){std::snprintf(out,size,"mock verification failure");return tls_error;}
 virtual ~WiFiClientSecure()=default;
 virtual int available(){return 0;}
 virtual int read(){return -1;}
 virtual int read(uint8_t*,size_t){return -1;}
 virtual int peek(){return -1;}
 virtual uint8_t connected(){return 1;}
 virtual size_t write(const uint8_t*,size_t n){return n;}
 virtual size_t write(uint8_t){return 1;}
 void stop(){++stops;}
};
'''
main = r'''
#include "cooperative_tls.h"
#include "api_retry.h"
int main(){
 // Exercise the EXACT virtual connect overload HTTPClient calls, including DNS failure.
 { CooperativeTLS client(0); WiFiClientSecure &http_client=client;
   assert(http_client.connect("test.invalid",443,4000)==1 && dns_calls==1 && tls_calls==1);
   dns_ok=false; assert(!http_client.connect("test.invalid",443,4000) && tls_calls==1);
   dns_ok=true; tls_ok=false; assert(!http_client.connect("test.invalid",443,4000));
   assert(logs.find("TLS error=-9984 hex=0x2700 text=mock verification failure")!=std::string::npos);
   assert(client.handshakeTimeout()==20);
   tls_error=-1; assert(!http_client.connect("test.invalid",443,4000));
   assert(logs.find("TLS error=-1 hex=0x0001")!=std::string::npos); // Preserve generic code, do not call it bad CA.
 }
 clock_ms=0;
 ApiRetry weather, oil;
 assert(weather.ready(0) && oil.ready(0));
 for(int i=1;i<=12;++i) {
   const uint32_t now=weather.attempted+weather.wait;
   weather.finish(now,false,600000);
   assert(!weather.ready(now) && !weather.ready(now+weather.wait-1));
   assert(weather.ready(now+weather.wait) && oil.ready(now));
   assert(weather.failures<=6 && weather.wait<=900000);
   if(i>=3) assert(weather.wait>=300000);
 }
 weather.finish(UINT32_MAX-10,true,600000);
 assert(weather.failures==0 && !weather.ready(20));
 assert(weather.ready(uint32_t(UINT32_MAX-10+600000U)));
 CooperativeTLS transport(3); // Working Yahoo keeps its old 5s/20s limits.
 assert(transport.handshakeTimeout()==5);
 WiFiClientSecure &stream=transport;
 // Model legacy timedRead busy retries, reached through a base-class pointer.
 for(int i=0;i<500;++i) { assert(stream.read()==-1); stream.available(); stream.peek(); }
 assert(sleeps>100 && !transport.expired());
 clock_ms=20001;
 assert(!stream.connected() && transport.expired() && stops==1);
 uint8_t byte=0;
 assert(stream.read(&byte,1)==-1 && stream.write(&byte,1)==0 && stream.available()==0);
 assert(stops==1);
 // Deadline arithmetic remains valid across millis wrap.
 clock_ms=UINT32_MAX-100;
 CooperativeTLS wrapped(3);
 clock_ms=100; assert(wrapped.connected());
 clock_ms=21000; assert(!wrapped.connected() && wrapped.expired());
 clock_ms=0; CooperativeTLS slow(0);
 clock_ms=20001; assert(slow.connected() && !slow.expired());
 clock_ms=35001; assert(!slow.connected() && slow.expired());
}
'''
with tempfile.TemporaryDirectory(prefix="cyd-tls-") as folder:
    path=Path(folder)
    (path/"WiFiClientSecure.h").write_text(mock,encoding="utf-8")
    (path/"esp_heap_caps.h").write_text('#pragma once\n#define MALLOC_CAP_8BIT 1\ninline unsigned heap_caps_get_free_size(int){return 100000;}\ninline unsigned heap_caps_get_largest_free_block(int){return 50000;}\n',encoding="utf-8")
    (path/"test.cpp").write_text(main,encoding="utf-8")
    subprocess.run(["g++","-std=c++17","-Wall","-Wextra","-I",str(path),"-I",str(ROOT/"src"),str(path/"test.cpp"),"-o",str(path/"test.exe")],check=True,timeout=30)
    subprocess.run([str(path/"test.exe")],check=True,timeout=10)
source=(ROOT/"src/live_data.cpp").read_text(encoding="utf-8")
assert "stream->readBytes(" not in source
assert "client.Stream::setTimeout(100)" in source
assert 'http.collectHeaders(headers, 1)' in source
assert "vTaskDelay(1)" in source
assert 'nullptr, tskIDLE_PRIORITY, nullptr, 0)' in source
assert 'configUSE_TIME_SLICING == 1' in source
assert 'retries[i].ready(now)' in source
assert 'retries[i].finish(millis(), ok, interval)' in source
print("PASS: actual virtual connect preserves exact hostname/SNI+CA, TLS error logging, source-specific 20s/35s vs 5s/20s limits, cooperative sleeps/deadline/wrap, source retry/fallback guards.")
