# DeskBridge Linux 服务端

Mac 客户端的主页拖放布局需要来源端也运行配套 DeskBridge。旧 Ubuntu Deskflow 可以继续共享键鼠，但不会提供远程布局能力。本文件准备独立的 Linux 服务端构建与配置。

2026-10-20 已在 Ubuntu 22.04.5 的 KDE 6.11 Flatpak SDK 中完成构建，8 个隔离 CTest suite 和 11 项配置迁移测试通过。独立应用 `local.shuai.deskbridge` 已安装并接管服务端，与 Mac 的加密连接正常；主页四方向拖放与服务端配置逐项核对通过，最后恢复 Mac 在来源电脑右侧，并迁移原本启用的登录启动设置。

## Ubuntu 22.04 与 Flatpak

这台 Ubuntu 的原服务端是 `org.deskflow.deskflow` Flatpak，使用 KDE Platform 6.11。系统软件源的 CMake 3.22、Qt 6.2 和 libportal 0.6 低于源码最低要求，因此本轮使用用户级 KDE SDK 6.11 构建独立 Flatpak，未升级系统软件包。构建环境为 GCC 15.2、Qt 6.11.2、OpenSSL 3.5.7，附加依赖按仓库 Flatpak manifest 的固定 commit 构建。

新应用 ID 为 `local.shuai.deskbridge`，保持原 Deskflow 安装及其数据目录。原配置位于 `~/.var/app/org.deskflow.deskflow/config/Deskflow`；新配置位于 `~/.var/app/local.shuai.deskbridge/config/DeskBridge`。迁移时应显式传入这两个目录。

已构建的本地 bundle 可通过 `flatpak install --user /path/to/DeskBridge-screen-layout.flatpak` 安装。完成停用旧服务端和配置检查后，通过 `flatpak run local.shuai.deskbridge` 启动。两端的登录启动设置应与实际使用的应用保持一致。

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

`--test` 运行隔离的布局、配置事务、传输和客户端协议测试，以及服务端配置迁移测试。默认输出为 `build/personal-linux/bin/DeskBridge` 和 `build/personal-linux/bin/deskflow-core`。可省略 `--test` 只构建，或指定构建目录和额外 CMake 参数：

```bash
./deploy/linux/build-personal.sh --test build/personal-linux -DQt6_DIR=/path/to/Qt/lib/cmake/Qt6
```

## 准备已有服务端身份

迁移工具默认只显示计划，不写文件。默认读取 `$XDG_CONFIG_HOME/Deskflow`，未设置该变量时读取 `~/.config/Deskflow`；默认目标为同一配置根目录下的 `DeskBridge`：

```bash
python3 tools/prepare_personal_server.py
```

如旧服务端使用 `/etc/Deskflow`、Flatpak 或自定义配置目录，显式传入实际目录：

```bash
python3 tools/prepare_personal_server.py --source /actual/Deskflow --destination /actual/DeskBridge
```

确认计划后，使用同样的参数并加 `--apply`。工具要求源配置确实为服务端、已开启 TLS 和指纹校验，且身份、客户端信任库、生成的服务端配置都存在。它不覆盖任何现有目标，发布阶段使用 Linux `renameat2(RENAME_NOREPLACE)` 拒绝并发创建的目录或符号链接；平台不支持时会退出并清理临时目录。

```bash
python3 tools/prepare_personal_server.py --apply
```

新配置保留原屏幕名称、布局网格、热键、未知设置、证书和所有 TLS 信任文件，并复制原服务端配置。除下列必要键值外，原配置文本保持不变：`gui/startCoreWithGui` 设为 `false`；证书和日志路径指向新目录；使用外部配置时，其路径指向新目录中的副本。配置目录权限为 `700`，证书、信任库和配置文件为 `600`。源配置不会修改，工具不会启动程序或修改登录启动项。

新版双方支持在首页排列三台及以上已配置电脑，并同步完整连通网格及各设备在线状态。新增设备仍使用来源端完整屏幕布局编辑器；数量受来源端配置的网格容量限制（默认 5 列 × 3 行）。外部配置、部分边缘映射或无法还原为完整相邻网格的自定义链接会显示只读原因，避免覆盖复杂配置。

## 独立安装与切换

构建脚本默认配置的安装根目录为 `~/.local/opt/deskbridge`；可在构建前通过 `DESKBRIDGE_INSTALL_PREFIX` 指定其他目录。安装是独立的一步：

```bash
cmake --install build/personal-linux
```

个人版将 GUI 和兼容核心安装到安装根目录下的 `libexec/deskbridge/`，不会替换系统 `/usr/bin/deskflow-core`。默认安装路径下的 GUI 为：

```text
~/.local/opt/deskbridge/libexec/deskbridge/DeskBridge
```

切换前保存并退出旧服务端的 GUI 和共享核心，再从 Ubuntu 当前图形会话中启动新 GUI。检查角色仍为服务端、屏幕名称及排列与迁移前一致，再手动开始共享；新配置的自动共享初始关闭。仅在确认跨屏和客户端映射正常后，再决定登录启动方式。

保持旧 Deskflow 安装和原配置以便回退。回退时退出 DeskBridge，再启动原 Deskflow。两套服务端不应同时占用原有共享端口。

## 验证

各端显示真实布局后，在 Mac 主页拖动任一电脑到相邻网格。每次以服务端确认后的布局为准，并使用真实鼠标验证相应边缘能够双向跨屏；再确认 Ctrl+C/V、Ctrl+空格和轻按 Shift 等已有功能。重叠、断开连通路径、过期请求、离线、保存失败或旧服务端连接下，界面应显示具体原因。

纯文件迁移回归测试可单独运行：

```bash
python3 -m unittest discover -s tools -p 'test_prepare_personal_server.py' -v
```
