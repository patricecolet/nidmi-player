#pragma once

#include "MidiFanOut.h"
#include "SmfParser.h"

/**
 * Lecteur MIDI non-bloquant : charge un fichier SMF (.mid),
 * dispatche les evenements en temps reel via MidiFanOut (RTP-MIDI + UART),
 * emet le MIDI Clock (24 ppqn) et les messages de transport.
 *
 * Appeler update() dans loop().
 */
class MidiPlayer {
public:
    enum class State : uint8_t { STOPPED, PLAYING, PAUSED };

    explicit MidiPlayer(MidiFanOut& out);

    bool load(const uint8_t* data, size_t length);
    bool loadFile(const char* path);

    void play();
    void stop();
    void pause();
    void togglePlayPause();

    void setLoop(bool on)  { loop_ = on; }
    bool isLooping() const { return loop_; }
    bool isLoaded()  const { return loaded_; }

    void update();

    State       state()       const { return state_; }
    const char* stateName()   const;
    float       bpm()         const;
    float       progress()    const;
    size_t      eventCount()  const { return score_.events.size(); }
    uint32_t    totalTicks()  const { return score_.totalTicks; }
    int64_t     durationUs()  const { return score_.totalTimeUs; }

private:
    MidiFanOut&        out_;
    SmfParser::Result  score_;

    State  state_  = State::STOPPED;
    bool   loaded_ = false;
    bool   loop_   = false;

    size_t nextEvent_    = 0;
    size_t nextTempo_    = 0;

    int64_t  playStartUs_   = 0;
    int64_t  pauseOffsetUs_ = 0;
    int64_t  lastClockUs_   = 0;
    uint32_t usPerQuarter_  = 500000;

    void dispatchEvent(const MidiEvent& evt);
    void sendAllNotesOff();
    void updateClock();
    void resetToStart();
};
