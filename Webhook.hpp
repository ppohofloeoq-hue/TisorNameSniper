#pragma once
#include <string>

class Webhook {
public:
    explicit Webhook(std::string url = "") : url_(std::move(url)) {}

    void        SetUrl(const std::string& url) { url_ = url; }
    const std::string& Url() const             { return url_; }

    // Fire-and-forget embed POST. Blocks up to 5 s.
    void SendSuccess(
        const std::string& username,
        const std::string& token,      // masked internally
        const std::string& proxy,      // shown as "Direct" if empty
        long long          ms_elapsed
    );

private:
    std::string url_;

    static std::string MaskToken(const std::string& token);
    static std::string ISOTimestamp();
};
