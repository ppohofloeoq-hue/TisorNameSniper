#include "Capsolver.hpp"
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <chrono>

Capsolver::Capsolver(std::string api_key) : api_key_(std::move(api_key)) {}

std::string Capsolver::CreateTask(const CaptchaTask& task) {
    nlohmann::json body = {
        {"clientKey", api_key_},
        {"task", {
            {"type",       "HCaptchaTaskProxyless"},
            {"websiteURL", kSiteUrl},
            {"websiteKey", task.site_key},
            {"userAgent",  kUserAgent},
            {"data",       task.rq_data},
            {"metadata",   {{"rqdata", task.rq_data}}}
        }}
    };

    auto r = cpr::Post(
        cpr::Url{kCreateUrl},
        cpr::Header{{"Content-Type", "application/json"}},
        cpr::Body{body.dump()},
        cpr::Timeout{15000}
    );

    if (r.status_code != 200) return "";

    auto j = nlohmann::json::parse(r.text, nullptr, false);
    if (j.is_discarded() || j.value("errorId", 1) != 0) return "";

    return j.value("taskId", "");
}

std::string Capsolver::PollResult(const std::string& task_id) {
    nlohmann::json body = {
        {"clientKey", api_key_},
        {"taskId",    task_id}
    };

    // Poll up to 30 × 3 s = 90 s
    for (int i = 0; i < 30; ++i) {
        std::this_thread::sleep_for(std::chrono::seconds(3));

        auto r = cpr::Post(
            cpr::Url{kResultUrl},
            cpr::Header{{"Content-Type", "application/json"}},
            cpr::Body{body.dump()},
            cpr::Timeout{15000}
        );

        if (r.status_code != 200) continue;

        auto j = nlohmann::json::parse(r.text, nullptr, false);
        if (j.is_discarded()) continue;
        if (j.value("errorId", 1) != 0) return "";

        if (j.value("status", "") == "ready") {
            return j.at("solution").value("gRecaptchaResponse", "");
        }
        // status == "processing" → keep polling
    }
    return ""; // timed out
}

std::string Capsolver::Solve(const CaptchaTask& task) {
    std::string tid = CreateTask(task);
    if (tid.empty()) return "";
    return PollResult(tid);
}
