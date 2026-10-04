/* Standalone desktop HAL backend. Compile this with crystal_hal.hpp in a host test target. */
#include "crystal_hal.hpp"

#include <chrono>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {
class MockBrightness final : public IBrightness {
public:
    void set(uint8_t pct) override { value_ = pct > 100 ? 100 : pct; }
    uint8_t get() const override { return value_; }
private:
    uint8_t value_ = 100;
};

class MockRtc final : public IRtc {
public:
    bool read(struct tm *out) override {
        if (!out) return false;
        const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        return localtime_r(&now, out) != nullptr;
    }
    bool write(const struct tm *) override { return true; }
};

class MockWifi final : public IWifi {
public:
    void set_event_callback(EventCallback callback, void *context) override { callback_ = callback; context_ = context; }
    void start() override { started_ = true; }
    void scan() override {}
    void connect(const char *, const char *) override { connected_ = started_; }
    void forget() override { connected_ = false; }
    bool connected() const override { return connected_; }
    bool has_ip() const override { return connected_; }
    bool enabled() const override { return started_; }
    void set_enabled(bool enabled) override { started_ = enabled; if (!enabled) connected_ = false; }
    const char *last_ssid() const override { return ssid_; }
    size_t scan_results(Network *, size_t) const override { return 0; }
    bool ip_config(IpConfig *out) const override { if (!out || !connected_) return false; *out = ip_; return true; }
    bool set_ip_config(const IpConfig &config) override { ip_ = config; return true; }
    bool mac(uint8_t out[6]) const override { if (!out) return false; const uint8_t value[6] = {0x02, 0, 0, 0, 0, 1}; std::memcpy(out, value, 6); return true; }
    bool rssi(int8_t *out) const override { if (!out || !connected_) return false; *out = -48; return true; }
    void set_power_save(bool enabled) override { power_save_ = enabled; }
    bool set_hostname(const char *name) override { if (!name || !*name) return false; hostname_ = name; return true; }
private:
    bool started_ = false;
    bool connected_ = false;
    EventCallback callback_ = nullptr;
    void *context_ = nullptr;
    char ssid_[33] = {};
    IpConfig ip_{true, 0x6401A8C0, 0x00FFFFFF, 0x0101A8C0, 0x08080808, 0};
    bool power_save_ = false;
    std::string hostname_ = "crystal";
};

class MockPower final : public IPower {
public:
    bool readBattery(int *percent, bool *charging) override { if (!percent || !charging) return false; *percent = 82; *charging = false; return true; }
};

class MockSystemInfo final : public ISystemInfo {
public:
    uint32_t free_heap() const override { return 256 * 1024; }
    uint32_t free_psram() const override { return 4 * 1024 * 1024; }
    uint32_t uptime_seconds() const override { return 3600; }
    const char *reset_reason() const override { return "Power on"; }
    const char *idf_version() const override { return "host"; }
    const char *app_version() const override { return "sim"; }
    bool chip_id(uint8_t out[6]) const override { if (!out) return false; const uint8_t value[6] = {0x02, 0, 0, 0, 0, 1}; std::memcpy(out, value, 6); return true; }
    bool storage_bytes(uint32_t *used, uint32_t *total) const override { if (!used || !total) return false; *used = 1024 * 1024; *total = 4 * 1024 * 1024; return true; }
};

class MockStorage final : public IStorage {
public:
    bool get(const char *key, void *value, size_t *length) override {
        if (!key || !length) return false;
        auto it = values_.find(key);
        if (it == values_.end() || *length < it->second.size()) return false;
        if (!value && !it->second.empty()) return false;
        memcpy(value, it->second.data(), it->second.size());
        *length = it->second.size();
        return true;
    }
    bool set(const char *key, const void *value, size_t length) override {
        if (!key || (!value && length)) return false;
        auto &entry = values_[key];
        entry.resize(length);
        if (length) std::memcpy(entry.data(), value, length);
        return true;
    }
    bool erase(const char *key) override { return key && values_.erase(key) != 0; }
private:
    std::map<std::string, std::vector<uint8_t>> values_;
};

class MockTouch final : public ITouchRaw {
public:
    bool read(Point *out) override { if (!out) return false; *out = point_; return true; }
    Point point_{};
};

MockBrightness brightness;
MockRtc rtc;
MockWifi wifi;
MockStorage storage;
MockTouch touch;
MockPower power;
MockSystemInfo system_info;
CrystalHal mock_hal = {&brightness, &rtc, &wifi, &storage, &touch, &power, &system_info};
}

CrystalHal &hal() { return mock_hal; }
void crystal_hal_init() {}
void crystal_hal_bind_touch(void *) {}
