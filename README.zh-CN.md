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

预览版。The Twilight Forest 4.8 的全部内容均已移植：维度与传送门、22 个生物群系与地形、每个地标及其 Boss
（娜迦、巫妖、米诺菇、九头蛇、骑士幻影、乌尔恶魂、雪人首领、冰雪女王、巨人、最终城堡）、生物、方块、物品、盔甲、
神器、拆解台与地图、旅行者装备、进度、天空与天气，以及原创的音效和结构模板。每个单元都由服务端自检（游戏内约
960 项，另有 C++ 单元测试）验证。仍可能有粗糙之处：客户端模组的截图审查尚未完成；基岩版无法实现（或实现方式不同）
的内容列于 [docs/spec/not-ported.md](docs/spec/not-ported.md)。问题请在 issues 中反馈。

| | |
|---|---|
| Minecraft | 基岩版 1.26.60 与 1.26.51 |
| LeviLamina | 26.60.x（lip 变体 `26_60`，默认）与 26.51.x（`26_51`） |
| 参照 | The Twilight Forest 4.8（分支 `1.21.1`，提交 `9e40a757`） |
| 平台 | Windows x64：BDS（`server`）与客户端（`client`） |

## 安装

在 LeviLamina BDS 或客户端实例的目录中用 [lip](https://github.com/LiteLDev/lip)（0.40 或更高版本）安装，
或在 LeviLauncher 的模组列表中安装：

```sh
lip install github.com/MiracleForest/GloamingForest          # 最新的 LeviLamina 版本线（26.60）
lip install github.com/MiracleForest/GloamingForest#26_51    # LeviLamina 26.51
```

lip 会按工作区选择服务端或客户端构建。每个发行版也为每一端和每条版本线提供一个 ZIP，
`gloamingforest-<server|client>-win-x64-ll<版本线>.zip`（例如 `gloamingforest-client-win-x64-ll26.60.zip`），
可手动解压到模组目录中 `LeviLamina` 旁边的 `GloamingForest` 文件夹。
安装了客户端模组的玩家获得完整体验；原版基岩版客户端也可以加入装有本模组的服务器（尽力兼容）。

游戏内：`/twilightforest`（别名 `/tf`、`/tffeature`）；管理员可以用 `/twilightforest config` 修改设置。

## 构建

Windows x64，需要 [xmake](https://xmake.io) 3.x 与 LLVM（clang-cl）：

```powershell
git config --global core.longpaths true
pwsh tools/build.ps1 -Target all -Tests     # bin/server/GloamingForest、bin/client/GloamingForest 与 gtest
xmake run -P . gf-tests
pwsh tools/build.ps1 -Target all -LeviLamina 26.51.7   # 较旧的受支持版本线（locks/26_51.lock）
```

贡献者与智能体请阅读 [CONTRIBUTING.md](CONTRIBUTING.md) 和 [AGENTS.md](AGENTS.md)。

## 许可

- GloamingForest 代码：[LGPL-3.0-or-later](COPYING.LESSER)（附 [GPL](COPYING)）。基础框架改编自
  [LiteLDev/Levitate](https://github.com/LiteLDev/Levitate)（LGPL-3.0-or-later）。
- 从暮色森林（LGPL-2.1-or-later）翻译的代码保留注明来源文件的署名头。
- 纹理、模型和翻译来自暮色森林，采用 [CC BY-NC-SA 4.0](assets/LICENSE)：仅限非商业用途，相同方式共享。
- 暮色森林的音效、音乐和结构模板为“保留所有权利”，**不**包含在本项目中：GloamingForest 使用原创的替代内容。

完整署名见 [NOTICE](NOTICE)。
