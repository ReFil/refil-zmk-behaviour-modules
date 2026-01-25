/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_key_release

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zmk/keys.h>
#include <dt-bindings/zmk/keys.h>
#include <zephyr/logging/log.h>
#include <zmk/behavior.h>
#include <zmk/matrix.h>
#include <zmk/endpoints.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/behavior.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#define ZMK_BHV_KEY_RELEASE_MAX_HELD 10

// increase if you have keyboard with more keys.
#define ZMK_BHV_KEY_RELEASE_POSITION_NOT_USED 9999

struct behavior_key_release_config {
    char *press_behavior_dev;
    char *release_behavior_dev;
    int press_tap_ms;
    int release_tap_ms;
};

struct behavior_key_release_data {
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    struct behavior_parameter_metadata_set set;
#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
};

// this data is specific for each hold-tap
struct active_key_release {
    int32_t position;
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
    uint8_t source;
#endif
    uint32_t param_press;
    uint32_t param_release;
    int64_t timestamp;
    const struct behavior_key_release_config *config;
    struct k_work_delayable press_work;
    struct k_work_delayable release_work;
    bool work_is_cancelled;
};

struct active_key_release *undecided_key_release = NULL;
struct active_key_release active_key_releases[ZMK_BHV_KEY_RELEASE_MAX_HELD] = {};

static struct active_key_release *
store_key_release(struct zmk_behavior_binding_event *event, uint32_t param_press,
                  uint32_t param_release, const struct behavior_key_release_config *config) {
    for (int i = 0; i < ZMK_BHV_KEY_RELEASE_MAX_HELD; i++) {
        if (active_key_releases[i].position != ZMK_BHV_KEY_RELEASE_POSITION_NOT_USED) {
            continue;
        }
        active_key_releases[i].position = event->position;
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
        active_key_releases[i].source = event->source;
#endif
        active_key_releases[i].config = config;
        active_key_releases[i].param_press = param_press;
        active_key_releases[i].param_release = param_release;
        active_key_releases[i].timestamp = event->timestamp;
        return &active_key_releases[i];
    }
    return NULL;
}

static struct active_key_release *find_key_release(uint32_t position) {
    for (int i = 0; i < ZMK_BHV_KEY_RELEASE_MAX_HELD; i++) {
        if (active_key_releases[i].position == position) {
            return &active_key_releases[i];
        }
    }
    return NULL;
}

static void clear_key_release(struct active_key_release *key_release) {
    key_release->position = ZMK_BHV_KEY_RELEASE_POSITION_NOT_USED;
    key_release->work_is_cancelled = false;
}

static int press_press_binding(struct active_key_release *key_release) {
    struct zmk_behavior_binding_event event = {
        .position = key_release->position,
        .timestamp = key_release->timestamp,
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
        .source = key_release->source,
#endif
    };
    LOG_DBG("Press press binding for position %d", key_release->position);

    struct zmk_behavior_binding binding = {.behavior_dev = key_release->config->press_behavior_dev,
                                           .param1 = key_release->param_press};
    return zmk_behavior_invoke_binding(&binding, event, true);
}

static int press_release_binding(struct active_key_release *key_release) {
    struct zmk_behavior_binding_event event = {
        .position = key_release->position,
        .timestamp = key_release->timestamp,
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
        .source = key_release->source,
#endif
    };
    LOG_DBG("Press release binding for position %d", key_release->position);
    struct zmk_behavior_binding binding = {.behavior_dev =
                                               key_release->config->release_behavior_dev,
                                           .param1 = key_release->param_release};
    return zmk_behavior_invoke_binding(&binding, event, true);
}

static int release_press_binding(struct active_key_release *key_release) {
    struct zmk_behavior_binding_event event = {
        .position = key_release->position,
        .timestamp = key_release->timestamp,
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
        .source = key_release->source,
#endif
    };
    LOG_DBG("Release press binding for position %d", key_release->position);
    struct zmk_behavior_binding binding = {.behavior_dev = key_release->config->press_behavior_dev,
                                           .param1 = key_release->param_press};
    return zmk_behavior_invoke_binding(&binding, event, false);
}

static int release_release_binding(struct active_key_release *key_release) {
    struct zmk_behavior_binding_event event = {
        .position = key_release->position,
        .timestamp = key_release->timestamp,
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
        .source = key_release->source,
#endif
    };
    LOG_DBG("Release release binding for position %d", key_release->position);
    struct zmk_behavior_binding binding = {.behavior_dev =
                                               key_release->config->release_behavior_dev,
                                           .param1 = key_release->param_release};
    return zmk_behavior_invoke_binding(&binding, event, false);
}

static int on_key_release_binding_pressed(struct zmk_behavior_binding *binding,
                                          struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_key_release_config *cfg = dev->config;

    struct active_key_release *key_release =
        store_key_release(&event, binding->param1, binding->param2, cfg);

    if (key_release == NULL) {
        LOG_ERR("unable to store hold-tap info, did you press more than %d hold-taps?",
                ZMK_BHV_KEY_RELEASE_MAX_HELD);
        return ZMK_BEHAVIOR_OPAQUE;
    }

    press_press_binding(key_release);

    // if this behavior was queued we have to adjust the timer to only
    // wait for the remaining time
    k_work_schedule(&key_release->press_work, K_MSEC(cfg->press_tap_ms));

    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_key_release_binding_released(struct zmk_behavior_binding *binding,
                                           struct zmk_behavior_binding_event event) {

    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_key_release_config *cfg = dev->config;
    struct active_key_release *key_release = find_key_release(event.position);
    if (key_release == NULL) {
        LOG_ERR("ACTIVE_KEY_RELEASE_CLEANED_UP_TOO_EARLY");
        return ZMK_BEHAVIOR_OPAQUE;
    }

    // Try and cancel timer if it's still pending
    if (k_work_delayable_is_pending(&key_release->press_work)) {
        int work_cancel_result = k_work_cancel_delayable(&key_release->press_work);
        if (work_cancel_result == -EINPROGRESS) {
            // let the timer handler clean up
            // if we'd clear now, the timer may call back for an uninitialized active_key_release.
            LOG_DBG("%d key-release timer work in event queue", event.position);
            key_release->work_is_cancelled = true;
        } else {
            release_press_binding(key_release);
        }
    } else {
        release_press_binding(key_release);
    }

    press_release_binding(key_release);

    // if this behavior was queued we have to adjust the timer to only
    // wait for the remaining time
    k_work_schedule(&key_release->release_work, K_MSEC(cfg->release_tap_ms));

    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_key_release_driver_api = {
    .binding_pressed = on_key_release_binding_pressed,
    .binding_released = on_key_release_binding_released,
};

void behavior_key_release_timer_press_work_handler(struct k_work *item) {
    struct k_work_delayable *d_work = k_work_delayable_from_work(item);
    struct active_key_release *key_release =
        CONTAINER_OF(d_work, struct active_key_release, press_work);

    release_press_binding(key_release);
}

void behavior_key_release_timer_release_work_handler(struct k_work *item) {
    struct k_work_delayable *d_work = k_work_delayable_from_work(item);
    struct active_key_release *key_release =
        CONTAINER_OF(d_work, struct active_key_release, release_work);

    release_release_binding(key_release);
    clear_key_release(key_release);
}

static int behavior_key_release_init(const struct device *dev) {
    static bool init_first_run = true;

    if (init_first_run) {
        for (int i = 0; i < ZMK_BHV_KEY_RELEASE_MAX_HELD; i++) {
            k_work_init_delayable(&active_key_releases[i].press_work,
                                  behavior_key_release_timer_press_work_handler);
            k_work_init_delayable(&active_key_releases[i].release_work,
                                  behavior_key_release_timer_release_work_handler);
            active_key_releases[i].position = ZMK_BHV_KEY_RELEASE_POSITION_NOT_USED;
        }
    }
    init_first_run = false;
    return 0;
}

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
static int key_release_parameter_metadata(const struct device *key_release,
                                          struct behavior_parameter_metadata *param_metadata) {
    const struct behavior_key_release_config *cfg = key_release->config;
    struct behavior_key_release_data *data = key_release->data;
    int err;
    struct behavior_parameter_metadata child_meta;

    err = behavior_get_parameter_metadata(zmk_behavior_get_binding(cfg->press_behavior_dev),
                                          &child_meta);
    if (err < 0) {
        LOG_WRN("Failed to get the press behavior parameter: %d", err);
        return err;
    }

    if (child_meta.sets_len > 0) {
        data->set.param1_values = child_meta.sets[0].param1_values;
        data->set.param1_values_len = child_meta.sets[0].param1_values_len;
    }

    err = behavior_get_parameter_metadata(zmk_behavior_get_binding(cfg->release_behavior_dev),
                                          &child_meta);
    if (err < 0) {
        LOG_WRN("Failed to get the release behavior parameter: %d", err);
        return err;
    }

    if (child_meta.sets_len > 0) {
        data->set.param2_values = child_meta.sets[0].param1_values;
        data->set.param2_values_len = child_meta.sets[0].param1_values_len;
    }

    param_metadata->sets = &data->set;
    param_metadata->sets_len = 1;

    return 0;
}

#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)

#define KP_INST(n)                                                                                 \
    static const struct behavior_key_release_config behavior_key_release_config_##n = {            \
        .press_behavior_dev = DEVICE_DT_NAME(DT_INST_PHANDLE_BY_IDX(n, bindings, 0)),              \
        .release_behavior_dev = DEVICE_DT_NAME(DT_INST_PHANDLE_BY_IDX(n, bindings, 1)),            \
        .press_tap_ms = DT_INST_PROP(n, press_tap_ms),                                             \
        .release_tap_ms = DT_INST_PROP(n, release_tap_ms),                                         \
    };                                                                                             \
    static struct behavior_key_release_data behavior_key_release_data_##n = {};                    \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_key_release_init, NULL, &behavior_key_release_data_##n,    \
                            &behavior_key_release_config_##n, POST_KERNEL,                         \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                                   \
                            &behavior_key_release_driver_api);

DT_INST_FOREACH_STATUS_OKAY(KP_INST)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
