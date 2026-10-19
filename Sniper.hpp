#pragma once

#include "ProxyManager.hpp"
#include "Capsolver.hpp"
#include "Webhook.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

// ─── SniperStats — read by UI thread ─────────────────────────────────────────
struct SniperStats {
    std::atomic<int>  total{0};
    std::atomic<int>  checked{0};
    std::atomic<int>  sniped{0};
    std::atomic<int>  rate_limited{0};
    std::atomic<int>  failed{0};
    std::atomic<bool> running{false};

    struct LogEntry {
        enum class Kind { INFO, SUCCESS, FAIL, WARN };
        Kind        kind;
        std::string text;
    };

    void PushLog(LogEntry::Kind k, std::string msg) {
        std::lock_guard lk(log_mtx_);
        log_.push_back({ k, std::move(msg) });
        if (static_cast<int>(log_.size()) > kMaxLog) log_.pop_front();
    }

    std::vector<LogEntry> SnapshotLog() const {
        std::lock_guard lk(log_mtx_);
        return { log_.begin(), log_.end() };
    }

    void Reset() {
        total = checked = sniped = rate_limited = failed = 0;
        std::lock_guard lk(log_mtx_);
        log_.clear();
    }

private:
    static constexpr int   kMaxLog = 60;
    std::deque<LogEntry>   log_;
    mutable std::mutex     log_mtx_;
};

// ─── Config — written by Settings screen ─────────────────────────────────────
struct Config {
    std::string  webhook_url;
    int          concurrency  = 25;
    int          timeout_ms   = 10000;
    int          delay_ms     = 3000;
    std::string  capsolver_key =
        "CAP-935C7FEC50264CAA8D031CF2DD9E5031AEE2AED76DD5CDBB4BBE889CFF8365FE";
};

// ─── ThreadPool ───────────────────────────────────────────────────────────────
class ThreadPool {
public:
    explicit ThreadPool(size_t n);
    ~ThreadPool();

    void Enqueue(std::function<void()> task);
    // Blocks until task queue empty AND all workers idle
    void WaitAll();

private:
    std::vector<std::thread>          workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex                        mtx_;
    std::condition_variable           cv_;
    std::atomic<int>                  active_{ 0 };
    bool                              stop_{ false };
};

// ─── Sniper ───────────────────────────────────────────────────────────────────
class Sniper {
public:
    Sniper(Config& config, SniperStats& stats, ProxyManager& proxy_mgr);

    // Reads tokens.txt (or given path). Returns count loaded.
    int  LoadTokens(const std::string& path = "tokens.txt");
    int  TokenCount() const;

    // Blocking snipe loop — run in a std::thread.
    void RunList(std::vector<std::string> targets);

    // Blocking single check. Returns true if username is claimable.
    bool CheckSingle(const std::string& target);

    // Signal RunList to exit.
    void Stop();
    bool IsRunning() const { return stats_.running.load(); }

    // Called by worker threads after any state change — use to trigger UI refresh.
    std::function<void()> on_update;

private:
    enum class PatchResult { SUCCESS, TAKEN, RATE_LIMITED, CAPTCHA, ERROR };

    struct RLInfo    { double retry_after = 1.0; };

    PatchResult AttemptPatch(
        const std::string& target,
        const std::string& token,
        const std::string& proxy,
        const std::string& captcha_key,
        RLInfo&            rl_out,
        CaptchaTask&       cap_out
    );

    std::string PopToken();          // blocks until token available or stop
    void        PushToken(std::string);
    void        FireUpdate();

    Config&        config_;
    SniperStats&   stats_;
    ProxyManager&  proxy_mgr_;
    Capsolver      capsolver_;
    Webhook        webhook_;

    std::deque<std::string> token_queue_;
    mutable std::mutex      token_mtx_;
    std::condition_variable token_cv_;

    std::atomic<bool> stop_flag_{ false };

    // HTTP constants
    static const std::string kUserAgent;
    static const std::string kSuperProps; // pre-encoded X-Super-Properties
};
