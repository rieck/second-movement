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

static inline void _beep()
{
    if (!movement_button_should_sound())
        return;
    watch_buzzer_play_note(BUZZER_NOTE_C7, 50);
}

static bool _settings_blink(uint8_t subsecond)
{
    if (subsecond % 2 == 0) {
        watch_display_text_with_fallback(WATCH_POSITION_BOTTOM, "      ", "      ");
        return true;
    }
    return false;
}

static void _settings_display(step_counter_state_t *state, uint8_t subsecond)
{
    watch_display_text_with_fallback(WATCH_POSITION_TOP, "SET  ", "ST  ");
}


static void _counter_display(step_counter_state_t *state)
{
    watch_display_text_with_fallback(WATCH_POSITION_TOP, "STEPS", "SC  ");
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
    state->page = PAGE_SETTINGS;
    watch_clear_colon();
    _settings_display(state, 0);
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
            _settings_display(state, event.subsecond);
            break;
        case EVENT_MODE_BUTTON_UP:
            _switch_to_counter(state);
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
}

void step_counter_face_activate(void *context)
{
    step_counter_state_t *state = (step_counter_state_t *) context;

    /* Setup lis2dw to run in background at 12.5 Hz sampling rate. */
    movement_set_accelerometer_background_rate(LIS2DW_DATA_RATE_12_5_HZ);

    /* Enable fifo and clear it. */
    lis2dw_enable_fifo();
    lis2dw_clear_fifo();

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
}

movement_watch_face_advisory_t step_counter_face_advise(void *context)
{
    (void) context;
    movement_watch_face_advisory_t retval = { 0 };
    return retval;
}
