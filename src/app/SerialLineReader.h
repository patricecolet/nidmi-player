#pragma once

#include <Arduino.h>
#include <functional>

class SerialLineReader {
public:
    static constexpr size_t kBufSize = 200;

    void setCallback(std::function<void(const char*)> cb) { onLine_ = std::move(cb); }
    void poll();

private:
    char                           buf_[kBufSize];
    size_t                         len_ = 0;
    std::function<void(const char*)> onLine_;
};
