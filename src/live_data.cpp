#include "live_data.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <math.h>
#include "api_roots.h"
#include "cooperative_tls.h"
#include "api_retry.h"

namespace live_data {
namespace {
portMUX_TYPE snapshot_mux = portMUX_INITIALIZER_UNLOCKED;
Snapshot shared;
constexpr size_t MAX_BODY = 24576;
constexpr uint32_t BODY_TIMEOUT = 8000;
const char *const URLS[COUNT] = {
    "https://api.open-meteo.com/v1/forecast?latitude=13.7563&longitude=100.5018&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m&timezone=Asia%2FBangkok",
    "https://api.chnwt.dev/thai-gold-api/latest",
    "https://api.chnwt.dev/thai-oil-api/latest",
    "https://query1.finance.yahoo.com/v8/finance/chart/%5ESET.BK?interval=1d&range=1mo",
    "https://query1.finance.yahoo.com/v8/finance/chart/PTT.BK?interval=1d&range=1mo",
    "https://query1.finance.yahoo.com/v8/finance/chart/PTG.BK?interval=1d&range=1mo",
    "https://query1.finance.yahoo.com/v8/finance/chart/AOT.BK?interval=1d&range=1mo",
    "https://query1.finance.yahoo.com/v8/finance/chart/ADVANC.BK?interval=1d&range=1mo",
    "https://query1.finance.yahoo.com/v8/finance/chart/CPALL.BK?interval=1d&range=1mo"
};

bool number(JsonVariantConst value, float &out) {
    if (!value.is<float>() && !value.is<long>()) return false;
    out = value.as<float>();
    return isfinite(out);
}
bool price(JsonVariantConst value, float &out) {
    return value.is<const char *>() && parse_price(value.as<const char *>(), out);
}

bool decode(Source source, const String &body, Reading &out) {
    // Bounded daily observations; never retain the full upstream quote/station tree.
    StaticJsonDocument<1536> filter;
    if (source == WEATHER) filter["current"] = true;
    else if (source == THAI_GOLD) {
        filter["status"] = true;
        filter["response"]["price"]["gold_bar"] = true;
        filter["response"]["update_date"] = true;
        filter["response"]["update_time"] = true;
    } else if (source == THAI_OIL) {
        filter["status"] = true;
        filter["response"]["date"] = true;
        for (int station = 0; station < 3; ++station)
            for (int product = 0; product < 4; ++product)
                filter["response"]["stations"][OIL_STATIONS[station]][
                    station == 2 && product == 3 ? "disel" : OIL_PRODUCTS[product]] = true;
    } else {
        auto meta = filter["chart"]["result"][0]["meta"];
        meta["regularMarketPrice"] = true;
        meta["regularMarketTime"] = true;
        meta["symbol"] = true;
        meta["currency"] = true;
        meta["instrumentType"] = true;
        filter["chart"]["error"] = true;
        filter["chart"]["result"][0]["timestamp"] = true;
        filter["chart"]["result"][0]["indicators"]["quote"][0]["close"] = true;
    }
    DynamicJsonDocument doc(6144);
    if (deserializeJson(doc, body, DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(16))) {
        out.error = -20;
        return false;
    }
    out.error = -21;
    if (source == WEATHER) {
        const auto current = doc["current"];
        const char *keys[] = {"temperature_2m", "relative_humidity_2m", "apparent_temperature", "weather_code", "wind_speed_10m"};
        for (int i = 0; i < 5; ++i) if (!number(current[keys[i]], out.values[i])) return false;
        if (out.values[1] < 0 || out.values[1] > 100 || out.values[4] < 0) return false;
        const char *stamp = current["time"] | "";
        if (strlen(stamp) < 16) return false;
        snprintf(out.source_time, sizeof(out.source_time), "%.5s %.5s", stamp + 5, stamp + 11);
    } else if (source == THAI_GOLD) {
        if (strcmp(doc["status"] | "", "success")) return false;
        if (!price(doc["response"]["price"]["gold_bar"]["sell"], out.values[0]) ||
            !price(doc["response"]["price"]["gold_bar"]["buy"], out.values[1])) return false;
        // Buddhist-era date retained exactly; extract only ASCII HH:MM from Thai prose.
        const char *date = doc["response"]["update_date"] | "";
        const char *stamp = doc["response"]["update_time"] | "";
        char clock[6] = "";
        for (size_t i = 0; i + 4 < strlen(stamp); ++i) {
            if (isdigit(static_cast<unsigned char>(stamp[i])) && isdigit(static_cast<unsigned char>(stamp[i + 1])) &&
                stamp[i + 2] == ':' && isdigit(static_cast<unsigned char>(stamp[i + 3])) && isdigit(static_cast<unsigned char>(stamp[i + 4]))) {
                memcpy(clock, stamp + i, 5); break;
            }
        }
        snprintf(out.source_time, sizeof(out.source_time), "%.10s %s", date, clock);
    } else if (source == THAI_OIL) {
        if (strcmp(doc["status"] | "", "success")) return false;
        out.available = 0;
        for (int station = 0; station < 3; ++station) {
            for (int product = 0; product < 4; ++product) {
                const char *key = station == 2 && product == 3 ? "disel" : OIL_PRODUCTS[product];
                const auto entry = doc["response"]["stations"][OIL_STATIONS[station]][key];
                const char *expected[] = {"แก๊สโซฮอล์ 95", "แก๊สโซฮอล์ 91", "แก๊สโซฮอล์ E20", "ดีเซล B7"};
                const int index = station * 4 + product;
                if (!strcmp(entry["name"] | "", expected[product]) && price(entry["price"], out.values[index]))
                    out.available |= 1U << index;
                else out.values[index] = 0;
            }
        }
        // A successful partial response clears missing products instead of substituting cached peers.
        if (!out.available) return false;
        // Convert the source's Thai month / Buddhist year into an ASCII date, not fetch time.
        const char *date = doc["response"]["date"] | "";
        const char *months[] = {"มกราคม", "กุมภาพันธ์", "มีนาคม", "เมษายน", "พฤษภาคม", "มิถุนายน",
            "กรกฎาคม", "สิงหาคม", "กันยายน", "ตุลาคม", "พฤศจิกายน", "ธันวาคม"};
        int day = atoi(date), month = 0, year = 0;
        for (int i = 0; i < 12; ++i) {
            const char *found = strstr(date, months[i]);
            if (found) { month = i + 1; year = atoi(found + strlen(months[i])); break; }
        }
        if (year > 2400) year -= 543;
        if (day >= 1 && day <= 31 && month && year >= 2024 && year <= 2200)
            snprintf(out.source_time, sizeof(out.source_time), "As of %02d/%02d/%04d", day, month, year);
        else strlcpy(out.source_time, "Source date unknown", sizeof(out.source_time));
    } else {
        if (!doc["chart"]["error"].isNull()) return false;
        const auto meta = doc["chart"]["result"][0]["meta"];
        if (strcmp(meta["symbol"] | "", STOCK_SYMBOLS[source - SET]) ||
            strcmp(meta["instrumentType"] | "", source == SET ? "INDEX" : "EQUITY") ||
            strcmp(meta["currency"] | "", "THB")) return false;
        if (!number(meta["regularMarketPrice"], out.values[0]) || out.values[0] <= 0) return false;
        out.source_epoch = meta["regularMarketTime"] | time_t(0);
        if (out.source_epoch < 1704067200) return false;
        const auto result = doc["chart"]["result"][0];
        JsonArrayConst stamps = result["timestamp"];
        JsonArrayConst closes = result["indicators"]["quote"][0]["close"];
        out.history_count = 0;
        if (stamps.size() == closes.size()) {
            const size_t start = stamps.size() > HISTORY_CAPACITY ? stamps.size() - HISTORY_CAPACITY : 0;
            time_t previous = 0;
            for (size_t i = start; i < stamps.size(); ++i) {
                float close = 0;
                time_t epoch = stamps[i] | time_t(0);
                if (epoch >= 1704067200 && epoch > previous && epoch <= out.source_epoch &&
                    number(closes[i], close) && close > 0) {
                    out.history[out.history_count++] = {epoch, close};
                    previous = epoch;
                }
            }
        }
    }
    out.error = 0;
    return true;
}

bool fetch(Source source, Reading &out) {
    const uint32_t request_started = millis();
    Serial.printf("API[%u] phase=request start\n", unsigned(source));
    CooperativeTLS client{unsigned(source)};
    client.setCACert(API_ROOTS);

    client.setTimeout(3); // WiFiClientSecure takes SECONDS; Stream below takes milliseconds.
    client.Stream::setTimeout(100); // Bound legacy timedRead's tail after request expiry.
    HTTPClient http;
    http.setConnectTimeout(4000);
    http.setTimeout(3000);
    http.setReuse(false);
    http.useHTTP10(true); // Avoid chunk framing when reading a bounded body.
    http.setUserAgent("Mozilla/5.0"); // Yahoo returns 429 for the default client UA.
    if (!http.begin(client, URLS[source])) { out.error = -10; return false; }
    const char *headers[] = {"Transfer-Encoding"};
    http.collectHeaders(headers, 1);
    const int status = http.GET();
    Serial.printf("API[%u] phase=headers ms=%u status=%d expired=%u\n", unsigned(source),
        unsigned(millis() - request_started), status, unsigned(client.expired()));
    out.error = client.expired() ? -25 : status;
    if (status != HTTP_CODE_OK || client.expired()) { http.end(); return false; }
    // HTTP/1.0 was requested; never feed chunk framing to the JSON parser.
    if (http.header("Transfer-Encoding").length() &&
        !http.header("Transfer-Encoding").equalsIgnoreCase("identity")) {
        out.error = -26; http.end(); return false;
    }
    const int length = http.getSize();
    if (length > int(MAX_BODY)) { out.error = -22; http.end(); return false; }
    String body;
    if (!body.reserve(length > 0 ? length : 4096)) { out.error = -23; http.end(); return false; }
    auto *stream = http.getStreamPtr();
    const uint32_t started = millis();
    bool complete = false;
    while (millis() - started < BODY_TIMEOUT) {
        if (WiFi.status() != WL_CONNECTED) break;
        int available = stream->available();
        if (available > 0) {
            char buffer[512];
            // Read only currently available bytes; never enter Stream::readBytes timed polling.
            const int count = stream->read(reinterpret_cast<uint8_t *>(buffer), min(available, int(sizeof(buffer))));
            if (count > 0 && (body.length() + count > MAX_BODY || !body.concat(buffer, count))) break;
        }
        if ((length >= 0 && body.length() == size_t(length)) || (!http.connected() && !stream->available())) {
            complete = length < 0 || body.length() == size_t(length);
            break;
        }
        vTaskDelay(1); // Let IDLE0 run even on configurations with a coarse tick.
    }
    http.end();
    Serial.printf("API[%u] phase=body ms=%u bytes=%u complete=%u expired=%u\n", unsigned(source),
        unsigned(millis() - request_started), unsigned(body.length()), unsigned(complete), unsigned(client.expired()));
    if (client.expired()) { out.error = -25; return false; }
    if (!complete || body.isEmpty()) { out.error = -24; return false; }
    return decode(source, body, out);
}

// Called under snapshot_mux; never sleep/log/network while holding the lock.
void mark_failure(Source source, int error) {
    shared.readings[source].failed = true;
    shared.readings[source].error = error;
    if (error == 429 && source >= SET) {
        for (int j = SET; j < COUNT; ++j) {
            shared.readings[j].failed = true;
            shared.readings[j].error = 429;
        }
    }
}

void worker(void *) {
    ApiRetry retries[COUNT]{};
    uint32_t yahoo_blocked_at = 0;
    bool yahoo_blocked = false;
    for (;;) {
        // Correct wall time is mandatory for certificate validity; never bypass verification.
        if (WiFi.status() != WL_CONNECTED || time(nullptr) < 1704067200) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        for (int i = 0; i < COUNT && WiFi.status() == WL_CONNECTED; ++i) {
            uint32_t now = millis();
            const uint32_t interval = i == WEATHER ? WEATHER_INTERVAL : FINANCE_INTERVAL;
            if (!retries[i].ready(now)) continue;
            if (i >= SET && yahoo_blocked && now - yahoo_blocked_at < FINANCE_INTERVAL) continue;

            Reading result;
            const uint32_t fetch_started = millis();
            bool ok = fetch(static_cast<Source>(i), result);
            retries[i].finish(millis(), ok, interval);
            Serial.printf("API[%u] phase=done ms=%u ok=%u error=%d stack-min=%u retry-ms=%u failures=%u fallback=%s\n", unsigned(i),
                unsigned(millis() - fetch_started), unsigned(ok), int(result.error),
                unsigned(uxTaskGetStackHighWaterMark(nullptr)), unsigned(retries[i].wait),
                unsigned(retries[i].failures), ok ? "none" : "last-good-or-unavailable");
            if (!ok && result.error == 429 && i >= SET) {
                yahoo_blocked = true;
                yahoo_blocked_at = retries[i].attempted;
            }
            const time_t observed_at = time(nullptr);
            const uint32_t stack_free = uxTaskGetStackHighWaterMark(nullptr);
            portENTER_CRITICAL(&snapshot_mux);
            shared.worker_stack_free_bytes = stack_free;
            if (ok) {
                result.valid = true;
                result.received_ms = retries[i].attempted;
                if (i == THAI_GOLD) {
                    const auto &previous = shared.readings[i];
                    result.history_count = previous.history_count;
                    memcpy(result.history, previous.history, sizeof(result.history));
                    // Observation time, not a fabricated historical source/update timestamp.
                    append_observation(result, observed_at, result.values[0]);
                } else if (i >= SET && result.history_count < 2) {
                    const auto &previous = shared.readings[i];
                    if (previous.history_observed) {
                        result.history_count = previous.history_count;
                        memcpy(result.history, previous.history, sizeof(result.history));
                    } else result.history_count = 0;
                    result.history_observed = true;
                    append_observation(result, result.source_epoch, result.values[0]);
                }
                shared.readings[i] = result;
            } else {
                // Keep last good values and source timestamps; mark them visibly stale/error.
                mark_failure(static_cast<Source>(i), result.error);
            }
            ++shared.revision;
            portEXIT_CRITICAL(&snapshot_mux);
            vTaskDelay(pdMS_TO_TICKS(250)); // Sequential requests, not a burst of TLS clients.
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
} // namespace

bool parse_price(const char *value, float &result) {
    if (!value || !*value) return false;
    char plain[32];
    size_t n = 0;
    int group = 0;
    bool comma = false, decimal = false;
    for (const char *p = value; *p; ++p) {
        if (*p == ',') {
            if (decimal || group < 1 || (!comma && group > 3) || (comma && group != 3)) return false;
            comma = true;
            group = 0;
            continue;
        }
        if (*p == '.') {
            if (decimal || !group || (comma && group != 3)) return false;
            decimal = true;
        } else if (*p < '0' || *p > '9') return false;
        else ++group;
        if (n >= sizeof(plain) - 1) return false;
        plain[n++] = *p;
    }
    if (!group || (!decimal && comma && group != 3) || plain[n - 1] == '.') return false;
    plain[n] = '\0';
    char *end = nullptr;
    result = strtof(plain, &end);
    return end && !*end && isfinite(result) && result > 0;
}

void append_observation(Reading &reading, time_t epoch, float value) {
    if (epoch < 1704067200 || !isfinite(value) || value <= 0) return;
    if (reading.history_count && epoch <= reading.history[reading.history_count - 1].epoch) return;
    if (reading.history_count >= HISTORY_CAPACITY) {
        memmove(reading.history, reading.history + 1, (HISTORY_CAPACITY - 1) * sizeof(Observation));
        reading.history_count = HISTORY_CAPACITY - 1;
    }
    reading.history[reading.history_count++] = {epoch, value};
}

bool begin() {
    // Preemptible idle-priority background work shares CPU0 with IDLE0 even inside
    // framework/TLS calls that do not reach our cooperative stream polling.
    static_assert(configUSE_PREEMPTION == 1 && configUSE_TIME_SLICING == 1,
        "API idle-priority scheduling requires preemption and time slicing");
    const bool ok = xTaskCreatePinnedToCore(worker, "api_worker", 12288, nullptr, tskIDLE_PRIORITY, nullptr, 0) == pdPASS;
    portENTER_CRITICAL(&snapshot_mux);
    shared.worker_ok = ok;
    portEXIT_CRITICAL(&snapshot_mux);
    return ok;
}
void copy_snapshot(Snapshot &destination) {
    portENTER_CRITICAL(&snapshot_mux);
    destination = shared;
    portEXIT_CRITICAL(&snapshot_mux);
}
Diagnostics diagnostics() {
    portENTER_CRITICAL(&snapshot_mux);
    const Diagnostics result = {shared.revision, shared.worker_ok, shared.worker_stack_free_bytes};
    portEXIT_CRITICAL(&snapshot_mux);
    return result;
}
bool stale(const Reading &reading, Source source, uint32_t now) {
    return reading.failed || (reading.valid && now - reading.received_ms >= 2 * (source == WEATHER ? WEATHER_INTERVAL : FINANCE_INTERVAL));
}
} // namespace live_data
