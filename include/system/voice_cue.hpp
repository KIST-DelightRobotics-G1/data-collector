#pragma once
// Voice cues on the robot's own speaker — one short Korean phrase per episode
// event so the VR pilot hears what the collector did without looking at a
// terminal. Same path as kist-drl-g1-cortex's speaker_node: Unitree SDK
// AudioClient.PlayStream (DDS RPC to the G1 audio service) with 16 kHz mono
// 16-bit PCM, loaded once from assets/voice/<cue>.wav.
//
// say() only enqueues; a worker thread does the RPC (it blocks up to the
// client timeout when the audio service is unreachable) and spaces cues so a
// phrase is never cut off by the next one.
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <yaml-cpp/yaml.h>

namespace unitree { namespace robot { namespace g1 { class AudioClient; } } }

namespace kist {

enum class VoiceCue : uint8_t { Start, Stop, Success, Fail, Partial };
constexpr int kVoiceCueCount = 5;
const char* voice_cue_name(VoiceCue cue);   // file stem: start / stop / success / fail / partial

class VoiceCuePlayer {
public:
    struct Settings {
        bool        enabled       = true;
        std::string dir           = "assets/voice";      // <dir>/<cue>.wav, 16 kHz mono 16-bit
        std::string app_name      = "kist_data_collector"; // AudioClient stream owner tag
        double      timeout_s     = 2.0;                 // per-RPC reply wait
        double      play_margin_s = 0.2;                 // robot starts playing a little after the send
        double      gain          = 0.5;                 // sample scale 0..1, applied once at load
        // record_control: node of config.yaml (reads its `voice:` child).
        static Settings from_yaml(const YAML::Node& record_control);
    };

    VoiceCuePlayer();
    ~VoiceCuePlayer();

    // Loads the WAVs and creates the AudioClient on the SDK's ChannelFactory
    // (initialized here for domain_id; the NIC comes from CYCLONEDDS_URI).
    // Returns false when disabled or when no cue could be loaded — the
    // caller carries on without voice either way.
    bool start(const Settings& settings, int domain_id);
    void say(VoiceCue cue);   // non-blocking; dropped when not started
    void stop();              // plays what is queued, then joins the worker

private:
    void run();

    Settings settings_;
    std::vector<uint8_t> pcm_[kVoiceCueCount];
    std::unique_ptr<unitree::robot::g1::AudioClient> client_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<VoiceCue> queue_;
    std::thread worker_;
    bool running_ = false;
};

}  // namespace kist
