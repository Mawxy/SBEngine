#include "view.h"

#include <algorithm>
#include "fail.h"

namespace sb {

int lead_key(int t) { return is_double(t) ? 1000 + tile_hi(t) : tile_pips(t) * 10 + tile_hi(t); }

TileMask tiles_above_key(int key) {
    TileMask m = 0;
    for (int t = 0; t < kNumTiles; ++t)
        if (lead_key(t) > key) m |= tile_bit(t);
    return m;
}

static int max_key_tile(TileMask m) {
    int best = -1, key = -1;
    for (; m; m &= m - 1) {
        const int t = first_tile(m);
        if (lead_key(t) > key) {
            key = lead_key(t);
            best = t;
        }
    }
    return best;
}

// Uniformly random set bit of m (m != 0).
static int random_tile(TileMask m, std::mt19937_64& rng) {
    int k = static_cast<int>(rng() % static_cast<uint64_t>(tile_count(m)));
    for (; k > 0; --k) m &= m - 1;
    return first_tile(m);
}

View::View(int me, Rules r) : rules_(r), me_(me) {}

void View::new_game() {
    pool_ = {rules_.start_chips, rules_.start_chips};
    jockey_ = -1;
    winner_ = -1;
    hand_number_ = 0;
    phase_ = Phase::HandOver;
    last_end_ = HandEnd::None;
}

void View::start_hand(TileMask my_hand, int leader) {
    if (tile_count(my_hand) != rules_.hand_size) fail("start_hand: wrong hand size");
    my_hand_ = my_hand;
    layout_ = Layout{};
    bone_count_ = kNumTiles - 2 * rules_.hand_size;
    groups_.assign(1, Group{rules_.hand_size, my_hand});
    bid_ = 0;
    high_bidder_ = -1;
    forced_lead_ = -1;
    opp_forced_ = false;
    passes_ = 0;
    last_end_ = HandEnd::None;
    ++hand_number_;

    if (jockey_ >= 0) {
        phase_ = Phase::Bidding;
        to_move_ = 1 - jockey_;
        return;
    }
    phase_ = Phase::Play;
    to_move_ = leader;
    if (leader == me_) {
        forced_lead_ = max_key_tile(my_hand_);
        groups_[0].forbid |= tiles_above_key(lead_key(forced_lead_));
    } else {
        opp_forced_ = true;
    }
}

int View::opp_count() const {
    int n = 0;
    for (const auto& g : groups_) n += g.count;
    return n;
}

TileMask View::opp_excluded() const {
    TileMask m = kAllTiles;
    for (const auto& g : groups_)
        if (g.count > 0) m &= g.forbid;
    return m & unknown();
}

int View::bid_cost(int player, int bid) const {
    return player == jockey_ ? std::max(0, bid - rules_.jockey_credit) : bid;
}

int View::max_bid(int player) const {
    return player == jockey_ ? pool_[player] + rules_.jockey_credit : pool_[player];
}

void View::pay(int payer, int chips) {
    if (chips <= 0) return;
    pool_[payer] -= std::min(chips, pool_[payer]);
    if (pool_[payer] == 0) {
        phase_ = Phase::GameOver;
        winner_ = 1 - payer;
    }
}

void View::merge_groups() {
    std::vector<Group> out;
    for (const auto& g : groups_) {
        if (g.count <= 0) continue;
        bool merged = false;
        for (auto& o : out)
            if (o.forbid == g.forbid) {
                o.count += g.count;
                merged = true;
                break;
            }
        if (!merged) out.push_back(g);
    }
    groups_ = std::move(out);
}

void View::void_opp(TileMask tiles) {
    for (auto& g : groups_) g.forbid |= tiles;
    merge_groups();
}

void View::add_group(int count, TileMask forbid) {
    groups_.push_back(Group{count, forbid | my_hand_ | layout_.played()});
    merge_groups();
}

void View::remove_from_groups(int t) {
    int best = -1;
    for (int i = 0; i < static_cast<int>(groups_.size()); ++i) {
        if (groups_[i].count <= 0 || (groups_[i].forbid & tile_bit(t))) continue;
        if (best < 0 || tile_count(groups_[i].forbid) > tile_count(groups_[best].forbid)) best = i;
    }
    if (best < 0) {
        // Inconsistent with inference (input error or a missed event): drop the
        // loosest group's slot and clear the contradiction.
        for (int i = 0; i < static_cast<int>(groups_.size()); ++i)
            if (groups_[i].count > 0 && (best < 0 || tile_count(groups_[i].forbid) < tile_count(groups_[best].forbid)))
                best = i;
        if (best < 0) fail("opponent has no tiles");
        for (auto& g : groups_) g.forbid &= ~tile_bit(t);
    }
    --groups_[best].count;
    merge_groups();
}

void View::observe(int player, const Action& a, int drawn_tile) {
    const int other = 1 - player;
    switch (a.kind) {
        case Action::Bid: {
            if (phase_ != Phase::Bidding) fail("view: bid outside bidding");
            if (high_bidder_ < 0 && a.bid == 0) {
                phase_ = Phase::Play;
                to_move_ = jockey_;
                return;
            }
            bid_ = a.bid;
            high_bidder_ = player;
            to_move_ = other;
            return;
        }
        case Action::Accept: {
            const int w = high_bidder_;
            pay(w, bid_cost(w, bid_));
            if (phase_ == Phase::GameOver) return;
            phase_ = Phase::Play;
            to_move_ = w;
            return;
        }
        case Action::Draw: {
            if (bone_count_ <= 0) fail("view: draw from empty boneyard");
            --bone_count_;
            if (player == me_) {
                if (drawn_tile < 0) fail("view: my draw needs the tile");
                my_hand_ |= tile_bit(drawn_tile);
                for (auto& g : groups_) g.forbid |= tile_bit(drawn_tile);
                merge_groups();
            } else {
                void_opp(layout_.playable_mask());
                add_group(1, 0);
            }
            passes_ = 0;
            return;
        }
        case Action::Pass: {
            if (player == opp()) void_opp(layout_.playable_mask());
            to_move_ = other;
            if (++passes_ >= 2) end_hand(HandEnd::Blocked, -1);
            return;
        }
        case Action::Play: {
            const int t = a.tile;
            if (player == me_) {
                if (!(my_hand_ & tile_bit(t))) fail("view: tile not in my hand");
                my_hand_ &= ~tile_bit(t);
            } else {
                if (opp_forced_ && layout_.empty()) groups_[0].forbid |= tiles_above_key(lead_key(t));
                remove_from_groups(t);
                for (auto& g : groups_) g.forbid |= tile_bit(t);
            }
            opp_forced_ = false;
            forced_lead_ = -1;
            layout_.play(t, a.target);
            passes_ = 0;

            const int c = layout_.count();
            if (c > 0 && c % 5 == 0) pay(other, c / 5);
            if (phase_ == Phase::GameOver) return;

            const bool out = player == me_ ? my_hand_ == 0 : opp_count() == 0;
            if (out) {
                // Opponent's domino can be settled now (my pips are known).
                if (player == opp()) pay(me_, round5_chips(pip_total(my_hand_)));
                end_hand(HandEnd::Domino, player);
                return;
            }
            to_move_ = other;
            return;
        }
    }
}

void View::end_hand(HandEnd e, int jockey) {
    last_end_ = e;
    jockey_ = e == HandEnd::Domino ? jockey : -1;
    if (phase_ != Phase::GameOver) phase_ = Phase::HandOver;
}

void View::settle(TileMask opp_revealed) {
    const int mine = pip_total(my_hand_), theirs = pip_total(opp_revealed);
    if (last_end_ == HandEnd::Domino && jockey_ == me_) {
        pay(opp(), round5_chips(theirs));
    } else if (last_end_ == HandEnd::Blocked) {
        if (mine < theirs) pay(opp(), round5_chips(theirs));
        else if (theirs < mine) pay(me_, round5_chips(mine));
    }
}

void View::set_pools(int p0, int p1) {
    pool_ = {p0, p1};
    if (p0 <= 0 || p1 <= 0) {
        phase_ = Phase::GameOver;
        winner_ = p0 <= 0 ? 1 : 0;
    }
}

Game View::determinize(std::mt19937_64& rng, bool* ok) const {
    const TileMask unk = unknown();
    const int n_opp = opp_count();

    // Most constrained groups first.
    std::vector<Group> gs = groups_;
    std::sort(gs.begin(), gs.end(),
              [](const Group& a, const Group& b) { return tile_count(a.forbid) > tile_count(b.forbid); });

    TileMask opp_hand = 0;
    bool good = false;
    for (int attempt = 0; attempt < 64 && !good; ++attempt) {
        TileMask avail = unk;
        opp_hand = 0;
        good = true;
        for (const auto& g : gs) {
            for (int k = 0; k < g.count; ++k) {
                const TileMask allowed = avail & ~g.forbid;
                if (!allowed) {
                    good = false;
                    break;
                }
                const int t = random_tile(allowed, rng);
                opp_hand |= tile_bit(t);
                avail &= ~tile_bit(t);
            }
            if (!good) break;
        }
        if (good && opp_forced_ && layout_.empty())
            good = lead_key(max_key_tile(opp_hand)) > lead_key(max_key_tile(my_hand_));
    }
    if (!good) {
        // Fall back to an unconstrained sample.
        TileMask avail = unk;
        opp_hand = 0;
        for (int k = 0; k < n_opp; ++k) {
            const int t = random_tile(avail, rng);
            opp_hand |= tile_bit(t);
            avail &= ~tile_bit(t);
        }
    }
    if (ok) *ok = good;

    Snapshot s;
    s.phase = phase_;
    s.to_move = to_move_;
    s.jockey = jockey_;
    s.hand_number = hand_number_;
    s.pool = pool_;
    s.hand[me_] = my_hand_;
    s.hand[opp()] = opp_hand;
    for (TileMask m = unk & ~opp_hand; m; m &= m - 1) s.boneyard.push_back(static_cast<int8_t>(first_tile(m)));
    portable_shuffle(s.boneyard.data(), static_cast<int>(s.boneyard.size()), rng);
    s.layout = layout_;
    s.bid = bid_;
    s.high_bidder = high_bidder_;
    if (phase_ == Phase::Play && layout_.empty()) {
        if (forced_lead_ >= 0) s.forced_lead = forced_lead_;
        else if (opp_forced_) s.forced_lead = max_key_tile(opp_hand);
    }
    Game g(rules_);
    g.load(s);
    return g;
}

std::vector<Action> View::legal_actions() const {
    std::mt19937_64 rng(0);
    return determinize(rng).legal_actions();
}

std::string View::to_string() const {
    static const char* kPhase[] = {"Bidding", "Play", "HandOver", "GameOver"};
    std::string s = "P" + std::to_string(me_) + " view, hand " + std::to_string(hand_number_) + ", " +
                    kPhase[static_cast<int>(phase_)] + ", to move P" + std::to_string(to_move_) + "\n";
    s += "pools: me " + std::to_string(pool_[me_]) + ", opp " + std::to_string(pool_[opp()]);
    if (jockey_ >= 0) s += jockey_ == me_ ? " (I am jockey)" : " (opp is jockey)";
    s += "\nmy hand: " + mask_str(my_hand_) + "\n";
    s += "opp tiles " + std::to_string(opp_count()) + ", boneyard " + std::to_string(bone_count_) + "\n";
    s += "layout: " + layout_.to_string() + "\n";
    const TileMask ex = opp_excluded();
    if (ex) s += "opp cannot hold: " + mask_str(ex) + "\n";
    return s;
}

}  // namespace sb
