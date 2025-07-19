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
#include "stepcounter_logging_face.h"
#include "watch.h"
#include "watch_utility.h"
#include "filesystem.h"
#include "lfs.h"

/* Access to file system */
extern lfs_t eeprom_filesystem;
#define lfs_fs (eeprom_filesystem)

/* Constants*/
#define LOG_FILE_NAME       "sc_log.bin"
#define LOG_FILE_SPACER     0xff
#define ERROR_OPEN_FILE     0x01
#define ERROR_CLOSE_FILE    0x02
#define ERROR_WRITE_HEADER  0x03
#define ERROR_WRITE_SPACER  0x04
#define ERROR_WRITE_DATA    0x05
#define MAX_DURATION        100

/* 16-bit absolute value */
static inline int16_t abs16(int16_t x)
{
    int16_t mask = x >> 15;
    return (x + mask) ^ mask;
}

static void _start_recording(stepcounter_logging_state_t *state)
{
    uint32_t ret = 0;
    printf("Starting recording (index: %d)\n", state->index);

    /* Clear FIFO to avoid recording old data */
    lis2dw_clear_fifo();

    /* Open log file */
    int err = lfs_file_open(&lfs_fs, &state->file, LOG_FILE_NAME,
                            LFS_O_WRONLY | LFS_O_CREAT | LFS_O_APPEND);
    if (err < 0) {
        state->error = ERROR_OPEN_FILE;
        return;
    }

    /* Initialize log index and start time */
    watch_date_time_t now = watch_rtc_get_date_time();
    uint32_t now_ts = watch_utility_date_time_to_unix_time(now, 0);
    state->start_ts = now_ts;

    /* Write log header */
    ret += lfs_file_write(&lfs_fs, &state->file, &state->index, sizeof(state->index));
    ret += lfs_file_write(&lfs_fs, &state->file, &state->data_type, sizeof(state->data_type));
    ret += lfs_file_write(&lfs_fs, &state->file, &state->start_ts, sizeof(state->start_ts));
    if (ret != sizeof(state->index) + sizeof(state->start_ts) + sizeof(state->data_type)) {
        state->error = ERROR_WRITE_HEADER;
        return;
    }
}

static void _stop_recording(stepcounter_logging_state_t *state)
{
    uint32_t ret = 0;
    int8_t spacer = LOG_FILE_SPACER;
    printf("Stopping recording (index: %d)\n", state->index);

    /* Write spacer */
    ret += lfs_file_write(&lfs_fs, &state->file, &spacer, sizeof(spacer));
    if (ret != sizeof(spacer)) {
        state->error = ERROR_WRITE_SPACER;
        return;
    }

    /* Close log file */
    lfs_file_sync(&lfs_fs, &state->file);
    int err = lfs_file_close(&lfs_fs, &state->file);
    if (err < 0) {
        state->error = ERROR_CLOSE_FILE;
        return;
    }

    /* Update log index and reset time */
    state->index++;
    state->start_ts = 0;
}

static void _log_data(stepcounter_logging_state_t *state, lis2dw_fifo_t *fifo)
{
    uint32_t ret = 0;
    printf("Logging data (%d measurements)\n", fifo->count);

    /* Store fifo count (8 bit) */
    ret = lfs_file_write(&lfs_fs, &state->file, &fifo->count, sizeof(fifo->count));
    if (ret != sizeof(fifo->count))
        goto error;

    for (uint8_t cnt = 0; cnt < fifo->count; cnt++) {
        if (state->data_type & LOG_DATA_XYZ) {
            /* Write readings data (48bit) */
            ret = 0;
            ret += lfs_file_write(&lfs_fs, &state->file, &fifo->readings[cnt].x, sizeof(fifo->readings[cnt].x));
            ret += lfs_file_write(&lfs_fs, &state->file, &fifo->readings[cnt].y, sizeof(fifo->readings[cnt].y));
            ret += lfs_file_write(&lfs_fs, &state->file, &fifo->readings[cnt].z, sizeof(fifo->readings[cnt].z));
            if (ret != 3 * sizeof(fifo->readings[cnt].x))
                goto error;
        }

        if (state->data_type & LOG_DATA_MAG) {
            /* Write magnitude of readings (32bit). We are using abs instead of sq for efficiency */
            uint32_t mag = abs16(fifo->readings[cnt].x) + abs16(fifo->readings[cnt].y) + abs16(fifo->readings[cnt].z);
            ret = lfs_file_write(&lfs_fs, &state->file, &mag, sizeof(mag));
            if (ret != sizeof(mag))
                goto error;
        }
    }
    return;

  error:
    state->error = ERROR_WRITE_DATA;
    return;
}

static void _display_state(stepcounter_logging_state_t *state)
{
    char buf[10];

    watch_clear_colon();
    snprintf(buf, sizeof(buf), "%d", state->index);
    watch_display_text_with_fallback(WATCH_POSITION_TOP_RIGHT, buf, buf);
    watch_display_text_with_fallback(WATCH_POSITION_TOP_LEFT, "SL", "SL");

    if (state->error) {
        snprintf(buf, sizeof(buf), "E %.2d  ", state->error);
        watch_display_text_with_fallback(WATCH_POSITION_BOTTOM, buf, buf);
        return;
    }

    if (!state->start_ts) {
        int32_t free_space = filesystem_get_free_space();
        snprintf(buf, sizeof(buf), "F %.4ld", free_space);
        watch_display_text_with_fallback(WATCH_POSITION_BOTTOM, buf, buf);
        return;
    }

    watch_date_time_t now = watch_rtc_get_date_time();
    uint32_t now_ts = watch_utility_date_time_to_unix_time(now, 0);
    uint32_t diff = now_ts - state->start_ts;
    snprintf(buf, sizeof(buf), "R %.4lu", diff);
    watch_display_text_with_fallback(WATCH_POSITION_BOTTOM, buf, buf);
}

void stepcounter_logging_face_setup(uint8_t watch_face_index, void **context_ptr)
{
    (void) watch_face_index;
    if (*context_ptr == NULL) {
        *context_ptr = malloc(sizeof(stepcounter_logging_state_t));
        memset(*context_ptr, 0, sizeof(stepcounter_logging_state_t));

        /* 
         * Configure accelerometer to run in background at 12.5 Hz sampling rate.
         * This rate is required for step counting since human walking/running 
         * movements occur at frequencies up to 5 Hz
         */
        movement_set_accelerometer_background_rate(LIS2DW_DATA_RATE_12_5_HZ);

        /*
         * Enable lis2dw FIFO to collect data. The FIFO can hold up to 32 samples 
         * of measurements, enabling sampling rates of 12.5 Hz and 25 Hz when 
         * processing data every second.
         */
        lis2dw_enable_fifo();

        /* 
         * Moreover, we assume the accelerometer is configured by default as follows 
         * (see lis2dw_begin in lis2dw.c):
         *  - Low power mode enabled
         *  - LP mode 1 (12-bit)
         *  - Bandwidth filtering ODR/2 (6.25 Hz)
         *  - ±2g range
         */
    }

    stepcounter_logging_state_t *state = (stepcounter_logging_state_t *) * context_ptr;
    state->index = 1;
    state->data_type = LOG_DATA_MAG;
}

void stepcounter_logging_face_activate(void *context)
{
    stepcounter_logging_state_t *state = (stepcounter_logging_state_t *) context;
    _display_state(state);
}

bool stepcounter_logging_face_loop(movement_event_t event, void *context)
{
    stepcounter_logging_state_t *state = (stepcounter_logging_state_t *) context;
    lis2dw_fifo_t fifo;

    switch (event.event_type) {
        case EVENT_ACTIVATE:
            _display_state(state);
            break;
        case EVENT_TICK:
            if (state->start_ts) {
                lis2dw_read_fifo(&fifo);
                _log_data(state, &fifo);
                lis2dw_clear_fifo();

                watch_date_time_t now = watch_rtc_get_date_time();
                uint32_t now_ts = watch_utility_date_time_to_unix_time(now, 0);
                uint32_t diff = now_ts - state->start_ts;

                /* Check if recording duration exceeds maximum */
                if (diff > MAX_DURATION) {
                    _stop_recording(state);
                }
            }

            _display_state(state);
            break;
        case EVENT_ALARM_BUTTON_DOWN:
            if (!state->start_ts)
                _start_recording(state);
            else
                _stop_recording(state);
            _display_state(state);
            break;
        default:
            movement_default_loop_handler(event);
            break;
    }

    return true;
}

void stepcounter_logging_face_resign(void *context)
{
    stepcounter_logging_state_t *state = (stepcounter_logging_state_t *) context;
    if (state->start_ts)
        _stop_recording(state);
}

movement_watch_face_advisory_t stepcounter_logging_face_advise(void *context)
{
    (void) context;
    movement_watch_face_advisory_t retval = { 0 };
    return retval;
}
