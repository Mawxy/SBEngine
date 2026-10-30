#include "game.h"

#include <algorithm>
#include <numeric>
#include "fail.h"

namespace sb {

std::string to_string(const Action& a) {
    switch (a.kind) {
        case Action::Play: {
            std::string where = a.target == kTargetLead      ? "lead"
                                : a.target == kTargetSpinner ? "spinner"
                                                             : "arm" + std::to_string(a.target);
            return "play " + tile_str(a.tile) + " " + where;
        }
        case Action::Draw: return "draw";
        case Action::Pass: return "pass";
        case Action::Bid: return "bid " + std::to_string(a.bid);
        case Action::Accept: return "accept";
    }
    return "?";
}

void Game::new_game(std::mt19937_64& rng) {
    pool_ = {rules_.start_chips, rules_.start_chips};
    trophy_ = {0, 0};
    jockey_ = -1;
    winner_ = -1;
    hand_number_ = 0;
    last_end_ = HandEnd::None;
    phase_ = Phase::HandOver;
    next_hand(rng);
}

void Game::next_hand(std::mt19937_64& rng) {
    if (phase_ != Phase::HandOver) fail("next_hand: hand not over");
    std::array<int8_t, kNumTiles> order;
    std::iota(order.begin(), order.end(), int8_t{0});
    portable_shuffle(order.data(), kNumTiles, rng);
    deal(order);
}

void Game::deal(const std::array<int8_t, kNumTiles>& order) {
    const int n = rules_.hand_size;
    hand_ = {0, 0};
    for (int i = 0; i < n; ++i) hand_[0] |= tile_bit(order[i]);
    for (int i = n; i < 2 * n; ++i) hand_[1] |= tile_bit(order[i]);
    bone_pos_ = 0;
    bone_end_ = kNumTiles - 2 * n;
    for (int i = 0; i < bone_end_; ++i) boneyard_[i] = order[2 * n + i];
    layout_ = Layout{};
    ++hand_number_;
    begin_hand();
}

void Game::begin_hand() {
    bid_ = 0;
    high_bidder_ = -1;
    forced_lead_ = -1;
    if (jockey_ >= 0) {
        // Bidding: the loser of the last hand acts first.
        phase_ = Phase::Bidding;
        to_move_ = 1 - jockey_;
        return;
    }
    // Highest double leads and must lead it; if no doubles, heaviest tile
    // (tie broken by highest single pip) leads and must be led.
    int best_key = -1;
    for (int p = 0; p < 2; ++p)
        for (TileMask m = hand_[p]; m; m &= m - 1) {
            const int t = first_tile(m);
            const int key = is_double(t) ? 1000 + tile_hi(t) : tile_pips(t) * 10 + tile_hi(t);
            if (key > best_key) {
                best_key = key;
                forced_lead_ = t;
                to_move_ = p;
            }
        }
    phase_ = Phase::Play;
}

bool Game::set_next_draw(int t) {
    for (int i = bone_pos_; i < bone_end_; ++i)
        if (boneyard_[i] == t) {
            std::swap(boneyard_[i], boneyard_[bone_pos_]);
            return true;
        }
    return false;
}

TileMask Game::boneyard_mask() const {
    TileMask m = 0;
    for (int i = bone_pos_; i < bone_end_; ++i) m |= tile_bit(boneyard_[i]);
    return m;
}

int Game::bid_cost(int player, int bid) const {
    return player == jockey_ ? std::max(0, bid - rules_.jockey_credit) : bid;
}

int Game::max_bid(int player) const {
    return player == jockey_ ? pool_[player] + rules_.jockey_credit : pool_[player];
}

bool Game::can_play(int player) const { return (hand_[player] & layout_.playable_mask()) != 0; }

void Game::legal_actions(std::vector<Action>& out) const {
    out.clear();
    if (phase_ == Phase::Bidding) {
        const int hi = max_bid(to_move_);
        if (high_bidder_ < 0) {
            for (int b = 0; b <= hi; ++b) out.push_back(Action::make_bid(b));
        } else {
            out.push_back(Action::accept());
            for (int b = bid_ + 1; b <= hi; ++b) out.push_back(Action::make_bid(b));
        }
        return;
    }
    if (phase_ != Phase::Play) return;

    if (layout_.empty()) {
        if (forced_lead_ >= 0) {
            out.push_back(Action::play(forced_lead_, kTargetLead));
        } else {
            for (TileMask m = hand_[to_move_]; m; m &= m - 1) out.push_back(Action::play(first_tile(m), kTargetLead));
        }
        return;
    }

    std::vector<int8_t> tg;
    for (TileMask m = hand_[to_move_] & layout_.playable_mask(); m; m &= m - 1) {
        const int t = first_tile(m);
        tg.clear();
        layout_.targets(t, tg);
        for (int8_t x : tg) out.push_back(Action::play(t, x));
    }
    if (!out.empty()) return;
    out.push_back(boneyard_size() > 0 ? Action::draw() : Action::pass());
}

std::vector<Action> Game::legal_actions() const {
    std::vector<Action> v;
    legal_actions(v);
    return v;
}

void Game::pay(int payer, int chips) {
    if (chips <= 0) return;
    const int amt = std::min(chips, pool_[payer]);
    pool_[payer] -= amt;
    trophy_[1 - payer] += amt;
    if (pool_[payer] == 0) {
        phase_ = Phase::GameOver;
        winner_ = 1 - payer;
    }
}

void Game::apply(const Action& a) {
    const int me = to_move_;
    const int opp = 1 - me;

    switch (a.kind) {
        case Action::Bid: {
            if (phase_ != Phase::Bidding) fail("bid outside bidding");
            if (high_bidder_ < 0 && a.bid == 0) {  // opener passes
                phase_ = Phase::Play;
                to_move_ = jockey_;
                return;
            }
            const int lo = high_bidder_ < 0 ? 0 : bid_ + 1;
            if (a.bid < lo || a.bid > max_bid(me)) fail("illegal bid");
            bid_ = a.bid;
            high_bidder_ = me;
            to_move_ = opp;
            return;
        }
        case Action::Accept: {
            if (phase_ != Phase::Bidding || high_bidder_ < 0) fail("illegal accept");
            const int w = high_bidder_;
            pay(w, bid_cost(w, bid_));
            if (phase_ == Phase::GameOver) return;
            phase_ = Phase::Play;
            to_move_ = w;
            return;
        }
        case Action::Draw: {
            if (phase_ != Phase::Play || can_play(me) || boneyard_size() == 0) fail("illegal draw");
            last_drawn_ = boneyard_[bone_pos_++];
            hand_[me] |= tile_bit(last_drawn_);
            check_block();
            return;
        }
        case Action::Pass: {
            if (phase_ != Phase::Play || can_play(me) || boneyard_size() > 0) fail("illegal pass");
            to_move_ = opp;
            check_block();
            return;
        }
        case Action::Play: {
            if (phase_ != Phase::Play) fail("play outside play phase");
            const int t = a.tile;
            if (!(hand_[me] & tile_bit(t))) fail("tile not in hand");
            if (layout_.empty() && forced_lead_ >= 0 && t != forced_lead_) fail("must lead forced tile");
            layout_.play(t, a.target);
            hand_[me] &= ~tile_bit(t);
            forced_lead_ = -1;

            const int c = layout_.count();
            if (c > 0 && c % 5 == 0) pay(opp, c / 5);
            if (phase_ == Phase::GameOver) return;

            if (hand_[me] == 0) {  // domino
                pay(opp, round5_chips(pip_total(hand_[opp])));
                jockey_ = me;
                last_end_ = HandEnd::Domino;
                if (phase_ != Phase::GameOver) phase_ = Phase::HandOver;
                return;
            }
            to_move_ = opp;
            check_block();
            return;
        }
    }
}

void Game::check_block() {
    if (phase_ == Phase::Play && boneyard_size() == 0 && !can_play(0) && !can_play(1)) resolve_block();
}

void Game::resolve_block() {
    const int p0 = pip_total(hand_[0]);
    const int p1 = pip_total(hand_[1]);
    if (p0 < p1) pay(1, round5_chips(p1));
    else if (p1 < p0) pay(0, round5_chips(p0));
    jockey_ = -1;
    last_end_ = HandEnd::Blocked;
    if (phase_ != Phase::GameOver) phase_ = Phase::HandOver;
}

void Game::load(const Snapshot& s) {
    phase_ = s.phase;
    to_move_ = s.to_move;
    jockey_ = s.jockey;
    hand_number_ = s.hand_number;
    winner_ = -1;
    set_pools(s.pool[0], s.pool[1]);
    hand_ = s.hand;
    bone_pos_ = 0;
    bone_end_ = static_cast<int>(s.boneyard.size());
    for (int i = 0; i < bone_end_; ++i) boneyard_[i] = s.boneyard[i];
    layout_ = s.layout;
    bid_ = s.bid;
    high_bidder_ = s.high_bidder;
    forced_lead_ = s.forced_lead;
    last_end_ = HandEnd::None;
    last_drawn_ = -1;
}

void Game::force_leader(int p) {
    phase_ = Phase::Play;
    to_move_ = p;
    forced_lead_ = -1;
    bid_ = 0;
    high_bidder_ = -1;
}

void Game::set_pools(int p0, int p1) {
    pool_ = {p0, p1};
    trophy_ = {rules_.start_chips - p1, rules_.start_chips - p0};
}

std::string Game::to_string() const {
    static const char* kPhase[] = {"Bidding", "Play", "HandOver", "GameOver"};
    std::string s = "hand " + std::to_string(hand_number_) + "  phase " + kPhase[static_cast<int>(phase_)] +
                    "  to_move P" + std::to_string(to_move_) + "\n";
    for (int p = 0; p < 2; ++p)
        s += "P" + std::to_string(p) + " pool " + std::to_string(pool_[p]) + " trophy " + std::to_string(trophy_[p]) +
             (jockey_ == p ? " (jockey)" : "") + "  hand: " + mask_str(hand_[p]) + "\n";
    s += "boneyard " + std::to_string(boneyard_size()) + "  layout: " + layout_.to_string() + "\n";
    if (phase_ == Phase::Bidding && high_bidder_ >= 0)
        s += "bid " + std::to_string(bid_) + " by P" + std::to_string(high_bidder_) + "\n";
    return s;
}

}  // namespace sb
