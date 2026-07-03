#include "JsonCommandApi.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <libb64/cdecode.h>

#include "../MidiPlayer.h"
#include "../NetworkConfig.h"

JsonCommandApi::JsonCommandApi(MidiPlayer& player, String& currentFile, NetworkConfig& netConfig)
    : player_(player), currentFile_(currentFile), netConfig_(netConfig) {}

bool JsonCommandApi::isValidMidiPath(const String& path) {
    if (!path.startsWith("/")) return false;
    if (path.indexOf("..") >= 0) return false;
    String lower = path;
    lower.toLowerCase();
    return lower.endsWith(".mid") || lower.endsWith(".midi");
}

void JsonCommandApi::abortUpload() {
    if (uploadFile_) {
        uploadFile_.close();
        LittleFS.remove(uploadPath_);
    }
    uploadActive_       = false;
    uploadPath_          = "";
    uploadExpectedSize_ = 0;
    uploadWritten_       = 0;
}

String JsonCommandApi::handle(const char* requestLine) {
    JsonDocument req;
    DeserializationError parseErr = deserializeJson(req, requestLine);

    JsonDocument resp;
    if (req["id"].is<long>()) resp["id"] = req["id"].as<long>();

    if (parseErr) {
        resp["ok"]    = false;
        resp["error"] = "JSON invalide";
        String out;
        serializeJson(resp, out);
        return out;
    }

    const char* cmd = req["cmd"].is<const char*>() ? req["cmd"].as<const char*>() : nullptr;
    if (!cmd) {
        resp["ok"]    = false;
        resp["error"] = "champ 'cmd' manquant";
        String out;
        serializeJson(resp, out);
        return out;
    }

    JsonVariantConst data = req["data"];
    bool ok = true;
    String error;

    // -------------------------------------------------------------- config
    if (strcmp(cmd, "config.get") == 0) {
        JsonObject wifi = resp["data"]["wifi"].to<JsonObject>();
        wifi["apSsid"]   = netConfig_.apSsid;
        wifi["apPass"]   = "********";
        wifi["mdnsHost"] = netConfig_.mdnsHost;
        resp["data"]["rtp"]["sessionName"] = netConfig_.rtpSessionName;
        JsonObject osc = resp["data"]["osc"].to<JsonObject>();
        osc["localPort"] = netConfig_.oscLocalPort;
        osc["destIp"]    = netConfig_.oscDestIp;
        osc["destPort"]  = netConfig_.oscDestPort;

    } else if (strcmp(cmd, "config.set") == 0) {
        if (data["wifi"]["apSsid"].is<const char*>())     netConfig_.apSsid         = data["wifi"]["apSsid"].as<const char*>();
        if (data["wifi"]["apPass"].is<const char*>())     netConfig_.apPass         = data["wifi"]["apPass"].as<const char*>();
        if (data["wifi"]["mdnsHost"].is<const char*>())   netConfig_.mdnsHost       = data["wifi"]["mdnsHost"].as<const char*>();
        if (data["rtp"]["sessionName"].is<const char*>()) netConfig_.rtpSessionName = data["rtp"]["sessionName"].as<const char*>();
        if (data["osc"]["localPort"].is<uint16_t>())      netConfig_.oscLocalPort   = data["osc"]["localPort"].as<uint16_t>();
        if (data["osc"]["destIp"].is<const char*>())      netConfig_.oscDestIp      = data["osc"]["destIp"].as<const char*>();
        if (data["osc"]["destPort"].is<uint16_t>())       netConfig_.oscDestPort    = data["osc"]["destPort"].as<uint16_t>();

        if (netConfig_.save()) {
            resp["data"]["needsReboot"] = true;
        } else {
            ok    = false;
            error = "echec ecriture " + String(NetworkConfig::kPath);
        }

    } else if (strcmp(cmd, "config.reboot") == 0) {
        resp["ok"] = true;
        String out;
        serializeJson(resp, out);
        Serial.println(out);
        Serial.flush();
        delay(100);
        ESP.restart();
        return "";  // jamais atteint

    // -------------------------------------------------------------- player
    } else if (strcmp(cmd, "player.play") == 0) {
        player_.play();
    } else if (strcmp(cmd, "player.stop") == 0) {
        player_.stop();
    } else if (strcmp(cmd, "player.pause") == 0) {
        player_.pause();
    } else if (strcmp(cmd, "player.toggle") == 0) {
        player_.togglePlayPause();
    } else if (strcmp(cmd, "player.loop") == 0) {
        bool on = data["on"].is<bool>() ? data["on"].as<bool>() : !player_.isLooping();
        player_.setLoop(on);
        resp["data"]["loop"] = on;
    } else if (strcmp(cmd, "player.load") == 0) {
        const char* path = data["path"].is<const char*>() ? data["path"].as<const char*>() : nullptr;
        if (!path) {
            ok = false; error = "champ 'data.path' manquant";
        } else if (!player_.loadFile(path)) {
            ok = false; error = "echec chargement " + String(path);
        } else {
            currentFile_ = path;
        }
    } else if (strcmp(cmd, "player.info") == 0) {
        resp["data"]["state"]    = player_.stateName();
        resp["data"]["bpm"]      = player_.bpm();
        resp["data"]["progress"] = player_.progress();
        resp["data"]["file"]     = currentFile_;
        resp["data"]["events"]   = static_cast<unsigned>(player_.eventCount());
        resp["data"]["loop"]     = player_.isLooping();
        resp["data"]["durationS"] = player_.durationUs() / 1000000.0f;

    // ---------------------------------------------------------------- file
    } else if (strcmp(cmd, "file.begin") == 0) {
        const char* path = data["path"].is<const char*>() ? data["path"].as<const char*>() : nullptr;
        long size = data["size"].is<long>() ? data["size"].as<long>() : -1;
        if (uploadActive_) {
            ok = false; error = "upload deja en cours (" + uploadPath_ + ")";
        } else if (!path || !isValidMidiPath(String(path))) {
            ok = false; error = "chemin invalide (attendu /nom.mid)";
        } else if (size <= 0) {
            ok = false; error = "champ 'data.size' invalide";
        } else if (static_cast<size_t>(size) > LittleFS.totalBytes() - LittleFS.usedBytes()) {
            ok = false; error = "espace LittleFS insuffisant";
        } else {
            uploadFile_ = LittleFS.open(path, "w");
            if (!uploadFile_) {
                ok = false; error = "impossible d'ouvrir " + String(path);
            } else {
                uploadPath_          = path;
                uploadExpectedSize_ = static_cast<size_t>(size);
                uploadWritten_       = 0;
                uploadActive_        = true;
            }
        }

    } else if (strcmp(cmd, "file.chunk") == 0) {
        long offset = data["offset"].is<long>() ? data["offset"].as<long>() : -1;
        const char* b64 = data["data"].is<const char*>() ? data["data"].as<const char*>() : nullptr;
        if (!uploadActive_) {
            ok = false; error = "aucun upload en cours (file.begin d'abord)";
        } else if (offset < 0 || static_cast<size_t>(offset) != uploadWritten_) {
            ok = false; error = "offset invalide, attendu " + String(static_cast<unsigned>(uploadWritten_));
        } else if (!b64 || strlen(b64) > kMaxChunkB64Len) {
            ok = false; error = "chunk 'data.data' manquant ou trop grand";
            abortUpload();
        } else {
            char decoded[kMaxChunkBytes + 16];
            int n = base64_decode_chars(b64, static_cast<int>(strlen(b64)), decoded);
            if (n <= 0 || static_cast<size_t>(n) > kMaxChunkBytes ||
                uploadWritten_ + static_cast<size_t>(n) > uploadExpectedSize_) {
                ok = false; error = "chunk invalide ou depasse la taille declaree";
                abortUpload();
            } else {
                size_t written = uploadFile_.write(reinterpret_cast<const uint8_t*>(decoded), n);
                if (written != static_cast<size_t>(n)) {
                    ok = false; error = "echec ecriture LittleFS";
                    abortUpload();
                } else {
                    uploadWritten_ += written;
                    resp["data"]["written"] = static_cast<unsigned>(uploadWritten_);
                }
            }
        }

    } else if (strcmp(cmd, "file.end") == 0) {
        if (!uploadActive_) {
            ok = false; error = "aucun upload en cours";
        } else if (uploadWritten_ != uploadExpectedSize_) {
            error = "taille recue (" + String(static_cast<unsigned>(uploadWritten_)) +
                    ") != annoncee (" + String(static_cast<unsigned>(uploadExpectedSize_)) + ")";
            ok = false;
            abortUpload();
        } else {
            resp["data"]["path"] = uploadPath_;
            resp["data"]["size"] = static_cast<unsigned>(uploadWritten_);
            uploadFile_.close();
            uploadActive_ = false;
            uploadPath_   = "";
        }

    } else if (strcmp(cmd, "file.abort") == 0) {
        if (!uploadActive_) {
            ok = false; error = "aucun upload en cours";
        } else {
            abortUpload();
        }

    } else if (strcmp(cmd, "file.list") == 0) {
        JsonArray files = resp["data"]["files"].to<JsonArray>();
        File root = LittleFS.open("/");
        if (root && root.isDirectory()) {
            File f = root.openNextFile();
            while (f) {
                String name = f.name();
                if (isValidMidiPath(name.startsWith("/") ? name : "/" + name)) {
                    JsonObject entry = files.add<JsonObject>();
                    entry["path"] = name.startsWith("/") ? name : "/" + name;
                    entry["size"] = static_cast<unsigned>(f.size());
                }
                f = root.openNextFile();
            }
        }

    } else if (strcmp(cmd, "file.delete") == 0) {
        const char* path = data["path"].is<const char*>() ? data["path"].as<const char*>() : nullptr;
        if (!path || !isValidMidiPath(String(path))) {
            ok = false; error = "chemin invalide";
        } else if (!LittleFS.exists(path)) {
            ok = false; error = "fichier introuvable";
        } else if (!LittleFS.remove(path)) {
            ok = false; error = "echec suppression";
        }

    } else {
        ok = false;
        error = "commande inconnue: " + String(cmd);
    }

    resp["ok"] = ok;
    if (!ok) resp["error"] = error;

    String out;
    serializeJson(resp, out);
    return out;
}
