#include "Sniper.hpp"
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <chrono>
#include <sstream>

// ─── HTTP fingerprint ─────────────────────────────────────────────────────────
const std::string Sniper::kUserAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";

// Base64( {"os":"Windows","browser":"Chrome","device":"","system_locale":"en-US",
//          "browser_user_agent":"Mozilla/5.0 (Windows NT 10.0; Win64; x64) ...",
//          "browser_version":"120.0.0.0","os_version":"10","referrer":"",
//          "referring_domain":"","referrer_current":"","referring_domain_current":"",
//          "release_channel":"stable","client_build_number":261725,
//          "client_event_source":null} )
const std::string Sniper::kSuperProps =
    "eyJvcyI6IldpbmRvd3MiLCJicm93c2VyIjoiQ2hyb21lIiwiZGV2aWNlIjoiIiwic3lzdGVtX2"
    "xvY2FsZSI6ImVuLVVTIiwiYnJvd3Nlcl91c2VyX2FnZW50IjoiTW96aWxsYS81LjAgKFdpbmRv"
    "d3MgTlQgMTAuMDsgV2luNjQ7IHg2NCkgQXBwbGVXZWJLaXQvNTM3LjM2IChLSFRNTCwgbGlrZS"
    "BHZWN rbykgQ2hyb21lLzEyMC4wLjAuMCBTYWZhcmkvNTM3LjM2IiwiYnJvd3Nlcl92ZXJzaW9u"
    "IjoiMTIwLjAuMC4wIiwib3NfdmVyc2lvbiI6IjEwIiwicmVmZXJyZXIiOiIiLCJyZWZlcnJpbmdf"
    "ZG9tYWluIjoiIiwicmVmZXJyZXJfY3VycmVudCI6IiIsInJlZmVycmluZ19kb21haW5fY3VycmVu"
    "dCI6IiIsInJlbGVhc2VfY2hhbm5lbCI6InN0YWJsZSIsImNsaWVudF9idWlsZF9udW1iZXIiOjI2"
    "MTcyNSwiY2xpZW50X2V2ZW50X3NvdXJjZSI6bnVsbH0=";

// ─── ThreadPool ───────────────────────────────────────────────────────────────
ThreadPool::ThreadPool(size_t n) {
    workers_.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        workers_.emplace_back([this] {
            for (;;) {
                std::function<void()> task;
                {
                    std::unique_lock lk(mtx_);
                    cv_.wait(lk, [this] { return stop_ || !tasks_.empty(); });
                    if (stop_ && tasks_.empty()) return;
                    task = std::move(tasks_.front());
                    tasks_.pop();
                    ++active_;
                }
                task();
                --active_;
                cv_.notify_all();
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    { std::lock_guard lk(mtx_); stop_ = true; }
    cv_.notify_all();
    for (auto& w : workers_) if (w.joinable()) w.join();
}

void ThreadPool::Enqueue(std::function<void()> task) {
    { std::lock_guard lk(mtx_); tasks_.push(std::move(task)); }
    cv_.notify_one();
}

void ThreadPool::WaitAll() {
    std::unique_lock lk(mtx_);
    cv_.wait(lk, [this] { return tasks_.empty() && active_.load() == 0; });
}

// ─── Sniper ────────────────────────────────────────────────────────────────────
Sniper::Sniper(Config& cfg, SniperStats& stats, ProxyManager& pm)
    : config_(cfg), stats_(stats), proxy_mgr_(pm),
      capsolver_(cfg.capsolver_key),
      webhook_(cfg.webhook_url)
{}

void Sniper::FireUpdate() {
    if (on_update) on_update();
}

int Sniper::LoadTokens(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        stats_.PushLog(SniperStats::LogEntry::Kind::WARN,
            "[!] " + path + " not found — place it next to the .exe");
        FireUpdate();
        return 0;
    }
    std::lock_guard lk(token_mtx_);
    token_queue_.clear();
    std::string line;
    while (std::getline(f, line)) {
        auto& s = line;
        s.erase(0, s.find_first_not_of(" \t\r\n"));
        auto e = s.find_last_not_of(" \t\r\n");
        if (e != std::string::npos) s.erase(e + 1);
        if (!s.empty()) token_queue_.push_back(s);
    }
    return static_cast<int>(token_queue_.size());
}

int Sniper::TokenCount() const {
    std::lock_guard lk(token_mtx_);
    return static_cast<int>(token_queue_.size());
}

std::string Sniper::PopToken() {
    std::unique_lock lk(token_mtx_);
    token_cv_.wait(lk, [this] {
        return !token_queue_.empty() || stop_flag_.load();
    });
    if (stop_flag_.load() || token_queue_.empty()) return "";
    std::string t = std::move(token_queue_.front());
    token_queue_.pop_front();
    return t;
}

void Sniper::PushToken(std::string t) {
    if (t.empty()) return;
    { std::lock_guard lk(token_mtx_); token_queue_.push_back(std::move(t)); }
    token_cv_.notify_one();
}

void Sniper::Stop() {
    stop_flag_ = true;
    token_cv_.notify_all(); // unblock any waiting PopToken
}

// ─── Core PATCH ──────────────────────────────────────────────────────────────
Sniper::PatchResult Sniper::AttemptPatch(
    const std::string& target,
    const std::string& token,
    const std::string& proxy,
    const std::string& captcha_key,
    RLInfo&            rl_out,
    CaptchaTask&       cap_out
) {
    nlohmann::json body_j = {{"username", target}};
    if (!captcha_key.empty()) body_j["captcha_key"] = captcha_key;

    cpr::Header hdrs{
        {"Authorization",      token},
        {"Content-Type",       "application/json"},
        {"User-Agent",         kUserAgent},
        {"X-Super-Properties", kSuperProps},
        {"X-Discord-Locale",   "en-US"},
        {"Accept",             "*/*"},
        {"Accept-Language",    "en-US,en;q=0.9"},
        {"Origin",             "https://discord.com"},
        {"Referer",            "https://discord.com/channels/@me"}
    };

    long to = static_cast<long>(config_.timeout_ms);

    cpr::Response r;
    if (proxy.empty()) {
        r = cpr::Patch(
            cpr::Url{"https://discord.com/api/v9/users/@me"},
            hdrs,
            cpr::Body{body_j.dump()},
            cpr::Timeout{to}
        );
    } else {
        r = cpr::Patch(
            cpr::Url{"https://discord.com/api/v9/users/@me"},
            hdrs,
            cpr::Body{body_j.dump()},
            cpr::Proxies{{"https", proxy}},
            cpr::Timeout{to}
        );
    }

    if (r.status_code == 200) return PatchResult::SUCCESS;

    if (r.status_code == 429) {
        auto j = nlohmann::json::parse(r.text, nullptr, false);
        if (!j.is_discarded()) rl_out.retry_after = j.value("retry_after", 1.0);
        else                   rl_out.retry_after = 1.0;
        return PatchResult::RATE_LIMITED;
    }

    if (r.status_code == 400) {
        auto j = nlohmann::json::parse(r.text, nullptr, false);
        if (!j.is_discarded() && j.contains("captcha_key")) {
            cap_out.site_key = j.value("captcha_sitekey", "4c672d35-0701-42b2-88c3-78380b0db560");
            cap_out.rq_data  = j.value("captcha_rqdata",  "");
            cap_out.rq_token = j.value("captcha_rqtoken", "");
            return PatchResult::CAPTCHA;
        }
        return PatchResult::TAKEN;
    }

    if (r.status_code == 401 || r.status_code == 403) {
        stats_.PushLog(SniperStats::LogEntry::Kind::WARN,
            "[!] Token flagged/banned (" + std::to_string(r.status_code) + "), rotating");
        return PatchResult::ERROR;
    }

    return PatchResult::ERROR;
}

// ─── RunList ─────────────────────────────────────────────────────────────────
void Sniper::RunList(std::vector<std::string> targets) {
    stop_flag_           = false;
    stats_.running       = true;
    stats_.total         = static_cast<int>(targets.size());
    stats_.checked       = 0;
    stats_.sniped        = 0;
    stats_.rate_limited  = 0;
    stats_.failed        = 0;

    webhook_.SetUrl(config_.webhook_url);
    capsolver_.SetKey(config_.capsolver_key);

    ThreadPool pool(static_cast<size_t>(config_.concurrency));

    for (const auto& target : targets) {
        if (stop_flag_) break;

        pool.Enqueue([this, target] {
            if (stop_flag_) { ++stats_.checked; return; }

            std::string token = PopToken();
            if (token.empty()) { ++stats_.checked; return; } // stop signalled

            std::string proxy = proxy_mgr_.GetNext();
            auto t0 = std::chrono::steady_clock::now();

            std::string cap_key;

            for (int attempt = 0; attempt < 5 && !stop_flag_; ++attempt) {
                RLInfo      rl;
                CaptchaTask cap;

                PatchResult res = AttemptPatch(target, token, proxy, cap_key, rl, cap);
                cap_key.clear();

                switch (res) {

                case PatchResult::SUCCESS: {
                    long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0).count();

                    ++stats_.sniped;
                    ++stats_.checked;
                    stats_.PushLog(SniperStats::LogEntry::Kind::SUCCESS,
                        "[+] SNIPED: " + target + "  (" + std::to_string(ms) + " ms)");

                    // Persist
                    {
                        static std::mutex file_mtx;
                        std::lock_guard lk(file_mtx);
                        std::ofstream out("sniped.txt", std::ios::app);
                        out << target << "\t" << token << "\n";
                    }

                    webhook_.SendSuccess(target, token, proxy, ms);
                    PushToken(std::move(token));
                    FireUpdate();
                    return;
                }

                case PatchResult::TAKEN:
                    ++stats_.checked;
                    stats_.PushLog(SniperStats::LogEntry::Kind::FAIL,
                        "[-] TAKEN:   " + target);
                    PushToken(std::move(token));
                    FireUpdate();
                    return;

                case PatchResult::RATE_LIMITED:
                    ++stats_.rate_limited;
                    stats_.PushLog(SniperStats::LogEntry::Kind::WARN,
                        "[~] RATE_LIM: " + target +
                        "  (retry_after " + std::to_string(rl.retry_after) + "s)");

                    PushToken(std::move(token));
                    proxy = proxy_mgr_.GetNext();
                    token = PopToken();
                    if (token.empty()) { ++stats_.checked; FireUpdate(); return; }

                    std::this_thread::sleep_for(std::chrono::milliseconds(
                        static_cast<long long>(rl.retry_after * 1000.0)));

                    FireUpdate();
                    continue;

                case PatchResult::CAPTCHA:
                    stats_.PushLog(SniperStats::LogEntry::Kind::WARN,
                        "[?] CAPTCHA: " + target + " — solving...");
                    FireUpdate();

                    cap_key = capsolver_.Solve(cap);
                    if (cap_key.empty()) {
                        stats_.PushLog(SniperStats::LogEntry::Kind::WARN,
                            "[!] Capsolver failed on " + target);
                        ++stats_.failed;
                        PushToken(std::move(token));
                        ++stats_.checked;
                        FireUpdate();
                        return;
                    }
                    stats_.PushLog(SniperStats::LogEntry::Kind::INFO,
                        "[*] Captcha solved — retrying " + target);
                    FireUpdate();
                    continue; // retry with cap_key set

                case PatchResult::ERROR:
                    // Rotate token + proxy, one retry
                    PushToken(std::move(token));
                    token = PopToken();
                    if (token.empty()) { ++stats_.failed; ++stats_.checked; FireUpdate(); return; }
                    proxy = proxy_mgr_.GetNext();
                    continue;
                }
            }

            ++stats_.failed;
            ++stats_.checked;
            PushToken(std::move(token));

            if (config_.delay_ms > 0) {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(config_.delay_ms));
            }

            FireUpdate();
        });
    }

    pool.WaitAll();
    stats_.running = false;
    FireUpdate();
}

bool Sniper::CheckSingle(const std::string& target) {
    if (TokenCount() == 0) return false;

    std::string token = PopToken();
    if (token.empty()) return false;

    std::string proxy = proxy_mgr_.GetNext();
    RLInfo rl; CaptchaTask cap;

    auto res = AttemptPatch(target, token, proxy, "", rl, cap);
    PushToken(std::move(token));

    return res == PatchResult::SUCCESS;
}
