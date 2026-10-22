#pragma once

// Body-command recording assembly — the action twin of LowstateRecorder:
// subscribes a LowCmd_ topic and rows every message into a CSV via
// RowRecorder:
//
//   [DDS rt/lowcmd | rt/arm_sdk] --take-thread--> [RowRecorder] -> <csv_name>
//
// rt/lowcmd (low-level control) and rt/arm_sdk (arm control while the
// loco controller holds the legs) carry the SAME unitree_hg LowCmd_ type,
// so one class records either — one instance per topic. A command topic
// is silent while no controller publishes: time gaps = no commands, not
// loss (columns: unitree_recorder/lowcmd_rows.hpp).

#include "common/row_recorder.hpp"
#include "common/waitset_subscriber.hpp"

#include <unitree/idl/hg/LowCmd_.hpp>

#include <memory>
#include <string>


namespace kist {

inline constexpr const char* kLowCmdTopic = "rt/lowcmd";
inline constexpr const char* kArmSdkTopic = "rt/arm_sdk";

class LowcmdRecorder {
public:
    LowcmdRecorder();
    ~LowcmdRecorder();

    // Opens <session_dir>/<csv_name> and starts the subscriber + writer.
    // Defaults record rt/lowcmd; pass (kArmSdkTopic, "arm_sdk.csv") for the
    // arm-sdk instance.
    bool start(int domain_id, const std::string& network_interface,
               const std::string& session_dir, size_t queue_capacity,
               const std::string& topic = kLowCmdTopic,
               const std::string& csv_name = "lowcmd.csv");

    // Stops the subscriber first (no more records), then drains and closes
    // the writer — every record received before stop() is on disk after.
    void stop();

    // CSV stem ("lowcmd" / "arm_sdk") — the report/summary label.
    const std::string& label() const { return label_; }
    StreamStats stats() const { return rec_.stats(); }

    // internal: per-sample handler (take-thread)
    void on_lowcmd(const void* message);

private:
    WaitsetSubscriber<unitree_hg::msg::dds_::LowCmd_> sub_;

    RowRecorder<unitree_hg::msg::dds_::LowCmd_> rec_;
    std::string label_;
    bool        running_ = false;
};

} // namespace kist
