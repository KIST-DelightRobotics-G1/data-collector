#include "system/lowcmd_recorder.hpp"

#include "unitree_recorder/lowcmd_rows.hpp"


#include <filesystem>
#include <iostream>

namespace kist {

LowcmdRecorder::LowcmdRecorder() = default;
LowcmdRecorder::~LowcmdRecorder() { stop(); }

bool LowcmdRecorder::start(int domain_id, const std::string& network_interface,
                           const std::string& session_dir, size_t queue_capacity,
                           const std::string& topic, const std::string& csv_name) {
    if (running_) return true;
    label_ = std::filesystem::path(csv_name).stem().string();

    const auto csv_path = std::filesystem::path(session_dir) / csv_name;
    if (!rec_.open(csv_path.string(), lowcmd_header(), &lowcmd_row, queue_capacity)) {
        std::cerr << "[LowcmdRecorder] cannot open " << csv_path << "\n";
        return false;
    }

    // Waitset reader (no listener — see common/waitset_subscriber.hpp); the
    // NIC comes from CYCLONEDDS_URI, so network_interface is informational.
    if (!sub_.start(domain_id, topic, /*depth=*/256,
                    [this](const unitree_hg::msg::dds_::LowCmd_& m) { on_lowcmd(&m); })) {
        std::cerr << "[LowcmdRecorder] DDS reader failed on " << topic << "\n";
        rec_.close();
        return false;
    }

    std::cout << "[LowcmdRecorder] started on domain=" << domain_id
              << " interface=" << network_interface << " topic=" << topic << "\n";
    running_ = true;
    return true;
}

void LowcmdRecorder::stop() {
    sub_.stop();         // take-thread joined, reader closed — no more pushes
    rec_.close();
    running_ = false;
}

void LowcmdRecorder::on_lowcmd(const void* message) {
    rec_.push(*static_cast<const unitree_hg::msg::dds_::LowCmd_*>(message));
}

} // namespace kist
