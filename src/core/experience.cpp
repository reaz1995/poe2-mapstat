// Experience tables (character levels as in Path of Exile 1, monster experience from poe2db) and the level
// difference penalty.
#include "experience.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace xp {

static const uint64_t kTotal[100] = {
    0,          525,        1760,       3781,       7184,       12186,      19324,      29377,      43181,      61693,
    85990,      117506,     157384,     207736,     269997,     346462,     439268,     551295,     685171,     843709,
    1030734,    1249629,    1504995,    1800847,    2142652,    2535122,    2984677,    3496798,    4080655,    4742836,
    5490247,    6334393,    7283446,    8348398,    9541110,    10874351,   12361842,   14018289,   15859432,   17905634,
    20171471,   22679999,   25456123,   28517857,   31897771,   35621447,   39721017,   44225461,   49176560,   54607467,
    60565335,   67094245,   74247659,   82075627,   90631041,   99984974,   110197515,  121340161,  133497202,  146749362,
    161191120,  176922628,  194049893,  212684946,  232956711,  255001620,  278952403,  304972236,  333233648,  363906163,
    397194041,  433312945,  472476370,  514937180,  560961898,  610815862,  664824416,  723298169,  786612664,  855129128,
    929261318,  1009443795, 1096169525, 1189918242, 1291270350, 1400795257, 1519130326, 1646943474, 1784977296, 1934009687,
    2094900291, 2268549086, 2455921256, 2658074992, 2876116901, 3111280300, 3364828162, 3638186694, 3932818530, 4250334444,
};

// Base experience of a monster per monster level 1-99 (poe2db.tw Monster table, "Experience" base stat).
// A monster gives this times its own multiplier (e.g. 0.95). From level 80 on the base stays at 100,000.
static const int kMonsterXp[99] = {
        17,     18,     20,     24,     31,     43,     57,     75,     96,    120,
       150,    181,    219,    261,    308,    358,    416,    482,    549,    623,
       704,    796,    881,    968,   1058,   1153,   1252,   1407,   1573,   1750,
      1941,   2142,   2361,   2587,   2831,   3092,   3360,   3646,   3958,   4259,
      4586,   4940,   5279,   5652,   6045,   6466,   6909,   7390,   7894,   8439,
      9018,   9641,  10300,  10997,  11751,  12546,  13393,  14302,  15267,  16300,
     17402,  18576,  19825,  21161,  21507,  23667,  26020,  28634,  31491,  34639,
     38100,  41931,  46103,  50688,  55765,  61373,  67522,  74226,  81664, 100000,
    100000, 100000, 100000, 100000, 100000, 100000, 100000, 100000, 100000, 100000,
    100000, 100000, 100000, 100000, 100000, 100000, 100000, 100000, 100000,
};


uint64_t toNextLevel(int level) {
    if (level < 1 || level >= 100) return 0;
    return kTotal[level] - kTotal[level - 1];
}

int safeRange(int charLevel) { return 3 + std::max(charLevel, 1) / 16; }

int monsterExp(int level) { return kMonsterXp[std::clamp(level, 1, 99) - 1]; }

double penaltyMultiplier(int charLevel, int areaLevel) {
    int ed = std::max(std::abs(charLevel - areaLevel) - safeRange(charLevel), 0);
    if (ed == 0) return 1.0;
    double l = charLevel + 5.0;
    return std::pow(l / (l + std::pow((double)ed, 2.5)), 1.5);
}

int bestArea(int charLevel) {
    int best = 1;
    for (int a = 2; a <= 99; ++a)
        if (expectedExp(charLevel, a) > expectedExp(charLevel, best) * (1 + 1e-9)) best = a;
    return best;
}

}  // namespace xp
