#pragma once

#include "../include/GakuMotionControl.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>

namespace GakuMotion::control::v0
{
    inline const char* Message(Error error) noexcept
    {
        switch (error)
        {
        case Error::Ok: return "Command completed";
        case Error::Pending: return "Waiting for Unity main-thread command drain";
        case Error::Unavailable: return "Control provider is unavailable";
        case Error::InvalidArgument: return "Invalid control argument";
        case Error::VersionMismatch: return "Unsupported Control API version";
        case Error::SizeMismatch: return "Control API structure size mismatch";
        case Error::Unsupported: return "Control capability is not supported";
        case Error::NotFound: return "Control item or ticket was not found";
        case Error::NotReady: return "Playback or scene is not ready";
        case Error::Protected: return "Screenshot, sharing, or scene protection blocks control";
        case Error::StaleSession: return "Session changed; refresh state before submitting";
        case Error::StaleMotion: return "Motion or library changed; refresh state before submitting";
        case Error::Busy: return "Control queue or operation is busy";
        case Error::Failed: return "Control operation failed";
        }
        return "Unknown control error";
    }

    inline Error ValidateCommand(const Command* command) noexcept
    {
        if (!command) return Error::InvalidArgument;
        if (command->apiVersion != Version) return Error::VersionMismatch;
        if (command->structSize != sizeof(Command)) return Error::SizeMismatch;
        if (!Mask(command->kind)) return Error::Unsupported;
        switch (command->kind)
        {
        case CommandKind::Load:
            if (!command->motionKey[0] || !std::memchr(command->motionKey, '\0', sizeof(command->motionKey)))
                return Error::InvalidArgument;
            break;
        case CommandKind::Seek:
            if (!std::isfinite(command->value) || command->value < 0.0f) return Error::InvalidArgument;
            break;
        case CommandKind::Speed:
            if (!std::isfinite(command->value) || command->value < 0.1f || command->value > 5.0f)
                return Error::InvalidArgument;
            break;
        case CommandKind::Offset:
            if (!std::isfinite(command->offset.x) || !std::isfinite(command->offset.y) ||
                !std::isfinite(command->offset.z)) return Error::InvalidArgument;
            break;
        case CommandKind::Camera:
            if (command->camera.enabled > 1 || command->camera.mode > 2 ||
                command->camera.followBehavior > 1 || command->camera.followTarget > 2)
                return Error::InvalidArgument;
            for (float rate : command->camera.rateXYZ)
                if (!std::isfinite(rate) || rate < 0.0f || rate > 1.0f) return Error::InvalidArgument;
            break;
        case CommandKind::Scene:
            if (command->scene > static_cast<std::uint32_t>(ScenePreset::Gray)) return Error::InvalidArgument;
            break;
        default: break;
        }
        return Error::Ok;
    }

    // Only plain completion records live here. Core owns the command queue and
    // session gates. This class never calls a consumer or Unity while locked.
    class Requests
    {
        mutable std::mutex mutex_;
        std::map<std::uint64_t, Result> results_;
        std::uint64_t nextTicket_{1};

        static void SetResult(Result& result, Error error, const char* message) noexcept
        {
            result.status = error;
            std::snprintf(result.message, sizeof(result.message), "%s", message ? message : Message(error));
        }

    public:
        static constexpr std::uint32_t Capacity = MaxOutstanding;

        Error Allocate(std::uint64_t& ticket)
        {
            ticket = 0;
            std::lock_guard<std::mutex> lock(mutex_);
            if (results_.size() >= Capacity) return Error::Busy;
            // Exhaustion is terminal rather than wrapping into a stale ticket.
            if (!nextTicket_) return Error::Unavailable;
            Result result;
            result.ticket = nextTicket_;
            SetResult(result, Error::Pending, nullptr);
            try { results_.emplace(nextTicket_, result); }
            catch (...) { return Error::Failed; }
            ticket = nextTicket_++;
            return Error::Ok;
        }

        void Complete(std::uint64_t ticket, Error error, const char* message = nullptr)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto found = results_.find(ticket);
            if (found != results_.end() && found->second.status == Error::Pending)
                SetResult(found->second, error, message);
        }

        Error Poll(std::uint64_t ticket, std::uint32_t outputSize, Result* output) const
        {
            if (!output) return Error::InvalidArgument;
            if (outputSize != sizeof(Result)) return Error::SizeMismatch;
            std::lock_guard<std::mutex> lock(mutex_);
            const auto found = results_.find(ticket);
            if (found == results_.end()) return Error::NotFound;
            *output = found->second;
            return Error::Ok;
        }

        Error Release(std::uint64_t ticket)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto found = results_.find(ticket);
            if (found == results_.end()) return Error::NotFound;
            if (found->second.status == Error::Pending) return Error::Busy;
            results_.erase(found);
            return Error::Ok;
        }

        void CancelPending(Error reason = Error::Unavailable)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (reason == Error::Pending) reason = Error::Unavailable;
            for (auto& item : results_)
                if (item.second.status == Error::Pending) SetResult(item.second, reason, nullptr);
        }
    };
}
