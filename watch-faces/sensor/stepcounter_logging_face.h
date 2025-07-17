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
 * This watch face supports the development of a step counter face.
 * It operates in four main modes: Recording, Labeling, and Chirping. 
 * Each mode is activated by button interactions and provides targeted
 * functionality for data collection, annotation, and export.
 *
 * 1. Recording Mode
 *    - Used to capture accelerometer data
 *    - Shows available space ("F") or recorded bytes ("R")
 *    - Press ALARM to start recording
 *    - Press ALARM again to stop recording and enter labeling mode.
 *    - Long press ALARM to enter chirping mode.
 *
 * 2. Labeling Mode
 *    - Activated after stopping a recording.
 *    - Enter the number of steps taken during the session.
 *    - Press ALARM to increment steps by one
 *    - Press LIGHT to decrement steps by ten.
 *    - Press MODE to return to recording mode.
 *
 * 3. Chirping Mode
 *    - Used to transmit recorded data acoustically.
 *    - Shows remaining bytes to chirp out when running 
 *    - Press ALARM to start or stop chirping out the session data.
 *    - Press MODE to cancel and return to recording mode.
 *    - Long press LIGHT to delete all data and return.
 * */

#include "movement.h"
#include "lfs.h"
#include "chirpy_tx.h"

/* Mask for data type and format */
#define LOG_DATA_XYZ     0x01
#define LOG_DATA_MAG     0x02
#define LOG_DATA_L1      0x04

typedef enum {
    PAGE_RECORDING,
    PAGE_LABELING,
    PAGE_CHIRPING,
} stepcounter_logging_page_t;

typedef struct {
    uint32_t start_ts;
    uint8_t data_type;
    uint8_t index;
    uint8_t error;
    uint16_t steps;

    /* Displayed page */
    stepcounter_logging_page_t page;

    /* Logfile handle */
    lfs_file_t file;

    /* Chirpy state */
    chirpy_tick_state_t chirpy_tick_state;
    chirpy_encoder_state_t chirpy_encoder_state;
    bool chirping;
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
