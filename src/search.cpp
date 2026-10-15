#include "search.h"

#include <algorithm>
#include <atomic>
#ifdef SB_THREADS
#include <thread>
#endif
#include <cmath>
#include "fail.h"

namespace sb {

namespace {

constexpr int kInf = 100'000'000;
constexpr int kMaxPly = 128;
constexpr int8_t kSolvedDepth = 127;

uint64_t mix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

// Points a play would score (0 if the count is not a multiple of 5).
int play_points(const Layout& layout, const Action& m) {
    Layout l = layout;
    l.play(m.tile, m.target);
    const int c = l.count();
    return (c > 0 && c % 5 == 0) ? c : 0;
}

}  // namespace

Searcher::Searcher(const SearchParams& p, int tt_bits)
    : p_(p), tt_(size_t{1} << tt_bits), tt_mask_((uint64_t{1} << tt_bits) - 1), move_stack_(kMaxPly) {
    for (auto& v : move_stack_) v.reserve(32);
}

uint64_t Searcher::hash(const Game& g) const {
    const Layout& l = g.layout();
    int arms[4];
    const int n = l.arm_count();
    for (int i = 0; i < n; ++i) arms[i] = l.arm(i).pip * 2 + (l.arm(i).dbl ? 1 : 0);
    for (int i = 1; i < n; ++i)  // tiny insertion sort: arms are interchangeable
        for (int j = i; j > 0 && arms[j - 1] > arms[j]; --j) std::swap(arms[j - 1], arms[j]);
    uint64_t canon = 0;
    for (int i = 0; i < n; ++i) canon |= static_cast<uint64_t>(arms[i]) << (4 * i);
    uint64_t k2 = canon | static_cast<uint64_t>(l.spinner_pip() + 1) << 16 | static_cast<uint64_t>(n) << 20 |
                  static_cast<uint64_t>(l.empty()) << 23 | static_cast<uint64_t>(g.to_move()) << 24 |
                  static_cast<uint64_t>(g.bone_pos()) << 25 | static_cast<uint64_t>(g.pool(0)) << 32 |
                  static_cast<uint64_t>(g.pool(1)) << 40;
    const uint64_t k1 = static_cast<uint64_t>(g.hand(0)) | static_cast<uint64_t>(g.hand(1)) << 32;
    return mix64(k1 ^ mix64(k2 ^ salt_));
}

int Searcher::evaluate(const Game& g) const {
    if (p_.eval.nn) {
        int idx[kNNMaxActive];
        const int n = nn_features(g, idx);
        return static_cast<int>(std::lround(nn_eval(*p_.eval.nn, idx, n) * 100.0f));
    }
    const int p = g.to_move(), o = 1 - p;
    const TileMask hp = g.hand(p), ho = g.hand(o);
    return p_.eval.pip_weight * (pip_total(ho) - pip_total(hp)) +
           p_.eval.tile_weight * (tile_count(ho) - tile_count(hp));
}

int Searcher::terminal(const Game& c, int p) const {
    if (c.phase() == Phase::GameOver) {
        const int w = static_cast<int>(std::lround(p_.win_value * 100));
        return c.winner() == p ? w : -w;
    }
    if (c.last_hand_end() == HandEnd::Domino) {
        const int j = static_cast<int>(std::lround(p_.jockey_value * 100));
        return c.jockey() == p ? j : -j;
    }
    return 0;
}

void Searcher::order(const Game& g, std::vector<Action>& moves, int tt_tile, int tt_target) const {
    if (moves.size() < 2) return;
    int keys[64];
    const int n = static_cast<int>(std::min<size_t>(moves.size(), 64));
    for (int i = 0; i < n; ++i) {
        const Action& m = moves[i];
        int k = 0;
        if (m.kind == Action::Play) {
            if (m.tile == tt_tile && m.target == tt_target) k += 1'000'000;
            k += play_points(g.layout(), m) * 100 + tile_pips(m.tile);
        }
        keys[i] = k;
    }
    // Insertion sort, descending (move lists are short).
    for (int i = 1; i < n; ++i) {
        const Action m = moves[i];
        const int k = keys[i];
        int j = i - 1;
        while (j >= 0 && keys[j] < k) {
            moves[j + 1] = moves[j];
            keys[j + 1] = keys[j];
            --j;
        }
        moves[j + 1] = m;
        keys[j + 1] = k;
    }
}

int Searcher::child_value(const Game& g, const Game& c, const Action& m, int depth, int alpha, int beta, int ply) {
    const int p = g.to_move(), o = 1 - p;
    const int gain = 100 * ((g.pool(o) - c.pool(o)) - (g.pool(p) - c.pool(p)));
    if (c.phase() != Phase::Play) return gain + terminal(c, p);
    // Draws and passes are forced and do not consume depth.
    const int d = m.kind == Action::Play ? depth - 1 : depth;
    if (c.to_move() == p) return gain + negamax(c, d, alpha - gain, beta - gain, ply + 1);
    return gain - negamax(c, d, gain - beta, gain - alpha, ply + 1);
}

int Searcher::negamax(const Game& g, int depth, int alpha, int beta, int ply) {
    ++nodes_;
    if (hard_limit_ > 0 && nodes_ > hard_limit_) {
        stop_ = true;
        return 0;
    }
    if (depth <= 0 || ply >= kMaxPly - 1) {
        hit_horizon_ = true;
        if (p_.leaf_hook) p_.leaf_hook(g);
        return evaluate(g);
    }

    const uint64_t key = hash(g);
    TTEntry& e = tt_[key & tt_mask_];
    int tt_tile = -1, tt_target = -1;
    if (e.key == key) {
        tt_tile = e.tile;
        tt_target = e.target;
        if (e.depth >= depth) {
            const bool cut = e.bound == 1 || (e.bound == 2 && e.value >= beta) || (e.bound == 3 && e.value <= alpha);
            if (cut) {
                if (e.depth != kSolvedDepth) hit_horizon_ = true;
                return e.value;
            }
        }
    }

    std::vector<Action>& moves = move_stack_[ply];
    g.legal_actions(moves);
    order(g, moves, tt_tile, tt_target);

    const bool saved_horizon = hit_horizon_;
    hit_horizon_ = false;
    const int alpha0 = alpha;
    int best = -kInf;
    int8_t bt = -1, btg = -1;
    for (const Action& m : moves) {
        Game c = g;
        c.apply(m);
        const int v = child_value(g, c, m, depth, alpha, beta, ply);
        if (stop_) return 0;
        if (v > best) {
            best = v;
            bt = m.tile;
            btg = m.target;
        }
        if (v > alpha) alpha = v;
        if (alpha >= beta) break;
    }
    const bool horizon = hit_horizon_;
    hit_horizon_ = saved_horizon || horizon;

    e.key = key;
    e.value = best;
    e.depth = horizon ? static_cast<int8_t>(std::min(depth, 126)) : kSolvedDepth;
    e.bound = best <= alpha0 ? 3 : (best >= beta ? 2 : 1);
    e.tile = bt;
    e.target = btg;
    return best;
}

void Searcher::root_values(const Game& g, const std::vector<Action>& acts, std::vector<int>& values) {
    salt_ = mix64(salt_ + 1);  // fresh TT generation for every position/determinization
    nodes_ = 0;
    stop_ = false;
    last_depth_ = 0;
    last_exact_ = false;
    values.assign(acts.size(), 0);
    std::vector<int> cur(acts.size());

    for (int d = 1; d <= p_.max_depth; ++d) {
        hard_limit_ = d == 1 ? 0 : 4 * p_.nodes_per_det;  // depth 1 always completes
        hit_horizon_ = false;
        for (size_t i = 0; i < acts.size() && !stop_; ++i) {
            Game c = g;
            c.apply(acts[i]);
            cur[i] = child_value(g, c, acts[i], d, -kInf, kInf, 0);
        }
        if (stop_) break;  // keep the last completed depth
        values = cur;
        last_depth_ = d;
        if (!hit_horizon_) {
            last_exact_ = true;
            break;
        }
        if (nodes_ >= p_.nodes_per_det) break;
    }
    total_nodes_ += nodes_;
}

namespace {

// Runs f(d, searcher) for every determinization d. With SB_THREADS (native desktop
// build) the determinizations are spread over all cores, one Searcher per thread.
// Each determinization has its own RNG stream, so results do not depend on threading.
struct DetResult {
    int depth = 0;
    bool exact = false;
    bool fallback = false;
};

template <class F>
SearchStats run_dets(const SearchParams& p, F&& f) {
    const int D = p.determinizations;
    std::vector<DetResult> res(D);
    std::atomic<int64_t> nodes{0};
    auto worker = [&](std::atomic<int>& next) {
        Searcher s(p, 19);
        for (;;) {
            const int d = next++;
            if (d >= D) break;
            f(d, s, res[d]);
            res[d].depth = s.last_depth();
            res[d].exact = s.last_exact();
        }
        nodes += s.nodes();
    };
    std::atomic<int> next{0};
#ifdef SB_THREADS
    const int nt = std::max(1, std::min<int>(static_cast<int>(std::thread::hardware_concurrency()), D));
    if (nt > 1) {
        std::vector<std::thread> ts;
        for (int t = 0; t < nt; ++t) ts.emplace_back(worker, std::ref(next));
        for (auto& t : ts) t.join();
    } else {
        worker(next);
    }
#else
    worker(next);
#endif
    SearchStats st;
    double depth_sum = 0;
    for (const auto& r : res) {
        depth_sum += r.depth;
        st.exact += r.exact ? 1 : 0;
        st.fallbacks += r.fallback ? 1 : 0;
    }
    st.nodes = nodes;
    st.avg_depth = D ? depth_sum / D : 0;
    return st;
}

std::mt19937_64 det_rng(const SearchParams& p, int d) { return std::mt19937_64(mix64(p.seed * 0x100000001B3ull + static_cast<uint64_t>(d))); }

}  // namespace

std::vector<MoveEval> analyze_moves(const View& v, const SearchParams& p, SearchStats* stats) {
    if (v.phase() != Phase::Play || v.to_move() != v.me()) fail("analyze_moves: not my move");
    const std::vector<Action> root = v.legal_actions();
    const size_t n = root.size();
    const int D = p.determinizations;
    std::vector<std::vector<int>> per(D);

    SearchStats st = run_dets(p, [&](int d, Searcher& s, DetResult& r) {
        std::mt19937_64 rng = det_rng(p, d);
        bool ok = true;
        const Game g = v.determinize(rng, &ok);
        r.fallback = !ok;
        const std::vector<Action> acts = g.legal_actions();
        if (acts != root) fail("analyze_moves: determinization changed my legal moves");
        s.root_values(g, acts, per[d]);
    });

    std::vector<MoveEval> out(n);
    for (size_t i = 0; i < n; ++i) {
        double sum = 0, sumsq = 0;
        for (int d = 0; d < D; ++d) {
            const double x = per[d][i] / 100.0;
            sum += x;
            sumsq += x * x;
        }
        const double mean = sum / D;
        const double var = std::max(0.0, sumsq / D - mean * mean);
        out[i] = MoveEval{root[i], mean, std::sqrt(var / D), D};
    }
    // Information-set network: judge each play from what I can actually see, and blend
    // it with the PIMC value (which searches as if every tile were visible).
    if (p.info_model && p.info_weight > 0) {
        const int me = v.me(), opp = v.opp();
        for (auto& m : out) {
            if (m.action.kind != Action::Play) continue;
            View after = v;
            after.observe(me, m.action);
            if (after.phase() != Phase::Play) continue;  // hand over: the search value is exact
            const double gain = (v.pool(opp) - after.pool(opp)) - (v.pool(me) - after.pool(me));
            int idx[kInfoInputs];
            const int n = info_features(after, idx);
            const double info = gain + p.info_model->eval(idx, n);
            m.ev = (1 - p.info_weight) * m.ev + p.info_weight * info;
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const MoveEval& a, const MoveEval& b) { return a.ev > b.ev; });
    if (stats) *stats = st;
    return out;
}

BidEval analyze_bid(const View& v, const SearchParams& p, SearchStats* stats) {
    if (v.phase() != Phase::Bidding) fail("analyze_bid: not bidding");
    const int me = v.me(), opp = v.opp();
    const int D = p.determinizations;
    std::vector<std::vector<int>> mine(D);
    std::vector<double> theirs(D);

    SearchStats st = run_dets(p, [&](int d, Searcher& s, DetResult&) {
        std::mt19937_64 rng = det_rng(p, d);
        const Game g = v.determinize(rng);
        Game a = g;
        a.force_leader(me);
        s.root_values(a, a.legal_actions(), mine[d]);
        std::vector<int> vals;
        Game b = g;
        b.force_leader(opp);
        s.root_values(b, b.legal_actions(), vals);
        theirs[d] = -*std::max_element(vals.begin(), vals.end()) / 100.0;
    });

    BidEval r;
    // My lead: I pick one tile without seeing the hidden tiles, so average each lead over
    // the deals first, then take the best (taking the best per deal would overrate the lead).
    const size_t n = mine[0].size();
    double best = -1e9;
    for (size_t i = 0; i < n; ++i) {
        double sum = 0;
        for (int d = 0; d < D; ++d) sum += mine[d][i];
        best = std::max(best, sum / D / 100.0);
    }
    r.ev_i_lead = best;
    for (int d = 0; d < D; ++d) r.ev_opp_leads += theirs[d] / D;
    {
        const double mid = (r.ev_i_lead + r.ev_opp_leads) / 2;
        double half = (r.ev_i_lead - r.ev_opp_leads) / 2 * p.bid_calibration;
        if (p.bid_model) {
            // Learned from self-play: what leading with this hand actually wins.
            int idx[kBidInputs];
            const int n = bid_features(v.my_hand(), idx);
            half = p.bid_model->eval(idx, n) / 2.0;
        }
        r.ev_i_lead = mid + half;
        r.ev_opp_leads = mid - half;
    }
    r.lead_value = r.ev_i_lead - r.ev_opp_leads;

    // Highest bid b where winning at b is still at least as good as conceding at b-1.
    for (int b = 1; b <= v.max_bid(me); ++b) {
        const double win_at_b = r.ev_i_lead - v.bid_cost(me, b);
        const double concede_prev = r.ev_opp_leads + v.bid_cost(opp, b - 1);
        if (win_at_b >= concede_prev) r.walk_away = b;
        else break;
    }

    if (v.to_move() == me) {
        if (v.high_bidder() < 0) {
            r.suggestion = Action::make_bid(r.walk_away);
        } else {
            const int next = v.current_bid() + 1;
            r.suggestion = next <= r.walk_away ? Action::make_bid(next) : Action::accept();
        }
    }
    if (stats) *stats = st;
    return r;
}

double evaluate_position(const View& v, const SearchParams& p, SearchStats* stats) {
    if (v.phase() != Phase::Play) fail("evaluate_position: not in play");
    const int D = p.determinizations;
    std::vector<double> val(D);
    SearchStats st = run_dets(p, [&](int d, Searcher& s, DetResult& r) {
        std::mt19937_64 rng = det_rng(p, d);
        bool ok = true;
        const Game g = v.determinize(rng, &ok);
        r.fallback = !ok;
        std::vector<int> vals;
        s.root_values(g, g.legal_actions(), vals);
        const double best = *std::max_element(vals.begin(), vals.end()) / 100.0;
        val[d] = g.to_move() == v.me() ? best : -best;
    });
    double sum = 0;
    for (double x : val) sum += x;
    if (stats) *stats = st;
    return sum / D;
}

double evaluate_spectator(const Game& game, const SearchParams& p, SearchStats* stats) {
    if (game.phase() != Phase::Play) fail("evaluate_spectator: not in play");
    const int D = p.determinizations;
    std::vector<double> val(D);
    SearchStats st = run_dets(p, [&](int d, Searcher& s, DetResult&) {
        std::mt19937_64 rng = det_rng(p, d);
        Game g = game;
        g.shuffle_boneyard(rng);
        std::vector<int> vals;
        s.root_values(g, g.legal_actions(), vals);
        const double best = *std::max_element(vals.begin(), vals.end()) / 100.0;
        val[d] = g.to_move() == 0 ? best : -best;
    });
    double sum = 0;
    for (double x : val) sum += x;
    if (stats) *stats = st;
    return sum / D;
}

void evaluate_spectator_bid(const Game& game, const SearchParams& p, double& seat1_leads, double& seat2_leads,
                            SearchStats* stats) {
    const int D = p.determinizations;
    std::vector<double> a(D), b(D);
    SearchStats st = run_dets(p, [&](int d, Searcher& s, DetResult&) {
        std::mt19937_64 rng = det_rng(p, d);
        Game g = game;
        g.shuffle_boneyard(rng);
        std::vector<int> vals;
        for (int leader = 0; leader < 2; ++leader) {
            Game x = g;
            x.force_leader(leader);
            s.root_values(x, x.legal_actions(), vals);
            const double best = *std::max_element(vals.begin(), vals.end()) / 100.0;
            (leader == 0 ? a : b)[d] = leader == 0 ? best : -best;
        }
    });
    seat1_leads = seat2_leads = 0;
    for (int d = 0; d < D; ++d) {
        seat1_leads += a[d] / D;
        seat2_leads += b[d] / D;
    }
    if (stats) *stats = st;
}

Action choose_action(const View& v, const SearchParams& p) {
    if (v.phase() == Phase::Bidding) return analyze_bid(v, p).suggestion;
    const std::vector<Action> acts = v.legal_actions();
    if (acts.size() == 1) return acts[0];
    return analyze_moves(v, p).front().action;
}

}  // namespace sb
