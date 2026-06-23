#pragma once

#include "MidiEvent.h"
#include <cstddef>
#include <cstdint>
#include <vector>

/**
 * Parser Standard MIDI File (format 0 et 1).
 * Lit un buffer complet en memoire, produit une liste triee d'evenements
 * avec temps absolus pre-calcules (microsecondes).
 */
class SmfParser {
public:
    struct Result {
        uint16_t format     = 0;
        uint16_t trackCount = 0;
        uint16_t division   = 480;
        std::vector<MidiEvent>  events;
        std::vector<TempoChange> tempoMap;
        uint32_t totalTicks  = 0;
        int64_t  totalTimeUs = 0;
    };

    static bool parse(const uint8_t* data, size_t length, Result& out);

private:
    static void     computeTimes(Result& result);
    static bool     parseTrack(const uint8_t* data, size_t length, Result& out);
    static uint32_t readVLQ(const uint8_t*& p, const uint8_t* end);
    static uint16_t readU16BE(const uint8_t* p);
    static uint32_t readU32BE(const uint8_t* p);
};
