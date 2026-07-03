#pragma once

#include <Arduino.h>

/**
 * Config reseau persistee sur LittleFS (/config.json). Les valeurs par defaut
 * ci-dessous reprennent les anciennes constantes en dur de main.cpp ; elles
 * restent en place si le fichier est absent ou invalide (aucun echec bloquant).
 */
struct NetworkConfig {
    String   apSsid         = "nidmi-player";
    String   apPass         = "nidmipass";
    String   mdnsHost       = "nidmiplayer";
    String   rtpSessionName = "nidmi-player";
    uint16_t oscLocalPort   = 4000;
    String   oscDestIp      = "192.168.4.255";
    uint16_t oscDestPort    = 9000;

    static constexpr const char* kPath = "/config.json";

    // LittleFS.begin() doit avoir ete appele avant. Ne modifie rien et
    // retourne false si le fichier est absent ou invalide.
    bool load();

    // Ecrase /config.json avec l'etat courant. Retourne false si l'ecriture echoue.
    bool save() const;
};
