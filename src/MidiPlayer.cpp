#include "MidiPlayer.h"
#include <Arduino.h>
#include <esp_timer.h>
#include <FS.h>
#include <LittleFS.h>

static inline int64_t nowUs() { return esp_timer_get_time(); }

MidiPlayer::MidiPlayer(nidmi_core::RtpMidiService& rtp) : rtp_(rtp) {}

// ---------------------------------------------------------------------------
// Chargement
// ---------------------------------------------------------------------------

bool MidiPlayer::load(const uint8_t* data, size_t length) {
    stop();
    score_  = SmfParser::Result{};
    loaded_ = false;

    if (!SmfParser::parse(data, length, score_)) {
        Serial.println("[player] Erreur parsing SMF");
        return false;
    }

    loaded_ = true;
    resetToStart();

    Serial.printf("[player] Charge : format=%u pistes=%u div=%u events=%u duree=%.1fs\n",
        score_.format, score_.trackCount, score_.division,
        static_cast<unsigned>(score_.events.size()),
        score_.totalTimeUs / 1000000.0f);
    return true;
}

bool MidiPlayer::loadFile(const char* path) {
    File f = LittleFS.open(path, "r");
    if (!f) {
        Serial.printf("[player] Impossible d'ouvrir : %s\n", path);
        return false;
    }

    size_t sz = f.size();
    uint8_t* buf = new (std::nothrow) uint8_t[sz];
    if (!buf) {
        Serial.println("[player] Memoire insuffisante");
        f.close();
        return false;
    }

    f.read(buf, sz);
    f.close();

    bool ok = load(buf, sz);
    delete[] buf;
    return ok;
}

// ---------------------------------------------------------------------------
// Transport
// ---------------------------------------------------------------------------

void MidiPlayer::play() {
    if (!loaded_) return;

    if (state_ == State::PAUSED) {
        playStartUs_ = nowUs();
        lastClockUs_ = playStartUs_;
        state_       = State::PLAYING;
        rtp_.sendContinue();
        Serial.println("[player] Continue");
        return;
    }

    resetToStart();
    playStartUs_ = nowUs();
    lastClockUs_ = playStartUs_;
    state_       = State::PLAYING;

    rtp_.sendStart();
    Serial.printf("[player] Play (%.1f BPM)\n", bpm());
}

void MidiPlayer::stop() {
    if (state_ == State::STOPPED) return;
    sendAllNotesOff();
    rtp_.sendStop();
    state_ = State::STOPPED;
    resetToStart();
    Serial.println("[player] Stop");
}

void MidiPlayer::pause() {
    if (state_ != State::PLAYING) return;
    sendAllNotesOff();
    pauseOffsetUs_ += nowUs() - playStartUs_;
    state_ = State::PAUSED;
    Serial.println("[player] Pause");
}

void MidiPlayer::togglePlayPause() {
    if (state_ == State::PLAYING) pause();
    else play();
}

// ---------------------------------------------------------------------------
// Boucle principale
// ---------------------------------------------------------------------------

void MidiPlayer::update() {
    if (state_ != State::PLAYING || !loaded_) return;

    const int64_t elapsed = pauseOffsetUs_ + (nowUs() - playStartUs_);

    while (nextEvent_ < score_.events.size()) {
        const auto& evt = score_.events[nextEvent_];
        if (evt.timeUs > elapsed) break;
        dispatchEvent(evt);
        ++nextEvent_;
    }

    // Avancer le tempo courant pour l'affichage BPM
    while (nextTempo_ < score_.tempoMap.size() &&
           nextEvent_ > 0 &&
           score_.tempoMap[nextTempo_].tick <= score_.events[nextEvent_ - 1].tick) {
        usPerQuarter_ = score_.tempoMap[nextTempo_].usPerQuarter;
        ++nextTempo_;
    }

    updateClock();

    if (nextEvent_ >= score_.events.size()) {
        if (loop_) {
            Serial.println("[player] Boucle");
            sendAllNotesOff();
            rtp_.sendStop();
            resetToStart();
            playStartUs_ = nowUs();
            lastClockUs_ = playStartUs_;
            rtp_.sendStart();
        } else {
            Serial.println("[player] Fin du morceau");
            sendAllNotesOff();
            rtp_.sendStop();
            state_ = State::STOPPED;
            resetToStart();
        }
    }
}

// ---------------------------------------------------------------------------
// Accesseurs
// ---------------------------------------------------------------------------

const char* MidiPlayer::stateName() const {
    switch (state_) {
        case State::STOPPED: return "STOPPED";
        case State::PLAYING: return "PLAYING";
        case State::PAUSED:  return "PAUSED";
    }
    return "?";
}

float MidiPlayer::bpm() const {
    return 60000000.0f / static_cast<float>(usPerQuarter_);
}

float MidiPlayer::progress() const {
    if (!loaded_ || score_.totalTimeUs <= 0) return 0.0f;
    if (state_ == State::STOPPED) return 0.0f;

    int64_t elapsed = pauseOffsetUs_;
    if (state_ == State::PLAYING)
        elapsed += nowUs() - playStartUs_;

    float p = static_cast<float>(elapsed) / static_cast<float>(score_.totalTimeUs);
    return (p > 1.0f) ? 1.0f : p;
}

// ---------------------------------------------------------------------------
// Dispatch MIDI
// ---------------------------------------------------------------------------

void MidiPlayer::dispatchEvent(const MidiEvent& evt) {
    const uint8_t ch = evt.channel + 1;  // nidmi-core : 1-16

    switch (evt.status) {
        case 0x90:
            if (evt.data2 > 0)
                rtp_.sendNoteOn(ch, evt.data1, evt.data2);
            else
                rtp_.sendNoteOff(ch, evt.data1, 0);
            break;
        case 0x80:
            rtp_.sendNoteOff(ch, evt.data1, evt.data2);
            break;
        case 0xB0:
            rtp_.sendControlChange(ch, evt.data1, evt.data2);
            break;
        case 0xC0:
            rtp_.sendProgramChange(ch, evt.data1);
            break;
        case 0xD0:
            rtp_.sendAftertouch(ch, evt.data1);
            break;
        case 0xE0: {
            int bend = (static_cast<int>(evt.data2) << 7 | evt.data1) - 8192;
            rtp_.sendPitchBend(ch, bend);
            break;
        }
        case 0xA0:
            rtp_.sendKeyPressure(ch, evt.data1, evt.data2);
            break;
    }
}

void MidiPlayer::sendAllNotesOff() {
    for (uint8_t ch = 1; ch <= 16; ++ch) {
        rtp_.sendControlChange(ch, 123, 0);  // All Notes Off
        rtp_.sendControlChange(ch, 121, 0);  // Reset All Controllers
    }
}

void MidiPlayer::updateClock() {
    if (usPerQuarter_ == 0) return;
    const int64_t interval = usPerQuarter_ / 24;  // 24 ppqn
    const int64_t now      = nowUs();

    while (now - lastClockUs_ >= interval) {
        rtp_.sendClock();
        lastClockUs_ += interval;
    }
}

void MidiPlayer::resetToStart() {
    nextEvent_      = 0;
    nextTempo_      = 0;
    pauseOffsetUs_  = 0;
    usPerQuarter_   = 500000;

    if (!score_.tempoMap.empty() && score_.tempoMap[0].tick == 0) {
        usPerQuarter_ = score_.tempoMap[0].usPerQuarter;
        nextTempo_    = 1;
    }
}
