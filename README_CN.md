# rauc-ble-otad

[English](README.md)

基于 RAUC 的 STM32MP2 BLE OTA 更新守护进程。该守护进程作为 BLE GATT 外设运行，允许手机通过蓝牙低功耗推送固件更新并通过 RAUC 完成安装。

## 架构

```
手机 (BLE Central)
  │
  ▼
┌─────────────────────────────────────────┐
│  BLE 帧编解码 (ble_pack / ble_reasm)    │  C — ATT 尺寸 PDU 分片与重组
├─────────────────────────────────────────┤
│  应用分发 (app_dispatch)                │  C++ (C ABI) — protobuf 编解码、
│                                         │     消息类型路由
├─────────────────────────────────────────┤
│  OTA 处理 + Casync 执行器               │  C — 文件接收状态机、差分更新、
│                                         │     RAUC 安装
├─────────────────────────────────────────┤
│  GATT 服务 (gatt_server)                │  C/GLib — BlueZ D-Bus 外设、
│                                         │     GATT 应用注册
└─────────────────────────────────────────┘
```

### BLE 帧协议

每条逻辑消息被拆分为 BLE ATT 尺寸的 PDU（默认 20 字节载荷），使用 1 字节控制前缀标识帧位置：

| 前缀 | 含义 |
|------|------|
| `^`  | 首帧（携带 4 字节大端序总长度） |
| `~`  | 中间帧 |
| `;`  | 末帧 |
| `\|` | 单帧（携带 4 字节大端序总长度） |

### OTA 传输流程

```
手机                              开发板
  │  OtaBegin(filename, size, chunk_size)  │
  │───────────────────────────────────────►│  创建文件，开始写入
  │  OtaBeginAck(ok)                       │
  │◄───────────────────────────────────────│
  │                                        │
  │  OtaChunk(seq=0, data)                 │  ×N 个分片
  │───────────────────────────────────────►│
  │  OtaChunkAck(seq, ok)                  │
  │◄───────────────────────────────────────│
  │                                        │
  │  OtaEnd(crc64)                         │
  │───────────────────────────────────────►│  关闭文件，校验大小
  │  OtaEndAck(ok)                         │
  │◄───────────────────────────────────────│
  │                                        │
  │  (若为 rootfs.caibx)                   │
  │  MissingChunks(ok, content)            │  casync list-chunks 输出
  │◄───────────────────────────────────────│
  │                                        │
  │  OtaInstall(sha256)                    │  casync extract → 校验 → rauc
  │───────────────────────────────────────►│
  │  OtaInstallReply(ok, error)            │
  │◄───────────────────────────────────────│
```

### 其他消息

- **Ping / Pong** — 心跳 / 连通性检测
- **GetVersion / VersionReply** — 从 `/data/os-release` 读取固件版本号

## 构建

宿主机需要安装 `protoc`、libprotobuf 和 GLib/GIO 开发头文件。

```sh
make            # 构建守护进程 + 所有测试二进制文件
make check      # 构建并运行所有测试
make clean      # 清除构建产物
```

交叉编译变量：

```sh
make CC=aarch64-linux-gnu-gcc CXX=aarch64-linux-gnu-g++ PROTOC=protoc \
     GLIB_CFLAGS="..." GLIB_LIBS="..."
```

## 运行

```sh
# 启动守护进程（需要 BlueZ + system D-Bus）
sudo ./rauc-ble-otad

# 调试：十六进制转储每个 BLE 帧
sudo OTA_LOG_HEX=1 ./rauc-ble-otad

# 调整重组超时时间（默认 5000 ms）
sudo OTA_REASM_TIMEOUT_MS=10000 ./rauc-ble-otad
```

守护进程广播名称为 **OTA-STM32MP2**，暴露一个 GATT 服务，包含两个特征值：

| 特征值 | UUID | 属性 |
|--------|------|------|
| RX | `...def1` | WriteWithoutResponse |
| TX | `...def2` | Notify |

## 测试

```sh
make check
# 或单独运行某个测试：
make tests/test_ble_pack && ./tests/test_ble_pack
make tests/test_ble_reasm && ./tests/test_ble_reasm
make tests/test_app_dispatch && ./tests/test_app_dispatch
```

## 项目结构

```
├── Makefile
├── proto/
│   └── app.proto              # Protobuf 消息定义
├── src/
│   ├── ble_pack.{c,h}         # BLE 帧打包（发送端迭代器）
│   ├── ble_reasm.{c,h}        # BLE 帧重组（接收端状态机）
│   ├── app_dispatch.{cc,h}    # Protobuf 编解码 + C ABI 包装
│   ├── ota_handler.{c,h}      # 文件接收状态机
│   ├── firmware_version.{c,h} # 从 os-release 读取 VERSION_ID
│   ├── casync_runner.{c,h}    # casync/rauc 进程封装
│   ├── gatt_server.{c,h}      # BlueZ GATT 外设（D-Bus）
│   └── main.c                 # 守护进程入口
└── tests/
    ├── test_ble_pack.c
    ├── test_ble_reasm.c
    └── test_app_dispatch.cc
```

## 许可证

请参阅各源文件中的许可证信息。
