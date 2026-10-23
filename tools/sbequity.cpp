// sbequity: builds the match equity table (MET), the chance of winning the game from
// the start of any hand, from self-play hands.
//
//   sbequity [--hands 40000] [--threads N] [--dets 16] [--nodes 4000] [--models DIR]
//            [--dir equity] [--seed 1] [--hours 0]
//   sbequity --build-only [--dir equity]
//
// 1. Plays hands at 30-30 with a random starting state (Seat 1 jockey, Seat 2 jockey, or
//    nobody, which means a forced lead), bidding included, and records what each seat
//    paid and who is jockey afterwards. At 30-30 no hand is cut short by a bust, so each
//    hand shows the full payment distribution. Appends to <dir>/hands.csv; stop any time
//    and run again to add more.
// 2. Mirrors every hand for the other seat and solves the game by dynamic programming:
//    pools only go down (chips won go to a trophy pile that is never spent), so the
//    state (my pool, opponent pool, jockey) always moves toward 0 except in hands where
//    nobody pays, which are handled by iteration. A payment that empties a pool loses;
//    if both pools would empty in the same hand, it counts as a coin flip.
// 3. Writes <dir>/met.txt (copy it next to the app, into the models folder, to use it),
//    <dir>/met_table.cpp (the built-in table, for src/), and <dir>/REPORT.md.

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
#include <numeric>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "../src/met.h"
#include "../src/search.h"

namespace fs = std::filesystem;
using namespace sb;
using Clock = std::chrono::steady_clock;

namespace {

struct HandRow {
    int start_jockey = -1;  // seat, -1 = nobody
    int pay[2] = {0, 0};    // chips each seat paid this hand (bid included)
    int bid_pay[2] = {0, 0};
    int next_jockey = -1;
    int over = 0;  // game ended during the hand
};

HandRow play_hand(std::mt19937_64& rng, const SearchParams& base) {
    HandRow r;
    r.start_jockey = static_cast<int>(rng() % 3) - 1;
    Game g;
    g.set_pools(30, 30);
    g.set_jockey(r.start_jockey);
    std::array<int8_t, kNumTiles> order;
    std::iota(order.begin(), order.end(), int8_t{0});
    portable_shuffle(order.data(), kNumTiles, rng);
    g.deal(order);
    View v[2] = {View(0), View(1)};
    for (int q = 0; q < 2; ++q) {
        v[q].new_game();
        v[q].set_jockey(r.start_jockey);
        v[q].set_pools(30, 30);
        v[q].start_hand(g.hand(q), g.to_move());
    }
    bool bid_done = g.phase() != Phase::Bidding;
    for (int guard = 0; guard < 2000 && (g.phase() == Phase::Play || g.phase() == Phase::Bidding); ++guard) {
        const int w = g.to_move();
        SearchParams p = base;
        p.seed = rng();
        const Action a = choose_action(v[w], p);
        g.apply(a);
        for (int k = 0; k < 2; ++k) v[k].observe(w, a, (k == w && a.kind == Action::Draw) ? g.last_drawn() : -1);
        if (!bid_done && g.phase() != Phase::Bidding) {
            bid_done = true;
            for (int q = 0; q < 2; ++q) r.bid_pay[q] = 30 - g.pool(q);
        }
    }
    for (int q = 0; q < 2; ++q) r.pay[q] = 30 - g.pool(q);
    r.over = g.phase() == Phase::GameOver ? 1 : 0;
    r.next_jockey = r.over ? -1 : g.jockey();
    return r;
}

std::vector<HandRow> read_rows(const fs::path& f) {
    std::vector<HandRow> rows;
    std::ifstream in(f);
    for (std::string line; std::getline(in, line);) {
        if (line.empty() || !(std::isdigit(static_cast<unsigned char>(line[0])) || line[0] == '-')) continue;
        HandRow r;
        char c;
        std::istringstream s(line);
        if (s >> r.start_jockey >> c >> r.pay[0] >> c >> r.pay[1] >> c >> r.bid_pay[0] >> c >> r.bid_pay[1] >> c >> r.next_jockey >> c >> r.over)
            rows.push_back(r);
    }
    return rows;
}

// Jockey in "me" terms: 0 me, 1 opponent, 2 nobody. Seat 0 is "me" unless mirrored.
int rel(int seat_jockey, bool mirror) {
    if (seat_jockey < 0) return 2;
    return (seat_jockey == 0) != mirror ? 0 : 1;
}

struct Trans {
    int x, y, nj;  // I pay x, opponent pays y, next jockey (relative)
    double w;
};

MetTable solve(const std::vector<HandRow>& rows, int counts[3]) {
    // Aggregate transitions per starting jockey state, with mirrored copies.
    std::map<std::tuple<int, int, int>, double> agg[3];
    for (const HandRow& r : rows)
        for (bool m : {false, true}) {
            const int j = rel(r.start_jockey, m);
            const int x = m ? r.pay[1] : r.pay[0], y = m ? r.pay[0] : r.pay[1];
            agg[j][{x, y, rel(r.next_jockey, m)}] += 1;
        }
    std::vector<Trans> T[3];
    for (int j = 0; j < 3; ++j) {
        double n = 0;
        for (auto& [k, c] : agg[j]) n += c;
        counts[j] = static_cast<int>(n);
        for (auto& [k, c] : agg[j]) T[j].push_back({std::get<0>(k), std::get<1>(k), std::get<2>(k), c / n});
    }
    MetTable t{};
    for (int s = 2; s <= 2 * kMetPool; ++s)
        for (int a = std::max(1, s - kMetPool); a <= std::min(kMetPool, s - 1); ++a) {
            const int b = s - a;
            double e[3] = {0.5, 0.5, 0.5};
            for (int it = 0; it < 500; ++it) {
                double ne[3], diff = 0;
                for (int j = 0; j < 3; ++j) {
                    double v = 0;
                    for (const Trans& tr : T[j]) {
                        const bool me_bust = tr.x >= a, op_bust = tr.y >= b;
                        double val;
                        if (me_bust && op_bust) val = 0.5;
                        else if (me_bust) val = 0;
                        else if (op_bust) val = 1;
                        else if (tr.x == 0 && tr.y == 0) val = e[tr.nj];
                        else val = t.p[a - tr.x][b - tr.y][tr.nj];
                        v += tr.w * val;
                    }
                    ne[j] = v;
                    diff = std::max(diff, std::abs(v - e[j]));
                }
                std::copy(ne, ne + 3, e);
                if (diff < 1e-9) break;
            }
            for (int j = 0; j < 3; ++j) t.p[a][b][j] = static_cast<float>(e[j]);
        }
    return t;
}

std::string f2(double x, const char* f = "%.1f") {
    char b[32];
    std::snprintf(b, sizeof b, f, x);
    return b;
}

// Chips the opponent would have to lose to match being jockey, at pools a-b.
double jockey_chips(const MetTable& t, int a, int b) {
    const double target = t.p[a][b][0];
    for (int d = 1; d < b; ++d) {
        const double lo = t.p[a][b - d + 1][2], hi = t.p[a][b - d][2];
        if (target <= hi) return d - 1 + (target - lo) / std::max(1e-9, hi - lo);
    }
    return b - 1;
}

void build(const fs::path& dir) {
    const std::vector<HandRow> rows = read_rows(dir / "hands.csv");
    if (rows.empty()) {
        std::printf("no hands in %s\n", (dir / "hands.csv").string().c_str());
        return;
    }
    int counts[3];
    const MetTable t = solve(rows, counts);
    const std::string header = std::to_string(rows.size()) + " self-play hands";
    met_save(t, (dir / "met.txt").string(), header);

    {
        std::ofstream c(dir / "met_table.cpp");
        c << "// Generated by tools/sbequity.cpp from " << header << ". Do not edit.\n"
          << "#include \"met.h\"\nnamespace sb {\nnamespace {\nconst MetTable kTable = {{\n";
        for (int a = 0; a <= kMetPool; ++a) {
            c << "{";
            for (int b = 0; b <= kMetPool; ++b) {
                c << "{";
                for (int j = 0; j < 3; ++j) c << f2(t.p[a][b][j], "%.5f") << (j < 2 ? "f," : "f");
                c << "}" << (b < kMetPool ? "," : "");
            }
            c << "}" << (a < kMetPool ? ",\n" : "\n");
        }
        c << "}};\n}  // namespace\nconst MetTable* met_builtin() { return &kTable; }\n}  // namespace sb\n";
    }

    // Report.
    double mean_pay[3] = {0, 0, 0}, dom = 0, blk = 0, over = 0, bid_hands = 0, bid_sum = 0;
    int n_by[3] = {0, 0, 0};
    for (const HandRow& r : rows) {
        const int j = r.start_jockey < 0 ? 2 : r.start_jockey;
        mean_pay[j] += r.pay[0] + r.pay[1];
        ++n_by[j];
        if (r.next_jockey >= 0) ++dom;
        else if (!r.over) ++blk;
        over += r.over;
        if (r.start_jockey >= 0) {
            ++bid_hands;
            bid_sum += r.bid_pay[0] + r.bid_pay[1];
        }
    }
    std::ofstream o(dir / "REPORT.md");
    o << "# Match equity table\n\n"
      << "Built from " << rows.size() << " self-play hands at 30-30 (mirrored for both seats: "
      << counts[0] << " with me as jockey, " << counts[1] << " with the opponent as jockey, " << counts[2]
      << " with a forced lead).\n\n"
      << "Hands: " << f2(100 * dom / rows.size()) << "% end in a domino, " << f2(100 * blk / rows.size())
      << "% block, " << f2(100 * over / rows.size()) << "% end the game from 30-30. Chips paid per hand (both seats): "
      << f2(mean_pay[2] / std::max(1, n_by[2])) << " after a forced lead, "
      << f2((mean_pay[0] + mean_pay[1]) / std::max(1, n_by[0] + n_by[1])) << " with a jockey. Average bid paid: "
      << f2(bid_sum / std::max(1.0, bid_hands), "%.2f") << " chips.\n\n"
      << "## P(win the game), nobody jockey (forced lead)\nRows: my pool. Columns: opponent's pool.\n\n|  |";
    const int cols[] = {30, 25, 20, 15, 10, 5, 3, 1};
    for (int b : cols) o << " " << b << " |";
    o << "\n|---|";
    for (int i = 0; i < 8; ++i) o << "---|";
    o << "\n";
    for (int a : cols) {
        o << "| **" << a << "** |";
        for (int b : cols) o << " " << f2(100 * t.p[a][b][2], "%.0f") << " |";
        o << "\n";
    }
    o << "\n## Value of being jockey\n\n| Pools | Win % as jockey | nobody | opponent jockey | jockey worth (chips) |\n|---|---|---|---|---|\n";
    for (auto [a, b] : std::vector<std::pair<int, int>>{{30, 30}, {25, 25}, {20, 20}, {15, 15}, {10, 10}, {5, 5}, {20, 10}, {10, 20}})
        o << "| " << a << "-" << b << " | " << f2(100 * t.p[a][b][0]) << " | " << f2(100 * t.p[a][b][2]) << " | "
          << f2(100 * t.p[a][b][1]) << " | " << f2(jockey_chips(t, a, b)) << " |\n";
    o << "\n## Win % per chip of lead (nobody jockey)\n\n| Pools | +1 chip for me |\n|---|---|\n";
    for (int a : {30, 20, 10, 5})
        o << "| " << a << "-" << a << " | +" << f2(100 * (t.p[a][a - 1][2] - t.p[a][a][2])) << " points |\n";
    std::printf("built met.txt, met_table.cpp, REPORT.md from %zu hands\n", rows.size());
}

}  // namespace

int main(int argc, char** argv) {
    std::map<std::string, std::string> a;
    bool build_only = false;
    for (int i = 1; i < argc; ++i) {
        std::string k = argv[i];
        if (k == "--build-only") { build_only = true; continue; }
        if (k.rfind("--", 0) == 0 && i + 1 < argc) a[k.substr(2)] = argv[++i];
    }
    const auto get = [&](const char* k, const std::string& d) { return a.count(k) ? a[k] : d; };
    const fs::path dir = get("dir", "equity");
    fs::create_directories(dir);
    if (build_only) {
        build(dir);
        return 0;
    }
    if (a.count("models")) std::printf("Engine models: %s\n", load_models_from(a["models"]).c_str());
    const int target = std::atoi(get("hands", "40000").c_str());
    const double hours = std::atof(get("hours", "0").c_str());
    const int hw = static_cast<int>(std::thread::hardware_concurrency());
    const int threads = std::max(1, std::atoi(get("threads", std::to_string(std::max(1, hw - 1))).c_str()));
    SearchParams base;
    base.determinizations = std::atoi(get("dets", "16").c_str());
    base.nodes_per_det = std::atoll(get("nodes", "4000").c_str());
    const uint64_t seed = std::strtoull(get("seed", "1").c_str(), nullptr, 10);

    const fs::path csv = dir / "hands.csv";
    const int have = static_cast<int>(read_rows(csv).size());
    std::ofstream out(csv, std::ios::app);
    if (have == 0) out << "start_jockey,pay1,pay2,bid_pay1,bid_pay2,next_jockey,game_over\n";
    std::printf("%d hands already in %s; playing up to %d with %d threads at %d x %lld\n", have, csv.string().c_str(), target,
                threads, base.determinizations, static_cast<long long>(base.nodes_per_det));
    std::fflush(stdout);

    std::atomic<int> done{have};
    std::mutex mu;
    const auto t0 = Clock::now();
    const auto out_of_time = [&] {
        return hours > 0 && std::chrono::duration<double>(Clock::now() - t0).count() > hours * 3600;
    };
    std::vector<std::thread> ts;
    for (int t = 0; t < threads; ++t)
        ts.emplace_back([&, t] {
            std::mt19937_64 rng(seed * 1000003ull + static_cast<uint64_t>(have) * 7919ull + t);
            while (done < target && !out_of_time()) {
                const HandRow r = play_hand(rng, base);
                std::lock_guard<std::mutex> lk(mu);
                if (done >= target) break;
                out << r.start_jockey << ',' << r.pay[0] << ',' << r.pay[1] << ',' << r.bid_pay[0] << ',' << r.bid_pay[1] << ','
                    << r.next_jockey << ',' << r.over << '\n';
                const int n = ++done;
                if (n % 500 == 0) {
                    out.flush();
                    const double el = std::chrono::duration<double>(Clock::now() - t0).count();
                    std::printf("%d / %d hands (%.0f per minute)\n", n, target, (n - have) / el * 60);
                    std::fflush(stdout);
                }
            }
        });
    for (auto& th : ts) th.join();
    out.close();
    build(dir);
}
