#include "PlayerCommandHandler.h"
#include "../MidiPlayer.h"
#include <LittleFS.h>
#include <cstring>

static const char* nextTok(char** ctx) { return strtok_r(nullptr, " \t", ctx); }

void playerCommandLine(MidiPlayer& player, String& currentFile, char* line) {
    char* ctx = nullptr;
    char* cmd = strtok_r(line, " \t", &ctx);
    if (!cmd) return;

    if (strcmp(cmd, "play") == 0) {
        player.play();
        return;
    }
    if (strcmp(cmd, "stop") == 0) {
        player.stop();
        return;
    }
    if (strcmp(cmd, "pause") == 0) {
        player.pause();
        return;
    }
    if (strcmp(cmd, "toggle") == 0) {
        player.togglePlayPause();
        return;
    }
    if (strcmp(cmd, "loop") == 0) {
        const char* v = nextTok(&ctx);
        if (v) player.setLoop(atoi(v) != 0);
        else player.setLoop(!player.isLooping());
        return;
    }
    if (strcmp(cmd, "load") == 0) {
        const char* path = nextTok(&ctx);
        if (!path) {
            Serial.println("[player] usage: player load <path>");
            return;
        }
        if (player.loadFile(path)) {
            currentFile = path;
            Serial.printf("[player] charge %s\n", path);
        }
        return;
    }
    if (strcmp(cmd, "info") == 0) {
        Serial.printf("[player] %s BPM=%.1f prog=%.1f%% file=%s\n",
            player.stateName(), player.bpm(), player.progress() * 100.0f,
            currentFile.c_str());
        return;
    }
    Serial.println("[player] sous-commandes: play stop pause toggle loop [0|1] load <path> info");
}
