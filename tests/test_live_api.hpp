#pragma once
#include "GkLiveApi.hpp"

namespace LiveApiHarness
{
    using namespace StageRuntimeHarness;
    inline MetadataClass scene{"LiveScenePresenter"}, presenter{"LivePresenter"},
        player{"LivePlayerPresenter"}, execution{"LiveExecutionData"};
    inline void* Find(const char* image, const char* space, const char* name)
    {
        if (std::strcmp(image, "Assembly-CSharp.dll")) return nullptr;
        if (!std::strcmp(space, "Campus.Live"))
        {
            if (!std::strcmp(name, scene.name)) return &scene;
            if (!std::strcmp(name, presenter.name)) return &presenter;
            if (!std::strcmp(name, player.name)) return &player;
        }
        if (!std::strcmp(space, "Campus.Live.Data") && !std::strcmp(name, execution.name)) return &execution;
        return nullptr;
    }
    inline void Test()
    {
        GakuMotion::ExactMethodMetadata metadata{Methods, MethodName, MethodCount, MethodFlags,
            MethodGeneric, MethodInflated, MethodClass, MethodParameter, MethodResult,
            TypeCode, TypeByRef, TypeClass};
        MetadataMethod pause{}, resume{}, getExecution{}, paused{}, time{}, duration{}, seek{};
        auto init = [](MetadataMethod& m, const char* name, void* owner, MetadataType ret, unsigned count) {
            m.name = name; m.owner = owner; m.returns = ret; m.count = count; m.flags = 0x86;
        };
        init(pause, "Pause", &presenter, {0x01}, 0);
        init(resume, "UnPause", &presenter, {0x01}, 0);
        init(getExecution, "get_ExecutionData", &player, {0x12, &execution}, 0);
        init(paused, "get_IsPause", &execution, {0x02}, 0);
        init(time, "get_TimelineTime", &player, {0x0c}, 0);
        init(duration, "get_Duration", &player, {0x0c}, 0);
        init(seek, "Seek", &presenter, {0x01}, 1); seek.parameters[0] = {0x0c};
        methods = {&pause, &resume, &getExecution, &paused, &time, &duration, &seek};
        auto api = GakuMotion::live_takeover::Api::Resolve(Find, metadata);
        Check(api.Ready() && api.seek == &seek && api.isPaused == &paused,
            "Live API requires exact instance signatures for pause, progress and state");
        seek.parameters[0] = {0x0d};
        Check(!GakuMotion::live_takeover::Api::Resolve(Find, metadata).Ready(),
            "Live API rejects changed seek argument type");
        seek.parameters[0] = {0x0c}; pause.flags |= 0x10;
        Check(!GakuMotion::live_takeover::Api::Resolve(Find, metadata).Ready(),
            "Live API rejects a static pause method");
        pause.flags &= ~0x10;
        MetadataMethod duplicate = pause;
        methods.push_back(&duplicate);
        Check(!GakuMotion::live_takeover::Api::Resolve(Find, metadata).Ready(),
            "Live API rejects ambiguous metadata");
        methods.clear();
    }
}
