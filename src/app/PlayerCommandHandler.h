#pragma once

#include <Arduino.h>

class MidiPlayer;

void playerCommandLine(MidiPlayer& player, String& currentFile, char* line);
