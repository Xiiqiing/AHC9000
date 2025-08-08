#pragma once

#include "esphome/core/component.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/number/number.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/modbus_controller/modbus_controller.h"

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

  void set_battery_level_sensor(sensor::Sensor *battery_level_sensor) { 
    battery_level_sensor_ = battery_level_sensor; 
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

  // —— 新增：对外输出电池相关实体（百分比 + 低电量）及阈值 ——
  void set_battery_percent_sensor(sensor::Sensor *s) { this->battery_percent_sensor_ = s; }
  void set_battery_low_binary(binary_sensor::BinarySensor *b) { this->battery_low_sensor_ = b; }
  void set_low_batt_threshold(float pct) { this->low_batt_threshold_ = pct; }

protected:
  /// Override control to change settings of the climate device.
  void control(const climate::ClimateCall& call) override;

  /// Return the traits of this controller.
  climate::ClimateTraits traits() override;

  /// The sensor used for getting the current temperature
  sensor::Sensor *current_temp_sensor_{ nullptr };

  /// The number component used for getting the temperature setpoint
  number::Number *temp_setpoint_number_{ nullptr };

  /// The select component used for getting the operation mode
  switch_::Switch *mode_switch_{ nullptr };

  /// The select component used for getting the current action
  binary_sensor::BinarySensor *hvac_action_{ nullptr };

  sensor::Sensor *battery_level_sensor_{nullptr};

private:
  // —— 新增成员：输出的电池百分比 / 低电量实体与阈值 ——
  sensor::Sensor *battery_percent_sensor_{nullptr};
  binary_sensor::BinarySensor *battery_low_sensor_{nullptr};
  float low_batt_threshold_{20.0f};
};
} // namespace wavinahc9000v2
} // namespace esphome
