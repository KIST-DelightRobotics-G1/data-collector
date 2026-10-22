// kist-data-collector — records every enabled stream (cameras, lowstate,
// dex3 hands, UWB, command streams, gearsonic tokens) into session
// directories (compressed payloads verbatim + CSV indices; see
// common/session.hpp for the layout).
//
// Episodes are driven remotely (config `record_control.enabled`, default
// on): the process starts idle and waits for kist_msgs::RecordCommand on
// rt/kist/record_cmd — published by kist-gearsonic-inference from the VR
// controller: B tap = start, B double-tap = stop, then A tap = success /
// A double-tap = fail / A triple-tap = partial (keep, not a clean success)
// -> meta.yaml `result:`. One process records any number of episodes, one
// session directory each.
//
// With record_control disabled the legacy flow runs: recording starts at
// launch, Ctrl-C stops it, and one keypress labels it (S / F / P).
// In both modes Ctrl-C / SIGTERM is the exit: a recording in progress is
// closed first, and an interactive Ctrl-C offers the keypress label.
//
//   ./kist-data-collector [config_path]      (default config/config.yaml)

#include "common/config.hpp"
#include "common/dds_config.hpp"
#include "system/data_collector.hpp"
#include "system/record_command_listener.hpp"
#include "system/record_control.hpp"
#include "system/voice_cue.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <string>
#include <thread>
#include <termios.h>
#include <unistd.h>

static std::atomic<bool> g_stop{false};
static std::atomic<int>  g_stop_signal{0};

// One keypress, no Enter: S = success, F = fail, P = partial. Empty string = unlabeled
// (stdin not a terminal, or the operator pressed Ctrl-C again to skip).
static std::string prompt_result() {
    if (!isatty(STDIN_FILENO)) return "";
    // Re-register SIGINT without SA_RESTART so a second Ctrl-C interrupts
    // the blocking read() (glibc's signal() restarts syscalls by default).
    struct sigaction sa{};
    sa.sa_handler = [](int) {};
    sigaction(SIGINT, &sa, nullptr);
    termios saved{};
    if (tcgetattr(STDIN_FILENO, &saved) != 0) return "";
    termios raw = saved;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    std::printf("label this episode — [S]uccess / [F]ail / [P]artial (Ctrl-C = skip): ");
    std::fflush(stdout);
    std::string result;
    for (;;) {
        char c = 0;
        if (read(STDIN_FILENO, &c, 1) <= 0) break;   // EOF or Ctrl-C
        if (c == 's' || c == 'S') { result = "success"; break; }
        if (c == 'f' || c == 'F') { result = "fail";    break; }
        if (c == 'p' || c == 'P') { result = "partial"; break; }
    }
    tcsetattr(STDIN_FILENO, TCSANOW, &saved);
    std::printf("%s\n", result.empty() ? "(unlabeled)" : result.c_str());
    return result;
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    const std::string config_path = (argc >= 2) ? argv[1] : "config/config.yaml";
    kist::Config::instance().load(config_path);
    const auto& root = kist::Config::instance().root();
    auto settings = kist::DataCollector::Settings::from_yaml(root);
    if (!kist::apply_dds_config(root, &settings.dds_uri)) return 1;

    const auto rc = root["record_control"];
    const bool remote = !rc || !rc["enabled"] || rc["enabled"].as<bool>();

    std::signal(SIGINT,  [](int) { g_stop_signal = SIGINT;  g_stop = true; });
    std::signal(SIGTERM, [](int) { g_stop_signal = SIGTERM; g_stop = true; });

    kist::DataCollector         collector;
    kist::RecordCommandListener listener;
    kist::VoiceCuePlayer        voice;      // robot-speaker cue per episode event
    kist::RecordState           state = kist::RecordState::Idle;

    if (remote) {
        if (!listener.start(settings.domain_id, /*network_interface=*/""))
            return 1;
        voice.start(kist::VoiceCuePlayer::Settings::from_yaml(rc), settings.domain_id);   // optional
        std::printf("[kist_data_collector] idle — waiting for %s (VR: B tap start, B double-tap stop, "
                    "A tap success / double-tap fail / triple-tap partial). Ctrl-C exits.\n", kist::kRecordCmdTopic);
    } else {
        if (!collector.start(settings)) return 1;
        state = kist::RecordState::Recording;
    }

    auto window = std::chrono::steady_clock::now();
    while (!g_stop) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        if (remote) {
            for (auto action : listener.drain()) {
                const auto step = kist::record_fsm(state, action);
                if (step.effect == kist::RecordEffect::None) {
                    std::printf("[kist_data_collector] %s ignored (%s, state %s)\n",
                                kist::record_action_name(action), step.note,
                                kist::record_state_name(state));
                    continue;
                }
                if (step.note[0])
                    std::printf("[kist_data_collector] %s: %s\n", kist::record_action_name(action), step.note);
                switch (step.effect) {
                    case kist::RecordEffect::StartSession:
                        if (!collector.start(settings)) {
                            std::printf("[kist_data_collector] start failed — staying %s\n",
                                        kist::record_state_name(state));
                            continue;
                        }
                        std::printf("[kist_data_collector] recording -> %s\n", collector.session_dir().c_str());
                        voice.say(kist::VoiceCue::Start);
                        break;
                    case kist::RecordEffect::StopSession:
                        collector.stop();
                        std::printf("[kist_data_collector] stopped — awaiting label (A tap success / double-tap fail / "
                                    "triple-tap partial; B tap starts the next episode unlabeled)\n");
                        voice.say(kist::VoiceCue::Stop);
                        break;
                    case kist::RecordEffect::WriteSuccess:
                        collector.write_result("success");
                        std::printf("[kist_data_collector] result: success -> %s\n", collector.session_dir().c_str());
                        voice.say(kist::VoiceCue::Success);
                        break;
                    case kist::RecordEffect::WriteFail:
                        collector.write_result("fail");
                        std::printf("[kist_data_collector] result: fail -> %s\n", collector.session_dir().c_str());
                        voice.say(kist::VoiceCue::Fail);
                        break;
                    case kist::RecordEffect::WritePartial:
                        collector.write_result("partial");
                        std::printf("[kist_data_collector] result: partial -> %s\n", collector.session_dir().c_str());
                        voice.say(kist::VoiceCue::Partial);
                        break;
                    default: break;
                }
                state = step.next;
            }
        }

        const auto now = std::chrono::steady_clock::now();
        if (now - window < std::chrono::seconds(1)) continue;
        window = now;
        if (state == kist::RecordState::Recording)
            collector.print_report();
    }

    // Exit: close a recording in progress; an interactive Ctrl-C may still
    // label it (or a stopped-but-unlabeled episode) from the keyboard. A
    // SIGTERM (service manager, kill) must not wait on keyboard input.
    voice.stop();        // says what is still queued, then releases the client
    listener.stop();
    if (state == kist::RecordState::Recording) {
        collector.stop();
        state = kist::RecordState::AwaitingLabel;
    }
    if (state == kist::RecordState::AwaitingLabel && g_stop_signal == SIGINT)
        collector.write_result(prompt_result());
    return 0;
}
