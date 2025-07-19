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

/*
 * Experimental watch face for recording accelerometer data, intended to support
 * step counter development. The face can record x, y, z readings or the squared
 * magnitude of the readings.
 *
 * Behavior:
 * - On startup, the face displays the available filesystem space, labeled with "F".
 * - The number in the top right indicates the current recording session number.
 * - Pressing the alarm button starts recording. While recording:
 *     - The display shows "r" to indicate active recording.
 *     - The elapsed recording time in seconds is shown.
 * - Pressing the alarm button again stops recording.
 * - After stopping, the session number is incremented, and the available space is shown.
 * - If an error occurs, the display shows "E" followed by the error code.
 */

#include "movement.h"
#include "lfs.h"

/* Mask for data type and format */
#define LOG_DATA_XYZ     0x01
#define LOG_DATA_MAG     0x02
#define LOG_DATA_L1      0x04

typedef enum {
    PAGE_RECORDING,
    PAGE_LABELING,
} stepcounter_logging_page_t;

typedef struct {
    stepcounter_logging_page_t page;
    lfs_file_t file;
    uint32_t start_ts;
    uint8_t data_type;
    uint8_t index;
    uint8_t error;
    uint16_t steps;
} stepcounter_logging_state_t;

void stepcounter_logging_face_setup(uint8_t watch_face_index, void **context_ptr);
void stepcounter_logging_face_activate(void *context);
bool stepcounter_logging_face_loop(movement_event_t event, void *context);
void stepcounter_logging_face_resign(void *context);
movement_watch_face_advisory_t stepcounter_logging_face_advise(void *context);

#define stepcounter_logging_face ((const watch_face_t){ \
    stepcounter_logging_face_setup, \
    stepcounter_logging_face_activate, \
    stepcounter_logging_face_loop, \
    stepcounter_logging_face_resign, \
    stepcounter_logging_face_advise, \
})
