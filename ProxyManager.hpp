#pragma once
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <functional>

class ProxyManager {
public:
    // Load proxy list from file; returns false if file missing
    bool LoadFromFile(const std::string& path);

    // Single manual proxy
    void SetManual(const std::string& proxy);

    // Direct connection (no proxy)
    void SetDirect();

    bool IsDirect() const { return direct_; }

    // Parallel health check. on_progress(alive, dead) called after each result.
    // timeout_ms: per-proxy connection timeout.
    void CheckAll(int timeout_ms, std::function<void(int, int)> on_progress);

    // Round-robin rotation over alive list. Falls back to raw list if unchecked.
    // Returns "" when direct.
    std::string GetNext();

    // Accessors
    size_t TotalCount() const               { return raw_.size(); }
    int    AliveCount() const               { return alive_count_.load(); }
    int    DeadCount()  const               { return dead_count_.load(); }
    const std::vector<std::string>& AliveList() const { return alive_; }

private:
    bool                     direct_      = true;
    std::vector<std::string> raw_;
    std::vector<std::string> alive_;

    std::atomic<int>    alive_count_{0};
    std::atomic<int>    dead_count_{0};
    std::atomic<size_t> rr_idx_{0};
    mutable std::mutex  mtx_;
};
