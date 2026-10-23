#pragma once
// Match equity table (MET): the chance of winning the GAME from the start of a hand,
// given both chip pools and who is jockey. Built by tools/sbequity.cpp from self-play
// hands (see REPORT.md it writes). Pools only go down in Spectacular Bid (chips you win
// go to a trophy pile that is never spent), so the table covers pools 1..30 per side.

#include <string>

namespace sb {

constexpr int kMetPool = 30;

// p[my pool][opponent pool][jockey]: jockey 0 = me, 1 = opponent, 2 = nobody (forced lead).
struct MetTable {
    float p[kMetPool + 1][kMetPool + 1][3];
};

// The table the engine uses (built-in, or loaded from met.txt by load_models_from).
const MetTable*& met_active();
const MetTable* met_builtin();  // null if none compiled in

// P(I win the game) at the start of a hand. Pools of 0 or less mean that side has lost.
double met_win(int my_pool, int opp_pool, int jockey_rel);

bool met_load(MetTable& t, const std::string& path);
bool met_save(const MetTable& t, const std::string& path, const std::string& header);

}  // namespace sb
