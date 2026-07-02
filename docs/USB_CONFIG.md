# Etude — serveur de config via USB (en complement du routing MIDI)

> Statut : **etude** (2026-07-02). Rien d'implemente. A retravailler avant de coder.

## 1. Besoin

Exposer un **service de controle/config** du player (transport, reglages, patterns,
parametres reseau...) accessible **par le cable USB**, en **complement** du routing
MIDI existant (RTP-MIDI + OSC sur WiFi).

Cibles : **ESP32-S3 et ESP32-C3** (parite exigee).

## 2. Contraintes materielles USB

| Capacite USB | ESP32-S3 | ESP32-C3 |
|---|---|---|
| USB-OTG natif (TinyUSB : CDC, MIDI, HID, reseau NCM/RNDIS/ECM) | oui | **non** |
| USB-Serial/JTAG (port serie CDC simple) | oui (en plus) | oui (**seul** dispo) |
| Serveur **HTTP reel** via USB (device = carte reseau USB -> IP -> httpd) | possible | **impossible** |
| Canal **serie CDC** (protocole requete/reponse) | oui | oui |

**Consequence** : un vrai serveur web navigable par cable (`http://...`) suppose que la
carte s'annonce comme adaptateur reseau USB (TinyUSB NCM/RNDIS + lwIP). Faisable
**sur S3 uniquement**. Le **seul denominateur commun S3 + C3 est un canal serie USB
(CDC)** — toute solution bi-carte passe par la, pas par un HTTP embarque.

## 3. Architectures etudiees

### Option A — Protocole config sur CDC + UI web cote hote (Web Serial) — **recommandee**

- Le device expose un protocole requete/reponse (JSON ou lignes) sur le port USB-CDC.
- L'UI « web » est une **page HTML cote hote** qui parle au port via la
  **Web Serial API** (Chrome/Edge). Le device n'est pas un serveur HTTP : il est un
  pair serie ; la page fournit l'interface.
- Marche sur **S3 ET C3** (meme code device). Zero pile reseau embarquee en plus.
- Limite : Web Serial = **Chrome/Edge desktop uniquement** (pas Safari, pas iPad/iOS).
  Sur iPad : on garde le **WiFi** (ou app native).

### Option B — USB reseau (NCM/RNDIS) + httpd embarque

- Vrai `http://...` par cable, navigateur standard.
- **S3 uniquement** -> casse la parite C3. TinyUSB-NET sous Arduino-ESP32 n'est pas
  cle-en-main.
- Verdict : **ecarte** tant que la C3 est requise. Peut revenir comme *bonus S3*
  plus tard (voir §5, etape 5).

### Option C — CDC + pont hote (proxy serie -> HTTP/WebSocket)

- Un mini-process sur le Mac traduit le CDC en HTTP/WS -> n'importe quel navigateur
  (y compris iPad via le reseau du Mac).
- Marche sur les deux cartes, mais impose d'installer/lancer un pont -> moins
  plug & play. **Non retenu en v1.**

## 4. Recommandation

**Option A.** Seule solution qui respecte S3 **et** C3 avec un seul chemin device,
sans pile reseau embarquee. Le device est un « serveur de config » au sens
**protocole sur CDC** ; le « web » vit sur l'hote (Web Serial). Le **WiFi** reste la
voie pour les clients sans Web Serial (iPad/Safari) — les deux voies attaquent la
**meme API de config**.

## 5. Impact sur le code existant

La graine du serveur de config **existe deja mais n'est pas branchee** :

- `src/app/SerialLineReader` + `playerCommandLine` + `sequencerCommandLine` :
  protocole ligne deja ecrit (`play`, `loop 1`, `seq bpm 128`, `seq ts 7 8`...).
  **`main.cpp` ne les utilise pas** (single-key `handleSerial()` inline).
- La **config reseau est en dur** dans `main.cpp` (SSID, pass, mDNS, nom RTP,
  ports/cible OSC). C'est precisement ce que le serveur config doit exposer/editer
  -> a sortir vers **NVS ou un JSON LittleFS** (LittleFS deja monte).
- Cote transport USB, rien a ecrire au niveau protocole : le CDC *est* deja `Serial`.
  Le vrai sujet est que **le port console est aussi le port config** (voir §6).

### Etapes proposees (incremental)

1. Cabler `SerialLineReader` + un dispatcher racine (`player ...` / `seq ...` /
   `config ...`) dans `loop()` (remplace/complete `handleSerial`).
2. Definir l'**API config** (get/set : reseau, transports, transport player, pattern)
   + un **format** (recommande : JSON requete/reponse, une ligne = un message).
3. Persistance config (NVS ou `/config.json` LittleFS).
4. Page **Web Serial** cote hote qui parle cette API — meme API reutilisee par un
   futur web WiFi.
5. (Bonus S3, plus tard) evaluer NCM + httpd pour un vrai `http://` par cable.

### Ce qui concerne `nidmi-core` (vs player)

- L'archi multi-transport documentee dans nidmi-core (`MidiSender` -> `MidiRouter`
  -> RTP/BLE/USB/UART) est **specifiee mais non implementee** ; ce chantier config
  n'en depend pas.
- Si la config reseau/transports devient generique (SSID, ports, flags transports),
  sa structure (`CoreConfig`-like) et sa persistance pourraient migrer vers
  nidmi-core ; en v1, rester cote player.

## 6. Points a decider avant de coder

- **Console vs protocole sur le meme CDC** : les logs `Serial.println` humains et le
  protocole partagent le port USB. Options :
  - (a) framer le protocole (prefixe/JSON par ligne) et garder les logs ;
  - (b) basculer les logs sur une UART secondaire ;
  - (c) sur **S3 seulement**, device composite 2x CDC (console + config) —
    impossible sur C3.
- **Format** : JSON (souple, parseable navigateur) vs lignes texte (deja en place,
  leger). Penchant : JSON pour l'UI.
- **iPad/Safari** : passent par le **WiFi**, pas l'USB (Web Serial indisponible).
- **Perimetre config v1** : transport uniquement (play/loop/bpm...) ou aussi
  reseau + patterns ?
