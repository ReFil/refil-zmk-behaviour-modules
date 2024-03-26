/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_gpio_action

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>
#include <zmk/behavior.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

struct behavior_sarcasm_config {
    const struct gpio_dt_spec control;
    const uint8_t delay_ms;
};

struct behavior_sarcasm_data {
    bool active;
};

static int on_sarcasm_binding_pressed(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    const struct device *dev = device_get_binding(binding->behavior_dev);
    struct behavior_sarcasm_data *data = dev->data;

    if (data->active) {
        deactivate_sarcasm(dev);
    } else {
        activate_sarcasm(dev);
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_sarcasm_binding_released(struct zmk_behavior_binding *binding,
                                       struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_sarcasm_driver_api = {
    .binding_pressed = on_sarcasm_binding_pressed,
    .binding_released = on_sarcasm_binding_released,
};

static const struct device *devs[DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT)];

static int behavior_sarcasm_init(const struct device *dev) {
    const struct behavior_sarcasm_config *config = dev->config;
    devs[config->index] = dev;
    return 0;
}

#define KP_INST(n)                                                                                 \
    static struct behavior_sarcasm_data behavior_sarcasm_data_##n = {.active = false};             \
    static struct behavior_sarcasm_config behavior_sarcasm_config_##n = {                          \
        .index = n,                                                                                \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_sarcasm_init, NULL,                         \
                          &behavior_sarcasm_data_##n, &behavior_sarcasm_config_##n, APPLICATION,   \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_sarcasm_driver_api);

DT_INST_FOREACH_STATUS_OKAY(KP_INST)

#endif