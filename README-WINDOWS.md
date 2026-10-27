# DeskBridge · Windows 11 x64 个人构建

Windows 版本使用原生 QtWidgets 与现有共享核心；个人版显示名称与 GUI 程序为 **DeskBridge / DeskBridge.exe**，核心保留 `deskflow-core.exe` 以保持协议、资源和启动兼容。

Windows 包由仓库中的手动 GitHub Actions 工作流构建。工作流会编译源码、运行隔离回归测试，并检查包中的依赖与程序版本；安装版还检查安装、重装时保留设置及卸载。具体通过状态请查看对应的 Actions 运行记录。Windows 11 实机的键鼠与输入法交互验收仍需单独完成。

## 安装版与 Portable ZIP

每次成功构建提供：

- `deskbridge-版本-win-x64-setup.exe`：当前用户安装版，默认安装到 `%LOCALAPPDATA%\Programs\DeskBridge`，添加开始菜单入口，可选桌面快捷方式。
- `deskbridge-版本-win-x64-portable.zip`：解压到可写目录后双击 `DeskBridge.exe`。
- `SHA256SUMS-windows.txt`：两个包的 SHA-256 校验值。

当前包未加 Authenticode 签名，Windows 可能显示未知发布者；下载后请核对同次发布的 SHA-256 清单。

安装版使用独立的 DeskBridge 安装身份；重新安装会保留已有设置，卸载也会保留个人设置与证书。安装目录中的 `settings/DeskBridge.conf` 是桌面进程模式标记，GUI 与 core 共用同一位置的设置及 `settings/tls/` 信任数据。Portable 版也必须保留此标记。

更新 Portable 版时，先退出应用，将新 ZIP 解压到新目录，再把旧目录中的整个 `settings/` 文件夹复制过去（包括 `settings/tls/`）。新 ZIP 自带空白配置标记，直接覆盖解压会覆盖已有配置。

两种包均不包含 daemon，不安装系统服务；UAC 安全桌面和登录屏幕不在桌面进程模式的可用范围内。GUI/core 的单实例及本机 IPC 使用 `local.shuai.deskbridge` 身份，与其他安装身份隔离。首次连接需要正常核对与信任证书。窗口位置等 GUI 状态也可能写入当前用户的 DeskBridge 状态目录。

## 在 Windows 构建

使用 **Windows x64 MSVC developer PowerShell**，准备 Visual Studio 2022 C++ 工具及 Windows SDK、CMake 3.24+、Ninja、Git 和 Python 3.9+。依赖固定为：

- Qt **6.11.2 / msvc2022_64**，含 `qtbase`、`qtsvg`、`qttools`、`qttranslations`，以及 `lupdate` 构建工具需要的 `qtdeclarative`。
- vcpkg checkout **`469a0504e4a6ebf6173d1c0f51a7fa34c382fd43`**，先运行其 `bootstrap-vcpkg.bat`；个人版 manifest 使用同一 baseline，OpenSSL 为 **3.6.5**。
- Inno Setup **6.7.3**：用于将检查后的 portable 内容制作成当前用户安装程序。

Qt 使用现成的 MSVC 包，保持 `VCPKG_QT=OFF`。在源码根目录执行：

```powershell
./deploy/windows/build-personal.ps1 -QtRoot 'C:/Qt/6.11.2/msvc2022_64' -VcpkgRoot 'C:/src/vcpkg'
```

Inno Setup 不在默认位置时增加 `-InnoSetupCompiler 'C:/path/ISCC.exe'`；只需 ZIP 时增加 `-PortableOnly`。默认包目录是 `dist/windows/`，构建目录是 `build/personal-windows-x64/`。输出目录已有同类包时拒绝覆盖，可用 `-OutputDirectory '新目录'` 指定另一个位置。`-SkipTests` 只跳过回归测试，仍检查包与版本；正式工作流不跳过测试。

脚本只运行选定的 mock、数据／状态、协议与 widget 测试，包括多屏布局、持久化回滚、客户端／服务端布局同步、快捷键、偏好与断线恢复。它不会运行原生输入注入／剪贴板测试。测试使用 `QT_QPA_PLATFORM=offscreen`，并为测试二进制临时创建独立的 `settings/DeskBridge.conf`，以隔离 Settings 首次初始化；测试结束后恢复环境并移除本次创建的标记。

随后检查 ZIP 中的 GUI/core、portable 标记、Qt 平台插件、OpenSSL 与 VC runtime。版本 smoke 使用移除开发 Qt/vcpkg 路径后的 PATH；安装 smoke 使用构建目录里的临时安装位置，检查程序可运行、重装不覆盖设置、卸载删除程序但保留设置。

两个包均包含 `ThirdPartyLicenses/`：从经过 SHA-256 校验的对应 Qt 模块源码中收集许可与归属文本，复制已安装 OpenSSL 包的许可证，并附微软官方 VC runtime 原始许可文档及纯文本。文件中的来源链接对应各组件版本。

## 手动托管构建

[Windows 工作流](.github/workflows/personal-windows.yml) 使用 `windows-2022` 上的 x64 MSVC，固定 Qt、vcpkg 与经过 SHA-256 校验的 Inno Setup 编译器。工作流只有 `workflow_dispatch` 入口，权限为源码只读，不直接发布 Release。成功后上传 `deskbridge-windows-x64` artifact，保留 30 天。

工作流进入仓库默认分支后，可在 Actions 中运行 **DeskBridge Windows x64 packages**，也可以执行：

```sh
gh workflow run personal-windows.yml --repo shuaige619/DeskBridge --ref main
```

默认完整构建无需输入或发布凭据；仓库需允许 GitHub Actions，操作者需有运行工作流的权限。Windows Server runner 的测试结果与真实 Windows 11 桌面交互验收分别记录。可选 `diagnostics_only=true` 只构建并运行 `ShortcutRulesTests`、`SettingsTests`，用于读取平台断言，不生成或上传发布包；本地脚本对应 `-DiagnosticsOnly`。

发布前，再运行 **Verify Windows release packages**，输入成功构建的 run ID。此流程直接下载原始 ZIP／安装版并核对 SHA-256，不安装开发 SDK；清除 Qt／QML／OpenSSL 开发环境变量并限制 PATH 后，再检查程序版本、安装、重装与卸载：

```sh
gh workflow run personal-windows-verify.yml --repo shuaige619/DeskBridge --ref main -f source_run_id=构建运行ID
```

## 快捷键模式

快捷键设置只处理另一台设备送来的按键。自动方案根据键鼠来源系统与当前接收系统选择习惯；来源系统未知时不猜测，可在设置中明确指定来源，或选择 Mac／Windows/Linux 习惯。方案切换不会更换键鼠服务端。

- 复制／粘贴模式只转换所选应用中的 C/V；自定义规则继续按自己的应用范围生效。
- 开启完整快捷键模式后，Windows/Linux 习惯到 Mac 将 `Ctrl+键` 转为 `⌘+键`；Mac 习惯到 Windows/Linux 将 `⌘+键` 转为 `Ctrl+键`。同习惯的系统组合保持原生行为。
- 完整模式可选仅所选应用，或所有应用。所有应用只扩大全组合转换范围；自定义规则仍有各自的应用范围，所选应用中明确关闭的对应规则保留为例外。
- 仅转换包含普通键、方向键或功能键的组合；单独按住或松开 Ctrl／⌘ 保持原样。Shift、Alt 和锁定状态保留，已经同时包含 Ctrl 与 ⌘ 的组合不折叠；`Ctrl+Space` 保留给系统输入法。AltGr 等字符组合状态不参与完整模式转换。
- 首次按下决定规则，重复与松开沿用该次决定；切换前台应用不会改写仍按住的组合键。跨系统转换的是修饰键，具体命令仍由接收端应用解释。

所有应用模式也覆盖终端及应用内嵌终端：例如 Windows/Linux 习惯到 Mac 的 `Ctrl+C` 会发送 `⌘C`，不再是终端中断。需要原始 Ctrl 行为时，可关闭完整模式或改用所选应用范围，并将终端所在应用排除。

“轻按 Shift 切换中英文”本轮仅在 Mac 接收端合成为 Ctrl+Space；Windows/Linux 接收端保留原生输入法行为，不合成额外快捷键。该偏好保存为 schema 4，旧 schema 1–3 默认关闭。

Windows 本轮仍使用系统的登录启动设置；应用内“登录时启动”开关目前只实现了 macOS，Windows 中不会登记登录项。新增 `LoginStartupTests` 使用模拟操作，不写用户启动设置。

## 前台应用识别与验收

Windows 按完整 exe 路径标识应用，统一 `/` 分隔符并忽略大小写，例如 `c:/program files/example/example.exe`；只用查询进程镜像所需的有限权限，查询失败就不匹配应用规则，不读取窗口标题或输入内容。

Linux X11 使用前台窗口的 EWMH PID 解析本机 `/proc/<pid>/exe` 的规范绝对路径，保留大小写；远程 X11 与 Wayland 不尝试绕过系统隔离，无法识别时不匹配按应用规则。macOS 继续使用 bundle ID。配置可视选择器应使用与各平台一致的标识。

最终验收应在无开发 Qt/vcpkg PATH 的 Windows 11 普通用户会话中，检查启动、TLS、鼠标双向跨屏、键盘/输入法、按 App 与所有应用两种范围、按键释放、断开恢复、原版配置隔离。`--version` 通过不代表这些交互已经验收。

Mac 的 `⌘` 在 Windows 原生层对应 Win/Super 修饰键；当前代码提供普通修饰键调整与释放配对，尚未在 Windows 11 实机验证转换为 Ctrl 时的开始菜单行为。需要逐项确认：

- 左、右 `⌘` 分别配合 C/V；完整模式还应覆盖 A/F/Z、带 Shift／Alt 的组合与方向／功能键。转换正确且不意外打开开始菜单或触发 Win 组合。
- 先松开字母再松 `⌘`、先松 `⌘` 再松字母，两种顺序均不残留 Win/Ctrl 状态。
- 长按产生 repeat 时，转换和释放一致，不产生多余的菜单操作。
- 按住组合键时跨屏返回、断线、停止共享和重连后，不残留修饰键；仅所选应用模式下，未匹配的 App 保持原始按键行为。
- 单独按下／松开左右修饰键，以及 `Ctrl+Space`，保持系统原始行为；所有应用模式中的终端与关闭的规则例外符合设置说明。

这部分保留为实机验收项，没有依据推测增加 Windows 原生按键补丁。

## Linux 个人构建

Ubuntu 安装与构建步骤见 [README-LINUX.md](README-LINUX.md)。完整屏幕布局的远程调整需要两端运行配套 DeskBridge。
