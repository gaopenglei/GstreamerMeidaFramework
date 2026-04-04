# GStreamer Media Framework

基于 GStreamer 的跨平台音视频媒体框架，支持播放、录制和转码功能。

## 功能特性

### 音视频播放
- 支持 MP4、MKV、WebM 等多种容器格式
- 支持 H.264/H.265 视频解码
- 支持 AAC/Opus 音频解码
- 低延迟播放模式
- 硬件加速解码支持

### 音视频录制
- 支持 V4L2 摄像头采集
- 支持 ALSA/PulseAudio 音频采集
- 支持 H.264/H.265 视频编码
- 支持 AAC/Opus 音频编码
- MP4 格式封装输出
- 低延迟录制模式

### 音视频转码
- 支持格式转换（H.264↔H.265, AAC↔Opus）
- 支持视频流复制（无需重新编码）
- 支持转码范围设置
- 进度回调支持

## 系统要求

- Linux 操作系统（Ubuntu 18.04+, Fedora 30+, Arch Linux 等）
- GStreamer 1.14 或更高版本
- CMake 3.10 或更高版本
- GCC 或 Clang 编译器

## 快速开始

### 1. 安装依赖

**Ubuntu/Debian:**
```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential cmake pkg-config git \
    libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
    gstreamer1.0-plugins-good gstreamer1.0-plugins-bad \
    gstreamer1.0-plugins-ugly gstreamer1.0-libav \
    libx264-dev libx265-dev libopus-dev libv4l-dev libasound2-dev
```

**Fedora/RHEL:**
```bash
sudo dnf install -y \
    gcc gcc-c++ make cmake pkgconfig git \
    gstreamer1-devel gstreamer1-plugins-base-devel \
    gstreamer1-plugins-good gstreamer1-plugins-bad-free \
    gstreamer1-plugins-ugly-free gstreamer1-libav \
    x264-devel x265-devel opus-devel libv4l-devel alsa-lib-devel
```

**Arch Linux:**
```bash
sudo pacman -S --noconfirm \
    base-devel cmake pkg-config git \
    gst-plugins-base gst-plugins-good gst-plugins-bad \
    gst-plugins-ugly gst-libav \
    x264 x265 opus v4l-utils alsa-lib
```

### 2. 编译项目

```bash
# 使用安装脚本（推荐）
chmod +x scripts/install.sh
./scripts/install.sh

# 或手动编译
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
sudo make install
```

### 3. 使用示例

**播放媒体文件:**
```bash
media_framework play /path/to/video.mp4
media_framework play /path/to/video.mp4 --low-latency
```

**录制视频:**
```bash
# 列出可用设备
media_framework devices

# 从摄像头录制 10 秒
media_framework record -o output.mp4 \
    --video-device /dev/video0 \
    --video-codec h264 \
    --width 1920 --height 1080 \
    --framerate 30 \
    --duration 10
```

**转码媒体文件:**
```bash
# H.264 转 H.265
media_framework transcode -i input.mp4 -o output.mp4 \
    --video-codec h265 --audio-codec copy

# AAC 转 Opus
media_framework transcode -i input.mp4 -o output.mp4 \
    --video-codec copy --audio-codec opus

# 指定码率和时间范围
media_framework transcode -i input.mp4 -o output.mp4 \
    --video-codec h265 --bitrate 8000000 \
    --start 10 --end 60
```

**查看媒体信息:**
```bash
media_framework info /path/to/video.mp4
```

## 项目结构

```
gstreamer-media-framework/
├── include/              # 头文件
│   ├── logger.h          # 日志模块
│   ├── error.h           # 错误处理
│   ├── media_types.h     # 媒体类型定义
│   ├── player.h          # 播放器接口
│   ├── recorder.h        # 录制器接口
│   ├── transcoder.h      # 转码器接口
│   └── media_controller.h # 控制器接口
├── src/                  # 源文件
│   ├── core/             # 核心模块
│   ├── modules/          # 功能模块
│   ├── utils/            # 工具模块
│   └── main.c            # 主程序入口
├── tests/                # 测试文件
├── docs/                 # 文档
├── scripts/              # 脚本
│   └── install.sh        # 安装脚本
└── CMakeLists.txt        # CMake 配置
```

## API 使用

### 播放器 API

```c
#include "player.h"

// 创建播放器
PlayerConfig config;
player_config_init(&config);
MediaPlayer *player = player_create(&config);

// 设置回调
PlayerCallbacks callbacks;
player_callbacks_init(&callbacks);
callbacks.on_state_changed = my_state_callback;
player_set_callbacks(player, &callbacks);

// 播放
player_set_uri(player, "/path/to/video.mp4");
player_play(player);

// 控制
player_pause(player);
player_seek(player, 10000000000LL);  // 10秒
player_stop(player);

// 清理
player_destroy(player);
```

### 录制器 API

```c
#include "recorder.h"

// 创建录制器
RecorderConfig config;
recorder_config_init(&config);
strcpy(config.output_file, "output.mp4");
config.video_source = VIDEO_SOURCE_V4L2;
strcpy(config.video_device, "/dev/video0");
config.video_params.codec = VIDEO_CODEC_H264;
config.video_params.width = 1920;
config.video_params.height = 1080;

MediaRecorder *recorder = recorder_create(&config);

// 开始录制
recorder_start(recorder);

// 停止录制
recorder_stop(recorder);

// 清理
recorder_destroy(recorder);
```

### 转码器 API

```c
#include "transcoder.h"

// 创建转码器
TranscoderConfig config;
transcoder_config_init(&config);
strcpy(config.input_file, "input.mp4");
strcpy(config.output_file, "output.mp4");
config.video_transcode = 1;
config.video_params.codec = VIDEO_CODEC_H265;
config.video_params.bitrate = 8000000;

MediaTranscoder *transcoder = transcoder_create(&config);

// 开始转码
transcoder_start(transcoder);

// 获取进度
TranscodeProgress progress;
transcoder_get_progress(transcoder, &progress);
printf("Progress: %.1f%%\n", progress.progress);

// 清理
transcoder_destroy(transcoder);
```

### 控制器 API

```c
#include "media_controller.h"

// 创建控制器
MediaController *controller = controller_create(NULL);

// 播放
controller_play(controller, "/path/to/video.mp4");

// 录制
RecorderConfig rec_config;
recorder_config_init(&rec_config);
// ... 配置 ...
controller_start_recording(controller, &rec_config);

// 转码
TranscoderConfig trans_config;
transcoder_config_init(&trans_config);
// ... 配置 ...
controller_start_transcoding(controller, &trans_config);

// 控制
controller_pause(controller);
controller_resume(controller);
controller_stop(controller);

// 清理
controller_destroy(controller);
```

## 性能优化

### 低延迟配置

```c
PlayerConfig config;
player_config_init(&config);
config.low_latency = 1;
config.buffer_duration = 200;  // 200ms
config.sync = 0;  // 禁用同步
```

### 硬件加速

```c
// 启用硬件解码
config.enable_hardware_decode = 1;

// 使用 NVENC 编码（NVIDIA GPU）
// 需要安装 gstreamer1.0-plugins-bad 和 NVIDIA 驱动
```

### 编码优化

```c
VideoParams params;
video_params_init(&params);
params.codec = VIDEO_CODEC_H264;
params.bitrate = 4000000;  // 4 Mbps
params.gop_size = 30;      // GOP 大小
params.profile = 100;      // High Profile
```

## 错误处理

框架使用统一的错误码系统：

```c
MediaErrorCode ret = player_play(player);
if (IS_ERROR(ret)) {
    const ErrorInfo *error = error_get_last();
    printf("Error: %s (code: %d)\n", error->message, error->code);
}
```

## 日志系统

```c
// 初始化日志
LoggerConfig log_config;
logger_config_init(&log_config);
log_config.min_level = LOG_LEVEL_DEBUG;
log_config.target = LOG_TARGET_BOTH;
logger_init(&log_config);

// 使用日志
LOG_DEBUG("Debug message: %d", value);
LOG_INFO("Info message");
LOG_WARN("Warning message");
LOG_ERROR("Error message: %s", error_str);
```

## 常见问题

### Q: 找不到 GStreamer 插件
A: 确保安装了所有必要的 GStreamer 插件：
```bash
gst-inspect-1.0 x264enc  # 检查 x264 编码器
gst-inspect-1.0 mp4mux   # 检查 MP4 复用器
```

### Q: 摄像头无法打开
A: 检查设备权限和可用性：
```bash
ls -la /dev/video*
v4l2-ctl --list-devices
```

### Q: 音频设备无法访问
A: 检查 ALSA 设备：
```bash
arecord -l  # 列出录音设备
aplay -l    # 列出播放设备
```

### Q: 编译错误
A: 确保安装了所有开发包：
```bash
pkg-config --cflags --libs gstreamer-1.0
```

## 许可证

MIT License

## 贡献

欢迎提交 Issue 和 Pull Request。

## 作者

plg
