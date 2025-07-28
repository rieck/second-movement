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
#include "lis2dw_monitor_face.h"

/* Access to file system */
extern lfs_t eeprom_filesystem;
#define lfs_fs (eeprom_filesystem)

/* Constants*/
#define LOG_FILE_NAME       "log.scl"
#define LOG_FILE_MARKER     0xff
#define LOG_MAGIC_BYTES     0x4223
#define LOG_VERSION         0x01
#define ERROR_OPEN_FILE     0x01
#define ERROR_READ_FILE     0x02
#define ERROR_WRITE_HEADER  0x03
#define ERROR_WRITE_DATA    0x05
#define ERROR_ALLOC_MEM     0x06
#define MIN_FS_SPACE        512 // Small remaining space

/* Chirp state */
static uint8_t *chirp_data_ptr;
static uint16_t chirp_data_ix;
static uint16_t chirp_data_len;

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

/* Open log file */
static void _log_open(stepcounter_logging_state_t *state)
{
    int err = lfs_file_open(&lfs_fs, &state->file, LOG_FILE_NAME,
                            LFS_O_WRONLY | LFS_O_CREAT | LFS_O_APPEND);
    if (err < 0) {
        state->error = ERROR_OPEN_FILE;
        return;
    }
}

/* Close log file */
static void _log_close(stepcounter_logging_state_t *state)
{
    lfs_file_sync(&lfs_fs, &state->file);
    int err = lfs_file_close(&lfs_fs, &state->file);
    if (err < 0) {
        /* Ignore errors. It is too late now */
        return;
    }
}

static void _start_recording(stepcounter_logging_state_t *state)
{
    uint32_t ret = 0, expected_size = 0;
    printf("Starting recording (index: %d)\n", state->index);
    _beep();

    /* Clear FIFO to avoid recording old data */
    lis2dw_clear_fifo();
    _log_open(state);

    /* Initialize log index and start time */
    watch_date_time_t now = watch_rtc_get_date_time();
    uint32_t now_ts = watch_utility_date_time_to_unix_time(now, 0);
    state->start_ts = now_ts;

    /* Write log header */
    uint16_t magic = LOG_MAGIC_BYTES;
    ret += lfs_file_write(&lfs_fs, &state->file, &magic, sizeof(magic));
    uint8_t version = LOG_VERSION;
    ret += lfs_file_write(&lfs_fs, &state->file, &version, sizeof(version));
    expected_size += sizeof(magic) + sizeof(version);

    /* Write sensor state and config */
    lis2dw_device_state_t device_state;
    lis2dw_get_state(&device_state);
    ret += lfs_file_write(&lfs_fs, &state->file, &device_state, sizeof(device_state));
    ret += lfs_file_write(&lfs_fs, &state->file, &state->data_type, sizeof(state->data_type));
    expected_size += sizeof(device_state) + sizeof(state->data_type);

    /* Write index and start time */
    ret += lfs_file_write(&lfs_fs, &state->file, &state->index, sizeof(state->index));
    ret += lfs_file_write(&lfs_fs, &state->file, &state->start_ts, sizeof(state->start_ts));
    expected_size += sizeof(state->index) + sizeof(state->start_ts);
    if (ret != expected_size) {
        state->error = ERROR_WRITE_HEADER;
        return;
    }
}

static void _stop_recording(stepcounter_logging_state_t *state)
{
    printf("Stopping recording (index: %d)\n", state->index);
    _beep();
    _log_close(state);

    /* Reset time and increment index */
    state->start_ts = 0;
    state->index++;
}

static void _log_data(stepcounter_logging_state_t *state, lis2dw_fifo_t *fifo)
{
    uint32_t ret = 0;
    printf("Logging data (%d measurements)\n", fifo->count);
    if (fifo->count == 0)
        return;

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
}

static void _log_steps(stepcounter_logging_state_t *state)
{
    uint32_t ret = 0;
    uint8_t marker = LOG_FILE_MARKER;
    printf("Steps in recording: %d\n", state->steps);

    _log_open(state);

    /* Write marker */
    ret += lfs_file_write(&lfs_fs, &state->file, &marker, sizeof(marker));
    if (ret != sizeof(marker))
        goto error;

    /* Write steps */
    ret = lfs_file_write(&lfs_fs, &state->file, &state->steps, sizeof(state->steps));
    if (ret != sizeof(state->steps))
        goto error;

    _log_close(state);

    /* Reset steps */
    state->steps = 0;
    return;

  error:
    state->error = ERROR_WRITE_DATA;
}

static void _delete_log_file(stepcounter_logging_state_t *state)
{
    (void) state;
    printf("Deleting log file\n");
    int err = lfs_remove(&lfs_fs, LOG_FILE_NAME);
    if (err < 0) {
        /* Ignore error */
        return;
    }
}

static void _chirp_quit(stepcounter_logging_state_t *state)
{
    printf("Quitting chirp (progress: %d/%d)\n", chirp_data_ix, chirp_data_len);

    watch_clear_indicator(WATCH_INDICATOR_BELL);
    watch_set_buzzer_off();
    movement_request_tick_frequency(1);
    state->chirping = false;

    /* Reset chirp state */
    if (chirp_data_ptr) {
        free(chirp_data_ptr);
    }
    chirp_data_ptr = NULL;
    chirp_data_ix = 0;
    chirp_data_len = 0;
}

static void _chirp_tick_transmit(void *context)
{
    stepcounter_logging_state_t *state = (stepcounter_logging_state_t *) context;

    uint8_t tone = chirpy_get_next_tone(&state->chirpy_encoder_state);
    // Transmission over?
    if (tone == 255) {
        _chirp_quit(state);
        return;
    }
    uint16_t period = chirpy_get_tone_period(tone);
    watch_set_buzzer_period_and_duty_cycle(period, 25);
    watch_set_buzzer_on();
}

static uint8_t _chirp_next_byte(uint8_t *next_byte)
{
    if (chirp_data_ix == chirp_data_len)
        return 0;
    *next_byte = chirp_data_ptr[chirp_data_ix];
    ++chirp_data_ix;
    return 1;
}

static void _load_log_file(stepcounter_logging_state_t *state)
{
    /* Check if log file exists */
    if (!filesystem_file_exists(LOG_FILE_NAME)) {
        state->error = ERROR_OPEN_FILE;
        return;
    }

    /* Get file size */
    chirp_data_len = filesystem_get_file_size(LOG_FILE_NAME);
    chirp_data_ptr = (uint8_t *) malloc(chirp_data_len);
    if (chirp_data_ptr == NULL) {
        state->error = ERROR_ALLOC_MEM;
        return;
    }

    /* Read file into memory */
    int ret = filesystem_read_file(LOG_FILE_NAME, (char *) chirp_data_ptr, chirp_data_len);
    if (!ret) {
        state->error = ERROR_READ_FILE;
        return;
    }
}

static void _chirp_countdown_tick(void *context)
{
    stepcounter_logging_state_t *state = (stepcounter_logging_state_t *) context;
    chirpy_tick_state_t *tick_state = &state->chirpy_tick_state;

    // Countdown over: start actual broadcast
    if (tick_state->seq_pos == 8 * 3) {
        tick_state->tick_compare = 3;
        tick_state->tick_count = -1;
        tick_state->seq_pos = 0;

        // Set up the encoder
        chirpy_init_encoder(&state->chirpy_encoder_state, _chirp_next_byte);
        tick_state->tick_fun = _chirp_tick_transmit;

        // Set up the data
        _load_log_file(state);
        printf("Starting chirp (progress:%d/%d)\n", chirp_data_ix, chirp_data_len);
        return;
    }
    // Sound or turn off buzzer
    if ((tick_state->seq_pos % 8) == 0) {
        watch_set_buzzer_period_and_duty_cycle(NotePeriods[BUZZER_NOTE_A5], 25);
        watch_set_buzzer_on();
    } else if ((tick_state->seq_pos % 8) == 1) {
        watch_set_buzzer_off();
    }
    ++tick_state->seq_pos;
}

static void _chrip_setup(stepcounter_logging_state_t *state)
{
    // We want frequent callbacks from now on
    movement_request_tick_frequency(64);
    watch_set_indicator(WATCH_INDICATOR_BELL);
    state->chirping = true;

    // Set up tick state; start with countdown
    state->chirpy_tick_state.tick_count = -1;
    state->chirpy_tick_state.tick_compare = 8;
    state->chirpy_tick_state.seq_pos = 0;
    state->chirpy_tick_state.tick_fun = _chirp_countdown_tick;
}

static void _chirping_display(stepcounter_logging_state_t *state)
{
    char buf[10];
    watch_display_text_with_fallback(WATCH_POSITION_TOP, "CHIRP", "CH");

    if (state->error) {
        snprintf(buf, sizeof(buf), "E %.2d  ", state->error);
        watch_display_text_with_fallback(WATCH_POSITION_BOTTOM, buf, buf);
        return;
    }

    uint32_t left = chirp_data_len - chirp_data_ix;
    snprintf(buf, sizeof(buf), "%.4lu%2d", left, state->chirpy_tick_state.tick_count);
    watch_display_text_with_fallback(WATCH_POSITION_BOTTOM, buf, buf);
}

static void _labeling_display(stepcounter_logging_state_t *state, uint8_t subsecond)
{
    char buf[10];

    watch_display_text_with_fallback(WATCH_POSITION_TOP, "STEPS", "SC");

    /* Blink the steps counter */
    if (subsecond % 2 == 0)
        snprintf(buf, sizeof(buf), "%4d  ", state->steps);
    else
        snprintf(buf, sizeof(buf), "      ");

    watch_display_text_with_fallback(WATCH_POSITION_BOTTOM, buf, buf);
}

static void _recording_display(stepcounter_logging_state_t *state)
{
    char buf[10];

    watch_clear_colon();
    snprintf(buf, sizeof(buf), "%2d", state->index);
    watch_display_text_with_fallback(WATCH_POSITION_TOP_RIGHT, buf, buf);
    watch_display_text_with_fallback(WATCH_POSITION_TOP_LEFT, "REC", "RE");

    int32_t free_space = filesystem_get_free_space();
    if (state->error) {
        snprintf(buf, sizeof(buf), "E %.2d  ", state->error);
    } else if (!state->start_ts) {
        snprintf(buf, sizeof(buf), "F%5ld", free_space);
    } else {
        snprintf(buf, sizeof(buf), "R%5ld", free_space);
    }

    watch_display_text_with_fallback(WATCH_POSITION_BOTTOM, buf, buf);
}

static void _switch_to_labeling(stepcounter_logging_state_t *state)
{
    /* Switch to labeling page */
    movement_request_tick_frequency(4);
    state->page = PAGE_LABELING;
    _labeling_display(state, 0);
    _beep();
}

static void _switch_to_recording(stepcounter_logging_state_t *state)
{
    /* Switch to recording page */
    movement_request_tick_frequency(1);
    state->page = PAGE_RECORDING;
    _recording_display(state);
    _beep();
}

static void _switch_to_chirping(stepcounter_logging_state_t *state)
{
    /* Switch to chirping page */
    movement_request_tick_frequency(1);
    state->page = PAGE_CHIRPING;
    _chirping_display(state);
    _beep();
}


static void _enforce_quota(stepcounter_logging_state_t *state)
{
    if (filesystem_get_free_space() < MIN_FS_SPACE) {
        _stop_recording(state);
        _switch_to_labeling(state);
    }
}

static bool _recording_loop(movement_event_t event, void *context)
{
    stepcounter_logging_state_t *state = (stepcounter_logging_state_t *) context;
    lis2dw_fifo_t fifo;

    switch (event.event_type) {
        case EVENT_ACTIVATE:
            _recording_display(state);
            break;
        case EVENT_TICK:
            if (state->start_ts) {
                lis2dw_read_fifo(&fifo);
                _log_data(state, &fifo);
                lis2dw_clear_fifo();
                _enforce_quota(state);
            }
            _recording_display(state);
            break;
        case EVENT_ALARM_BUTTON_UP:
            if (!state->start_ts) {
                _start_recording(state);
                _recording_display(state);
            } else {
                _stop_recording(state);
                _switch_to_labeling(state);
            }
            break;
        case EVENT_ALARM_LONG_PRESS:
            _switch_to_chirping(state);
            break;
        default:
            movement_default_loop_handler(event);
            break;
    }

    return true;
}

static bool _labeling_loop(movement_event_t event, void *context)
{
    stepcounter_logging_state_t *state = (stepcounter_logging_state_t *) context;

    switch (event.event_type) {
        case EVENT_ACTIVATE:
        case EVENT_TICK:
            _labeling_display(state, event.subsecond);
            break;
        case EVENT_LIGHT_BUTTON_DOWN:
            state->steps = (state->steps > 0) ? state->steps - 1 : 0;
            _labeling_display(state, event.subsecond);
            break;
        case EVENT_ALARM_BUTTON_DOWN:
            state->steps += 10;
            _labeling_display(state, event.subsecond);
            break;
        case EVENT_MODE_BUTTON_UP:
            _log_steps(state);
            _switch_to_recording(state);
            break;
        default:
            movement_default_loop_handler(event);
            break;
    }
    return true;
}

static bool _chirping_loop(movement_event_t event, void *context)
{
    stepcounter_logging_state_t *state = (stepcounter_logging_state_t *) context;

    switch (event.event_type) {
        case EVENT_ACTIVATE:
        case EVENT_TICK:
            _chirping_display(state);
            if (state->chirping) {
                ++state->chirpy_tick_state.tick_count;
                if (state->chirpy_tick_state.tick_count == state->chirpy_tick_state.tick_compare) {
                    state->chirpy_tick_state.tick_count = 0;
                    state->chirpy_tick_state.tick_fun(context);
                }
            }
            break;
        case EVENT_LIGHT_LONG_PRESS:
            if (state->chirping) {
                _chirp_quit(state);
            }
            _delete_log_file(state);
            _switch_to_recording(state);
            break;
        case EVENT_LIGHT_BUTTON_DOWN:
            /* Do nothing. */
            break;
        case EVENT_MODE_BUTTON_UP:
            if (state->chirping) {
                _chirp_quit(state);
            }
            _switch_to_recording(state);
            break;
        case EVENT_ALARM_BUTTON_UP:
            _chrip_setup(state);
            break;
        default:
            movement_default_loop_handler(event);
            break;
    }
    return true;
}

void stepcounter_logging_face_setup(uint8_t watch_face_index, void **context_ptr)
{
    (void) watch_face_index;
    if (*context_ptr == NULL) {
        *context_ptr = malloc(sizeof(stepcounter_logging_state_t));
        memset(*context_ptr, 0, sizeof(stepcounter_logging_state_t));
    }

    stepcounter_logging_state_t *state = (stepcounter_logging_state_t *) * context_ptr;
    state->index = 1;
    state->data_type = LOG_DATA_MAG; // | LOG_DATA_L1;
    state->page = PAGE_RECORDING;
}

void stepcounter_logging_face_activate(void *context)
{
    stepcounter_logging_state_t *state = (stepcounter_logging_state_t *) context;
    state->error = 0;
    lis2dw_enable_fifo();

    _recording_display(state);
}

bool stepcounter_logging_face_loop(movement_event_t event, void *context)
{
    stepcounter_logging_state_t *state = (stepcounter_logging_state_t *) context;

    switch (state->page) {
        default:
        case PAGE_RECORDING:
            return _recording_loop(event, context);
        case PAGE_LABELING:
            return _labeling_loop(event, context);
        case PAGE_CHIRPING:
            return _chirping_loop(event, context);
    }
}

void stepcounter_logging_face_resign(void *context)
{
    stepcounter_logging_state_t *state = (stepcounter_logging_state_t *) context;

    /* Stop recording if active */
    if (state->start_ts) {
        _stop_recording(state);
        _labeling_display(state, 0);
    }

    /* Stop chirping if active */
    if (state->chirping) {
        _chirp_quit(state);
    }

    /* Disable accelerometer */
    lis2dw_disable_fifo();
}

movement_watch_face_advisory_t stepcounter_logging_face_advise(void *context)
{
    (void) context;
    movement_watch_face_advisory_t retval = { 0 };
    return retval;
}
