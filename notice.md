# CUA 验证与部署说明

CUA 位于 WUA-CLI 的同一工程和 EXE。公开入口使用 Windows Session ID、HWND 和截图坐标；不暴露捕获后端、RPC、View 或自定义引用。用法见 [Agent Guide](Source/WUA-CLI/AGENTS.md)、[命令参考](Source/WUA-CLI/Tools/CUA/README.md) 和 [调用示例](Source/WUA-CLI/Tools/CUA/Examples.md)。

## 本地 MLE 构建

WUA 暂时依赖尚未发布的 `KNSoft.MakeLifeEasier 1.0.12-beta.local.4`。为复现构建，在仓库根目录运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Source/Build/Build-MLE.ps1
msbuild Source/WinUseAgent.slnx /m /p:Configuration=Release /p:Platform=x64
msbuild Source/WinUseAgent.slnx /m /p:Configuration=Release /p:Platform=x86
```

脚本下载固定 MLE 公开提交，校验源码归档和补丁哈希，加入 `PS_CreateProcess` 的 CreationFlags 参数，构建 x64/x86 Debug/Release 库并恢复依赖。产物位于 `Source/OutDir/MleLocal4`；不要求另一个 MLE 工作区或预先保存的包。正式分发前需发布包含该入口的 MLE 版本并更新依赖。

## 2026-10-08 虚拟机验证

专用 VM 为 `ZPigeon-Win11-26H1`，ID `BD0484E6-87F2-46CD-9E5D-43098A0B60E5`，Windows 11 x64 `28000.2956`。本轮检查点 ID 为 `e4a57ca1-82dd-4df9-834a-5925ae8d40f7`，证据目录为 `Source/OutDir/CuaVm-20261008-025643`。下列结果来自实际应用或独立 Windows 状态检查，不仅是 API 返回成功：

- 管理员真实 RDP Session 2：x64 WGC Harness 136 项、GDI Harness 123 项、x86 Harness 131 项、RPC 阴性测试 14 项通过。覆盖窗口／桌面观察、UIA RuntimeId、键鼠、Message／Paste、剪贴板恢复、窗口状态、Worker 重启及 UIA provider 卡住后的有限等待和原生观察／输入可用性。
- 管理员 100% 桌面：DPI unaware、system aware、PMv2 及各自的 RTL 镜像控件，共六组输入和 caret 坐标检查通过。
- ChildSession 46 项通过：并发 CreateChild 复用、Preview 显隐、隐藏预览下截图／UIA／输入、两次 Worker 故障恢复、父会话保护、Destroy 注销以及再次创建均经 OS 状态核对。
- 删除测试用保存凭据后，两次 Child Worker 故障恢复保持同一登录和 Explorer；重复 CreateChild 复用会话，随后 Destroy 完成。生产 CLI 没有接收或保存密码的接口。
- Windows Notepad：通过公有 CUA 命令观察、Message 输入、按键保存并关闭；独立读取保存文件确认 `WUA harness verification 中文 🙂`。
- 普通用户真实 Session 6、150% 桌面：Harness 130 项和六组 DPI／RTL 输入及 caret 坐标检查通过。runner／Worker 均为 SID 结尾 1006，admin=false、UIAccess=false、Mandatory Label RID 8192；最终二进制的跨账户 `CUA Inspect Session=2` 返回 `E_ACCESSDENIED`。

管理员测试账号 SID 结尾 1005。另一次启动验证中，Medium RID 8192 的过滤管理员调用方成功启动同 SID、High RID 12288、admin=true、UIAccess=true 的 Worker；独立 OS 检查确认仅一个 launcher 和 Worker。该项使用 VM 内临时自动同意的 UAC 策略，原值已恢复；没有验证人工 UAC 同意／取消路径。

测试仅操作 VM 桌面，未使用主机 CUA 或键鼠，未读取主机密码文件，未休眠本机。运行过程中发现并修复了 RuntimeId 有符号边界、普通用户窗口激活、当前会话 UIAccess 启动失败回退，以及原生菜单循环中的焦点／输入提示误导。每次失败的证据保留，最终回归使用部署哈希一致的二进制。

整套 solution 的 x64/x86 Debug/Release 编译和代码分析通过：WUA 自身 0 警告，NDK 头文件每配置仍有 5 条既有诊断。源码的 BOM／CRLF／行宽、项目引用、PMv2 manifest、PowerShell 5.1 语法，以及 23 个公开分支的命令和 JSON 示例均通过检查。

已恢复本轮开始前的 VM 检查点并保持关机；仅删除本轮检查点，原有两个检查点、磁盘和网卡保留。VM 内测试账户／设置由回滚还原，临时凭据 helper 正常退出并删除保存凭据；仅删除本轮两份主机 DPAPI 文件，其他凭据保留。最终 726 份证据文件逐文件 SHA-256 核对通过；记录见证据目录中的 `evidence-final-collected.json`、`auth-final-cleanup.json` 和 `vm-cleanup-result.json`。

## 行为与限制

- `ok` 表示主要 API 调用成功，不保证目标应用接受输入。操作后的观察、截图或剪贴板恢复错误不能作为自动重发输入的依据。
- 只有指定 OutFile 才截图；窗口图片包含非客户区，UIA 和 caret 已转换到对应截图像素坐标。图像、UIA、GetGUIThreadInfo 不是原子采样；HWND 没有跨调用的身份保证。
- UIA 每 Worker 使用一个 MTA 线程。超时结束调用方等待，底层请求仍需完成回收；永久阻塞的 provider 会让后续 UIA 持续忙。Element 超时后仍可能执行，不自动重发。
- Paste 按既定取舍保留 OleGetClipboard 对象，恢复前检查所有权，尽力恢复；对象引用不是独立数据快照。异常 OLE 源可能阻塞，没有严格执行时限。
- 原生菜单和移动／大小循环不输出焦点或 caret 提示，Text 拒绝向循环后面保留的编辑框输入；Keys 可用于完成或取消循环。UIA focused 只表示 provider 报告的焦点。
- Worker 按当前令牌能力尽力运行。管理员组账户尝试 UAC 和 UIAccess，失败仍可继续；其他账户直接运行。RPC 要求同用户 SID 且至少 medium 完整性。

Windows 10、旧版 Windows Server、多显示器负坐标尚未运行验证。Session 参数已预留其他 Windows 会话的路由，但仅可选择已有的同账户 Worker，不支持任意会话登录；当前只创建一个 ChildSession。

## 可选 ChildSession 设置

`CUA Session EnableChildSession` 显式启用 Windows Child Sessions，并补充默认凭据委派的 `TERMSRV/localhost` 项和密码登录设置。CreateChild 不自动修改系统配置。

委派策略位于 `HKLM\SOFTWARE\Policies\Microsoft\Windows\CredentialsDelegation`，密码登录值位于 `HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\PasswordLess\Device`；两种架构均访问 64 位视图，组织策略可能覆盖本地值。这些设置不保证免除系统凭据提示。需要认证时由用户在 Windows 系统窗口输入；CLI 不接收或保存密码。保留已登录 ChildSession 可避免 Worker 重启时重新认证，隐藏 Preview 不注销会话。

## 此前主机设置：需要用户定夺

以下设置来自此前主机测试，本轮 VM 验证未修改这些主机配置：

| 设置 | 测试前 | 此前测试后 |
| --- | --- | --- |
| Windows Child Sessions | 关闭 | 启用 |
| AllowDefaultCredentials、AllowDefCredentialsWhenNTLMOnly | DWORD 1 | 未改 |
| 两个同名子键中的字符串 2147483647 | 不存在 | TERMSRV/localhost |
| DevicePasswordLessBuildVersion | 2 | 0 |

既有 `TERMSRV/*` 项未改。原值备份位于 `Source/OutDir/ChildSessionDemo-20261001/registry-backup.json` 和 `original-enabled.txt`。用户需决定保留或恢复这些设置；不能因本轮 VM 成功便视为已批准长期改变主机配置。
