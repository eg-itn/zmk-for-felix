/*
 * Turbo click behavior.
 *
 * Port of getreuer's QMK "Mouse Turbo Click"
 * (https://getreuer.info/posts/keyboards/mouse-turbo-click):
 *
 *  - Holding the key taps the wrapped binding (e.g. &mkp LCLK) every period-ms.
 *  - Double-tapping the key locks it. Taps continue until the key is pressed again.
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_turbo_click

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <drivers/behavior.h>

#include <zmk/behavior.h>
#include <zmk/behavior_queue.h>
#include <zmk/keymap.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

struct behavior_turbo_click_config {
    struct zmk_behavior_binding binding;
    uint32_t period_ms;
    uint32_t tap_ms;
    uint32_t lock_tap_ms;
};

struct behavior_turbo_click_data {
    const struct device *dev;
    struct k_work_delayable work;
    struct zmk_behavior_binding_event event;
    bool locked;
    /* Set when the current press only unlocked the turbo click, so its release does nothing. */
    bool unlocking;
    int64_t press_time;
    /* Time of the last short tap's release, or 0 if the last press was a hold. */
    int64_t tap_release_time;
};

static void turbo_click_tick(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct behavior_turbo_click_data *data =
        CONTAINER_OF(dwork, struct behavior_turbo_click_data, work);
    const struct behavior_turbo_click_config *cfg = data->dev->config;

    /* The queue always delivers the release, so the button cannot get stuck when stopped. */
    zmk_behavior_queue_add(&data->event, cfg->binding, true, cfg->tap_ms);
    zmk_behavior_queue_add(&data->event, cfg->binding, false, 0);

    k_work_schedule(&data->work, K_MSEC(cfg->period_ms));
}

static void turbo_click_stop(struct behavior_turbo_click_data *data) {
    data->locked = false;
    k_work_cancel_delayable(&data->work);
}

static int on_turbo_click_binding_pressed(struct zmk_behavior_binding *binding,
                                          struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_turbo_click_config *cfg = dev->config;
    struct behavior_turbo_click_data *data = dev->data;

    if (data->locked) {
        turbo_click_stop(data);
        data->unlocking = true;
        data->tap_release_time = 0;
        return ZMK_BEHAVIOR_OPAQUE;
    }

    data->unlocking = false;
    data->press_time = event.timestamp;
    data->event = event;

    if (data->tap_release_time != 0 &&
        event.timestamp - data->tap_release_time < cfg->lock_tap_ms) {
        data->locked = true;
    }

    k_work_schedule(&data->work, K_NO_WAIT);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_turbo_click_binding_released(struct zmk_behavior_binding *binding,
                                           struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_turbo_click_config *cfg = dev->config;
    struct behavior_turbo_click_data *data = dev->data;

    if (data->unlocking) {
        data->unlocking = false;
        return ZMK_BEHAVIOR_OPAQUE;
    }

    if (data->locked) {
        /* The second tap of a double tap: keep clicking. */
        data->tap_release_time = 0;
        return ZMK_BEHAVIOR_OPAQUE;
    }

    k_work_cancel_delayable(&data->work);
    data->tap_release_time =
        (event.timestamp - data->press_time < cfg->lock_tap_ms) ? event.timestamp : 0;
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_turbo_click_driver_api = {
    .binding_pressed = on_turbo_click_binding_pressed,
    .binding_released = on_turbo_click_binding_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
};

static int behavior_turbo_click_init(const struct device *dev) {
    struct behavior_turbo_click_data *data = dev->data;

    data->dev = dev;
    k_work_init_delayable(&data->work, turbo_click_tick);
    return 0;
}

#define TURBO_CLICK_INST(n)                                                                        \
    BUILD_ASSERT(DT_INST_PROP(n, tap_ms) < DT_INST_PROP(n, period_ms),                             \
                 "turbo click: tap-ms must be shorter than period-ms");                            \
    static struct behavior_turbo_click_data behavior_turbo_click_data_##n = {};                    \
    static const struct behavior_turbo_click_config behavior_turbo_click_config_##n = {            \
        .binding = ZMK_KEYMAP_EXTRACT_BINDING(0, DT_DRV_INST(n)),                                  \
        .period_ms = DT_INST_PROP(n, period_ms),                                                   \
        .tap_ms = DT_INST_PROP(n, tap_ms),                                                         \
        .lock_tap_ms = DT_INST_PROP(n, lock_tap_ms),                                               \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_turbo_click_init, NULL, &behavior_turbo_click_data_##n,    \
                            &behavior_turbo_click_config_##n, POST_KERNEL,                         \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                                   \
                            &behavior_turbo_click_driver_api);

DT_INST_FOREACH_STATUS_OKAY(TURBO_CLICK_INST)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
