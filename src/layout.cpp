#include "layout.h"

#include <cassert>
#include "fail.h"

namespace sb {

int Layout::count() const {
    int s = 0;
    for (int i = 0; i < n_arms_; ++i) s += arms_[i].dbl ? 2 * arms_[i].pip : arms_[i].pip;
    if (has_spinner() && n_arms_ < 2) s += 2 * spinner_pip_;
    return s;
}

TileMask Layout::playable_mask() const {
    if (!started_) return kAllTiles;
    TileMask m = 0;
    for (int i = 0; i < n_arms_; ++i) m |= kSuit[arms_[i].pip];
    if (spinner_open()) m |= kSuit[spinner_pip_];
    return m & ~played_;
}

void Layout::targets(int t, std::vector<int8_t>& out) const {
    if (!started_) {
        out.push_back(kTargetLead);
        return;
    }
    for (int i = 0; i < n_arms_; ++i) {
        if (!tile_has(t, arms_[i].pip)) continue;
        bool dup = false;
        for (int j = 0; j < i; ++j) dup |= (arms_[j] == arms_[i]);
        if (!dup) out.push_back(static_cast<int8_t>(i));
    }
    if (spinner_open() && tile_has(t, spinner_pip_)) out.push_back(kTargetSpinner);
}

bool Layout::is_legal(int t, int target) const {
    if (played_ & tile_bit(t)) return false;
    if (!started_) return target == kTargetLead;
    if (target == kTargetSpinner) return spinner_open() && tile_has(t, spinner_pip_) && !is_double(t);
    if (target >= 0 && target < n_arms_) return tile_has(t, arms_[target].pip);
    return false;
}

void Layout::play(int t, int target) {
    if (!is_legal(t, target)) fail("illegal layout play " + tile_str(t));
    played_ |= tile_bit(t);

    if (target == kTargetLead) {
        started_ = true;
        if (is_double(t)) {
            spinner_pip_ = static_cast<int8_t>(tile_hi(t));
            n_arms_ = 0;
        } else {
            arms_[0] = {static_cast<int8_t>(tile_hi(t)), false};
            arms_[1] = {static_cast<int8_t>(tile_lo(t)), false};
            n_arms_ = 2;
        }
        return;
    }

    if (target == kTargetSpinner) {
        arms_[n_arms_++] = {static_cast<int8_t>(tile_other(t, spinner_pip_)), false};
        return;
    }

    const int v = arms_[target].pip;
    if (is_double(t)) {
        if (!has_spinner()) {
            // First double: it becomes the spinner. The line it was played against
            // is now the spinner's first arm (the other line end).
            assert(n_arms_ == 2);
            spinner_pip_ = static_cast<int8_t>(v);
            arms_[0] = arms_[1 - target];
            arms_[1] = {};
            n_arms_ = 1;
        } else {
            arms_[target] = {static_cast<int8_t>(v), true};
        }
    } else {
        arms_[target] = {static_cast<int8_t>(tile_other(t, v)), false};
    }
}

std::string Layout::to_string() const {
    if (!started_) return "(empty)";
    std::string s;
    if (has_spinner()) s += "spinner " + tile_str(tile_id(spinner_pip_, spinner_pip_)) + " ";
    s += "ends:";
    for (int i = 0; i < n_arms_; ++i) {
        s += " " + std::to_string(arms_[i].pip);
        if (arms_[i].dbl) s += "d";
    }
    s += "  count=" + std::to_string(count());
    return s;
}

}  // namespace sb
