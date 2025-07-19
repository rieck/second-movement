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
#define MAX_FILE_SIZE       0.9

/* 16-bit absolute value */
static inline uint16_t fast_abs16(int16_t x)
{
    int16_t mask = x >> 15;
    return (x + mask) ^ mask;
}

/* Approximate l2 norm of (x, y, z) */
static inline uint32_t fast_l2_norm(lis2dw_reading_t reading)
{
    /* Absolute values */
    uint16_t ax = fast_abs16(reading.x);
    uint16_t ay = fast_abs16(reading.y);
    uint16_t az = fast_abs16(reading.z);

    /* *INDENT-OFF* */
    /* Sort values: ax >= ay >= az */
    if (ax < ay) { uint16_t t = ax; ax = ay; ay = t; }
    if (ay < az) { uint16_t t = ay; ay = az; az = t; }
    if (ax < ay) { uint16_t t = ax; ax = ay; ay = t; }
    /* *INDENT-ON* */

    /* Approximate sqrt(x^2 + y^2 + z^2) */
    /* alpha ≈ 0.9375 (15/16), beta ≈ 0.375 (3/8) */
    return ax + ((15 * ay) >> 4) + ((3 * az) >> 3);
}

/* Simple l1 norm of (x, y, z) */
static inline uint32_t fast_l1_norm(lis2dw_reading_t reading)
{
    return fast_abs16(reading.x) + fast_abs16(reading.y) + fast_abs16(reading.z);
}

/* Play beep sound */
static inline void _beep()
{
    if (!movement_button_should_sound())
        return;
    watch_buzzer_play_note(BUZZER_NOTE_C7, 50);
}

static void _start_recording(stepcounter_logging_state_t *state)
{
    uint32_t ret = 0;
    printf("Starting recording (index: %d)\n", state->index);
    _beep();

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
    _beep();

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
            /* Store xyz data (3x16bit) */
            ret = 0;
            ret += lfs_file_write(&lfs_fs, &state->file, &fifo->readings[cnt].x, sizeof(fifo->readings[cnt].x));
            ret += lfs_file_write(&lfs_fs, &state->file, &fifo->readings[cnt].y, sizeof(fifo->readings[cnt].y));
            ret += lfs_file_write(&lfs_fs, &state->file, &fifo->readings[cnt].z, sizeof(fifo->readings[cnt].z));
            if (ret != 3 * sizeof(fifo->readings[cnt].x))
                goto error;
        }

        if (state->data_type & LOG_DATA_MAG) {
            /* Store magnitude (24bit). */
            uint32_t mag = 0;
            if (state->data_type & LOG_DATA_L1)
                mag = fast_l1_norm(fifo->readings[cnt]);
            else
                mag = fast_l2_norm(fifo->readings[cnt]);

            /* Pack magnitude into 3-byte buffer (little-endian) */
            uint8_t mag_buffer[3];
            mag_buffer[0] = (uint8_t) ((mag >> 0) & 0xFF);      /* Least significant byte */
            mag_buffer[1] = (uint8_t) ((mag >> 8) & 0xFF);      /* Middle byte */
            mag_buffer[2] = (uint8_t) ((mag >> 16) & 0xFF);     /* Most significant byte */

            ret = lfs_file_write(&lfs_fs, &state->file, mag_buffer, sizeof(mag_buffer));
            if (ret != sizeof(mag_buffer))
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
        snprintf(buf, sizeof(buf), "F%5ld", free_space);
        watch_display_text_with_fallback(WATCH_POSITION_BOTTOM, buf, buf);
        return;
    }

    watch_date_time_t now = watch_rtc_get_date_time();
    uint32_t now_ts = watch_utility_date_time_to_unix_time(now, 0);
    uint32_t diff = now_ts - state->start_ts;
    snprintf(buf, sizeof(buf), "R%5lu  ", diff);
    watch_display_text_with_fallback(WATCH_POSITION_BOTTOM, buf, buf);
}

static void _enforce_quota(stepcounter_logging_state_t *state)
{
    long int avail_quota = (long int) (filesystem_get_free_space() * MAX_FILE_SIZE);
    if (lfs_file_size(&lfs_fs, &state->file) > avail_quota) {
        _stop_recording(state);
    }
}

/* Print LIS2DW status */
static void _print_lis2dw_status(void)
{
    printf("LIS2DW status:\n");

    uint8_t mode = lis2dw_get_mode();
    printf("  Power mode:\t%d (0=LP, 1=HP, 2=On demand)\n", mode);

    uint8_t data_rate = lis2dw_get_data_rate();
    printf("  Data rate:\t%d (0=1.6Hz, 1=12.5Hz, 2=25Hz, 3=50Hz, ...)\n", data_rate);

    uint8_t lp_mode = lis2dw_get_low_power_mode();
    printf("  LP mode:\t%d (0=12-bit, 1-3=14-bit)\n", lp_mode);

    uint8_t bw_filt = lis2dw_get_bandwidth_filtering();
    printf("  BW filter:\t%d (0=ODR/2, 1=ODR/4, 2=ODR/10, 3=ODR/20)\n", bw_filt);

    uint8_t range = lis2dw_get_range();
    printf("  Range:\t%d (0=±2g, 1=±4g, 2=±8g, 3=±16g)\n", range);

    uint8_t filter_type = lis2dw_get_filter_type();
    printf("  Filter type:\t%d (0=LP, 1=HP)\n", filter_type);
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
        _print_lis2dw_status();
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
                _enforce_quota(state);
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
