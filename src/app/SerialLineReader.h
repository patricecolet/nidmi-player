#pragma once

#include <Arduino.h>
#include <functional>

class SerialLineReader {
public:
    // 1024 : absorbe une ligne JSON avec un chunk d'upload base64
    // (512 o bruts -> ~684 caracteres + enveloppe JSON, voir JsonCommandApi).
    static constexpr size_t kBufSize = 1024;

    void setCallback(std::function<void(const char*)> cb) { onLine_ = std::move(cb); }
    void poll();

private:
    char                           buf_[kBufSize];
    size_t                         len_ = 0;
    std::function<void(const char*)> onLine_;
};
