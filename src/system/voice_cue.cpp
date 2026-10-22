#include "system/voice_cue.hpp"
#include "system/voice_wav.hpp"

#include <unitree/robot/g1/audio/g1_audio_client.hpp>

#include <chrono>
#include <cstdio>

namespace kist {

const char* voice_cue_name(VoiceCue cue) {
    switch (cue) {
        case VoiceCue::Start:   return "start";
        case VoiceCue::Stop:    return "stop";
        case VoiceCue::Success: return "success";
        case VoiceCue::Fail:    return "fail";
        case VoiceCue::Partial: return "partial";
    }
    return "?";
}

VoiceCuePlayer::Settings VoiceCuePlayer::Settings::from_yaml(const YAML::Node& record_control) {
    Settings s;
    if (!record_control) return s;
    const auto v = record_control["voice"];
    if (!v) return s;
    if (v["enabled"])       s.enabled       = v["enabled"].as<bool>();
    if (v["dir"])           s.dir           = v["dir"].as<std::string>();
    if (v["app_name"])      s.app_name      = v["app_name"].as<std::string>();
    if (v["timeout_s"])     s.timeout_s     = v["timeout_s"].as<double>();
    if (v["play_margin_s"]) s.play_margin_s = v["play_margin_s"].as<double>();
    if (v["gain"])          s.gain          = v["gain"].as<double>();
    return s;
}

VoiceCuePlayer::VoiceCuePlayer() = default;
VoiceCuePlayer::~VoiceCuePlayer() { stop(); }

bool VoiceCuePlayer::start(const Settings& settings) {
    if (running_) return true;
    settings_ = settings;
    if (!settings_.enabled) return false;

    int loaded = 0;
    for (int i = 0; i < kVoiceCueCount; ++i) {
        const auto cue = static_cast<VoiceCue>(i);
        const std::string path = settings_.dir + "/" + voice_cue_name(cue) + ".wav";
        std::string err;
        if (load_wav16k_mono(path, pcm_[i], err)) {
            scale_pcm16(pcm_[i], settings_.gain);
            ++loaded;
        } else {
            std::fprintf(stderr, "[VoiceCue] %s: %s — cue '%s' silent\n", path.c_str(), err.c_str(), voice_cue_name(cue));
        }
    }
    if (loaded == 0) {
        std::fprintf(stderr, "[VoiceCue] no cue loaded from %s — voice off\n", settings_.dir.c_str());
        return false;
    }

    try {
        client_.reset(new unitree::robot::g1::AudioClient());
        client_->SetTimeout(static_cast<float>(settings_.timeout_s));
        client_->Init();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[VoiceCue] AudioClient init failed: %s — voice off\n", e.what());
        client_.reset();
        return false;
    }

    running_ = true;
    worker_ = std::thread(&VoiceCuePlayer::run, this);
    std::printf("[VoiceCue] %d/%d cues from %s -> robot speaker (app %s, gain %.2f)\n",
                loaded, kVoiceCueCount, settings_.dir.c_str(), settings_.app_name.c_str(), settings_.gain);
    return true;
}

void VoiceCuePlayer::say(VoiceCue cue) {
    if (!running_ || pcm_[static_cast<int>(cue)].empty()) return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back(cue);
    }
    cv_.notify_one();
}

void VoiceCuePlayer::stop() {
    if (!running_) return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
    }
    cv_.notify_one();
    if (worker_.joinable()) worker_.join();
    client_.reset();
}

void VoiceCuePlayer::run() {
    using clock = std::chrono::steady_clock;
    auto play_end = clock::now();   // when the robot falls silent
    for (;;) {
        VoiceCue cue;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [&] { return !queue_.empty() || !running_; });
            if (queue_.empty()) return;   // stopped and drained
            cue = queue_.front();
            queue_.pop_front();
        }
        // Never start a phrase over the previous one.
        std::this_thread::sleep_until(play_end);

        const auto& pcm = pcm_[static_cast<int>(cue)];
        const std::string stream_id = std::to_string(
            std::chrono::duration_cast<std::chrono::milliseconds>(clock::now().time_since_epoch()).count());
        const int32_t rc = client_->PlayStream(settings_.app_name, stream_id, pcm);
        const auto sent = clock::now();
        if (rc != 0) {
            std::fprintf(stderr, "[VoiceCue] PlayStream '%s' failed: %d (audio service unreachable?)\n",
                         voice_cue_name(cue), rc);
            play_end = sent;
            continue;
        }
        play_end = sent + std::chrono::milliseconds(static_cast<long>(
            1000.0 * (settings_.play_margin_s + double(pcm.size()) / kVoiceBytesPerSecond)));
    }
}

}  // namespace kist
