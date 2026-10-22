// Injection test for the remote-recording episode state machine
// (system/record_control.hpp) — pure, no DDS.
//
// Run: ./build/test_record_fsm   (exit 0 = all transitions hold)
#include "system/record_control.hpp"
#include <cstdio>

using kist::RecordAction; using kist::RecordState; using kist::RecordEffect;
static int failures = 0;
static void check(const char* what, bool ok) {
    std::printf("%s %s\n", ok ? "[PASS]" : "[FAIL]", what);
    if (!ok) ++failures;
}
static bool is(kist::RecordStep s, RecordState next, RecordEffect eff) { return s.next == next && s.effect == eff; }

int main() {
    const auto I = RecordState::Idle, R = RecordState::Recording, L = RecordState::AwaitingLabel;
    check("idle + start -> recording / StartSession",       is(kist::record_fsm(I, RecordAction::Start), R, RecordEffect::StartSession));
    check("idle + stop -> ignored",                          is(kist::record_fsm(I, RecordAction::Stop), I, RecordEffect::None));
    check("idle + label -> ignored",                         is(kist::record_fsm(I, RecordAction::LabelSuccess), I, RecordEffect::None));
    check("recording + stop -> awaiting / StopSession",      is(kist::record_fsm(R, RecordAction::Stop), L, RecordEffect::StopSession));
    check("recording + start -> ignored",                    is(kist::record_fsm(R, RecordAction::Start), R, RecordEffect::None));
    check("recording + label -> ignored",                    is(kist::record_fsm(R, RecordAction::LabelFail), R, RecordEffect::None));
    check("awaiting + success -> idle / WriteSuccess",       is(kist::record_fsm(L, RecordAction::LabelSuccess), I, RecordEffect::WriteSuccess));
    check("awaiting + fail -> idle / WriteFail",             is(kist::record_fsm(L, RecordAction::LabelFail), I, RecordEffect::WriteFail));
    check("awaiting + partial -> idle / WritePartial",       is(kist::record_fsm(L, RecordAction::LabelPartial), I, RecordEffect::WritePartial));
    check("idle + partial -> ignored",                       is(kist::record_fsm(I, RecordAction::LabelPartial), I, RecordEffect::None));
    check("awaiting + start -> recording, noted unlabeled",  is(kist::record_fsm(L, RecordAction::Start), R, RecordEffect::StartSession)
                                                             && kist::record_fsm(L, RecordAction::Start).note[0] != 0);
    check("awaiting + stop -> ignored",                      is(kist::record_fsm(L, RecordAction::Stop), L, RecordEffect::None));
    check("unknown action -> ignored",                       is(kist::record_fsm(R, static_cast<RecordAction>(9)), R, RecordEffect::None));
    // full cycle
    RecordState s = I;
    s = kist::record_fsm(s, RecordAction::Start).next;
    s = kist::record_fsm(s, RecordAction::Stop).next;
    s = kist::record_fsm(s, RecordAction::LabelSuccess).next;
    check("cycle start/stop/success returns to idle", s == I);
    std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "OK", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
