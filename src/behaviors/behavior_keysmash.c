/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_keysmash

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zmk/behavior.h>
#include <zephyr/random/random.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

struct behavior_keysmash_config {
    int delay_ms;
};

static void zmk_keysmash_tick(struct k_work *work) {
    int key = (sys_rand32_get() % 26) + 4;
    raise_zmk_keycode_state_changed_from_encoded(key, true, k_uptime_get());
    k_msleep(5);
    raise_zmk_keycode_state_changed_from_encoded(key, false, k_uptime_get());
}

K_WORK_DEFINE(keysmash_work, zmk_keysmash_tick);

static void zmk_keysmash_tick_handler(struct k_timer *timer) { k_work_submit(&keysmash_work); }

K_TIMER_DEFINE(keysmash_tick, zmk_keysmash_tick_handler, NULL);

static int on_keysmash_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    const struct device *dev = device_get_binding(binding->behavior_dev);
    const struct behavior_keysmash_config *cfg = dev->config;

    k_timer_start(&keysmash_tick, K_NO_WAIT, K_MSEC(cfg->delay_ms));

    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_keysmash_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    k_timer_stop(&keysmash_tick);
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_keysmash_driver_api = {
    .binding_pressed = on_keysmash_binding_pressed,
    .binding_released = on_keysmash_binding_released,
};

static int behavior_keysmash_init(const struct device *dev) { return 0; }

static struct behavior_keysmash_config behavior_keysmash_config = {
    .delay_ms = DT_INST_PROP(0, delay_ms),
};

BEHAVIOR_DT_INST_DEFINE(0, behavior_keysmash_init, NULL, NULL, &behavior_keysmash_config,
                      POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
                      &behavior_keysmash_driver_api);

#endif