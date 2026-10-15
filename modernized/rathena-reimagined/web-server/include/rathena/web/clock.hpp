// rathena-web-server: injectable clock (token grace window, booking timestamps).
#pragma once

#include <atomic>
#include <chrono>

#include "rathena/web/types.hpp"

namespace rathena::web {

class Clock {
public:
    virtual ~Clock() = default;
    virtual Timestamp now() const = 0;
};

class SystemClock final : public Clock {
public:
    Timestamp now() const override {
        return std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());
    }
};

/// Test clock.
class ManualClock final : public Clock {
public:
    explicit ManualClock(Timestamp start = Timestamp{std::chrono::seconds{1'700'000'000}})
        : now_(start.time_since_epoch().count()) {}
    Timestamp now() const override { return Timestamp{std::chrono::seconds{now_.load()}}; }
    void advance(std::chrono::seconds d) { now_ += d.count(); }
    void set(Timestamp t) { now_ = t.time_since_epoch().count(); }

private:
    std::atomic<long long> now_;
};

}  // namespace rathena::web
