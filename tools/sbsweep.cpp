// sbsweep: tests settings in full self-play games, scored by games won.
//
//   sbsweep [--param jockey_value] [--values 0,0.5,1,1.5,2,2.5,3,4] [--baseline 0]
//           [--pairs 1000] [--hours 0] [--threads N] [--dets 12] [--nodes 4000]
//           [--models DIR] [--bidding calibrated|learned] [--dir sweep] [--seed 1]
//
// For every value, the engine with that value plays the same engine with the baseline
// value. Each pair is two full games on the same deal sequence with seats swapped, and
// every value plays the same deal sequences, so values are compared on equal luck.
// Including the baseline in --values gives a control that should land near 50%.
//
// --param      jockey_value (chips credited for going out), win_value, or bid_calibration
// --bidding    calibrated (default): bids come from the search, so jockey_value affects them.
//              learned: the trained bidding model, which ignores jockey_value in bidding.
// --models     folder with eval.bin / bid.bin / info.bin; omit for the untrained engine
// --hours      stop after this long (0 = run until every value has --pairs pairs)
//
// Results: <dir>/results.csv (one line per pair) and <dir>/REPORT.md (rewritten as it goes).
// Stop any time; run the same command again to resume.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "../src/search.h"

namespace fs = std::filesystem;
using namespace sb;
using Clock = std::chrono::steady_clock;

namespace {

struct GameResult {
    int winner = -1;  // seat
    int margin = 0;   // seat 0 pool minus seat 1 pool at the end
    int hands = 0;
};

// One full game to 0 chips, bidding included.
GameResult play_game(uint64_t deal_seed, const SearchParams* players, std::mt19937_64& rng) {
    std::mt19937_64 deal_rng(deal_seed);
    Game g;
    g.new_game(deal_rng);
    View v[2] = {View(0), View(1)};
    for (int q = 0; q < 2; ++q) {
        v[q].new_game();
        v[q].start_hand(g.hand(q), g.to_move());
    }
    int hands = 1, guard = 0;
    while (g.phase() != Phase::GameOver && ++guard < 20000) {
        if (g.phase() == Phase::HandOver) {
            g.next_hand(deal_rng);
            ++hands;
            for (int q = 0; q < 2; ++q) {
                v[q].set_jockey(g.jockey());
                v[q].set_pools(g.pool(0), g.pool(1));
                v[q].start_hand(g.hand(q), g.to_move());
            }
            continue;
        }
        const int w = g.to_move();
        SearchParams q = players[w];
        q.seed = rng();
        const Action a = choose_action(v[w], q);
        g.apply(a);
        for (int k = 0; k < 2; ++k) v[k].observe(w, a, (k == w && a.kind == Action::Draw) ? g.last_drawn() : -1);
        if (g.phase() == Phase::HandOver || g.phase() == Phase::GameOver)
            for (int k = 0; k < 2; ++k) {
                if (v[k].phase() == Phase::Play || v[k].phase() == Phase::Bidding) v[k].end_hand(g.last_hand_end(), g.jockey());
                v[k].set_jockey(g.jockey());
                v[k].set_pools(g.pool(0), g.pool(1));
            }
    }
    return {g.winner(), g.pool(0) - g.pool(1), hands};
}

struct Row {
    int value_idx = 0;
    int pair = 0;
    int cand_wins = 0;     // 0, 1 or 2
    int cand_margin = 0;   // candidate's pool minus baseline's, summed over both games
    int hands = 0;
};

struct Stats {
    int pairs = 0, wins = 0, hands = 0;
    double margin = 0, pair_score_sq = 0;
};

std::string fmt(const char* f, double x) {
    char b[64];
    std::snprintf(b, sizeof b, f, x);
    return b;
}

void set_param(SearchParams& p, const std::string& name, double x) {
    if (name == "jockey_value") p.jockey_value = x;
    else if (name == "win_value") p.win_value = x;
    else if (name == "bid_calibration") p.bid_calibration = x;
    else {
        std::fprintf(stderr, "unknown --param %s (use jockey_value, win_value or bid_calibration)\n", name.c_str());
        std::exit(1);
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::map<std::string, std::string> a;
    for (int i = 1; i + 1 < argc; i += 2) {
        std::string k = argv[i];
        if (k.rfind("--", 0) == 0) k = k.substr(2);
        a[k] = argv[i + 1];
    }
    const auto get = [&](const char* k, const std::string& d) { return a.count(k) ? a[k] : d; };
    const std::string param = get("param", "jockey_value");
    std::vector<double> values;
    {
        std::stringstream ss(get("values", "0,0.5,1,1.5,2,2.5,3,4"));
        for (std::string t; std::getline(ss, t, ',');)
            if (!t.empty()) values.push_back(std::atof(t.c_str()));
    }
    const double baseline = std::atof(get("baseline", "0").c_str());
    const int pairs = std::atoi(get("pairs", "1000").c_str());
    const double hours = std::atof(get("hours", "0").c_str());
    const int hw = static_cast<int>(std::thread::hardware_concurrency());
    const int threads = std::max(1, std::atoi(get("threads", std::to_string(std::max(1, hw - 1))).c_str()));
    const int dets = std::atoi(get("dets", "12").c_str());
    const long long nodes = std::atoll(get("nodes", "4000").c_str());
    const std::string models_dir = get("models", "");
    const std::string bidding = get("bidding", "calibrated");
    const std::string dir = get("dir", "sweep");
    const uint64_t seed = std::strtoull(get("seed", "1").c_str(), nullptr, 10);
    if (values.empty() || pairs < 1) {
        std::fprintf(stderr, "nothing to run\n");
        return 1;
    }

    const std::string models_line = models_dir.empty() ? "none (untrained)" : load_models_from(models_dir);
    fs::create_directories(dir);
    const std::string csv = dir + "/results.csv";

    // Resume: skip pairs already recorded with the same settings.
    std::vector<Row> rows;
    std::set<std::pair<int, int>> done;
    const std::string settings = param + " baseline " + fmt("%g", baseline) + ", " + std::to_string(dets) + "x" +
                                 std::to_string(nodes) + ", bidding " + bidding + ", models " + models_line + ", seed " +
                                 std::to_string(seed);
    {
        std::ifstream in(csv);
        std::string line;
        if (std::getline(in, line) && line != "# " + settings) {
            if (!line.empty()) {
                std::fprintf(stderr, "%s holds results from different settings:\n  %s\nUse another --dir.\n", csv.c_str(), line.c_str());
                return 1;
            }
        }
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#' || line[0] == 'v') continue;
            double val;
            Row r;
            if (std::sscanf(line.c_str(), "%lf,%d,%d,%d,%d", &val, &r.pair, &r.cand_wins, &r.cand_margin, &r.hands) != 5) continue;
            r.value_idx = -1;
            for (size_t i = 0; i < values.size(); ++i)
                if (std::fabs(values[i] - val) < 1e-9) r.value_idx = static_cast<int>(i);
            if (r.value_idx < 0 || !done.insert({r.value_idx, r.pair}).second) continue;
            rows.push_back(r);
        }
    }
    {
        std::ifstream test(csv);
        if (!test.good()) {
            std::ofstream out(csv);
            out << "# " << settings << "\nvalue,pair,cand_wins,cand_margin,hands\n";
        }
    }

    std::mutex mu;
    std::ofstream out(csv, std::ios::app);
    const auto t0 = Clock::now();
    const auto deadline = hours > 0 ? t0 + std::chrono::seconds(static_cast<long long>(hours * 3600)) : Clock::time_point::max();
    const long long total = static_cast<long long>(values.size()) * pairs;
    std::atomic<long long> next{0};
    std::atomic<int> new_rows{0};

    const auto write_report = [&](bool final_report) {
        std::vector<Stats> st(values.size());
        for (const Row& r : rows) {
            Stats& s = st[r.value_idx];
            ++s.pairs;
            s.wins += r.cand_wins;
            s.hands += r.hands;
            s.margin += r.cand_margin;
            const double x = r.cand_wins / 2.0;
            s.pair_score_sq += x * x;
        }
        std::ostringstream o;
        o << "# Sweep: " << param << "\n\n";
        o << "Each value plays the baseline (" << param << " = " << fmt("%g", baseline) << ") in pairs of full games, same deals, seats swapped.\n\n";
        o << "Settings: " << dets << " samples x " << nodes << " nodes per decision, bidding " << bidding << ", models: " << models_line
          << ", " << threads << " threads.\n\n";
        o << "| " << param << " | games | win % | 95% range | chips per game vs baseline | hands per game |\n|---|---|---|---|---|---|\n";
        for (size_t i = 0; i < values.size(); ++i) {
            const Stats& s = st[i];
            if (s.pairs == 0) {
                o << "| " << fmt("%g", values[i]) << " | 0 | | | | |\n";
                continue;
            }
            const double mean = s.wins / (2.0 * s.pairs);
            const double var = std::max(0.0, s.pair_score_sq / s.pairs - mean * mean);
            const double se = std::sqrt(var / s.pairs);
            o << "| " << fmt("%g", values[i]) << (std::fabs(values[i] - baseline) < 1e-9 ? " (control)" : "") << " | " << 2 * s.pairs
              << " | " << fmt("%.1f", 100 * mean) << " | " << fmt("%.1f", 100 * (mean - 1.96 * se)) << " to "
              << fmt("%.1f", 100 * (mean + 1.96 * se)) << " | " << fmt("%+.2f", s.margin / (2.0 * s.pairs)) << " | "
              << fmt("%.1f", s.hands / (2.0 * s.pairs)) << " |\n";
        }
        const double el = std::chrono::duration<double>(Clock::now() - t0).count();
        o << "\nWin % is the tested value's share of games won. A 95% range entirely above 50 means it beats the baseline.\n";
        o << "\n" << rows.size() << " of " << total << " pairs done. " << (final_report ? "Finished" : "Running") << ", "
          << fmt("%.1f", el / 3600) << " h this session.\n";
        std::ofstream rep(dir + "/REPORT.md");
        rep << o.str();
        std::printf("\n%s", o.str().c_str());
        std::fflush(stdout);
    };

    std::printf("sbsweep: %s over {", param.c_str());
    for (size_t i = 0; i < values.size(); ++i) std::printf("%s%g", i ? ", " : "", values[i]);
    std::printf("} vs %g, %d pairs each, %d threads, %d x %lld, bidding %s\nmodels: %s\n%zu pairs already done. Folder: %s\n",
                baseline, pairs, threads, dets, nodes, bidding.c_str(), models_line.c_str(), rows.size(), dir.c_str());
    std::fflush(stdout);

    std::vector<std::thread> ts;
    for (int t = 0; t < threads; ++t)
        ts.emplace_back([&, t] {
            std::mt19937_64 rng(seed * 1000003 + static_cast<uint64_t>(t) * 7919 + 17);
            for (;;) {
                if (Clock::now() >= deadline) return;
                const long long k = next++;
                if (k >= total) return;
                const int vi = static_cast<int>(k % static_cast<long long>(values.size()));
                const int pair = static_cast<int>(k / static_cast<long long>(values.size()));
                {
                    std::lock_guard<std::mutex> lk(mu);
                    if (done.count({vi, pair})) continue;
                }
                SearchParams cand, base;
                for (SearchParams* p : {&cand, &base}) {
                    p->determinizations = dets;
                    p->nodes_per_det = nodes;
                    if (bidding != "learned") p->bid_model = nullptr;
                }
                set_param(cand, param, values[vi]);
                set_param(base, param, baseline);
                const uint64_t deal_seed = seed * 0x9E3779B97F4A7C15ULL + static_cast<uint64_t>(pair) * 2654435761ULL + 1;
                const SearchParams g1[2] = {cand, base}, g2[2] = {base, cand};
                const GameResult r1 = play_game(deal_seed, g1, rng);
                const GameResult r2 = play_game(deal_seed, g2, rng);
                Row row;
                row.value_idx = vi;
                row.pair = pair;
                row.cand_wins = (r1.winner == 0) + (r2.winner == 1);
                row.cand_margin = r1.margin - r2.margin;
                row.hands = r1.hands + r2.hands;
                std::lock_guard<std::mutex> lk(mu);
                rows.push_back(row);
                done.insert({vi, pair});
                out << fmt("%g", values[vi]) << ',' << pair << ',' << row.cand_wins << ',' << row.cand_margin << ',' << row.hands << '\n';
                out.flush();
                if (++new_rows % std::max<int>(50, static_cast<int>(values.size()) * 10) == 0) write_report(false);
            }
        });
    for (auto& th : ts) th.join();
    write_report(true);
}
