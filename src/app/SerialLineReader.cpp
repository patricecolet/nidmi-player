#include "SerialLineReader.h"

void SerialLineReader::poll() {
    if (!onLine_) return;
    while (Serial.available()) {
        int c = Serial.read();
        if (c < 0) break;
        if (c == '\r') continue;
        if (c == '\n') {
            if (len_ > 0) {
                buf_[len_] = '\0';
                onLine_(buf_);
                len_ = 0;
            }
            continue;
        }
        if (len_ + 1 < kBufSize)
            buf_[len_++] = static_cast<char>(c);
    }
}
