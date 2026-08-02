/*
 * MIT License
 *
 * Copyright (c) 2022 Joey Castillo
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdlib.h>
#include <string.h>
#include "accelerometer_status_face.h"
#include "lis2dw.h"
#include "watch.h"

/* Default motion threshold: 16 = 0.5 G at +/-2g.
   This is lower than the default 1G used in movement core because it should
   track general motion and not just taps on the watch */
#define ACCEL_DEFAULT_THRESHOLD 16

/* Settings pages */
typedef enum {
    ACCEL_SETTING_THRESHOLD = 0,
    ACCEL_SETTING_WAKE,
} accel_setting_t;

#define ACCEL_NUM_SETTINGS (ACCEL_SETTING_WAKE + 1)

static void _status_display(void) {
    watch_display_text_with_fallback(WATCH_POSITION_TOP, "ACCEL", "AC");
    watch_set_indicator(WATCH_INDICATOR_SIGNAL);

    // the motion pin reads HIGH when still at rest, LOW when active
    if (HAL_GPIO_A4_read()) watch_display_text(WATCH_POSITION_BOTTOM, "Still ");
    else watch_display_text_with_fallback(WATCH_POSITION_BOTTOM, "Active", " ACtiv");
}

static void _settings_title(accel_interrupt_count_state_t *state, const char *primary, const char *fallback) {
    watch_display_text_with_fallback(WATCH_POSITION_TOP, primary, fallback);
    if (watch_get_lcd_type() != WATCH_LCD_TYPE_CUSTOM) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%2d", state->settings_page + 1);
        watch_display_text(WATCH_POSITION_TOP_RIGHT, buf);
    }
}

static bool _settings_blink(uint8_t subsecond) {
    if (subsecond % 2 == 0) {
        watch_display_text(WATCH_POSITION_BOTTOM, "      ");
        watch_clear_decimal_if_available();
        return true;
    }
    return false;
}

static void _settings_display(accel_interrupt_count_state_t *state, uint8_t subsecond) {
    watch_clear_indicator(WATCH_INDICATOR_SIGNAL);

    switch (state->settings_page) {
        case ACCEL_SETTING_THRESHOLD:
            _settings_title(state, "THRSH", "TH");
            if (_settings_blink(subsecond)) return;
            watch_display_float_with_best_effort(state->new_threshold * 0.03125f, " G");
            break;
        case ACCEL_SETTING_WAKE:
            _settings_title(state, "WAKE ", "WA");
            if (_settings_blink(subsecond)) return;
            watch_clear_decimal_if_available();
            watch_display_text(WATCH_POSITION_BOTTOM, movement_get_wake_on_motion() ? "  on  " : "  off ");
            break;
    }
}

static void _settings_advance(accel_interrupt_count_state_t *state) {
    switch (state->settings_page) {
        case ACCEL_SETTING_THRESHOLD:
            state->new_threshold = (state->new_threshold + 1) % 64;
            movement_set_accelerometer_motion_threshold(state->new_threshold);
            state->threshold = state->new_threshold;
            break;
        case ACCEL_SETTING_WAKE:
            movement_set_wake_on_motion(!movement_get_wake_on_motion());
            break;
    }
}

void accelerometer_status_face_setup(uint8_t watch_face_index, void ** context_ptr) {
    (void) watch_face_index;
    if (*context_ptr == NULL) {
        *context_ptr = malloc(sizeof(accel_interrupt_count_state_t));
        memset(*context_ptr, 0, sizeof(accel_interrupt_count_state_t));

        /* Set up accelerometer and enable background sensing */
        movement_set_accelerometer_background_rate(LIS2DW_DATA_RATE_LOWEST);
        movement_set_accelerometer_motion_threshold(ACCEL_DEFAULT_THRESHOLD);
    }
}

void accelerometer_status_face_activate(void *context) {
    accel_interrupt_count_state_t *state = (accel_interrupt_count_state_t *)context;

    state->is_setting = false;
    state->settings_page = 0;

    movement_request_tick_frequency(4);

    state->threshold = movement_get_accelerometer_motion_threshold();
}

bool accelerometer_status_face_loop(movement_event_t event, void *context) {
    accel_interrupt_count_state_t *state = (accel_interrupt_count_state_t *)context;

    if (state->is_setting) {
        switch (event.event_type) {
            case EVENT_ACTIVATE:
            case EVENT_TICK:
                _settings_display(state, event.subsecond);
                break;
            case EVENT_LIGHT_BUTTON_UP:
                state->settings_page = (state->settings_page + 1) % ACCEL_NUM_SETTINGS;
                _settings_display(state, event.subsecond);
                break;
            case EVENT_ALARM_BUTTON_UP:
                _settings_advance(state);
                _settings_display(state, event.subsecond);
                break;
            case EVENT_MODE_BUTTON_UP:
                state->is_setting = false;
                watch_clear_decimal_if_available();
                _status_display();
                break;
            case EVENT_TIMEOUT:
                movement_move_to_face(0);
                break;
            default:
                movement_default_loop_handler(event);
                break;
        }
    } else {
        switch (event.event_type) {
            case EVENT_ACTIVATE:
            case EVENT_TICK:
                _status_display();
                break;
            case EVENT_LOW_ENERGY_UPDATE:
                // start tick animation if necessary
                if (!watch_sleep_animation_is_running()) watch_start_sleep_animation(1000);
                _status_display();
                // on classic LCD, clear seconds since they interfere with the sleep animation.
                if (watch_get_lcd_type() == WATCH_LCD_TYPE_CLASSIC) watch_display_text(WATCH_POSITION_SECONDS, "  ");
                break;
            case EVENT_LIGHT_LONG_PRESS:
                state->is_setting = true;
                state->settings_page = 0;
                state->new_threshold = state->threshold;
                _settings_display(state, event.subsecond);
                break;
            default:
                movement_default_loop_handler(event);
                break;
        }
    }

    return true;
}

void accelerometer_status_face_resign(void *context) {
    (void)context;
}
