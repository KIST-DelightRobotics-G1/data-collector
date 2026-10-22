#pragma once
// DDS intake for rt/kist/record_cmd (kist_msgs::RecordCommand, published by
// kist-gearsonic-inference from the VR controller's A/B taps). The take-
// thread only queues the action; the runner's main loop drains the queue
// and drives record_control.hpp's state machine. Duplicate/old seq are
// dropped.
#include "common/waitset_subscriber.hpp"
#include "system/record_control.hpp"
#include "kist_msgs.hpp"

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace kist {

inline constexpr const char* kRecordCmdTopic = "rt/kist/record_cmd";

class RecordCommandListener {
public:
    RecordCommandListener();
    ~RecordCommandListener();

    // network_interface is informational: the NIC comes from CYCLONEDDS_URI.
    bool start(int domain_id, const std::string& network_interface);
    void stop();

    // All actions received since the last call, in arrival order.
    std::vector<RecordAction> drain();

    // internal: per-sample handler (take-thread)
    void on_command(const kist_msgs::RecordCommand& m);

private:
    WaitsetSubscriber<kist_msgs::RecordCommand> sub_;
    std::mutex                mutex_;
    std::deque<RecordAction>  queue_;
    uint64_t                  last_seq_ = 0;
    bool                      have_seq_ = false;
    bool                      running_  = false;
};

} // namespace kist
