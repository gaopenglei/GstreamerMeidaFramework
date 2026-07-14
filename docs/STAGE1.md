# 阶段 1：GStreamer 基础机制

本阶段不依赖摄像头、声卡或显示器。所有练习使用测试源和 `fakesink`，
适合在本机、WSL 和 CI 中重复运行。

## 构建与测试

```bash
cmake -S . -B build-m1 \
    -DMEDIA_FRAMEWORK_STAGE=1 \
    -DBUILD_TESTS=ON \
    -DCMAKE_BUILD_TYPE=Debug
cmake --build build-m1 -j$(nproc)
ctest --test-dir build-m1 --output-on-failure
```

`MEDIA_FRAMEWORK_STAGE` 会成为同名 C 编译宏。阶段常量和条件宏定义在
`include/learning_stage.h`：

```c
#if MEDIA_FRAMEWORK_STAGE_AT_LEAST(MEDIA_FRAMEWORK_STAGE_M1)
/* M1 及后续阶段代码 */
#endif
```

默认值为 `0`，因此普通构建不会编译学习示例。设置为 `1` 时，CMake
会加入 `examples/stage1`，并把六个练习注册到 CTest。

## 建议学习方式

每个练习按以下顺序完成：

1. 先运行对应的 `gst-launch-1.0` 命令。
2. 使用 `gst-inspect-1.0` 查看涉及的 Element、属性和 Pad Template。
3. 阅读对应 C 文件，找出命令行管线与 C API 的一一对应关系。
4. 修改一个属性或 Caps，预测结果后重新构建运行。
5. 使用 `GST_DEBUG=3` 观察协商、状态切换和错误信息。

## 01：Element 与 Factory

源码：`examples/stage1/01_elements.c`

掌握：

- `gst_element_factory_make` 如何通过工厂创建 Element。
- Element 名称、Factory 名称和 klass 元数据的区别。
- GObject 属性设置、Bin 所有权和对象引用释放。

```bash
gst-inspect-1.0 videotestsrc
gst-launch-1.0 videotestsrc num-buffers=1 ! fakesink sync=false
./build-m1/examples/stage1/stage1_01_elements
```

练习：把 `videotestsrc` 换成 `audiotestsrc`，并用
`gst-inspect-1.0 audiotestsrc` 找一个可修改的属性。

## 02：手动构建 Pipeline

源码：`examples/stage1/02_pipeline.c`

掌握：

- Pipeline、Bin 和 Element 的包含关系。
- `gst_bin_add_many` 与 `gst_element_link_many` 的职责。
- `NULL -> READY -> PAUSED -> PLAYING` 状态变化。

```bash
gst-launch-1.0 -v \
    videotestsrc num-buffers=30 ! videoconvert ! fakesink sync=false
./build-m1/examples/stage1/stage1_02_pipeline
```

练习：在 `videoconvert` 后加入 `videoscale`，确认链接仍然成功。

## 03：Bus 与 Message

源码：`examples/stage1/03_bus.c`、`examples/stage1/stage1_common.c`

掌握：

- Pipeline 通过 Bus 向应用层发送消息。
- ERROR、EOS 和 STATE_CHANGED 的处理与资源清理。
- GStreamer 异步数据流和应用控制流的边界。

```bash
gst-launch-1.0 -m \
    audiotestsrc num-buffers=20 ! audioconvert ! fakesink sync=false
./build-m1/examples/stage1/stage1_03_bus
```

练习：故意把 `audioconvert` 改成不存在的插件，观察创建阶段错误；再给
管线加入不兼容 Caps，观察运行阶段 ERROR 消息。

## 04：动态 Pad

源码：`examples/stage1/04_dynamic_pad.c`

管线前半段把测试音频编码为 WAV，再交给 `decodebin`。`decodebin`
识别出流之后才创建输出 Pad，因此应用必须监听 `pad-added` 信号并在
运行时完成链接。

```bash
gst-launch-1.0 -v \
    audiotestsrc num-buffers=32 ! wavenc ! decodebin ! \
    audioconvert ! audioresample ! fakesink sync=false
./build-m1/examples/stage1/stage1_04_dynamic_pad
```

掌握：

- Always、Sometimes 和 Request Pad 的差异。
- 通过 Pad Caps 判断音频流与视频流。
- 获取静态 Sink Pad 后使用 `gst_pad_link` 动态链接。

练习：打印完整 Caps，而不只是结构名称；随后拒绝非
`audio/x-raw` 类型的 Pad。

## 05：Caps 与协商

源码：`examples/stage1/05_caps.c`

掌握：

- Caps 描述媒体类型，不承载媒体数据。
- `capsfilter` 如何限制格式、分辨率和帧率。
- CAPS Event 与最终协商结果。

```bash
gst-launch-1.0 -v \
    videotestsrc num-buffers=10 ! \
    'video/x-raw,format=I420,width=320,height=240,framerate=30/1' ! \
    fakesink sync=false
./build-m1/examples/stage1/stage1_05_caps
```

练习：把格式依次改为 `NV12` 和一个不受支持的值，比较成功协商与
`not-negotiated` 错误。

## 06：Request Pad 与 Tee 分支

源码：`examples/stage1/06_request_pad.c`

`tee` 的输出 Pad 不会自动存在。应用从 `src_%u` Pad Template 请求两
个 Pad，分别连接到两个 Queue；结束后显式释放 Request Pad。

```bash
gst-launch-1.0 \
    audiotestsrc num-buffers=20 ! tee name=t \
    t. ! queue ! fakesink sync=false \
    t. ! queue ! fakesink sync=false
./build-m1/examples/stage1/stage1_06_request_pad
```

掌握：

- `gst_element_request_pad_simple` 和旧版兼容 API。
- 为什么 `tee` 的每条分支都应使用独立 Queue。
- Request Pad 的请求、链接、释放和引用计数。

练习：移除其中一个 Queue，观察线程和背压行为；然后把一个
`fakesink` 的 `sync` 设置为 `true` 比较运行时间。

## 调试工具

```bash
# 查看插件、属性和 Pad Template
gst-inspect-1.0 decodebin

# 输出常用调试日志
GST_DEBUG=3 ./build-m1/examples/stage1/stage1_04_dynamic_pad

# 输出 Pipeline DOT 图
mkdir -p /tmp/gst-dot
GST_DEBUG_DUMP_DOT_DIR=/tmp/gst-dot \
    ./build-m1/examples/stage1/stage1_06_request_pad
dot -Tpng /tmp/gst-dot/*.dot -o pipeline.png
```

完成标准：

- 能独立解释六个示例中每个 Element 的作用。
- 能区分静态 Pad、动态 Pad、Request Pad。
- 能从 Bus ERROR 的 `message` 和 `debug` 定位链接或协商失败。
- 能用 `gst-launch-1.0` 先验证管线，再把它翻译为 C API。
- 能正确处理 Pipeline 状态、EOS、对象引用和 Request Pad 生命周期。
