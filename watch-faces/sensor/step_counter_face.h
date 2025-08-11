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

#include "movement.h"
#include "watch_utility.h"

/* Main pages of watch face*/
typedef enum {
    PAGE_COUNTER,
    PAGE_SETTINGS,
} step_counter_page_t;

/* Setting display and advance functions */
typedef struct {
    void (*display)(void *, uint8_t);
    void (*advance)(void *);
} step_counter_settings_t;

typedef struct {
    uint32_t steps;             /* Number of steps taken */
    step_counter_page_t page;   /* Displayed page */
    /* Main parameters for detection */
    uint16_t threshold;         /* Threshold for step detection */
    uint8_t min_steps;          /* Minimum time between steps */
    uint8_t max_steps;          /* Maximum time between steps */
    /* Flexible settings */
    step_counter_settings_t *settings;  /* Settings config */
    uint8_t settings_page:3;    /* Subpage in settings */
} step_counter_state_t;

void step_counter_face_setup(uint8_t watch_face_index, void **context_ptr);
void step_counter_face_activate(void *context);
bool step_counter_face_loop(movement_event_t event, void *context);
void step_counter_face_resign(void *context);
movement_watch_face_advisory_t step_counter_face_advise(void *context);

#define step_counter_face ((const watch_face_t){ \
    step_counter_face_setup, \
    step_counter_face_activate, \
    step_counter_face_loop, \
    step_counter_face_resign, \
    step_counter_face_advise, \
})
