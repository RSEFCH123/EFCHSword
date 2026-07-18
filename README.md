# EFCHSword
<p align="center">
<strong>Windows 平台驱动级系统管理工具</strong>
</p>

<p align="center">
<img alt="Platform" src="https://img.shields.io/badge/平台-Windows%207%2B%20%2F%20Win10%2B%20驱动-blue">
<img alt="Mode" src="https://img.shields.io/badge/运行模式-R0%20%2F%20R3-cyan">
<img alt="Version" src="https://img.shields.io/badge/版本-EFCHSword%20%2F%20EFCHSwordNeo-orange">
</p>

<p align="center">
<a href="">EFCHSword</a> ·
<a href="">EFCHSwordNeo</a>
</p>

EFCHSword 是一款强大的 Windows 平台驱动级计算机管理工具，提供进程、窗口、文件、自启动、服务、注册表与进程行为的全方位管理能力。  

本项目目前包含两个版本：  
  
EFCHSword：基于 易语言（E） 编写，主打极致的自我保护能力与最强悍的暴力操作。  
EFCHSwordNeo：基于 Qt 编写，拥有更加现代、美观的 UI 界面与更优秀的视图体验，底层共用相同的驱动与核心功能。  
驱动模式（R0）：Windows 10 及以上系统  
无驱动模式（R3）：Windows 7 及以上系统  
📊 版本对比  
两个版本共用 EFCHKernelDriver，拥有完全一致的驱动级功能，但在侧重点上有所不同：  
  
| 特性 | EFCHSword (易语言) | EFCHSwordNeo (Qt) |
|---|---|---|
| 核心驱动 | EFCHKernelDriver | EFCHKernelDriver |
| 核心功能 | 完整驱动级功能 | 完整驱动级功能 |
| UI 界面 | 经典实用 | ✨ 现代化、更好的视图体验 |
| 自我保护能力 | 🛡️ 极度强大 | 🔸 标准保护 |
| 暴力操作程度 | 🔥 极度强硬 | 🔸 标准 |
| 适用场景 | 极端环境对抗、底层强杀 | 日常管理、高效操作 |  
  
🛡️ Rootkit 对抗 · Fake 模式  
你是否曾被 Rootkit 的专门对抗所困扰？EFCHSword 拥有 Fake 模式，可在一定程度上对抗 Rootkit 与针对性病毒——一种专为对抗而生的特殊模式。  
  
普通模式  
bash  
  
EFCHSword.exe  
Ob 进程保护  
Fake 模式  
bash  
  
EFCHSword.exe -fake  
关键进程保护  
Ob 进程保护  
对抗内核 NtTerminateProcess  
PPL_System 保护  
自动隐藏进程  
自我文件保护  
FakeSafe 模式（兼容版本）  
bash  
  
EFCHSword.exe -fakesafe  
Fake 模式的兼容版本，在部分环境下提供更好的兼容性。  
  
💾 Storport 级扇区读写  
EFCHSword 支持 Storport 级扇区读写，可突破绝大多数还原软件与 DiskHook，绕过上层过滤驱动，直达存储栈底层——让你夺回硬盘的真正控制权。  

🧩 核心功能  
🧠 进程管理  
实时查看所有进程  
终止任意进程（驱动级）  
暂停 / 恢复进程执行  
进程优先级调整  
DLL 加载管理  
进程窗口管理  
进程线程管理  
重置 PID  
进程保护  
⊞ 窗口管理  
查看所有活动窗口  
强制关闭窗口  
窗口置顶 / 取消置顶  
窗口隐藏 / 显示  
📁 文件管理  
高级文件浏览器  
文件提取与保护  
强制删除 / IRP 驱动级删除  
文件内容管理  
集成 Explorer 右键菜单  
文件占用分析  
文件属性管理  
⚙️ 系统管理  
启动项管理（启用 / 禁用）  
系统服务管理  
NT 驱动安装与调试  
PPL 设置  
🔒 安全监控  
实时进程行为监控  
文件操作追踪  
注册表变更监控  
网络活动监视  
🛠️ 工具与注册表  
注册表编辑器  
注册表项查看 / 新建 / 删除 / 管理  
快速关机  
PC 配置信息  
Windows Update 设置  
文件数据读取器  
DiskHex  
🧬 Windows NT 内核控制  
驱动查看  
驱动卸载器  
🧪 实验性功能  
有趣 · 危险 · 强大  
  
| 功能 | 说明 |
|---|---|
| 🎨 多色彩蓝屏 | 重新定义系统崩溃的视觉表达，甚至可以彩色蓝屏 |
| 🔑 SudoForWindows | 让普通程序无视文件权限，类 Unix sudo 的 Windows 实现 |
| 🚫 禁用蓝屏 | 让系统不再蓝屏，在极端场景下维持持续运行 |
| 🔒 关闭 CPU | 让整个屏幕冻结，极致的系统级控制能力展示 |  
  
⚠️ 重要提示  
EFCHSword 使用驱动级操作，实验性功能具有高度危险性，请谨慎使用。开发者对因使用本软件导致的任何问题不承担责任。  
  
📌 系统要求  
| 模式 | 最低系统 |
|---|---|
| 驱动模式（R0） | Windows 10 及以上 |
| 无驱动模式（R3） | Windows 7 及以上 |  
  
📦 相关项目  
EFCHSword — 基于易语言开发，极致的自我保护与暴力对抗版本。  
EFCHSwordNeo — 基于 Qt 开发，拥有现代化 UI 与优质视图体验的版本。  
