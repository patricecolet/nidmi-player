# Serveur de config via USB (en complement du routing MIDI)

> Statut : **etapes 1 et 2 implementees et validees sur XIAO ESP32-S3**
> (2026-07-03). Etape 3 (page Web Serial) et le transport WiFi restent a faire —
> voir §7.

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

### Etapes

1. ✅ Cablage `SerialLineReader` + dispatcher racine (`player ...` / `seq ...` /
   `config ...` / JSON) dans `loop()`.
2. ✅ **API config JSON** (`JsonCommandApi`, `src/app/JsonCommandApi.h/.cpp`) +
   persistance (`NetworkConfig`, `/config.json` LittleFS) — voir §7 pour le
   schema complet et §8 pour les decisions de conception.
3. Page **Web Serial** cote hote qui parle cette API — meme API reutilisee par un
   futur web WiFi (non fait ; testable pour l'instant via scripts Python, cf. §7).
4. (Bonus S3, plus tard) evaluer NCM + httpd pour un vrai `http://` par cable.

### Ce qui concerne `nidmi-core` (vs player)

- L'archi multi-transport documentee dans nidmi-core (`MidiSender` -> `MidiRouter`
  -> RTP/BLE/USB/UART) est **specifiee mais non implementee** ; ce chantier config
  n'en depend pas.
- Si la config reseau/transports devient generique (SSID, ports, flags transports),
  sa structure (`CoreConfig`-like) et sa persistance pourraient migrer vers
  nidmi-core ; en v1, rester cote player.

## 6. Points tranches (etaient "a decider")

- **Console vs protocole sur le meme CDC** : option (a) retenue — une ligne
  commencant par `{` est routee vers `JsonCommandApi`, tout le reste suit
  l'ancien chemin texte (`player`/`seq`/raccourcis clavier/logs). Les deux
  coexistent sans framing special ; les logs `Serial.println` des handlers
  existants continuent d'apparaitre normalement, une page Web Serial les
  ignorera simplement (elle ne lit que les lignes JSON qu'elle sait parser).
- **Format** : JSON, une requete/reponse par ligne (newline-delimited).
  Lib `bblanchon/ArduinoJson@^7`.
- **iPad/Safari** : confirme, passent par le WiFi (pas construit ici).
- **Perimetre config v1** : reseau (`config.*`) + controle du lecteur MIDI
  (`player.*`, reprend `MidiPlayer` existant) + **upload de fichier `.mid` a
  chaud** (`file.*`, nouveau — jusque-la impossible sans `pio run -t uploadfs`).
  Le sequenceur (`seq ...`) reste sur son chemin texte existant.

## 7. Schema JSON implemente

Dispatcher : `JsonCommandApi::handle(const char*) -> String`
(`src/app/JsonCommandApi.h/.cpp`), **transport-agnostique** — ne connait pas le
port serie. Un futur transport WiFi (WebSocket/HTTP) n'a qu'a lui passer le
corps de la requete et renvoyer la `String` telle quelle : c'est ce qui
realise « meme interface CDC que WiFi » sans construire le serveur WiFi.

Convention uniforme : **tous** les parametres d'une commande vivent sous
`data` (objet), y compris pour `file.chunk` ou `data.data` est la charge utile
base64 elle-meme (nom malheureux mais coherent : `data` = "les parametres",
un de ses champs s'appelle aussi `data` pour le payload).

```
→ {"cmd":"config.get","id":1}
← {"id":1,"ok":true,"data":{"wifi":{"apSsid":"nidmi-player","apPass":"********","mdnsHost":"nidmiplayer"},"rtp":{"sessionName":"nidmi-player"},"osc":{"localPort":4000,"destIp":"192.168.4.255","destPort":9000}}}

→ {"cmd":"config.set","id":2,"data":{"wifi":{"apSsid":"studio-player"}}}
← {"id":2,"ok":true,"data":{"needsReboot":true}}

→ {"cmd":"config.reboot","id":3}
← {"id":3,"ok":true}   (puis redemarrage — ESP.restart())

→ {"cmd":"player.play","id":4}
← {"id":4,"ok":true}
→ {"cmd":"player.stop","id":5}          → {"cmd":"player.pause","id":5}
→ {"cmd":"player.toggle","id":5}        → {"cmd":"player.loop","id":5,"data":{"on":true}}

→ {"cmd":"player.info","id":6}
← {"id":6,"ok":true,"data":{"state":"PLAYING","bpm":120,"progress":0.42,"file":"/demo.mid","events":16,"loop":false,"durationS":4}}

→ {"cmd":"file.begin","id":7,"data":{"path":"/song.mid","size":12345}}
← {"id":7,"ok":true}
→ {"cmd":"file.chunk","id":8,"data":{"offset":0,"data":"<base64, 512o bruts max par chunk>"}}
← {"id":8,"ok":true,"data":{"written":512}}
   ... (repete, offset = octets deja ecrits, jusqu'a couvrir size)
→ {"cmd":"file.end","id":9}
← {"id":9,"ok":true,"data":{"path":"/song.mid","size":12345}}
→ {"cmd":"file.abort","id":9}           (annule un upload en cours, ferme+supprime le fichier partiel)

→ {"cmd":"player.load","id":10,"data":{"path":"/song.mid"}}
← {"id":10,"ok":true}

→ {"cmd":"file.list","id":11}
← {"id":11,"ok":true,"data":{"files":[{"path":"/demo.mid","size":113},{"path":"/song.mid","size":12345}]}}

→ {"cmd":"file.delete","id":12,"data":{"path":"/demo.mid"}}
← {"id":12,"ok":true}
```

Erreurs : `{"id":N,"ok":false,"error":"message"}`.

Teste de bout en bout sur XIAO ESP32-S3 : `config.get/set/reboot` (persistance
verifiee apres redemarrage reel), `player.play/stop/info` (coherent avec l'etat
observe par les commandes texte existantes), upload complet d'un `.mid` de
113 o suivi de `file.list`/`player.load`/`player.play`/`file.delete`, et
non-regression des commandes texte (`h`, raccourcis clavier).

## 8. Decisions de conception (implementation)

- **Upload binaire en base64 par chunks JSON**, pas de sous-protocole binaire
  brut : reutilise `SerialLineReader` sans state machine separee. Overhead
  ~33% negligeable pour des `.mid` (quelques Ko a ~100 Ko). Chunk brut max
  = 512 o (~684 caracteres base64) ; `SerialLineReader::kBufSize` = 1024
  (etait 200) pour absorber une ligne complete. Base64 decode via
  `libb64/cdecode.h`, deja present dans le core Arduino-ESP32 (aucune
  dependance supplementaire).
- **Un seul upload actif a la fois** (etat simple dans `JsonCommandApi` : `File`,
  chemin, taille attendue, octets ecrits). `file.chunk` rejette un `offset` qui
  ne correspond pas exactement au nombre d'octets deja recus (pas de fenetre
  glissante). `file.abort` permet de debloquer un upload interrompu (ex. client
  deconnecte en plein transfert) sans redemarrer la carte.
- **Persistance reseau : `/config.json` sur LittleFS** (pas NVS) — coherent
  avec l'usage deja fait de LittleFS pour les `.mid`, lisible/editable
  directement. Valeurs par defaut = anciennes constantes de `main.cpp` si le
  fichier est absent ou invalide (aucun echec bloquant). `main.cpp` monte
  LittleFS et charge `/config.json` **avant** `netBeginSoftAp`/RTP-MIDI (ordre
  important : la config doit etre lue avant que le WiFi demarre).
- **Application de la config reseau : apres reboot explicite**, jamais a chaud.
  `config.set` persiste et repond `needsReboot:true` ; le client declenche
  `config.reboot` quand il le souhaite. Reconfigurer WiFi/mDNS/RTP-MIDI a
  chaud sans redemarrage serait un chantier plus risque (AppleMIDI/mDNS ne se
  re-initialisent pas proprement) pour un gain marginal.
- **Mot de passe WiFi masque dans `config.get`** (`"apPass":"********"`, valeur
  fixe independante de la longueur reelle), en clair uniquement en entree de
  `config.set`.
- **Securite upload** : chemins limites a `.mid`/`.midi`, doivent commencer
  par `/`, rejet de `..` — meme garde-fou que `listMidiFiles()`/
  `findFirstMidi()`, empeche d'ecraser `/config.json` ou d'ecrire hors de
  l'usage prevu.
- **`seq.*` en JSON** : non fait, le texte `seq ...` existant suffit pour
  l'instant. Ajoutable plus tard sur le meme patron que `player.*`.

## 9. Hors scope (differe)

- Le transport WiFi reel (HTTP/WebSocket) — `JsonCommandApi` est concu pour
  etre reutilise tel quel, mais aucun serveur n'est construit.
- Page Web Serial cote hote — testable avec des scripts Python (`dsrdtr=True`,
  voir memoire de session sur les pieges serie du XIAO), une vraie page HTML
  est un suivi naturel.
- Reconfiguration WiFi a chaud sans reboot.
