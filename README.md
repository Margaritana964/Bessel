# Bessel — 相位 / 频率调制载波效果器

[![License: AGPL v3](https://img.shields.io/badge/License-AGPL_v3-blue.svg)](LICENSE)
[![Release](https://img.shields.io/github/v/release/Margaritana964/Bessel?label=download&color=2ea44f)](https://github.com/Margaritana964/Bessel/releases/latest)
[![Platform](https://img.shields.io/badge/platform-Windows%20x64-lightgrey)](#系统要求)

> **A VST3 effect that turns any audio into a harmonic generator.**
> Your material drives the phase/frequency of an internal carrier through true PM/FM,
> while its own volume envelope gates the output. No input, no sound. No keyboard required.

外部音频进来，**分成两路**：一路滤波后当调制源去推内部载波的相位或频率，
一路取音量包络做 AM。所以没有输入时它完全安静、输入多大输出就多大——
本质是一台**用素材驱动的谐波发生器**。

![Bessel](ui-preview.png)

**⬇️ [下载 Windows x64 版](https://github.com/Margaritana964/Bessel/releases/latest)** · 免安装，解压即用

## 它能做什么

| | |
|---|---|
| **真 PM + 真 FM，可同时开** | PM 不动音高（干净重合成），FM 动音高（撕裂、下潜）。两个量独立，等于给调制源加 1/f 倾斜 |
| **4× 过采样** | 深 PM 的贝塞尔边带会折到 Nyquist 以上。实测温和深度下 4× 比 1× 干净 **57 dB** |
| **真立体声** | 左右声道的滤波器与相位累加器完全独立，不是复制一份 |
| **带限波表载波** | 正弦 → 三角 → 带限锯齿**连续形变**；按 Nyquist 自动降谐波数，深 PM 不爆裂 |
| **0 Hz = Wavefolder** | 载波拧到 DC，输出变成 `wave(调制信号)`，一台素材驱动的波形整形器 |
| **MIDI 追踪梳状滤波** | 把 PM 炸出的宽频噪波对齐到谐波列：正反馈全谐波，负反馈只剩奇次 |
| **调制矩阵** | LFO（可同步 BPM）+ 输入包络 → 音高 / 滤波截止 / LFO 速率 |
| **干净 sub + 软削波** | 补回被 `J₀` 抽空的基频，再把上层纹理焊死在 sub 周期上 |
| **7 个工厂预设 + 预设浏览器** | 文件在 `%APPDATA%\Bessel\Presets`，Factory / User 分离 |

![Presets](ui-preview-presets.png)

## 安装

1. 下载 [`Bessel-v*-win64.zip`](https://github.com/Margaritana964/Bessel/releases/latest)
2. 解压，把整个 **`Bessel.vst3` 文件夹**（不是里面的 dll）拷到：

```
%LOCALAPPDATA%\Programs\Common\VST3\
```

3. 在宿主里重扫插件（REAPER：Preferences → Plug-ins → Re-scan；Bitwig：Preferences → Plug-ins → Rescan）

包是**静态链接 CRT** 的，不需要装 VC++ 运行库。没有代码签名，首次加载 Windows 可能弹 SmartScreen，选「更多信息 → 仍要运行」。

## 十分钟上手

1. 素材拖进轨道，Bessel 插上，`Mix 100%`
2. PM 和 FM 先都归零——只听载波。它应该和素材**同音量起落**，但没有素材的音色
3. 推 `PM` 到 1–2 圈：谐波长出来了，**音高不动**
4. 扫 `MOD FREQ`：从 200 Hz 往上，能听到「共振腔」在变
5. 音高来源 `Pitch Source`：`FREE` 锁旋钮 / `FOLLOW` 跟素材音高 / `MIDI` 听音符

三个立刻能听出来的点：

- **载波拧到 0**（显示 `DC / Folder`）→ 变成 Wavefolder
- **Shape 从 Sine 拧到 Saw** → 连续形变，中间没有档位
- **Comb FB 正反馈 / 负反馈** → 全谐波（金属管）vs 只剩奇次（空心方波）

## 预设

点界面右上角的预设名或 `LIST` 打开浏览器。`SAVE` 覆盖当前用户预设，`SAVE AS` 新建，`RENAME` 改名，`DELETE` 进回收站。

```
%APPDATA%\Bessel\Presets\
  Factory\   随插件版本重写
  User\      你自己存的，插件永远不碰
```

插件升级不动 `User\`；卸载插件也不会删预设。

## 从源码构建

需要 CMake ≥ 3.22 + Visual Studio 2022/2026（C++ 桌面开发）。JUCE 由 CMake 自动拉取。

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

产物在 `build/Bessel_artefacts/Release/VST3/Bessel.vst3`。

跑测试（提交前必须全绿）：

```powershell
build\PmEffectTest_artefacts\Release\PmEffectTest.exe      # DSP：23 组
build\PluginScanTest_artefacts\Release\PluginScanTest.exe  # 宿主视角
build\UiSnapshot_artefacts\Release\UiSnapshot.exe          # 出界面快照
```

## 文档

| | |
|---|---|
| [技术内幕](docs/engineering.md) | 信号流、每条参数的物理意义、实测数据、所有踩过的坑 |
| [参与开发](CONTRIBUTING.md) | 构建、测试、提交约定 |
| [介绍视频口播稿](docs/video-script.md) | 3 分钟版 + 60 秒版 + 拍摄清单 |

## 系统要求

Windows x64（VST3）。没有 macOS 版本。固定 4× 过采样，延迟 60 采样（约 1.25 ms，已上报宿主）。

**已知限制**：音高跟随范围 50–1200 Hz（低于 50 Hz、噪声、复音素材会跟丢）；弯音固定 ±12 半音。

## 许可与致谢

以 **AGPL-3.0** 发布（见 [LICENSE](LICENSE)）。依赖两套同样是双授权的框架：

- **JUCE 9** — AGPLv3 / 商业授权
- **VST3 SDK** — GPLv3 / Steinberg 授权

本项目两处都走自由分支。要在闭源或商业产品里用这份代码，请自行取得商业授权。
