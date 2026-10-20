# 桌桥 DeskBridge

一套键盘和鼠标，在 macOS、Windows 和 Linux 电脑之间切换。基于 [Deskflow](https://github.com/deskflow/deskflow) 开发，使用独立的应用与配置目录。

## 下载与安装

从 [Releases](https://github.com/shuaige619/DeskBridge/releases) 下载对应系统与架构的安装包。此仓库为私有仓库，下载时需要登录有访问权限的 GitHub 账号。

| 设备 | 安装包 | 说明 |
| --- | --- | --- |
| Apple Silicon Mac | `macos-arm64.dmg` 或 `.zip` | 打开 DMG，将 DeskBridge 拖入 Applications；最低系统版本见发布说明 |
| Windows 11 x64 | `windows-x64-setup.exe` | 当前用户安装；另提供便携 ZIP |
| Linux x86_64 | `linux-x86_64.flatpak` | 使用 Flatpak 安装；已在 Ubuntu 22.04 X11 验证 |

Intel Mac、Windows ARM64 和 Linux ARM64 暂未提供经过验证的二进制包。Mac 与 Windows 包暂未使用商业代码签名或 Apple 公证。

Linux 安装示例（文件名中的版本以所下载版本为准）：

```bash
flatpak remote-add --user --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
flatpak install --user ./DeskBridge-1.27.0.1-linux-x86_64.flatpak
flatpak run local.shuai.deskbridge
```

应用需要 KDE Platform 6.11 运行时；Flatpak 会根据环境提示安装。离线机器需要预先准备运行时。

## 使用

1. 在连接物理键鼠的电脑选择“服务端／键鼠来源”，启动共享。
2. 在其他电脑选择“客户端”，填写来源电脑地址并连接，核对并信任对端证书。
3. 在来源端完整布局编辑器中添加设备。首页支持拖动三台及以上已配置设备，保存后同步实际跨屏布局。默认网格为 5 列 × 3 行，所有电脑须保持上下左右相邻连通。
4. 在接收端设置快捷键映射。Mac 可将远程 Ctrl 组合适配为 Command，或启用轻按 Shift 切换输入法。

布局画布提供设备选择、缩放、平移、适合窗口和键盘操作，显示来源、本机及在线状态。布局同步需要双方运行配套新版 DeskBridge；旧 Deskflow 仍可共享键鼠。

## 功能与构建文档

- [个人版功能、配置与 macOS 构建](README-PERSONAL.md)
- [Linux 构建与旧配置迁移](README-LINUX.md)
- [Windows 构建与验收范围](README-WINDOWS.md)
- [版本记录](CHANGELOG.md)

源码归档保留 `VERSION`，从 Git 或归档构建会得到相同的个人版版本号。安装包通过 Releases 分发，不放入 Git 历史。安装包不包含用户配置、私钥、信任记录或连接日志；升级继续使用本机已有配置。

## 许可证与来源

本项目保留 Deskflow、Synergy 及其他贡献者的版权与许可声明，遵循 [GPL-2.0](LICENSE) 及 [OpenSSL 链接例外](LICENSES/LicenseRef-OpenSSL-Exception.txt)。第三方依赖保留各自许可证。DeskBridge 是基于 Deskflow 的独立版本，与 Deskflow 官方发布分开维护。
