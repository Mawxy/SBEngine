// How much should you bid? Samples random hands at the start of a bidding hand and
// asks the engine what the lead is worth, from both seats' situations.
//
//   bidstudy <hands> [dets=24] [nodes=8000] [seed=1] > bids.csv
//
// Columns: hand, doubles, pips, ten_leads (5-5 or 6-4 in hand), five_leads (tiles worth
// 5 or 10 on the lead), lead_value, walk_away_opener (you lost the last hand),
// walk_away_jockey (you dominoed: 3-chip credit).

#include <cstdio>
#include <cstdlib>
#include <numeric>

#include "../src/search.h"

using namespace sb;

int main(int argc, char** argv) {
    const int hands = argc > 1 ? std::atoi(argv[1]) : 200;
    SearchParams p;
    p.determinizations = argc > 2 ? std::atoi(argv[2]) : 24;
    p.nodes_per_det = argc > 3 ? std::atoll(argv[3]) : 8000;
    std::mt19937_64 rng(argc > 4 ? std::strtoull(argv[4], nullptr, 10) : 1);
    std::printf("hand,doubles,pips,ten_leads,five_leads,lead_value,walk_away_opener,walk_away_jockey\n");
    for (int h = 0; h < hands; ++h) {
        std::array<int8_t, kNumTiles> order;
        std::iota(order.begin(), order.end(), int8_t{0});
        portable_shuffle(order.data(), kNumTiles, rng);
        TileMask mine = 0;
        for (int i = 0; i < 7; ++i) mine |= tile_bit(order[i]);

        int doubles = 0, ten = 0, five = 0;
        std::string codes;
        for (TileMask m = mine; m; m &= m - 1) {
            const int t = first_tile(m);
            doubles += is_double(t);
            const int lead = is_double(t) ? 2 * tile_hi(t) : tile_pips(t);
            if (lead == 10) ++ten;
            if (lead % 5 == 0 && lead > 0) ++five;
            codes += (codes.empty() ? "" : " ") + std::to_string(tile_hi(t)) + std::to_string(tile_lo(t));
        }

        double lead_value = 0;
        int walk[2] = {0, 0};
        for (int as_jockey = 0; as_jockey < 2; ++as_jockey) {
            View v(0);
            v.new_game();
            v.set_jockey(as_jockey ? 0 : 1);
            v.start_hand(mine, -1);
            p.seed = rng();
            const BidEval b = analyze_bid(v, p);
            walk[as_jockey] = b.walk_away;
            if (!as_jockey) lead_value = b.lead_value;
        }
        std::printf("%s,%d,%d,%d,%d,%.2f,%d,%d\n", codes.c_str(), doubles, pip_total(mine), ten, five, lead_value, walk[0], walk[1]);
        std::fflush(stdout);
    }
}
