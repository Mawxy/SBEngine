#pragma once
// Tile encoding for a double-six set.
// Tile id = hi*(hi+1)/2 + lo, with hi >= lo. Ids 0..27.
// Sets of tiles are 32-bit masks (bit i = tile id i).

#include <array>
#include <bit>
#include <cstdint>
#include <string>

namespace sb {

constexpr int kNumTiles = 28;
constexpr int kNumPips = 7;  // 0..6
using TileMask = uint32_t;
constexpr TileMask kAllTiles = (TileMask{1} << kNumTiles) - 1;

struct TileDef {
    int8_t hi;
    int8_t lo;
};

constexpr int tile_id(int a, int b) {
    const int h = a > b ? a : b;
    const int l = a > b ? b : a;
    return h * (h + 1) / 2 + l;
}

namespace detail {
constexpr std::array<TileDef, kNumTiles> make_tiles() {
    std::array<TileDef, kNumTiles> t{};
    for (int h = 0; h < kNumPips; ++h)
        for (int l = 0; l <= h; ++l) t[tile_id(h, l)] = TileDef{static_cast<int8_t>(h), static_cast<int8_t>(l)};
    return t;
}
constexpr std::array<TileMask, kNumPips> make_suits() {
    std::array<TileMask, kNumPips> s{};
    for (int h = 0; h < kNumPips; ++h)
        for (int l = 0; l <= h; ++l) {
            s[h] |= TileMask{1} << tile_id(h, l);
            s[l] |= TileMask{1} << tile_id(h, l);
        }
    return s;
}
}  // namespace detail

inline constexpr std::array<TileDef, kNumTiles> kTileDefs = detail::make_tiles();
// kSuit[p] = every tile containing pip p.
inline constexpr std::array<TileMask, kNumPips> kSuit = detail::make_suits();

constexpr int tile_hi(int t) { return kTileDefs[t].hi; }
constexpr int tile_lo(int t) { return kTileDefs[t].lo; }
constexpr bool is_double(int t) { return tile_hi(t) == tile_lo(t); }
constexpr int tile_pips(int t) { return tile_hi(t) + tile_lo(t); }
constexpr bool tile_has(int t, int pip) { return tile_hi(t) == pip || tile_lo(t) == pip; }
constexpr int tile_other(int t, int pip) { return tile_hi(t) == pip ? tile_lo(t) : tile_hi(t); }
constexpr TileMask tile_bit(int t) { return TileMask{1} << t; }

inline int tile_count(TileMask m) { return std::popcount(m); }
inline int first_tile(TileMask m) { return std::countr_zero(m); }

inline int pip_total(TileMask m) {
    int s = 0;
    for (; m; m &= m - 1) s += tile_pips(first_tile(m));
    return s;
}

// Nearest multiple of 5, expressed in chips (5 points each). 12 -> 2, 13 -> 3.
constexpr int round5_chips(int points) { return (points + 2) / 5; }

// Fisher-Yates shuffle with a fixed algorithm, so the same seed deals the same tiles
// on every compiler and platform (std::shuffle is implementation-defined). Keeps the
// browser (WebAssembly) and desktop (native) engines in lockstep.
template <class T, class Rng>
void portable_shuffle(T* first, int n, Rng& rng) {
    for (int i = n - 1; i > 0; --i) {
        const int j = static_cast<int>(rng() % static_cast<uint64_t>(i + 1));
        T tmp = first[i];
        first[i] = first[j];
        first[j] = tmp;
    }
}

inline std::string tile_str(int t) {
    return "[" + std::to_string(tile_hi(t)) + "-" + std::to_string(tile_lo(t)) + "]";
}

inline std::string mask_str(TileMask m) {
    std::string s;
    for (; m; m &= m - 1) {
        if (!s.empty()) s += ' ';
        s += tile_str(first_tile(m));
    }
    return s;
}

}  // namespace sb
