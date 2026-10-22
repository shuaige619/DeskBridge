# DeskBridge 配置参考

日常连接请通过桌桥界面配置，完整操作见[使用 README](../README.md)。本文说明个人版的数据目录、常用配置键和共享核心配置，便于备份及排查问题。

## 配置与数据目录

| 环境 | 设置文件 |
| --- | --- |
| macOS | `~/Library/DeskBridge/DeskBridge.conf` |
| Linux 原生运行 | `~/.config/DeskBridge/DeskBridge.conf` |
| Linux Flatpak | `~/.var/app/local.shuai.deskbridge/config/DeskBridge/DeskBridge.conf` |
| Windows 安装版／便携发行包 | `<安装或解压目录>/settings/DeskBridge.conf` |
| Windows 原生运行且无 portable 标记 | `%APPDATA%/DeskBridge/DeskBridge.conf` |

设置了 `XDG_CONFIG_HOME` 时，应用优先使用 `$XDG_CONFIG_HOME/DeskBridge/DeskBridge.conf`，适用于隔离测试配置。用户路径没有配置时，macOS、Linux 还会检查 `/Library/DeskBridge/DeskBridge.conf`、`/etc/DeskBridge/DeskBridge.conf` 等系统路径；Windows 检查 `C:/ProgramData/DeskBridge/DeskBridge.conf`。实际加载路径会输出到启动日志。

TLS 证书、私钥及信任数据库位于配置目录的 `tls/`；Windows 无 portable 标记时 TLS 数据使用系统数据目录。全新个人版默认使用 `tls/deskbridge.pem`。升级继续使用已经选定的证书路径，不能仅因文件名与新默认值不同就删除或重建证书。日志路径可在“高级”中配置。

Windows 发行包中的 `settings/DeskBridge.conf` 同时是桌面进程模式标记，请保留。配置文件与证书需一起备份；备份含私钥和已信任设备信息，应保存在自己的受限目录中。退出应用后再做文件级备份或恢复。

窗口位置等状态存放在 Qt 的状态目录下的 `DeskBridge.state`，可用 `XDG_STATE_HOME` 隔离；portable 模式的状态键随设置保存。清除窗口状态不等于重置连接、快捷键或证书。

## 常用设置键

设置为 QSettings INI 格式，键名通常表示为 `section/option`。例如：

```ini
[core]
computerName=macbook
coreMode=1
port=24800

[client]
remoteHost=192.168.1.20

[security]
tlsEnabled=true
checkPeerFingerprints=true
```

这是接收端设置片段，不能替代来源端设备布局。界面修改一般先保存为草稿，按“应用更改”才写入文件；连接核心相关设置会按需重新连接。手动编辑前退出应用，避免正在运行的界面覆盖文件。

| 键 | 类型／默认值 | 用途 |
| --- | --- | --- |
| `core/computerName` | 字符串，初始取本机名称 | 本机设备身份，必须与来源端布局中的名称或别名匹配 |
| `core/coreMode` | `0` 未选择、`1` 接收端、`2` 来源端 | 共享角色 |
| `core/port` | 整数，`24800` | 两端使用一致的 TCP 端口 |
| `core/interface` | IP 地址，空值自动选择 | 来源端监听地址 |
| `core/preventSleep` | 布尔值 | 共享核心运行时防止本机休眠 |
| `client/remoteHost` | IP 地址或主机名 | 来源端地址，可用逗号分隔候选地址 |
| `client/dynamicConnectionInterval` | 布尔值，默认 `false` | 连接失败时逐步延长重试间隔；关闭后仍会重试 |
| `client/invertYScroll`、`client/invertXScroll` | 布尔值，默认 `false` | 接收端远程滚动方向 |
| `client/yScrollScale`、`client/xScrollScale` | 小数，默认 `1.0` | 接收端滚动速度 |
| `client/shortcutConfig` | JSON 字符串 | 接收端快捷键规则、应用范围及输入法手势，当前保存 schema 4 |
| `gui/startCoreWithGui` | 布尔值 | 打开应用时自动开始共享 |
| `gui/autoHide` | 布尔值 | 启动时隐藏主窗口 |
| `gui/closeToTray` | 布尔值 | 关闭窗口后继续在菜单栏／托盘运行 |
| `gui/showVersionInTitle` | 布尔值 | 在标题显示版本 |
| `security/tlsEnabled` | 布尔值，默认 `true` | TLS 加密 |
| `security/checkPeerFingerprints` | 布尔值，默认 `true` | 核对并信任对端指纹 |
| `security/certificate` | 文件绝对路径 | 本机证书及私钥文件 |
| `security/keySize` | `2048` 或 `4096`，默认 `2048` | 新建证书的密钥长度；更改偏好不会隐式重建证书 |
| `log/level` | 日志级别，默认 `INFO` | 基础日志级别 |
| `log/guiDebug` | 布尔值 | GUI 调试日志 |
| `log/toFile`、`log/file` | 布尔值、文件路径 | 是否保存日志及目标位置 |

Mac 的登录启动由系统登录项管理，独立于 `gui/startCoreWithGui`。更新检查与 Windows 系统服务不是当前个人发行包的常规设置，不应通过旧配置示例启用。

## 来源端布局与共享行为

来源端的“管理设备与控制…”提供设备布局、控制热键、共享行为、连接与配置四页。常用键包括：

| 键 | 默认值／说明 |
| --- | --- |
| `server/gridWidth`、`server/gridHeight` | 默认 `5`、`3`，决定图形布局网格容量 |
| `server/enableClipboard` | 是否共享剪贴板 |
| `server/clipboardSize` | 默认 `3` MiB，由来源端控制剪贴板大小限制 |
| `server/enableSwitchDelay`、`server/switchDelay` | 是否启用边缘停留切屏及等待毫秒数，默认数值 `250` |
| `server/enableSwitchDoubleTap`、`server/switchDoubleTap` | 是否启用双击边缘切屏及间隔毫秒数，默认数值 `250` |
| `server/disableLockToComputer` | 是否禁用默认 Scroll Lock 锁定切屏功能 |
| `server/defaultLockToComputerState` | 切换后默认锁定光标的状态 |
| `server/relativeMouseMoves` | 光标锁定时使用相对鼠标移动 |
| `server/protocol` | `barrier`（默认）或 `synergy`，两端需兼容 |
| `server/externalConfig` | 是否从外部文件读取来源端配置 |
| `server/externalConfigFile` | 外部配置文件路径 |

图形设备和热键数据位于 `[internalConfig]`；单个设备的兼容设置位于 `[computer_设备名]`。这些数据有数组及索引关系，建议通过完整编辑器保存或导出，不直接修改单个索引。

首页编辑只支持可表示为完整相邻连通网格的布局。外部文件、部分边缘映射或其他复杂链接可能只读；完整编辑入口仍可查看和导出。导出不会隐式保存尚未应用的草稿。

## 共享核心配置文件

GUI 根据布局生成核心配置；外部配置使用共享核心的文本格式，区别于上面的 INI。下面表示 `macbook` 位于 `linux-pc` 右侧，两侧可互相返回：

```text
section: screens
    linux-pc:
    macbook:
end

section: links
    linux-pc:
        right = macbook
    macbook:
        left = linux-pc
end
```

设备先在 `screens` 声明，再在 `links` 中引用；设备名和别名查找忽略大小写，不能仅以大小写区别两台设备。每个方向需明确写出，单向链接不会自动生成反向链接。以 `#` 开头的内容是注释。

需要命令行诊断时，真实核心程序名仍为 `deskflow-core`（Windows 为 `deskflow-core.exe`），它是安装包内部兼容名称。用该文件的 `--help` 查看当前参数；来源端使用 `--server --config /absolute/path/server.conf`。正常使用由 GUI 启动核心，避免另开核心争用端口。

配置键及搜索路径以 [`Settings.h`](../src/lib/common/Settings.h)、[`Settings.cpp`](../src/lib/common/Settings.cpp) 为准；完整语法解析见 [`Config.cpp`](../src/lib/server/Config.cpp)。[返回使用说明](../README.md)。
