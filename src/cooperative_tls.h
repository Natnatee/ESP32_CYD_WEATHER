#pragma once
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <time.h>

// Background priority 0 protects IDLE0 even inside TLS; these sleeps also cover
// Arduino Stream::timedRead(), which otherwise busy-polls the virtual read().
class CooperativeTLS : public WiFiClientSecure {
    uint32_t started_ = millis(), rested_ = started_;
    bool expired_ = false;
    unsigned source_;
    const uint32_t request_budget_;
    bool poll() {
        if (millis() - rested_ >= 2) {
            vTaskDelay(1);
            rested_ = millis();
        }
        if (millis() - started_ >= request_budget_) {
            if (!expired_) WiFiClientSecure::stop();
            expired_ = true;
        }
        return !expired_;
    }
public:
    explicit CooperativeTLS(unsigned source = 0)
        : source_(source), request_budget_(source <= 2 ? 35000U : 20000U) {
        // Device logs: these three hosts failed around the old 5s handshake cap
        // (weather sometimes 12s inside an opaque crypto call). Yahoo works at 5s.
        // Allow bounded headroom for their different chains on an idle-priority ESP32.
        setHandshakeTimeout(source <= 2 ? 20 : 5); // Seconds, not milliseconds.
    }
    using WiFiClientSecure::connect;
    int connect(const char *host, uint16_t port) override { return connect(host, port, 4000); }
    int connect(const char *host, uint16_t port, int32_t timeout) override {
        if (!poll()) return 0;
        Serial.printf("API[%u] phase=dns start ms=%u\n", source_, unsigned(millis() - started_));
        IPAddress address;
        // Framework DNS has blocking event waits (16s serialization + 15s lookup),
        // not a caller timeout. Its callback storage stays owned by the framework.
        const int resolved = WiFi.hostByName(host, address);
        Serial.printf("API[%u] phase=dns done ms=%u ok=%u\n", source_, unsigned(millis() - started_), unsigned(resolved != 0));
        if (!poll() || !resolved) return 0;
        _timeout = timeout > 0 && timeout <= 4000 ? timeout : 4000;
        Serial.printf("API[%u] phase=tcp-tls start ms=%u epoch=%lld heap=%u largest=%u handshake-s=%u budget-ms=%u\n",
            source_, unsigned(millis() - started_), static_cast<long long>(time(nullptr)),
            unsigned(heap_caps_get_free_size(MALLOC_CAP_8BIT)),
            unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),
            source_ <= 2 ? 20U : 5U, unsigned(request_budget_));
        // This SIX-argument overload preserves hostname for both SNI and certificate
        // verification. The five-argument IP-only overload would replace it with the IP.
        const int result = WiFiClientSecure::connect(address, port, host, _CA_cert, _cert, _private_key);
        if (!result) {
            char message[96]{};
            const int code = lastError(message, sizeof(message));
            // -1 is a generic framework failure (including handshake timeout), NOT
            // proof of a bad CA. Do not turn it into a misleading certificate error.
            Serial.printf("API[%u] TLS error=%d hex=0x%04x text=%s\n", source_, code,
                unsigned(code < 0 ? -code : code), message);
        }
        Serial.printf("API[%u] phase=tcp-tls done ms=%u ok=%u\n", source_, unsigned(millis() - started_), unsigned(result != 0));
        return poll() ? result : 0;
    }
    bool expired() const { return expired_; }
    int available() override { return poll() ? WiFiClientSecure::available() : 0; }
    int read() override {
        uint8_t byte;
        return read(&byte, 1) == 1 ? byte : -1;
    }
    int read(uint8_t *buffer, size_t size) override {
        return poll() ? WiFiClientSecure::read(buffer, size) : -1;
    }
    int peek() override { return poll() ? WiFiClientSecure::peek() : -1; }
    uint8_t connected() override { return poll() ? WiFiClientSecure::connected() : 0; }
    size_t write(const uint8_t *buffer, size_t size) override {
        return poll() ? WiFiClientSecure::write(buffer, size) : 0;
    }
    size_t write(uint8_t byte) override { return write(&byte, 1); }
};
