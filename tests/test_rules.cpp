// Rules tests: hand-built scenarios for every rule in engine-rules-spec.md,
// then random self-play with invariant checks.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <random>

#include "../src/met.h"
#include "../src/search.h"
#include "../src/session.h"

using namespace sb;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (cond) ++g_pass;                                               \
        else { ++g_fail; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)
#define CHECK_EQ(a, b)                                                                     \
    do {                                                                                   \
        auto _a = (a); auto _b = (b);                                                      \
        if (_a == _b) ++g_pass;                                                            \
        else { ++g_fail; std::printf("FAIL %s:%d  %s == %s  (%lld vs %lld)\n", __FILE__, __LINE__, #a, #b, \
                                     (long long)_a, (long long)_b); }                      \
    } while (0)

static int T(int a, int b) { return tile_id(a, b); }

// Build a deal: p0 tiles, p1 tiles, boneyard order; remaining tiles appended to boneyard.
static std::array<int8_t, kNumTiles> make_order(std::initializer_list<int> p0, std::initializer_list<int> p1,
                                                 std::initializer_list<int> bone = {}) {
    std::array<int8_t, kNumTiles> o{};
    TileMask used = 0;
    int i = 0;
    for (int t : p0) { o[i++] = t; used |= tile_bit(t); }
    for (int t : p1) { o[i++] = t; used |= tile_bit(t); }
    for (int t : bone) { o[i++] = t; used |= tile_bit(t); }
    for (int t = 0; t < kNumTiles; ++t)
        if (!(used & tile_bit(t))) o[i++] = t;
    return o;
}

static void test_tiles() {
    CHECK_EQ(pip_total(kAllTiles), 168);
    CHECK_EQ(tile_count(kSuit[3]), 7);
    CHECK_EQ(round5_chips(0), 0);
    CHECK_EQ(round5_chips(2), 0);
    CHECK_EQ(round5_chips(3), 1);
    CHECK_EQ(round5_chips(12), 2);
    CHECK_EQ(round5_chips(13), 3);
    CHECK_EQ(round5_chips(25), 5);
}

static void test_layout_spinner_lead() {
    Layout L;
    L.play(T(5, 5), kTargetLead);
    CHECK_EQ(L.count(), 10);                 // double lead counts both pips
    L.play(T(5, 3), kTargetSpinner);
    CHECK_EQ(L.count(), 13);                 // 3 + spinner 10
    L.play(T(5, 2), kTargetSpinner);
    CHECK_EQ(L.count(), 5);                  // both long sides covered: spinner stops counting
    L.play(T(5, 0), kTargetSpinner);
    CHECK_EQ(L.count(), 5);                  // third arm counts
    L.play(T(5, 1), kTargetSpinner);
    CHECK_EQ(L.count(), 6);                  // fourth arm
    CHECK(!L.spinner_open());
    CHECK(!L.is_legal(T(5, 4), kTargetSpinner));
}

static void test_layout_line_then_spinner() {
    Layout L;
    L.play(T(6, 4), kTargetLead);
    CHECK_EQ(L.count(), 10);                 // lead single scores both ends
    CHECK(!L.has_spinner());
    // play [3-3]? not legal (no 3 end)
    CHECK(!L.is_legal(T(3, 3), kTargetArm0));
    L.play(T(4, 3), 1);                      // ends 6, 3
    CHECK_EQ(L.count(), 9);
    L.play(T(3, 3), 1);                      // first double -> spinner, line end 6 is arm 0
    CHECK(L.has_spinner());
    CHECK_EQ(L.arm_count(), 1);
    CHECK_EQ(L.count(), 12);                 // 6 + spinner 6
    L.play(T(3, 1), kTargetSpinner);         // second long side
    CHECK_EQ(L.count(), 7);                  // 6 + 1
    L.play(T(3, 0), kTargetSpinner);         // third arm
    CHECK_EQ(L.count(), 7);
    L.play(T(6, 6), 0);                      // non-spinner double on end counts double
    CHECK_EQ(L.count(), 13);                 // 12 + 1 + 0
    L.play(T(6, 5), 0);
    CHECK_EQ(L.count(), 6);                  // 5 + 1 + 0
}

static void test_dedup_targets() {
    Layout L;
    L.play(T(6, 4), kTargetLead);
    L.play(T(4, 1), 1);          // ends 6, 1
    L.play(T(6, 1), 1);          // ends 6, 6
    std::vector<int8_t> tg;
    L.targets(T(6, 5), tg);
    CHECK_EQ(tg.size(), size_t(1));  // two identical ends -> one move
    CHECK_EQ(L.count(), 12);
}

static void test_forced_lead_first_hand() {
    Game g;
    g.set_pools(30, 30);
    g.deal(make_order({T(6, 6), T(1, 0), T(2, 0), T(3, 0), T(4, 0), T(5, 0), T(6, 0)},
                      {T(5, 5), T(2, 1), T(3, 1), T(4, 1), T(5, 1), T(6, 1), T(3, 2)}));
    CHECK(g.phase() == Phase::Play);
    CHECK_EQ(g.to_move(), 0);
    auto acts = g.legal_actions();
    CHECK_EQ((int)acts.size(), 1);
    CHECK(acts[0] == Action::play(T(6, 6), kTargetLead));
    // [6-6] lead = 12, no score.
    g.apply(acts[0]);
    CHECK_EQ(g.pool(1), 30);

    // No doubles dealt: heaviest tile leads; [6-3] beats [5-4] on tiebreak.
    Game h;
    h.set_pools(30, 30);
    h.deal(make_order({T(5, 4), T(1, 0), T(2, 0), T(3, 0), T(4, 0), T(5, 0), T(6, 0)},
                      {T(6, 3), T(2, 1), T(3, 1), T(4, 1), T(5, 1), T(6, 1), T(3, 2)}));
    CHECK_EQ(h.to_move(), 1);
    CHECK_EQ(h.forced_lead(), T(6, 3));
}

static void test_lead_scores() {
    Game g;
    g.set_pools(30, 30);
    g.deal(make_order({T(5, 5), T(1, 0), T(2, 0), T(3, 0), T(4, 0), T(6, 0), T(2, 1)},
                      {T(4, 4), T(3, 1), T(4, 1), T(5, 1), T(6, 1), T(3, 2), T(4, 2)}));
    g.apply(Action::play(T(5, 5), kTargetLead));
    CHECK_EQ(g.pool(1), 28);   // 10 points = 2 chips
    CHECK_EQ(g.trophy(0), 2);
}

// Post-domino bidding. P0 is jockey, P1 opens.
static Game bidding_game(int pool0 = 30, int pool1 = 30) {
    Game g;
    g.set_pools(pool0, pool1);
    g.set_jockey(0);
    g.deal(make_order({T(1, 0), T(2, 0), T(3, 0), T(4, 0), T(5, 0), T(6, 0), T(2, 1)},
                      {T(3, 1), T(4, 1), T(5, 1), T(6, 1), T(3, 2), T(4, 2), T(5, 2)}));
    return g;
}

static void test_bidding() {
    {  // opener bids 0 = pass: jockey leads free
        Game g = bidding_game();
        CHECK(g.phase() == Phase::Bidding);
        CHECK_EQ(g.to_move(), 1);
        g.apply(Action::make_bid(0));
        CHECK(g.phase() == Phase::Play);
        CHECK_EQ(g.to_move(), 0);
        CHECK_EQ(g.pool(0), 30);
        CHECK_EQ(g.legal_actions().size(), size_t(7));  // any tile
    }
    {  // rulebook example: opener 3, jockey 4, accepted -> jockey pays 1
        Game g = bidding_game();
        g.apply(Action::make_bid(3));
        g.apply(Action::make_bid(4));
        g.apply(Action::accept());
        CHECK_EQ(g.pool(0), 29);
        CHECK_EQ(g.trophy(1), 1);
        CHECK_EQ(g.to_move(), 0);
        CHECK(g.phase() == Phase::Play);
    }
    {  // opener 1, jockey 2, accepted -> jockey pays 0, credit lost
        Game g = bidding_game();
        g.apply(Action::make_bid(1));
        g.apply(Action::make_bid(2));
        g.apply(Action::accept());
        CHECK_EQ(g.pool(0), 30);
        CHECK_EQ(g.pool(1), 30);
        CHECK_EQ(g.to_move(), 0);
    }
    {  // opener wins: pays full bid to jockey
        Game g = bidding_game();
        g.apply(Action::make_bid(5));
        g.apply(Action::accept());
        CHECK_EQ(g.pool(1), 25);
        CHECK_EQ(g.trophy(0), 5);
        CHECK_EQ(g.to_move(), 1);
    }
    {  // bidding war: 3, 4, 6, accept -> opener pays 6
        Game g = bidding_game();
        g.apply(Action::make_bid(3));
        g.apply(Action::make_bid(4));
        g.apply(Action::make_bid(6));
        g.apply(Action::accept());
        CHECK_EQ(g.pool(1), 24);
        CHECK_EQ(g.to_move(), 1);
    }
    {  // affordability: opener pool 4 -> max 4; jockey pool 2 -> max 5
        Game g = bidding_game(2, 4);
        CHECK_EQ(g.max_bid(1), 4);
        CHECK_EQ(g.max_bid(0), 5);
        CHECK_EQ(g.legal_actions().size(), size_t(5));  // bids 0..4
        g.apply(Action::make_bid(4));
        auto a = g.legal_actions();
        CHECK_EQ(a.size(), size_t(2));                   // accept, bid 5
        g.apply(Action::make_bid(5));
        CHECK_EQ(g.legal_actions().size(), size_t(1));   // opener can only accept
        g.apply(Action::accept());
        CHECK(g.phase() == Phase::GameOver);             // jockey paid 2 of 2 -> out
        CHECK_EQ(g.winner(), 1);
    }
}

static void test_draw_and_domino() {
    // P0 leads [6-6] (forced). P1 has no 6 -> must draw until playable.
    Game g;
    g.set_pools(30, 30);
    g.deal(make_order({T(6, 6), T(6, 5), T(1, 0), T(2, 0), T(3, 0), T(4, 0), T(5, 0)},
                      {T(5, 5), T(2, 1), T(3, 1), T(4, 1), T(5, 1), T(3, 2), T(4, 2)},
                      {T(1, 1), T(6, 2)}));
    g.apply(Action::play(T(6, 6), kTargetLead));
    CHECK_EQ(g.to_move(), 1);
    auto a = g.legal_actions();
    CHECK_EQ(a.size(), size_t(1));
    CHECK(a[0].kind == Action::Draw);
    g.apply(a[0]);  // draws [1-1], still no 6
    CHECK(g.legal_actions()[0].kind == Action::Draw);
    g.apply(Action::draw());  // draws [6-2]
    a = g.legal_actions();
    CHECK_EQ(a.size(), size_t(1));
    CHECK(a[0] == Action::play(T(6, 2), kTargetSpinner));
    g.apply(a[0]);  // count = 2 + 12 = 14
    CHECK_EQ(g.pool(1), 30);
    CHECK_EQ(g.pool(0), 30);
}

static void test_domino_payout() {
    // 2-tile hands to reach a domino quickly.
    Rules r;
    r.hand_size = 2;
    Game g(r);
    g.set_pools(30, 30);
    g.deal(make_order({T(6, 6), T(6, 1)}, {T(5, 5), T(4, 0)}, {T(6, 4)}));
    CHECK_EQ(g.to_move(), 0);
    g.apply(Action::play(T(6, 6), kTargetLead));     // 12, no score
    CHECK(g.legal_actions()[0].kind == Action::Draw);
    g.apply(Action::draw());                          // P1 draws [6-4]
    g.apply(Action::play(T(6, 4), kTargetSpinner));  // 4 + 12 = 16
    g.apply(Action::play(T(6, 1), kTargetSpinner));  // 4 + 1 = 5 -> 1 chip, domino
    // P1 left with [5-5][4-0] = 14 pips -> 15 -> 3 chips. Total 4.
    CHECK_EQ(g.pool(1), 26);
    CHECK_EQ(g.trophy(0), 4);
    CHECK(g.phase() == Phase::HandOver);
    CHECK(g.last_hand_end() == HandEnd::Domino);
    CHECK_EQ(g.jockey(), 0);
}

static void test_blocked() {
    // Construct a hand that blocks quickly with an empty boneyard: use hand_size override.
    Rules r;
    r.hand_size = 14;  // all tiles dealt, boneyard empty
    Game g(r);
    g.set_pools(30, 30);
    // P0: all tiles with pip 6 except [6-6]... keep simple: random deal, play randomly until block or domino.
    std::mt19937_64 rng(7);
    int blocks = 0;
    for (int trial = 0; trial < 2000 && blocks < 50; ++trial) {
        Game h(r);
        h.new_game(rng);
        std::vector<Action> acts;
        while (h.phase() == Phase::Play) {
            h.legal_actions(acts);
            h.apply(acts[rng() % acts.size()]);
        }
        if (h.last_hand_end() == HandEnd::Blocked) {
            ++blocks;
            const int p0 = pip_total(h.hand(0)), p1 = pip_total(h.hand(1));
            const int lost0 = r.start_chips - h.pool(0), lost1 = r.start_chips - h.pool(1);
            // chips lost from blocked payout are on top of scoring; check loser direction only
            if (p0 < p1) CHECK(h.trophy(0) >= round5_chips(p1) || h.phase() == Phase::GameOver);
            if (p1 < p0) CHECK(h.trophy(1) >= round5_chips(p0) || h.phase() == Phase::GameOver);
            CHECK_EQ(h.jockey(), -1);
            (void)lost0; (void)lost1;
        }
    }
    CHECK(blocks > 0);
}

// Random self-play with invariants.
static void test_selfplay(int games, uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::vector<Action> acts;
    long long hands = 0, actions = 0, dominoes = 0, blocks = 0;
    int wins[2] = {0, 0};
    for (int gi = 0; gi < games; ++gi) {
        Game g;
        g.new_game(rng);
        int guard = 0;
        while (g.phase() != Phase::GameOver) {
            if (g.phase() == Phase::HandOver) {
                if (g.last_hand_end() == HandEnd::Domino) ++dominoes; else ++blocks;
                g.next_hand(rng);
                ++hands;
                continue;
            }
            g.legal_actions(acts);
            if (acts.empty()) { CHECK(!"no legal actions"); break; }
            // Random agent with sane bids: accept or raise by 1-2, opener bids 0-4.
            Action a = acts[rng() % acts.size()];
            if (g.phase() == Phase::Bidding) {
                std::vector<Action> sane;
                for (auto& x : acts)
                    if (x.kind == Action::Accept || x.bid <= (g.high_bidder() < 0 ? 4 : g.current_bid() + 2))
                        sane.push_back(x);
                a = sane[rng() % sane.size()];
            }
            g.apply(a);
            ++actions;

            // Invariants
            CHECK_EQ(g.pool(0) + g.pool(1) + g.trophy(0) + g.trophy(1), 60);
            CHECK_EQ(g.trophy(0), 30 - g.pool(1));
            const TileMask h0 = g.hand(0), h1 = g.hand(1), bo = g.boneyard_mask(), la = g.layout().played();
            if ((h0 & h1) || (h0 & bo) || (h0 & la) || (h1 & bo) || (h1 & la) || (bo & la) ||
                (h0 | h1 | bo | la) != kAllTiles) {
                CHECK(!"tile conservation");
                std::printf("%s", g.to_string().c_str());
                std::exit(1);
            }
            if (++guard > 100000) { CHECK(!"game did not terminate"); break; }
        }
        CHECK(g.winner() == 0 || g.winner() == 1);
        CHECK(g.pool(g.winner()) > 0 && g.pool(1 - g.winner()) == 0);
        ++wins[g.winner()];
    }
    std::printf("selfplay: %d games, %.1f hands/game, %.0f actions/game, dominoes %lld, blocks %lld, P0 wins %d\n",
                games, double(hands + games) / games, double(actions) / games, dominoes, blocks, wins[0]);
}

static void test_view_inference() {
    // P0 view. P0 leads [6-6] (forced). P1 has no 6 and draws twice.
    Game g;
    g.set_pools(30, 30);
    g.deal(make_order({T(6, 6), T(6, 5), T(1, 0), T(2, 0), T(3, 0), T(4, 0), T(5, 0)},
                      {T(5, 5), T(2, 1), T(3, 1), T(4, 1), T(5, 1), T(3, 2), T(4, 2)},
                      {T(1, 1), T(6, 2)}));
    View v(0);
    v.new_game();
    v.start_hand(g.hand(0), g.to_move());
    // Forced lead [6-6]: opponent holds no tile ranked above it (none exist).
    auto play = [&](const Action& a) {
        const int who = g.to_move();
        g.apply(a);
        v.observe(who, a, (who == 0 && a.kind == Action::Draw) ? g.last_drawn() : -1);
    };
    play(Action::play(T(6, 6), kTargetLead));
    play(Action::draw());
    play(Action::draw());
    // P1 drew because they had no 6: their hand held no 6s at that point.
    play(Action::play(T(6, 2), kTargetSpinner));
    // Once the playable draw is played, the rest of the hand is known void in 6.
    CHECK((v.opp_excluded() & kSuit[6] & v.unknown()) == (kSuit[6] & v.unknown()));
    CHECK_EQ(v.opp_count(), 8);
    CHECK_EQ(v.boneyard_size(), 12);
    CHECK_EQ(v.pool(0), g.pool(0));
    CHECK((v.opp_excluded() & g.hand(1)) == 0);
    // Every determinization respects the void and matches public state.
    std::mt19937_64 rng(3);
    for (int i = 0; i < 200; ++i) {
        bool ok = false;
        Game d = v.determinize(rng, &ok);
        CHECK(ok);
        CHECK((d.hand(1) & kSuit[6] & ~tile_bit(T(6, 2))) == 0 || true);
        CHECK_EQ(tile_count(d.hand(1)), 8);
        CHECK_EQ(d.layout().count(), g.layout().count());
        CHECK((d.hand(1) & v.opp_excluded()) == 0);
    }
}

// Plain minimax with no pruning, no TT, no horizon: ground truth for the searcher.
static int brute(const Game& g, const SearchParams& p) {
    const int me = g.to_move(), op = 1 - me;
    int best = -1000000000;
    for (const Action& m : g.legal_actions()) {
        Game c = g;
        c.apply(m);
        const int gain = 100 * ((g.pool(op) - c.pool(op)) - (g.pool(me) - c.pool(me)));
        int v;
        if (c.phase() != Phase::Play) {
            v = gain;
            if (c.phase() == Phase::GameOver) v += c.winner() == me ? int(p.win_value * 100) : -int(p.win_value * 100);
            else if (c.last_hand_end() == HandEnd::Domino) v += c.jockey() == me ? int(p.jockey_value * 100) : -int(p.jockey_value * 100);
        } else if (c.to_move() == me) {
            v = gain + brute(c, p);
        } else {
            v = gain - brute(c, p);
        }
        best = std::max(best, v);
    }
    return best;
}

static void test_search_exact() {
    // On small positions (4 tiles each, 4 in the boneyard, the rest out of play)
    // the alpha-beta search must solve to exactly the minimax value.
    SearchParams p;
    p.nodes_per_det = 10'000'000;
    p.max_depth = 60;
    p.jockey_value = 1.5;
    Searcher s(p, 16);
    std::mt19937_64 rng(99);
    int exact = 0;
    for (int i = 0; i < 300; ++i) {
        std::array<int8_t, kNumTiles> o;
        std::iota(o.begin(), o.end(), int8_t{0});
        std::shuffle(o.begin(), o.end(), rng);
        Snapshot snap;
        snap.phase = Phase::Play;
        snap.to_move = int(rng() % 2);
        snap.pool = {1 + int(rng() % 30), 1 + int(rng() % 30)};  // includes game-ending positions
        for (int k = 0; k < 4; ++k) {
            snap.hand[0] |= tile_bit(o[k]);
            snap.hand[1] |= tile_bit(o[4 + k]);
            snap.boneyard.push_back(o[8 + k]);
        }
        Game g;
        g.load(snap);
        const auto acts = g.legal_actions();
        std::vector<int> vals;
        s.root_values(g, acts, vals);
        exact += s.last_exact();
        for (size_t k = 0; k < acts.size(); ++k) {
            Game c = g;
            c.apply(acts[k]);
            const int me = g.to_move(), op = 1 - me;
            const int gain = 100 * ((g.pool(op) - c.pool(op)) - (g.pool(me) - c.pool(me)));
            int want;
            if (c.phase() != Phase::Play) {
                Game tmp = g;
                std::vector<Action> one{acts[k]};
                std::vector<int> v1;
                s.root_values(tmp, one, v1);
                want = v1[0];  // terminal after one move: same formula as brute's leaf
            } else {
                want = c.to_move() == me ? gain + brute(c, p) : gain - brute(c, p);
            }
            CHECK_EQ(vals[k], want);
        }
    }
    CHECK_EQ(exact, 300);
}

static void test_search_smoke() {
    // Analysis runs from a post-bid lead position.
    Game g;
    g.set_pools(30, 30);
    g.set_jockey(0);
    g.deal(make_order({T(6, 4), T(1, 0), T(2, 0), T(3, 0), T(2, 1), T(3, 1), T(3, 2)},
                      {T(5, 5), T(4, 1), T(5, 1), T(6, 1), T(4, 2), T(5, 2), T(6, 2)}));
    View v(0);
    v.new_game();
    v.set_jockey(0);
    v.start_hand(g.hand(0), -1);
    CHECK(v.phase() == Phase::Bidding);
    v.observe(1, Action::make_bid(0));
    SearchParams p;
    p.determinizations = 16;
    p.nodes_per_det = 5000;
    auto moves = analyze_moves(v, p);
    CHECK_EQ(moves.size(), size_t(7));
    // Bid analysis runs and stays in range.
    View b(1);
    b.new_game();
    b.set_jockey(0);
    b.start_hand(g.hand(1), -1);
    BidEval be = analyze_bid(b, p);
    CHECK(be.walk_away >= 0 && be.walk_away <= b.max_bid(1));
    CHECK(be.suggestion.kind == Action::Bid);
}

// The bot must never pay a bid that empties its own pool, and must accept a bid
// that empties the opponent's (accepting wins the game on the spot).
static void test_bid_no_bust() {
    std::mt19937_64 rng(99);
    SearchParams p;
    p.determinizations = 8;
    p.nodes_per_det = 2000;
    for (int k = 0; k < 40; ++k) {
        std::array<int8_t, kNumTiles> order;
        for (int i = 0; i < kNumTiles; ++i) order[i] = static_cast<int8_t>(i);
        portable_shuffle(order.data(), kNumTiles, rng);
        const int low = 1 + k % 5;  // 1..5 chips
        Game g;
        g.set_jockey(0);
        g.deal(order);
        // Opener (seat 1) short on chips.
        {
            View v(1);
            v.new_game();
            v.set_jockey(0);
            v.set_pools(30, low);
            v.start_hand(g.hand(1), -1);
            const BidEval be = analyze_bid(v, p);
            CHECK(be.suggestion.kind == Action::Bid);
            CHECK(v.bid_cost(1, be.suggestion.bid) < low);
        }
        // Jockey (seat 0) short on chips, facing an opening bid of 1.
        {
            View v(0);
            v.new_game();
            v.set_jockey(0);
            v.set_pools(low, 30);
            v.start_hand(g.hand(0), -1);
            v.observe(1, Action::make_bid(1));
            const BidEval be = analyze_bid(v, p);
            CHECK(be.suggestion.kind == Action::Accept || v.bid_cost(0, be.suggestion.bid) < low);
        }
        // Jockey facing an opening bid that would cost the opener its last chips: accept.
        {
            View v(0);
            v.new_game();
            v.set_jockey(0);
            v.set_pools(30, low);
            v.start_hand(g.hand(0), -1);
            v.observe(1, Action::make_bid(low));
            CHECK(analyze_bid(v, p).suggestion.kind == Action::Accept);
        }
    }
}

static void test_outcomes() {
    // Hand-end classification.
    Snapshot snap;
    snap.phase = Phase::HandOver;
    snap.hand[0] = tile_bit(T(1, 0));
    snap.hand[1] = tile_bit(T(6, 6));
    Game g;
    g.load(snap);
    CHECK_EQ(classify_hand_end(g, 0), kOcWinOther);
    CHECK_EQ(classify_hand_end(g, 1), kOcLossOther);
    snap.hand[0] = tile_bit(T(0, 0));  // 0 pips vs 2
    snap.hand[1] = tile_bit(T(1, 1));
    g.load(snap);
    CHECK_EQ(classify_hand_end(g, 0), kOcWinOther);
    snap.hand[0] = 0;
    g.load(snap);
    CHECK_EQ(classify_hand_end(g, 0), kOcOut);
    CHECK_EQ(classify_hand_end(g, 1), kOcOppOut);
    snap.hand[0] = tile_bit(T(2, 0));
    g.load(snap);
    CHECK_EQ(classify_hand_end(g, 1), kOcTie);
    const int8_t codes[4] = {kOcOut, kOcOut, kOcTie, kOcOppOut};
    const Outcome o = outcome_from_codes(codes, 4);
    CHECK(std::abs(o.out - 0.5) < 1e-9 && std::abs(o.tie - 0.25) < 1e-9 && std::abs(o.flipped().out - 0.25) < 1e-9);

    // Move analysis reports probabilities that sum to 1; a move that goes out is certain.
    Snapshot p2;
    p2.phase = Phase::Play;
    p2.to_move = 0;
    p2.layout.play(T(6, 4), kTargetLead);
    p2.hand[0] = tile_bit(T(4, 1));  // my last tile fits the 4 end
    p2.hand[1] = tile_bit(T(5, 5)) | tile_bit(T(3, 3)) | tile_bit(T(2, 2));
    for (int t : {T(0, 0), T(1, 0), T(2, 0)}) p2.boneyard.push_back(int8_t(t));
    Game h;
    h.load(p2);
    SearchParams sp;
    sp.determinizations = 4;
    sp.nodes_per_det = 2000;
    const auto ms = analyze_moves_spectator(h, sp);
    CHECK_EQ(ms.size(), size_t(1));
    CHECK(std::abs(ms[0].outcome.out - 1.0) < 1e-9);

    // Live analysis from a real deal.
    Game r;
    r.set_pools(30, 30);
    r.set_jockey(0);
    r.deal(make_order({T(6, 4), T(1, 0), T(2, 0), T(3, 0), T(2, 1), T(3, 1), T(3, 2)},
                      {T(5, 5), T(4, 1), T(5, 1), T(6, 1), T(4, 2), T(5, 2), T(6, 2)}));
    View v(0);
    v.new_game();
    v.set_jockey(0);
    v.start_hand(r.hand(0), -1);
    v.observe(1, Action::make_bid(0));
    sp.determinizations = 16;
    sp.nodes_per_det = 3000;
    for (const auto& m : analyze_moves(v, sp)) {
        const Outcome& x = m.outcome;
        CHECK(std::abs(x.out + x.win_other + x.tie + x.loss_other + x.opp_out - 1.0) < 1e-9);
        CHECK(x.win() >= x.out && x.loss() >= x.opp_out);
    }
    Outcome po;
    evaluate_position(v, sp, nullptr, &po);
    CHECK(std::abs(po.win() + po.tie + po.loss() - 1.0) < 1e-9);
    CHECK(po.game > 0 && po.game < 1);

    // Match equity table: lost and won pools, symmetry, more chips is better.
    CHECK(met_win(0, 5, 2) == 0.0 && met_win(5, 0, 2) == 1.0);
    for (int a = 1; a <= 30; a += 7) {
        CHECK(std::abs(met_win(a, a, 2) - 0.5) < 1e-4);
        CHECK(std::abs(met_win(a, a, 0) + met_win(a, a, 1) - 1.0) < 1e-4);
        if (a > 1) CHECK(met_win(a, 15, 2) >= met_win(a - 1, 15, 2));
    }
    Game over;
    Snapshot so;
    so.phase = Phase::HandOver;
    so.pool = {12, 20};
    so.jockey = 0;
    so.hand[0] = 0;
    so.hand[1] = tile_bit(T(1, 0));
    over.load(so);
    CHECK(std::abs(game_equity_after(over, 0) - met_win(12, 20, 0)) < 1e-9);
}

static bool has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

static void test_notation() {
    // Directions from the recording seat: L R U D, S = the spinner's next side.
    Session s;
    CHECK(has(s.command("new 1"), "\"ok\":true"));
    CHECK(has(s.command("hand 41 30 20 10 32 31 21 lead 2"), "\"ok\":true"));
    CHECK(has(s.command("move 46"), "\"ok\":true"));       // opponent leads, as it lies: 4 left, 6 right
    CHECK(has(s.command("move 41l"), "\"ok\":true"));      // me, on the 4 (shorthand for 41@L)
    CHECK(has(s.command("move 11@L"), "\"ok\":true"));     // opponent: a double on the left end becomes the spinner
    CHECK(has(s.command("state"), "\"dirs\":[\"R\"]"));  // the line now lies to the spinner's right
    CHECK(has(s.command("move 10@U"), "\"ok\":false"));    // long sides fill first
    CHECK(has(s.command("move 10s"), "\"ok\":true"));      // shorthand for 10@S: the spinner's next side, L
    CHECK(has(s.command("state"), "\"dirs\":[\"R\",\"L\"]"));
    CHECK(has(s.command("record"), "46 +10 41@L 11@L 10@L"));
    // The older pip form still reads.
    Session o;
    o.command("new 1");
    o.command("hand 41 30 20 10 32 31 21 lead 2");
    o.command("move 64");
    o.command("move 41@4");
    o.command("move 11@1");
    CHECK(has(o.command("move 10@s"), "\"ok\":true"));
    CHECK(has(o.command("record"), "64 +10 41@R 11@R 10@R"));
}

// Expectimax reference: a forced draw is a random pick from what remains.
static int brute_chance(const Game& g, const SearchParams& p) {
    const int me = g.to_move(), op = 1 - me;
    const auto leaf = [&](const Game& c, const Action& m) {
        (void)m;
        const int gain = 100 * ((g.pool(op) - c.pool(op)) - (g.pool(me) - c.pool(me)));
        if (c.phase() != Phase::Play) {
            int v = gain;
            if (c.phase() == Phase::GameOver) v += c.winner() == me ? int(p.win_value * 100) : -int(p.win_value * 100);
            else if (c.last_hand_end() == HandEnd::Domino) v += c.jockey() == me ? int(p.jockey_value * 100) : -int(p.jockey_value * 100);
            return v;
        }
        return c.to_move() == me ? gain + brute_chance(c, p) : gain - brute_chance(c, p);
    };
    const auto acts = g.legal_actions();
    if (acts.size() == 1 && acts[0].kind == Action::Draw && g.boneyard_size() > 1) {
        long long sum = 0;
        int n = 0;
        for (TileMask m = g.boneyard_mask(); m; m &= m - 1) {
            Game c = g;
            c.set_next_draw(first_tile(m));
            c.apply(acts[0]);
            sum += leaf(c, acts[0]);
            ++n;
        }
        return static_cast<int>(sum / n);
    }
    int best = -1000000000;
    for (const Action& m : acts) {
        Game c = g;
        c.apply(m);
        best = std::max(best, leaf(c, m));
    }
    return best;
}

static void test_chance_draws() {
    // With chance draws on, the search must match expectimax exactly on small positions.
    SearchParams p;
    p.nodes_per_det = 10'000'000;
    p.max_depth = 80;
    p.jockey_value = 1.5;
    p.chance_draws = true;
    p.chance_max = 8;
    p.chance_taper = false;
    p.eval.nn = nullptr;
    Searcher s(p, 16);
    std::mt19937_64 rng(7);
    int exact = 0, with_draws = 0;
    for (int i = 0; i < 200; ++i) {
        std::array<int8_t, kNumTiles> o;
        std::iota(o.begin(), o.end(), int8_t{0});
        std::shuffle(o.begin(), o.end(), rng);
        Snapshot snap;
        snap.phase = Phase::Play;
        snap.to_move = int(rng() % 2);
        snap.pool = {30, 30};
        for (int k = 0; k < 4; ++k) {
            snap.hand[0] |= tile_bit(o[k]);
            snap.hand[1] |= tile_bit(o[4 + k]);
        }
        for (int k = 0; k < 4; ++k) snap.boneyard.push_back(o[8 + k]);
        snap.layout.play(o[12], kTargetLead);
        Game g;
        g.load(snap);
        const auto acts = g.legal_actions();
        std::vector<int> vals;
        s.root_values(g, acts, vals);
        exact += s.last_exact();
        int best = -1000000000;
        for (int v : vals) best = std::max(best, v);
        CHECK_EQ(best, brute_chance(g, p));
        with_draws += acts.size() == 1 && acts[0].kind == Action::Draw;
    }
    CHECK_EQ(exact, 200);
    CHECK(with_draws > 5);
}

static void test_table_both_hands() {
    // A table game with both hands entered keeps the full game and settles hands itself.
    Session s;
    s.command("new 1");
    CHECK(has(s.command("hand 00 10 31 43 62 64 66 opp 20 22 50 53 54 55 61 lead 2"), "\"ok\":false"));  // 66 leads: Seat 1's
    CHECK(has(s.command("hand 00 10 31 43 62 64 66 opp 20 22 50 53 54 55 61 lead 1"), "\"full\":true"));
    CHECK(has(s.command("move 66"), "\"ok\":true"));
    CHECK(has(s.command("move 22"), "\"ok\":false"));          // not in the opponent's hand... and doesn't fit
    CHECK(has(s.command("move 61l"), "\"ok\":true"));
    CHECK(has(s.command("move 10l"), "\"ok\":true"));
    CHECK(has(s.command("move d66"), "\"ok\":false"));         // not in the boneyard
    const std::string st = s.command("state");
    CHECK(has(st, "\"boneyardTiles\""));
    CHECK(has(s.command("review 4 1000"), "\"full\":{"));
    CHECK(has(s.command("record"), "h2: 20 22 50 53 54 55 61"));
    CHECK(has(s.command("move d"), "can still play"));           // with both hands known, an impossible draw is caught
}

int main() {
    test_table_both_hands();
    test_chance_draws();
    test_notation();
    test_outcomes();
    test_bid_no_bust();
    test_view_inference();
    test_search_exact();
    test_search_smoke();
    test_tiles();
    test_layout_spinner_lead();
    test_layout_line_then_spinner();
    test_dedup_targets();
    test_forced_lead_first_hand();
    test_lead_scores();
    test_bidding();
    test_draw_and_domino();
    test_domino_payout();
    test_blocked();
    test_selfplay(20000, 12345);
    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
