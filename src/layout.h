#pragma once
// The layout (tableau) with a single spinner, as scored in All Fives.
//
// Before any double is played the layout is a line with two open ends (arms 0 and 1).
// The first double played becomes the spinner. From then on every open end is an "arm"
// growing out of the spinner. Attachments to the spinner happen in order: the first two
// on its long sides, then the third and fourth on its short ends. Because the order is
// fixed, the spinner exposes a single generic "spinner slot" target while it has < 4 arms.
//
// Scoring: each arm end counts its pip (double on an end counts both pips). The spinner
// itself counts both pips until two arms are attached.

#include <array>
#include <string>
#include <vector>

#include "tile.h"

namespace sb {

enum Target : int8_t {
    kTargetArm0 = 0,
    kTargetArm1 = 1,
    kTargetArm2 = 2,
    kTargetArm3 = 3,
    kTargetSpinner = 4,
    kTargetLead = 5,
};

struct ArmEnd {
    int8_t pip = -1;
    bool dbl = false;  // end tile is a (non-spinner) double laid crosswise
    bool operator==(const ArmEnd&) const = default;
};

class Layout {
public:
    bool empty() const { return !started_; }
    bool has_spinner() const { return spinner_pip_ >= 0; }
    int spinner_pip() const { return spinner_pip_; }
    int arm_count() const { return n_arms_; }
    ArmEnd arm(int i) const { return arms_[i]; }
    bool spinner_open() const { return has_spinner() && n_arms_ < 4; }
    TileMask played() const { return played_; }

    // Sum of all open ends (the All Fives count).
    int count() const;

    // Every tile that could be placed somewhere right now.
    TileMask playable_mask() const;

    // Distinct legal targets for tile t (equivalent arms are de-duplicated).
    void targets(int t, std::vector<int8_t>& out) const;
    bool is_legal(int t, int target) const;

    void play(int t, int target);

    std::string to_string() const;

private:
    bool started_ = false;
    int8_t spinner_pip_ = -1;
    int8_t n_arms_ = 0;
    std::array<ArmEnd, 4> arms_{};
    TileMask played_ = 0;
};

}  // namespace sb
