# DeskBridge 开发文档

桌桥将一台电脑的键盘、鼠标输入通过 TCP 发送到其他电脑，并共享剪贴板。来源端管理设备布局和切屏控制；接收端按本机系统及前台应用适配快捷键。

## 架构

```text
实体键盘／鼠标
       |
来源端平台输入 → 来源端共享核心
                         |
                   TCP / TLS，默认 24800
                         |
                接收端共享核心
                         |
             快捷键适配 → 平台输入注入
```

各端 Qt GUI 通过本机 IPC 管理核心进程和运行状态。配套版本还在共享连接上传递来源系统、布局及在线状态；来源端验证布局请求并持久化成功后发布结果，接收端显示确认后的布局。

## 源码入口

| 目录 | 内容 |
| --- | --- |
| `src/apps` | GUI、共享核心和平台程序入口 |
| `src/lib/gui/personal` | 桌桥主界面、屏幕画布、快捷键设置 |
| `src/lib/gui/dialogs` | 设备控制、证书、帮助等编辑器与窗口 |
| `src/lib/common` | 共享设置、快捷键规则及公共类型 |
| `src/lib/server`、`src/lib/client` | 来源端与接收端会话、布局协议和输入处理 |
| `src/lib/platform` | macOS、Windows、X11 和输入后端 |
| `src/lib/deskflow` | 既有共享核心抽象及协议类型，保留实际内部目录名 |
| `src/unittests` | 隔离逻辑、协议和界面回归测试 |
| `deploy` | 平台构建、许可证收集与打包脚本 |
| `tools` | 维护、配置工具及纯文件测试 |

GUI 产物显示为 `DeskBridge`；共享核心的实际名称仍为 `deskflow-core`。内部资源路径和协议兼容值不等同于用户界面产品名称。

## 继续阅读

- [构建指南](build.md)
- [贡献说明](contributing.md)
- [协议参考](protocol_reference.md)
- [设置与数据目录](../Configuration.md)
- [用户指南](../../README.md)
