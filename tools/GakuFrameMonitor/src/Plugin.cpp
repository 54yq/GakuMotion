#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <GakuPlugin.h>
#include <GakuFrameContext.h>
#include <imgui.h>
#include "FrameDetector.hpp"
#include <chrono>
#include <cstdio>
#include <mutex>

namespace GakuFrameMonitor {
using Clock=std::chrono::steady_clock;
struct Snapshot { float fps{}, frameMs{}, baselineMs{}, lastStallMs{}; uint64_t stalls{}; bool live{}, available{}; GakuFrameContextV1 context{}; };
struct State { GakuLogService log{}; GakuDesktopUiService desktop{}; std::mutex mutex; Snapshot display{};
    Clock::time_point last{}, window{}, lastLog{}; unsigned frames{}; FrameDetector detector; GakuMotionGetFrameContextFn query{}; };
static State state;
template <class T> bool Query(const GakuHostApi* host,const char* name,T& result) {
    T value{}; if(host->queryService(host->hostContext,name,1,sizeof(value),&value)!=GAKU_OK || value.abiVersion!=1 || value.structSize<sizeof(value))return false;
    result=value;return true;
}
static GakuStatus Activate(const GakuHostApi* host, void** output) noexcept {
    if(!host||!output||host->abiVersion!=1||host->structSize<sizeof(*host)||!host->queryService)return GAKU_E_INVALID_ARGUMENT;
    Query(host,GAKU_SERVICE_LOG,state.log); Query(host,GAKU_SERVICE_DESKTOP_UI,state.desktop); *output=&state; return GAKU_OK;
}
static void Tick(void*,float) noexcept {
    try {
        auto& s=state; const auto now=Clock::now();
        if(!s.last.time_since_epoch().count()){s.last=now;s.window=now;return;}
        const double ms=std::chrono::duration<double,std::milli>(now-s.last).count();s.last=now;
        if(ms>10000){s.detector.sample(ms);s.frames=0;s.window=now;return;}
        const bool anomaly=s.detector.sample(ms); ++s.frames;
        const double seconds=std::chrono::duration<double>(now-s.window).count();
        if(!s.query){const auto m=GetModuleHandleW(L"GakuMotion.dll");if(m)s.query=reinterpret_cast<GakuMotionGetFrameContextFn>(GetProcAddress(m,"GakuMotion_GetFrameContext"));}
        const bool publish=seconds>=1.0 || anomaly;
        if(!publish)return;
        GakuFrameContextV1 context{};const bool available=s.query&&s.query(&context,sizeof(context))==1&&context.version==1&&context.size==sizeof(context);
        Snapshot snapshot; {std::lock_guard<std::mutex> lock(s.mutex);snapshot=s.display;}
        snapshot.available=available;snapshot.live=available&&context.liveConfirmed;snapshot.context=context;
        snapshot.frameMs=static_cast<float>(ms);snapshot.baselineMs=static_cast<float>(s.detector.baselineMs);
        if(seconds>=1.0){snapshot.fps=static_cast<float>(s.frames/seconds);s.frames=0;s.window=now;}
        if(anomaly && snapshot.live) {
            ++snapshot.stalls;snapshot.lastStallMs=static_cast<float>(ms);
            if(s.log.write && (!s.lastLog.time_since_epoch().count() || now-s.lastLog>=std::chrono::seconds(2))) {
                SYSTEMTIME time{};GetLocalTime(&time);char line[1024]{};
                std::snprintf(line,sizeof(line),"[GakuFrameMonitor][Stall] time=%04u-%02u-%02uT%02u:%02u:%02u.%03u frame_ms=%.2f baseline_ms=%.2f fps_1s=%.1f live_time=%.3f motion_time=%.3f live_paused=%u takeover=%u followers=%u physics_requested=%u physics_actual=%u physics_dispatch_ms=%.3f physics_native_avg_ms=%.3f physics_solver_avg_ms=%.3f failures=%llu expired=%llu identity_skips=%llu",
                    time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,time.wMilliseconds,ms,s.detector.baselineMs,snapshot.fps,context.liveTime,context.motionTime,context.livePaused,context.takeover,context.boundFollowers,context.physicsRequested,context.physicsActual,context.physicsDispatchNs/1e6,context.physicsNativeNs/1e6,context.physicsSolverNs/1e6,static_cast<unsigned long long>(context.physicsFailures),static_cast<unsigned long long>(context.physicsExpired),static_cast<unsigned long long>(context.physicsIdentitySkips));
                s.log.write(s.log.context,GAKU_LOG_WARNING,line);s.lastLog=now;
            }
        }
        {std::lock_guard<std::mutex> lock(s.mutex);s.display=snapshot;}
    } catch(...) {}
}
static void Desktop(void*) noexcept {
    try {
        if(!state.desktop.bind)return;
        const GakuImGuiAbi abi{IMGUI_VERSION_NUM,sizeof(ImGuiIO),sizeof(ImGuiStyle),sizeof(ImDrawVert),sizeof(ImDrawIdx),sizeof(ImWchar)};
        GakuDesktopUiBinding binding{};if(state.desktop.bind(&abi,sizeof(binding),&binding)!=GAKU_OK||!binding.imguiContext||!binding.alloc||!binding.free)return;
        ImGui::SetAllocatorFunctions(binding.alloc,binding.free,binding.allocatorUserData);
        auto* previous=ImGui::GetCurrentContext();ImGui::SetCurrentContext(static_cast<ImGuiContext*>(binding.imguiContext));
        Snapshot s;{std::lock_guard<std::mutex> lock(state.mutex);s=state.display;}
        ImGui::TextUnformatted("Live 帧数监测");
        if(!s.available)ImGui::TextWrapped("等待兼容的 GakuMotion 运行状态。");
        else if(!s.live)ImGui::TextWrapped("等待 Live 场景；监测数据不会写入卡顿日志。");
        else {ImGui::Text("Unity 更新频率：%.1f FPS",s.fps);ImGui::Text("最近帧间隔：%.1f ms",s.frameMs);ImGui::Text("本次运行卡顿次数：%llu",static_cast<unsigned long long>(s.stalls));ImGui::Text("最近卡顿：%.1f ms",s.lastStallMs);ImGui::Text("绑定的副角色：%u",s.context.boundFollowers);}
        ImGui::TextWrapped("帧间隔达到 50 ms 且超过近期基准两倍时记录线索。记录包含主角色最近的物理指标；指标本身不能证明卡顿原因。");
        ImGui::SetCurrentContext(previous);
    } catch(...) {}
}
}
extern "C" GAKU_PLUGIN_EXPORT GakuStatus GakuPlugin_Query(uint32_t version,uint32_t size,GakuPluginDescriptor* output) {
    if(version!=GAKU_ABI_VERSION)return GAKU_E_VERSION;if(!output)return GAKU_E_INVALID_ARGUMENT;if(size<sizeof(*output))return GAKU_E_SIZE;
    *output={1,sizeof(*output),"gakuframemonitor","Live 帧数监测","0.1.0",GakuFrameMonitor::Activate,nullptr,GakuFrameMonitor::Tick,nullptr,GakuFrameMonitor::Desktop,nullptr};return GAKU_OK;
}
