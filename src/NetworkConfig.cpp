#include "NetworkConfig.h"

#include <ArduinoJson.h>
#include <LittleFS.h>

bool NetworkConfig::load() {
    File f = LittleFS.open(kPath, "r");
    if (!f) return false;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) return false;

    if (doc["wifi"]["apSsid"].is<const char*>())     apSsid         = doc["wifi"]["apSsid"].as<const char*>();
    if (doc["wifi"]["apPass"].is<const char*>())     apPass         = doc["wifi"]["apPass"].as<const char*>();
    if (doc["wifi"]["mdnsHost"].is<const char*>())   mdnsHost       = doc["wifi"]["mdnsHost"].as<const char*>();
    if (doc["rtp"]["sessionName"].is<const char*>()) rtpSessionName = doc["rtp"]["sessionName"].as<const char*>();
    if (doc["osc"]["localPort"].is<uint16_t>())      oscLocalPort   = doc["osc"]["localPort"].as<uint16_t>();
    if (doc["osc"]["destIp"].is<const char*>())      oscDestIp      = doc["osc"]["destIp"].as<const char*>();
    if (doc["osc"]["destPort"].is<uint16_t>())        oscDestPort    = doc["osc"]["destPort"].as<uint16_t>();
    return true;
}

bool NetworkConfig::save() const {
    JsonDocument doc;
    doc["wifi"]["apSsid"]     = apSsid;
    doc["wifi"]["apPass"]     = apPass;
    doc["wifi"]["mdnsHost"]   = mdnsHost;
    doc["rtp"]["sessionName"] = rtpSessionName;
    doc["osc"]["localPort"]   = oscLocalPort;
    doc["osc"]["destIp"]      = oscDestIp;
    doc["osc"]["destPort"]    = oscDestPort;

    File f = LittleFS.open(kPath, "w");
    if (!f) return false;
    bool ok = serializeJson(doc, f) > 0;
    f.close();
    return ok;
}
