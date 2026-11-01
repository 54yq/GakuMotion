# GakuMotion 源码冻结候选

本目录是独立的本地源码审核仓，原始 GakuMotion 快照为
`2b28931e8b50b780668baec5e3982927667c3e08`。
作者程序代码、接口映射、必要的第三方源码及许可证按显式清单冻结；没有复制研发整仓、
真实动作、模型、权重、捕获记录、游戏文件或原仓库的协作历史。

**当前状态为 `blocked-review-partial-source`。完整 Runtime 与 NativeAOT 源码构建尚不可用。**
11 个必需文件含实机几何、模型导出结构或训练／校准数据，尚缺分发依据，因此没有纳入。
构建入口会逐项报告缺项并停止，不会生成空资源或替代实现。详见
[来源审核](SOURCE_REVIEW.md) 与 `freeze-manifest.json`。

## 本地验证

需要 Windows x64、带 C++ 桌面开发工具的 Visual Studio/MSVC 和 Windows SDK。
NativeAOT 完整构建还需要 .NET 8 SDK、NativeAOT 工具链及审核通过的全部必需输入。
脚本没有部署步骤；产物默认写到本目录旁的 `GakuMotion-build/`，也可显式指定输出位置。

```powershell
# 校验冻结文件并列出完整构建的待审输入；当前预期退出码为 2。
pwsh -NoProfile -File build.ps1 -Target Preflight

# 仅构建并运行合成的舞台加载边界契约测试，不启动游戏。
pwsh -NoProfile -File build.ps1 -Target Contracts

# 当前会在同一缺项预检处停止。
pwsh -NoProfile -File build.ps1 -Target Runtime
pwsh -NoProfile -File build.ps1 -Target NativeAOT
```

本次审核使用的外部输出位置为工作区的 `build/staging_validation/GakuMotion/`。
本仓库不依赖原研发目录或目录链接，也不携带 `.gitmodules`。

## 依赖与范围

- KineTexture 仅包含生产构建所需的 8 个核心头及 Apache LICENSE／NOTICE。
  代码取自原 gitlink `4f9c7e39d1620d262d94fa8ef4a929bad69ed0d5`，与已审阅新版头文件逐字节一致；
  NOTICE 单独保留了已纠正的历史来源引用，具体版本在冻结清单中记录。
- Dear ImGui 只包含 4 个核心 `.cpp`、必要头和 MIT 许可证。本地快照没有可解析的独立 Git 对象，
  因而记录实际文件哈希与原仓声明的 gitlink，不声称已证明上游 commit 字节完全一致。
- MinHook／HDE 的源码版权头及完整 BSD-2-Clause 文本保留。
- Candidate 中 NumPy 标量三角运算的 C# 转录保留 BSD-3-Clause 许可证及专门说明。

以前用户确认通过的游戏内验收不因本次源码整理而被推翻；本候选没有部署，也没有进行新的
游戏内加载或画面验收。合成契约测试只证明所测边界，不能代替完整插件构建或游戏内验收。
