#pragma once

#include <nidmi_core/RtpMidiService.h>
#include <nidmi_core/UartMidiTransport.h>

/**
 * Duplique chaque evenement MIDI vers RTP-MIDI (WiFi) et UART (DIN/TRS filaire).
 * Fan-out fixe a 2 sorties : le MidiRouter generique (N transports) est
 * documente dans nidmi-core/docs/ARCHITECTURE.md mais pas encore implemente ;
 * cette classe couvre le seul besoin actuel du player.
 */
class MidiFanOut {
public:
    MidiFanOut(nidmi_core::RtpMidiService& rtp, nidmi_core::UartMidiTransport& uart)
        : rtp_(rtp), uart_(uart) {}

    void sendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
        rtp_.sendNoteOn(channel, note, velocity);
        uart_.sendNoteOn(channel, note, velocity);
    }
    void sendNoteOff(uint8_t channel, uint8_t note, uint8_t velocity) {
        rtp_.sendNoteOff(channel, note, velocity);
        uart_.sendNoteOff(channel, note, velocity);
    }
    void sendControlChange(uint8_t channel, uint8_t control, uint8_t value) {
        rtp_.sendControlChange(channel, control, value);
        uart_.sendControlChange(channel, control, value);
    }
    void sendProgramChange(uint8_t channel, uint8_t program) {
        rtp_.sendProgramChange(channel, program);
        uart_.sendProgramChange(channel, program);
    }
    void sendPitchBend(uint8_t channel, int bend) {
        rtp_.sendPitchBend(channel, bend);
        uart_.sendPitchBend(channel, bend);
    }
    void sendAftertouch(uint8_t channel, uint8_t pressure) {
        rtp_.sendAftertouch(channel, pressure);
        uart_.sendAftertouch(channel, pressure);
    }
    void sendKeyPressure(uint8_t channel, uint8_t note, uint8_t pressure) {
        rtp_.sendKeyPressure(channel, note, pressure);
        uart_.sendKeyPressure(channel, note, pressure);
    }
    void sendClock()    { rtp_.sendClock();    uart_.sendClock(); }
    void sendStart()    { rtp_.sendStart();    uart_.sendStart(); }
    void sendStop()     { rtp_.sendStop();     uart_.sendStop(); }
    void sendContinue() { rtp_.sendContinue(); uart_.sendContinue(); }

private:
    nidmi_core::RtpMidiService&    rtp_;
    nidmi_core::UartMidiTransport& uart_;
};
