#include "wavinahc9000v2_climate.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/version.h"

#include <algorithm>
#include <cmath>

namespace esphome {
namespace wavinahc9000v2 {
static const char *const TAG = "wavinahc9000v2.climate";

// 与 Target Temperature number 一致（min 6 / max 40 / step 0.5）
static const float MIN_TEMPERATURE = 6.0f;
static const float MAX_TEMPERATURE = 40.0f;
static const float TARGET_TEMPERATURE_STEP = 0.5f;
// 温度传感器精度 0.1 °C
static const float CURRENT_TEMPERATURE_STEP = 0.1f;

// 下发命令后的保持期：排在写命令之前的轮询仍会读到旧值（ESPHome 2026.9 实测写命令要排队十几秒：
// Wavin 的 0x44/0x45 不算“写”功能码，和轮询一起按先后排队）。
// 保持期内只忽略“命令前的旧值”，直到读回与命令一致、目标温度 number 又被轮询读了 COMMAND_HOLD_POLLS 次，
// 或超过 COMMAND_HOLD_MAX_MS；之后重新以设备状态为准。
// 用 number 的读数计数：它由 modbus_controller 每个轮询周期读一次（不去重），与 current_temp_sensor 的
// 刷新频率无关（current 可能是 2 s 刷新的 template 传感器）。额外的 component.update 不会重复计数：
// 0x43 轮询的 max_pending 为 1，同一请求还在队列里时重复的会被拒绝；所以正常情况下写入前最多只有 1 次旧读数，
// 4 次和下面的 3 次都留了余量。只有写入本身超时重发（重发排到队尾）时才可能多出旧读数，此时保持期可能提前结束，
// 最多显示一个周期的旧值（只影响显示）。
// 本 climate 的 control() 之外的 number 发布都会计数：HA 里直接设置 number，或同一通道另一个 climate
// （包里的和 Safe 层的共用一个 number）下发目标温度时的乐观发布，都会让本 climate 的保持期提前结束（只影响显示）。
static const uint8_t COMMAND_HOLD_POLLS = 4;
static const uint32_t COMMAND_HOLD_MAX_MS = 180000;
// 待机开关去重：写入后读回与乐观发布相同的值时不会触发回调。命令后 number 已读了这么多次（正常情况下已在写入之后，
// 写入之前排队的开关旧读数也都已完成）、开关状态又与命令一致，就认为写入已确认
static const uint8_t STANDBY_CONFIRM_POLLS = 3;

// 浮点比较：NaN（尚无读数）与 NaN 视为未变化
static bool same_value(float a, float b) { return (std::isnan(a) && std::isnan(b)) || a == b; }
// 设定值比较：读数是 raw*0.1，与 0.5 步长的命令值有浮点误差
static bool same_setpoint(float a, float b) { return (std::isnan(a) && std::isnan(b)) || std::fabs(a - b) < 0.05f; }

void Wavinahc9000v2Climate::setup() {
  current_temp_sensor_->add_on_state_callback([this](float state) {
    // ESP_LOGD(TAG, "CURRENT TEMP SENSOR CALLBACK: %f", state);
    current_temperature = state;
    this->recalc_action_();   // 温度变了，重算动作
  });
  temp_setpoint_number_->add_on_state_callback([this](float state) {
    // ESP_LOGD(TAG, "TEMP SETPOINT SENSOR CALLBACK: %f", state);
    if (!this->in_control_) {
      // 轮询读数（或 HA 里直接设置 number）：数命令之后读了几次（见 COMMAND_HOLD_POLLS）
      if (this->standby_hold_polls_ < 255)
        this->standby_hold_polls_++;
      if (this->target_hold_polls_ < 255)
        this->target_hold_polls_++;
      this->target_reading_ = state;   // 设备（或 HA 里直接设置的 number）报告的最新值
      if (this->hold_active_(this->target_hold_, this->target_hold_start_, this->target_hold_polls_)) {
        if (same_setpoint(state, this->target_hold_value_)) {
          this->target_hold_ = false;   // 读回的设定值与命令一致
        } else if (std::isnan(state) || same_setpoint(state, this->target_hold_stale_)) {
          // 命令前的旧值（写入还没生效时的轮询）或读取失败：保持命令值
          ESP_LOGD(TAG, "Ignoring setpoint %.1f read before the write of %.1f", state, this->target_hold_value_);
          this->recalc_action_();       // 待机保持期可能刚结束
          return;
        } else {
          // 既不是命令值也不是命令前的旧值：HA 里直接设置了 number、墙上改了温度，或更早的一次写入刚生效。
          // 以它为准；保持期改为等它读回，命令前的旧值继续忽略
          this->target_hold_value_ = state;
          this->target_hold_start_ = millis();
          this->target_hold_polls_ = 0;
        }
      }
    }
    target_temperature = state;
    this->recalc_action_();   // 温度变了，重算动作
  });
  mode_switch_->add_on_state_callback([this](bool state) {
    ESP_LOGD(TAG, "OPERATION MODE CALLBACK: %s", ONOFF(state));
    this->standby_known_ = true;
    if (this->standby_hold_ && !this->in_control_ && state == this->standby_hold_value_)
      this->standby_hold_ = false;   // 读回的待机状态与命令一致
    this->recalc_action_();          // mode 在 recalc_action_() 里按开关状态计算
  });
  hvac_action_->add_on_state_callback([this](bool state) {
    ESP_LOGD(TAG, "Current action is : %s", ONOFF(state));
    // recalc_action_() 直接读 hvac_action_->state：binary_sensor 的第一次状态不会触发这个回调
    this->recalc_action_();
  });

  // 启动时通常还没有轮询结果：只取已有读数（Number::state 在第一次发布前未初始化）
  current_temperature = current_temp_sensor_->has_state() ? current_temp_sensor_->state : NAN;
  target_temperature  = temp_setpoint_number_->has_state() ? temp_setpoint_number_->state : NAN;
  this->target_reading_ = target_temperature;
  this->recalc_action_();         // 启动时也算一次
}

void Wavinahc9000v2Climate::control(const climate::ClimateCall& call) {
  // number/开关在 perform()/turn_*() 里会同步触发上面的回调；这些回调不单独发布，最后统一发布一次
  this->in_control_ = true;

  if (call.get_target_temperature().has_value())
  {
    float target = *call.get_target_temperature();
    if (std::isnan(target)) {
      ESP_LOGW(TAG, "Ignoring NaN target temperature");
    } else {
      // 取整到 0.5 再限制在 6–40：number 会拒绝范围外的值；写入的 x*10 只有 0.5 的倍数才能精确转换
      target = std::round(target / TARGET_TEMPERATURE_STEP) * TARGET_TEMPERATURE_STEP;
      target = std::max(MIN_TEMPERATURE, std::min(MAX_TEMPERATURE, target));
      ESP_LOGD(TAG, "Target temperature changed to: %.1f", target);
      this->target_temperature = target;
      // 保持期内忽略命令前的旧值；连续下发时旧值仍是第一次命令之前读到的值
      if (!this->hold_active_(this->target_hold_, this->target_hold_start_, this->target_hold_polls_))
        this->target_hold_stale_ = this->target_reading_;
      this->target_hold_ = true;
      this->target_hold_value_ = target;
      this->target_hold_start_ = millis();
      this->target_hold_polls_ = 0;
      temp_setpoint_number_->make_call().set_value(target).perform();
    }
  }

  if (call.get_mode().has_value())
  {
    ESP_LOGD(TAG, "MODE CHANGED");
    auto new_mode = *call.get_mode();

    if (new_mode == climate::CLIMATE_MODE_AUTO || new_mode == climate::CLIMATE_MODE_HEAT) // 添加加热
    {
      this->on_mode_ = new_mode;   // 退出待机后报告这个模式
      // 只在需要退出待机时写 MODE=0（MANUAL）：开关报告待机、还没读到过开关、或刚下发的 OFF 还在保持期。
      // 不在待机时（MANUAL/ECO/COMFORT/PARTY/HOLIDAY）HEAT↔AUTO 或重复下发只改报告的模式，
      // 否则会把 ECO/COMFORT 等模式悄悄改成 MANUAL。
      bool off_pending = this->hold_active_(this->standby_hold_, this->standby_hold_start_, this->standby_hold_polls_) &&
                         this->standby_hold_value_;
      if (!this->standby_known_ || mode_switch_->state || off_pending) {
        this->standby_hold_ = true;
        this->standby_hold_value_ = false;
        this->standby_hold_start_ = millis();
        this->standby_hold_polls_ = 0;
        ESP_LOGD(TAG, "Turning off thermostat standby mode");
        mode_switch_->turn_off();
      }
    }
    else if (new_mode == climate::CLIMATE_MODE_OFF)
    {
      this->standby_hold_ = true;
      this->standby_hold_value_ = true;
      this->standby_hold_start_ = millis();
      this->standby_hold_polls_ = 0;
      ESP_LOGD(TAG, "Turning on thermostat standby mode");
      mode_switch_->turn_on();
    }
  }

  this->in_control_ = false;
  this->recalc_action_(true);   // control() 之后总是发布
}

climate::ClimateTraits Wavinahc9000v2Climate::traits() {
  auto traits = climate::ClimateTraits();

  traits.set_supported_modes({
    climate::ClimateMode::CLIMATE_MODE_OFF,
    climate::ClimateMode::CLIMATE_MODE_AUTO,
    climate::CLIMATE_MODE_HEAT // 新的heat
  });

  // ESPHome 2025.11.0 起改用 feature flags，2026.5.0 删除了旧 setter；新旧版本都能编译
#if ESPHOME_VERSION_CODE >= VERSION_CODE(2025, 11, 0)
  traits.add_feature_flags(climate::CLIMATE_SUPPORTS_ACTION | climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE);
#else
  traits.set_supports_action(true);
  traits.set_supports_current_temperature(true);
#endif
  // 目标温度步长 0.5；当前温度保留 0.1（HA 按当前温度步长决定精度，0.5 会把 21.3 显示成 21.5）
  traits.set_visual_target_temperature_step(TARGET_TEMPERATURE_STEP);
  traits.set_visual_current_temperature_step(CURRENT_TEMPERATURE_STEP);
  traits.set_visual_min_temperature(MIN_TEMPERATURE);
  traits.set_visual_max_temperature(MAX_TEMPERATURE);

  return traits;
}

void Wavinahc9000v2Climate::dump_config() {
  LOG_CLIMATE("", "Wavinahc9000v2 Climate", this);
}

bool Wavinahc9000v2Climate::hold_active_(bool &hold, uint32_t start, uint8_t polls) {
  if (hold && (polls >= COMMAND_HOLD_POLLS || millis() - start >= COMMAND_HOLD_MAX_MS))
    hold = false;   // 超时：以设备为准
  return hold;
}

void Wavinahc9000v2Climate::recalc_action_(bool force_publish) { //新增的重算函数
  // mode：待机 → OFF，否则报告最近一次下发的开启模式（默认 HEAT）。
  // 以前开关回调固定写 AUTO，Home App 里选的 HEAT 会被改回 AUTO，磁贴不再按加热着色。
  // 命令保持期内报告命令的状态；开关还没报告过时保持原值（开机为 OFF）。
  // 保持期结束时开关通常已在写入之后被读过；若还是写入前的旧值（轮询失败，或写入超时重发排到了队尾），
  // 下一次读到新值时开关状态会变化、回调会触发，
  // 最多晚一个轮询周期纠正（只影响显示）。
  if (this->standby_hold_ && this->standby_known_ && this->standby_hold_polls_ >= STANDBY_CONFIRM_POLLS &&
      this->mode_switch_->state == this->standby_hold_value_)
    this->standby_hold_ = false;   // 读回与乐观发布相同，被开关去重吞掉了回调：这里确认
  if (this->hold_active_(this->standby_hold_, this->standby_hold_start_, this->standby_hold_polls_)) {
    this->mode = this->standby_hold_value_ ? climate::CLIMATE_MODE_OFF : this->on_mode_;
  } else if (this->standby_known_) {
    this->mode = this->mode_switch_->state ? climate::CLIMATE_MODE_OFF : this->on_mode_;
  }

  if (this->mode == climate::CLIMATE_MODE_OFF) {
    // 1) OFF 就直接 OFF
    this->action = climate::CLIMATE_ACTION_OFF;
  } else if (this->hvac_action_->state) {
    // 2) 只要硬件输出 on，就标记 HEATING
    this->action = climate::CLIMATE_ACTION_HEATING;
  } else if (!std::isnan(this->current_temperature) && !std::isnan(this->target_temperature) &&
             this->target_temperature >= this->current_temperature + this->action_hysteresis_) {
    // 3) 软判定：设定 > 当前 + 余量，就认为“在加热”
    this->action = climate::CLIMATE_ACTION_HEATING;
  } else {
    // 否则（或信息不全时）为 idle
    this->action = climate::CLIMATE_ACTION_IDLE;
  }

  if (this->in_control_)
    return;  // control() 结束时统一发布

  // 每次轮询都会触发回调，状态没变就不重复发布；第一次总是发布
  if (!force_publish && this->published_ && this->mode == this->published_mode_ &&
      this->action == this->published_action_ &&
      same_value(this->current_temperature, this->published_current_) &&
      same_value(this->target_temperature, this->published_target_)) {
    return;
  }
  this->published_ = true;
  this->published_mode_ = this->mode;
  this->published_action_ = this->action;
  this->published_current_ = this->current_temperature;
  this->published_target_ = this->target_temperature;
  this->publish_state();
}

} // namespace wavinahc9000v2
} // namespace esphome
