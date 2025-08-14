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
#define NUM_SETTINGS 4

/* Default settings */
#define SAMPLING_RATE 25        /* Sampling rate in Hz */
#define DEFAULT_WINDOW_BITS 4   /* Window size is 2^4 = 16 samples */
#define DEFAULT_THRESHOLD 15    /* Threshold for step detection */
#define DEFAULT_MAX_DURATION 4  /* Maximum duration of a step */
#define DEFAULT_MIN_INTERVAL 8  /* Minimum interval between steps */

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

    if (state->threshold > 40) {
        state->threshold = 10;
    }
}

static void _settings_max_duration_display(void *context, uint8_t subsecond)
{
    char buf[10];
    step_counter_state_t *state = (step_counter_state_t *) context;

    _settings_title_display(state, "MAXDU", "MD");
    if (_settings_blink(subsecond))
        return;

    snprintf(buf, sizeof(buf), "%4d  ", state->max_duration);
    watch_display_text_with_fallback(WATCH_POSITION_BOTTOM, buf, buf);
}

static void _settings_max_duration_advance(void *context)
{
    step_counter_state_t *state = (step_counter_state_t *) context;
    state->max_duration++;

    if (state->max_duration > 10) {
        state->max_duration = 1;
    }
}

static void _settings_min_interval_display(void *context, uint8_t subsecond)
{
    char buf[10];
    step_counter_state_t *state = (step_counter_state_t *) context;

    _settings_title_display(state, "MININ", "MI");
    if (_settings_blink(subsecond))
        return;

    snprintf(buf, sizeof(buf), "%4d  ", state->min_interval);
    watch_display_text_with_fallback(WATCH_POSITION_BOTTOM, buf, buf);
}

static void _settings_min_interval_advance(void *context)
{
    step_counter_state_t *state = (step_counter_state_t *) context;
    state->min_interval++;

    if (state->min_interval > 20) {
        state->min_interval = 1;
    }
}

static void _settings_window_bits_display(void *context, uint8_t subsecond)
{
    char buf[10];
    step_counter_state_t *state = (step_counter_state_t *) context;

    _settings_title_display(state, "WINSZ", "WS");
    if (_settings_blink(subsecond))
        return;

    snprintf(buf, sizeof(buf), "%4d  ", 1 << state->window_bits);
    watch_display_text_with_fallback(WATCH_POSITION_BOTTOM, buf, buf);
}

static void _settings_window_bits_advance(void *context)
{
    step_counter_state_t *state = (step_counter_state_t *) context;
    state->window_bits++;

    if (state->window_bits == 6) {
        state->window_bits = 1;
    }
}

static void _counter_display(step_counter_state_t *state, uint8_t counter)
{
    char buf[10];

    watch_display_text_with_fallback(WATCH_POSITION_TOP_LEFT, "STEPS", "SC");

    snprintf(buf, sizeof(buf), "%2u", counter);
    watch_display_text_with_fallback(WATCH_POSITION_TOP_RIGHT, buf, buf);

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

    _counter_display(state, 10);
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
    state->buffer_start = 0;
    state->buffer_end = 0;
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

    printf("Recording %d samples to %d\n", count, state->buffer_end);

    /* Record data */
    for (uint8_t i = 0; i < count; i++) {
        /* Calculate magnitude of acceleration */
        uint32_t mag = _approx_l2_norm(fifo.readings[i]);
        /* Clamp magnitude to 16 bits and scale down to 8 bits */
        mag = (mag > 0xffff) ? 0xffff : mag;
        mag >>= 8;

        /* Add to buffer */
        state->buffer[state->buffer_end] = mag;
        state->buffer_end = (state->buffer_end + 1) % sizeof(state->buffer);
    }
    lis2dw_clear_fifo();

    return count;
}

/* Calculate distance between two indices in circular buffer */
static inline uint8_t _buffer_dist(uint8_t i, uint8_t j, step_counter_state_t *state)
{
    return (j - i + sizeof(state->buffer)) % sizeof(state->buffer);
}

static void _detect_steps(step_counter_state_t *state)
{
    uint8_t *window;
    uint8_t j, win_size;
    uint32_t window_sum = 0;
    uint8_t above_thres = 0;
    uint8_t last_step = 0;

    /* Allocate window */
    win_size = 1 << state->window_bits;
    window = malloc(win_size * sizeof(uint8_t));
    if (window == NULL) {
        printf("Failed to allocate window\n");
        return;
    }

    uint8_t size = _buffer_dist(state->buffer_start, state->buffer_end, state);
    printf("Available samples: %d (from %d to %d)\n", size, state->buffer_start, state->buffer_end);

    /* Fill window with old data */
    j = _buffer_dist(win_size, state->buffer_start, state);
    for (uint8_t i = 0; i < win_size; i++) {
        window[i] = state->buffer[j];
        window_sum += window[i];
        j = (j + 1) % sizeof(state->buffer);
    }

    /* Process new data */
    j = state->buffer_start;
    for (uint8_t i = 0; i < size; i++) {
        /* Remove oldest value and add new */
        window_sum -= window[i % win_size];
        window[i % win_size] = state->buffer[j];
        window_sum += window[i % win_size];

        /* High pass filter: hp_value = current - mean */
        int32_t hp_value = state->buffer[j] - (window_sum >> state->window_bits);

        /* Detect step */
        if (hp_value > state->threshold && above_thres == 0) {
            above_thres = i;
        } else if (hp_value < state->threshold && above_thres > 0) {
            bool step_too_long = (i - above_thres > state->max_duration);
            bool step_too_early = (i - last_step < state->min_interval);

            if (step_too_long) {
                printf("Step too long at %d, ignoring\n", j);
            }
            if (step_too_early) {
                printf("Step too early at %d, ignoring\n", j);
            }

            if (!step_too_long && !step_too_early) {
                printf("Step detected at %d\n", j);
                state->steps++;
                last_step = i;
            }
            above_thres = 0;
        }

        j = (j + 1) % sizeof(state->buffer);
    }

    /* Reset buffer and free window */
    state->buffer_start = state->buffer_end;
    free(window);
}

static bool _counter_loop(movement_event_t event, void *context)
{
    step_counter_state_t *state = (step_counter_state_t *) context;
    watch_date_time_t now;

    printf("Counter loop %d\n", event.event_type);

    switch (event.event_type) {
        case EVENT_ACTIVATE:
            _counter_display(state, 10);
            break;
        case EVENT_TICK:
            /* Reset state at midnight */
            now = movement_get_local_date_time();
            if (now.unit.hour == 0 && now.unit.minute == 0 && now.unit.second == 0) {
                _reset_state(state);
            }

            /* Record new accelerometer data */
            int8_t count = _record_data(state);
            if (count < SAMPLING_RATE - 1) {
                printf("Not enough samples collected anymore\n");
            }

            /* Run detection if every 10 seconds or not enough samples collected anymore */
            if (now.unit.second % 10 == 0 || count < SAMPLING_RATE - 1) {
                _detect_steps(state);
            }

            _counter_display(state, 10 - now.unit.second % 10);
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
        state->max_duration = DEFAULT_MAX_DURATION;
        state->min_interval = DEFAULT_MIN_INTERVAL;
        state->window_bits = DEFAULT_WINDOW_BITS;

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
        state->settings[settings_page].display = _settings_max_duration_display;
        state->settings[settings_page].advance = _settings_max_duration_advance;
        settings_page++;
        state->settings[settings_page].display = _settings_min_interval_display;
        state->settings[settings_page].advance = _settings_min_interval_advance;
        settings_page++;
        state->settings[settings_page].display = _settings_window_bits_display;
        state->settings[settings_page].advance = _settings_window_bits_advance;
        settings_page++;
    }

    /* Set up accelerometer */
    state->prev_rate = movement_get_accelerometer_background_rate();
    movement_set_accelerometer_background_rate(LIS2DW_DATA_RATE_25_HZ);
    state->prev_bw = lis2dw_get_bandwidth_filtering();
    lis2dw_set_bandwidth_filtering(LIS2DW_BANDWIDTH_FILTER_DIV4);
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
    lis2dw_set_bandwidth_filtering(state->prev_bw);
    _lis2dw_print_state();
}

movement_watch_face_advisory_t step_counter_face_advise(void *context)
{
    (void) context;
    movement_watch_face_advisory_t retval = { 0 };
    return retval;
}
