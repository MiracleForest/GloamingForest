# GloamingForest

[English](README.md) | 简体中文

GloamingForest 为 Minecraft 基岩版带来薄暮森林：一个由幽暗林地、地标、Boss 及其战利品组成的魔法维度，
以 [LeviLamina](https://github.com/LiteLDev/LeviLamina) 原生模组的形式编写。
同一套代码同时构建服务端模组（基岩版专用服务器 BDS）和客户端模组（通过 LeviLauncher 运行的本地世界），
目标是完整的体验：所有特性、交互和界面。

> GloamingForest 是非官方的同人项目，与 Mojang Studios 和 Microsoft 没有任何关联，也未获得其认可；
> “Minecraft” 是 Mojang Studios / Microsoft 的商标。第三方声明见 [NOTICE](NOTICE)。遇到问题请在本仓库反馈。

## 状态

预览版。全部内容均已完成：维度与传送门、22 个生物群系与地形、每个地标及其 Boss
（娜迦、巫妖、米诺菇、九头蛇、骑士幻影、乌尔恶魂、雪人首领、冰雪女王、巨人、最终城堡）、生物、方块、物品、盔甲、
神器、拆解台与地图、旅行者装备、进度、天空与天气，以及原创的音效和结构模板。每个单元都由服务端自检（游戏内约
960 项，另有 C++ 单元测试）验证。仍可能有粗糙之处：客户端模组的截图审查尚未完成；基岩版无法实现（或实现方式不同）
的内容列于 [docs/spec/not-ported.md](docs/spec/not-ported.md)。问题请在 issues 中反馈。

| | |
|---|---|
| Minecraft | 基岩版 1.26.51 |
| LeviLamina | 26.51.x（lip 变体 `26_51`，默认） |
| 参照 | [NOTICE](NOTICE) 中锁定的 Java 源码（版本 4.8，提交 `9e40a757`） |
| 平台 | Windows x64：BDS（`server`）与客户端（`client`） |

## 安装

在 LeviLamina BDS 或客户端实例的目录中用 [lip](https://github.com/LiteLDev/lip) 0.40 或更高版本安装，
或在 LeviLauncher 的模组列表中安装：

```sh
lip install github.com/MiracleForest/GloamingForest          # 最新的受支持 LeviLamina 版本线
lip install github.com/MiracleForest/GloamingForest#26_51    # LeviLamina 26.51（Minecraft 1.26.51）
```

lip 会按工作区选择服务端或客户端构建。手动安装：每个发行版为每一端和每条 LeviLamina 版本线提供一个 ZIP，
`gloamingforest-<server|client>-win-x64-ll<版本线>.zip`（例如 `gloamingforest-server-win-x64-ll26.51.zip`），
把其中的文件放进 LeviLamina 26.51 BDS 的 `plugins/` 或 LeviLamina 客户端实例的 `mods/` 中的 `GloamingForest` 文件夹。
安装了客户端模组的玩家获得完整体验；原版基岩版客户端也可以加入装有本模组的服务器（尽力兼容）。

游戏内：`/gloamingforest`；管理员可以用 `/gloamingforest config` 修改设置。

## 构建

Windows x64，需要 [xmake](https://xmake.io) 3.x 与 LLVM（clang-cl）：

```powershell
git config --global core.longpaths true
pwsh tools/build.ps1 -Target all -Tests     # bin/server/GloamingForest、bin/client/GloamingForest 与 gtest
xmake run -P . gf-tests
```

`-LeviLamina <版本>` 指定构建所用的 LeviLamina 版本（默认 26.51.7）；每条受支持的版本线的包锁文件在
`locks/<变体>.lock`。

贡献者与智能体请阅读 [CONTRIBUTING.md](CONTRIBUTING.md) 和 [AGENTS.md](AGENTS.md)。

## 许可

- GloamingForest 代码：[LGPL-3.0-or-later](COPYING.LESSER)（附 [GPL](COPYING)）。基础框架改编自
  [LiteLDev/Levitate](https://github.com/LiteLDev/Levitate)（LGPL-3.0-or-later）。
- 从上游 Java 源码（LGPL-2.1-or-later）翻译的代码保留注明来源文件的署名头。
- 纹理、模型和翻译来自上游项目，采用 [CC BY-NC-SA 4.0](assets/LICENSE)：仅限非商业用途，相同方式共享。
- 上游的音效、音乐和结构模板为“保留所有权利”，**不**包含在本项目中：GloamingForest 使用原创的替代内容。

第三方声明与完整署名见 [NOTICE](NOTICE)。
