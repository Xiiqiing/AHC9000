# AHC9000：Wavin AHC 9000 地暖控制器的 ESPHome 组件

这个 fork 基于 [heinekmadsen/esphome_components](https://github.com/heinekmadsen/esphome_components) 修改。

用 ESP32 通过 RS-485（Modbus）读写 Wavin AHC 9000 地暖控制器。每个通道会生成以下实体，可在 Home Assistant 里使用，也能经 HomeKit Bridge 进入「家庭」App：

- 室温（Temperature）
- 温控器电量（Battery）
- 阀门输出（Output）
- 目标温度（Target Temperature）
- 待机开关（Standby）
- 一个 climate

带地面探头的通道还可以加一组 Comfort 实体（地面温度和 comfort 设定温度）。

本仓库维护的组件是 `components/wavinahc9000v2`。`genvex`、`genvexv2`、`sentio`、`wavinAhc9000` 是上游原样保留的副本，这里没有修改，也没有测试。

## 要求

- **ESPHome ≥ 2026.9.0**。包里设置了 `min_version`，版本太旧时编译会直接停下。
- ESP32，包括 ESP32-C3（作者用的是 `esp32-c3-devkitm-1` + arduino 框架，已测试），esp-idf 框架也能编译；再加一个 RS-485 收发器，接到控制器的 Modbus 口，波特率 38400。
- `modbus_controller` 的 `address: 1`，`id` 必须是 `${device}_modbus_controller`。
- 在 `modbus:` 里设 `turnaround_time`（例如 300ms）。ESPHome 2026.7 起默认是 600 ms，5 个房间一轮要约 16 秒。原来 `modbus_controller:` 里的 `command_throttle` 已经不起作用，删掉即可。
- 每个通道要设置这些替换变量：
  - `channel_XX`：通道索引，`0x00` = 通道 1；
  - `channel_XX_sensor`：该通道温控器（元件）的索引；
  - `channel_XX_id`、`channel_XX_friendly_name`；
  - 另外还需要全局的 `name` 和 `device`。

## 用法（示例）

下面只列出和本组件有关的部分；`wifi`、`api`、`ota`、`logger` 按常规写。更多通道就照 `channel_01` 的样子加替换变量和 `channel_XX.yaml`。

```yaml
substitutions:
  device: wavin
  name: Wavin
  channel_01: "0x00"            # 通道 1
  channel_01_sensor: "0x00"     # 该通道温控器的元件索引
  channel_01_id: channel_01
  channel_01_friendly_name: "My Bedroom"

esphome:
  name: ${device}

esp32:
  board: esp32-c3-devkitm-1     # 按你的板子修改
  framework:
    type: arduino

uart:
  - id: uart_${device}
    rx_pin: 20                  # 按你的硬件修改
    tx_pin: 21
    baud_rate: 38400
    stop_bits: 1
    parity: NONE

modbus:
  - id: ${device}_modbus
    uart_id: uart_${device}
    flow_control_pin: 10        # 按你的硬件修改
    turnaround_time: 300ms      # 收到回复后等多久再发下一条

modbus_controller:
  id: ${device}_modbus_controller
  address: 1
  modbus_id: ${device}_modbus
  update_interval: 15s

packages:
  remote_package:
    url: https://github.com/Xiiqiing/AHC9000/
    ref: main                   # 也可以钉到 tag 或提交
    refresh: 0s
    files:
      - components/wavinahc9000v2/configs/basic.yaml
      - components/wavinahc9000v2/configs/channel_01.yaml
      # - components/wavinahc9000v2/configs/channel_01_comfort.yaml   # 有地面探头时加上
```

## 文档

- `components/wavinahc9000v2/Wavin Modbus specification AHC9000 direkte.pdf`：控制器的 Modbus 协议说明。
