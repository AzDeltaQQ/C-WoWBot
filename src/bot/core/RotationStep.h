#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct RotationStep {
    uint32_t spellId = 0;
    std::string spellName;
    bool triggersGCD = true;

    bool requiresTarget = true;
    float castRange = 30.0f;
    float minPlayerHealthPercent = 0.0f;
    float maxPlayerHealthPercent = 100.0f;
    float minTargetHealthPercent = 0.0f;
    float maxTargetHealthPercent = 100.0f;
    float minPlayerManaPercent = 0.0f;
    float maxPlayerManaPercent = 100.0f;

    RotationStep() = default;

    RotationStep(uint32_t id, std::string name, bool gcd, 
                 bool reqTarget = true, float range = 30.0f, 
                 float minPlayerHp = 0.0f, float maxPlayerHp = 100.0f,
                 float minTargetHp = 0.0f, float maxTargetHp = 100.0f,
                 float minPlayerMp = 0.0f, float maxPlayerMp = 100.0f)
        : spellId(id), 
          spellName(std::move(name)), 
          triggersGCD(gcd),
          requiresTarget(reqTarget),
          castRange(range),
          minPlayerHealthPercent(minPlayerHp),
          maxPlayerHealthPercent(maxPlayerHp),
          minTargetHealthPercent(minTargetHp),
          maxTargetHealthPercent(maxTargetHp),
          minPlayerManaPercent(minPlayerMp),
          maxPlayerManaPercent(maxPlayerMp) {}
};