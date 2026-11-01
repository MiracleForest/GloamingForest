# GloamingForest

[English](README.md) | 简体中文

GloamingForest 是 [暮色森林（The Twilight Forest）](https://github.com/TeamTwilight/twilightforest) 4.8 的
**非官方** Minecraft 基岩版移植，以 [LeviLamina](https://github.com/LiteLDev/LeviLamina) 原生模组的形式编写。
同一套代码同时构建服务端模组（基岩版专用服务器 BDS）和客户端模组（通过 LeviLauncher 运行的本地世界），
目标是与暮色森林完全一致：所有特性、交互和界面。

> GloamingForest 与 TeamTwilight、Mojang Studios 和 Microsoft 没有任何关联，也未获得其认可或支持。
> “The Twilight Forest” 是 TeamTwilight 模组的名称；“Minecraft” 是 Mojang Studios / Microsoft 的商标。
> 遇到问题请在本仓库反馈，不要报告给 TeamTwilight。

## 状态

早期开发中。基础框架、构建、契约头文件和测试工具已经就绪；维度、内容和客户端界面正在编写中
（见 [CHANGELOG.md](CHANGELOG.md)）。

| | |
|---|---|
| Minecraft | 基岩版 1.26.51 |
| LeviLamina | 26.51.7 |
| 参照 | The Twilight Forest 4.8（分支 `1.21.1`，提交 `9e40a757`） |
| 平台 | Windows x64：BDS（`server`）与客户端（`client`） |

## 安装

从 Releases 下载 `gloamingforest-server-windows-x64.zip` 或 `gloamingforest-client-windows-x64.zip`，
把其中的 `GloamingForest` 文件夹解压到 LeviLamina BDS 的 `plugins/`，或 LeviLamina 客户端实例的 `mods/`。
安装了客户端模组的玩家获得完整体验；原版基岩版客户端也可以加入装有本模组的服务器（尽力兼容）。

游戏内：`/twilightforest`（别名 `/tf`、`/tffeature`）；管理员可以用 `/twilightforest config` 修改设置。

## 构建

Windows x64，需要 [xmake](https://xmake.io) 3.x 与 LLVM（clang-cl）：

```powershell
git config --global core.longpaths true
pwsh tools/build.ps1 -Target all -Tests     # bin/server/GloamingForest、bin/client/GloamingForest 与 gtest
xmake run -P . gf-tests
```

贡献者与智能体请阅读 [CONTRIBUTING.md](CONTRIBUTING.md) 和 [AGENTS.md](AGENTS.md)。

## 许可

- GloamingForest 代码：[LGPL-3.0-or-later](COPYING.LESSER)（附 [GPL](COPYING)）。基础框架改编自
  [LiteLDev/Levitate](https://github.com/LiteLDev/Levitate)（LGPL-3.0-or-later）。
- 从暮色森林（LGPL-2.1-or-later）翻译的代码保留注明来源文件的署名头。
- 纹理、模型和翻译来自暮色森林，采用 [CC BY-NC-SA 4.0](assets/LICENSE)：仅限非商业用途，相同方式共享。
- 暮色森林的音效、音乐和结构模板为“保留所有权利”，**不**包含在本项目中：GloamingForest 使用原创的替代内容。

完整署名见 [NOTICE](NOTICE)。
