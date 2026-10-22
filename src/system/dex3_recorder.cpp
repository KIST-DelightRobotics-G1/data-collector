#include "system/dex3_recorder.hpp"

#include "unitree_recorder/dex3_rows.hpp"


#include <cstdio>
#include <filesystem>
#include <iostream>

namespace kist {

Dex3Recorder::Dex3Recorder() = default;
Dex3Recorder::~Dex3Recorder() { stop(); }

bool Dex3Recorder::start(int domain_id, const std::string& network_interface,
                         const std::string& session_dir, const std::string& side,
                         size_t queue_capacity) {
    if (running_) return true;
    side_ = side;

    const auto csv_path = std::filesystem::path(session_dir) / ("hand_" + side + ".csv");
    if (!rec_.open(csv_path.string(), dex3_header(), &dex3_row, queue_capacity)) {
        std::cerr << "[Dex3Recorder] cannot open " << csv_path << "\n";
        return false;
    }

    // Waitset reader (no listener — see common/waitset_subscriber.hpp); the
    // NIC comes from CYCLONEDDS_URI, so network_interface is informational.
    if (!sub_.start(domain_id, dex3_state_topic(side), /*depth=*/64,
                    [this](const unitree_hg::msg::dds_::HandState_& m) { on_hand_state(&m); })) {
        std::cerr << "[Dex3Recorder] DDS reader failed on " << dex3_state_topic(side) << "\n";
        rec_.close();
        return false;
    }

    std::cout << "[Dex3Recorder] started on domain=" << domain_id
              << " interface=" << network_interface
              << " topic=" << dex3_state_topic(side) << "\n";
    running_ = true;
    return true;
}

void Dex3Recorder::stop() {
    sub_.stop();         // take-thread joined, reader closed — no more pushes
    rec_.close();
    running_ = false;
}

void Dex3Recorder::on_hand_state(const void* message) {
    const auto& s = *static_cast<const unitree_hg::msg::dds_::HandState_*>(message);
    // One-time report of the actual vector sizes, so a mismatch with the
    // fixed CSV schema (kMotors/kPressPads) is visible in the field.
    if (!sizes_logged_) {
        sizes_logged_ = true;
        std::printf("[Dex3Recorder] %s: %zu motor(s), %zu press pad(s) "
                    "(csv schema: %d motors, %d pads)\n",
                    side_.c_str(), s.motor_state().size(),
                    s.press_sensor_state().size(), kDex3Motors, kDex3PressPads);
    }
    rec_.push(s);
}

} // namespace kist
