#pragma once

#include <cmath>

#include "esphome/core/component.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/number/number.h"
#include "esphome/components/switch/switch.h"

namespace esphome {
namespace wavinahc9000v2 {
class Wavinahc9000v2Climate : public climate::Climate, public Component {
public:

  Wavinahc9000v2Climate() {}

  void setup() override;
  void dump_config() override;

  void set_current_temp_sensor(sensor::Sensor *sensor) {
    this->current_temp_sensor_ = sensor;
  }

  void set_temp_setpoint_number(number::Number *number) {
    this->temp_setpoint_number_ = number;
  }

  void set_mode_switch(switch_::Switch *switch_) {
    this->mode_switch_ = switch_;
  }

  void set_hvac_action(binary_sensor::BinarySensor *binary_sensor) {
    this->hvac_action_ = binary_sensor;
  }


protected:
  /// Override control to change settings of the climate device.
  void control(const climate::ClimateCall& call) override;

  /// Return the traits of this controller.
  climate::ClimateTraits traits() override;

  // 统一重算 hvac_action；只有 mode/action/当前温度/目标温度变化时才发布（force_publish 时总是发布）
  void recalc_action_(bool force_publish = false);

  // 命令保持期是否仍有效（见 .cpp 的 COMMAND_HOLD_*），过期时把 hold 清掉
  static bool hold_active_(bool &hold, uint32_t start, uint8_t polls);

  /// The sensor used for getting the current temperature
  sensor::Sensor *current_temp_sensor_{ nullptr };

  /// The number component used for getting the temperature setpoint
  number::Number *temp_setpoint_number_{ nullptr };

  /// The standby switch (ON = standby = climate OFF)
  switch_::Switch *mode_switch_{ nullptr };

  /// The binary sensor reporting the controller's heating output for this channel
  binary_sensor::BinarySensor *hvac_action_{ nullptr };

private:
  float action_hysteresis_{0.3f};  // ← 判定余量，避免抖动，可按需改

  // control() 执行期间为 true：number/开关在 perform()/turn_*() 里同步触发的回调不单独发布，由 control() 最后统一发布
  bool in_control_{false};

  // 待机开关是否已经报告过状态（开机时未知，climate 保持 OFF）
  bool standby_known_{false};
  // 命令保持期（见 .cpp 的 COMMAND_HOLD_*）：下发后在总线读回之前，忽略与命令相反的旧读数
  bool standby_hold_{false};
  bool standby_hold_value_{false};
  uint32_t standby_hold_start_{0};
  uint8_t standby_hold_polls_{0};
  bool target_hold_{false};
  float target_hold_value_{NAN};
  float target_hold_stale_{NAN};   // 命令前设备报告的设定值：保持期内只忽略这个旧值
  uint32_t target_hold_start_{0};
  uint8_t target_hold_polls_{0};
  float target_reading_{NAN};      // control() 之外 number 报告的最新值

  // 上次发布的状态，用来跳过重复发布（温度传感器 force_update，每次轮询都会触发回调）
  bool published_{false};
  climate::ClimateMode published_mode_{climate::CLIMATE_MODE_OFF};
  climate::ClimateAction published_action_{climate::CLIMATE_ACTION_OFF};
  float published_current_{NAN};
  float published_target_{NAN};
};
} // namespace wavinahc9000v2
} // namespace esphome
