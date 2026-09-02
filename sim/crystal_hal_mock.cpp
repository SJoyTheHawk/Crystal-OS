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
    void start() override { started_ = true; }
    void scan() override {}
    void connect(const char *, const char *) override { connected_ = started_; }
    bool connected() const override { return connected_; }
private:
    bool started_ = false;
    bool connected_ = false;
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
CrystalHal mock_hal = {&brightness, &rtc, &wifi, &storage, &touch};
}

CrystalHal &hal() { return mock_hal; }
void crystal_hal_init() {}
void crystal_hal_bind_touch(void *) {}
