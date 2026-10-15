// Command-line front end: one session command per input line, one JSON reply per
// output line (see src/session.h). The desktop app talks to the engine this way,
// the same way chess GUIs talk to Stockfish over UCI.
//
//   sbcli [models_dir]   loads eval.bin / bid.bin / info.bin from models_dir (e.g. training/promoted)

#include <iostream>
#include <string>

#include "../src/models.h"
#include "../src/session.h"

int main(int argc, char** argv) {
    std::ios::sync_with_stdio(false);
    if (argc > 1) std::cerr << "Engine models: " << sb::load_models_from(argv[1]) << std::endl;
    sb::Session s;
    for (std::string line; std::getline(std::cin, line);) {
        if (line == "quit") break;
        if (line.empty()) continue;
        std::string out;
        try {
            out = s.command(line);
        } catch (const std::exception& e) {
            out = std::string("{\"ok\":false,\"error\":\"internal: ") + e.what() + "\"}";
        }
        std::cout << out << '\n' << std::flush;
    }
}
