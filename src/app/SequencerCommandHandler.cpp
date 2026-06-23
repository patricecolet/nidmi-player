#include "SequencerCommandHandler.h"
#include "../sequencer/EspSequencerAdapter.h"
#include <Arduino.h>
#include <cstring>

static const char* nextTok(char** ctx) { return strtok_r(nullptr, " \t", ctx); }

static void printUsage() {
    Serial.println("[seq] commandes:");
    Serial.println("  play stop pause toggle");
    Serial.println("  loop [0|1]");
    Serial.println("  bpm <float>");
    Serial.println("  steps <n>");
    Serial.println("  ts <num> <den>");
    Serial.println("  toggle_step <row> <step>");
    Serial.println("  print");
}

void sequencerCommandLine(EspSequencerAdapter& seq, char* line) {
    if (!line || !*line) return;

    char* ctx = nullptr;
    char* cmd = strtok_r(line, " \t", &ctx);
    if (!cmd) return;

    SequencerCommand c;

    if (strcmp(cmd, "play") == 0) {
        c.id = SequencerCommandId::Play;
        seq.dispatchCommand(c);
        return;
    }
    if (strcmp(cmd, "stop") == 0) {
        c.id = SequencerCommandId::Stop;
        seq.dispatchCommand(c);
        return;
    }
    if (strcmp(cmd, "pause") == 0) {
        c.id = SequencerCommandId::Pause;
        seq.dispatchCommand(c);
        return;
    }
    if (strcmp(cmd, "toggle") == 0) {
        c.id = SequencerCommandId::TogglePlayPause;
        seq.dispatchCommand(c);
        return;
    }
    if (strcmp(cmd, "loop") == 0) {
        const char* v = nextTok(&ctx);
        c.id = SequencerCommandId::SetLoop;
        c.x  = v ? (atoi(v) != 0) : !seq.isLooping();
        seq.dispatchCommand(c);
        return;
    }
    if (strcmp(cmd, "bpm") == 0) {
        const char* v = nextTok(&ctx);
        if (!v) {
            Serial.println("[seq] usage: seq bpm <float>");
            return;
        }
        c.id  = SequencerCommandId::SetBpm;
        c.f32 = static_cast<float>(atof(v));
        seq.dispatchCommand(c);
        return;
    }
    if (strcmp(cmd, "steps") == 0) {
        const char* v = nextTok(&ctx);
        if (!v) {
            Serial.println("[seq] usage: seq steps <n>");
            return;
        }
        c.id = SequencerCommandId::SetSteps;
        c.a  = static_cast<uint8_t>(atoi(v));
        seq.dispatchCommand(c);
        return;
    }
    if (strcmp(cmd, "ts") == 0) {
        const char* n = nextTok(&ctx);
        const char* d = nextTok(&ctx);
        if (!n || !d) {
            Serial.println("[seq] usage: seq ts <num> <den>");
            return;
        }
        c.id = SequencerCommandId::SetTimeSignature;
        c.a  = static_cast<uint8_t>(atoi(n));
        c.b  = static_cast<uint8_t>(atoi(d));
        seq.dispatchCommand(c);
        return;
    }
    if (strcmp(cmd, "toggle_step") == 0) {
        const char* row  = nextTok(&ctx);
        const char* step = nextTok(&ctx);
        if (!row || !step) {
            Serial.println("[seq] usage: seq toggle_step <row> <step>");
            return;
        }
        c.id = SequencerCommandId::ToggleStep;
        c.a  = static_cast<uint8_t>(atoi(row));
        c.b  = static_cast<uint8_t>(atoi(step));
        seq.dispatchCommand(c);
        return;
    }
    if (strcmp(cmd, "print") == 0) {
        seq.printPattern();
        return;
    }

    printUsage();
}

void sequencerCommandKey(EspSequencerAdapter& seq, char key) {
    SequencerCommand c;
    switch (key) {
        case '1':
            c.id = SequencerCommandId::TogglePlayPause;
            seq.dispatchCommand(c);
            break;
        case '2':
            c.id = SequencerCommandId::Stop;
            seq.dispatchCommand(c);
            break;
        case '3':
            c.id = SequencerCommandId::ToggleStep;
            c.a  = 0;
            c.b  = seq.currentStep();
            seq.dispatchCommand(c);
            Serial.printf("[seq] Toggle R0 pas %u\n", seq.currentStep());
            break;
        case 't':
            seq.printPattern();
            break;
        default:
            break;
    }
}

