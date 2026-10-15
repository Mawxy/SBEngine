// sbtrain: unattended training for Spectacular Bid (Windows or Linux).
//
//   sbtrain run    [--hours 48] [--dir training] [--threads N] [--cycle-minutes 80] [--from DIR]
//   sbtrain quick  [--dir training-quick] [--threads N]   about 12 minutes, checks everything works
//   sbtrain status [--dir training]
//
// --from DIR  starting models for a new training folder (copied into <dir>/promoted if it is empty),
//             e.g. --dir training2 --from training\promoted to continue from an earlier run.
//
// Each cycle trains a new generation of three models, and keeps a model only if it beats
// the current best in head-to-head play (the same rule Stockfish's testing uses):
//
//   1. Self-play data. The current best engine plays hands against itself. Random moves
//      (for variety) only happen in the first few plies, so outcomes reflect real play.
//        - NNUE examples: positions where the players' own searches stopped at their
//          horizon (the only places the NNUE is ever used), sampled during self-play and
//          labelled with a deeper perfect-information search.
//        - Information-set examples: what each player could see at every position,
//          labelled with the chips that player went on to win.
//   2. Bidding data. Deals played twice, each seat leading once; the difference is what
//      the lead was worth with those hands.
//   3. Train the three networks (continuing from the current best).
//   4. Gates. Candidate vs best on mirrored hands (NNUE, info network) or full games
//      (bidding). Promoted only if it scores better with one-sided 95% confidence.
//
// Promoted models go to <dir>/promoted (eval.bin, bid.bin, info.bin). SpectacularBid.exe
// loads them from training\promoted next to it at startup.
// Stop any time (close the window or Ctrl+C); run the same command again to resume.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "../src/search.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;
using namespace sb;
using Clock = std::chrono::steady_clock;

namespace {

// ------------------------------------------------------------------ settings

struct Config {
    std::string dir = "training";
    double hours = 48;
    double cycle_minutes = 80;
    int threads = 1;
    bool quick = false;
    std::string from;
};

constexpr int kRandomPct = 25;        // self-play exploration: chance of a random move...
constexpr int kExplorePlies = 6;      // ...in the first plays of each hand only
constexpr int kLeavesPerDecision = 2; // NNUE: horizon positions kept per search (reservoir sample)
constexpr int kEvalWindow = 4;        // generations of data kept for training
constexpr int kInfoWindow = 6;
constexpr size_t kEvalCap = 5'000'000, kInfoCap = 10'000'000, kBidCap = 5'000'000;
constexpr double kPromoteZ = 1.645;   // one-sided 95%

// Information-set network size. A file of another size (e.g. 128 -> 32 from the first run)
// is still used as the current best, and a new network of this size has to beat it.
constexpr int kInfoNetH1 = 256, kInfoNetH2 = 64;

// Share of each cycle per phase (training time comes on top and is usually a few minutes).
// Most of the time goes to self-play data and the info network's gate, which is where
// the measurable gains are; the NNUE and bidding gates get less.
constexpr double kShareSelfplay = 0.45, kShareBidData = 0.08, kShareEvalGate = 0.12, kShareInfoGate = 0.25,
                 kShareBidGate = 0.10;

// ------------------------------------------------------------------ logging

std::mutex g_log_mu;
std::string g_dir = ".";

std::string stamp() {
    std::time_t t = std::time(nullptr);
    char b[32];
    std::strftime(b, sizeof b, "%Y-%m-%d %H:%M:%S", std::localtime(&t));
    return b;
}

void logf(const char* fmt, ...) {
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    std::lock_guard<std::mutex> lock(g_log_mu);
    std::printf("[%s] %s\n", stamp().c_str(), buf);
    std::fflush(stdout);
    if (FILE* f = std::fopen((g_dir + "/log.txt").c_str(), "a")) {
        std::fprintf(f, "[%s] %s\n", stamp().c_str(), buf);
        std::fclose(f);
    }
}

std::string fmt_dur(double secs) {
    char b[64];
    const int s = static_cast<int>(secs);
    std::snprintf(b, sizeof b, "%dh %02dm", s / 3600, (s / 60) % 60);
    return b;
}

// ------------------------------------------------------------------ persistent state

struct State {
    std::map<std::string, std::string> kv;
    double num(const std::string& k, double d = 0) const {
        auto it = kv.find(k);
        return it == kv.end() ? d : std::atof(it->second.c_str());
    }
    std::string str(const std::string& k, const std::string& d = "") const {
        auto it = kv.find(k);
        return it == kv.end() ? d : it->second;
    }
    void set(const std::string& k, double v) {
        std::ostringstream o;
        o.precision(10);
        o << v;
        kv[k] = o.str();
    }
    void set_str(const std::string& k, const std::string& v) { kv[k] = v; }
    void load(const std::string& path) {
        std::ifstream in(path);
        for (std::string line; std::getline(in, line);) {
            const auto eq = line.find('=');
            if (eq != std::string::npos) kv[line.substr(0, eq)] = line.substr(eq + 1);
        }
    }
    void save(const std::string& path) const {
        const std::string tmp = path + ".tmp";
        {
            std::ofstream out(tmp);
            for (const auto& [k, v] : kv) out << k << '=' << v << '\n';
        }
        std::error_code ec;
        fs::remove(path, ec);
        fs::rename(tmp, path, ec);
    }
};

// ------------------------------------------------------------------ models

struct ModelSet {
    bool has_eval = false, has_bid = false, has_info = false;
    Mlp eval, bid, info;
    NNWeights eval_w{};

    void load(const std::string& dir) {
        has_eval = eval.load(dir + "/eval.bin") && mlp_to_nnue(eval, eval_w);
        has_bid = bid.load(dir + "/bid.bin") && bid.in == kBidInputs;
        has_info = info.load(dir + "/info.bin") && info.in == kInfoInputs;
    }
    void apply(SearchParams& p) const {
        p.eval.nn = has_eval ? &eval_w : nullptr;
        p.bid_model = has_bid ? &bid : nullptr;
        p.info_model = has_info ? &info : nullptr;
        p.info_weight = has_info ? info.meta : 0.0f;
    }
    std::string describe() const {
        std::string s = has_eval ? "NNUE" : "classic eval";
        s += has_bid ? ", learned bidding" : ", calibrated bidding";
        if (has_info) {
            char b[64];
            std::snprintf(b, sizeof b, ", info network %d-%d (weight %.2f)", info.h1, info.h2, info.meta);
            s += b;
        }
        return s;
    }
};

SearchParams player_params(const ModelSet& m, int dets, long long nodes) {
    SearchParams p;
    p.determinizations = dets;
    p.nodes_per_det = nodes;
    m.apply(p);
    return p;
}

// ------------------------------------------------------------------ self-play

std::array<int8_t, kNumTiles> random_deal(std::mt19937_64& rng) {
    std::array<int8_t, kNumTiles> o;
    std::iota(o.begin(), o.end(), int8_t{0});
    portable_shuffle(o.data(), kNumTiles, rng);
    return o;
}

// Plays one hand. leader < 0: forced lead (highest double); otherwise that seat leads
// freely after the opener's bid of 0. on_pos(g, views) runs before every play-phase
// action. Returns the final pools.
template <class OnPos>
std::array<int, 2> play_hand(const std::array<int8_t, kNumTiles>& order, int leader, const SearchParams* players,
                             std::mt19937_64& rng, int random_pct, int explore_plies, OnPos&& on_pos) {
    Game g;
    g.set_pools(30, 30);
    if (leader >= 0) g.set_jockey(leader);
    g.deal(order);
    View v[2] = {View(0), View(1)};
    for (int q = 0; q < 2; ++q) {
        v[q].new_game();
        if (leader >= 0) v[q].set_jockey(leader);
        v[q].start_hand(g.hand(q), g.to_move());
    }
    if (leader >= 0) {
        const int opener = g.to_move();
        g.apply(Action::make_bid(0));
        for (auto& x : v) x.observe(opener, Action::make_bid(0));
    }
    int plies = 0;
    while (g.phase() == Phase::Play) {
        on_pos(g, v);
        const int p = g.to_move();
        const std::vector<Action> acts = g.legal_actions();
        Action a;
        if (acts.size() == 1) a = acts[0];
        else if (random_pct > 0 && plies < explore_plies && static_cast<int>(rng() % 100) < random_pct) a = acts[rng() % acts.size()];
        else {
            SearchParams q = players[p];
            q.seed = rng();
            a = choose_action(v[p], q);
        }
        g.apply(a);
        if (a.kind == Action::Play) ++plies;
        for (int k = 0; k < 2; ++k) v[k].observe(p, a, (k == p && a.kind == Action::Draw) ? g.last_drawn() : -1);
    }
    return {g.pool(0), g.pool(1)};
}

std::array<int, 2> play_hand(const std::array<int8_t, kNumTiles>& order, int leader, const SearchParams* players,
                             std::mt19937_64& rng) {
    return play_hand(order, leader, players, rng, 0, 0, [](const Game&, View*) {});
}

// A full game to 0 chips, bidding included. Returns seat 0's final margin (pool difference).
int play_game(uint64_t deal_seed, const SearchParams* players, std::mt19937_64& rng) {
    std::mt19937_64 deal_rng(deal_seed);
    Game g;
    g.new_game(deal_rng);
    View v[2] = {View(0), View(1)};
    for (int q = 0; q < 2; ++q) {
        v[q].new_game();
        v[q].start_hand(g.hand(q), g.to_move());
    }
    int guard = 0;
    while (g.phase() != Phase::GameOver && ++guard < 5000) {
        if (g.phase() == Phase::HandOver) {
            g.next_hand(deal_rng);
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
    return g.pool(0) - g.pool(1);
}

// Runs fn(thread, rng) repeatedly on every thread until the deadline.
void run_until(const Config& c, Clock::time_point deadline, uint64_t seed, const std::function<void(int, std::mt19937_64&)>& fn) {
    std::vector<std::thread> ts;
    for (int t = 0; t < c.threads; ++t)
        ts.emplace_back([&, t] {
            std::mt19937_64 rng(seed * 1000003 + static_cast<uint64_t>(t) * 7919 + 1);
            do fn(t, rng);
            while (Clock::now() < deadline);
        });
    for (auto& th : ts) th.join();
}

// ------------------------------------------------------------------ data files

void fill_sample(MlpSample& s, const int* idx, int n, float target) {
    s.n = static_cast<uint8_t>(std::min<int>(n, sizeof s.idx));
    for (int i = 0; i < s.n; ++i) s.idx[i] = static_cast<uint8_t>(idx[i]);
    s.target = target;
}

void append_samples(const std::string& path, const std::vector<MlpSample>& s, std::mutex& mu) {
    std::vector<uint8_t> buf;
    buf.reserve(s.size() * 48);
    for (const auto& x : s) {
        buf.push_back(x.n);
        buf.insert(buf.end(), x.idx, x.idx + x.n);
        const auto* b = reinterpret_cast<const uint8_t*>(&x.target);
        buf.insert(buf.end(), b, b + 4);
    }
    std::lock_guard<std::mutex> lock(mu);
    if (FILE* f = std::fopen(path.c_str(), "ab")) {
        std::fwrite(buf.data(), 1, buf.size(), f);
        std::fclose(f);
    }
}

size_t read_samples(const std::string& path, std::vector<MlpSample>& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return 0;
    size_t n = 0;
    for (;;) {
        MlpSample s;
        if (std::fread(&s.n, 1, 1, f) != 1) break;
        if (s.n > sizeof s.idx || std::fread(s.idx, 1, s.n, f) != s.n || std::fread(&s.target, 4, 1, f) != 1) break;
        out.push_back(s);
        ++n;
    }
    std::fclose(f);
    return n;
}

std::string data_path(const Config& c, const std::string& kind, int gen) { return c.dir + "/data/" + kind + "_" + std::to_string(gen) + ".bin"; }

std::vector<MlpSample> load_window(const Config& c, const std::string& kind, int gen, int window, size_t cap, std::mt19937_64& rng) {
    std::vector<MlpSample> all;
    for (int g = std::max(1, gen - window + 1); g <= gen; ++g) read_samples(data_path(c, kind, g), all);
    if (all.size() > cap) {
        std::shuffle(all.begin(), all.end(), rng);
        all.resize(cap);
    }
    return all;
}

void prune_data(const Config& c, const std::string& kind, int gen, int window) {
    for (int g = 1; g <= gen - window; ++g) {
        std::error_code ec;
        fs::remove(data_path(c, kind, g), ec);
    }
}

// ------------------------------------------------------------------ phases

// Horizon positions seen by the current search on this thread (reservoir sample).
thread_local std::vector<Game> t_leaves;
thread_local uint64_t t_leaf_seen = 0;
thread_local std::mt19937_64 t_leaf_rng{12345};

void leaf_hook(const Game& g) {
    ++t_leaf_seen;
    if (t_leaves.size() < static_cast<size_t>(kLeavesPerDecision)) t_leaves.push_back(g);
    else if (const uint64_t r = t_leaf_rng() % t_leaf_seen; r < static_cast<uint64_t>(kLeavesPerDecision)) t_leaves[r] = g;
}

// 1. Self-play data for the NNUE and the information-set network.
void phase_selfplay(const Config& c, const ModelSet& best, int gen, Clock::time_point deadline) {
    std::mutex mu;
    std::atomic<long long> hands{0}, eval_n{0}, info_n{0};
    SearchParams player = c.quick ? player_params(best, 8, 1500) : player_params(best, 12, 4000);
    player.leaf_hook = leaf_hook;
    const SearchParams label = player_params(best, 1, c.quick ? 6000 : 30000);
    const std::string eval_path = data_path(c, "eval", gen), info_path = data_path(c, "info", gen);
    const auto t0 = Clock::now();
    run_until(c, deadline, 1000 + gen, [&](int, std::mt19937_64& rng) {
        thread_local std::unique_ptr<Searcher> labeler;
        if (!labeler) labeler = std::make_unique<Searcher>(label, 19);
        t_leaf_rng.seed(rng());
        struct Pending {
            MlpSample s;
            int observer, pool_me, pool_opp;
        };
        std::vector<Pending> pend;
        std::vector<MlpSample> ev;
        std::vector<int> vals;
        // Label the horizon positions from the last search with a deeper search (side to move's view).
        auto flush_leaves = [&] {
            for (const Game& lg : t_leaves) {
                if (lg.phase() != Phase::Play) continue;
                const std::vector<Action> acts = lg.legal_actions();
                if (acts.empty()) continue;
                labeler->root_values(lg, acts, vals);
                int idx[kInfoInputs];
                MlpSample e;
                fill_sample(e, idx, nn_features(lg, idx), *std::max_element(vals.begin(), vals.end()) / 100.0f);
                ev.push_back(e);
            }
            t_leaves.clear();
            t_leaf_seen = 0;
        };
        t_leaves.clear();
        t_leaf_seen = 0;
        const SearchParams players[2] = {player, player};
        const int leader = rng() % 2 ? static_cast<int>(rng() % 2) : -1;
        const auto end = play_hand(random_deal(rng), leader, players, rng, kRandomPct, kExplorePlies, [&](const Game& g, View* v) {
            flush_leaves();
            int idx[kInfoInputs];
            for (int q = 0; q < 2; ++q) {
                Pending i;
                fill_sample(i.s, idx, info_features(v[q], idx), 0);
                i.observer = q;
                i.pool_me = g.pool(q);
                i.pool_opp = g.pool(1 - q);
                pend.push_back(i);
            }
        });
        flush_leaves();
        std::vector<MlpSample> inf;
        for (auto& p : pend) {
            p.s.target = static_cast<float>((p.pool_opp - end[1 - p.observer]) - (p.pool_me - end[p.observer]));
            inf.push_back(p.s);
        }
        append_samples(eval_path, ev, mu);
        append_samples(info_path, inf, mu);
        eval_n += static_cast<long long>(ev.size());
        info_n += static_cast<long long>(inf.size());
        ++hands;
    });
    logf("  self-play: %lld hands, %lld NNUE horizon positions, %lld info examples (%s)", hands.load(), eval_n.load(), info_n.load(),
         fmt_dur(std::chrono::duration<double>(Clock::now() - t0).count()).c_str());
}

// 2. Bidding data: each deal played twice, each seat leading once.
void phase_bid_data(const Config& c, const ModelSet& best, int gen, Clock::time_point deadline) {
    std::mutex mu;
    std::atomic<long long> deals{0};
    const SearchParams player = player_params(best, 8, 1500);
    const std::string path = data_path(c, "bid", gen);
    run_until(c, deadline, 5000 + gen, [&](int, std::mt19937_64& rng) {
        const auto order = random_deal(rng);
        const SearchParams players[2] = {player, player};
        const auto a = play_hand(order, 0, players, rng);
        const auto b = play_hand(order, 1, players, rng);
        // Seat 0's net when seat 0 leads, minus its net when seat 1 leads.
        const float lv = static_cast<float>((a[0] - a[1]) - (b[0] - b[1]));
        TileMask h0 = 0, h1 = 0;
        for (int i = 0; i < 7; ++i) h0 |= tile_bit(order[i]);
        for (int i = 7; i < 14; ++i) h1 |= tile_bit(order[i]);
        std::vector<MlpSample> s(2);
        int idx[kBidInputs];
        fill_sample(s[0], idx, bid_features(h0, idx), lv);
        fill_sample(s[1], idx, bid_features(h1, idx), lv);
        append_samples(path, s, mu);
        ++deals;
    });
    logf("  bidding data: %lld deals played both ways", deals.load());
}

struct GateResult {
    long long n = 0;
    double mean = 0, se = 0;
    bool promote = false;
};

GateResult finish_gate(double sum, double sumsq, long long n, long long min_n) {
    GateResult r;
    r.n = n;
    if (n > 1) {
        r.mean = sum / n;
        r.se = std::sqrt(std::max(0.0, sumsq / n - r.mean * r.mean) / n);
        r.promote = n >= min_n && r.mean > 0 && r.se > 0 && r.mean / r.se > kPromoteZ;
    }
    return r;
}

// Mirrored hands: each deal played twice with seats swapped. Score: candidate's chips per hand.
GateResult gate_hands(const Config& c, const SearchParams& cand, const SearchParams& base, Clock::time_point deadline, uint64_t seed) {
    std::mutex mu;
    double sum = 0, sumsq = 0;
    long long n = 0;
    run_until(c, deadline, seed, [&](int, std::mt19937_64& rng) {
        const auto order = random_deal(rng);
        const int leader = rng() % 2 ? static_cast<int>(rng() % 2) : -1;
        const SearchParams p1[2] = {cand, base}, p2[2] = {base, cand};
        const auto a = play_hand(order, leader, p1, rng);
        const auto b = play_hand(order, leader, p2, rng);
        const double x = ((a[0] - a[1]) + (b[1] - b[0])) / 2.0;
        std::lock_guard<std::mutex> lock(mu);
        sum += x;
        sumsq += x * x;
        ++n;
    });
    return finish_gate(sum, sumsq, n, c.quick ? 4 : 200);
}

// Full games with bidding, seats swapped on the same deal sequence. Score: candidate's final chip margin.
GateResult gate_games(const Config& c, const SearchParams& cand, const SearchParams& base, Clock::time_point deadline, uint64_t seed) {
    std::mutex mu;
    double sum = 0, sumsq = 0;
    long long n = 0;
    run_until(c, deadline, seed, [&](int, std::mt19937_64& rng) {
        const uint64_t deal_seed = rng();
        const SearchParams p1[2] = {cand, base}, p2[2] = {base, cand};
        const double x = (play_game(deal_seed, p1, rng) - play_game(deal_seed, p2, rng)) / 2.0;
        std::lock_guard<std::mutex> lock(mu);
        sum += x;
        sumsq += x * x;
        ++n;
    });
    return finish_gate(sum, sumsq, n, c.quick ? 2 : 100);
}

void record_gate(const Config& c, const std::string& model, int gen, const GateResult& g, double holdout, double baseline) {
    const std::string path = c.dir + "/results.csv";
    const bool fresh = !fs::exists(path);
    if (FILE* f = std::fopen(path.c_str(), "a")) {
        if (fresh) std::fprintf(f, "time,model,generation,samples,score,se,promoted,heldout_mse,baseline_mse\n");
        std::fprintf(f, "%s,%s,%d,%lld,%.4f,%.4f,%s,%.4f,%.4f\n", stamp().c_str(), model.c_str(), gen, g.n, g.mean, g.se,
                     g.promote ? "yes" : "no", holdout, baseline);
        std::fclose(f);
    }
}

TrainOptions train_opts(const Config& c, int epochs, uint64_t seed) {
    TrainOptions o;
    o.epochs = c.quick ? 2 : epochs;
    o.threads = c.threads;
    o.seed = seed;
    o.verbose = true;
    return o;
}

// ------------------------------------------------------------------ report

void write_report(const Config& c, const State& st, const ModelSet& best) {
    std::ofstream out(c.dir + "/REPORT.md");
    out << "# Spectacular Bid training\n\n";
    out << "Updated " << stamp() << ". Time used " << fmt_dur(st.num("elapsed")) << " of " << fmt_dur(st.num("budget")) << ", "
        << static_cast<int>(st.num("cycle")) << " cycles.\n\n";
    out << "**In use now (training\\promoted):** " << best.describe() << "\n\n";
    out << "| Model | Generations tried | Promoted | Last result |\n|---|---|---|---|\n";
    for (const char* m : {"eval", "info", "bid"}) {
        const std::string name = std::string(m) == "eval" ? "NNUE evaluation" : std::string(m) == "info" ? "Info-set network" : "Bidding model";
        out << "| " << name << " | " << static_cast<int>(st.num(std::string(m) + "_gen")) << " | "
            << static_cast<int>(st.num(std::string(m) + "_promoted")) << " | " << st.str(std::string(m) + "_last", "none yet") << " |\n";
    }
    out << "\nScores are the candidate's chips per hand (NNUE, info network) or final chip margin per game (bidding) "
           "against the current best. A model is promoted when it is ahead with one-sided 95% confidence.\n\n";
    out << "Full history: `results.csv`. Running log: `log.txt`.\n";
}

// ------------------------------------------------------------------ main loop

void prevent_sleep(bool on) {
#ifdef _WIN32
    SetThreadExecutionState(on ? (ES_CONTINUOUS | ES_SYSTEM_REQUIRED) : ES_CONTINUOUS);
#else
    (void)on;
#endif
}

int run(Config c) {
    fs::create_directories(c.dir + "/data");
    fs::create_directories(c.dir + "/promoted");
    fs::create_directories(c.dir + "/candidates");
    g_dir = c.dir;
    State st;
    st.load(c.dir + "/state.txt");
    const double budget = c.hours * 3600;
    st.set("budget", budget);
    if (st.num("elapsed") >= budget) {
        logf("This run has already used its %s. Use --hours to extend it (for example --hours %.0f).", fmt_dur(budget).c_str(), c.hours + 24);
        return 0;
    }
    prevent_sleep(true);
#ifdef _WIN32
    SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);  // keep the PC usable
#endif
    const auto session_start = Clock::now();
    const double elapsed0 = st.num("elapsed");
    auto elapsed = [&] { return elapsed0 + std::chrono::duration<double>(Clock::now() - session_start).count(); };
    auto remaining = [&] { return budget - elapsed(); };
    auto save = [&] {
        st.set("elapsed", elapsed());
        st.save(c.dir + "/state.txt");
    };

    if (!c.from.empty()) {
        bool empty = true;
        for (const char* f : {"eval.bin", "info.bin", "bid.bin"})
            if (fs::exists(c.dir + "/promoted/" + f)) empty = false;
        if (empty) {
            for (const char* f : {"eval.bin", "info.bin", "bid.bin"}) {
                std::error_code ec;
                if (fs::exists(c.from + "/" + f)) fs::copy_file(c.from + "/" + f, c.dir + "/promoted/" + f, ec);
            }
            logf("Starting from the models in %s", fs::absolute(c.from).string().c_str());
        }
    }
    ModelSet best;
    best.load(c.dir + "/promoted");
    logf("Spectacular Bid training: %d threads, %s budget, %s left. Folder: %s", c.threads, fmt_dur(budget).c_str(),
         fmt_dur(remaining()).c_str(), fs::absolute(c.dir).string().c_str());
    logf("Starting models: %s", best.describe().c_str());
    write_report(c, st, best);

    const double cycle = c.cycle_minutes * 60;
    auto until = [&](double share) {
        const double secs = std::max(5.0, std::min(cycle * share, remaining()));
        return Clock::now() + std::chrono::milliseconds(static_cast<long long>(secs * 1000));
    };

    while (remaining() > 30) {
        const int cyc = static_cast<int>(st.num("cycle")) + 1;
        logf("Cycle %d (%s left)", cyc, fmt_dur(remaining()).c_str());
        std::mt19937_64 rng(cyc * 31337ULL);

        // 1. self-play data
        const int eval_gen = static_cast<int>(st.num("eval_gen")) + 1;
        const int info_gen = static_cast<int>(st.num("info_gen")) + 1;
        const int data_gen = eval_gen;  // eval and info data share a generation number
        phase_selfplay(c, best, data_gen, until(kShareSelfplay));
        save();
        if (remaining() <= 30) break;

        // 2. bidding data
        const int bid_gen = static_cast<int>(st.num("bid_gen")) + 1;
        phase_bid_data(c, best, bid_gen, until(kShareBidData));
        save();
        if (remaining() <= 30) break;

        // 3a. NNUE: train, gate
        {
            std::vector<MlpSample> data = load_window(c, "eval", data_gen, kEvalWindow, kEvalCap, rng);
            logf("  NNUE generation %d: training on %zu horizon positions", eval_gen, data.size());
            Mlp cand = best.has_eval ? best.eval : Mlp(kNNInputs, kNNH1, kNNH2);
            if (!best.has_eval) cand.init_random(rng, 0.0f);
            const TrainResult tr = train_mlp(cand, data, train_opts(c, 4, rng()));
            data.clear();
            data.shrink_to_fit();
            cand.save(c.dir + "/candidates/eval_" + std::to_string(eval_gen) + ".bin");
            ModelSet cm = best;
            cm.eval = cand;
            cm.has_eval = mlp_to_nnue(cm.eval, cm.eval_w);
            const GateResult g = gate_hands(c, player_params(cm, 12, 4000), player_params(best, 12, 4000), until(kShareEvalGate), rng());
            logf("  NNUE generation %d vs best: %+.3f chips/hand (se %.3f, %lld pairs) -> %s", eval_gen, g.mean, g.se, g.n,
                 g.promote ? "PROMOTED" : "kept current");
            if (g.promote) {
                cand.save(c.dir + "/promoted/eval.bin");
                best.eval = cand;
                best.has_eval = mlp_to_nnue(best.eval, best.eval_w);
                st.set("eval_promoted", st.num("eval_promoted") + 1);
            }
            record_gate(c, "eval", eval_gen, g, tr.holdout_mse, tr.holdout_var);
            char b[160];
            std::snprintf(b, sizeof b, "gen %d: %+.2f (se %.2f) %s", eval_gen, g.mean, g.se, g.promote ? "promoted" : "not promoted");
            st.set_str("eval_last", b);
            st.set("eval_gen", eval_gen);
            prune_data(c, "eval", data_gen, kEvalWindow);
            save();
        }
        if (remaining() <= 30) break;

        // 3b. Information-set network: train, gate
        {
            std::vector<MlpSample> data = load_window(c, "info", data_gen, kInfoWindow, kInfoCap, rng);
            // Continue from the best network if it already has the new size; otherwise a fresh
            // network of the new size (trained longer) has to beat the current one.
            const bool cont = best.has_info && best.info.h1 == kInfoNetH1 && best.info.h2 == kInfoNetH2;
            logf("  Info network generation %d: training %s %d-%d-%d network on %zu positions", info_gen, cont ? "the current" : "a new",
                 kInfoInputs, kInfoNetH1, kInfoNetH2, data.size());
            Mlp cand = cont ? best.info : Mlp(kInfoInputs, kInfoNetH1, kInfoNetH2);
            if (!cont) cand.init_random(rng, 0.0f);
            const TrainResult tr = train_mlp(cand, data, train_opts(c, cont ? 4 : 8, rng()));
            data.clear();
            data.shrink_to_fit();
            // Alternate the blend weight between generations to find what works.
            static const float kWeights[] = {0.45f, 0.55f, 0.7f};
            cand.meta = best.has_info && info_gen % 2 ? best.info.meta : kWeights[info_gen % 3];
            cand.save(c.dir + "/candidates/info_" + std::to_string(info_gen) + ".bin");
            ModelSet cm = best;
            cm.info = cand;
            cm.has_info = true;
            const GateResult g = gate_hands(c, player_params(cm, 12, 4000), player_params(best, 12, 4000), until(kShareInfoGate), rng());
            logf("  Info network generation %d (weight %.2f) vs best: %+.3f chips/hand (se %.3f, %lld pairs) -> %s", info_gen, cand.meta,
                 g.mean, g.se, g.n, g.promote ? "PROMOTED" : "kept current");
            if (g.promote) {
                cand.save(c.dir + "/promoted/info.bin");
                best.info = cand;
                best.has_info = true;
                st.set("info_promoted", st.num("info_promoted") + 1);
            }
            record_gate(c, "info", info_gen, g, tr.holdout_mse, tr.holdout_var);
            char b[160];
            std::snprintf(b, sizeof b, "gen %d (w %.2f): %+.2f (se %.2f) %s", info_gen, cand.meta, g.mean, g.se, g.promote ? "promoted" : "not promoted");
            st.set_str("info_last", b);
            st.set("info_gen", info_gen);
            prune_data(c, "info", data_gen, kInfoWindow);
            save();
        }
        if (remaining() <= 30) break;

        // 3c. Bidding model: train on all bidding data so far, gate on full games
        {
            std::vector<MlpSample> data = load_window(c, "bid", bid_gen, 1000, kBidCap, rng);
            logf("  Bidding model generation %d: training on %zu hands", bid_gen, data.size());
            Mlp cand(kBidInputs, kBidH1, kBidH2);
            cand.init_random(rng, 2.0f);
            const TrainResult tr = train_mlp(cand, data, train_opts(c, 12, rng()));
            data.clear();
            cand.save(c.dir + "/candidates/bid_" + std::to_string(bid_gen) + ".bin");
            ModelSet cm = best;
            cm.bid = cand;
            cm.has_bid = true;
            const GateResult g = gate_games(c, player_params(cm, 12, 4000), player_params(best, 12, 4000), until(kShareBidGate), rng());
            logf("  Bidding model generation %d vs best: %+.3f chips/game (se %.3f, %lld game pairs) -> %s", bid_gen, g.mean, g.se, g.n,
                 g.promote ? "PROMOTED" : "kept current");
            if (g.promote) {
                cand.save(c.dir + "/promoted/bid.bin");
                best.bid = cand;
                best.has_bid = true;
                st.set("bid_promoted", st.num("bid_promoted") + 1);
            }
            record_gate(c, "bid", bid_gen, g, tr.holdout_mse, tr.holdout_var);
            char b[160];
            std::snprintf(b, sizeof b, "gen %d: %+.2f (se %.2f) %s", bid_gen, g.mean, g.se, g.promote ? "promoted" : "not promoted");
            st.set_str("bid_last", b);
            st.set("bid_gen", bid_gen);
            save();
        }

        st.set("cycle", cyc);
        save();
        write_report(c, st, best);
        logf("Cycle %d done. In use: %s", cyc, best.describe().c_str());
    }
    save();
    write_report(c, st, best);
    prevent_sleep(false);
    logf("Training finished. Promoted models are in %s. Restart SpectacularBid.exe to use them.",
         fs::absolute(c.dir + "/promoted").string().c_str());
    return 0;
}

int status(const Config& c) {
    std::ifstream in(c.dir + "/REPORT.md");
    if (!in) {
        std::printf("No training found in %s\n", c.dir.c_str());
        return 1;
    }
    std::printf("%s", std::string(std::istreambuf_iterator<char>(in), {}).c_str());
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string mode = argc > 1 ? argv[1] : "";
    Config c;
    c.threads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1);
    if (mode == "quick") {
        c.dir = "training-quick";
        c.hours = 0.2;
        c.cycle_minutes = 3.5;
        c.quick = true;
    }
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        const bool has = i + 1 < argc;
        if (a == "--hours" && has) c.hours = std::atof(argv[++i]);
        else if (a == "--dir" && has) c.dir = argv[++i];
        else if (a == "--threads" && has) c.threads = std::max(1, std::atoi(argv[++i]));
        else if (a == "--cycle-minutes" && has) c.cycle_minutes = std::max(1.0, std::atof(argv[++i]));
        else if (a == "--from" && has) c.from = argv[++i];
        else {
            std::printf("unknown option %s\n", a.c_str());
            return 1;
        }
    }
    if (mode == "run" || mode == "quick") return run(c);
    if (mode == "status") return status(c);
    std::printf(
        "Spectacular Bid training\n\n"
        "  sbtrain run    [--hours 48] [--dir training] [--threads N] [--cycle-minutes 80] [--from DIR]\n"
        "  sbtrain quick  [--threads N]        about 12 minutes; checks everything works\n"
        "  sbtrain status [--dir training]\n");
    return 1;
}
