#pragma once
#include <string>
#include <vector>
#include <random>

class Generator {
public:
    // All valid 3-character names (alnum + _ and . with Discord rules)
    static std::vector<std::string> All3L();
    // All valid 4-character names
    static std::vector<std::string> All4L();

    // Single random name
    static std::string Random3L();
    static std::string Random4L();

    // In-place Fisher-Yates shuffle
    static void Shuffle(std::vector<std::string>& list);

    // True if s passes: no leading/trailing special, no consecutive specials,
    // all chars in [a-z0-9_.]
    static bool IsValid(const std::string& s);

private:
    static bool        IsSpecial(char c) noexcept;
    static std::mt19937& RNG();

    // Character sets (static storage, init once)
    static const std::string kAlnum; // a-z 0-9
    static const std::string kAll;   // a-z 0-9 _ .
};
