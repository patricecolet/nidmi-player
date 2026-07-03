#pragma once

#include <Arduino.h>
#include <FS.h>

class MidiPlayer;
struct NetworkConfig;

/**
 * Dispatcher JSON transport-agnostique : une ligne JSON en entree, une ligne
 * JSON en sortie (sans '\n' final ; String vide si aucune reponse a envoyer,
 * seul cas actuel : config.reboot, qui imprime elle-meme sa reponse avant de
 * redemarrer). Reutilisable tel quel par un futur transport WiFi (meme
 * requete/reponse, seul le porteur change) — voir docs/USB_CONFIG.md.
 *
 * Commandes : config.get/set/reboot, player.play/stop/pause/toggle/loop/
 * load/info, file.begin/chunk/end/abort/list/delete.
 */
class JsonCommandApi {
public:
    JsonCommandApi(MidiPlayer& player, String& currentFile, NetworkConfig& netConfig);

    String handle(const char* requestLine);

private:
    MidiPlayer&    player_;
    String&        currentFile_;
    NetworkConfig& netConfig_;

    // --- Upload de fichier : un seul transfert actif a la fois ---
    static constexpr size_t kMaxChunkBytes  = 512;
    static constexpr size_t kMaxChunkB64Len = 700;  // marge au-dela de base64(512)

    File   uploadFile_;
    String uploadPath_;
    size_t uploadExpectedSize_ = 0;
    size_t uploadWritten_      = 0;
    bool   uploadActive_       = false;

    void abortUpload();
    static bool isValidMidiPath(const String& path);
};
