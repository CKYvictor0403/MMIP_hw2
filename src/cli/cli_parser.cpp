#include "cli/cli_parser.hpp"

namespace mcodec {

void CliParser::parse(int argc, char** argv) {
    kv_.clear();
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i] ? argv[i] : "";
        if (a.rfind("--", 0) == 0) {
            std::string key = a.substr(2);
            std::string val = "true";
            if (i + 1 < argc) {
                std::string next = argv[i + 1] ? argv[i + 1] : "";
                if (next.rfind("--", 0) != 0) {
                    val = next;
                    ++i;
                }
            }
            kv_[key] = val;
        }
    }
}

bool CliParser::has(const std::string& key) const {
    return kv_.find(key) != kv_.end();
}

std::string CliParser::get(const std::string& key, const std::string& def) const {
    auto it = kv_.find(key);
    if (it == kv_.end()) return def;
    return it->second;
}

} // namespace mcodec


