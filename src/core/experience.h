// Character experience: level table, monster experience per level, safe range and level penalty.
#pragma once
#include <cstdint>

namespace xp {

uint64_t toNextLevel(int level);    // experience from `level` to `level + 1`, 0 at level 100

// Areas within this many levels of the character give full experience: 3 + level / 16.
int safeRange(int charLevel);

// Base experience of a monster of this level (before the monster's own multiplier).
int monsterExp(int level);

// Level difference penalty: ((L + 5) / (L + 5 + ED^2.5))^1.5, where ED is the level difference beyond the
// safe range. 1.0 inside the safe range. No 1% minimum: far outside the range the game gives no experience.
double penaltyMultiplier(int charLevel, int areaLevel);

// Experience from a monster of the area level after the penalty.
inline double expectedExp(int charLevel, int areaLevel) {
    return monsterExp(areaLevel) * penaltyMultiplier(charLevel, areaLevel);
}

// Area level (1-99) with the most experience per monster for this character; the lowest one on a tie.
int bestArea(int charLevel);

// The same compared to a monster of the character's own level (1.0 = the same, 1.2 = 20% more).
inline double expectedMultiplier(int charLevel, int areaLevel) {
    return expectedExp(charLevel, areaLevel) / monsterExp(charLevel);
}

}  // namespace xp
