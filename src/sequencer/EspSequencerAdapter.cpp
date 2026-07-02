#include "EspSequencerAdapter.h"
#include <Arduino.h>

void EspSequencerAdapter::printPattern() const {
    const auto& p = engine_.pattern();
    Serial.printf("[seq] Pattern %u/%u  %u pas  %.1f BPM  delay=%lld us\n",
        p.numerator, p.denominator, p.numSteps, engine_.bpm(), engine_.stepDelayUs());
    Serial.printf("  harm=%s scale=%u root=%u follow=%s progLen=%u\n",
        p.harmony.harmonyEnabled ? "on" : "off",
        p.harmony.scaleId, p.harmony.rootPc,
        p.harmony.followProgression ? "yes" : "no",
        p.progression.len);
    Serial.printf("  accent=%u swing=%s %u%%\n",
        p.timing.accentAmount,
        p.timing.swingEnabled ? "on" : "off",
        p.timing.swingPositionPercent);

    for (uint8_t b = 0; b < p.numBars; ++b) {
        if (p.numBars > 1)
            Serial.printf("  -- mesure %u --\n", b);
        for (uint8_t r = 0; r < p.numRows; ++r) {
            const auto& row = p.rows[r];
            Serial.printf("  R%u ch%u : ", r, row.channel + 1);
            for (uint8_t s = 0; s < row.numSteps; ++s) {
                const auto& sd = row.step(b, s);
                if (!sd.enabled)
                    Serial.print('.');
                else if (sd.subPatIdx != kNoSubPattern)
                    Serial.print('S');
                else
                    Serial.print('X');
            }
            Serial.printf("  [%u]\n", engine_.currentStep());
        }
    }

    for (uint8_t i = 0; i < kMaxSubPatterns; ++i) {
        const auto& sp = p.subPatterns[i];
        if (sp.numSteps == 0) continue;
        Serial.printf("  sub[%u] %u/%u : ", i, sp.numSteps, sp.duration);
        for (uint8_t s = 0; s < sp.numSteps; ++s)
            Serial.print(sp.steps[s].enabled ? 'x' : '.');
        Serial.println();
    }
}
