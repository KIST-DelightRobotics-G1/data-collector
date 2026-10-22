#include "system/lowstate_recorder.hpp"

#include "unitree_recorder/lowstate_rows.hpp"


#include <filesystem>
#include <iostream>

namespace kist {

LowstateRecorder::LowstateRecorder() = default;
LowstateRecorder::~LowstateRecorder() { stop(); }

bool LowstateRecorder::start(int domain_id, const std::string& network_interface,
                            const std::string& session_dir, size_t queue_capacity,
                            const std::string& topic) {
    if (running_) return true;

    const auto csv_path = std::filesystem::path(session_dir) / "lowstate.csv";
    if (!rec_.open(csv_path.string(), lowstate_header(), &lowstate_row, queue_capacity)) {
        std::cerr << "[LowstateRecorder] cannot open " << csv_path << "\n";
        return false;
    }

    // Waitset reader (no listener — see common/waitset_subscriber.hpp); the
    // NIC comes from CYCLONEDDS_URI, so network_interface is informational.
    if (!sub_.start(domain_id, topic, /*depth=*/256,
                    [this](const unitree_hg::msg::dds_::LowState_& m) { on_lowstate(&m); })) {
        std::cerr << "[LowstateRecorder] DDS reader failed on " << topic << "\n";
        rec_.close();
        return false;
    }

    std::cout << "[LowstateRecorder] started on domain=" << domain_id
              << " interface=" << network_interface << " topic=" << topic << "\n";
    running_ = true;
    return true;
}

void LowstateRecorder::stop() {
    sub_.stop();         // take-thread joined, reader closed — no more pushes
    rec_.close();
    running_ = false;
}

void LowstateRecorder::on_lowstate(const void* message) {
    rec_.push(*static_cast<const unitree_hg::msg::dds_::LowState_*>(message));
}

} // namespace kist
