#pragma once

#include <string>
#include <unordered_map>

namespace mcodec {

// Very small CLI parser:
//   --key value
//   --flag (treated as "true")
class CliParser {
public:
    void parse(int argc, char** argv);
    bool has(const std::string& key) const;
    std::string get(const std::string& key, const std::string& def = "") const;
private:
    std::unordered_map<std::string, std::string> kv_;
};

} // namespace mcodec


