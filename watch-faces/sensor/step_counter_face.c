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
#define SAMPLING_RATE 25        /* Sampling rate in Hz */
#define DEFAULT_WIN_BITS 4      /* Window size is 2^4 = 16 samples */
#define DEFAULT_THRESHOLD 28    /* Threshold for step detection */
#define DEFAULT_MAX_STEP 3      /* Maximum duration of a step */

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

    snprintf(buf, sizeof(buf), "%4d  ", state->threshold);
    watch_display_text_with_fallback(WATCH_POSITION_BOTTOM, buf, buf);
}

static void _settings_threshold_advance(void *context)
{
    step_counter_state_t *state = (step_counter_state_t *) context;
    state->threshold++;

    if (state->threshold > 50) {
        state->threshold = 20;
    }
}

static void _settings_max_step_display(void *context, uint8_t subsecond)
{
    char buf[10];
    step_counter_state_t *state = (step_counter_state_t *) context;

    _settings_title_display(state, "MAXST", "MS");
    if (_settings_blink(subsecond))
        return;

    snprintf(buf, sizeof(buf), "%4d  ", state->max_step);
    watch_display_text_with_fallback(WATCH_POSITION_BOTTOM, buf, buf);
}

static void _settings_max_step_advance(void *context)
{
    step_counter_state_t *state = (step_counter_state_t *) context;
    state->max_step++;

    if (state->max_step > 10) {
        state->max_step = 1;
    }
}

static void _settings_win_bits_display(void *context, uint8_t subsecond)
{
    char buf[10];
    step_counter_state_t *state = (step_counter_state_t *) context;

    _settings_title_display(state, "WINBI", "WB");
    if (_settings_blink(subsecond))
        return;

    snprintf(buf, sizeof(buf), "%4d  ", state->win_bits);
    watch_display_text_with_fallback(WATCH_POSITION_BOTTOM, buf, buf);
}

static void _settings_win_bits_advance(void *context)
{
    step_counter_state_t *state = (step_counter_state_t *) context;
    state->win_bits <<= 1;

    if (state->win_bits == 64) {
        state->win_bits = 4;
    }
}

static void _counter_display(step_counter_state_t *state)
{
    char buf[10];

    watch_display_text_with_fallback(WATCH_POSITION_TOP_LEFT, "STEPS", "SC");

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

    /* Clear display */
    watch_clear_colon();
    watch_display_text_with_fallback(WATCH_POSITION_TOP_RIGHT, "  ", "  ");

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
    state->steps = 0;

    /* Empty buffer */
    memset(state->buffer, 0, sizeof(state->buffer));
    state->buffer_idx = 0;
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

static int8_t _record_data(step_counter_state_t *state)
{
    lis2dw_fifo_t fifo;

    lis2dw_read_fifo(&fifo);
    int8_t count = fifo.count;

    printf("Recording %d samples to %d\n", count, state->buffer_idx);

    /* Record data */
    for (uint8_t i = 0; i < count; i++) {
        /* Calculate magnitude of acceleration */
        uint32_t mag = _approx_l2_norm(fifo.readings[i]);
        /* Clamp magnitude to 16 bits and scale down to 8 bits */
        mag = (mag > 0xffff) ? 0xffff : mag;
        mag >>= 8;

        /* Add to buffer */
        state->buffer[state->buffer_idx] = mag;
        state->buffer_idx++;
    }
    lis2dw_clear_fifo();

    return count;
}

static void _detect_steps(step_counter_state_t *state)
{
    uint8_t *window;
    uint8_t window_idx = 0;
    uint32_t window_sum = 0;
    uint8_t above_threshold = 0;

    printf("Detecting steps in %d samples\n", state->buffer_idx);
    window = malloc((1 << state->win_bits) * sizeof(uint8_t));
    if (window == NULL) {
        printf("Failed to allocate window\n");
        return;
    }

    /* Count step */
    state->steps++;

    for (uint8_t i = 0; i < state->buffer_idx; i++) {

        /* Add current value to window and sum */
        window[window_idx] = state->buffer[i];
        window_sum += window[window_idx];
        window_idx = (window_idx + 1) % sizeof(window);

        /* Wait until window is full */
        if (i < sizeof(window)) {
            continue;
        }

        /* Remove oldest value from sum */
        window_sum -= window[window_idx];

        /* High pass filter: hp_value = current - mean */
        int32_t hp_value = state->buffer[i] - (window_sum >> state->win_bits);

        /* Detect step */
        if (hp_value > state->threshold && above_threshold == 0) {
            above_threshold = i;
        } else if (hp_value < state->threshold && above_threshold > 0) {
            /* Check for sudden drop in acceleration */
            if (i - above_threshold <= state->max_step) {
                state->steps++;
            }
            above_threshold = 0;
        }
    }

    /* Reset buffer index */
    state->buffer_idx = 0;

    /* Free window */
    free(window);
}

static bool _counter_loop(movement_event_t event, void *context)
{
    step_counter_state_t *state = (step_counter_state_t *) context;
    watch_date_time_t now;

    printf("Counter loop %d\n", event.event_type);

    switch (event.event_type) {
        case EVENT_ACTIVATE:
            _counter_display(state);
            break;
        case EVENT_TICK:
            /* Reset state at midnight */
            now = movement_get_local_date_time();
            if (now.unit.hour == 0 && now.unit.minute == 0 && now.unit.second == 0) {
                _reset_state(state);
            }

            /* Record data */
            int8_t count = _record_data(state);
            bool buffer_full = (sizeof(state->buffer) - state->buffer_idx) < SAMPLING_RATE;

            /* Run detection if buffer is full or no data is available anymore */
            if (count == 0 || buffer_full) {
                _detect_steps(state);
            }

            _counter_display(state);
            break;
        case EVENT_ALARM_LONG_PRESS:
            _switch_to_settings(state);
            _beep();
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

    printf("Setting up step counter face\n");

    /* Initialize state */
    if (*context_ptr == NULL) {
        *context_ptr = malloc(sizeof(step_counter_state_t));
        memset(*context_ptr, 0, sizeof(step_counter_state_t));
        state = (step_counter_state_t *) * context_ptr;

        /* Default setup */
        state->threshold = DEFAULT_THRESHOLD;
        state->max_step = DEFAULT_MAX_STEP;
        state->win_bits = DEFAULT_WIN_BITS;

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
        state->settings[settings_page].display = _settings_max_step_display;
        state->settings[settings_page].advance = _settings_max_step_advance;
        settings_page++;
        state->settings[settings_page].display = _settings_win_bits_display;
        state->settings[settings_page].advance = _settings_win_bits_advance;
        settings_page++;
    }

    /* Set up accelerometer */
    state->prev_rate = movement_get_accelerometer_background_rate();
    movement_set_accelerometer_background_rate(LIS2DW_DATA_RATE_25_HZ);
    _lis2dw_print_state();

    /* Enable fifo and clear it. */
    lis2dw_enable_fifo();
    lis2dw_clear_fifo();
}

void step_counter_face_activate(void *context)
{
    step_counter_state_t *state = (step_counter_state_t *) context;

    /* Switch to counter page. */
    printf("Activating step counter face\n");
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
    step_counter_state_t *state = (step_counter_state_t *) context;

    /* Disable accelerometer */
    movement_set_accelerometer_background_rate(state->prev_rate);
    _lis2dw_print_state();
}

movement_watch_face_advisory_t step_counter_face_advise(void *context)
{
    (void) context;
    movement_watch_face_advisory_t retval = { 0 };
    return retval;
}
