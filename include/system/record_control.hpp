#pragma once
// Remote recording control — the episode state machine the runner drives
// from VR-button commands (kist-gearsonic-inference publishes
// kist_msgs::RecordCommand on rt/kist/record_cmd; one-way, this side's
// console is where the state is read).
//
//   idle            --start--> recording
//   recording       --stop---> awaiting_label
//   awaiting_label  --label--> idle            (success / fail / partial -> meta.yaml)
//   awaiting_label  --start--> recording       (previous episode left unlabeled)
// Everything else is ignored and logged. Pure: no I/O, probe-testable.
#include <cstdint>

namespace kist {

// Wire values: kist_msgs::RecordCommand.action.
enum class RecordAction : uint8_t { None = 0, Start = 1, Stop = 2, LabelSuccess = 3, LabelFail = 4, LabelPartial = 5 };
enum class RecordState  : uint8_t { Idle = 0, Recording = 1, AwaitingLabel = 2 };

// What the runner must do for a transition.
enum class RecordEffect : uint8_t { None, StartSession, StopSession, WriteSuccess, WriteFail, WritePartial };

struct RecordStep {
    RecordState  next;
    RecordEffect effect;
    const char*  note;   // "" when the command was applied as-is
};

inline RecordStep record_fsm(RecordState state, RecordAction action) {
    switch (action) {
        case RecordAction::Start:
            if (state == RecordState::Recording)
                return {state, RecordEffect::None, "already recording"};
            return {RecordState::Recording, RecordEffect::StartSession,
                    state == RecordState::AwaitingLabel ? "previous episode left unlabeled" : ""};
        case RecordAction::Stop:
            if (state != RecordState::Recording)
                return {state, RecordEffect::None, "not recording"};
            return {RecordState::AwaitingLabel, RecordEffect::StopSession, ""};
        case RecordAction::LabelSuccess:
        case RecordAction::LabelFail:
        case RecordAction::LabelPartial:
            if (state != RecordState::AwaitingLabel)
                return {state, RecordEffect::None, "no stopped episode to label"};
            return {RecordState::Idle,
                    action == RecordAction::LabelSuccess ? RecordEffect::WriteSuccess
                  : action == RecordAction::LabelFail    ? RecordEffect::WriteFail
                                                         : RecordEffect::WritePartial, ""};
        default:
            return {state, RecordEffect::None, "unknown action"};
    }
}

inline const char* record_action_name(RecordAction a) {
    switch (a) {
        case RecordAction::Start:        return "start";
        case RecordAction::Stop:         return "stop";
        case RecordAction::LabelSuccess: return "label_success";
        case RecordAction::LabelFail:    return "label_fail";
        case RecordAction::LabelPartial: return "label_partial";
        default:                         return "unknown";
    }
}
inline const char* record_state_name(RecordState s) {
    switch (s) {
        case RecordState::Idle:          return "idle";
        case RecordState::Recording:     return "recording";
        case RecordState::AwaitingLabel: return "awaiting_label";
    }
    return "?";
}

} // namespace kist
