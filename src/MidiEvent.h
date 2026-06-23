#pragma once

#include <cstdint>

struct MidiEvent {
    uint32_t tick;
    int64_t  timeUs;   // absolute microseconds from start (pre-computed)
    uint8_t  status;   // high nibble: 0x80-0xE0
    uint8_t  channel;  // 0-15
    uint8_t  data1;
    uint8_t  data2;
};

struct TempoChange {
    uint32_t tick;
    uint32_t usPerQuarter;
};
