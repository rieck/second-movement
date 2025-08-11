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

static bool _counter_loop(movement_event_t event, void *context)
{
    step_counter_state_t *state = (step_counter_state_t *) context;

    switch (event.event_type) {
        case EVENT_ACTIVATE:
        case EVENT_TICK:
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
    if (*context_ptr == NULL) {
        *context_ptr = malloc(sizeof(step_counter_state_t));
        memset(*context_ptr, 0, sizeof(step_counter_state_t));
    }
    step_counter_state_t *state = (step_counter_state_t *) * context_ptr;

    /* Default setup */
    state->steps = 0;
    state->threshold = DEFAULT_THRESHOLD;
    state->min_steps = DEFAULT_MIN_STEPS;
    state->max_steps = DEFAULT_MAX_STEPS;

    /* Initialize settings */
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
    settings_page++;
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
    step_counter_state_t *state = (step_counter_state_t *) context;

    /* Free allocated memory */
    if (state->settings != NULL) {
        free(state->settings);
        state->settings = NULL;
    }
}

movement_watch_face_advisory_t step_counter_face_advise(void *context)
{
    (void) context;
    movement_watch_face_advisory_t retval = { 0 };
    return retval;
}
