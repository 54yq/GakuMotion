#pragma once
#ifdef GKMOTION_GUI_ACTIVE
namespace GakuMotion {
inline void DrawSwingCheckpointPanel(const PhysicsMonitor& monitor, bool current) {
    if(!ImGui::CollapsingHeader("Swing 检查点")) return;
    ImGui::TextUnformatted("实时运行（与下方 64 条固定报告分开）");
    const double uiNow=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if(monitor.liveTelemetryActive)ImGui::Text("监控刷新距今 %.2f 秒%s",(std::max)(0.0,uiNow-monitor.liveTelemetryTime),uiNow-monitor.liveTelemetryTime>2?"（数据过期）":"");
    ImGui::Text("状态：%s",!monitor.liveTelemetryActive?"未确认 Live":monitor.liveTelemetryClockTrial?"角色图时钟实验":monitor.liveTelemetryPhase==2?"接管":monitor.liveTelemetryPhase==1?"Live 暂停":"Live 播放");
    const auto drawCounter=[&](const char* name,const RealtimeCounter& counter){
        if(!monitor.liveTelemetryActive||!counter.known){ImGui::Text("%s：不可用（不等于 0）",name);return;}
        ImGui::Text("%s：%.1f 次/秒；本次监控累计 %llu",name,counter.rate,static_cast<unsigned long long>(counter.total));
        const double age=counter.Age(uiNow);
        if(age<0)ImGui::TextUnformatted("尚未观察到新调用增量");
        else ImGui::Text("距最近观察到增量 %.2f 秒%s",age,age>2?"（超过 2 秒无增量）":"");
    };
    drawCounter("播放器 Evaluate（手动入口）",monitor.liveEvalMeter);
    drawCounter("Swing 入口（进程范围）",monitor.liveSwingMeter);
    ImGui::TextWrapped("Swing 包含其它角色；不能据此认定当前角色匹配或 MMD 注入成功。Unity 自动推进图时，手动 Evaluate 入口也可能不增长。");
    ImGui::Separator();
    ImGui::Text("运行时代码：%u 个文件；读取/写入失败：%u",monitor.codeCaptureFiles,monitor.codeCaptureFailures);
    ImGui::TextWrapped("代码采集在首次识别角色图时自动执行；输出位于诊断目录 live_code_<进程号>。失败不会改变页面保护或游戏状态。");
    const auto& report=monitor.checkpointReport;
    ImGui::TextWrapped("%s",current?"当前场景的固定采样报告（最多 64 条；不是实时累计）":"历史固定报告；不用于当前角色写入");
    if(!report.source.empty()) ImGui::TextWrapped("来源：%s",report.source.c_str());
    ImGui::Text("最近监控角色：%p；场景持有者：%p",reinterpret_cast<void*>(monitor.liveActorToken),reinterpret_cast<void*>(monitor.graphOwner));
    ImGui::TextWrapped("图有效：%s；首个输出匹配：%s；整图独占：未确认。",monitor.graphValid==1?"是":"未确认",monitor.graphActorTarget==1?"是":"未确认");
    ImGui::Text("已返回的推进样本：%u / %u；任务完成屏障：未确认。",monitor.scheduleTimingCompleted,monitor.scheduleTimingSamples);
    if(!report.parsed) ImGui::TextWrapped("尚未载入可读的列表报告。");
    if(ImGui::BeginTable("##swing_checkpoint_regions",4,ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg|ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("区域"); ImGui::TableSetupColumn("布局 / 样本"); ImGui::TableSetupColumn("数量 / 容量"); ImGui::TableSetupColumn("字节"); ImGui::TableHeadersRow();
        const char* names[]{"动态骨骼","静态碰撞","胸部","链条"};
        for(unsigned i=0;i<4;++i) {
            const auto& r=report.lists[i]; ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted(names[i]);
            ImGui::TableNextColumn(); ImGui::Text("%s / %llu",!r.seen?"缺失":!r.layout?"未验证":r.stable?"稳定":"需检查",static_cast<unsigned long long>(r.samples));
            ImGui::TableNextColumn(); ImGui::Text("%llu / %llu",static_cast<unsigned long long>(r.count),static_cast<unsigned long long>(r.capacity));
            ImGui::TableNextColumn(); ImGui::Text("%llu",static_cast<unsigned long long>(r.count*r.stride));
        }
        ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted("物理时钟");
        ImGui::TableNextColumn(); ImGui::TextUnformatted(report.clockLayout?"布局已知":"布局未确认");
        ImGui::TableNextColumn(); ImGui::TextUnformatted("实例未捕获"); ImGui::TableNextColumn(); ImGui::TextUnformatted("4"); ImGui::EndTable();
    }
    if(ImGui::TreeNode("缓冲区地址与变化")) {
        for(unsigned i=0;i<4;++i) {
            const auto& r=report.lists[i]; ImGui::TextWrapped("%u：容器 %s，数据 %s，变化 %llu，失败 %llu",i+1,r.container.c_str(),r.payload.c_str(),static_cast<unsigned long long>(r.changes),static_cast<unsigned long long>(r.failures));
        }
        ImGui::TreePop();
    }
    ImGui::TextUnformatted("旧版物理快照方案缺少：整张图的独占归属、任务完成屏障、完整状态覆盖。");
    ImGui::TextUnformatted("快照捕获：未接入；字节恢复：未接入。");
    ImGui::TextWrapped("完整物理快照方案未接入；角色图实验和 MMD 接管使用另一条 Unity 时钟路径。");
}
}
#endif
