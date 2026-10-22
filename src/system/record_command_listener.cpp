#include "system/record_command_listener.hpp"

#include <unitree/robot/channel/channel_factory.hpp>
#include <unitree/robot/channel/channel_subscriber.hpp>

#include <iostream>

namespace kist {

RecordCommandListener::RecordCommandListener() = default;
RecordCommandListener::~RecordCommandListener() { stop(); }

bool RecordCommandListener::start(int domain_id, const std::string& network_interface) {
    if (running_) return true;
    try {
        // Safe when another recorder already initialized the factory.
        unitree::robot::ChannelFactory::Instance()->Init(domain_id, network_interface);
        sub_.reset(new Sub(kRecordCmdTopic));
        sub_->InitChannel([this](const void* msg) { on_command(msg); }, 8);
    } catch (const std::exception& e) {
        std::cerr << "[RecordCommandListener] DDS init failed: " << e.what() << "\n";
        return false;
    }
    std::cout << "[RecordCommandListener] listening on " << kRecordCmdTopic << "\n";
    running_ = true;
    return true;
}

void RecordCommandListener::stop() {
    sub_.reset();
    running_ = false;
}

std::vector<RecordAction> RecordCommandListener::drain() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<RecordAction> out(queue_.begin(), queue_.end());
    queue_.clear();
    return out;
}

void RecordCommandListener::on_command(const void* message) {
    const auto& m = *static_cast<const kist_msgs::RecordCommand*>(message);
    std::lock_guard<std::mutex> lock(mutex_);
    // Reliable + KeepLast(8): a redelivery or a late sample is dropped;
    // a publisher restart (seq back to 1) is accepted.
    if (have_seq_ && m.seq() <= last_seq_ && m.seq() > 1) return;
    have_seq_ = true;
    last_seq_ = m.seq();
    queue_.push_back(static_cast<RecordAction>(m.action()));
}

} // namespace kist
