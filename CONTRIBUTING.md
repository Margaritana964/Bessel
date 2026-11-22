# 参与开发

谢谢你看这个项目。下面是能让你在十分钟内跑起来、并且不给别人添麻烦的最短路径。

## 环境

| | 版本 |
|---|---|
| CMake | ≥ 3.22 |
| Visual Studio | 2022 / 2026 Build Tools（要有 C++ 桌面开发） |
| JUCE | 9.0.5 ——**不用手动准备**，CMake 会自己拉 |

## 构建

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

本地如果已经有 JUCE（放在同级目录 `../JUCE`），CMake 会直接用它，跳过下载。

产物在 `build/Bessel_artefacts/Release/VST3/Bessel.vst3`。
拷到 `%LOCALAPPDATA%\Programs\Common\VST3\` 就能在宿主里加载。

## 测试（提交前必须全绿）

```powershell
build\PmEffectTest_artefacts\Release\PmEffectTest.exe     # DSP：23 组，含频率/混叠/调制/预设
build\PluginScanTest_artefacts\Release\PluginScanTest.exe # 宿主视角：扫描/实例化/参数数/编辑器
```

界面改了就顺手出图，一眼能看出有没有重叠或裁切：

```powershell
build\UiSnapshot_artefacts\Release\UiSnapshot.exe
```

会生成 `ui-snapshot.png` 和 `ui-snapshot-presets.png`。

## 改代码前请先读

`README.md` 里的「五条铁律」。一句话版：

1. `processBlock` 里不许分配内存、不许加锁
2. 参数指针构造时缓存，`processBlock` 里只 `load()`
3. 平滑值每**采样**推进一次，不能在声道循环里推进
4. `tan()` 之类放块边界算
5. `PLUGIN_CODE` 一旦发出去就不能改

## 提交

- 一个提交做一件事；说明写**为什么**，不是写了什么
- 改了 DSP 就补测试；改了界面就贴 `ui-snapshot.png`
- 不要提交 `build/`、`build-static/`、`dist/`、`test-material/`、`test-presets/`

## 报告问题

带上这些信息，能省掉一半来回：

- 宿主 + 版本（REAPER / Bitwig / Cubase / FL …）
- 输入是什么（人声？鼓？合成器？多响？）
- 哪些参数非默认
- 期望听到什么 / 实际听到什么

## 许可证

提交代码即表示同意以 **AGPL-3.0** 授权（见 `LICENSE`）。
如果你不同意，请不要提交。
