// Training data for the NNUE evaluation.
//
// Plays hands with the engine itself (small PIMC search, plus some random moves for
// variety). At every play-phase position it records, from the side to move's view:
//   - the network inputs (nn_features)
//   - a search label: perfect-information alpha-beta value of this exact deal
//     (what the evaluation stands in for at the search horizon), in chips
//   - the outcome: net chips that side actually won from here to the end of the hand
// Half the hands start with a free lead (as after bidding), half with the forced lead.
//
//   gendata <out.bin> <hands> [seed=1] [label_nodes=20000] [threads=all] [net.bin]
//
// With net.bin the labels and the playing engine use that network (the next generation).
// Record format: uint8 n, uint8 idx[n], float search, float outcome.

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <numeric>
#include <thread>
#include <vector>

#include "../src/search.h"

using namespace sb;

namespace {

struct Rec {
    std::vector<uint8_t> idx;
    float search;
    int mover;
    int pool_me, pool_opp;
};

std::mutex g_out_mu;
std::atomic<long long> g_positions{0}, g_hands{0};

void play_hands(FILE* out, int hands, uint64_t seed, long long label_nodes, const NNWeights* net) {
    std::mt19937_64 rng(seed);
    SearchParams label;
    label.determinizations = 1;
    label.nodes_per_det = label_nodes;
    label.eval.nn = net;
    Searcher labeler(label, 19);

    SearchParams policy;
    policy.determinizations = 8;
    policy.nodes_per_det = 1000;
    policy.eval.nn = net;

    std::vector<uint8_t> buf;
    std::vector<int> vals;
    for (int h = 0; h < hands; ++h) {
        std::array<int8_t, kNumTiles> order;
        std::iota(order.begin(), order.end(), int8_t{0});
        portable_shuffle(order.data(), kNumTiles, rng);

        // Half the hands: free lead after a bid of 0 (jockey leads). Otherwise forced lead.
        const bool free_lead = rng() % 2;
        const int jockey = static_cast<int>(rng() % 2);
        Game g;
        g.set_pools(30, 30);
        if (free_lead) g.set_jockey(jockey);
        g.deal(order);
        View v[2] = {View(0), View(1)};
        for (int q = 0; q < 2; ++q) {
            v[q].new_game();
            if (free_lead) v[q].set_jockey(jockey);
            v[q].start_hand(g.hand(q), g.to_move());
        }
        if (free_lead) {
            const int opener = g.to_move();
            g.apply(Action::make_bid(0));
            for (auto& x : v) x.observe(opener, Action::make_bid(0));
        }

        std::vector<Rec> recs;
        while (g.phase() == Phase::Play) {
            const int p = g.to_move();
            const std::vector<Action> acts = g.legal_actions();

            Rec r;
            int idx[kNNMaxActive];
            const int n = nn_features(g, idx);
            r.idx.assign(idx, idx + n);
            labeler.root_values(g, acts, vals);
            int best = vals[0];
            for (int x : vals) best = std::max(best, x);
            r.search = best / 100.0f;
            r.mover = p;
            r.pool_me = g.pool(p);
            r.pool_opp = g.pool(1 - p);
            recs.push_back(std::move(r));

            Action a;
            if (acts.size() == 1) a = acts[0];
            else if (rng() % 100 < 15) a = acts[rng() % acts.size()];
            else {
                policy.seed = rng();
                a = choose_action(v[p], policy);
            }
            g.apply(a);
            for (int q = 0; q < 2; ++q) v[q].observe(p, a, (q == p && a.kind == Action::Draw) ? g.last_drawn() : -1);
        }

        buf.clear();
        for (const Rec& r : recs) {
            const float outcome = static_cast<float>((r.pool_opp - g.pool(1 - r.mover)) - (r.pool_me - g.pool(r.mover)));
            buf.push_back(static_cast<uint8_t>(r.idx.size()));
            buf.insert(buf.end(), r.idx.begin(), r.idx.end());
            const float f[2] = {r.search, outcome};
            const auto* b = reinterpret_cast<const uint8_t*>(f);
            buf.insert(buf.end(), b, b + sizeof f);
        }
        {
            std::lock_guard<std::mutex> lock(g_out_mu);
            std::fwrite(buf.data(), 1, buf.size(), out);
        }
        g_positions += static_cast<long long>(recs.size());
        const long long done = ++g_hands;
        if (done % 500 == 0) {
            std::fprintf(stderr, "%lld hands, %lld positions\n", done, g_positions.load());
            std::fflush(out);
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: gendata <out.bin> <hands> [seed] [label_nodes] [threads] [net.bin]\n");
        return 1;
    }
    const int hands = std::atoi(argv[2]);
    const uint64_t seed = argc > 3 ? std::strtoull(argv[3], nullptr, 10) : 1;
    const long long label_nodes = argc > 4 ? std::atoll(argv[4]) : 20000;
    int threads = argc > 5 ? std::atoi(argv[5]) : 0;
    if (threads <= 0) threads = std::max(1u, std::thread::hardware_concurrency());
    static NNWeights net;
    const NNWeights* netp = nullptr;
    if (argc > 6) {
        if (!nn_load(net, argv[6])) { std::fprintf(stderr, "cannot read %s\n", argv[6]); return 1; }
        netp = &net;
    }
    FILE* out = std::fopen(argv[1], "ab");
    if (!out) { std::fprintf(stderr, "cannot write %s\n", argv[1]); return 1; }
    std::vector<std::thread> ts;
    for (int t = 0; t < threads; ++t)
        ts.emplace_back(play_hands, out, hands / threads + (t < hands % threads ? 1 : 0), seed * 7919 + t, label_nodes, netp);
    for (auto& t : ts) t.join();
    std::fclose(out);
    std::fprintf(stderr, "done: %lld hands, %lld positions\n", g_hands.load(), g_positions.load());
}
