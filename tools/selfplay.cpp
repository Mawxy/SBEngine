// Self-play harness.
//
//   selfplay duel [pairs] [dets] [nodes] [seed]
//     Engine vs a greedy baseline on mirrored single hands (each deal is played
//     twice with seats swapped). Reports the engine's average net chips per hand.
//     Also verifies that the engine's view stays in sync with the referee and that
//     inference never rules out a tile the opponent actually holds.
//
//   selfplay ab <net.bin|builtin> [pairs] [dets] [nodes] [seed]
//     Engine with the NNUE network vs the same engine with the classic evaluation,
//     same search budget, mirrored hands. Reports the network side's chips per hand.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <string>

#include "../src/search.h"

using namespace sb;

// Baseline: the most points now, then the heaviest tile. Uses only its own hand
// and the layout.
static Action greedy(const Game& g) {
    std::vector<Action> acts = g.legal_actions();
    Action best = acts[0];
    int best_key = -1;
    for (const Action& a : acts) {
        if (a.kind != Action::Play) return a;
        Layout l = g.layout();
        l.play(a.tile, a.target);
        const int c = l.count();
        const int key = ((c > 0 && c % 5 == 0) ? c : 0) * 100 + tile_pips(a.tile);
        if (key > best_key) {
            best_key = key;
            best = a;
        }
    }
    return best;
}

struct Checks {
    long long sync_errors = 0;
    long long inference_errors = 0;
    long long decisions = 0;
};

// Plays one hand from `order`. Engine sits in seat `eng`; the other seat uses `other`
// (nullptr = greedy baseline). Returns engine net chips.
static int play_hand(const std::array<int8_t, kNumTiles>& order, int eng, const SearchParams& sp, Checks& ck,
                     const SearchParams* other = nullptr) {
    Game g;
    g.set_pools(30, 30);
    g.deal(order);
    View view[2] = {View(0), View(1)};
    for (int q = 0; q < 2; ++q) {
        view[q].new_game();
        view[q].start_hand(g.hand(q), g.to_move());
    }
    SearchParams p = sp;
    while (g.phase() == Phase::Play) {
        const int who = g.to_move();
        Action a;
        if (who == eng) {
            ++ck.decisions;
            p.seed = sp.seed + static_cast<uint64_t>(ck.decisions) * 7919;
            a = choose_action(view[who], p);
        } else if (other) {
            SearchParams q = *other;
            q.seed = sp.seed + static_cast<uint64_t>(++ck.decisions) * 104729;
            a = choose_action(view[who], q);
        } else {
            a = greedy(g);
        }
        g.apply(a);
        for (int q = 0; q < 2; ++q) view[q].observe(who, a, (q == who && a.kind == Action::Draw) ? g.last_drawn() : -1);

        if (g.phase() == Phase::Play) {
            const View& v = view[eng];
            if (v.pool(0) != g.pool(0) || v.pool(1) != g.pool(1) || v.layout().count() != g.layout().count() ||
                v.my_hand() != g.hand(eng) || v.opp_count() != tile_count(g.hand(1 - eng)) ||
                v.boneyard_size() != g.boneyard_size())
                ++ck.sync_errors;
            if (v.opp_excluded() & g.hand(1 - eng)) ++ck.inference_errors;
        }
    }
    return (30 - g.pool(1 - eng)) - (30 - g.pool(eng));
}

int main(int argc, char** argv) {
    const std::string mode = argc > 1 ? argv[1] : "duel";
    if (mode == "ab") {
        if (argc < 3) { std::printf("usage: selfplay ab <net.bin|builtin> [pairs] [dets] [nodes] [seed]\n"); return 1; }
        static NNWeights net;
        const NNWeights* netp = nn_builtin();
        if (std::string(argv[2]) != "builtin") {
            if (!nn_load(net, argv[2])) { std::printf("cannot read %s\n", argv[2]); return 1; }
            netp = &net;
        }
        if (!netp) { std::printf("no network\n"); return 1; }
        const int pairs = argc > 3 ? std::atoi(argv[3]) : 200;
        SearchParams a, b;
        a.determinizations = b.determinizations = argc > 4 ? std::atoi(argv[4]) : 32;
        a.nodes_per_det = b.nodes_per_det = argc > 5 ? std::atoll(argv[5]) : 20000;
        a.seed = b.seed = argc > 6 ? std::strtoull(argv[6], nullptr, 10) : 1;
        a.eval.nn = netp;
        b.eval.nn = nullptr;
        std::mt19937_64 rng(a.seed * 1000003);
        Checks ck;
        double sum = 0, sumsq = 0;
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < pairs; ++i) {
            std::array<int8_t, kNumTiles> order;
            std::iota(order.begin(), order.end(), int8_t{0});
            portable_shuffle(order.data(), kNumTiles, rng);
            const double pair = (play_hand(order, 0, a, ck, &b) + play_hand(order, 1, a, ck, &b)) / 2.0;
            sum += pair;
            sumsq += pair * pair;
            if ((i + 1) % 50 == 0) std::printf("  %d pairs: %+.3f\n", i + 1, sum / (i + 1));
        }
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        const double mean = sum / pairs, se = std::sqrt(std::max(0.0, sumsq / pairs - mean * mean) / pairs);
        std::printf("NNUE vs classic eval: %+.3f chips/hand for NNUE (se %.3f) over %d mirrored pairs, %.0f s\n", mean, se, pairs, secs);
        return 0;
    }
    if (mode != "duel") {
        std::printf("usage: selfplay duel [pairs] [dets] [nodes_per_det] [seed]\n");
        return 1;
    }
    const int pairs = argc > 2 ? std::atoi(argv[2]) : 200;
    SearchParams sp;
    sp.determinizations = argc > 3 ? std::atoi(argv[3]) : 32;
    sp.nodes_per_det = argc > 4 ? std::atoll(argv[4]) : 20000;
    sp.seed = argc > 5 ? std::strtoull(argv[5], nullptr, 10) : 1;

    std::mt19937_64 rng(sp.seed * 1000003);
    Checks ck;
    double sum = 0, sumsq = 0;
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < pairs; ++i) {
        std::array<int8_t, kNumTiles> order;
        std::iota(order.begin(), order.end(), int8_t{0});
        std::shuffle(order.begin(), order.end(), rng);
        const int pair = play_hand(order, 0, sp, ck) + play_hand(order, 1, sp, ck);
        sum += pair / 2.0;
        sumsq += (pair / 2.0) * (pair / 2.0);
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const double mean = sum / pairs;
    const double se = std::sqrt(std::max(0.0, sumsq / pairs - mean * mean) / pairs);
    std::printf("engine vs greedy: %+.3f chips/hand (se %.3f) over %d mirrored pairs\n", mean, se, pairs);
    std::printf("decisions %lld, %.1f ms/decision, sync errors %lld, inference errors %lld\n", ck.decisions,
                1000.0 * secs / std::max(1LL, ck.decisions), ck.sync_errors, ck.inference_errors);
    return (ck.sync_errors || ck.inference_errors) ? 2 : 0;
}
