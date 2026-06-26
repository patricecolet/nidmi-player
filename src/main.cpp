/**
 * nidmi-player : lecteur MIDI (.mid) + step sequencer XOX via nidmi-core.
 *
 * WiFi AP + mDNS + RTP-MIDI + OSC UDP + MidiOscRouter.
 * Fichiers .mid sur LittleFS (flash) : pio run -t uploadfs
 * Commandes serie : voir showHelp().
 */

#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <nidmi_core/MidiOscRouter.h>
#include <nidmi_core/NetBootstrap.h>
#include <nidmi_core/OscUdpService.h>
#include <nidmi_core/RtpMidiService.h>
#include <nidmi_core/Version.h>

#include "MidiPlayer.h"
#include "app/SequencerCommandHandler.h"
#include "sequencer/EspSequencerAdapter.h"

static constexpr const char* kApSsid   = "nidmi-player";
static constexpr const char* kApPass   = "nidmipass";
static constexpr const char* kMdnsHost = "nidmiplayer";
static constexpr const char* kRtpName  = "nidmi-player";

static constexpr uint16_t kOscLocalPort = 4000;
static constexpr uint16_t kOscDestPort  = 9000;

static nidmi_core::RtpMidiService g_rtp;
static nidmi_core::OscUdpService  g_osc;
static nidmi_core::MidiOscRouter  g_router(g_rtp, g_osc);
static MidiPlayer            g_player(g_rtp);
static EspSequencerAdapter   g_seq(g_rtp);

static String g_currentFile;

// ---------------------------------------------------------------------------
// Pattern demo (4 rangees kick/snare/hihat/clap)
// ---------------------------------------------------------------------------

static void loadDemoPattern() {
    auto& e = g_seq.engine();
    e.setSteps(16);
    e.setNumRows(4);
    e.setTimeSignature(4, 4);
    e.setBpm(120);

    e.setRowChannel(0, 9);  // canal 10 (drums)
    e.setRowChannel(1, 9);
    e.setRowChannel(2, 9);
    e.setRowChannel(3, 9);

    // kick (note 36) : 1 et 9
    e.setStep(0, 0,  36, 100, 80);
    e.setStep(0, 8,  36, 100, 80);

    // snare (note 38) : 5 et 13
    e.setStep(1, 4,  38, 110, 60);
    e.setStep(1, 12, 38, 110, 60);

    // hihat (note 42) : tous les pas pairs
    for (uint8_t s = 0; s < 16; s += 2)
        e.setStep(2, s, 42, 80, 40);

    // clap (note 39) : pas 4
    e.setStep(3, 4, 39, 90, 50);

    // --- Sous-pattern demo : roll de hihat au pas 14, 4 sous-pas sur 2 pas ---
    uint8_t rollIdx = e.allocSubPattern(4, 2);
    if (rollIdx != kNoSubPattern) {
        e.setSubStep(rollIdx, 0, 42, 90, 50);
        e.setSubStep(rollIdx, 1, 42, 70, 50);
        e.setSubStep(rollIdx, 2, 42, 100, 50);
        e.setSubStep(rollIdx, 3, 42, 60, 50);
        e.setStepSubPattern(2, 14, rollIdx);
    }

    Serial.println("[seq] Pattern demo charge (avec sous-pattern)");
}

// ---------------------------------------------------------------------------
// Fichiers
// ---------------------------------------------------------------------------

static void listMidiFiles() {
    Serial.println("[player] Fichiers MIDI sur LittleFS :");
    File root = LittleFS.open("/");
    if (!root || !root.isDirectory()) {
        Serial.println("  (vide)");
        return;
    }
    int count = 0;
    File f = root.openNextFile();
    while (f) {
        String name = f.name();
        if (name.endsWith(".mid") || name.endsWith(".MID") || name.endsWith(".midi")) {
            Serial.printf("  /%s  (%u octets)\n", f.name(), static_cast<unsigned>(f.size()));
            ++count;
        }
        f = root.openNextFile();
    }
    if (count == 0)
        Serial.println("  (aucun fichier .mid)");
}

static String findFirstMidi() {
    File root = LittleFS.open("/");
    if (!root || !root.isDirectory()) return {};
    File f = root.openNextFile();
    while (f) {
        String name = f.name();
        if (name.endsWith(".mid") || name.endsWith(".MID") || name.endsWith(".midi")) {
            if (!name.startsWith("/")) name = "/" + name;
            return name;
        }
        f = root.openNextFile();
    }
    return {};
}

// ---------------------------------------------------------------------------
// Interface serie
// ---------------------------------------------------------------------------

static void showHelp() {
    Serial.println();
    Serial.println("=== nidmi-player ===");
    Serial.println(" -- Lecteur MIDI --");
    Serial.println("  p      play / reprendre");
    Serial.println("  s      stop");
    Serial.println("  ESPACE toggle play/pause");
    Serial.println("  l      toggle boucle");
    Serial.println("  i      info");
    Serial.println("  f      lister fichiers");
    Serial.println(" -- Sequenceur XOX --");
    Serial.println("  1      play/pause sequenceur");
    Serial.println("  2      stop sequenceur");
    Serial.println("  3      toggle pas courant (rangee 0)");
    Serial.println("  t      afficher pattern");
    Serial.println(" -- General --");
    Serial.println("  h      aide");
    Serial.println();
}

static void showInfo() {
    Serial.printf("[player] Etat: %s | BPM: %.1f | Progression: %.1f%%\n",
        g_player.stateName(), g_player.bpm(), g_player.progress() * 100.0f);
    Serial.printf("[player] Fichier: %s | Events: %u | Boucle: %s\n",
        g_currentFile.c_str(),
        static_cast<unsigned>(g_player.eventCount()),
        g_player.isLooping() ? "oui" : "non");
    if (g_player.durationUs() > 0)
        Serial.printf("[player] Duree: %.1fs\n", g_player.durationUs() / 1000000.0f);

    Serial.printf("[seq]    Etat: %s | BPM: %.1f | Pas: %u/%u | Boucle: %s\n",
        g_seq.stateName(), g_seq.bpm(),
        g_seq.currentStep(), g_seq.engine().pattern().numSteps,
        g_seq.isLooping() ? "oui" : "non");
}

static void handleSerial() {
    while (Serial.available()) {
        char c = Serial.read();
        switch (c) {
            // --- Lecteur MIDI ---
            case 'p': g_player.play();            break;
            case 's': g_player.stop();            break;
            case ' ': g_player.togglePlayPause(); break;
            case 'l':
                g_player.setLoop(!g_player.isLooping());
                Serial.printf("[player] Boucle : %s\n", g_player.isLooping() ? "oui" : "non");
                break;
            case 'i': showInfo();      break;
            case 'f': listMidiFiles(); break;

            // --- Sequenceur XOX ---
            case '1':
            case '2':
            case '3':
            case 't':
                sequencerCommandKey(g_seq, c);
                break;

            // --- General ---
            case 'h':
            case '?': showHelp(); break;
        }
    }
}

// ---------------------------------------------------------------------------
// Setup / Loop
// ---------------------------------------------------------------------------

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.print("[nidmi-player] nidmi-core ");
    Serial.println(nidmi_core::version());

    // --- WiFi AP ---
    if (!nidmi_core::netBeginSoftAp(kApSsid, kApPass, kMdnsHost)) {
        Serial.println("[nidmi-player] ERREUR: WiFi AP / mDNS");
        return;
    }
    Serial.print("[nidmi-player] AP IP: ");
    Serial.println(WiFi.softAPIP());

    // --- RTP-MIDI ---
    if (!g_rtp.begin(kRtpName)) {
        Serial.println("[nidmi-player] ERREUR: RTP-MIDI");
        return;
    }
    Serial.printf("[nidmi-player] RTP-MIDI pret (port %u)\n", g_rtp.port());

    // --- OSC UDP ---
    if (!g_osc.begin(kOscLocalPort)) {
        Serial.println("[nidmi-player] ERREUR: OSC UDP");
        return;
    }
    g_osc.setBroadcast(true);
    g_osc.setInterface(nidmi_core::OscNetInterface::AP);
    g_osc.setTarget("192.168.4.255", kOscDestPort);
    g_router.setAddressPrefix("/nidmi");
    g_router.wire();
    Serial.println("[nidmi-player] OSC + routeur MIDI<->OSC OK");

    // --- LittleFS ---
    if (!LittleFS.begin(true)) {
        Serial.println("[nidmi-player] LittleFS impossible (meme apres formatage)");
    } else {
        Serial.println("[nidmi-player] LittleFS OK");
        listMidiFiles();

        String first = findFirstMidi();
        if (!first.isEmpty()) {
            if (g_player.loadFile(first.c_str())) {
                g_currentFile = first;
            }
        } else {
            Serial.println("[nidmi-player] Aucun .mid — copier dans data/ puis 'pio run -t uploadfs'");
        }
    }

    // --- Sequenceur ---
    loadDemoPattern();

    showHelp();
}

void loop() {
    g_rtp.update();
    g_osc.update();
    g_player.update();
    g_seq.update();
    handleSerial();
}
