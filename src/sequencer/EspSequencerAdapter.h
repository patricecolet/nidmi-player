#pragma once

#include <nidmi_seq/SequencerEngine.h>
#include <nidmi_seq/SequencerCommandApi.h>
#include <nidmi_seq/SequencerClockDriver.h>
#include "../MidiFanOut.h"
#include <esp_timer.h>

class EspSequencerAdapter {
public:
    explicit EspSequencerAdapter(MidiFanOut& out)
        : out_(out) {}

    // --- Transport ---
    void play() {
        SequencerCommand cmd;
        cmd.id = SequencerCommandId::Play;
        dispatchCommand(cmd);
    }
    void stop() {
        SequencerCommand cmd;
        cmd.id = SequencerCommandId::Stop;
        dispatchCommand(cmd);
    }
    void pause() {
        SequencerCommand cmd;
        cmd.id = SequencerCommandId::Pause;
        dispatchCommand(cmd);
    }
    void togglePlayPause() {
        SequencerCommand cmd;
        cmd.id = SequencerCommandId::TogglePlayPause;
        dispatchCommand(cmd);
    }

    SequencerCommandResult dispatchCommand(const SequencerCommand& cmd) {
        return dispatchCommandAt(cmd, esp_timer_get_time());
    }

    SequencerCommandResult dispatchCommandAt(const SequencerCommand& cmd, int64_t nowUs) {
        auto out = SequencerCommandApi::dispatch(engine_, cmd, nowUs);

        switch (cmd.id) {
            case SequencerCommandId::Play:
            case SequencerCommandId::Stop:
            case SequencerCommandId::Pause:
            case SequencerCommandId::TogglePlayPause:
            case SequencerCommandId::SetBpm:
            case SequencerCommandId::SetTimeSignature:
            case SequencerCommandId::SetSteps:
                clock_.reset(nowUs);
                break;
            default:
                break;
        }

        dispatchEvents();
        return out;
    }

    // --- Boucle principale : appeler dans loop() ---
    void update() {
        int64_t now = esp_timer_get_time();
        clock_.tick(engine_, now);
        dispatchEvents();
    }

    // --- Acces direct au moteur pour l'edition ---
    SequencerEngine&       engine()       { return engine_; }
    const SequencerEngine& engine() const { return engine_; }

    // --- Raccourcis d'infos ---
    SequencerEngine::State state()       const { return engine_.state(); }
    const char*            stateName()   const { return engine_.stateName(); }
    float                  bpm()         const { return engine_.bpm(); }
    uint8_t                currentStep() const { return engine_.currentStep(); }
    bool                   isLooping()   const { return engine_.isLooping(); }

    void printPattern() const;

private:
    SequencerEngine        engine_;
    SequencerClockDriver   clock_;
    MidiFanOut&            out_;

    void dispatchEvents() {
        const auto& q = engine_.events();
        for (uint8_t i = 0; i < q.count; ++i) {
            const auto& e = q.buf[i];
            switch (e.type) {
                case SeqEventType::NoteOn:
                    out_.sendNoteOn(e.channel, e.data1, e.data2);
                    break;
                case SeqEventType::NoteOff:
                    out_.sendNoteOff(e.channel, e.data1, e.data2);
                    break;
                default:
                    break;
            }
        }
        engine_.clearEvents();
    }
};
