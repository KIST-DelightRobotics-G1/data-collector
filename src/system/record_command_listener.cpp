#include "system/record_command_listener.hpp"

#include <iostream>

namespace kist {

RecordCommandListener::RecordCommandListener() = default;
RecordCommandListener::~RecordCommandListener() { stop(); }

bool RecordCommandListener::start(int domain_id, const std::string& network_interface) {
    if (running_) return true;
    (void)network_interface;
    // Best-effort like the SDK reader this replaces (always matches the
    // publisher); a lost tap is visible to the pilot (no voice cue) and
    // simply repeated.
    if (!sub_.start(domain_id, kRecordCmdTopic, /*depth=*/8,
                    [this](const kist_msgs::RecordCommand& m) { on_command(m); })) {
        std::cerr << "[RecordCommandListener] DDS reader failed on " << kRecordCmdTopic << "\n";
        return false;
    }
    std::cout << "[RecordCommandListener] listening on " << kRecordCmdTopic << "\n";
    running_ = true;
    return true;
}

void RecordCommandListener::stop() {
    sub_.stop();
    running_ = false;
}

std::vector<RecordAction> RecordCommandListener::drain() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<RecordAction> out(queue_.begin(), queue_.end());
    queue_.clear();
    return out;
}

void RecordCommandListener::on_command(const kist_msgs::RecordCommand& m) {
    std::lock_guard<std::mutex> lock(mutex_);
    // A redelivery or a late sample is dropped; a publisher restart (seq
    // back to 1) is accepted.
    if (have_seq_ && m.seq() <= last_seq_ && m.seq() > 1) return;
    have_seq_ = true;
    last_seq_ = m.seq();
    queue_.push_back(static_cast<RecordAction>(m.action()));
}

} // namespace kist
