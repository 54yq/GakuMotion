# GakuMotion 文档

首次使用从安装与使用指南开始。开发与格式文档说明当前代码契约；带日期的历史记录保留各自版本的结论。

## 使用与开发

| 需要完成的事 | 文档 |
| --- | --- |
| 了解项目与功能 | [项目首页](../README.md) |
| 安装、更新与确认文件位置 | [安装指南](INSTALLATION.md) |
| 导入动作、管理曲库、播放和设置镜头 | [使用指南](USAGE.md) |
| 排查面板不可用、导入或播放问题 | [常见问题](TROUBLESHOOTING.md) |
| 准备工具链与查看当前构建入口 | [构建指南](../BUILD_NOTES.md) |
| 理解宿主、导入器、运行时与共享核心 | [架构](ARCHITECTURE.md) |
| 读写动作与镜头文件 | [动作文件格式](formats/ACTION_FORMATS.md) |
| 开发宿主插件 | [GakuLoader 插件指南](https://github.com/crested-ezios/GakuLoader/blob/main/docs/PLUGIN_DEVELOPMENT.md) |
| 查看组件许可 | [第三方通知](../THIRD_PARTY_NOTICES.md) |

## 历史与兼容性记录

[历史文档索引](history/README.md) 区分当轮实现、离线验证、部署和用户验收。原生表情尾部的离线工具说明也保留为历史契约，相关 Python 工具不属于当前产品导入流程。

## 源码维护

当前源码范围与维护记录见 [SOURCE_REVIEW.md](../SOURCE_REVIEW.md)，逐文件身份保存在 `freeze-manifest.json`。Agent 工作规则从 [AGENTS.md](../AGENTS.md) 开始。
