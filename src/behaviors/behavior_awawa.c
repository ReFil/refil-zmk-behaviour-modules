/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_awawa

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

struct behavior_awawa_config {
    int delay_ms;
};

static void zmk_awawa_tick(struct k_work *work) {
    // Press and release A
    raise_zmk_keycode_state_changed_from_encoded(0x04, true, k_uptime_get());
    k_msleep(2);
    raise_zmk_keycode_state_changed_from_encoded(0x04, false, k_uptime_get());
    // Press and release W
    raise_zmk_keycode_state_changed_from_encoded(0x1a, true, k_uptime_get());
    k_msleep(2);
    raise_zmk_keycode_state_changed_from_encoded(0x1a, false, k_uptime_get());
}

K_WORK_DEFINE(awawa_work, zmk_awawa_tick);

static void zmk_awawa_tick_handler(struct k_timer *timer) { k_work_submit(&awawa_work); }

K_TIMER_DEFINE(awawa_tick, zmk_awawa_tick_handler, NULL);

static int on_awawa_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    const struct device *dev = device_get_binding(binding->behavior_dev);
    const struct behavior_awawa_config *cfg = dev->config;

    k_timer_start(&awawa_tick, K_NO_WAIT, K_MSEC(cfg->delay_ms));

    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_awawa_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    k_timer_stop(&awawa_tick);
    raise_zmk_keycode_state_changed_from_encoded(0x04, true, k_uptime_get());
    k_msleep(2);
    raise_zmk_keycode_state_changed_from_encoded(0x04, false, k_uptime_get());
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_awawa_driver_api = {
    .binding_pressed = on_awawa_binding_pressed,
    .binding_released = on_awawa_binding_released,
};

static int behavior_awawa_init(const struct device *dev) { return 0; }

static struct behavior_awawa_config behavior_awawa_config = {
    .delay_ms = DT_INST_PROP(0, delay_ms),
};

BEHAVIOR_DT_INST_DEFINE(0, behavior_awawa_init, NULL, NULL, &behavior_awawa_config,
                      POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
                      &behavior_awawa_driver_api);

#endif