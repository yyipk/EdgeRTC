# EdgeRTC

[English](README.md)

EdgeRTC 是一款面向 IoT 及嵌入式设备的轻量级、高性能 WebRTC 库，基于
libpeer 开发，专注于小内存、小 Flash 占用以及低延迟的实时音频、视频和数据
通信。它保留设备端通信所需的核心协议能力，同时避免引入浏览器级运行时的
资源开销。

## 设计目标

- 将 RAM 和 Flash 占用控制在资源受限设备可接受的范围内。
- 为实时媒体提供可预测的处理耗时和内存分配行为。
- 减少数据复制，并高效处理网络接收队列中的连续数据包。
- 通过精简的 socket、时钟和传输适配接口隔离平台差异。
- 允许产品只集成实际需要的媒体链路和音频算法。

## 主要能力

- 支持 STUN、TURN、候选提名和对端反射候选的 ICE 连接。
- 支持 DTLS-SRTP 媒体加密和 SCTP DataChannel。
- 提供 H.264、G.711 A-law、G.711 mu-law 和 Opus 的 RTP/RTCP 媒体链路。
- 支持 WHIP 和 MQTT 信令。
- 提供可重复连接的传输生命周期管理和有边界的数据包解析。
- 提供适用于资源受限音频接收端的可选精简 PCMA 抖动缓冲和丢包补偿。
- 提供包括 AEC4 回声消除在内的可选嵌入式音频算法。

EdgeRTC 适用于摄像头、对讲设备、传感器、网关及其他需要 WebRTC 通信能力
的设备端产品。采集、播放、编解码、网络和平台适配由具体应用按目标平台提供。

## 仓库结构

```text
EdgeRTC/
|-- libpeer/          WebRTC 传输和信令实现
|-- cJSON/            JSON 依赖
|-- coreHTTP/         HTTP 依赖
|-- coreMQTT/         MQTT 依赖
|-- libsrtp/          SRTP 依赖
|-- mbedtls/          TLS 和 DTLS 依赖
`-- usrsctp/          SCTP 依赖
```

## 构建

可直接配置并构建 libpeer 的通用目标和示例：

```sh
cmake -S libpeer -B build
cmake --build build
```

仓库根目录的 `CMakeLists.txt` 是嵌入式 SDK 的组件入口，负责注册已启用的
libpeer 和音频算法目标，并对 EdgeRTC 目标启用链接时优化。

`libpeer/examples/bouffalo` 例程自行将本仓库源码接入构建，不启用 SDK 内置的
`CONFIG_LIBPEER`。

## 上游与许可

EdgeRTC 基于 [libpeer](https://github.com/sepfy/libpeer) 开发。相对上游基线的
修改记录在 [`libpeer/MODIFICATIONS.md`](libpeer/MODIFICATIONS.md) 中。

EdgeRTC 自有代码采用 [MIT License](LICENSE)。libpeer、AEC4 和各项依赖保留
各自的许可证，分发前请检查对应目录中的许可证文件。
