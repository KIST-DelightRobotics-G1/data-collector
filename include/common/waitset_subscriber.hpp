#pragma once
// Waitset DDS subscriber for the row streams (lowstate, hands, commands,
// motion tokens, record commands) — one message type, one callback, no
// listener.
//
// Why not the unitree ChannelSubscriber: it delivers through a ddscxx
// listener, and dropping it while the DDS thread is inside that callback
// trips cyclonedds-cxx 0.10's EntityDelegate::prevent_callbacks() assertion
// ("the thread would wait for itself"; it cannot tell our main thread from
// the callback thread, and the SDK wrapper hides the reader so close() can
// never be called first). Observed as a core dump on session stop, a few
// percent of the time at the ~1 kHz streams. Same cure as the cameras'
// ReliableSubscriber: a waitset + our own take-thread, so teardown is
// "stop the thread, then close the reader" with no callback machinery.
//
// QoS: best-effort + KeepLast(depth) by default, matching the SDK readers
// these replace (a reliable reader would not match the robot's best-effort
// writers). depth only needs to cover the samples that land between two
// wake-ups of the take-thread. reliable=true requests RELIABLE + KeepAll
// for writers known to offer it.
//
// All instances share one DomainParticipant per process (created on first
// use; it reads CYCLONEDDS_URI, which main routes to config/cyclonedds.xml).

#include <dds/dds.hpp>

#include <atomic>
#include <functional>
#include <iostream>
#include <string>
#include <thread>

namespace kist {

inline dds::domain::DomainParticipant& shared_dds_participant(int domain_id) {
    static dds::domain::DomainParticipant dp{static_cast<uint32_t>(domain_id)};
    return dp;
}

template <typename MsgT>
class WaitsetSubscriber {
public:
    using OnMsgFn = std::function<void(const MsgT&)>;

    WaitsetSubscriber() = default;
    ~WaitsetSubscriber() { stop(); }

    bool start(int domain_id, const std::string& topic_name, int depth,
               OnMsgFn on_msg, bool reliable = false) {
        if (thread_.joinable()) return true;
        try {
            auto& dp = shared_dds_participant(domain_id);
            topic_   = dds::topic::Topic<MsgT>(dp, topic_name);
            sub_     = dds::sub::Subscriber(dp);
            auto qos = sub_.default_datareader_qos();
            if (reliable)
                qos << dds::core::policy::Reliability::Reliable(
                           dds::core::Duration::from_millisecs(100))
                    << dds::core::policy::History::KeepAll()
                    << dds::core::policy::ResourceLimits(depth);
            else
                qos << dds::core::policy::History::KeepLast(depth);
            reader_ = dds::sub::DataReader<MsgT>(sub_, topic_, qos);
            on_msg_ = std::move(on_msg);

            cond_ = dds::sub::cond::ReadCondition(reader_, dds::sub::status::DataState::any());
            waitset_.attach_condition(cond_);
        } catch (const std::exception& e) {
            std::cerr << "[WaitsetSubscriber] start failed on " << topic_name
                      << ": " << e.what() << "\n";
            return false;
        }
        running_ = true;
        thread_  = std::thread(&WaitsetSubscriber::run, this);
        return true;
    }

    void stop() {
        if (!thread_.joinable()) return;
        running_ = false;
        thread_.join();       // take-thread gone -> nothing touches the reader
        drain();              // last sweep for samples that landed meanwhile
        waitset_.detach_condition(cond_);
        reader_.close();
        reader_ = dds::core::null;
    }

private:
    void run() {
        while (running_.load(std::memory_order_relaxed)) {
            try {
                waitset_.wait(dds::core::Duration::from_millisecs(100));
            } catch (const dds::core::TimeoutError&) {
                continue;  // periodic running_ check
            }
            drain();
        }
    }

    void drain() {
        auto samples = reader_.take();
        for (const auto& s : samples)
            if (s.info().valid())
                on_msg_(s.data());
    }

    dds::topic::Topic<MsgT>       topic_  = dds::core::null;
    dds::sub::Subscriber          sub_    = dds::core::null;
    dds::sub::DataReader<MsgT>    reader_ = dds::core::null;
    dds::sub::cond::ReadCondition cond_   = dds::core::null;
    dds::core::cond::WaitSet      waitset_;

    std::thread       thread_;
    std::atomic<bool> running_{false};
    OnMsgFn           on_msg_;
};

}  // namespace kist
