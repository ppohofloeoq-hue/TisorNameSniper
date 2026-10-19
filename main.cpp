#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

#include "Sniper.hpp"
#include "ProxyManager.hpp"
#include "Generator.hpp"

#include <atomic>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace ftxui;

// ─── ASCII logo (TISOR, ANSI Shadow style, Cyan) ─────────────────────────────
static Element MakeLogo() {
    static const std::vector<std::string> kLines = {
        " \u2588\u2588\u2588\u2588\u2588\u2588\u2588\u2588\u256c\u2588\u2588\u2566\u2588\u2588\u2588\u2588\u2588\u2588\u2588\u256c \u2588\u2588\u2588\u2588\u2588\u2588\u256c \u2588\u2588\u2588\u2588\u2588\u2588\u256c ",
        "    \u2588\u2588\u2554\u2550\u2550\u255d\u2588\u2588\u2551\u2588\u2588\u2554\u2550\u2550\u2550\u2550\u255d\u2588\u2588\u2554\u2550\u2550\u2550\u2588\u2588\u256c\u2588\u2588\u2554\u2550\u2550\u2588\u2588\u256c",
        "    \u2588\u2588\u2551   \u2588\u2588\u2551\u2588\u2588\u2588\u2588\u2588\u2588\u2588\u256c\u2588\u2588\u2551   \u2588\u2588\u2551\u2588\u2588\u2588\u2588\u2588\u2588\u2554\u255d ",
        "    \u2588\u2588\u2551   \u2588\u2588\u2551\u255a\u2550\u2550\u2550\u2550\u2588\u2588\u256c\u2588\u2588\u2551   \u2588\u2588\u2551\u2588\u2588\u2554\u2550\u2550\u2588\u2588\u256c ",
        "    \u2588\u2588\u2551   \u2588\u2588\u2551\u2588\u2588\u2588\u2588\u2588\u2588\u2588\u2551\u255a\u2588\u2588\u2588\u2588\u2588\u2588\u2554\u255d\u2588\u2588\u2551  \u2588\u2588\u2551 ",
        "    \u255a\u2550\u255d   \u255a\u2550\u255d\u255a\u2550\u2550\u2550\u2550\u2550\u2550\u255d \u255a\u2550\u2550\u2550\u2550\u2550\u255d \u255a\u2550\u255d  \u255a\u2550\u255d "
    };
    Elements rows;
    for (const auto& l : kLines)
        rows.push_back(text(l) | color(Color::Cyan) | bold);
    rows.push_back(
        text("               N A M E   S N I P E R               ")
        | color(Color::Blue) | bold
    );
    return vbox(std::move(rows)) | hcenter;
}

// ─── Log entry color mapping ──────────────────────────────────────────────────
static Element ColorLog(const SniperStats::LogEntry& e) {
    Color c;
    switch (e.kind) {
        case SniperStats::LogEntry::Kind::SUCCESS: c = Color::Green;      break;
        case SniperStats::LogEntry::Kind::FAIL:    c = Color::Red;        break;
        case SniperStats::LogEntry::Kind::WARN:    c = Color::Yellow;     break;
        default:                                   c = Color::GrayLight;  break;
    }
    return text(e.text) | color(c);
}

// ─── Pages ────────────────────────────────────────────────────────────────────
enum class Page {
    MAIN,
    SETTINGS,
    PROXY_SETUP,
    PROXY_CHECKING,
    PROXY_MANUAL,
    SINGLE_CHECK,
    RUNNING
};

// ─── main ─────────────────────────────────────────────────────────────────────
int main() {
    // ── Subsystems ────────────────────────────────────────────────────────────
    Config        config;
    SniperStats   stats;
    ProxyManager  proxy_mgr;
    Sniper        sniper(config, stats, proxy_mgr);

    sniper.LoadTokens("tokens.txt");

    auto screen = ScreenInteractive::Fullscreen();

    // ── Page state ────────────────────────────────────────────────────────────
    Page page = Page::MAIN;

    // ── Main menu ─────────────────────────────────────────────────────────────
    int main_sel = 0;
    const std::vector<std::string> kMainEntries = {
        "  [1]  Load from list.txt    ",
        "  [2]  Check single username ",
        "  [3]  Generate 3-characters ",
        "  [4]  Generate 4-characters ",
        "  [5]  Settings              ",
        "  [6]  Exit                  ",
    };

    // ── Settings ──────────────────────────────────────────────────────────────
    std::string s_webhook = config.webhook_url;
    std::string s_conc    = std::to_string(config.concurrency);
    std::string s_timeout = std::to_string(config.timeout_ms);
    std::string s_delay   = std::to_string(config.delay_ms);

    auto inp_webhook  = Input(&s_webhook, "https://discord.com/api/webhooks/...");
    auto inp_conc     = Input(&s_conc,    "25");
    auto inp_timeout  = Input(&s_timeout, "10000");
    auto inp_delay    = Input(&s_delay,   "3000");
    auto cont_settings = Container::Vertical({inp_webhook, inp_conc, inp_timeout, inp_delay});

    // ── Single check ──────────────────────────────────────────────────────────
    std::string single_name;
    std::string single_result;
    std::atomic<bool> single_busy{false};
    auto inp_single = Input(&single_name, "username to check...");

    // ── Proxy setup ───────────────────────────────────────────────────────────
    int  proxy_menu_sel = 0;
    std::string manual_proxy_str;
    std::atomic<int>  proxy_alive{0}, proxy_dead{0}, proxy_total{0};
    std::atomic<bool> proxy_is_checking{false};
    auto inp_proxy_manual = Input(&manual_proxy_str, "ip:port  or  user:pass@ip:port");

    // ── Active tab routing ────────────────────────────────────────────────────
    int active_tab = 0;
    auto all_inputs = Container::Tab(
        { cont_settings, inp_single, inp_proxy_manual },
        &active_tab
    );

    // ── Background thread handles ─────────────────────────────────────────────
    std::thread sniper_thread;
    std::thread proxy_check_thread;

    auto join_sniper = [&] {
        sniper.Stop();
        if (sniper_thread.joinable()) sniper_thread.join();
    };
    auto join_proxy_check = [&] {
        if (proxy_check_thread.joinable()) proxy_check_thread.join();
    };

    // ── Renderers ─────────────────────────────────────────────────────────────

    auto render_main = [&]() -> Element {
        Elements opts;
        for (int i = 0; i < (int)kMainEntries.size(); ++i) {
            auto e = text(kMainEntries[i]);
            if (i == main_sel)
                e = e | color(Color::Cyan) | bold | inverted;
            opts.push_back(e);
        }
        return vbox({
            MakeLogo(),
            separator(),
            vbox(std::move(opts)) | flex,
            separator(),
            hbox({
                text("  Tokens: ") | color(Color::Blue),
                text(std::to_string(sniper.TokenCount())) | bold,
                text("   Proxies alive: ") | color(Color::Blue),
                text(std::to_string(proxy_mgr.AliveCount())) | bold,
            }),
            text("  \u2191\u2193 navigate  \u23ce select  q quit") | color(Color::GrayDark),
        }) | border | flex;
    };

    auto render_settings = [&]() -> Element {
        return vbox({
            MakeLogo(),
            separator(),
            text("  \u25a0 S E T T I N G S") | bold | color(Color::Cyan) | hcenter,
            separator(),
            vbox({
                text("  Webhook URL:") | color(Color::Blue),
                inp_webhook->Render() | border,
                text("  Concurrency (threads):") | color(Color::Blue),
                inp_conc->Render() | border,
                text("  Timeout (ms):") | color(Color::Blue),
                inp_timeout->Render() | border,
                text("  Request delay between calls (ms):") | color(Color::Blue),
                inp_delay->Render() | border,
            }) | flex,
            separator(),
            hbox({
                text("  Tokens loaded: ") | color(Color::GrayDark),
                text(std::to_string(sniper.TokenCount())) | bold,
                text("   |   TAB to move between fields") | color(Color::GrayDark),
            }),
            text("  ESC = save + go back") | color(Color::GrayDark),
        }) | border | flex;
    };

    auto render_proxy_menu = [&]() -> Element {
        const std::vector<std::string> opts = {
            "  [1]  No proxy (direct connection) ",
            "  [2]  Load from proxy.txt          ",
            "  [3]  Enter proxy manually          ",
        };
        Elements rows;
        for (int i = 0; i < (int)opts.size(); ++i) {
            auto e = text(opts[i]);
            if (i == proxy_menu_sel) e = e | color(Color::Cyan) | bold | inverted;
            rows.push_back(e);
        }
        return vbox({
            MakeLogo(),
            separator(),
            text("  \u25a0 P R O X Y   S E T U P") | bold | color(Color::Cyan) | hcenter,
            separator(),
            vbox(std::move(rows)) | flex,
            separator(),
            text("  \u2191\u2193 navigate  \u23ce select  ESC back") | color(Color::GrayDark),
        }) | border | flex;
    };

    auto render_proxy_checking = [&]() -> Element {
        int alive  = proxy_alive.load();
        int dead   = proxy_dead.load();
        int total  = proxy_total.load();
        float prog = total > 0 ? static_cast<float>(alive + dead) / total : 0.f;
        bool busy  = proxy_is_checking.load();

        return vbox({
            MakeLogo(),
            separator(),
            text("  Checking " + std::to_string(total) + " proxies in parallel...")
                | bold | color(Color::Cyan) | hcenter,
            separator(),
            hbox({
                text("  "),
                gauge(prog) | flex | color(Color::Cyan),
                text("  " + std::to_string(alive + dead) + " / " + std::to_string(total)),
            }),
            hbox({
                text("  ALIVE: ") | color(Color::Green),
                text(std::to_string(alive)) | color(Color::Green) | bold,
                text("      DEAD: ") | color(Color::Red),
                text(std::to_string(dead)) | color(Color::Red) | bold,
            }),
            separator(),
            busy
                ? text("  Probing...") | color(Color::Yellow)
                : hbox({
                    text("  Done — ") | color(Color::Green),
                    text(std::to_string(alive)) | bold | color(Color::Green),
                    text(" proxies ready.  Press any key.") | color(Color::Green),
                  }),
        }) | border | flex;
    };

    auto render_proxy_manual = [&]() -> Element {
        return vbox({
            MakeLogo(),
            separator(),
            text("  \u25a0 MANUAL PROXY ENTRY") | bold | color(Color::Cyan) | hcenter,
            separator(),
            text("  Proxy (ip:port  or  user:pass@ip:port):") | color(Color::Blue),
            inp_proxy_manual->Render() | border,
            separator(),
            text("  ENTER to confirm   ESC to cancel") | color(Color::GrayDark),
        }) | border | flex;
    };

    auto render_single = [&]() -> Element {
        Elements content = {
            MakeLogo(),
            separator(),
            text("  \u25a0 S I N G L E   C H E C K") | bold | color(Color::Cyan) | hcenter,
            separator(),
            text("  Username:") | color(Color::Blue),
            inp_single->Render() | border,
            separator(),
        };
        if (!single_result.empty()) {
            Color rc = (single_result[0] == '+') ? Color::Green : Color::Red;
            content.push_back(
                text("  " + single_result) | color(rc) | bold
            );
            content.push_back(separator());
        }
        if (single_busy.load())
            content.push_back(text("  Checking...") | color(Color::Yellow));
        content.push_back(
            text("  ENTER to check   ESC to go back") | color(Color::GrayDark));
        return vbox(std::move(content)) | border | flex;
    };

    auto render_running = [&]() -> Element {
        int tot  = stats.total.load();
        int chk  = stats.checked.load();
        int snip = stats.sniped.load();
        int rl   = stats.rate_limited.load();
        int fail = stats.failed.load();
        float prog = tot > 0 ? static_cast<float>(chk) / tot : 0.f;
        bool running = stats.running.load();

        auto log_snap = stats.SnapshotLog();
        Elements log_elems;
        int start = (int)log_snap.size() > 16
            ? (int)log_snap.size() - 16 : 0;
        for (int i = start; i < (int)log_snap.size(); ++i)
            log_elems.push_back(ColorLog(log_snap[i]));
        if (log_elems.empty())
            log_elems.push_back(text("  (waiting...)") | color(Color::GrayDark));

        return vbox({
            MakeLogo(),
            separator(),
            hbox({
                vbox({
                    hbox({text("  Checked : ") | color(Color::Blue),
                          text(std::to_string(chk) + " / " + std::to_string(tot)) | bold}),
                    hbox({text("  Sniped  : ") | color(Color::Green),
                          text(std::to_string(snip)) | color(Color::Green) | bold}),
                    hbox({text("  RL Hits : ") | color(Color::Yellow),
                          text(std::to_string(rl))   | color(Color::Yellow)}),
                    hbox({text("  Failed  : ") | color(Color::Red),
                          text(std::to_string(fail))  | color(Color::Red)}),
                }) | flex,
                vbox({
                    hbox({text("  Tokens  : "), text(std::to_string(sniper.TokenCount()))}),
                    hbox({text("  Proxies : "), text(std::to_string(proxy_mgr.AliveCount()))}),
                    running
                        ? text("  [ \u25b6 RUNNING ]") | color(Color::Green) | bold
                        : text("  [ \u25a0 STOPPED ]") | color(Color::Red)   | bold,
                }) | flex,
            }),
            separator(),
            hbox({
                text("  "),
                gauge(prog) | flex | color(Color::Cyan),
                text("  " + std::to_string(static_cast<int>(prog * 100.f)) + "%"),
            }),
            separator(),
            vbox(std::move(log_elems)) | flex,
            separator(),
            running
                ? text("  Q / ESC to stop") | color(Color::GrayDark)
                : text("  Finished — press any key to return") | color(Color::Green) | bold,
        }) | border | flex;
    };

    // ── Root renderer ─────────────────────────────────────────────────────────
    auto main_renderer = Renderer(all_inputs, [&]() -> Element {
        switch (page) {
            case Page::MAIN:           return render_main();
            case Page::SETTINGS:       return render_settings();
            case Page::PROXY_SETUP:    return render_proxy_menu();
            case Page::PROXY_CHECKING: return render_proxy_checking();
            case Page::PROXY_MANUAL:   return render_proxy_manual();
            case Page::SINGLE_CHECK:   return render_single();
            case Page::RUNNING:        return render_running();
        }
        return text(""); // unreachable
    });

    // ── Event handler ─────────────────────────────────────────────────────────
    auto root = CatchEvent(main_renderer, [&](Event e) -> bool {

        // Always pass Custom events through — they just trigger re-render
        if (e == Event::Custom) return false;

        // ── MAIN ─────────────────────────────────────────────────────────────
        if (page == Page::MAIN) {

            auto activate = [&](int sel) {
                switch (sel) {

                case 0: { // Load from list.txt
                    std::ifstream f("list.txt");
                    if (!f.is_open()) {
                        stats.PushLog(SniperStats::LogEntry::Kind::WARN,
                            "[!] list.txt not found — place it next to the .exe");
                        return;
                    }
                    std::vector<std::string> targets;
                    std::string line;
                    while (std::getline(f, line)) {
                        line.erase(0, line.find_first_not_of(" \t\r\n"));
                        auto end = line.find_last_not_of(" \t\r\n");
                        if (end != std::string::npos) line.erase(end + 1);
                        if (!line.empty()) targets.push_back(line);
                    }
                    if (targets.empty()) {
                        stats.PushLog(SniperStats::LogEntry::Kind::WARN,
                            "[!] list.txt is empty");
                        return;
                    }
                    if (sniper.TokenCount() == 0) {
                        stats.PushLog(SniperStats::LogEntry::Kind::WARN,
                            "[!] No tokens loaded — add tokens to tokens.txt");
                        return;
                    }
                    stats.Reset();
                    join_sniper();
                    page = Page::RUNNING;
                    active_tab = 0;
                    sniper.on_update = [&] { screen.PostEvent(Event::Custom); };
                    sniper_thread = std::thread([&, tgts = std::move(targets)]() mutable {
                        sniper.RunList(std::move(tgts));
                    });
                    break;
                }

                case 1: // Single check
                    single_result.clear();
                    single_name.clear();
                    page = Page::SINGLE_CHECK;
                    active_tab = 1;
                    break;

                case 2: { // 3L generate
                    if (sniper.TokenCount() == 0) {
                        stats.PushLog(SniperStats::LogEntry::Kind::WARN,
                            "[!] No tokens loaded");
                        return;
                    }
                    stats.Reset();
                    join_sniper();
                    page = Page::RUNNING;
                    active_tab = 0;
                    sniper.on_update = [&] { screen.PostEvent(Event::Custom); };
                    sniper_thread = std::thread([&] {
                        auto targets = Generator::All3L();
                        Generator::Shuffle(targets);
                        sniper.RunList(std::move(targets));
                    });
                    break;
                }

                case 3: { // 4L generate
                    if (sniper.TokenCount() == 0) {
                        stats.PushLog(SniperStats::LogEntry::Kind::WARN,
                            "[!] No tokens loaded");
                        return;
                    }
                    stats.Reset();
                    join_sniper();
                    page = Page::RUNNING;
                    active_tab = 0;
                    sniper.on_update = [&] { screen.PostEvent(Event::Custom); };
                    sniper_thread = std::thread([&] {
                        auto targets = Generator::All4L();
                        Generator::Shuffle(targets);
                        sniper.RunList(std::move(targets));
                    });
                    break;
                }

                case 4: // Settings
                    s_webhook = config.webhook_url;
                    s_conc    = std::to_string(config.concurrency);
                    s_timeout = std::to_string(config.timeout_ms);
                    s_delay   = std::to_string(config.delay_ms);
                    page = Page::SETTINGS;
                    active_tab = 0;
                    break;

                case 5: // Exit
                    join_sniper();
                    join_proxy_check();
                    screen.ExitLoopClosure()();
                    break;
                }
            };

            if (e == Event::ArrowUp) {
                main_sel = std::max(0, main_sel - 1); return true;
            }
            if (e == Event::ArrowDown) {
                main_sel = std::min((int)kMainEntries.size() - 1, main_sel + 1); return true;
            }
            if (e == Event::Return) { activate(main_sel); return true; }

            for (int i = 0; i < 6; ++i) {
                if (e == Event::Character(std::string(1, char('1' + i)))) {
                    main_sel = i;
                    activate(i);
                    return true;
                }
            }
            if (e == Event::Character("q") || e == Event::Character("Q")) {
                join_sniper();
                join_proxy_check();
                screen.ExitLoopClosure()();
                return true;
            }
            return true; // eat all other keys on main page
        }

        // ── SETTINGS ─────────────────────────────────────────────────────────
        if (page == Page::SETTINGS) {
            if (e == Event::Escape) {
                config.webhook_url = s_webhook;
                try { config.concurrency = std::stoi(s_conc); }    catch (...) {}
                try { config.timeout_ms  = std::stoi(s_timeout); } catch (...) {}
                try { config.delay_ms    = std::stoi(s_delay); }   catch (...) {}
                page = Page::MAIN;
                return true;
            }
            // P key → proxy setup from within settings
            if (e == Event::Character("p") || e == Event::Character("P")) {
                proxy_menu_sel = 0;
                page = Page::PROXY_SETUP;
                return true;
            }
            return false; // let inputs handle it
        }

        // ── PROXY SETUP MENU ─────────────────────────────────────────────────
        if (page == Page::PROXY_SETUP) {
            if (e == Event::Escape)    { page = Page::SETTINGS; return true; }
            if (e == Event::ArrowUp)   { proxy_menu_sel = std::max(0, proxy_menu_sel - 1); return true; }
            if (e == Event::ArrowDown) { proxy_menu_sel = std::min(2, proxy_menu_sel + 1); return true; }

            auto do_proxy_select = [&](int opt) {
                if (opt == 0) {
                    proxy_mgr.SetDirect();
                    stats.PushLog(SniperStats::LogEntry::Kind::INFO,
                        "[*] Proxy: direct connection");
                    page = Page::SETTINGS;
                } else if (opt == 1) {
                    if (!proxy_mgr.LoadFromFile("proxy.txt")) {
                        stats.PushLog(SniperStats::LogEntry::Kind::WARN,
                            "[!] proxy.txt not found or empty");
                        page = Page::SETTINGS;
                        return;
                    }
                    proxy_total      = static_cast<int>(proxy_mgr.TotalCount());
                    proxy_alive      = 0;
                    proxy_dead       = 0;
                    proxy_is_checking = true;
                    page = Page::PROXY_CHECKING;

                    join_proxy_check();
                    proxy_check_thread = std::thread([&] {
                        proxy_mgr.CheckAll(5000, [&](int a, int d) {
                            proxy_alive = a;
                            proxy_dead  = d;
                            screen.PostEvent(Event::Custom);
                        });
                        proxy_is_checking = false;
                        screen.PostEvent(Event::Custom);
                    });
                } else {
                    manual_proxy_str.clear();
                    page = Page::PROXY_MANUAL;
                    active_tab = 2;
                }
            };

            if (e == Event::Return) { do_proxy_select(proxy_menu_sel); return true; }
            for (int i = 0; i < 3; ++i) {
                if (e == Event::Character(std::string(1, char('1' + i)))) {
                    proxy_menu_sel = i;
                    do_proxy_select(i);
                    return true;
                }
            }
            return true;
        }

        // ── PROXY CHECKING ────────────────────────────────────────────────────
        if (page == Page::PROXY_CHECKING) {
            if (!proxy_is_checking.load()) {
                // any key returns to settings
                join_proxy_check();
                page = Page::SETTINGS;
                return true;
            }
            return true; // eat input while checking
        }

        // ── PROXY MANUAL ENTRY ────────────────────────────────────────────────
        if (page == Page::PROXY_MANUAL) {
            if (e == Event::Escape) { page = Page::PROXY_SETUP; return true; }
            if (e == Event::Return && !manual_proxy_str.empty()) {
                proxy_mgr.SetManual(manual_proxy_str);
                stats.PushLog(SniperStats::LogEntry::Kind::INFO,
                    "[*] Proxy set: " + manual_proxy_str);
                page = Page::SETTINGS;
                active_tab = 0;
                return true;
            }
            return false; // let inp_proxy_manual handle char input
        }

        // ── SINGLE CHECK ──────────────────────────────────────────────────────
        if (page == Page::SINGLE_CHECK) {
            if (e == Event::Escape) {
                page = Page::MAIN;
                single_result.clear();
                return true;
            }
            if (e == Event::Return && !single_busy.load() && !single_name.empty()) {
                single_result = "Checking...";
                single_busy   = true;
                std::string name = single_name;
                std::thread([&, name] {
                    if (sniper.TokenCount() == 0) sniper.LoadTokens("tokens.txt");
                    bool avail = sniper.CheckSingle(name);
                    single_result = avail
                        ? "[+] AVAILABLE: " + name
                        : "[-] TAKEN:      " + name;
                    single_busy = false;
                    screen.PostEvent(Event::Custom);
                }).detach();
                return true;
            }
            return false; // let input handle typing
        }

        // ── RUNNING ───────────────────────────────────────────────────────────
        if (page == Page::RUNNING) {
            if (e == Event::Escape || e == Event::Character("q") || e == Event::Character("Q")) {
                if (stats.running.load()) {
                    sniper.Stop();
                } else {
                    join_sniper();
                    page = Page::MAIN;
                }
                return true;
            }
            if (!stats.running.load() && e == Event::Return) {
                join_sniper();
                page = Page::MAIN;
                return true;
            }
            return true;
        }

        return false;
    });

    // ── Register on_update before loop ───────────────────────────────────────
    sniper.on_update = [&] { screen.PostEvent(Event::Custom); };

    // ── Enter event loop ──────────────────────────────────────────────────────
    screen.Loop(root);

    // ── Clean shutdown ────────────────────────────────────────────────────────
    sniper.Stop();
    if (sniper_thread.joinable())      sniper_thread.join();
    if (proxy_check_thread.joinable()) proxy_check_thread.join();

    return 0;
}
