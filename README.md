# foo_input_cue_charset

`foo_input_cue_charset` 是一个只读的 foobar2000 播放列表加载器和输入组件，用于处理字符编码预先未知的外部 CUE 文件。0.3.5 版本还提供两个相互独立、默认关闭的只读输入信息过滤器：一个修复 WAVE 的 UTF-8 RIFF INFO，另一个修复少数 STREAMINFO 时长损坏的 FLAC 在 foobar2000 缓存中的时长和平均码率。项目同时保留可复用的 ICU 字符集核心库及其诊断 CLI。

组件会把 `.cue` 文件作为播放列表加载。每条 CUE 轨道均表示为标准的 foobar2000 多轨位置：规范化的本地 CUE 路径，加上原始 CUE `TRACK` 编号作为 subsong。`CUE Charset Input` 通过标准多轨输入 API 接管 `.cue`，直接提供元数据和音频解码，不生成代理文件，也不使用自定义 URL scheme 或文件系统服务。

请在 Decoder Priority 中将 `CUE Charset Input` 放在内置 CUE decoder 之前。早期 0.3.0 开发版本生成的 `cuecharset://` 条目不会自动迁移；请删除这些旧条目，再重新加入对应的 CUE 文件。

## 行为

- 单个 CUE 文件最多读取 128 MiB。
- 首先使用 ICU 77 检查 Unicode signature。不存在 signature 时，组件按 ICU 给出的统计候选顺序逐一验证，采用第一个同时满足以下条件的候选：转换过程没有替换字符、文本能解析为 CUE、全部引用音频文件均存在。如果所有候选都未通过，则保留 ICU 原始首选候选，使正常的转换错误或 CUE 错误仍能明确呈现。
- 只读诊断 CLI 仍只报告 ICU 的第一候选，因为它没有 foobar2000 文件系统上下文，无法验证引用文件。
- 将完整 CUE 文件转换为 UTF-8；使用回退首选候选时，非法源序列会记录为 U+FFFD，但不会因此改选其他编码。
- 使用 foobar2000 SDK 官方 `cue_parser` 解析 UTF-8 文本。每条轨道生成一个规范 CUE 路径条目，并以真实轨号作为 subsong；引用音频区间由 `input_helper_cue` 解码。
- 通过 foobar2000 文件系统 API 解析相对 `FILE` 路径。
- 接受本地 Windows 盘符绝对路径和 UNC `FILE` 路径，并通过同一 foobar2000 文件系统 API 进行规范化。
- 对 CUE 引用的经典小端 RIFF/WAVE 文件，读取 `LIST/INFO` 字段，并以原子方式修复原生 WAVE handler 可能按本机 ANSI 代码页解释的非 ASCII UTF-8 值。支持以下映射：
  - `INAM → TITLE`
  - `IART → ARTIST`
  - `IPRD → ALBUM`
  - `ITRK → TRACKNUMBER`，缺失时回退 `IPRT`
  - `ICRD → DATE`
  - `IGNR → GENRE`
  - `ICMT → COMMENT`
  - `ISFT → ENCODER`
- 显式 CUE 元数据最后应用，因此优先级高于 RIFF INFO 修复结果。
- 可容忍 RIFF 声明边界不匹配，以及最后一个 `data` 块声明长度超过实际可用字节。若结构化遍历无法恢复 INFO，只扫描文件开头和结尾各 4 MiB，寻找完整且按 word 对齐的 `LIST/INFO` 候选。候选边界、字段大小限制、NUL 处理和 UTF-8 校验仍保持严格；不会扫描完整音频 payload。
- 在 foobar2000 标准 **Input info filters** 设置页启用 `CUE Charset RIFF INFO Filter` 后，会对直接加入的本地 `.wav` 和 `.wave` 条目应用相同修复。该过滤器不会接管或包装 WAVE decoder，不会改变解码音频，也不会拦截远程 URL 或非零 subsong。
- 在同一设置页启用 `CUE Charset FLAC Duration Repair` 后，会对直接加入的本地 `.flac` 和 CUE 引用的本地 FLAC 检查异常时长。该过滤器仅处理 subsong 0；远程地址和其他格式保持原样。
- FLAC 时长过滤器只在原生信息同时满足“声明时长大于 600 秒”和“按文件大小及声明时长计算的平均码率低于 96 kbps”时完整解码。扫描使用 foobar2000 原生 decoder 的简单解码和禁用 postprocessor flags，不启用 integrity testing，因此损坏的 STREAMINFO MD5 不会把正常可播放文件误判为扫描失败。
- 解码采样数与声明时长相差超过 1 秒时，过滤器只覆盖当前 `file_info` 的时长并重新计算平均 `BITRATE`。成功修正输出一条 warning，包含路径、声明时长、实际时长和修正码率；采样率变化、空输出、溢出、无效文件统计或解码失败则保留原生信息并输出 warning。用户取消操作会原样传播且不记为错误。
- 已修正结果按规范路径、文件大小和修改时间保存在进程内缓存中。文件统计变化会自动失效；直接 FLAC 和 CUE 引用共享同一结果，同一文件不会重复完整解码。缓存不写入磁盘，失败结果不缓存。
- 以下情况保持原生元数据不变：仅含 ASCII 的 INFO、无效或混合编码、无法恢复的损坏块、不可 seek 的流、RF64、RIFX、WAVE64、未知 INFO 字段，以及非 WAVE 引用音频。
- RIFF INFO 修复失败不会阻止加载或播放。无效修复候选和可恢复读取错误会在每次受影响操作中记录一次 warning；成功修复保持静默。
- 不解析或重新解释 AIFF 文本块、ID3v1/ID3v2、APEv2、FLAC/Vorbis Comment、MP4、TAK 或 TTA 元数据；这些格式继续使用其原生 foobar2000 decoder 返回的元数据。
- 保留 SDK 对 `BINARY` 文件的 CDDA PCM 处理方式。
- 以只读技术字段暴露来源位置：`$info(CUE_SOURCE_PATH)` 和 `$info(REFERENCED_FILE)`。
- 根据每个 metadb handle 的 CUE 路径和 subsong 提供封面回退。首先转发引用音频的内嵌封面；Front Cover 缺失时，依次在引用音频目录和 CUE 目录查找 `cover.*`、`folder.*`、`front.*`。多选时，仅当全部所选轨道引用同一音频文件才返回共同封面。
- 所有标签写入操作均抛出 `exception_tagging_unsupported`。
- ICU、检测、转换、路径和 CUE 解析失败会直接报告，不会静默回退到内置 CUE handler。
- foobar2000 Console 仅输出 warning/error。成功的加载、检测、转换、轨道枚举、解码、seek、封面查询和元数据修复均不输出追踪；用户主动取消操作同样保持静默。

自定义 playlist loader 与内置 loader 都会声明 `.cue`，但 foobar2000 未提供 loader 优先级设置。目标 foobar2000 2.26 环境已通过 Console 诊断确认会调用本组件的 loader。Decoder Priority 仍负责决定由哪个 `.cue` input decoder 打开生成的原生 CUE/subsong 条目。

## 主机验证状态

原生 CUE/subsong 架构已在 2026-08-28 的 foobar2000 2.26 x64 preview build 上验证：

- 自定义 playlist loader 能将 UTF-8、GB18030、多文件和 pregap fixture 展开为 8 个规范 `.cue` 条目，并保留真实轨号 subsong。
- `CUE Charset Input` 已启用并位于 Decoder Priority 顶部。
- 播放会进入自定义 decoder；正常 open、解码、轨道结束和 seek 均保持 Console 静默。
- 连续播放可以跨越 CUE subsong 和引用文件；暂停/继续与 seek 正常；重启 foobar2000 后，8 个原生条目仍然有效。
- GB18030 条目在重启后仍可播放。
- 无封面状态和目录 `cover.png` 的 Front Cover 回退均正常。

此前出现过一次没有附加信息的 `Unrecoverable playback error`，原因是 foobar2000 未选择可用输出设备，并非 playlist loader 或 decoder 预检失败。组件不需要 redirect handler、虚拟文件系统、代理文件或隐式 hook。

组件没有自定义设置页，也不会修改 CUE、引用音频、标签、播放列表或媒体库设置。两个修复均采用标准 foobar2000 input-info filter，因此新安装后默认不启用；需要用户在 **Input info filters** 页面分别手动启用 `CUE Charset RIFF INFO Filter` 或 `CUE Charset FLAC Duration Repair`。

启用 FLAC 过滤器后，foobar2000 已缓存的旧时长不会自动刷新。请对相关条目执行 **Reload info from file(s)**。修复只存在于 foobar2000 的信息缓存：FLAC 内部错误的 `total_samples` 和 MD5 不会改变，`flac -t` 仍会报告原始 MD5 mismatch。

## 环境要求

- Windows x64 和 foobar2000 2.x x64
- CMake 3.24 或更高版本，以及 Ninja
- MSVC v143 x64 14.44 工具集及对应 ATL
- Windows SDK 10.0.26100.0
- foobar2000 SDK：`D:/Foobar2000/sdk`
- WTL 10.01：`deps/WTL10_01_Release`
- ICU 77：`ICU_ROOT`，默认 `E:/MSYS/clang64`

WTL 仅作为 header-only 构建依赖使用，采用 Microsoft Public License；其头文件不会复制进组件包。

字符集核心使用 ICU headers 编译，但不会链接 MinGW `.dll.a` import library。运行时从 `ICU_ROOT/bin` 通过绝对路径加载以下文件：

- `libc++.dll`
- `libicudt77.dll`
- `libicuuc77.dll`
- `libicuin77.dll`

ICU 是固定的本机运行时依赖；这些 DLL 不会被复制、安装或打包。

## 构建与测试

在 MSVC x64 环境中运行以下标准命令。`PATH`、`INCLUDE` 和 `LIB` 必须包含 v143 14.44 compiler 与 Windows SDK 工具：

```console
cmake -S D:/Foobar2000/foo_input_cue_charset -B D:/Foobar2000/foo_input_cue_charset/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER="D:/Microsoft Visual Studio/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/cl.exe" -DICU_ROOT=E:/MSYS/clang64
cmake --build D:/Foobar2000/foo_input_cue_charset/build
ctest --test-dir D:/Foobar2000/foo_input_cue_charset/build --output-on-failure
```

项目不提供 CMake Preset 或 wrapper build script。构建过程会使用 v143 调用官方 `pfc`、SDK、component-client 和 helpers MSBuild 工程，然后生成：

```text
build/dist/foo_input_cue_charset-0.3.5-x64.fb2k-component
build/dist/SHA256SUMS
```

组件包根目录只包含 `foo_input_cue_charset.dll`。

## 诊断 CLI

```console
cue-charset.exe "D:/Music/album.cue"
```

CLI 每次只接受一个 `.cue` 路径。成功时返回 `0`，并输出英文键值诊断，但不会输出转换后的 CUE 正文：

```text
path=D:/Music/album.cue
bytes=1234
unicode_signature=none
encoding=GB18030
confidence=100
conversion=success
replacements=0
first_replacement_offset=N/A
```

由 Unicode signature 确定编码时，置信度显示 `N/A (signature)`。用法、文件、ICU、检测或转换错误均返回 `1`。组件和 CLI 都不提供系统代码页回退。

## 测试 fixture

`tests/fixtures` 包含真实的 GB18030 CUE、故意损坏的 GB18030 fixture、一个被 ICU 错误地将 Big5 排在 GB18030 之前的样本、规范 UTF-8 文本和 SHA-256 清单。损坏 fixture 使用显式指定的 GB18030 converter 进行转换，避免统计检测对损坏数据的不同判断造成测试不稳定。

核心测试还会合成以下 RIFF/WAVE INFO 情形：结构正常、`data` 截断、受限的文件头/文件尾恢复、块结构损坏；同时覆盖具有代表性的 RF64、RIFX、WAVE64、AIFF、FLAC、APE/APEv2、WavPack、ID3、MP4、TAK 和 TTA signature。这些回归测试确保 UTF-8 overlay 始终只作用于经典小端 RIFF/WAVE，而不会演变成通用标签 decoder。

FLAC 时长核心测试覆盖 600 秒和 96 kbps 的严格边界、无效数值、无需扫描和需要扫描的策略、1 秒修正阈值、采样数溢出、采样率变化、空输出，以及缓存命中和文件统计变化失效。已知损坏样本的回归数据为 6,206,976 samples / 44,100 Hz，即约 140.748 秒和 543.8 kbps。
