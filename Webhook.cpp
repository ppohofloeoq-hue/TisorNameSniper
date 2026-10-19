#include "Webhook.hpp"
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <sstream>
#include <iomanip>

std::string Webhook::MaskToken(const std::string& t) {
    if (t.size() <= 20) return "***";
    return t.substr(0, 12) + "..." + t.substr(t.size() - 6);
}

std::string Webhook::ISOTimestamp() {
    using namespace std::chrono;
    auto tt = system_clock::to_time_t(system_clock::now());
    tm utc{};
    gmtime_s(&utc, &tt);
    std::ostringstream ss;
    ss << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S.000Z");
    return ss.str();
}

void Webhook::SendSuccess(
    const std::string& username,
    const std::string& token,
    const std::string& proxy,
    long long          ms_elapsed
) {
    if (url_.empty()) return;

    nlohmann::json embed = {
        {"title",  "\u2705 Username Claimed!"},
        {"color",  0x00D1FF},
        {"fields", nlohmann::json::array({
            {{"name","Username"}, {"value", "`" + username + "`"},          {"inline", true}},
            {{"name","Token"},    {"value", "`" + MaskToken(token) + "`"},  {"inline", true}},
            {{"name","Proxy"},    {"value", proxy.empty() ? "Direct" : proxy}, {"inline", true}},
            {{"name","Time"},     {"value", std::to_string(ms_elapsed) + " ms"}, {"inline", true}}
        })},
        {"footer",    {{"text", "Tisor Name Sniper"}}},
        {"timestamp", ISOTimestamp()}
    };

    nlohmann::json payload = {{"embeds", nlohmann::json::array({embed})}};

    cpr::Post(
        cpr::Url{url_},
        cpr::Header{{"Content-Type", "application/json"}},
        cpr::Body{payload.dump()},
        cpr::Timeout{5000}
    );
    // response ignored — best-effort delivery
}
