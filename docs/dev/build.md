# 构建 DeskBridge

各平台脚本负责启用个人版身份、检查依赖、隔离测试和打包。使用方式及完整环境要求分别见：

- [macOS 构建与发行包](../../README-PERSONAL.md)
- [Linux 原生构建与 Flatpak](../../README-LINUX.md)
- [Windows x64 构建与验证](../../README-WINDOWS.md)

## 公共要求

需要 CMake 3.24+、支持 C++20 的编译器、Qt 6.7+ 和 OpenSSL 3.0+。Linux 后端另需 libportal 0.9.1+、libei 1.3+ 和对应 X11／键盘库。实际发行脚本可能固定更高的 Qt、OpenSSL 和工具版本，以平台文档及脚本为准。

在仓库根目录执行对应平台命令：

```bash
# macOS 本机开发
./deploy/mac/build-personal.sh

# Linux 原生构建及隔离测试
./deploy/linux/build-personal.sh --test
```

Windows 在 MSVC developer PowerShell 中执行：

```powershell
./deploy/windows/build-personal.ps1 -QtRoot 'C:/Qt/6.11.2/msvc2022_64' -VcpkgRoot 'C:/src/vcpkg'
```

GUI 产物为 macOS 的 `bin/DeskBridge.app`、Linux 的 `bin/DeskBridge` 和 Windows 的 `bin/DeskBridge.exe`。共享核心仍使用真实兼容文件名 `deskflow-core`／`deskflow-core.exe`；macOS 位于 App 的 `Contents/MacOS/`。不要按产品显示名猜测内部文件路径。

## 常用 CMake 选项

| 选项 | 用途 |
| --- | --- |
| `DESKBRIDGE_PERSONAL_BUILD=ON` | 使用 DeskBridge 名称、独立配置及运行身份 |
| `BUILD_TESTS=ON` | 构建测试目标 |
| `SKIP_BUILD_TESTS=ON` | 不在构建过程中自动运行全部测试，随后显式运行适当测试 |
| `BUILD_INSTALLER` | 是否生成通用打包目标；个人发行脚本可使用自己的打包步骤 |
| `BUILD_X11_SUPPORT` | Linux／BSD 的 X11 后端 |
| `BUILD_OSX_BUNDLE` | macOS App bundle |
| `BUILD_DEV_DOCS` | 生成 Doxygen 开发文档，需要 Doxygen |
| `ENABLE_COVERAGE` | 测试覆盖率工具支持 |
| `APPLE_CODESIGN_DEV` | macOS 本地开发签名身份，不代替发行签名或公证 |

不同平台、依赖、签名或应用身份使用独立的构建目录。示例：

```bash
cmake -S . -B build/development -G Ninja \
  -DDESKBRIDGE_PERSONAL_BUILD=ON -DBUILD_TESTS=ON -DSKIP_BUILD_TESTS=ON
cmake --build build/development
```

此示例要求依赖已被 CMake 找到，不替代平台发行流程。固定 SDK、许可证收集、动态库验证和校验清单应使用平台脚本。

## 测试与安装

使用平台脚本中的隔离测试集合。部分原生输入、剪贴板测试会操作当前桌面，不要把全量 `ctest` 用作普通无交互验证。新增测试应先判断是否会注入按键、访问剪贴板或修改用户配置。

`cmake --install` 使用配置时的 `CMAKE_INSTALL_PREFIX`；检查路径后再安装。构建和打包不应自动启动应用、修改用户设置、迁移证书或替换运行中的共享进程。

## 发行前检查

1. `VERSION` 与 GUI、共享核心和安装包报告的版本一致。
2. 运行与改动相关的隔离回归测试，并查看完整失败日志。
3. 使用平台发行脚本收集依赖与许可证，核对包内文档和安装说明。
4. 在移除开发 SDK 路径的环境中检查可运行性，再单独记录真实桌面交互验证。
5. 仅发布可重现的安装包、校验文件及验证记录，不把缓存、私钥或用户配置加入 Git。

[开发概览](mainpage.md) · [贡献说明](contributing.md) · [使用 README](../../README.md)。
