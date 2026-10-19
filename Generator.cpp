#include "Generator.hpp"
#include <algorithm>
#include <stdexcept>

const std::string Generator::kAlnum = "abcdefghijklmnopqrstuvwxyz0123456789";
const std::string Generator::kAll   = "abcdefghijklmnopqrstuvwxyz0123456789_.";

bool Generator::IsSpecial(char c) noexcept {
    return c == '_' || c == '.';
}

std::mt19937& Generator::RNG() {
    static std::mt19937 rng{ std::random_device{}() };
    return rng;
}

bool Generator::IsValid(const std::string& s) {
    if (s.size() < 2 || s.size() > 32) return false;
    if (IsSpecial(s.front()) || IsSpecial(s.back())) return false;
    for (size_t i = 0; i + 1 < s.size(); ++i)
        if (IsSpecial(s[i]) && IsSpecial(s[i + 1])) return false;
    for (char c : s)
        if (kAll.find(c) == std::string::npos) return false;
    return true;
}

// ─── 3L ───────────────────────────────────────────────────────────────────────
// Layout:  pos0 ∈ kAlnum (36),  pos1 ∈ kAll (38),  pos2 ∈ kAlnum (36)
// No consecutive-special issue: only one middle position, can't be adjacent.
// Total: 36 × 38 × 36 = 49,248 names
std::vector<std::string> Generator::All3L() {
    std::vector<std::string> result;
    result.reserve(36 * 38 * 36);
    std::string s(3, '\0');
    for (char c0 : kAlnum) {
        for (char c1 : kAll) {
            for (char c2 : kAlnum) {
                s[0] = c0; s[1] = c1; s[2] = c2;
                result.push_back(s);
            }
        }
    }
    return result;
}

// ─── 4L ───────────────────────────────────────────────────────────────────────
// Layout: pos0 ∈ kAlnum, pos1 ∈ kAll, pos2 ∈ kAll (but not both pos1&pos2 special),
//         pos3 ∈ kAlnum
// Valid (pos1,pos2) pairs: 36×38 + 2×36 = 1440   →  Total: 36×1440×36 = 1,866,240
std::vector<std::string> Generator::All4L() {
    std::vector<std::string> result;
    result.reserve(1'870'000);
    std::string s(4, '\0');
    for (char c0 : kAlnum) {
        for (char c1 : kAll) {
            bool c1sp = IsSpecial(c1);
            for (char c2 : kAll) {
                if (c1sp && IsSpecial(c2)) continue; // consecutive specials
                for (char c3 : kAlnum) {
                    s[0] = c0; s[1] = c1; s[2] = c2; s[3] = c3;
                    result.push_back(s);
                }
            }
        }
    }
    return result;
}

std::string Generator::Random3L() {
    auto& rng = RNG();
    std::uniform_int_distribution<size_t> da(0, kAlnum.size() - 1);
    std::uniform_int_distribution<size_t> dl(0, kAll.size() - 1);
    std::string s(3, '\0');
    s[0] = kAlnum[da(rng)];
    s[1] = kAll[dl(rng)];
    s[2] = kAlnum[da(rng)];
    return s;
}

std::string Generator::Random4L() {
    auto& rng = RNG();
    std::uniform_int_distribution<size_t> da(0, kAlnum.size() - 1);
    std::uniform_int_distribution<size_t> dl(0, kAll.size() - 1);
    for (;;) {
        std::string s(4, '\0');
        s[0] = kAlnum[da(rng)];
        s[1] = kAll[dl(rng)];
        s[2] = kAll[dl(rng)];
        s[3] = kAlnum[da(rng)];
        if (!IsSpecial(s[1]) || !IsSpecial(s[2])) return s;
        // retry — consecutive specials in middle, ~0.28% chance
    }
}

void Generator::Shuffle(std::vector<std::string>& list) {
    std::shuffle(list.begin(), list.end(), RNG());
}
