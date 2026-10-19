#include "ProxyManager.hpp"
#include <cpr/cpr.h>
#include <fstream>
#include <future>
#include <algorithm>

// ─── helpers ─────────────────────────────────────────────────────────────────
static std::string TrimLine(std::string s) {
    const char* ws = " \t\r\n";
    s.erase(0, s.find_first_not_of(ws));
    auto end = s.find_last_not_of(ws);
    if (end != std::string::npos) s.erase(end + 1);
    return s;
}

// Any HTTP status > 0 from Discord means the proxy routes correctly
static bool ProbeProxy(const std::string& proxy, int timeout_ms) {
    auto r = cpr::Get(
        cpr::Url{"https://discord.com/api/v9/users/@me"},
        cpr::Proxies{{"https", proxy}},
        cpr::Timeout{static_cast<long>(timeout_ms)},
        cpr::VerifySsl{false}
    );
    return r.status_code > 0;
}

// ─── ProxyManager ──────────────────────────────────────────────────────────
bool ProxyManager::LoadFromFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    raw_.clear();
    std::string line;
    while (std::getline(f, line)) {
        line = TrimLine(line);
        if (!line.empty()) raw_.push_back(line);
    }
    direct_ = false;
    return !raw_.empty();
}

void ProxyManager::SetManual(const std::string& proxy) {
    raw_   = { TrimLine(proxy) };
    alive_ = { TrimLine(proxy) };
    alive_count_ = 1;
    dead_count_  = 0;
    direct_      = false;
}

void ProxyManager::SetDirect() {
    direct_ = true;
    raw_.clear();
    alive_.clear();
    alive_count_ = 0;
    dead_count_  = 0;
}

void ProxyManager::CheckAll(int timeout_ms, std::function<void(int, int)> on_progress) {
    alive_count_ = 0;
    dead_count_  = 0;
    {
        std::lock_guard lk(mtx_);
        alive_.clear();
    }

    // Launch one future per proxy
    std::vector<std::future<std::pair<std::string, bool>>> futs;
    futs.reserve(raw_.size());
    for (const auto& p : raw_) {
        futs.push_back(std::async(std::launch::async, [p, timeout_ms]() {
            return std::make_pair(p, ProbeProxy(p, timeout_ms));
        }));
    }

    // Collect results as they arrive
    for (auto& f : futs) {
        auto [proxy, ok] = f.get();
        if (ok) {
            ++alive_count_;
            std::lock_guard lk(mtx_);
            alive_.push_back(proxy);
        } else {
            ++dead_count_;
        }
        if (on_progress)
            on_progress(alive_count_.load(), dead_count_.load());
    }

    // Persist dead list for diagnostics
    std::ofstream dead_f("dead_proxies.txt", std::ios::app);
    for (const auto& p : raw_) {
        std::lock_guard lk(mtx_);
        bool is_alive = std::find(alive_.begin(), alive_.end(), p) != alive_.end();
        if (!is_alive) dead_f << p << "\n";
    }
}

std::string ProxyManager::GetNext() {
    if (direct_) return "";
    std::lock_guard lk(mtx_);
    if (!alive_.empty()) {
        size_t idx = rr_idx_.fetch_add(1) % alive_.size();
        return alive_[idx];
    }
    if (!raw_.empty()) {
        size_t idx = rr_idx_.fetch_add(1) % raw_.size();
        return raw_[idx];
    }
    return "";
}
