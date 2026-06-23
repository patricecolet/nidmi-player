#include "SmfParser.h"
#include <algorithm>

uint16_t SmfParser::readU16BE(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] << 8) | p[1];
}

uint32_t SmfParser::readU32BE(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |
            static_cast<uint32_t>(p[3]);
}

uint32_t SmfParser::readVLQ(const uint8_t*& p, const uint8_t* end) {
    uint32_t value = 0;
    for (int i = 0; i < 4 && p < end; ++i) {
        uint8_t b = *p++;
        value = (value << 7) | (b & 0x7F);
        if (!(b & 0x80)) return value;
    }
    return value;
}

bool SmfParser::parseTrack(const uint8_t* data, size_t length, Result& out) {
    const uint8_t* p   = data;
    const uint8_t* end = data + length;
    uint32_t absTick   = 0;
    uint8_t running    = 0;

    while (p < end) {
        uint32_t delta = readVLQ(p, end);
        absTick += delta;
        if (p >= end) break;

        uint8_t byte0 = *p;

        if (byte0 == 0xFF) {
            // --- Meta event ---
            p++;
            if (p >= end) break;
            uint8_t metaType = *p++;
            uint32_t metaLen = readVLQ(p, end);

            if (metaType == 0x51 && metaLen == 3 && p + 3 <= end) {
                uint32_t tempo = (static_cast<uint32_t>(p[0]) << 16) |
                                 (static_cast<uint32_t>(p[1]) << 8)  |
                                  static_cast<uint32_t>(p[2]);
                out.tempoMap.push_back({absTick, tempo});
            }
            p += metaLen;

        } else if (byte0 == 0xF0 || byte0 == 0xF7) {
            // --- SysEx (skip) ---
            p++;
            uint32_t sysLen = readVLQ(p, end);
            p += sysLen;

        } else {
            // --- Channel message ---
            uint8_t status;
            if (byte0 & 0x80) {
                status  = byte0;
                running = status;
                p++;
            } else {
                status = running;
            }

            uint8_t type    = status & 0xF0;
            uint8_t channel = status & 0x0F;

            MidiEvent evt{};
            evt.tick    = absTick;
            evt.status  = type;
            evt.channel = channel;

            switch (type) {
                case 0x80: // Note Off
                case 0x90: // Note On
                case 0xA0: // Poly Key Pressure
                case 0xB0: // Control Change
                case 0xE0: // Pitch Bend
                    if (p + 2 <= end) {
                        evt.data1 = *p++;
                        evt.data2 = *p++;
                    }
                    out.events.push_back(evt);
                    break;

                case 0xC0: // Program Change
                case 0xD0: // Channel Pressure
                    if (p + 1 <= end) {
                        evt.data1 = *p++;
                    }
                    out.events.push_back(evt);
                    break;

                default:
                    break;
            }
        }
    }

    if (absTick > out.totalTicks) {
        out.totalTicks = absTick;
    }
    return true;
}

bool SmfParser::parse(const uint8_t* data, size_t length, Result& out) {
    if (length < 14) return false;

    if (data[0] != 'M' || data[1] != 'T' || data[2] != 'h' || data[3] != 'd')
        return false;

    uint32_t headerLen = readU32BE(data + 4);
    if (headerLen < 6) return false;

    out.format     = readU16BE(data + 8);
    out.trackCount = readU16BE(data + 10);
    out.division   = readU16BE(data + 12);

    if (out.division & 0x8000) return false;  // SMPTE not supported

    const uint8_t* p   = data + 8 + headerLen;
    const uint8_t* end = data + length;

    for (uint16_t t = 0; t < out.trackCount && p + 8 <= end; ++t) {
        if (p[0] != 'M' || p[1] != 'T' || p[2] != 'r' || p[3] != 'k')
            return false;

        uint32_t trackLen = readU32BE(p + 4);
        p += 8;
        if (p + trackLen > end) return false;

        parseTrack(p, trackLen, out);
        p += trackLen;
    }

    std::stable_sort(out.events.begin(), out.events.end(),
        [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });

    std::stable_sort(out.tempoMap.begin(), out.tempoMap.end(),
        [](const TempoChange& a, const TempoChange& b) { return a.tick < b.tick; });

    computeTimes(out);
    return true;
}

void SmfParser::computeTimes(Result& result) {
    uint32_t usPerQ    = 500000;  // 120 BPM default
    uint32_t lastTick  = 0;
    int64_t  timeUs    = 0;
    size_t   tempoIdx  = 0;
    uint16_t division  = result.division;

    for (auto& evt : result.events) {
        while (tempoIdx < result.tempoMap.size() &&
               result.tempoMap[tempoIdx].tick <= evt.tick) {
            uint32_t tTick = result.tempoMap[tempoIdx].tick;
            if (tTick > lastTick) {
                timeUs   += static_cast<int64_t>(tTick - lastTick) * usPerQ / division;
                lastTick  = tTick;
            }
            usPerQ = result.tempoMap[tempoIdx].usPerQuarter;
            ++tempoIdx;
        }

        if (evt.tick > lastTick) {
            timeUs   += static_cast<int64_t>(evt.tick - lastTick) * usPerQ / division;
            lastTick  = evt.tick;
        }
        evt.timeUs = timeUs;
    }

    // totalTimeUs : fin du dernier evenement ou totalTicks
    if (result.totalTicks > lastTick) {
        timeUs += static_cast<int64_t>(result.totalTicks - lastTick) * usPerQ / division;
    }
    result.totalTimeUs = timeUs;
}
