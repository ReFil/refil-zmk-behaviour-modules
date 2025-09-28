/*
 * Copyright (c) 2025 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_sensor_rotate_pos_trig

#include <zephyr/device.h>

#include <drivers/behavior.h>


#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>

#include <zmk/behavior_queue.h>
#include <zmk/virtual_key_position.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/keymap.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct behavior_sensor_rotate_pos_trig_config {
    int pos_inc;
    int pos_dec;
    int tap_ms;
};

struct position_trigger_work_data {
    int position;
    struct k_work_delayable work;
    const struct device *dev;
};

static void position_trigger_work_cb(struct k_work *work) {
    struct k_work_delayable *dwork = CONTAINER_OF(work, struct k_work_delayable, work);
    struct position_trigger_work_data *data = CONTAINER_OF(dwork, struct position_trigger_work_data, work);
    LOG_DBG("Raise Poition %d", data->position);

    raise_zmk_position_state_changed(
            (struct zmk_position_state_changed){.source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
                                                .state = false,
                                                .position = data->position,
                                                .timestamp = k_uptime_get()});

}

struct behavior_sensor_rotate_pos_trig_data {
    struct sensor_value remainder[ZMK_KEYMAP_SENSORS_LEN];
    int triggers[ZMK_KEYMAP_SENSORS_LEN];
    struct position_trigger_work_data work_data [ZMK_KEYMAP_SENSORS_LEN];
};

int zmk_behavior_sensor_rotate_pos_trig_accept_data(
    struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event,
    const struct zmk_sensor_config *sensor_config, size_t channel_data_size,
    const struct zmk_sensor_channel_data *channel_data) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    struct behavior_sensor_rotate_pos_trig_data *data = dev->data;

    const struct sensor_value value = channel_data[0].value;
    int triggers;
    int sensor_index = ZMK_SENSOR_POSITION_FROM_VIRTUAL_KEY_POSITION(event.position);

    // Some funky special casing for "old encoder behavior" where ticks where reported in val2 only,
    // instead of rotational degrees in val1.
    // REMOVE ME: Remove after a grace period of old ec11 sensor behavior
    if (value.val1 == 0) {
        triggers = value.val2;
    } else {
        struct sensor_value remainder = data->remainder[sensor_index];

        remainder.val1 += value.val1;
        remainder.val2 += value.val2;

        if (remainder.val2 >= 1000000 || remainder.val2 <= 1000000) {
            remainder.val1 += remainder.val2 / 1000000;
            remainder.val2 %= 1000000;
        }

        int trigger_degrees = 360 / sensor_config->triggers_per_rotation;
        triggers = remainder.val1 / trigger_degrees;
        remainder.val1 %= trigger_degrees;

        data->remainder[sensor_index] = remainder;
    }

    LOG_DBG(
        "val1: %d, val2: %d, remainder: %d/%d triggers: %d ",
        value.val1, value.val2, data->remainder[sensor_index].val1,
        data->remainder[sensor_index].val2, triggers);

    data->triggers[sensor_index] = triggers;
    return 0;
}

int zmk_behavior_sensor_rotate_pos_trig_process(struct zmk_behavior_binding *binding,
                                              struct zmk_behavior_binding_event event,
                                              enum behavior_sensor_binding_process_mode mode) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_sensor_rotate_pos_trig_config *cfg = dev->config;
    struct behavior_sensor_rotate_pos_trig_data *data = dev->data;

    const int sensor_index = ZMK_SENSOR_POSITION_FROM_VIRTUAL_KEY_POSITION(event.position);

    if (mode != BEHAVIOR_SENSOR_BINDING_PROCESS_MODE_TRIGGER) {
        data->triggers[sensor_index] = 0;
        return ZMK_BEHAVIOR_TRANSPARENT;
    }

    int triggers = data->triggers[sensor_index];

    int triggered_position = 0;
    if (triggers > 0) {
        triggered_position = cfg->pos_inc;
        
    } else if (triggers < 0) {
        triggers = -triggers;
        triggered_position = cfg->pos_dec;
    } else {
        return ZMK_BEHAVIOR_TRANSPARENT;
    }

    for (int i = 0; i < triggers; i++) {
        raise_zmk_position_state_changed(
            (struct zmk_position_state_changed){.source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
                                                .state = true,
                                                .position = triggered_position,
                                                .timestamp = k_uptime_get()});

        data->work_data[sensor_index].position = triggered_position;

        k_work_schedule(&data->work_data[sensor_index].work, K_MSEC(cfg->tap_ms));
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

int behavior_sensor_rotate_pos_trig_init(const struct device *dev){
    struct behavior_sensor_rotate_pos_trig_data *data = dev->data;
    for (int i = 0; i < ZMK_KEYMAP_SENSORS_LEN; i++)
    {
        k_work_init_delayable(&data->work_data[i].work, position_trigger_work_cb);
    }
    
    return 0;
}

static const struct behavior_driver_api behavior_sensor_rotate_pos_trig_driver_api = {
    .sensor_binding_accept_data = zmk_behavior_sensor_rotate_pos_trig_accept_data,
    .sensor_binding_process = zmk_behavior_sensor_rotate_pos_trig_process};

#define SENSOR_ROTATE_POS_TRIG_INST(n)                                                                  \
    static struct behavior_sensor_rotate_pos_trig_config behavior_sensor_rotate_pos_trig_config_##n = {          \
        .pos_inc = DT_INST_PROP(n, pos_inc),    \
        .pos_dec = DT_INST_PROP(n, pos_dec),    \
        .tap_ms = DT_INST_PROP(n, tap_ms),                                                         \
    };                                                                                             \
    static struct behavior_sensor_rotate_pos_trig_data behavior_sensor_rotate_pos_trig_data_##n = {};            \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_sensor_rotate_pos_trig_init, NULL, &behavior_sensor_rotate_pos_trig_data_##n,                   \
                            &behavior_sensor_rotate_pos_trig_config_##n, POST_KERNEL,                   \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                                   \
                            &behavior_sensor_rotate_pos_trig_driver_api);

DT_INST_FOREACH_STATUS_OKAY(SENSOR_ROTATE_POS_TRIG_INST)