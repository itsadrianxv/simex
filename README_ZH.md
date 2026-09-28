# simex

simex（SIMulated EXchange，模拟交易所）是一个 SHFE 风格的 C++ 交易所，包含模拟市场参与者订单流，以及用于监控订单簿变化的 TUI。

![订单簿界面](assets/image.png)

## 快速开始

### 先决条件

- C++20 编译器（GCC 或 Clang）
- CMake 3.25+ 和 Ninja
- OpenSSL（Crypto）
- nlohmann_json，或者启用固定版本源码回退选项 `SIMEX_FETCH_NLOHMANN_JSON=ON`

### 使用 CMake 构建

为获得可复现的 Ninja 构建，请使用仓库提供的 CMake 预设：

```sh
cmake --preset ci-gcc
cmake --build --preset ci-gcc
ctest --preset test-ci-gcc
```

`dev-gcc`、`dev-clang`、`ci-gcc`、`ci-clang` 和 `sanitizer` 预设分别提供对应的本地、CI 和消毒器配置。

不使用预设时：

```sh
cmake -S . -B build -DSIMEX_FETCH_NLOHMANN_JSON=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

运行确定性演示程序：

```sh
./build/ci-gcc/simex_demo simex.json
```

### 查看实时订单簿

`simex_tui` 在 Linux 上运行，并监听 `server.json` 中配置的 UDP 端口。在仓库根目录打开两个终端：

终端 1：
```sh
./build/ci-gcc/simex_server server.json
```

终端 2：
```sh
./build/ci-gcc/simex_tui 19002
```

TUI 会显示聚合后的买卖盘价位，以及最近的 ADD、MODIFY、CANCEL 和 TRADE 事件。按 `p` 暂停显示，按 `+` 或 `-` 更改显示价位数量，按 `q` 或 Ctrl-C 退出。UDP 端口 `19002` 上只能有 TUI 一个监听器。

若当前不在配置的交易时段内，可在 `server.json` 顶层添加 `"phase_override": "CONTINUOUS"`，以便持续观察交易所。

## 说明

该交易所可以与我的市场参与者侧交易系统 [jev-qaunt](https://github.com/itsadrianxv/jev-quant) 同时运行，组成完整的交易生态。详情请参阅 [传输服务指南](docs/jev-transport-service.md)。

本项目还处于非常早期的阶段，预计会存在缺陷。

## 许可证

MIT
