#include "session.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <sstream>

namespace sb {

// ---------------------------------------------------------------- notation

std::string tile_code(int t) { return std::to_string(tile_hi(t)) + std::to_string(tile_lo(t)); }

int parse_tile(const std::string& in) {
    std::string s;
    for (char c : in)
        if (c != '-' && c != '[' && c != ']') s += c;
    if (s.size() != 2 || !std::isdigit(static_cast<unsigned char>(s[0])) ||
        !std::isdigit(static_cast<unsigned char>(s[1])))
        return -1;
    const int a = s[0] - '0', b = s[1] - '0';
    if (a > 6 || b > 6) return -1;
    return tile_id(a, b);
}

static bool has_plain_arm(const Layout& l, int pip) {
    for (int i = 0; i < l.arm_count(); ++i)
        if (l.arm(i).pip == pip && !l.arm(i).dbl) return true;
    return false;
}

std::string target_code(const Layout& l, int target) {
    if (target == kTargetLead) return "";
    if (target == kTargetSpinner)
        return has_plain_arm(l, l.spinner_pip()) ? "@s" : "@" + std::to_string(l.spinner_pip());
    const ArmEnd e = l.arm(target);
    return "@" + std::to_string(e.pip) + (e.dbl ? "d" : "");
}

std::string move_code(const Layout& before, const Action& a, int drawn) {
    switch (a.kind) {
        case Action::Play: return tile_code(a.tile) + target_code(before, a.target);
        case Action::Draw: return drawn >= 0 ? "d" + tile_code(drawn) : "d";
        case Action::Pass: return "p";
        case Action::Bid: return "b" + std::to_string(a.bid);
        case Action::Accept: return "a";
    }
    return "?";
}

bool parse_move(const std::string& raw, const Layout& l, Action& out, int& drawn, std::string& err) {
    std::string s;
    for (char c : raw)
        if (!std::isspace(static_cast<unsigned char>(c))) s += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    // Strip a score annotation such as "+10".
    if (const auto plus = s.find('+'); plus != std::string::npos && plus > 0) s = s.substr(0, plus);
    drawn = -1;
    if (s.empty()) return err = "empty move", false;
    if (s == "a") return out = Action::accept(), true;
    if (s == "p") return out = Action::pass(), true;
    if (s[0] == 'd') {
        out = Action::draw();
        if (s.size() > 1) {
            drawn = parse_tile(s.substr(1));
            if (drawn < 0) return err = "bad tile in draw: " + s, false;
        }
        return true;
    }
    if (s[0] == 'b') {
        if (s.size() < 2 || s.size() > 4) return err = "bad bid: " + s, false;
        for (size_t i = 1; i < s.size(); ++i)
            if (!std::isdigit(static_cast<unsigned char>(s[i]))) return err = "bad bid: " + s, false;
        out = Action::make_bid(std::stoi(s.substr(1)));
        return true;
    }

    const auto at = s.find('@');
    const int t = parse_tile(s.substr(0, at));
    if (t < 0) return err = "bad tile: " + s, false;
    if (l.empty()) return out = Action::play(t, kTargetLead), true;

    int target = -1;
    if (at == std::string::npos) {
        std::vector<int8_t> tg;
        l.targets(t, tg);
        if (tg.empty()) return err = tile_code(t) + " does not fit anywhere", false;
        if (tg.size() > 1) return err = tile_code(t) + " fits more than one end; add @<end>", false;
        target = tg[0];
    } else {
        const std::string tgt = s.substr(at + 1);
        if (tgt == "s") {
            target = kTargetSpinner;
        } else if (tgt.size() >= 1 && std::isdigit(static_cast<unsigned char>(tgt[0]))) {
            const int pip = tgt[0] - '0';
            const bool want_dbl = tgt.size() == 2 && tgt[1] == 'd';
            if (tgt.size() > 2 || (tgt.size() == 2 && !want_dbl)) return err = "bad end: @" + tgt, false;
            for (int i = 0; i < l.arm_count() && target < 0; ++i)
                if (l.arm(i).pip == pip && l.arm(i).dbl == want_dbl) target = i;
            if (target < 0 && !want_dbl && l.spinner_open() && l.spinner_pip() == pip) target = kTargetSpinner;
            if (target < 0 && !want_dbl)
                for (int i = 0; i < l.arm_count() && target < 0; ++i)
                    if (l.arm(i).pip == pip) target = i;
            if (target < 0) return err = "no open end @" + tgt, false;
        } else {
            return err = "bad end: @" + tgt, false;
        }
    }
    if (!l.is_legal(t, target)) return err = tile_code(t) + " cannot go on that end", false;
    out = Action::play(t, target);
    return true;
}

// ---------------------------------------------------------------- diagram

void Diagram::play(const Layout& before, int t, int target) {
    using P = Piece;
    const auto i8 = [](int x) { return static_cast<int8_t>(x); };
    if (target == kTargetLead) {
        for (auto& a : arms) a.clear();
        if (is_double(t)) {
            spinner = t;
            n_arms = 0;
        } else {
            center = t;
            n_arms = 2;
        }
        return;
    }
    if (target == kTargetSpinner) {
        const int sp = before.spinner_pip();
        arms[n_arms++] = {P{i8(t), i8(sp), i8(tile_other(t, sp))}};
        return;
    }
    const int v = before.arm(target).pip;
    if (is_double(t) && !before.has_spinner()) {
        // The line becomes the spinner's first arm, ordered from the spinner outward.
        std::vector<P> chain;
        for (auto it = arms[target].rbegin(); it != arms[target].rend(); ++it) chain.push_back(P{it->tile, it->out, it->in});
        const int near = target == 0 ? tile_hi(center) : tile_lo(center);
        const int far = target == 0 ? tile_lo(center) : tile_hi(center);
        chain.push_back(P{i8(center), i8(near), i8(far)});
        for (const P& p : arms[1 - target]) chain.push_back(p);
        for (auto& a : arms) a.clear();
        arms[0] = chain;
        center = -1;
        spinner = t;
        n_arms = 1;
        return;
    }
    arms[target].push_back(P{i8(t), i8(v), i8(is_double(t) ? v : tile_other(t, v))});
}

// ---------------------------------------------------------------- JSON helpers

namespace {

std::string q(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\', o += c;
        else if (c == '\n') o += "\\n";
        else o += c;
    }
    return o + "\"";
}

std::string num(double x) {
    char b[32];
    std::snprintf(b, sizeof b, "%.3f", x);
    return b;
}

std::string tiles_json(TileMask m) {
    std::string s = "[";
    for (TileMask x = m; x; x &= x - 1) {
        if (s.size() > 1) s += ',';
        s += q(tile_code(first_tile(x)));
    }
    return s + "]";
}

std::string err_json(const std::string& e) { return "{\"ok\":false,\"error\":" + q(e) + "}"; }

std::vector<std::string> split(const std::string& line) {
    std::istringstream in(line);
    std::vector<std::string> v;
    for (std::string w; in >> w;) v.push_back(w);
    return v;
}

bool parse_tiles(const std::vector<std::string>& args, size_t from, size_t to, TileMask& m, std::string& err) {
    m = 0;
    for (size_t i = from; i < to; ++i) {
        const int t = parse_tile(args[i]);
        if (t < 0) return err = "bad tile: " + args[i], false;
        if (m & tile_bit(t)) return err = "duplicate tile: " + args[i], false;
        m |= tile_bit(t);
    }
    return true;
}

const char* phase_name(Phase p) {
    switch (p) {
        case Phase::Bidding: return "bidding";
        case Phase::Play: return "play";
        case Phase::HandOver: return "handover";
        case Phase::GameOver: return "gameover";
    }
    return "?";
}

}  // namespace

// ---------------------------------------------------------------- session

std::string Session::command(const std::string& line) {
    const std::vector<std::string> args = split(line);
    if (args.empty()) return err_json("empty command");
    const std::string& cmd = args[0];

    if (cmd == "state") return state_json(args.size() > 1 ? std::atoi(args[1].c_str()) : 0);
    if (cmd == "record") return "{\"ok\":true,\"text\":" + q(record_text()) + "}";
    if (cmd == "analyze") {
        if (!started_ || hands_.empty()) return err_json("nothing to analyze");
        const int dets = args.size() > 1 ? std::max(1, std::atoi(args[1].c_str())) : 32;
        const long long nodes = args.size() > 2 ? std::max(100LL, std::atoll(args[2].c_str())) : 20000;
        const unsigned long long seed = args.size() > 3 ? std::strtoull(args[3].c_str(), nullptr, 10) : 1;
        return analyze_json(dets, nodes, seed);
    }
    if (cmd == "log") {
        std::string s = "{\"ok\":true,\"log\":[";
        for (size_t i = 0; i < log_.size(); ++i) s += (i ? "," : "") + q(log_[i]);
        return s + "]}";
    }
    if (cmd == "botchoice") {
        if (!match_ || (game_.phase() != Phase::Play && game_.phase() != Phase::Bidding) || (!arena_ && game_.to_move() == me_))
            return err_json("it is not the engine's turn");
        const View& bv = game_.to_move() == me_ ? view_ : bot_view_;
        SearchParams p;
        p.determinizations = args.size() > 1 ? std::max(1, std::atoi(args[1].c_str())) : 32;
        p.nodes_per_det = args.size() > 2 ? std::max(100LL, std::atoll(args[2].c_str())) : 20000;
        p.seed = args.size() > 3 ? std::strtoull(args[3].c_str(), nullptr, 10) : 1;
        const Action a = choose_action(bv, p);
        return "{\"ok\":true,\"move\":" + q(move_code(bv.layout(), a)) + "}";
    }
    if (cmd == "undo") {
        if (log_.size() <= 1) return err_json("nothing to undo");
        // Against the engine, take back to your own last decision.
        do {
            log_.pop_back();
            replay();
        } while (match_ && !arena_ && log_.size() > 1 && log_.back().rfind("move", 0) == 0 &&
                 (game_.phase() == Phase::Play || game_.phase() == Phase::Bidding) && game_.to_move() != me_);
        return state_json();
    }

    const std::string e = exec(args);
    if (!e.empty()) return err_json(e);
    if (cmd == "new" || cmd == "match" || cmd == "arena") log_.clear();
    log_.push_back(line);
    return state_json();
}

void Session::replay() {
    const std::vector<std::string> log = log_;
    for (const auto& line : log) exec(split(line));
}

std::string Session::exec(const std::vector<std::string>& args) {
    const std::string& cmd = args[0];

    if (cmd == "new") {
        if (args.size() < 2 || (args[1] != "1" && args[1] != "2")) return "usage: new <1|2> [pools <a> <b>]";
        started_ = true;
        me_ = args[1][0] - '1';
        view_ = View(me_);
        view_.new_game();
        if (args.size() == 5 && args[2] == "pools") view_.set_pools(std::atoi(args[3].c_str()), std::atoi(args[4].c_str()));
        diag_ = Diagram{};
        hands_.clear();
        settled_ = false;
        match_ = false;
        arena_ = false;
        return "";
    }
    if (cmd == "match" || cmd == "arena") {
        arena_ = cmd == "arena";
        if (arena_) {
            // arena [seed]: both seats are played by bots; viewed from Seat 1.
            std::vector<std::string> a2 = {"match", "1"};
            if (args.size() > 1) a2.push_back(args[1]);
            return exec_match_start(a2);
        }
        return exec_match_start(args);
    }
    if (!started_) return "start a game first (new <seat>)";
    if (cmd == "next") {
        if (!match_) return "next is for games against the engine";
        if (game_.phase() != Phase::HandOver) return "the current hand is not over";
        game_.next_hand(rng_);
        return begin_match_hand();
    }
    if (match_ && (cmd == "hand" || cmd == "reveal" || cmd == "blocked" || cmd == "pools"))
        return "the engine deals and settles hands in this mode";
    return exec_rest(args);
}

std::string Session::exec_match_start(const std::vector<std::string>& args) {
    {
        if (args.size() < 2 || (args[1] != "1" && args[1] != "2")) return "usage: match <1|2> [seed]";
        started_ = true;
        match_ = true;
        me_ = args[1][0] - '1';
        view_ = View(me_);
        view_.new_game();
        bot_view_ = View(1 - me_);
        bot_view_.new_game();
        rng_.seed(args.size() > 2 ? std::strtoull(args[2].c_str(), nullptr, 10) : 1);
        game_ = Game();
        game_.new_game(rng_);
        hands_.clear();
        settled_ = true;
        return begin_match_hand();
    }
}

std::string Session::exec_rest(const std::vector<std::string>& args) {
    const std::string& cmd = args[0];
    if (cmd == "hand") {
        if (view_.phase() != Phase::HandOver) return "the current hand is not over";
        size_t lead_at = args.size();
        for (size_t i = 1; i < args.size(); ++i)
            if (args[i] == "lead") lead_at = i;
        TileMask m;
        std::string err;
        if (!parse_tiles(args, 1, lead_at, m, err)) return err;
        if (tile_count(m) != view_.rules().hand_size) return "a hand has 7 tiles";
        int leader = -1;
        if (view_.jockey() < 0) {
            if (lead_at + 1 >= args.size() || (args[lead_at + 1] != "1" && args[lead_at + 1] != "2"))
                return "who holds the forced lead? add: lead <seat>";
            leader = args[lead_at + 1][0] - '1';
        }
        const int jockey = view_.jockey();
        view_.start_hand(m, leader);
        diag_ = Diagram{};
        settled_ = false;
        HandRec h;
        h.number = static_cast<int>(hands_.size()) + 1;
        h.header = "#" + std::to_string(h.number) +
                   (jockey >= 0 ? " jockey " + std::to_string(jockey + 1) : " lead " + std::to_string(leader + 1));
        h.my_hand = m;
        hands_.push_back(h);
        return "";
    }
    if (hands_.empty()) return "enter your hand first";

    if (cmd == "move") {
        if (args.size() != 2) return "usage: move <move>";
        return exec_move(args[1]);
    }
    if (cmd == "blocked") {
        if (view_.phase() != Phase::Play || view_.boneyard_size() > 0) return "a hand can only block with an empty boneyard";
        view_.end_hand(HandEnd::Blocked, -1);
        hands_.back().end = "end blocked";
        return "";
    }
    if (cmd == "reveal") {
        const bool my_domino = view_.last_hand_end() == HandEnd::Domino && view_.jockey() == me_;
        const bool blocked = view_.last_hand_end() == HandEnd::Blocked;
        if (view_.phase() != Phase::HandOver || settled_ || !(my_domino || blocked)) return "nothing to settle";
        TileMask m;
        std::string err;
        if (!parse_tiles(args, 1, args.size(), m, err)) return err;
        if ((m & ~view_.unknown()) != 0) return "those tiles are not hidden";
        if (tile_count(m) != view_.opp_count()) return "opponent holds " + std::to_string(view_.opp_count()) + " tiles";
        view_.settle(m);
        settled_ = true;
        hands_.back().revealed = m;
        return "";
    }
    if (cmd == "pools") {
        if (args.size() != 3) return "usage: pools <a> <b>";
        view_.set_pools(std::atoi(args[1].c_str()), std::atoi(args[2].c_str()));
        return "";
    }
    return "unknown command: " + cmd;
}

std::string Session::begin_match_hand() {
    View* views[2] = {&view_, &bot_view_};
    const int me[2] = {me_, 1 - me_};
    for (int i = 0; i < 2; ++i) {
        views[i]->set_jockey(game_.jockey());
        views[i]->set_pools(game_.pool(0), game_.pool(1));
        views[i]->start_hand(game_.hand(me[i]), game_.to_move());
    }
    diag_ = Diagram{};
    HandRec h;
    h.number = static_cast<int>(hands_.size()) + 1;
    h.header = "#" + std::to_string(h.number) +
               (game_.jockey() >= 0 ? " jockey " + std::to_string(game_.jockey() + 1)
                                    : " lead " + std::to_string(game_.to_move() + 1));
    h.my_hand = game_.hand(me_);
    hands_.push_back(h);
    return "";
}

std::string Session::exec_match_move(const std::string& code) {
    const Phase ph = game_.phase();
    if (ph != Phase::Play && ph != Phase::Bidding) return "no move expected now";
    const Layout before = game_.layout();
    Action a;
    int drawn = -1;
    std::string err;
    if (!parse_move(code, before, a, drawn, err)) return err;
    const int who = game_.to_move();
    bool found = false;
    for (const Action& x : game_.legal_actions())
        found |= x.kind == a.kind && (a.kind == Action::Play   ? (x.tile == a.tile && x.target == a.target)
                                      : a.kind == Action::Bid ? x.bid == a.bid
                                                              : true);
    if (!found) return (who == me_ ? "illegal move: " : "illegal engine move: ") + code;

    int pts = 0;
    if (a.kind == Action::Play) {
        Layout l = before;
        l.play(a.tile, a.target);
        const int c = l.count();
        pts = (c > 0 && c % 5 == 0) ? c : 0;
        diag_.play(before, a.tile, a.target);
    }
    game_.apply(a);
    const int d = a.kind == Action::Draw ? game_.last_drawn() : -1;
    view_.observe(who, a, who == me_ ? d : -1);
    bot_view_.observe(who, a, who != me_ ? d : -1);
    hands_.back().moves.push_back(HistMove{who + 1, move_code(before, a, who == me_ ? d : -1), pts});

    if (game_.phase() == Phase::HandOver || game_.phase() == Phase::GameOver) {
        for (View* v : {&view_, &bot_view_}) {
            if (v->phase() == Phase::Play || v->phase() == Phase::Bidding) v->end_hand(game_.last_hand_end(), game_.jockey());
            v->set_jockey(game_.jockey());
            v->set_pools(game_.pool(0), game_.pool(1));
        }
        HandRec& h = hands_.back();
        h.end = game_.last_hand_end() == HandEnd::Domino ? "end domino " + std::to_string(game_.jockey() + 1)
              : game_.last_hand_end() == HandEnd::Blocked ? "end blocked" : "";
        if (game_.phase() == Phase::GameOver)
            h.end += (h.end.empty() ? "" : "; ") + std::string("game over, seat ") + std::to_string(game_.winner() + 1) + " wins";
        h.revealed = game_.hand(1 - me_);
    }
    return "";
}

std::string Session::exec_move(const std::string& code) {
    if (match_) return exec_match_move(code);
    const Phase ph = view_.phase();
    if (ph != Phase::Play && ph != Phase::Bidding) return "no move expected now";
    const Layout before = view_.layout();
    Action a;
    int drawn = -1;
    std::string err;
    if (!parse_move(code, before, a, drawn, err)) return err;
    const int who = view_.to_move();

    if (ph == Phase::Bidding) {
        if (a.kind == Action::Bid) {
            const int lo = view_.high_bidder() < 0 ? 0 : view_.current_bid() + 1;
            if (a.bid < lo || a.bid > view_.max_bid(who))
                return "bid must be " + std::to_string(lo) + " to " + std::to_string(view_.max_bid(who));
        } else if (a.kind == Action::Accept) {
            if (view_.high_bidder() < 0) return "nothing to accept";
        } else {
            return "bidding: use b<n> or a";
        }
    } else if (who == me_) {
        const auto legal = view_.legal_actions();
        bool found = false;
        for (const auto& x : legal) found |= (x.kind == a.kind && (a.kind != Action::Play || (x.tile == a.tile && x.target == a.target)));
        if (!found) return "illegal move for you: " + code;
        if (a.kind == Action::Draw && (drawn < 0 || !(view_.unknown() & tile_bit(drawn))))
            return "enter the tile you drew, e.g. d32";
    } else {
        if (a.kind == Action::Play) {
            if (!(view_.unknown() & tile_bit(a.tile))) return tile_code(a.tile) + " is not in the opponent's hand";
        } else if (a.kind == Action::Draw) {
            if (view_.boneyard_size() == 0) return "the boneyard is empty";
            drawn = -1;
        } else if (a.kind == Action::Pass) {
            if (view_.boneyard_size() > 0) return "cannot pass while the boneyard has tiles";
        } else {
            return "not a play-phase move";
        }
    }

    int pts = 0;
    if (a.kind == Action::Play) {
        Layout l = before;
        l.play(a.tile, a.target);
        const int c = l.count();
        pts = (c > 0 && c % 5 == 0) ? c : 0;
        diag_.play(before, a.tile, a.target);
    }
    hands_.back().moves.push_back(HistMove{who + 1, move_code(before, a, who == me_ ? drawn : -1), pts});
    view_.observe(who, a, drawn);

    if (view_.phase() == Phase::HandOver || view_.phase() == Phase::GameOver) {
        if (view_.last_hand_end() == HandEnd::Domino && view_.jockey() == who && a.kind == Action::Play)
            hands_.back().end = "end domino " + std::to_string(who + 1);
        else if (view_.last_hand_end() == HandEnd::Blocked)
            hands_.back().end = "end blocked";
        if (view_.phase() == Phase::GameOver)
            hands_.back().end += (hands_.back().end.empty() ? "" : "; ") + std::string("game over, seat ") +
                                 std::to_string(view_.winner() + 1) + " wins";
    }
    return "";
}

std::string Session::state_json(int seat) const {
    // seat: 0 = my seat; 1 or 2 = that seat's view (only in arena games).
    const int me = (arena_ && seat > 0) ? seat - 1 : me_;
    const View& v = me == me_ ? view_ : bot_view_;
    std::string s = "{\"ok\":true,\"started\":" + std::string(started_ ? "true" : "false");
    if (!started_) return s + ",\"phase\":\"setup\"}";
    const Layout& l = v.layout();
    const int opp = 1 - me;
    s += ",\"me\":" + std::to_string(me + 1);
    s += ",\"phase\":" + q(phase_name(v.phase()));
    s += ",\"handNumber\":" + std::to_string(hands_.size());
    s += ",\"toMove\":" + std::to_string(v.to_move() + 1);
    s += ",\"pools\":[" + std::to_string(v.pool(0)) + "," + std::to_string(v.pool(1)) + "]";
    s += ",\"jockey\":" + std::to_string(v.jockey() + 1);
    s += ",\"winner\":" + std::to_string(v.winner() + 1);
    s += ",\"myHand\":" + tiles_json(v.my_hand());
    s += ",\"myPips\":" + std::to_string(pip_total(v.my_hand()));
    s += ",\"oppCount\":" + std::to_string(hands_.empty() ? 0 : v.opp_count());
    s += ",\"boneyard\":" + std::to_string(hands_.empty() ? 0 : v.boneyard_size());
    s += ",\"unknown\":" + tiles_json(hands_.empty() ? 0 : v.unknown());
    s += ",\"excluded\":" + tiles_json(hands_.empty() ? 0 : v.opp_excluded());

    const char* end = "none";
    if (v.last_hand_end() == HandEnd::Domino) end = "domino";
    if (v.last_hand_end() == HandEnd::Blocked) end = "blocked";
    s += ",\"lastEnd\":" + q(end);
    const bool can_settle = (v.phase() == Phase::HandOver) && !settled_ &&
                            ((v.last_hand_end() == HandEnd::Domino && v.jockey() == me) || v.last_hand_end() == HandEnd::Blocked);
    s += ",\"needsReveal\":" + std::string(can_settle && !match_ ? "true" : "false");
    s += ",\"match\":" + std::string(match_ ? "true" : "false");
    const bool over = match_ && (game_.phase() == Phase::HandOver || game_.phase() == Phase::GameOver);
    s += ",\"revealed\":" + tiles_json(over ? game_.hand(opp) : 0);
    s += ",\"arena\":" + std::string(arena_ ? "true" : "false");
    if (arena_) s += ",\"hands\":[" + tiles_json(game_.hand(0)) + "," + tiles_json(game_.hand(1)) + "]";

    s += ",\"bid\":{\"current\":" + std::to_string(v.current_bid()) + ",\"by\":" + std::to_string(v.high_bidder() + 1) +
         ",\"maxMe\":" + std::to_string(v.max_bid(me)) + ",\"maxOpp\":" + std::to_string(v.max_bid(opp)) + "}";

    // Layout drawing model.
    s += ",\"layout\":{\"count\":" + std::to_string(l.count());
    s += ",\"center\":" + (diag_.center >= 0 ? q(tile_code(diag_.center)) : std::string("null"));
    s += ",\"spinner\":" + (diag_.spinner >= 0 ? q(tile_code(diag_.spinner)) : std::string("null"));
    s += ",\"arms\":[";
    for (int i = 0; i < diag_.n_arms; ++i) {
        if (i) s += ',';
        s += '[';
        for (size_t k = 0; k < diag_.arms[i].size(); ++k) {
            const auto& p = diag_.arms[i][k];
            if (k) s += ',';
            s += "{\"t\":" + q(tile_code(p.tile)) + ",\"in\":" + std::to_string(p.in) + ",\"out\":" + std::to_string(p.out) + "}";
        }
        s += ']';
    }
    s += "],\"ends\":[";
    bool first = true;
    if (!l.empty()) {
        for (int i = 0; i < l.arm_count(); ++i) {
            const ArmEnd e = l.arm(i);
            s += std::string(first ? "" : ",") + "{\"arm\":" + std::to_string(i) + ",\"pip\":" + std::to_string(e.pip) +
                 ",\"dbl\":" + (e.dbl ? "true" : "false") + ",\"code\":" + q(target_code(l, i)) + "}";
            first = false;
        }
        if (l.spinner_open())
            s += std::string(first ? "" : ",") + "{\"arm\":-1,\"pip\":" + std::to_string(l.spinner_pip()) +
                 ",\"spinner\":true,\"slot\":" + std::to_string(l.arm_count()) + ",\"code\":" + q(target_code(l, kTargetSpinner)) + "}";
    }
    s += "]}";

    // My legal moves.
    s += ",\"legal\":[";
    if (!hands_.empty() && v.to_move() == me && v.phase() == Phase::Play) {
        const auto acts = v.legal_actions();
        for (size_t i = 0; i < acts.size(); ++i) {
            const Action& a = acts[i];
            if (i) s += ',';
            s += "{\"n\":" + q(move_code(l, a)) + ",\"kind\":" + q(a.kind == Action::Play ? "play" : a.kind == Action::Draw ? "draw" : "pass");
            if (a.kind == Action::Play) s += ",\"tile\":" + q(tile_code(a.tile)) + ",\"code\":" + q(target_code(l, a.target));
            s += "}";
        }
    }
    s += "]";

    // History.
    s += ",\"history\":[";
    for (size_t h = 0; h < hands_.size(); ++h) {
        const HandRec& r = hands_[h];
        if (h) s += ',';
        s += "{\"hand\":" + std::to_string(r.number) + ",\"header\":" + q(r.header) + ",\"end\":" + q(r.end) + ",\"moves\":[";
        for (size_t k = 0; k < r.moves.size(); ++k) {
            if (k) s += ',';
            s += "{\"seat\":" + std::to_string(r.moves[k].seat) + ",\"n\":" + q(r.moves[k].code) +
                 ",\"pts\":" + std::to_string(r.moves[k].points) + "}";
        }
        s += "]}";
    }
    s += "]}";
    return s;
}

std::string Session::analyze_json(int dets, long long nodes, unsigned long long seed) const {
    // Every value is reported from Seat 1's side: + favors Seat 1, - favors Seat 2
    // (like chess engines reporting from White's side).
    SearchParams p;
    p.determinizations = dets;
    p.nodes_per_det = nodes;
    p.seed = seed;
    SearchStats st;
    const double sgn = me_ == 0 ? 1.0 : -1.0;
    std::string s = "{\"ok\":true,\"perspective\":\"seat1\"";
    const View& v = view_;
    if (arena_) {
        // Spectator: both hands are on show; only the boneyard order is unknown.
        if (game_.phase() == Phase::Bidding) {
            double s1 = 0, s2 = 0;
            evaluate_spectator_bid(game_, p, s1, s2, &st);
            s += ",\"kind\":\"bid\",\"spectator\":true,\"seat1Leads\":" + num(s1) + ",\"seat2Leads\":" + num(s2);
        } else if (game_.phase() == Phase::Play) {
            s += ",\"kind\":\"position\",\"spectator\":true,\"eval\":" + num(evaluate_spectator(game_, p, &st));
        } else {
            return err_json("no position to evaluate");
        }
        s += ",\"stats\":{\"nodes\":" + std::to_string(st.nodes) + ",\"depth\":" + num(st.avg_depth) +
             ",\"exact\":" + std::to_string(st.exact) + ",\"dets\":" + std::to_string(dets) + ",\"fallbacks\":0" +
             ",\"nnue\":" + (p.eval.nn ? "true" : "false") + ",\"models\":" + q(models().description) + "}}";
        return s;
    }
    if (v.phase() == Phase::Bidding) {
        const BidEval b = analyze_bid(v, p, &st);
        const double s1 = sgn * (me_ == 0 ? b.ev_i_lead : b.ev_opp_leads);
        const double s2 = sgn * (me_ == 0 ? b.ev_opp_leads : b.ev_i_lead);
        s += ",\"kind\":\"bid\",\"seat1Leads\":" + num(s1) + ",\"seat2Leads\":" + num(s2) +
             ",\"leadValue\":" + num(b.lead_value) + ",\"walkAway\":" + std::to_string(b.walk_away);
        if (v.to_move() == me_) s += ",\"suggestion\":" + q(move_code(v.layout(), b.suggestion));
    } else if (v.phase() == Phase::Play) {
        if (v.to_move() == me_) {
            const auto moves = analyze_moves(v, p, &st);
            s += ",\"kind\":\"moves\",\"eval\":" + num(sgn * moves.front().ev) + ",\"moves\":[";
            for (size_t i = 0; i < moves.size(); ++i) {
                if (i) s += ',';
                s += "{\"n\":" + q(move_code(v.layout(), moves[i].action)) + ",\"ev\":" + num(sgn * moves[i].ev) +
                     ",\"se\":" + num(moves[i].se) + "}";
            }
            s += "]";
        } else {
            s += ",\"kind\":\"position\",\"eval\":" + num(sgn * evaluate_position(v, p, &st));
        }
    } else {
        return err_json("no decision to analyze");
    }
    s += ",\"stats\":{\"nodes\":" + std::to_string(st.nodes) + ",\"depth\":" + num(st.avg_depth) +
         ",\"exact\":" + std::to_string(st.exact) + ",\"dets\":" + std::to_string(dets) +
         ",\"fallbacks\":" + std::to_string(st.fallbacks) + ",\"nnue\":" + (p.eval.nn ? "true" : "false") + ",\"models\":" + q(models().description) + "}}";
    return s;
}

std::string Session::record_text() const {
    if (!started_) return "";
    std::string s = "[Event \"Spectacular Bid\"]\n[Seat1 \"" + std::string(me_ == 0 ? "You" : match_ ? "Engine" : "Opponent") +
                    "\"]\n[Seat2 \"" + std::string(me_ == 1 ? "You" : match_ ? "Engine" : "Opponent") + "\"]\n[View \"" +
                    std::to_string(me_ + 1) + "\"]\n";
    if (view_.phase() == Phase::GameOver) s += "[Result \"" + std::string(view_.winner() == 0 ? "1-0" : "0-1") + "\"]\n";
    for (const HandRec& h : hands_) {
        s += "\n" + h.header + "\n";
        std::string hand;
        for (TileMask m = h.my_hand; m; m &= m - 1) hand += " " + tile_code(first_tile(m));
        s += "h" + std::to_string(me_ + 1) + ":" + hand + "\n";
        std::string line;
        for (const auto& m : h.moves) {
            if (!line.empty()) line += ' ';
            line += m.code;
            if (m.points > 0) line += " +" + std::to_string(m.points);
        }
        if (!line.empty()) s += line + "\n";
        if (!h.end.empty()) s += h.end + "\n";
        if (h.revealed) {
            std::string r;
            for (TileMask m = h.revealed; m; m &= m - 1) r += " " + tile_code(first_tile(m));
            s += "r" + std::to_string(2 - me_) + ":" + r + "\n";
        }
    }
    return s;
}

}  // namespace sb
