/*
 * MIT License
 *
 * Copyright (c) 2025 Konrad Rieck
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
#include "step_counter_face.h"
#include "watch.h"

/* Settings pages */
#define NUM_SETTINGS 3

/* Default settings */
#define DEFAULT_THRESHOLD 26500
#define DEFAULT_MIN_STEPS 0     /* 0 = disabled */
#define DEFAULT_MAX_STEPS 0     /* 0 = disabled */

static inline void _beep()
{
    if (!movement_button_should_sound())
        return;
    watch_buzzer_play_note(BUZZER_NOTE_C7, 50);
}

static void _settings_title_display(step_counter_state_t *state, char *buf1, char *buf2)
{
    char buf[10];
    watch_display_text_with_fallback(WATCH_POSITION_TOP, buf1, buf2);
    if (watch_get_lcd_type() != WATCH_LCD_TYPE_CUSTOM) {
        snprintf(buf, sizeof(buf), "%2d", state->settings_page + 1);
        watch_display_text_with_fallback(WATCH_POSITION_TOP_RIGHT, buf, buf);
    }
}

static bool _settings_blink(uint8_t subsecond)
{
    if (subsecond % 2 == 0) {
        watch_display_text_with_fallback(WATCH_POSITION_BOTTOM, "      ", "      ");
        return true;
    }
    return false;
}

static void _settings_threshold_display(void *context, uint8_t subsecond)
{
    char buf[10];
    step_counter_state_t *state = (step_counter_state_t *) context;

    _settings_title_display(state, "THRES", "TH");
    if (_settings_blink(subsecond))
        return;

    snprintf(buf, sizeof(buf), "%6d", state->threshold);
    watch_display_text_with_fallback(WATCH_POSITION_BOTTOM, buf, buf);
}

static void _settings_threshold_advance(void *context)
{
    step_counter_state_t *state = (step_counter_state_t *) context;

    /* Increment threshold by 500, with reasonable bounds */
    state->threshold += 500;

    /* Empirically determined bounds (22000 ~ 1g and 33000 ~ 1.5g) */
    if (state->threshold > 33000) {
        state->threshold = 22000;
    }
}

static void _settings_min_steps_display(void *context, uint8_t subsecond)
{
    char buf[10];
    step_counter_state_t *state = (step_counter_state_t *) context;

    _settings_title_display(state, "MINST", "MI");
    if (_settings_blink(subsecond))
        return;

    snprintf(buf, sizeof(buf), "%4d  ", state->min_steps);
    watch_display_text_with_fallback(WATCH_POSITION_BOTTOM, buf, buf);
}

static void _settings_min_steps_advance(void *context)
{
    step_counter_state_t *state = (step_counter_state_t *) context;

    /* Increment min_steps by 1 */
    state->min_steps++;

    /* Reset at 12 (~1 second) */
    if (state->min_steps > 12) {
        state->min_steps = 0;
    }
}

static void _settings_max_steps_display(void *context, uint8_t subsecond)
{
    char buf[10];
    step_counter_state_t *state = (step_counter_state_t *) context;

    _settings_title_display(state, "MAXST", "MA");
    if (_settings_blink(subsecond))
        return;

    snprintf(buf, sizeof(buf), "%4d  ", state->max_steps);
    watch_display_text_with_fallback(WATCH_POSITION_BOTTOM, buf, buf);
}

static void _settings_max_steps_advance(void *context)
{
    step_counter_state_t *state = (step_counter_state_t *) context;

    /* Increment max_steps by 5 */
    state->max_steps += 5;

    /* Reset at 60 (~5 seconds) */
    if (state->max_steps > 60) {
        state->max_steps = 0;
    }
}

static void _counter_display(step_counter_state_t *state)
{
    char buf[10];

    watch_display_text_with_fallback(WATCH_POSITION_TOP, "STEPS", "SC  ");

    /* Display step count */
    if (state->steps < 10000) {
        snprintf(buf, sizeof(buf), "%4lu  ", state->steps);
    } else {
        snprintf(buf, sizeof(buf), "%6lu", state->steps % 1000000);
    }
    watch_display_text_with_fallback(WATCH_POSITION_BOTTOM, buf, buf);
}

static void _switch_to_counter(step_counter_state_t *state)
{
    /* Switch to counter page */
    movement_request_tick_frequency(1);
    state->page = PAGE_COUNTER;
    watch_clear_colon();
    _counter_display(state);
}

static void _switch_to_settings(step_counter_state_t *state)
{
    /* Switch to settings page */
    movement_request_tick_frequency(4);
    watch_clear_colon();
    state->page = PAGE_SETTINGS;
    state->settings_page = 0;
    state->settings[state->settings_page].display(state, 0);
}

/* Reset state to default */
static void _reset_state(step_counter_state_t *state)
{
    state->subticks = 0;
    state->steps = 0;
    state->above_threshold = false;
    state->last_steps[0] = -state->min_steps;
    state->last_steps[1] = -state->min_steps;
}

/* Approximate l2 norm */
static uint32_t _approx_l2_norm(lis2dw_reading_t reading)
{
    /* Absolute values */
    uint32_t ax = abs(reading.x);
    uint32_t ay = abs(reading.y);
    uint32_t az = abs(reading.z);

    /* *INDENT-OFF* */
    /* Sort values: ax >= ay >= az */
    if (ax < ay) { uint32_t t = ax; ax = ay; ay = t; }
    if (ay < az) { uint32_t t = ay; ay = az; az = t; }
    if (ax < ay) { uint32_t t = ax; ax = ay; ay = t; }
    /* *INDENT-ON* */

    /* Approximate sqrt(x^2 + y^2 + z^2) */
    /* alpha ≈ 0.9375 (15/16), beta ≈ 0.375 (3/8) */
    return ax + ((15 * ay) >> 4) + ((3 * az) >> 3);
}

/* Print lis2dw status to console. */
static void _lis2dw_print_state(void)
{
    printf("LIS2DW status:\n");
    printf("  Power mode:\t%x\n", lis2dw_get_mode());
    printf("  Data rate:\t%x\n", lis2dw_get_data_rate());
    printf("  LP mode:\t%x\n", lis2dw_get_low_power_mode());
    printf("  BW filter:\t%x\n", lis2dw_get_bandwidth_filtering());
    printf("  Range:\t%x \n", lis2dw_get_range());
    printf("  Filter type:\t%x\n", lis2dw_get_filter_type());
    printf("  Low noise:\t%x\n", lis2dw_get_low_noise_mode());
    printf("\n");
}

static void _detect_steps(step_counter_state_t *state)
{
    lis2dw_fifo_t fifo;
    bool step_too_short, step1_too_long, step2_too_long;

    lis2dw_read_fifo(&fifo);
    if (fifo.count == 0) {
        return;
    }

    for (uint8_t i = 0; i < fifo.count; i++) {

        /* Calculate magnitude of acceleration */
        uint32_t mag = _approx_l2_norm(fifo.readings[i]);

        /* Check if we are crossing the threshold upward */
        if (mag > state->threshold && !state->above_threshold) {
            /* Check if enough time has passed since last step */
            if (state->min_steps > 0) {
                step_too_short = (state->subticks - state->last_steps[0]) < state->min_steps;
            } else {
                step_too_short = false;
            }

            if (!step_too_short) {
                /* Count step */
                state->steps++;
                state->above_threshold = true;

                /* Shift step history: new step becomes most recent */
                state->last_steps[1] = state->last_steps[0];
                state->last_steps[0] = state->subticks;
            }
        }
        /* Reset threshold flag when magnitude drops below threshold */
        else if (mag < state->threshold && state->above_threshold) {
            state->above_threshold = false;
        }

        /* Check if we should remove a step due to timing constraints */
        if (state->max_steps > 0) {
            step1_too_long = (state->subticks - state->last_steps[0]) > state->max_steps;
            step2_too_long = (state->last_steps[0] - state->last_steps[1]) > state->max_steps;

            if (step1_too_long && step2_too_long) {
                printf("Step too long\n");
                state->steps--;
                state->last_steps[0] = state->last_steps[1];
            }
        }

        /* Increment subticks */
        state->subticks += 1;
    }

    lis2dw_clear_fifo();
}

static bool _counter_loop(movement_event_t event, void *context)
{
    step_counter_state_t *state = (step_counter_state_t *) context;
    watch_date_time_t now;

    switch (event.event_type) {
        case EVENT_ACTIVATE:
            _counter_display(state);
            break;
        case EVENT_TICK:
            now = movement_get_local_date_time();
            if (now.unit.hour == 0 && now.unit.minute == 0 && now.unit.second == 0) {
                _reset_state(state);
            }
            _detect_steps(state);
            _counter_display(state);
            break;
        case EVENT_LIGHT_LONG_PRESS:
            _switch_to_settings(state);
            _beep();
            break;
        case EVENT_LIGHT_BUTTON_DOWN:
            /* Do nothing. */
            break;
        default:
            movement_default_loop_handler(event);
            break;
    }

    return true;
}

static bool _settings_loop(movement_event_t event, void *context)
{
    step_counter_state_t *state = (step_counter_state_t *) context;

    switch (event.event_type) {
        case EVENT_ACTIVATE:
        case EVENT_TICK:
            state->settings[state->settings_page].display(context, event.subsecond);
            break;
        case EVENT_LIGHT_BUTTON_UP:
            /* Go to next settings page */
            state->settings_page = (state->settings_page + 1) % NUM_SETTINGS;
            state->settings[state->settings_page].display(context, event.subsecond);
            _beep();
            break;
        case EVENT_ALARM_BUTTON_UP:
            /* Advance current settings */
            state->settings[state->settings_page].advance(context);
            state->settings[state->settings_page].display(context, event.subsecond);
            break;
        case EVENT_LIGHT_BUTTON_DOWN:
            /* Do nothing. */
            break;
        case EVENT_MODE_BUTTON_UP:
            /* Exit settings and return to step counter */
            _reset_state(state);
            _switch_to_counter(state);
            _beep();
            break;
        default:
            movement_default_loop_handler(event);
            break;
    }
    return true;
}

void step_counter_face_setup(uint8_t watch_face_index, void **context_ptr)
{
    (void) watch_face_index;
    step_counter_state_t *state;

    /* Initialize state */
    if (*context_ptr == NULL) {
        *context_ptr = malloc(sizeof(step_counter_state_t));
        memset(*context_ptr, 0, sizeof(step_counter_state_t));
        state = (step_counter_state_t *) * context_ptr;

        /* Default setup */
        state->threshold = DEFAULT_THRESHOLD;
        state->min_steps = DEFAULT_MIN_STEPS;
        state->max_steps = DEFAULT_MAX_STEPS;

        /* Reset state */
        _reset_state(state);
    }

    /* Initialize settings */
    state = (step_counter_state_t *) * context_ptr;
    if (state->settings == NULL) {
        uint8_t settings_page = 0;
        state->settings = malloc(NUM_SETTINGS * sizeof(step_counter_settings_t));
        state->settings[settings_page].display = _settings_threshold_display;
        state->settings[settings_page].advance = _settings_threshold_advance;
        settings_page++;
        state->settings[settings_page].display = _settings_min_steps_display;
        state->settings[settings_page].advance = _settings_min_steps_advance;
        settings_page++;
        state->settings[settings_page].display = _settings_max_steps_display;
        state->settings[settings_page].advance = _settings_max_steps_advance;
    }

    /* Set up accelerometer */
    movement_set_accelerometer_background_rate(LIS2DW_DATA_RATE_12_5_HZ);
    _lis2dw_print_state();

    /* Enable fifo and clear it. */
    lis2dw_enable_fifo();
    lis2dw_clear_fifo();
}

void step_counter_face_activate(void *context)
{
    step_counter_state_t *state = (step_counter_state_t *) context;

    /* Switch to counter page. */
    _switch_to_counter(state);
}

bool step_counter_face_loop(movement_event_t event, void *context)
{
    step_counter_state_t *state = (step_counter_state_t *) context;

    switch (state->page) {
        default:
        case PAGE_COUNTER:
            return _counter_loop(event, context);
        case PAGE_SETTINGS:
            return _settings_loop(event, context);
    }
}

void step_counter_face_resign(void *context)
{
    (void) context;

#if 0
    step_counter_state_t *state = (step_counter_state_t *) context;
    /* Free allocated memory */
    if (state->settings != NULL) {
        free(state->settings);
        state->settings = NULL;
    }
#endif
}

movement_watch_face_advisory_t step_counter_face_advise(void *context)
{
    (void) context;
    movement_watch_face_advisory_t retval = { 0 };
    return retval;
}
