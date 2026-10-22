#pragma once

// Motion-token recording assembly — gearsonic's decoder-input token stream:
//
//   [DDS rt/kist/motion_token] --take-thread--> [RowRecorder] -> motion_token.csv
//
// The stream is silent while the robot is outside CONTROL (INIT ramp,
// damping, e-stop): time/seq gaps = not controlled, not loss. Owns its
// subscriber and its RowRecorder (own queue + writer thread), like the
// other row streams (columns: gearsonic_recorder/motion_token_rows.hpp).

#include "common/row_recorder.hpp"
#include "common/waitset_subscriber.hpp"

#include "kist_msgs.hpp"

#include <memory>
#include <string>


namespace kist {

// Published by kist-gearsonic-inference (its collector/MotionTokenPublisher).
inline constexpr const char* kMotionTokenTopic = "rt/kist/motion_token";

class MotionTokenRecorder {
public:
    MotionTokenRecorder();
    ~MotionTokenRecorder();

    // Opens <session_dir>/motion_token.csv and starts the subscriber + writer.
    bool start(int domain_id, const std::string& network_interface,
               const std::string& session_dir, size_t queue_capacity);

    void stop();

    StreamStats stats() const { return rec_.stats(); }

    // internal: per-sample handler (take-thread)
    void on_token(const void* message);

private:
    WaitsetSubscriber<kist_msgs::MotionTokenState> sub_;

    RowRecorder<kist_msgs::MotionTokenState> rec_;
    bool running_ = false;
};

} // namespace kist
