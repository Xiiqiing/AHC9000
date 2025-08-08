import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate, sensor, switch, number, binary_sensor
from .. import Wavinahc9000v2, CONF_WAVINAHC9000v2_ID
from esphome.const import (
    CONF_ID,
    UNIT_PERCENT,
    DEVICE_CLASS_BATTERY,
)

CONF_TARGET_TEMP = "target_temp_number_id"
CONF_CURRENT_TEMP = "current_temp_sensor_id"
CONF_MODE        = "mode_switch_sensor_id"
CONF_ACTION      = "action_sensor_id"
CONF_BATTERY_LEVEL = "battery_level_sensor_id"   # 只是本文件内的字符串键

# 新增两个“输出到 HA”的可选实体
CONF_BATTERY_PERCENT = "battery_percent"
CONF_BATTERY_LOW     = "battery_low"

wavinahc9000v2_ns = cg.esphome_ns.namespace('wavinahc9000v2')
Wavinahc9000v2Climate = wavinahc9000v2_ns.class_('Wavinahc9000v2Climate', climate.Climate, cg.Component)

CONFIG_SCHEMA = climate.climate_schema(Wavinahc9000v2Climate).extend({
    cv.GenerateID(): cv.declare_id(Wavinahc9000v2Climate),

    # 如无需要，可去掉对顶层组件的引用
    cv.GenerateID(CONF_WAVINAHC9000v2_ID): cv.use_id(Wavinahc9000v2),

    # 引用已有实体（use_id）
    cv.Required(CONF_TARGET_TEMP):  cv.use_id(number.Number),
    cv.Required(CONF_CURRENT_TEMP): cv.use_id(sensor.Sensor),
    cv.Required(CONF_MODE):         cv.use_id(switch.Switch),
    cv.Required(CONF_ACTION):       cv.use_id(binary_sensor.BinarySensor),
    cv.Required(CONF_BATTERY_LEVEL): cv.use_id(sensor.Sensor),

    # 新建输出实体（可选）
    cv.Optional(CONF_BATTERY_PERCENT): sensor.sensor_schema(
        unit_of_measurement=UNIT_PERCENT,
        accuracy_decimals=0,
        device_class=DEVICE_CLASS_BATTERY,
    ),
    cv.Optional(CONF_BATTERY_LOW): binary_sensor.binary_sensor_schema(
        device_class=DEVICE_CLASS_BATTERY
    ),
}).extend(cv.COMPONENT_SCHEMA)

def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    yield cg.register_component(var, config)
    yield climate.register_climate(var, config)

    # 绑定已有输入
    cg.add(var.set_temp_setpoint_number(  (yield cg.get_variable(config[CONF_TARGET_TEMP])) ))
    cg.add(var.set_current_temp_sensor(   (yield cg.get_variable(config[CONF_CURRENT_TEMP])) ))
    cg.add(var.set_mode_switch(           (yield cg.get_variable(config[CONF_MODE])) ))
    cg.add(var.set_hvac_action(           (yield cg.get_variable(config[CONF_ACTION])) ))
    cg.add(var.set_battery_level_sensor(  (yield cg.get_variable(config[CONF_BATTERY_LEVEL])) ))

    # 新建并绑定输出（可选）
    if CONF_BATTERY_PERCENT in config:
        batt_out = yield sensor.new_sensor(config[CONF_BATTERY_PERCENT])
        cg.add(var.set_battery_percent_sensor(batt_out))

    if CONF_BATTERY_LOW in config:
        low_out = yield binary_sensor.new_binary_sensor(config[CONF_BATTERY_LOW])
        cg.add(var.set_battery_low_binary(low_out))
