#pragma once
// Move search, Stockfish-style, adapted to hidden information.
//
// Stockfish searches one fully known position. Here the opponent's hand and the
// boneyard order are hidden, so the engine uses Perfect Information Monte Carlo
// (PIMC), the method behind strong bridge and skat engines:
//   1. Sample many determinizations (full deals consistent with the player's view
//      and inference).
//   2. In each one, run a Stockfish-style alpha-beta search: negamax, iterative
//      deepening, transposition table, move ordering (TT move, then scoring plays,
//      then heavy tiles), and a static evaluation at the horizon. Draws and passes
//      do not consume depth (like Stockfish's extensions for forced moves).
//   3. Average each root move's value across determinizations.
//
// Scores are in centichips (100 = 1 chip) and measure net chips won from this point
// to the end of the current hand, from the searching player's point of view.
// Known limitation of PIMC: inside one determinization the search "knows" the
// opponent's tiles and the draw order (strategy fusion). Averaging over many
// samples is the standard mitigation; information-set search can replace it later.

#include <cstdint>
#include <random>
#include <vector>

#include "models.h"

namespace sb {

struct EvalParams {
    // Static evaluation at the search horizon, centichips, side-to-move view.
    // With a network (NNUE) it is used; otherwise the classic formula below.
    const NNWeights* nn = models().eval;
    int pip_weight = 8;     // classic: per pip of difference (opponent pips - my pips)
    int tile_weight = 40;   // classic: per tile of difference (opponent tiles - my tiles)
};

struct SearchParams {
    int determinizations = 64;
    int64_t nodes_per_det = 50'000;  // soft budget for iterative deepening
    int max_depth = 40;
    double jockey_value = 0.0;  // chips credited for dominoing (worth of the jockey seat)
    double win_value = 60.0;    // chips credited for winning the game mid-hand
    // PIMC overrates the lead (each sampled deal is searched with every tile visible).
    // Self-play measured the real lead value at about 0.4x the raw estimate, so bid
    // decisions shrink the gap between "I lead" and "they lead" by this factor.
    double bid_calibration = 0.4;
    // Learned models (defaults: whatever the front end loaded; see models.h).
    const Mlp* bid_model = models().bid;    // replaces the calibrated PIMC lead value
    const Mlp* info_model = models().info;  // blended into root move values
    float info_weight = models().info_weight;
    EvalParams eval;
    uint64_t seed = 1;
    // Training only (sbtrain): called with every position the search evaluates at its
    // horizon, i.e. exactly the positions where the NNUE decides a value. Null in play.
    void (*leaf_hook)(const Game&) = nullptr;
};

// How the hand ends, as probabilities from one player's side. Each sampled deal follows
// its search's best line to the end of the hand (transposition-table moves, then a
// greedy playout past the horizon) and records the result. These are probabilities
// under chip-maximizing play by both sides, with the same PIMC optimism as the chip
// values.
struct Outcome {
    double out = 0;         // I go out (domino)
    double win_other = 0;   // I win a blocked hand, or the opponent runs out of chips first
    double tie = 0;         // blocked hand with equal pips
    double loss_other = 0;  // mirror of win_other
    double opp_out = 0;     // the opponent goes out
    double game = 0.5;      // I win the GAME: each line's end state looked up in the match equity table (met.h)
    double win() const { return out + win_other; }
    double loss() const { return opp_out + loss_other; }
    Outcome flipped() const { return {opp_out, loss_other, tie, win_other, out, 1.0 - game}; }
};

// Outcome codes for one finished hand, from a player's side.
enum OutcomeCode : int8_t { kOcOut = 0, kOcWinOther, kOcTie, kOcLossOther, kOcOppOut };
// Classify a finished hand (phase HandOver or GameOver) for player p.
int8_t classify_hand_end(const Game& c, int p);
// P(player p wins the game) at a finished hand: 1/0 if the game is over, else the table.
double game_equity_after(const Game& c, int p);
Outcome outcome_from_codes(const int8_t* codes, int n, int stride = 1, const float* game = nullptr);

struct MoveEval {
    Action action;
    double ev = 0;  // chips
    double se = 0;  // standard error across determinizations
    int n = 0;
    Outcome outcome;  // for the player making the move
};

struct SearchStats {
    int64_t nodes = 0;
    double avg_depth = 0;
    int exact = 0;       // determinizations solved to the end of the hand
    int fallbacks = 0;   // determinizations that could not honor inference
};

struct BidEval {
    double ev_i_lead = 0;     // chips, rest of hand, if I lead
    double ev_opp_leads = 0;  // chips, rest of hand, if opponent leads
    double lead_value = 0;    // difference
    Outcome if_i_lead;        // hand outcome for me if I lead (my best lead)
    Outcome if_opp_leads;     // hand outcome for me if the opponent leads
    int walk_away = 0;        // highest bid worth reaching in a bidding war
    Action suggestion;
};

// Alpha-beta searcher over one perfect-information Game.
class Searcher {
public:
    explicit Searcher(const SearchParams& p, int tt_bits = 20);

    // Values (centichips, for the player to move in g) of every action in `acts`,
    // by iterative deepening until solved or the node budget is spent.
    void root_values(const Game& g, const std::vector<Action>& acts, std::vector<int>& values);

    // After root_values on a position: follow this search's best line from `after`
    // (a child of that position) to the end of the hand and classify it for player p.
    // Uses the transposition table's moves where it has them, then greedy play.
    int8_t follow_line(Game after, int p, float* game = nullptr);

    int64_t nodes() const { return total_nodes_; }
    int last_depth() const { return last_depth_; }
    bool last_exact() const { return last_exact_; }

private:
    struct TTEntry {
        uint64_t key = 0;
        int32_t value = 0;
        int8_t depth = -1;
        int8_t bound = 0;  // 1 exact, 2 lower, 3 upper
        int8_t tile = -1;
        int8_t target = -1;
    };

    int negamax(const Game& g, int depth, int alpha, int beta, int ply);
    int child_value(const Game& g, const Game& c, const Action& m, int depth, int alpha, int beta, int ply);
    int terminal(const Game& c, int p) const;
    int evaluate(const Game& g) const;
    uint64_t hash(const Game& g) const;
    void order(const Game& g, std::vector<Action>& moves, int tt_tile, int tt_target) const;

    SearchParams p_;
    std::vector<TTEntry> tt_;
    uint64_t tt_mask_;
    uint64_t salt_ = 0;
    int64_t nodes_ = 0;
    int64_t total_nodes_ = 0;
    int64_t hard_limit_ = 0;
    bool stop_ = false;
    bool hit_horizon_ = false;
    int last_depth_ = 0;
    bool last_exact_ = false;
    std::vector<std::vector<Action>> move_stack_;
};

// Rank my legal moves (my turn, play phase). Sorted best first.
std::vector<MoveEval> analyze_moves(const View& v, const SearchParams& p, SearchStats* stats = nullptr);

// Value of the lead in a bidding phase and a suggested bid action.
BidEval analyze_bid(const View& v, const SearchParams& p, SearchStats* stats = nullptr);

// Position evaluation in chips (rest of the hand, my point of view), whoever is
// to move. Play phase only.
double evaluate_position(const View& v, const SearchParams& p, SearchStats* stats = nullptr, Outcome* outcome = nullptr);

// Spectator evaluation of a fully known deal (both hands visible, boneyard order
// sampled), in chips from Seat 1's side. Play phase: the position value. Bidding:
// the value if Seat 1 leads and if Seat 2 leads.
// `outcome` is from Seat 1's side.
double evaluate_spectator(const Game& g, const SearchParams& p, SearchStats* stats = nullptr, Outcome* outcome = nullptr);
void evaluate_spectator_bid(const Game& g, const SearchParams& p, double& seat1_leads, double& seat2_leads,
                            SearchStats* stats = nullptr, Outcome* seat1_leads_outcome = nullptr,
                            Outcome* seat2_leads_outcome = nullptr);
// Spectator move ranking (both hands visible, boneyard order sampled) for the player to
// move: values and outcomes from that player's side, best first. Play phase only.
std::vector<MoveEval> analyze_moves_spectator(const Game& g, const SearchParams& p, SearchStats* stats = nullptr);

// Engine decision for whatever phase the view is in (my turn).
Action choose_action(const View& v, const SearchParams& p);

}  // namespace sb
