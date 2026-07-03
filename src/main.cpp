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
#include <nidmi_core/EspUartMidiTransport.h>
#include <nidmi_core/MidiOscRouter.h>
#include <nidmi_core/NetBootstrap.h>
#include <nidmi_core/OscUdpService.h>
#include <nidmi_core/RtpMidiService.h>
#include <nidmi_core/UartMidiTransport.h>
#include <nidmi_core/Version.h>

#include <cstring>

#include "MidiFanOut.h"
#include "MidiPlayer.h"
#include "NetworkConfig.h"
#include "app/JsonCommandApi.h"
#include "app/PlayerCommandHandler.h"
#include "app/SequencerCommandHandler.h"
#include "app/SerialLineReader.h"
#include "sequencer/EspSequencerAdapter.h"

// Sortie MIDI filaire (DIN/TRS) : broche TX du XIAO ESP32-S3 (silkscreen "TX"/D6,
// GPIO43). Cable a l'entree MIDI IN de l'equipement via le circuit standard
// (resistance/optocoupleur cote reception) — voir nidmi-core/docs/UART_MIDI.md.
static constexpr uint8_t kUartMidiTxPin = 43;

static nidmi_core::RtpMidiService      g_rtp;
static nidmi_core::OscUdpService       g_osc;
static nidmi_core::MidiOscRouter       g_router(g_rtp, g_osc);
static nidmi_core::EspUartMidiTransport g_uartMidi;
static MidiFanOut             g_midiOut(g_rtp, g_uartMidi);
static MidiPlayer             g_player(g_midiOut);
static EspSequencerAdapter    g_seq(g_midiOut);
static SerialLineReader       g_lineReader;
static NetworkConfig          g_netConfig;
static String                 g_currentFile;
static JsonCommandApi         g_jsonApi(g_player, g_currentFile, g_netConfig);

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
    Serial.println("Chaque commande se termine par Entree.");
    Serial.println(" -- Raccourcis (1 caractere) --");
    Serial.println("  p      play / reprendre");
    Serial.println("  s      stop");
    Serial.println("  ESPACE toggle play/pause");
    Serial.println("  l      toggle boucle");
    Serial.println("  i      info");
    Serial.println("  f      lister fichiers");
    Serial.println("  1      play/pause sequenceur");
    Serial.println("  2      stop sequenceur");
    Serial.println("  3      toggle pas courant (rangee 0)");
    Serial.println("  t      afficher pattern");
    Serial.println("  h      aide");
    Serial.println(" -- Commandes ligne --");
    Serial.println("  player play|stop|pause|toggle|loop [0|1]|load <path>|info");
    Serial.println("  seq    play|stop|pause|toggle|loop [0|1]|bpm <f>|steps <n>|ts <n> <d>|toggle_step <r> <s>|print");
    Serial.println(" -- Protocole JSON (config / upload .mid) --");
    Serial.println("  Toute ligne commencant par '{' est traitee comme du JSON.");
    Serial.println("  Exemples: {\"cmd\":\"config.get\"}  {\"cmd\":\"player.info\"}");
    Serial.println("  Voir docs/USB_CONFIG.md pour le schema complet.");
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
    Serial.printf("[midi]   RTP: %s | UART(TX=GPIO%u): %s\n",
        g_rtp.isReady() ? "ok" : "off",
        kUartMidiTxPin,
        g_uartMidi.isReady() ? "ok" : "off");
}

static void handleKey(char c) {
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

        default:
            Serial.printf("[nidmi-player] touche inconnue '%c' — 'h' pour l'aide\n", c);
            break;
    }
}

// Dispatcher racine : "player ..." / "seq ..." / "config ..." / JSON ("{...}").
// Une ligne d'un seul caractere = raccourci clavier (voir showHelp).
static void handleLine(const char* lineIn) {
    if (lineIn[0] == '{') {
        String resp = g_jsonApi.handle(lineIn);
        if (resp.length()) Serial.println(resp);
        return;
    }

    char line[SerialLineReader::kBufSize];
    strlcpy(line, lineIn, sizeof(line));

    if (line[0] != '\0' && line[1] == '\0') {
        handleKey(line[0]);
        return;
    }

    // Separer le premier mot (racine) du reste de la ligne.
    char* rest = line;
    while (*rest && *rest != ' ' && *rest != '\t') ++rest;
    if (*rest) *rest++ = '\0';

    // Racine sans argument : sonde "?" pour declencher l'usage du handler.
    static char usageProbe[] = "?";
    if (!*rest) rest = usageProbe;

    if (strcmp(line, "player") == 0) {
        playerCommandLine(g_player, g_currentFile, rest);
        return;
    }
    if (strcmp(line, "seq") == 0) {
        sequencerCommandLine(g_seq, rest);
        return;
    }
    if (strcmp(line, "config") == 0) {
        Serial.println("[config] utiliser le protocole JSON, ex: {\"cmd\":\"config.get\"} — voir docs/USB_CONFIG.md");
        return;
    }
    if (strcmp(line, "help") == 0) {
        showHelp();
        return;
    }
    Serial.printf("[nidmi-player] commande inconnue: '%s' — 'h' pour l'aide\n", line);
}

// ---------------------------------------------------------------------------
// Setup / Loop
// ---------------------------------------------------------------------------

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.print("[nidmi-player] nidmi-core ");
    Serial.println(nidmi_core::version());

    // --- LittleFS (monte en premier : /config.json doit etre lu avant le WiFi) ---
    bool fsOk = LittleFS.begin(true);
    if (!fsOk) {
        Serial.println("[nidmi-player] LittleFS impossible (meme apres formatage)");
    } else {
        Serial.println("[nidmi-player] LittleFS OK");
        if (g_netConfig.load()) {
            Serial.println("[nidmi-player] /config.json charge");
        } else {
            Serial.println("[nidmi-player] /config.json absent — valeurs par defaut");
        }
    }

    // --- WiFi AP ---
    if (!nidmi_core::netBeginSoftAp(g_netConfig.apSsid.c_str(), g_netConfig.apPass.c_str(), g_netConfig.mdnsHost.c_str())) {
        Serial.println("[nidmi-player] ERREUR: WiFi AP / mDNS");
        return;
    }
    Serial.print("[nidmi-player] AP IP: ");
    Serial.println(WiFi.softAPIP());
    Serial.printf("[nidmi-player] AP SSID: %s\n", g_netConfig.apSsid.c_str());

    // --- RTP-MIDI ---
    if (!g_rtp.begin(g_netConfig.rtpSessionName.c_str())) {
        Serial.println("[nidmi-player] ERREUR: RTP-MIDI");
        return;
    }
    Serial.printf("[nidmi-player] RTP-MIDI pret (port %u)\n", g_rtp.port());

    // --- UART MIDI (DIN/TRS filaire, TX-only) ---
    {
        nidmi_core::UartMidiConfig uartCfg;
        uartCfg.enable   = true;
        uartCfg.uartIndex = 1;   // Serial1 (UART materiel dedie, independant de la console USB)
        uartCfg.txPin    = kUartMidiTxPin;
        uartCfg.rxPin    = -1;   // TX seul (voir docs/UART_MIDI.md : tester TX avant RX)
        if (g_uartMidi.begin(uartCfg)) {
            Serial.printf("[nidmi-player] UART MIDI pret (TX=GPIO%u)\n", kUartMidiTxPin);
        } else {
            Serial.println("[nidmi-player] ERREUR: UART MIDI (non bloquant, on continue)");
        }
    }

    // --- OSC UDP ---
    if (!g_osc.begin(g_netConfig.oscLocalPort)) {
        Serial.println("[nidmi-player] ERREUR: OSC UDP");
        return;
    }
    g_osc.setBroadcast(true);
    g_osc.setInterface(nidmi_core::OscNetInterface::AP);
    g_osc.setTarget(g_netConfig.oscDestIp.c_str(), g_netConfig.oscDestPort);
    g_router.setAddressPrefix("/nidmi");
    g_router.wire();
    Serial.println("[nidmi-player] OSC + routeur MIDI<->OSC OK");

    // --- Fichiers MIDI (LittleFS deja monte plus haut) ---
    if (fsOk) {
        listMidiFiles();

        String first = findFirstMidi();
        if (!first.isEmpty()) {
            if (g_player.loadFile(first.c_str())) {
                g_currentFile = first;
            }
        } else {
            Serial.println("[nidmi-player] Aucun .mid — copier dans data/ puis 'pio run -t uploadfs', ou 'file.begin' via JSON");
        }
    }

    // --- Sequenceur ---
    loadDemoPattern();

    // --- Commandes serie (lignes terminees par Entree) ---
    g_lineReader.setCallback(handleLine);

    showHelp();
}

void loop() {
    g_rtp.update();
    g_uartMidi.update();
    g_osc.update();
    g_player.update();
    g_seq.update();
    g_lineReader.poll();
}
