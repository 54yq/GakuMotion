#pragma once
#include <cstdint>
#include <string_view>

namespace GakuMotion::capture
{
    // Confirmed means native ownership/readback was verified. It is not a
    // certification of rendered pixels, which requires the game acceptance.
    enum class RestoreState : uint8_t { NotModified, Confirmed, Unconfirmed, InjectedFailure };
    inline const char* StateName(RestoreState value)
    {
        switch (value) {
        case RestoreState::NotModified: return "not_modified";
        case RestoreState::Confirmed: return "confirmed";
        case RestoreState::InjectedFailure: return "injected_failure";
        default: return "unconfirmed";
        }
    }
    inline bool Clean(RestoreState value)
    { return value == RestoreState::NotModified || value == RestoreState::Confirmed; }
    struct RestoreReport
    {
        RestoreState body{}, face{}, gaze{}, camera{}, stage{}, physics{}, nativeAudio{};
        bool AllConfirmed() const
        { return Clean(body) && Clean(face) && Clean(gaze) && Clean(camera) && Clean(stage) && Clean(physics) && Clean(nativeAudio); }
        const char* FailureReason() const
        {
            if (!Clean(body)) return "body_restore_unconfirmed";
            if (!Clean(face)) return "face_restore_unconfirmed";
            if (!Clean(gaze)) return "gaze_restore_unconfirmed";
            if (!Clean(camera)) return "camera_restore_unconfirmed";
            if (!Clean(stage)) return "stage_restore_or_pending_unconfirmed";
            if (!Clean(physics)) return "physics_restore_unconfirmed";
            if (!Clean(nativeAudio)) return "native_audio_restore_unconfirmed";
            return "clean_native_gate_passed";
        }
    };
#if defined(GKMOTION_CAPTURE_DIAGNOSTICS)
    enum Failure : uint32_t { FailBody = 1, FailCamera = 2, FailStage = 4 };
    inline uint32_t ParseFailureMask(std::string_view value)
    {
        uint32_t mask = 0;
        while (!value.empty()) {
            const auto pos = value.find('|');
            const auto token = value.substr(0, pos);
            if (token == "body") mask |= FailBody;
            else if (token == "camera") mask |= FailCamera;
            else if (token == "stage") mask |= FailStage;
            if (pos == std::string_view::npos) break;
            value.remove_prefix(pos + 1);
        }
        return mask;
    }
    inline void ApplyFailures(RestoreReport& report, uint32_t mask)
    {
        if (mask & FailBody) report.body = RestoreState::InjectedFailure;
        if (mask & FailCamera) report.camera = RestoreState::InjectedFailure;
        if (mask & FailStage) report.stage = RestoreState::InjectedFailure;
    }
#endif
}
