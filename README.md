# nidmi-player

Firmware **PlatformIO** pour le materiel **player**. Il ne depend **pas** du depot **NiDMI** : **`nidmi-core`** + dependances declarees dans `platformio.ini`.

## Objectif

- Lire des fichiers **Standard MIDI File** (`.mid`, format 0 et 1) stockes en flash (LittleFS).
- **Step sequencer XOX** (style TR-808) : patterns multi-rangees, note + velocity + gate par pas.
- Emettre les evenements MIDI en temps reel via **RTP-MIDI** (WiFi AP).
- Envoyer **MIDI Clock** (24 ppqn) + messages de transport (**Start / Stop / Continue**).
- Exposer **OSC UDP** (`OscUdpService`) et le **routage RTP-MIDI <-> OSC** (`MidiOscRouter`, prefixe `/nidmi`).
- Controle via **moniteur serie** : play, stop, pause, boucle.

## Prerequis

- [PlatformIO](https://platformio.org/)
- `nidmi-core` au meme niveau : `../nidmi-core`

## WiFi de test

- SSID : `nidmi-player`
- Mot de passe : `nidmipass`
- mDNS : `nidmiplayer.local`
- IP AP : `192.168.4.1`

Connecter le Mac ou l'iPad au WiFi du player, puis ajouter la session RTP-MIDI (nom affiche : `nidmi-player`).

## Charger des fichiers MIDI

1. Placer les fichiers `.mid` dans le dossier `data/` du projet.
2. Uploader le systeme de fichiers :

```bash
pio run -t uploadfs
```

Au demarrage, le player liste les fichiers trouves et charge le premier `.mid` automatiquement.

## Compilation & flash

```bash
cd nidmi-player
pio run -e esp32-s3-devkitc-1
pio run -t upload
pio device monitor
```

Adapter l'environnement (`board`) dans `platformio.ini` selon la carte.

## Commandes serie (115200 baud)

### Lecteur MIDI

| Touche   | Action                |
|----------|-----------------------|
| `p`      | Play / reprendre      |
| `s`      | Stop                  |
| `ESPACE` | Toggle play / pause   |
| `l`      | Toggle boucle         |
| `i`      | Info (etat, BPM, ...) |
| `f`      | Lister fichiers .mid  |

### Sequenceur XOX

| Touche | Action                               |
|--------|--------------------------------------|
| `1`    | Play / pause sequenceur              |
| `2`    | Stop sequenceur                      |
| `3`    | Toggle pas courant sur rangee 0      |
| `t`    | Afficher pattern (grille texte)      |

### General

| Touche | Action |
|--------|--------|
| `h`    | Aide   |

## Step sequencer

Le sequenceur divise une **mesure signee** (ex. 4/4, 3/4, 7/8) en N pas de duree egale.

```
stepDelay = measureDuration / numSteps

measureDuration = numerator * (4 / denominator) * usPerQuarter
usPerQuarter    = 60 000 000 / BPM
```

Chaque pas contient : **note**, **velocity**, **gate** (% de la duree du pas), **enabled**.
Un pattern a jusqu'a 16 rangees (une par son/note) et 64 pas.

Des **sous-patterns** (nombre de sous-pas et duree en pas principaux) peuvent etre attaches a un pas. **Un seul sous-pattern est joue a la fois** : un nouveau demarrage remplace le precedent ; si plusieurs rangees declenchent un sous-pattern sur le meme pas, seule la premiere rangee (ordre croissant) lance le sous-pattern.

Le sequenceur peut tourner **seul** ou **superpose** a la lecture d'un fichier `.mid`. Les deux partagent la meme sortie `RtpMidiService`.

Un pattern demo (kick/snare/hihat/clap sur canal 10) est charge au demarrage.

## Architecture

```
src/
  main.cpp              Point d'entree, WiFi AP, RTP-MIDI, OSC, commandes serie
  MidiEvent.h           Structures : MidiEvent, TempoChange
  SmfParser.h/cpp       Parser Standard MIDI File (MThd/MTrk, VLQ, running status, meta tempo)
  MidiPlayer.h/cpp      Lecteur .mid non-bloquant (transport, MIDI Clock 24 ppqn)
  StepData.h            Structures : Pattern, PatternRow, StepData, ActiveNote
  StepSequencer.h/cpp   Sequenceur XOX non-bloquant (timing par delay, gate note-off)
data/
  *.mid                 Fichiers MIDI a uploader via LittleFS
```

### Flux de donnees

```
Fichier .mid (LittleFS)          Pattern (StepSequencer)
    |                                |
    v                                v
SmfParser::parse()          StepSequencer::update()
    |                          delay entre chaque pas
    v                                |
MidiPlayer::update()                 |
    |                                |
    +---------- RtpMidiService ------+---->  WiFi (RTP-MIDI)
                    +
               OscUdpService  ---->  WiFi (OSC broadcast)
```

## Dependances

| Paquet | Role |
|--------|------|
| `../nidmi-core` | RTP-MIDI, OSC UDP, pont MIDI<->OSC, reseau (`netBeginSoftAp`) |
| `fortyseveneffects/MIDI Library` | Requis par AppleMIDI |
| `AppleMIDI` | RTP-MIDI |
| `cnmat/OSC` | OSC UDP (script `extra_scripts` exclut le SLIP Bluetooth sur ESP32-S3) |

## Formats supportes

- **SMF Format 0** : piste unique, tous les canaux melanges.
- **SMF Format 1** : pistes multiples fusionnees et triees par tick.
- **Division** : ticks par noire (SMPTE non supporte).
- **Meta events** : tempo (0x51) gere, les autres sont ignores.
- **SysEx** : ignore (skip).

## NiDMI

Le depot **NiDMI** (capteurs, serveur web) n'est pas une dependance de ce projet.
