# DeskBridge（桌桥）个人版

桌桥面向 macOS、Windows 和 Linux，用一个 App 处理跨电脑键鼠共享和快捷键适配。Mac 客户端与 Ubuntu 配套服务端已完成首页四方向布局的实连同步验证；Windows 构建与验收状态见 [README-WINDOWS.md](README-WINDOWS.md)。旧 Deskflow 服务端仍可共享键鼠，远程布局编辑需要配套 DeskBridge，无需运行 Hammerspoon。

## macOS 界面与可视化映射

主窗口使用 macOS 风格的侧栏、分组设置、原生窗口与编辑表单，支持系统浅色和深色外观：

- **设备连接**：显示实际屏幕关系、实时连接与 TLS 状态，可拖动电脑调整上下左右排列、连接、断开、修改地址和选择客户端／服务端角色。
- **快捷键映射**：自动／Mac 习惯／Windows・Linux 习惯三个快捷方案，以及总开关、应用范围、逐条开关、添加与编辑规则、还原草稿和恢复默认规则。
- **通用**：登录时启动、打开时自动连接、启动时隐藏窗口、关闭后继续在菜单栏运行、共享期间防止本机休眠，以及远程鼠标水平／垂直滚动方向和速度。
- **高级**：侧栏底部的独立页面，包含连接安全、本机证书指纹、查看／复制日志，以及默认折叠的网络、证书维护、诊断、自动化与重置选项。未完整支持的界面语言、Deskflow 更新检查和 Mac 无效图标样式不再作为个人版设置提供。

基础连接与错误日志保持启用，默认使用 INFO 级别；GUI 调试日志及详细输入耗时诊断默认关闭。查看或复制日志不会应用设置或重新连接共享。

通用与高级设置先保存在页面草稿中，切换页面或隐藏窗口不会丢失，点“应用更改”后才保存；“还原更改”取消本页未应用的编辑。窗口偏好及诊断日志级别无需重新连接；网络、滚动、防休眠等核心设置按需短暂重新连接，保存或启动失败时尝试恢复原配置。连接选项只编辑角色、来源地址和自动重连；远程键盘布局兼容入口位于快捷键映射页，与界面语言和输入法切换相互独立。

选择证书路径或密钥长度只修改维护草稿；只有明确确认“重新生成证书”才执行。维护前保留原证书备份，普通偏好保存不会隐式重建证书。新证书可能需要在另一台电脑上重新信任。Mac 不显示 Windows 系统服务选项；上游模式继续使用原有设置窗口。

新建或重建的个人版证书使用 `DeskBridge` 名称，全新安装默认保存为 `tls/deskbridge.pem`。升级会继续使用已配置的证书，旧安装未显式保存路径时会沿用既有的 `tls/deskflow.pem`，并固定保存所选路径。证书名称和文件名相互独立；旧 `Deskflow` 证书无需仅为更名而重建。证书与私钥在本机生成，不包含在安装包中。

默认将 Codex（`com.openai.codex`）和飞书（`com.electron.lark`）中的远程 `Ctrl+C / Ctrl+V` 转成 `Command+C / Command+V`。可以按下组合键录入来源和目标，并为每条规则选择应用，无需填写 bundle ID 或编写 Hammerspoon 脚本。支持字母、数字、功能键和导航键；macOS 可输出 F1–F20，F21–F24 仅能作为来源。

规则编辑器保存到草稿；点击主页面的 **应用更改** 才会写入设置。在线时会短暂重新连接，失败时尝试恢复上次设置并重新连接；离线且共享核心已停止时只保存，不自动启动共享。未应用的草稿切换页面或隐藏窗口后仍保留，退出前会提示。总开关只影响映射，键鼠共享继续工作。

### 首页屏幕布局

双方运行配套 DeskBridge 时，首页从键鼠来源端读取全部已配置电脑的实际布局，支持三台及以上设备。拖动任一电脑到相邻网格，松手后提交；也可选中电脑后使用方向键，Esc 取消。服务端保存并应用后，各端显示确认的排列和在线状态，无需重连共享。已占用的位置，以及会让设备失去相邻连通路径的移动会被拒绝并说明原因。

设备数量受来源端已配置网格容量限制（默认 5 列 × 3 行）；新增设备仍在来源端完整布局编辑器中配置。首页可排列完整双向相邻的连通布局；部分边缘映射、不可还原的自定义链接和外部配置保留完整编辑入口。只读、离线和不支持的布局会显示原因。旧 Deskflow 连接继续共享键鼠，远程布局需要双方更新为配套版本，Linux 构建与迁移见 [README-LINUX.md](README-LINUX.md)。保存或应用失败会恢复原配置；确认通信异常时先尝试重新读取，无法确认时可停止共享恢复原布局。

### 一键切换快捷键习惯

三个方案决定来源快捷键习惯，默认管理复制、粘贴；开启完整适配后也决定整组快捷键的转换方向。选择方案通过同一套保存与重连流程立即应用；已有自定义规则、应用范围、总开关和逐条开关保留：

| 方案 | 来源按键习惯 | 本机处理 |
| --- | --- | --- |
| 自动 | 新版 DeskBridge 服务端报告的系统 | Mac 来源使用 Command；Windows／Linux 来源使用 Ctrl，并转换为目标系统的原生 C/V |
| Mac 习惯 | Command+C/V | Mac 原生；Windows／Linux 转为 Ctrl+C/V |
| Windows・Linux 习惯 | Ctrl+C/V | Windows／Linux 原生；Mac 转为 Command+C/V |

相同习惯无需转换。`Ctrl+Space` 始终保留。手动修改受管复制／粘贴规则会进入“自定义”，避免方案在下次连接时覆盖编辑结果。一键切换不转移物理键鼠服务端；服务端角色需在连接选项中单独设置。

旧 Deskflow 服务端不会报告系统类型。自动方案中可手动选择一次来源系统；未选择且没有元数据时，受管 C/V 暂不转换，其他自定义规则保留。新服务端的检测结果优先；断线清除检测状态，更换地址清除旧手选来源。已有 schema 1 配置迁移为“自定义”，升级读取不写盘。 GUI 若附着到已运行的共享核心，可能未收到此前的系统报告；重新连接后刷新显示，核心已有的映射状态继续有效。

服务端若还配置了全局修饰键替换，预设 C/V 会暂停并在界面提示，避免重复转换；先在服务端取消冲突设置。Windows 按完整 exe 路径识别所选应用，Linux X11 按本地进程可执行路径识别，macOS 使用 bundle ID。Wayland 或前台应用查询失败时不匹配按应用规则；不会扩大到全部 App。

### 完整快捷键适配

开启“完整快捷键适配”，可将远程 Ctrl+A/C/V/X/Z/S/F 及 Ctrl+Shift 等组合统一转换为 Mac 的 Command 组合；反方向由所选来源习惯与目标系统决定。勾选“用于全部应用”后，整组适配覆盖全部应用；不勾选则限于现有应用范围。总开关仍可一键关闭全部转换。这两个选项经“应用更改”或一键方案保存，修改后在线连接会短暂重连。

这是组合键转换：裸 Ctrl/Command 和 Ctrl+鼠标点击不交换，Ctrl+Space 保留，Shift/Alt 和锁定状态保留。含 AltGr、Meta、Level5 或同时按来源与目标修饰键的组合维持原样，避免改变字符输入或把两个修饰键折叠为一个。

单条启用规则优先；其应用范围内停用的规则会保留对应原快捷键，作为完整适配的例外。全部应用不会扩大单条规则的应用范围。某组合涉及按应用规则但前台应用查询失败时保持原样；无需按应用判断的全局组合不查询前台应用。

完整模式会影响远程 Ctrl 原有功能，例如终端 Ctrl+C 会成为复制。需要终端原有行为时，可把完整适配限制到选定应用，或关闭完整适配。编辑受管复制/粘贴预设会切为自定义并关闭完整适配；添加其他自定义规则不关闭它。旧 schema 1/2 配置读取时将完整适配选项迁移为关闭；schema 1–3 的轻按 Shift 选项默认为关闭。读取不写盘；保存使用 schema 4。

### 规则范围与兼容

映射仅处理本机 **作为客户端收到的远程键盘输入**；本机物理键盘保持原行为。服务端发出按键，由各个接收客户端按目标系统转换。总应用范围与每条规则的应用范围共同限制生效位置，空范围不会匹配任何 App。添加总范围应用默认加入现有规则；从范围移除应用会停止匹配，并保留其规则选择供以后恢复。

匹配使用完整组合键，带额外 Shift / Alt / Command 等修饰的输入不会误触默认复制粘贴；Caps Lock 不影响匹配。首次按下时固定该次按键的来源、目标和应用选择，直到松开；重复、释放和断线清理沿用相同配对。`Ctrl+Space` 被保留为来源快捷键，不能用于重映射，以保持中英文切换。

**Codex 内置终端也属于 Codex。** 开启其复制规则后，终端中的 `Ctrl+C` 也会变成复制。需要保留终端中断时，请将 Codex 从应用范围移除，或在复制规则中取消选择 Codex。

完整规则作为 `client/shortcutConfig` 的一份 JSON 配置原子保存。首次读取旧版的复制粘贴开关与应用选择时会在内存中迁移，应用更改后才保存新格式。损坏或不支持的配置会关闭映射并在界面说明，可恢复默认规则。此功能在共享核心中执行，无需全局键盘监听器，也不运行 Hammerspoon 脚本。

## 轻按 Shift 切换输入法

在“快捷键映射 → 输入法切换”开启“轻按 Shift 切换中英文”，再点“应用更改”。Mac 接收端会将远程左或右 Shift 的独立轻按（按下至松开不超过300毫秒）转换为 Ctrl+Space；Mac 系统需已把 Ctrl+Space 配置为输入法切换。Ubuntu 原有的 Shift 切换继续由输入法处理，不需要为此替换旧服务端。

手势用于全部远程应用，独立于复制/粘贴及完整快捷键的应用范围，但受映射总开关控制；一键习惯方案和恢复默认规则保留这项独立偏好。它不会读取输入内容、判断当前中英文状态或修改系统输入法设置，也不处理本机物理键盘。Linux/Windows 接收端本版仅保留原生行为，界面明确显示此限制。

Shift 的按下与松开保持原样：只有独立轻按才补发切换组合。其它键、双 Shift、组合修饰键、鼠标按键/滚轮和长按都会取消候选；进入/离开屏幕、断线或停用共享不会把未完成的 Shift 当作一次轻按。服务端全局替换修饰键时暂停此转换，避免重复适配。

## 应用身份与配置

| 项目 | 个人版 |
| --- | --- |
| App / 主程序 | `DeskBridge.app` / `DeskBridge` |
| Bundle ID | `local.shuai.deskbridge` |
| macOS 设置 | `~/Library/DeskBridge/DeskBridge.conf` |
| TLS 证书与信任数据库 | `~/Library/DeskBridge/tls/` |
| GUI、core 的单实例和本机 IPC | 独立的 `local.shuai.deskbridge` 前缀 |
| 内置共享核心 | `Contents/MacOS/deskflow-core` |

`deskflow` 翻译、状态图标和核心二进制名称用于资源与协议兼容。个人版应用使用独立的桌桥图标；macOS 包含多尺寸 `DeskBridge.icns`，应用窗口与“关于”界面使用同一图形。关闭个人版构建选项时使用默认应用图标。构建与打包脚本不会安装 App，不会读取或复制原 Deskflow 的设置、证书、信任记录，也不会启动或停止任何共享进程。

## 构建

需要 macOS、Xcode Command Line Tools、CMake 3.24+、Ninja、Qt 6.7+（Core、Widgets、Network、LinguistTools、Qt 翻译，打包还需要 SVG 插件及框架）和 OpenSSL 3。Homebrew 环境可以安装：

```bash
brew install cmake ninja qtbase qttools qttranslations qtsvg openssl@3
```

在仓库根目录运行：

```bash
./deploy/mac/build-personal.sh
```

默认产物是 `build/personal-mac/bin/DeskBridge.app`。可以指定构建目录和额外 CMake 参数：

```bash
./deploy/mac/build-personal.sh build/personal-mac -DBUILD_TESTS=ON -DSKIP_BUILD_TESTS=ON
test_dir=$(mktemp -d /tmp/deskbridge-tests.XXXXXX)
XDG_CONFIG_HOME="$test_dir/config" XDG_STATE_HOME="$test_dir/state" \
QT_QPA_PLATFORM=offscreen ctest --test-dir build/personal-mac/src/unittests \
  -R '^(PacketStreamFilterTests|InputTimingMetricsTests|ShortcutRulesTests|ShortcutMapperTests|ShiftTapGestureTests|LoginStartupTests|OSXShortcutMapperTests|ComputerShortcutTests|SettingsTests|ChordRecorderTests|ShortcutPageTests|PersonalWindowTests|PersonalDialogTests|ShortcutApplyControllerTests|CoreProcessStopTests|ServerProxyTests|KeyMapTests|KeyStateTests|IKeyStateTests)$' \
  --output-on-failure
```

测试使用 Qt Test。`OSXKeyStateTests` 会向系统注入按键，`OSXClipboardTests` 会访问系统剪贴板；日常验证使用上述不产生系统输入的测试集合。可用临时的 `XDG_CONFIG_HOME`、`XDG_STATE_HOME` 隔离测试配置。迁移工具另有纯文件测试：`python3 -m unittest discover -s tools -p 'test_prepare_personal_config.py' -v`。

构建脚本只自动识别已经存在的依赖，不执行安装命令。非 Homebrew Qt 可以通过 `QT_PREFIX`、`QTTOOLS_PREFIX`、`QTTRANSLATIONS_PREFIX`、`OPENSSL_PREFIX`、`CMAKE_PREFIX_PATH` 指定位置；`DESKBRIDGE_BUILD_JOBS` 控制并行数。

个人身份由 `DESKBRIDGE_PERSONAL_BUILD=ON` 控制，Mac 默认开启；Windows、Linux 需显式开启。该选项关闭时恢复默认 App 身份。请为不同身份使用不同构建目录。

### 鼠标传输响应

发送协议消息时，长度头和正文现在一次交给网络层，避免发送线程在只有包头时提前唤醒。封包层对鼠标和控制小包使用栈缓冲区；协议编码与底层流缓存仍沿用各自的内存管理。较大的剪贴板消息保留相同的协议格式。客户端对输入消息的 `CNOP` 回复也使用此路径，因此 Ubuntu 服务端继续使用原 Deskflow 时，Mac 客户端可独立使用这项优化。

这将每条非空消息交给底层流的写入次数从两次减为一次，保留 TCP_NODELAY、TLS 和 Linux→Mac 的逐消息回复兼容机制。该次数变化可由 `PacketStreamFilterTests` 验证；实际鼠标端到端延迟仍需在两台机器的真实连接中测量，不能把写入次数减半等同于延迟减半。

Mac 客户端不再注册原来回调直接返回的全局事件过滤器，避免远程注入额外经过该过滤器的线程；Mac 作为服务端时继续捕获物理键鼠。拖拽和点击读取按钮事件映射时使用只读引用，避免复制映射表。

客户端收到剪贴板数据或剪贴板归属消息时，先提交已合并的鼠标位置，再解析和处理剪贴板。绝对／相对移动切换时也先提交上一类型的待处理移动，保持消息顺序；连续同类型移动继续合并，按键、点击和滚轮之前的刷新边界保留。这能避免已收到的鼠标位置等待剪贴板处理，但无法消除服务端大块数据占用同一 TCP 连接造成的传输等待。

#### 可选耗时诊断

设置 `DESKBRIDGE_INPUT_TIMING=1` 后，客户端按鼠标合并组及输入批次分别以 1/16 的比例采样。默认关闭，不读取诊断时钟；启用后使用固定大小的直方图，持续有输入时约每 10 秒在现有日志中输出汇总，不记录输入内容或坐标。

退出当前运行的桌桥后，可在开发环境从终端启动一次诊断会话：

```bash
DESKBRIDGE_INPUT_TIMING=1 build/personal-mac/bin/DeskBridge.app/Contents/MacOS/DeskBridge
```

这只影响该次启动，GUI 拉起的核心会继承开关。日志中的三个指标分别为：

- `mouse-last-parse-to-submit`：最终提交的位置，或合并位移中最后一条消息，从解析开始到系统注入调用返回的耗时。
- `mouse-pending-dwell`：一组鼠标移动从首次等待合并，到开始提交的等待时间。
- `input-batch`：正常完成的一批输入处理耗时，包含该批鼠标提交。断开或协议错误提前退出的批次不计入。

`p50/p95/p99` 是直方图桶的上界，`max` 是窗口内采样最大值。这些是客户端处理指标，不包含网络传输、macOS 消费已提交事件及屏幕显示的时间。建议分别对比普通移动、拖拽和复制大图片时的结果，再决定是否需要限制接收批次耗时或调整剪贴板调度。

## 打包到独立 App

```bash
./deploy/mac/package-personal.sh build/personal-mac
```

脚本使用新的暂存目录，将 GUI、core、Qt 框架和插件、OpenSSL 等动态依赖一起部署到 `dist/DeskBridge-时间戳.app`；检查非系统依赖是否仍指向外部绝对路径，并验证 bundle 签名。已有输出路径会被拒绝覆盖。

默认使用本地 ad-hoc 签名，不需要开发者账号，不做公证或上传。若已有用于自用开发的签名身份，可以显式选择：

```bash
SIGNING_IDENTITY='已有的签名身份' ./deploy/mac/package-personal.sh build/personal-mac
```

更换 ad-hoc 签名后的二进制可能需要在 macOS 中重新授权辅助功能等权限；个人版的 Bundle ID 与原版不同，原版的系统权限不会自动转给个人版。脚本不修改系统授权、不移除 quarantine、不更改 Gatekeeper 设置。请在系统正常的打开和授权流程中处理需要的权限。

## 首次配置与切换

如果要沿用同一台 Mac 已配对的 Ubuntu 连接，可先查看迁移计划：

```bash
python3 tools/prepare_personal_config.py
```

默认只检查配置和文件是否存在，不复制证书。确认后加 `--apply`，会把原客户端配置和本机 TLS 身份复制到新的私有 `~/Library/DeskBridge/`，保留服务端信任、屏幕名称及 TLS 验证，首次自动连接关闭。该目录必须尚不存在；工具拒绝覆盖，证书私钥只在本机复制且权限为 600。原配置不修改。沿用本机已被 Ubuntu 信任的身份时，不需要重新修改服务器的配对记录。

先审查个人版界面与快捷键选项，关闭“打开桌桥时自动连接 Ubuntu”，保留当前共享连接。确认设置后退出原 Deskflow 客户端和现有 Hammerspoon 转换，再手动启动个人版连接，避免两套客户端同时接收和注入输入。

Ubuntu 接着作为服务端、屏幕在左侧；Mac 在右侧。个人版使用客户端模式，填写 Ubuntu 的可达地址与原来的 Mac 屏幕名称，并使用与服务端一致的协议及 TLS 选项。如果选择全新身份，首次连接需正常核对、信任新的证书。确认鼠标能双向越过屏幕边缘后，再验证 Codex 和飞书中的复制、粘贴与 `Ctrl+Space`，最后启用个人版的自动连接或登录启动。

原版 App、配置和证书保持可用；需要恢复时退出个人版再启动原版。

## 本机验收（2026-10-19）

用户已在 Mac 与 Ubuntu 的实际连接中确认：

- 鼠标可以双向跨屏切换。
- Codex、飞书中的远程 `Ctrl+C / Ctrl+V` 均正常复制、粘贴。
- `Ctrl+Space` 切换输入法正常。

本机后续由 **Mac 运行 DeskBridge，Ubuntu 沿用 Deskflow**。Hammerspoon 不再需要运行，其配置和备份保留。Codex 内置终端的 `Ctrl+C` 限制仍适用；需要终端中断时，将 Codex 从应用范围移除，或在复制规则中取消选择 Codex。

### 多设备方案复验（2026-10-20）

用户使用接在 Ubuntu 上的真实键鼠确认，新增多设备快捷键方案后，当前“自动 + Linux 来源”配置下，鼠标双向跨屏、Codex／飞书普通输入框中的 Ctrl+C/V、Ctrl+空格输入法切换全部正常。三个方案的一键保存与重连此前已由安装版 GUI 验证。Windows 11 仍待编译和实机验收，见 [README-WINDOWS.md](README-WINDOWS.md)。

### 完整快捷键适配验收（2026-10-20）

本机已启用“完整快捷键适配 + 用于全部应用”，保持自动方案和 Linux 备用来源。用户用 Ubuntu 真实键盘完成 Ctrl+A/Z/Shift+Z/C/V、Ctrl+Space、鼠标双向跨屏及所请求浏览器 Ctrl+F 测试，回复全部正常。15 个相关安全测试 suite 通过。

启用时已备份并移除本机此前五个全局 Ctrl+C/V 菜单覆盖，恢复原生菜单组合；Codex/飞书各自 Command 菜单设置未改。旧 App、profile 与菜单备份在 `~/.cache/deskflow-setup/before-full-shortcuts-20261020/`。这是本机既有配置的恢复，应用不会自动修改其他用户的系统菜单快捷键。

## 登录启动与回退

在已安装的 Mac 版“通用 → 启动与窗口”中，“登录时启动”开关直接读取和修改系统登录项，独立即时生效，不需要点“应用更改”。它只控制后续登录时打开桌桥，不改变当前共享连接、自动连接、隐藏窗口或其他未保存设置。打开后要自动连接，可同时开启“打开桌桥时自动启动共享”。

macOS 13+ 使用系统 `SMAppService.mainApp` 管理主应用登录启动。若系统要求允许，开关旁显示待批准状态，可点“在系统设置中允许…”进入登录项设置；返回桌桥时重新读取状态。读取或修改失败会显示原因，读取不可靠时禁用开关。构建目录、dist 暂存副本与其他平台不会修改本机登录项；请从 `/Applications/DeskBridge.app` 或 `~/Applications/DeskBridge.app` 使用本功能。

原有 `~/Library/LaunchAgents/local.shuai.deskbridge.login.plist` 会兼容读取，查询不会自动迁移。明确关闭时只清理经过归属核对的旧桌桥启动文件；不卸载当前运行的 launchd job。随后开启使用系统主 App 登录项；新项需要批准时会保留旧文件并明确提示。不会操作原 Deskflow、Hammerspoon 或其他应用的启动项。

原 Deskflow 登录启动文件备份到 `~/.cache/deskflow-setup/org.deskflow.login.plist.before-deskbridge`，避免两套客户端在登录时同时连接。Hammerspoon 退出并关闭登录启动，保留原配置与备份供回退使用。

历史启动项模板保存在 `deploy/mac/local.shuai.deskbridge.login.plist`，用于兼容与恢复。此前已验证过该 LaunchAgent 拉起 GUI/core 并自动重连 Ubuntu；此检查通过加载启动项完成，未注销当前系统会话。新的应用内开关验证另见本轮验证记录。

需要回退时：

1. 在桌桥“通用”中关闭“登录时启动”，再退出 DeskBridge。旧版没有该开关时需停用旧的 DeskBridge LaunchAgent。
2. 将原 Deskflow plist 备份恢复至 `~/Library/LaunchAgents/org.deskflow.login.plist`，重新加载它或手动启动原 Deskflow。
3. 如需恢复旧的快捷键转换，再启动 Hammerspoon；需要随登录运行时重新启用其登录启动。

Ubuntu 端无需更换软件。原 Deskflow App、配置和证书继续保留；回退时也应只运行一套 Mac 客户端与快捷键转换规则。

### 统一轻按 Shift 实机验收（2026-10-20）

用户确认已安装版本的全部真实键鼠测试正常：远程左右Shift轻按切换中英文、Shift+A及Shift+方向键不误触、Ctrl+Space、Ctrl+C/V和鼠标双向跨屏。当前已开启轻按Shift，保留Ubuntu原生输入法行为及Mac的原有完整快捷键适配、登录启动和窗口偏好。

### 添加应用交互修复（2026-10-20）

“添加应用”窗口现使用整行多选：点击名称或图标即可选择，再次点击取消本次选择；按钮显示“添加N个应用”，没有新选时不能确认。搜索保留已选项，已添加应用明确标记并可返回映射页移除。确认后应用标签及规则范围立即回显为草稿，点“应用更改”后保存。修复版已在Mac原生界面验证新增回显，17组安全回归通过，原运行配置保持不变。
