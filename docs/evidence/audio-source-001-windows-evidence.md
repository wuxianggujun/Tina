# AUDIO-SOURCE-001：Windows 音频与全功能 SDK 验证

- 日期：2026-09-12。
- 环境：Windows x64，Visual Studio 18 2026，MSVC 19.50.35728.0，CMake 4.2.3，vcpkg `x64-windows`。
- 复用常驻树：`out/build/windows-msvc-vnext-bgfx-product-2d`；没有 clean-first 或第二份引擎构建树。
- 原始日志、JSON 与哈希：`out/validation/audio-sdk-20260912/`，本机保留、不提交生成物。

## 构建与直接测试

全部源码完成后集中构建 `tina_validation_artifacts`。修复门禁发现的问题后，Debug 完整图退出码为 0；
最终 Debug/Release 的 `tina_sdk_install_artifacts` 和 `tina_editor_desktop` 发布构建均退出 0。
GoogleTest 直接运行，未调用 CTest。下表是失败修复后各 executable 的最终结果，不是所有仓库测试的结论。

| Executable | 通过 | 失败 / skip | 证据批次 |
| --- | ---: | --- | --- |
| `tina_audio_tests` | 79 | 0 / 0 | `tests-3` |
| `tina_asset_tests` | 426 | 0 / 0 | `tests-3` |
| `tina_audio_miniaudio_tests` | 6 | 0 / 0 | `tests-2` |
| `tina_asset_format_tests` | 152 | 0 / 0 | `tests-2` |
| `tina_animation3d_tests` | 42 | 0 / 0 | `tests-2` |
| `tina_editor_tests` | 146 | 0 / 0 | `tests-2` |
| `tina_editor_app_tests` | 27 | 0 / 0 | `tests-2` |

共 878 项。覆盖真实 WAV/FLAC/MP3/Ogg Vorbis/Ogg Opus 解码、PCM 预算、损坏/截断 Ogg、RAII、
mono/stereo 转换和 1000–192000 Hz 代表采样率、Music bus/Stop/自然结束、PCM stream、
recipe/direct import 一致性、Unicode 路径、Catalog/Lease 生命周期与 Editor 扩展名入口。

集中验证还修复了历史动画测试的 U32 indices / canonical skeleton signature 迁移遗漏，未回退新契约。
已有 DXBC 生成头曾为 0 字节，仅重新生成该损坏文件，未清理整个 shader/build cache。

## 已安装的全功能 SDK

`tools/windows/InstallFullSdk.ps1` 已实际发布到 `D:/ProgramData/Tina`：

- SDK **0.3.0**，`lib/Debug/Tina.lib` 与 `lib/Release/Tina.lib`。
- GLFW、bgfx、FreeType/UIA、Physics2D、Jolt Physics3D、miniaudio device、TLS/mbedTLS、Tracy；
  基础五种 codec 始终启用，完整 shader variants 已构建。
- Release `TinaEditor/TinaEditor.exe`、字体资源及 app-local DLL。
- Release `bin/tina_assetc.exe`、`tina_catalog_validate.exe`、`tina_msdfgen.exe`、`shaderc.exe`。
- `dependencies/` 含 1063 个预编译依赖文件（381,285,634 字节），同时保留 Debug/Release。

安装验证识别了同名 Debug/Release DLL 具有相同时间戳时，CMake 将 Debug DLL 误判为无需覆盖的问题。
发布脚本现按本次 Debug 安装清单移除共享 `bin/` 中的临时工具文件，再安装 Release；不删除目录或无关文件。
最终 31 个工具/Editor EXE、DLL 的 SHA-256 均与 producer Release 匹配，无 Debug 工具 DLL 残留。
两份 `Tina.lib`、357 个安装头及全部 1063 个随包依赖均与 producer 逐文件哈希一致。

两配置 archive / package 使用同一 source/config fingerprint：

```text
6fa2047185ffd3305cdf4313ed72be298be62151793cfae4e9e4ae05b1345c39
```

## 独立消费者

使用 `tests/sdk_consumer_audio_miniaudio` 和 `tests/sdk_consumer` 的 archive probe；仅提供
`CMAKE_PREFIX_PATH=D:/ProgramData/Tina`，禁用 CMake package registry，无 vcpkg toolchain。

- Debug/Release 均成功 configure、编译消费者、链接和运行；没有编译引擎或第三方库。
- 25 个 imported targets 的实际头文件和库路径均在 D 盘安装 prefix 内。
- 真实解码全部五种 fixture，安装版 `tina_cook_catalog(AUDIOS ...)` 调用随包 assetc cook 两种 Ogg。
- 两配置均安装为独立 product，在移除 SDK/producer/vcpkg PATH 提示后通过 null device callback/shutdown。
- 随包 `tina_catalog_validate` 确认两个 AudioClip 的 typed payload、ContentHash 与加载均成功。
- 两配置 archive probe 的版本、build-id 与 CMake metadata 匹配，输出配置分别为 Debug / Release。
- 安装头第三方泄漏检查、严格 exact-version 检查、package 绝对路径检查、`shaderc --version` 通过。

## 结论边界

没有启动真实 Editor GUI、真实扬声器或 sample/视觉 gate；未执行 Linux/macOS/Android/iOS 二进制验证。
音频源完整离线解码后 cook 为 PCM，不宣称压缩磁盘按需流式解码、chained/multiplexed Ogg 或新 loop API。
Windows 全功能指当前可用的产品能力，不包括 Legacy、其他平台二进制或 sanitizer。

## 资源收尾

- 常驻 producer tree：18,686,963,203 字节，保留供后续增量构建；D 盘交付目录：803,390,624 字节。
- 本轮编译、测试和 helper 进程已结束，FastCtx 无运行中 job；没有创建子 agent、container、volume 或 image。
- 六个临时工具/consumer 目录共 178,590,543 字节；递归删除被执行策略拒绝，复查后仍存在，回收字节数为 0。
  完整路径及前后体积在 `out/validation/audio-sdk-20260912/resources-after.json`，不能视为已释放。
- 共享 vcpkg cache 未删除；恢复 shader 的小型诊断副本及验证日志作为证据保留。
