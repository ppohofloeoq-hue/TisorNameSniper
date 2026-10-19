#pragma once
#include <string>

// Data extracted from a Discord captcha challenge response
struct CaptchaTask {
    std::string site_key; // captcha_sitekey
    std::string rq_data;  // captcha_rqdata
    std::string rq_token; // captcha_rqtoken
};

class Capsolver {
public:
    explicit Capsolver(std::string api_key);

    // Submits HCaptchaTaskProxyless, polls until solution ready.
    // Returns gRecaptchaResponse string, or "" on failure / timeout.
    std::string Solve(const CaptchaTask& task);

    void SetKey(const std::string& key) { api_key_ = key; }

private:
    std::string api_key_;

    static constexpr const char* kCreateUrl  = "https://api.capsolver.com/createTask";
    static constexpr const char* kResultUrl  = "https://api.capsolver.com/getTaskResult";
    static constexpr const char* kSiteUrl    = "https://discord.com";
    static constexpr const char* kUserAgent  =
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
        "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";

    // Returns taskId or "" on error
    std::string CreateTask(const CaptchaTask& task);

    // Polls every 3 s up to 90 s. Returns solution or "".
    std::string PollResult(const std::string& task_id);
};
