#pragma once
// DDS intake for rt/kist/record_cmd (kist_msgs::RecordCommand, published by
// kist-gearsonic-inference from the VR controller's A/B taps). The DDS
// callback only queues the action; the runner's main loop drains the queue
// and drives record_control.hpp's state machine. Duplicate/old seq are
// dropped.
#include "system/record_control.hpp"
#include "kist_msgs.hpp"

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace unitree::robot {
template <typename T> class ChannelSubscriber;
}

namespace kist {

inline constexpr const char* kRecordCmdTopic = "rt/kist/record_cmd";

class RecordCommandListener {
public:
    RecordCommandListener();
    ~RecordCommandListener();

    bool start(int domain_id, const std::string& network_interface);
    void stop();

    // All actions received since the last call, in arrival order.
    std::vector<RecordAction> drain();

    // internal: DDS callback
    void on_command(const void* message);

private:
    using Sub = unitree::robot::ChannelSubscriber<kist_msgs::RecordCommand>;
    std::unique_ptr<Sub>      sub_;
    std::mutex                mutex_;
    std::deque<RecordAction>  queue_;
    uint64_t                  last_seq_ = 0;
    bool                      have_seq_ = false;
    bool                      running_  = false;
};

} // namespace kist
