# 阶段 2：重构三大核心模块

## 目标与阶段隔离

阶段 2 在不覆盖早期实现的前提下，重构 Player、Recorder 和
Transcoder。阶段选择同时存在于 CMake 和 C 预处理宏中：

```bash
cmake -S . -B build-m2 \
    -DMEDIA_FRAMEWORK_STAGE=2 \
    -DBUILD_TESTS=ON \
    -DCMAKE_BUILD_TYPE=Debug
cmake --build build-m2 -j$(nproc)
ctest --test-dir build-m2 --output-on-failure
```

CMake 的源码选择规则：

- `MEDIA_FRAMEWORK_STAGE=0/1`：使用 `src/modules/`。
- `MEDIA_FRAMEWORK_STAGE>=2`：使用 `src/stage2/`。
- M2 仍会构建 M1 的基础机制练习，便于回归。

C 代码使用 `include/learning_stage.h` 中的常量：

```c
#if MEDIA_FRAMEWORK_STAGE_AT_LEAST(MEDIA_FRAMEWORK_STAGE_M2)
/* Stage 2 implementation */
#endif
```

因此，将 CMake 参数改回 `0` 或 `1` 即可重新构建原始三模块，不需要
修改源码或撤销提交。

## Player 重构

实现：`src/stage2/player.c`

主要变化：

- 使用 `playbin` 管理解复用、解码和输出。
- 使用 `GstDiscoverer` 在播放前获取时长、可定位性和音视频轨道信息。
- Bus 使用同步处理器，EOS、ERROR 和状态回调不依赖应用运行默认
  `GMainLoop`。
- 内部位置线程按 `position_update_interval` 触发 `on_position`。
- 音量、静音可以在打开媒体前设置。
- 支持 seek、倍速、VideoOverlay 窗口和渲染区域。
- `player_get_frame` 使用 playbin 的 `convert-sample` 动作获取 RGB 帧。

管线概念：

```text
URI -> playbin -> decode -> video sink
                  └──────-> audio sink
```

回调可能运行在 GStreamer 的流线程或状态切换线程中。回调实现应快速
返回，不应在回调中销毁当前对象或执行耗时阻塞操作。

## Recorder 重构

实现：`src/stage2/recorder.c`

视频分支：

```text
V4L2/Test -> videoconvert -> capsfilter -> encoder
          -> h264parse/h265parse -> queue -> muxer
```

音频分支：

```text
ALSA/Pulse/Test -> audioconvert -> audioresample -> capsfilter
                -> encoder -> parser -> queue -> muxer
```

主要变化：

- 编码数据进入 MP4 前显式经过 `h264parse`、`h265parse` 或
  `aacparse`，避免协商失败和不可播放文件。
- 每个音视频分支使用独立 Queue，隔离编码速度和复用器背压。
- 停止时发送 EOS，并通过条件变量等待复用器完成文件尾写入。
- 每 100ms 触发录制进度回调，并执行 `max_duration`、`max_size` 限制。
- 硬件编码器插件工厂不存在时回退到 x264/x265 软件编码器。
- 测试源路径不依赖摄像头或声卡，可用于自动化集成测试。

当前 M2 仅实现 V4L2、ALSA、PulseAudio 和测试源。头文件中预留的
RTSP/File Source 将在网络阶段实现。

## Transcoder 重构

实现：`src/stage2/transcoder.c`

阶段 2 明确区分两种路径。

重新编码：

```text
uridecodebin -> raw video -> convert -> encoder -> parser -> muxer
uridecodebin -> raw audio -> convert/resample -> encoder -> parser -> muxer
```

无重新编码的封装复制：

```text
filesrc -> parsebin -> encoded video -> queue -> muxer
filesrc -> parsebin -> encoded audio -> queue -> muxer
```

主要变化：

- `video_transcode=0` 或 `audio_transcode=0` 时不再错误地使用解码后的
  raw data，而是从 `parsebin` 取得编码流。
- 支持视频和音频分别选择转码或复制。混合模式会分别读取输入文件，
  以保持实现清晰；这是教学实现的性能取舍。
- `start_time/end_time` 使用带 stop position 的 flushing seek。
- EOS 和 ERROR 通过同步 Bus 处理；主动停止会等待 EOS，确保输出
  容器完成收尾。
- `transcoder_get_progress` 查询 position/duration，计算百分比、速度、
  处理字节估算和已用时间。
- 内部进度线程每 200ms 更新进度并触发 `on_progress`。

M2 转码实现面向本地文件。网络 URI、字幕、多音轨选择、精确时间戳
重写和硬件转码将在后续阶段扩展。

## 自动化验收

集成测试：`tests/stage2/test_stage2_core.c`

测试会在 `/tmp` 中生成约 1.5 秒的 H.264/AAC MP4，然后依次验证：

1. Player 无外部 GLib 主循环也能发现轨道、播放并收到 EOS。
2. Recorder 使用测试音视频源录制，并通过 EOS 生成可落盘 MP4。
3. Transcoder 执行带时间范围的 H.264/AAC 重新编码。
4. Transcoder 使用 parsebin 执行无解码封装复制。
5. M0 单元测试和 M1 六个机制练习继续通过。

测试文件结束后会自动删除，不依赖物理媒体设备。

## 调试建议

```bash
# 查看阶段 2 的实际管线字符串和常规错误
GST_DEBUG=2 ./build-m2/tests/stage2/test_stage2_core

# 重点观察解码、动态链接和 Caps
GST_DEBUG=uridecodebin:5,decodebin:5,caps:5 \
    ./build-m2/tests/stage2/test_stage2_core

# 观察复用器和文件收尾
GST_DEBUG=qtmux:5,filesink:4 \
    ./build-m2/tests/stage2/test_stage2_core
```

## 已知边界

- M2 的目标是可靠的本地单文件、单音轨和单视频轨工作流。
- 硬件编码属性因插件和驱动而异，M2 测试固定使用软件编码器。
- `player_get_frame` 的调用方必须提供至少 `width * height * 3` 字节。
- 多轨选择、字幕、网络流、分段录制和零拷贝属于后续阶段。
