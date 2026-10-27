# DeskBridge Linux 服务端

Mac 客户端的主页拖放布局需要来源端也运行配套 DeskBridge。双方需使用配套版本以提供布局同步和来源系统检测。本文件准备独立的 Linux 服务端构建与配置。

## Linux x86_64 发行包

Linux 新版为 `DeskBridge-1.27.0.3-linux-x86_64.flatpak`，由 Ubuntu 24.04 GitHub runner 在 KDE SDK 6.11 中完整构建，使用 KDE Platform 6.11 运行时。新版统一了设备连接、快捷键映射、通用、高级和完整设备控制界面。个人版的托盘和启动器使用独立的桌桥图标；单色图标不可用时回退为桌桥彩色图标。安装发行版提供的 Flatpak 后，添加 Flathub 以取得匹配运行时，核对发行页 `SHA256SUMS`，再安装本地 bundle：

```bash
flatpak remote-add --user --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
flatpak install --user flathub org.kde.Platform//6.11
flatpak install --user ./DeskBridge-1.27.0.3-linux-x86_64.flatpak
```

完成配置检查后，从图形会话运行：

```bash
flatpak run local.shuai.deskbridge
```

应用 ID 为 `local.shuai.deskbridge`，配置位于 `~/.var/app/local.shuai.deskbridge/config/DeskBridge`。发行包不携带用户配置、证书、私钥、信任记录或运行日志。首次运行按[使用 README](README.md)设置来源／接收角色、设备布局并核对证书。

来源端与接收端都可从“设备连接”页打开“管理设备与控制…”。来源端可管理设备、控制热键和共享行为；接收端显示实际来源、设备信息，以及本机可用的布局、快捷键映射和连接设置入口。设备增删、控制热键及共享行为等由来源端管理的项目会标明“需要到来源端设置”；尚未同步的值会明确说明。接收端只有在来源端提供并允许编辑布局时，才能调整屏幕位置。

构建使用 KDE SDK 6.11 提供的编译器、Qt、OpenSSL 和系统库，以及按仓库 Flatpak manifest 固定 commit 构建的 libei 和 libportal。bundle 内含应用与附加依赖的许可证和源码链接；构建记录保留 SDK／运行时实际 commit。此前版本已在 Ubuntu 22.04 X11 完成真实连接验证，本次安装包的自动化构建结果不等于新的桌面实机验收。

## 从完整源码生成 Flatpak

[Linux GitHub Actions 工作流](.github/workflows/personal-linux.yml)在 Ubuntu 24.04 runner 上安装 KDE SDK／Platform 6.11，使用 [个人版 Flatpak manifest](deploy/linux/personal-flatpak.yml)编译完整源码、运行隔离回归测试、导出安装包，再独立导入核对包内二进制、依赖及文档。

工作流支持手动运行；`codex/**` 分支中的源码、Linux 打包、版本或相关文档变更也会触发构建，不自动发布 Release。将最终代码提交到目标分支后，可在 Actions 运行 **DeskBridge Linux x86_64 package**，或执行：

```bash
gh workflow run personal-linux.yml --repo shuaige619/DeskBridge --ref main
```

成功后下载 `deskbridge-linux-x86_64` artifact，内含 Flatpak、校验清单、安装说明、源码清单及验证记录。检查运行结果后再发布；失败日志由 diagnostics artifact 保留。SDK 和二进制运行验证与真实跨屏验收分别记录。

## 构建依赖

源码要求 CMake 3.24+、支持 C++20 的 C/C++ 编译器、Ninja、pkg-config、Qt 6.7+（Core、Widgets、Network、DBus、LinguistTools；测试还需 Test）、OpenSSL 3.0+、libei 1.3+、libportal 0.9.1+、xkbcommon 和 xkbregistry。默认 X11 后端还需要 X11、Xtst、xkbfile 及相关扩展的开发文件。Python 3 用于配置迁移和迁移测试。

仓库 [CI 依赖脚本](.github/actions/install-dependencies/action.yml) 的 Debian 系列包清单如下；应先检查目标 Ubuntu 的发行版和软件源提供的版本，包名存在不表示满足以上最低版本：

```text
cmake build-essential ninja-build xorg-dev libx11-dev libxtst-dev
libssl-dev libxkbfile-dev qt6-base-dev qt6-tools-dev libxkbregistry-dev
libxkbcommon-dev libei-dev libportal-dev help2man
```

构建脚本会检查已经安装的依赖，不安装软件、不启动共享。它关闭构建时自动运行全部上游测试，以及通过 help2man 调用程序生成手册的步骤。自定义 Qt 可设置 `QT_PREFIX` 或 `CMAKE_PREFIX_PATH`，依赖的 pkg-config 文件可通过 `PKG_CONFIG_PATH` 指定。

```bash
./deploy/linux/build-personal.sh --test
```

`--test` 运行隔离回归测试，覆盖布局、配置事务、设备名称校验、服务端发布、传输、客户端协议、快捷键、同用户 IPC、设置和托盘图标选择，并运行个人配置工具的纯文件测试。默认输出为 `build/personal-linux/bin/DeskBridge` 和 `build/personal-linux/bin/deskflow-core`。可省略 `--test` 只构建，或指定构建目录和额外 CMake 参数：

```bash
./deploy/linux/build-personal.sh --test build/personal-linux -DQt6_DIR=/path/to/Qt/lib/cmake/Qt6
```

已在 KDE SDK 内构建、测试并安装到 `/app` 暂存目录后，可用下列脚本导出本地 bundle。`prepared-flatpak` 是已完成 `flatpak build-finish` 的 SDK 构建目录；依赖源码目录需包含 manifest 固定 commit 的 `libei` 与 `libportal` Git 检出。脚本验证这两个 commit，复制运行所需文件与许可证，排除构建工具、头文件、Python 包和缓存，清除 ELF 调试路径，再检查私人路径及敏感文件名。

```bash
DESKBRIDGE_DEPENDENCY_SOURCES=/path/to/pinned-sources \
  ./deploy/linux/package-personal-flatpak.sh /path/to/prepared-flatpak \
  /path/to/new-release-directory SOURCE_COMMIT
```

此步骤需要 Flatpak、Git、Python 3、GNU strip、sha256sum，以及完整 GNU GPL 3 文本（默认 `/usr/share/common-licenses/GPL-3`，可通过 `DESKBRIDGE_GPL3_LICENSE` 指定）。它创建新的导出目录、仓库、bundle、校验文件和验证记录，拒绝覆盖输出，不会安装、启动或替换当前应用。

## 独立安装与切换

构建脚本默认配置的安装根目录为 `~/.local/opt/deskbridge`；可在构建前通过 `DESKBRIDGE_INSTALL_PREFIX` 指定其他目录。安装是独立的一步：

```bash
cmake --install build/personal-linux
```

个人版将 GUI 和兼容核心安装到安装根目录下的 `libexec/deskbridge/`，不会替换系统中的其他共享核心。默认安装路径下的 GUI 为：

```text
~/.local/opt/deskbridge/libexec/deskbridge/DeskBridge
```

从 Ubuntu 当前图形会话中启动 GUI，按使用 README 设置本机角色、设备名称及布局，再手动开始共享。每台电脑只运行一套共享进程。确认跨屏和客户端映射正常后，再决定登录启动方式。

备份设置时先退出桌桥，配置和 TLS 数据应一并保留，目录见[配置参考](docs/Configuration.md)。

## 验证

各端显示真实布局后，在 Mac 主页拖动任一电脑到相邻网格。每次以服务端确认后的布局为准，并使用真实鼠标验证相应边缘能够双向跨屏；再确认 Ctrl+C/V、Ctrl+空格和轻按 Shift 等已有功能。重叠、断开连通路径、过期请求、离线、保存失败或旧服务端连接下，界面应显示具体原因。

纯文件迁移回归测试可单独运行：

```bash
python3 -m unittest discover -s tools -p 'test_prepare_personal_server.py' -v
```
