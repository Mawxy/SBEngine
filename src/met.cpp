#include "met.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace sb {

const MetTable*& met_active() {
    static const MetTable* t = met_builtin();
    return t;
}

double met_win(int a, int b, int j) {
    if (a <= 0 && b <= 0) return 0.5;
    if (a <= 0) return 0.0;
    if (b <= 0) return 1.0;
    a = std::min(a, kMetPool);
    b = std::min(b, kMetPool);
    if (const MetTable* t = met_active()) return t->p[a][b][j < 0 || j > 2 ? 2 : j];
    // No table: a rough logistic on the pool difference, jockey worth about 1 chip.
    const double edge = (a - b) + (j == 0 ? 1.0 : j == 1 ? -1.0 : 0.0);
    return 1.0 / (1.0 + std::exp(-edge / 7.0));
}

bool met_load(MetTable& t, const std::string& path) {
    std::ifstream in(path);
    if (!in) return false;
    MetTable x{};
    int n = 0;
    for (std::string line; std::getline(in, line);) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream s(line);
        int a, b, j;
        double p;
        if (!(s >> a >> b >> j >> p)) continue;
        if (a < 1 || a > kMetPool || b < 1 || b > kMetPool || j < 0 || j > 2 || !(p >= 0 && p <= 1)) return false;
        x.p[a][b][j] = static_cast<float>(p);
        ++n;
    }
    if (n != kMetPool * kMetPool * 3) return false;
    t = x;
    return true;
}

bool met_save(const MetTable& t, const std::string& path, const std::string& header) {
    std::FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return false;
    std::fprintf(f, "# Spectacular Bid match equity table: my_pool opp_pool jockey(0 me,1 opp,2 none) P(I win the game)\n");
    if (!header.empty()) std::fprintf(f, "# %s\n", header.c_str());
    for (int a = 1; a <= kMetPool; ++a)
        for (int b = 1; b <= kMetPool; ++b)
            for (int j = 0; j < 3; ++j) std::fprintf(f, "%d %d %d %.5f\n", a, b, j, t.p[a][b][j]);
    std::fclose(f);
    return true;
}

}  // namespace sb
