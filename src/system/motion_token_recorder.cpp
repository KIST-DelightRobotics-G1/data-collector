#include "system/motion_token_recorder.hpp"

#include "gearsonic_recorder/motion_token_rows.hpp"


#include <filesystem>
#include <iostream>

namespace kist {

MotionTokenRecorder::MotionTokenRecorder() = default;
MotionTokenRecorder::~MotionTokenRecorder() { stop(); }

bool MotionTokenRecorder::start(int domain_id, const std::string& network_interface,
                                const std::string& session_dir,
                                size_t queue_capacity) {
    if (running_) return true;

    const auto csv_path = std::filesystem::path(session_dir) / "motion_token.csv";
    if (!rec_.open(csv_path.string(), motion_token_header(), &motion_token_row,
                   queue_capacity)) {
        std::cerr << "[MotionTokenRecorder] cannot open " << csv_path << "\n";
        return false;
    }

    // Waitset reader (no listener — see common/waitset_subscriber.hpp); the
    // NIC comes from CYCLONEDDS_URI, so network_interface is informational.
    if (!sub_.start(domain_id, kMotionTokenTopic, /*depth=*/64,
                    [this](const kist_msgs::MotionTokenState& m) { on_token(&m); })) {
        std::cerr << "[MotionTokenRecorder] DDS reader failed on " << kMotionTokenTopic << "\n";
        rec_.close();
        return false;
    }

    std::cout << "[MotionTokenRecorder] started on domain=" << domain_id
              << " interface=" << network_interface
              << " topic=" << kMotionTokenTopic << "\n";
    running_ = true;
    return true;
}

void MotionTokenRecorder::stop() {
    sub_.stop();         // take-thread joined, reader closed — no more pushes
    rec_.close();
    running_ = false;
}

void MotionTokenRecorder::on_token(const void* message) {
    rec_.push(*static_cast<const kist_msgs::MotionTokenState*>(message));
}

} // namespace kist
