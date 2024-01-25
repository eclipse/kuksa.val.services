/********************************************************************************
* Copyright (c) 2022 Contributors to the Eclipse Foundation
*
* See the NOTICE file(s) distributed with this work for additional
* information regarding copyright ownership.
*
* This program and the accompanying materials are made available under the
* terms of the Apache License 2.0 which is available at
* http://www.apache.org/licenses/LICENSE-2.0
*
* SPDX-License-Identifier: Apache-2.0
********************************************************************************/

#include "seatadjuster_engine.h"

#include <linux/can.h> // can_frame
#include <string.h>    // memset
#include <unistd.h>    // usleep
#include <error.h>     // errno
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>

//////////////////////// SocketCanMock CAN_Read and CAN_Write callbacks ////////////////

bool _sae_verbose = false; // verbose dumps from sim loop
bool _sae_debug = true;
bool _sae_all_motors = false;

#define SELF_INIT    "<MOCK> [SAE Init] "
#define SELF_CAN_RCB "<MOCK> [SAE-canR] "
#define SELF_CAN_WCB "<MOCK> [SAE-canW] "

#ifndef UNUSED
#  define UNUSED(x) (void)(x)
#endif

// external declarations
extern FILE* sim_log;
extern void fprintf_hex(FILE* fp, const void* buf, ssize_t len);

const char* sae_lrn_state(int lrn) {
    switch (lrn) {
        case MotorLRN_OK: return "OK";
        case MotorLRN_NOK: return "NOK";
        case MotorLRN_INV: return "INV";
        default: return "UNKNOWN";
    }
}

const char* sae_pos_mov_state(int state) {
    switch (state) {
        case MotorPosDirection_OFF: return "OFF";
        case MotorPosDirection_INC: return "INC";
        case MotorPosDirection_DEC: return "DEC";
        case MotorPosDirection_INV: return "INV";
        default: return "UNKNOWN";
    }
}

const char* sae_tilt_mov_state(int state) {
    switch (state) {
        case MotorTiltDirection_OFF: return "OFF";
        case MotorTiltDirection_INC: return "INC";
        case MotorTiltDirection_DEC: return "DEC";
        case MotorTiltDirection_INV: return "INV";
        default: return "UNKNOWN";
    }
}

const char* sae_height_mov_state(int state) {
    switch (state) {
        case MotorHeightDirection_OFF: return "OFF";
        case MotorHeightDirection_INC: return "INC";
        case MotorHeightDirection_DEC: return "DEC";
        case MotorHeightDirection_INV: return "INV";
        default: return "UNKNOWN";
    }
}

int64_t get_ts() {
    struct timespec spec;
    clock_gettime(CLOCK_MONOTONIC, &spec);
    return (int64_t)spec.tv_sec * 1000L + (int64_t)spec.tv_nsec / 1000000L;
}

#define POS_SHIFT       10 // 2^10 = 1024
#define POS_SHIFT_VAL   (1<<POS_SHIFT) // 2^10 = 1024
// #define POS_MASK  ((1 << POS_SHIFT)-1) // fractional point mask

/**
 * @brief Coverts percentage to raw fixed point format
 *
 * @param percent percentage
 * @return int fixed point value to set in _sae_motor1_pos
 */
static int sae_pos_raw(int percent) {
    return percent << POS_SHIFT;
}

/**
 * @brief Converts position fixed point raw value to percentage
 *
 * @param raw fixed point value
 * @return int percentage
 */
static int sae_pos_percent(int raw) {
    return raw >> POS_SHIFT;
}

/**
 * @brief Converts fixed point raw value to double
 *
 * @param raw fixed point position
 * @return double fractional position
 */
static double sae_pos_fp(int raw) {
    return raw / (double)POS_SHIFT_VAL;
}


int sae_init(sae_context_t* ctx) {
    if (!ctx) return -1;
    memset(ctx, 0, sizeof(sae_context_t));
    ctx->_sim_fd = -1;
    ctx->_sim_active = false;

    // motor1 = back/forward; motor2 = tilt; motor3 = height
    ctx->_sim_motor_pos = SAE_POS_INVALID;
    ctx->_sim_motor_pos_lrn = MotorLRN_OK;
    ctx->_sim_motor_pos_status = MotorPosDirection_OFF;
    ctx->_sim_motor_pos_rpm = 0;

    ctx->_sim_motor_tilt = SAE_POS_INVALID;
    ctx->_sim_motor_tilt_lrn = MotorLRN_OK;
    ctx->_sim_motor_tilt_status = MotorTiltDirection_OFF;
    ctx->_sim_motor_tilt_rpm = 0;

    ctx->_sim_motor_height = SAE_POS_INVALID;
    ctx->_sim_motor_height_lrn = MotorLRN_OK;
    ctx->_sim_motor_height_status = MotorHeightDirection_OFF;
    ctx->_sim_motor_height_rpm = 0;

    ctx->_sim_delay = 10; // 100ms maps to real hw, 80 rpm, it needs 10s to go through range
    ctx->_sim_threshold_enabled = true;
    ctx->_sim_ecux = 1;
    
    ctx->_sim_motor_pos_threshold_hi_stop = false;
    ctx->_sim_motor_pos_threshold_lo_stop = false;
    ctx->_sim_motor_pos_ts = -1; // not moving
    ctx->_sim_motor_pos_inc = 0;
    ctx->_sim_motor_pos_oldpos = -2; // really invalid

    ctx->_sim_motor_tilt_threshold_hi_stop = false;
    ctx->_sim_motor_tilt_threshold_lo_stop = false;
    ctx->_sim_motor_tilt_ts = -1; // not moving
    ctx->_sim_motor_tilt_inc = 0;
    ctx->_sim_motor_tilt_oldpos = -2; // really invalid

    ctx->_sim_motor_height_threshold_hi_stop = false;
    ctx->_sim_motor_height_threshold_lo_stop = false;
    ctx->_sim_motor_height_ts = -1; // not moving
    ctx->_sim_motor_height_inc = 0;
    ctx->_sim_motor_height_oldpos = -2; // really invalid

    // apply environment variables
    if (getenv("SAE_DEBUG")) {
        // global debug flag
        _sae_debug = atoi(getenv("SAE_DEBUG"));
    }
    if (getenv("SAE_VERBOSE")) {
        // global verbose flag
        _sae_verbose = atoi(getenv("SAE_VERBOSE"));
    }
    if (getenv("SAE_DELAY")) {
        ctx->_sim_delay = atoi(getenv("SAE_DELAY"));
    }
    if (getenv("SAE_POS")) {
        int pos = atoi(getenv("SAE_POS"));
        if (pos == -1 || pos == 255) {
            ctx->_sim_motor_pos = SAE_POS_INVALID;
            ctx->_sim_motor_tilt = SAE_POS_INVALID;
            ctx->_sim_motor_height = SAE_POS_INVALID;
        } else {
            ctx->_sim_motor_pos = sae_pos_raw(pos);
            ctx->_sim_motor_tilt = sae_pos_raw(pos);
            ctx->_sim_motor_height = sae_pos_raw(pos);
        }
    }
    if (getenv("SAE_LRN")) {
        ctx->_sim_motor_pos_lrn = atoi(getenv("SAE_LRN"));
        ctx->_sim_motor_tilt_lrn = atoi(getenv("SAE_LRN"));
        ctx->_sim_motor_height_lrn = atoi(getenv("SAE_LRN"));
    }
    if (getenv("SAE_STOP")) {
        // TODO: enable stop at thresholds
        ctx->_sim_threshold_enabled = atoi(getenv("SAE_STOP"));
    }
    if (getenv("SAE_ALL")) {
        // apply motor1 state to all 4 motors
        _sae_all_motors = atoi(getenv("SAE_ALL"));
    }
    if (_sae_debug) {
        fprintf(sim_log, SELF_INIT "Initialized with [ SAE_POS:%d, SAE_DELAY:%d, SAE_LRN:%d, SAE_STOP:%d, SAE_DEBUG:%d, SAE_VERBOSE:%d ]\n",
            sae_pos_percent(ctx->_sim_motor_pos), ctx->_sim_delay, ctx->_sim_motor_pos_lrn,
            ctx->_sim_threshold_enabled, _sae_debug, _sae_verbose);
    }
    return 0;
}

int sae_start(sae_context_t *ctx, int fd) {
    if (!ctx) return -1;
    ctx->_sim_fd = fd;
    ctx->_sim_active = true;
    return 0;
}

int sae_close(sae_context_t *ctx) {
    if (!ctx) return -1;
    ctx->_sim_fd = -1;
    ctx->_sim_active = false;
    // FIXME: wait for sae_ callbacks to finish
    return 0;
}

int sae_estimate_move_time(int rpm) {
    if (rpm < 30) {
        return 0; // do not move
    }
    if (rpm > 130) {
        return 1000; // prevent negative times (130rpm=2000ms)
    }
    // estimator function:
    // return 1000 * (8 + 2*(100 - rpm) / 10);
    return 8000 + 200 * (100 - rpm);
}

int sae_pos_increment(sae_context_t *ctx) {
    if (!ctx || ctx->_sim_fd == -1) {
        fprintf(sim_log, "sae_calculate_motor1_pos: Invalid context!\n");
        return 0; // same pos
    }
    if (!ctx->_sim_active) {
        return 0; // same pos
    }
    int move_time = sae_estimate_move_time(ctx->_sim_motor_pos_rpm);
    if (move_time == 0) {
        return 0; // don't crash
    }


    return (1<<POS_SHIFT) * // result is shifted to fixed point format
           100 *            // percent
           (ctx->_sim_delay + 10) // allow bigger overhead (+10ms) for processing the read callback
           / move_time;     // calculate increase per read 'tick' (defined by sim_delay)
}

int sae_tilt_increment(sae_context_t *ctx) {
    if (!ctx || ctx->_sim_fd == -1) {
        fprintf(sim_log, "sae_calculate_motor2_pos: Invalid context!\n");
        return 0; // same pos
    }
    if (!ctx->_sim_active) {
        return 0; // same pos
    }
    int move_time = sae_estimate_move_time(ctx->_sim_motor_tilt_rpm);
    if (move_time == 0) {
        return 0; // don't crash
    }


    return (1<<POS_SHIFT) * // result is shifted to fixed point format
           100 *            // percent
           (ctx->_sim_delay + 10) // allow bigger overhead (+10ms) for processing the read callback
           / move_time;     // calculate increase per read 'tick' (defined by sim_delay)
}

int sae_height_increment(sae_context_t *ctx) {
    if (!ctx || ctx->_sim_fd == -1) {
        fprintf(sim_log, "sae_calculate_motor3_pos: Invalid context!\n");
        return 0; // same pos
    }
    if (!ctx->_sim_active) {
        return 0; // same pos
    }
    int move_time = sae_estimate_move_time(ctx->_sim_motor_height_rpm);
    if (move_time == 0) {
        return 0; // don't crash
    }


    return (1<<POS_SHIFT) * // result is shifted to fixed point format
           100 *            // percent
           (ctx->_sim_delay + 10) // allow bigger overhead (+10ms) for processing the read callback
           / move_time;     // calculate increase per read 'tick' (defined by sim_delay)
}

// feeds custom data to mockedsocket.read(). should be a canframe
ssize_t sae_read_cb(sae_context_t *ctx, void *buf, size_t len) {
    struct can_frame *cf = (struct can_frame *)buf;

    if (!ctx || ctx->_sim_fd == -1) {
        fprintf(sim_log, SELF_CAN_RCB "Invalid context!\n");
        errno = EINVAL;
        return -1;
    }
    // only accept can_frames!
    if (!buf || len != sizeof(struct can_frame)) {
        fprintf(sim_log, SELF_CAN_RCB "Unexpected buffer length: %ld!\n", len);
        errno = EINVAL;
        return -1;
    }
    if (!ctx->_sim_active) {
        usleep(ctx->_sim_delay * 1000L);
        errno = EAGAIN;  // simulate read timeout
        return -1;
    }

    if (ctx->_sim_ecux == 1) {
        // motor3
        // go out of invalid pos. TODO: handle calibration loop in future
        if (ctx->_sim_motor_height == SAE_POS_INVALID) {
            usleep(ctx->_sim_delay * 1000L);
            ctx->_sim_motor_height = sae_pos_raw(42);
            fprintf(sim_log, SELF_CAN_RCB "  *** Resetting Invalid POS to %d\n", sae_pos_percent(ctx->_sim_motor_height));
        }

        if (ctx->_sim_threshold_enabled) {
            // handle hi/low threshold reset flags
            if (ctx->_sim_motor_height_threshold_lo_stop && sae_pos_percent(ctx->_sim_motor_height) > 14) {
                ctx->_sim_motor_height_threshold_lo_stop = false;
                fprintf(sim_log, SELF_CAN_RCB "*** Low threshold stop reset\n");
            }
            if (ctx->_sim_motor_height_threshold_hi_stop && sae_pos_percent(ctx->_sim_motor_height) < 85) {
                ctx->_sim_motor_height_threshold_hi_stop = false;
                fprintf(sim_log, SELF_CAN_RCB "*** High threshold stop reset\n");
            }
        }

        int next_pos = ctx->_sim_motor_height + ctx->_sim_motor_height_inc;
        if (_sae_verbose && ctx->_sim_motor_height_inc != 0) {
            int64_t elapsed = ctx->_sim_motor_height_ts != -1 ? get_ts() - ctx->_sim_motor_height_ts : 0;
            fprintf(sim_log, SELF_CAN_RCB "    --> motor3 pos:%.2f, new:%.2f, step:%.2f, elapsed:%d\n",
                sae_pos_fp(ctx->_sim_motor_height), sae_pos_fp(next_pos), sae_pos_fp(ctx->_sim_motor_height_inc), (int)elapsed);
        }
        if (ctx->_sim_motor_height_status == MotorHeightDirection_INC) {
            // FIXME: consider fractional part here e.g. 99.x
            if (sae_pos_percent(next_pos) <= 100) {
                ctx->_sim_motor_height = next_pos;
            } else {
                ctx->_sim_motor_height_status = MotorHeightDirection_OFF;
            }
            // handle stop @ 85% threshold (once)
            if (ctx->_sim_threshold_enabled && sae_pos_percent(ctx->_sim_motor_height) >= 85 && !ctx->_sim_motor_height_threshold_hi_stop) {
                fprintf(sim_log, SELF_CAN_RCB "* [INC] Stopping at %d%%\n", sae_pos_percent(ctx->_sim_motor_height));
                ctx->_sim_motor_height_status = MotorHeightDirection_OFF;
                ctx->_sim_motor_height_threshold_hi_stop = true;
            }
        } else if (ctx->_sim_motor_height_status == MotorHeightDirection_DEC) {
            if (sae_pos_percent(next_pos) >= 0) {
                ctx->_sim_motor_height = next_pos;
            } else {
                ctx->_sim_motor_height_status = MotorHeightDirection_OFF;
            }
            // handle stop @ 14% threshold (once)
            if (ctx->_sim_threshold_enabled && sae_pos_percent(ctx->_sim_motor_height) <= 14 && !ctx->_sim_motor_height_threshold_lo_stop) {
                fprintf(sim_log, SELF_CAN_RCB "* [DEC] Stopping at %d%%\n", sae_pos_percent(ctx->_sim_motor_height));
                ctx->_sim_motor_height_status = MotorHeightDirection_OFF;
                ctx->_sim_motor_height_threshold_lo_stop = true;
            }
        }

        int motor3_pos_percent = sae_pos_percent(ctx->_sim_motor_height); // ignore fractional

        cf->can_id = 0x712;  // CAN_SECU1_STAT_FRAME_ID
        cf->can_dlc = 8;
        memset(cf->data, 0, sizeof(cf->data));
        // data ### Sending SECU2_STAT [ MOTOR3_MOV_STATE: 'INC', MOTOR3_LEARNING_STATE: 'learned', MOTOR3_POS: 0..100 ]
        // cansend vcan0 712#46.44.01.00.00.00.00.00
        // cansend 712#${DIR}.44.${POS}.00.00.00.00.00 // $DIR: { 0x44=OFF, 0x46=Motor3::INC, 0x45=Notor1::DEC=0x45 }
        cf->data[0] = (ctx->_sim_motor_height_status & 3) |      // bit 0-1
                    ((ctx->_sim_motor_height_lrn & 3) << 2);  // bit 2-3
        cf->data[1] = 0; // all other motors off/not learned
        cf->data[2] = motor3_pos_percent;
        if (_sae_debug && ctx->_sim_motor_height_oldpos != motor3_pos_percent) {
            fprintf(sim_log, SELF_CAN_RCB "Generated: SECU2_STAT "
                    "{ m1_pos:%3d%%, m1_state:%3s, m1_lrn:%3s } \n",
                    motor3_pos_percent, sae_height_mov_state(ctx->_sim_motor_height_status), sae_lrn_state(ctx->_sim_motor_height_lrn));
        }
        if (_sae_verbose) {
            fprintf(sim_log, SELF_CAN_RCB "  --> ");
            fprintf_hex(sim_log, buf, len);
            fprintf(sim_log, "\n");
        }

        usleep(ctx->_sim_delay * 1000L);  // 100 ms sleep
        ssize_t res = sizeof(struct can_frame);
        memcpy(buf, cf, res);

        ctx->_sim_motor_height_oldpos = motor3_pos_percent;
        ctx->_sim_ecux = 2;

        return res;
    }
    else if (ctx->_sim_ecux == 2) {
        // motor1
        // go out of invalid pos. TODO: handle calibration loop in future
        if (ctx->_sim_motor_pos == SAE_POS_INVALID) {
            usleep(ctx->_sim_delay * 1000L);
            ctx->_sim_motor_pos = sae_pos_raw(42);
            fprintf(sim_log, SELF_CAN_RCB "  *** Resetting Invalid POS to %d\n", sae_pos_percent(ctx->_sim_motor_pos));
        }

        if (ctx->_sim_threshold_enabled) {
            // handle hi/low threshold reset flags
            if (ctx->_sim_motor_pos_threshold_lo_stop && sae_pos_percent(ctx->_sim_motor_pos) > 14) {
                ctx->_sim_motor_pos_threshold_lo_stop = false;
                fprintf(sim_log, SELF_CAN_RCB "*** Low threshold stop reset\n");
            }
            if (ctx->_sim_motor_pos_threshold_hi_stop && sae_pos_percent(ctx->_sim_motor_pos) < 85) {
                ctx->_sim_motor_pos_threshold_hi_stop = false;
                fprintf(sim_log, SELF_CAN_RCB "*** High threshold stop reset\n");
            }
        }

        int next_pos1 = ctx->_sim_motor_pos + ctx->_sim_motor_pos_inc;
        if (_sae_verbose && ctx->_sim_motor_pos_inc != 0) {
            int64_t elapsed = ctx->_sim_motor_pos_ts != -1 ? get_ts() - ctx->_sim_motor_pos_ts : 0;
            fprintf(sim_log, SELF_CAN_RCB "    --> motor1 pos:%.2f, new:%.2f, step:%.2f, elapsed:%d\n",
                sae_pos_fp(ctx->_sim_motor_pos), sae_pos_fp(next_pos1), sae_pos_fp(ctx->_sim_motor_pos_inc), (int)elapsed);
        }
        if (ctx->_sim_motor_pos_status == MotorPosDirection_INC) {
            // FIXME: consider fractional part here e.g. 99.x
            if (sae_pos_percent(next_pos1) <= 100) {
                ctx->_sim_motor_pos = next_pos1;
            } else {
                ctx->_sim_motor_pos_status = MotorPosDirection_OFF;
            }
            // handle stop @ 85% threshold (once)
            if (ctx->_sim_threshold_enabled && sae_pos_percent(ctx->_sim_motor_pos) >= 85 && !ctx->_sim_motor_pos_threshold_hi_stop) {
                fprintf(sim_log, SELF_CAN_RCB "* [INC] Stopping at %d%%\n", sae_pos_percent(ctx->_sim_motor_pos));
                ctx->_sim_motor_pos_status = MotorPosDirection_OFF;
                ctx->_sim_motor_pos_threshold_hi_stop = true;
            }
        } else if (ctx->_sim_motor_pos_status == MotorPosDirection_DEC) {
            if (sae_pos_percent(next_pos1) >= 0) {
                ctx->_sim_motor_pos = next_pos1;
            } else {
                ctx->_sim_motor_pos_status = MotorPosDirection_OFF;
            }
            // handle stop @ 14% threshold (once)
            if (ctx->_sim_threshold_enabled && sae_pos_percent(ctx->_sim_motor_pos) <= 14 && !ctx->_sim_motor_pos_threshold_lo_stop) {
                fprintf(sim_log, SELF_CAN_RCB "* [DEC] Stopping at %d%%\n", sae_pos_percent(ctx->_sim_motor_pos));
                ctx->_sim_motor_pos_status = MotorPosDirection_OFF;
                ctx->_sim_motor_pos_threshold_lo_stop = true;
            }
        }

        // motor2
        // go out of invalid pos. TODO: handle calibration loop in future
        if (ctx->_sim_motor_tilt == SAE_POS_INVALID) {
            usleep(ctx->_sim_delay * 1000L);
            ctx->_sim_motor_tilt = sae_pos_raw(42);
            fprintf(sim_log, SELF_CAN_RCB "  *** Resetting Invalid POS to %d\n", sae_pos_percent(ctx->_sim_motor_tilt));
        }

        if (ctx->_sim_threshold_enabled) {
            // handle hi/low threshold reset flags
            if (ctx->_sim_motor_tilt_threshold_lo_stop && sae_pos_percent(ctx->_sim_motor_tilt) > 14) {
                ctx->_sim_motor_tilt_threshold_lo_stop = false;
                fprintf(sim_log, SELF_CAN_RCB "*** Low threshold stop reset\n");
            }
            if (ctx->_sim_motor_tilt_threshold_hi_stop && sae_pos_percent(ctx->_sim_motor_tilt) < 85) {
                ctx->_sim_motor_tilt_threshold_hi_stop = false;
                fprintf(sim_log, SELF_CAN_RCB "*** High threshold stop reset\n");
            }
        }

        int next_pos2 = ctx->_sim_motor_tilt + ctx->_sim_motor_tilt_inc;
        if (_sae_verbose && ctx->_sim_motor_tilt_inc != 0) {
            int64_t elapsed = ctx->_sim_motor_tilt_ts != -1 ? get_ts() - ctx->_sim_motor_tilt_ts : 0;
            fprintf(sim_log, SELF_CAN_RCB "    --> motor2 pos:%.2f, new:%.2f, step:%.2f, elapsed:%d\n",
                sae_pos_fp(ctx->_sim_motor_tilt), sae_pos_fp(next_pos2), sae_pos_fp(ctx->_sim_motor_tilt_inc), (int)elapsed);
        }
        if (ctx->_sim_motor_tilt_status == MotorTiltDirection_INC) {
            // FIXME: consider fractional part here e.g. 99.x
            if (sae_pos_percent(next_pos2) <= 100) {
                ctx->_sim_motor_tilt = next_pos2;
            } else {
                ctx->_sim_motor_tilt_status = MotorTiltDirection_OFF;
            }
            // handle stop @ 85% threshold (once)
            if (ctx->_sim_threshold_enabled && sae_pos_percent(ctx->_sim_motor_tilt) >= 85 && !ctx->_sim_motor_tilt_threshold_hi_stop) {
                fprintf(sim_log, SELF_CAN_RCB "* [INC] Stopping at %d%%\n", sae_pos_percent(ctx->_sim_motor_tilt));
                ctx->_sim_motor_tilt_status = MotorTiltDirection_OFF;
                ctx->_sim_motor_tilt_threshold_hi_stop = true;
            }
        } else if (ctx->_sim_motor_tilt_status == MotorTiltDirection_DEC) {
            if (sae_pos_percent(next_pos2) >= 0) {
                ctx->_sim_motor_tilt = next_pos2;
            } else {
                ctx->_sim_motor_tilt_status = MotorTiltDirection_OFF;
            }
            // handle stop @ 14% threshold (once)
            if (ctx->_sim_threshold_enabled && sae_pos_percent(ctx->_sim_motor_tilt) <= 14 && !ctx->_sim_motor_tilt_threshold_lo_stop) {
                fprintf(sim_log, SELF_CAN_RCB "* [DEC] Stopping at %d%%\n", sae_pos_percent(ctx->_sim_motor_tilt));
                ctx->_sim_motor_tilt_status = MotorTiltDirection_OFF;
                ctx->_sim_motor_tilt_threshold_lo_stop = true;
            }
        }

        int motor1_pos_percent = sae_pos_percent(ctx->_sim_motor_pos); // ignore fractional
        int motor2_pos_percent = sae_pos_percent(ctx->_sim_motor_tilt); // ignore fractional

        cf->can_id = 0x714;  // CAN_SECU2_STAT_FRAME_ID
        cf->can_dlc = 8;
        memset(cf->data, 0, sizeof(cf->data));
        // data ### Sending SECU2_STAT [ MOTOR1_MOV_STATE: 'INC', MOTOR1_LEARNING_STATE: 'learned', MOTOR1_POS: 0..100 ]
        // cansend vcan0 714#46.44.01.00.00.00.00.00
        // cansend 714#${DIR}.44.${POS}.00.00.00.00.00 // $DIR: { 0x44=OFF, 0x46=Motor1::INC, 0x45=Notor1::DEC=0x45 }
        cf->data[0] = (ctx->_sim_motor_pos_status & 3) |      // bit 0-1
                    ((ctx->_sim_motor_pos_lrn & 3) << 2);  // bit 2-3

        cf->data[1] =  // 0x44; // motor 2 && 3 OFF / LRN
                    (ctx->_sim_motor_tilt_status & 3) |      // bit 0-1
                    ((ctx->_sim_motor_tilt_lrn & 3) << 2);  // bit 2-3

        cf->data[2] = motor1_pos_percent;
        cf->data[3] = 0; // all other motors off/not learned
        cf->data[4] = motor2_pos_percent;
        if (_sae_debug && ctx->_sim_motor_pos_oldpos != motor1_pos_percent) {
            fprintf(sim_log, SELF_CAN_RCB "Generated: SECU2_STAT "
                    "{ m1_pos:%3d%%, m1_state:%3s, m1_lrn:%3s } "
                    "{ m2_pos:%3d%%, m2_state:%3s, m2_lrn:%3s } \n",
                    motor1_pos_percent, sae_pos_mov_state(ctx->_sim_motor_pos_status), sae_lrn_state(ctx->_sim_motor_pos_lrn),
                    motor2_pos_percent, sae_tilt_mov_state(ctx->_sim_motor_tilt_status), sae_lrn_state(ctx->_sim_motor_tilt_lrn));
        }
        if (_sae_verbose) {
            fprintf(sim_log, SELF_CAN_RCB "  --> ");
            fprintf_hex(sim_log, buf, len);
            fprintf(sim_log, "\n");
        }

        usleep(ctx->_sim_delay * 1000L);  // 100 ms sleep
        ssize_t res = sizeof(struct can_frame);
        memcpy(buf, cf, res);

        ctx->_sim_motor_pos_oldpos = motor1_pos_percent;
        ctx->_sim_motor_tilt_oldpos = motor2_pos_percent;

        ctx->_sim_ecux = 1;

        return res;
    }
    else{
        return 0;
    }
}

// Handles mockedSocket.write(), e.g. detect custom command and change default_read_cb values.
ssize_t sae_write_cb(sae_context_t *ctx, const void *buf, size_t len) {
    if (!ctx || ctx->_sim_fd == -1) {
        fprintf(sim_log, SELF_CAN_WCB "Invalid context!\n");
        errno = EINVAL;
        return -1;
    }
    // only accept can_frames!
    if (!buf || len != sizeof(struct can_frame)) {
        fprintf(sim_log, SELF_CAN_WCB "Unexpected buffer length: %ld!\n", len);
        errno = EINVAL;
        return -1;
    }
    if (_sae_verbose) {
        fprintf(sim_log, SELF_CAN_WCB "TX buf: ");
        fprintf_hex(sim_log, buf, len);
        fprintf(sim_log, "\n");
    }
    struct can_frame *cf = (struct can_frame *)buf;
    if (_sae_debug) {
        fprintf(sim_log, SELF_CAN_WCB "TX: can_frame { canID:%4x, dlc:%d, data: 0x[", cf->can_id, cf->can_dlc);
        fprintf_hex(sim_log, cf->data, cf->can_dlc);
        fprintf(sim_log, " }\n");
    }
    if (cf->can_id == 0x707) { // CAN_SECU2_CMD_1_FRAME_ID
        unsigned char motor1_dir = (cf->data[0] >> 0) & 0x3;
        unsigned char motor2_dir = (cf->data[0] >> 2) & 0x3;
        unsigned char motor3_dir = (cf->data[0] >> 4) & 0x3;
        unsigned char motor4_dir = (cf->data[0] >> 6) & 0x3;

        unsigned char motor1_rpm = cf->data[1];
        unsigned char motor2_rpm = cf->data[2];
        unsigned char motor3_rpm = cf->data[3];
        unsigned char motor4_rpm = cf->data[4];

        fprintf(sim_log, SELF_CAN_WCB "SECU2_CMD_1 { m1_dir:%d, m1_rpm:%d,  m2_dir:%d, m2_rpm:%d,  m3_dir:%d, m3_rpm:%d,  m4_dir:%d, m4_rpm:%d }\n",
                motor1_dir, motor1_rpm, motor2_dir, motor2_rpm, motor3_dir, motor3_rpm, motor4_dir, motor4_rpm);

        ctx->_sim_motor_pos_rpm = motor1_rpm;
        int motor_inc = sae_pos_increment(ctx); // calculate increase per read 'tick' (defined by sim_delay)
        if (motor1_dir == MotorPosDirection_OFF) {
            fprintf(sim_log, SELF_CAN_WCB "*** Motor1::OFF\n");
            ctx->_sim_motor_pos_status = MotorPosDirection_OFF;
            ctx->_sim_motor_pos_rpm = 0; // make sure it is zero
            ctx->_sim_motor_pos_ts = -1; // reset move timestamp
            ctx->_sim_motor_pos_inc = 0;
        } else if (motor1_dir == MotorPosDirection_INC && motor1_rpm > 0) {
            if (_sae_debug) {
                fprintf(sim_log, SELF_CAN_WCB "*** Motor1::INC [ step:%f, delay:%d, move_time:%d ] \n",
                        sae_pos_fp(motor_inc), ctx->_sim_delay, sae_estimate_move_time(ctx->_sim_motor_pos_rpm));
            } else {
                fprintf(sim_log, SELF_CAN_WCB "*** Motor1::INC\n");
            }
            ctx->_sim_motor_pos_status = MotorPosDirection_INC;
            ctx->_sim_motor_pos_inc = motor_inc;
            ctx->_sim_motor_pos_ts = get_ts();
            // todo : calculate increment per sim_delay cycle
        } else if (motor1_dir == MotorPosDirection_DEC && motor1_rpm > 0) {
            if (_sae_debug) {
                fprintf(sim_log, SELF_CAN_WCB "*** Motor1::DEC [ step:%f, delay:%d, move_time:%d ] \n",
                        sae_pos_fp(-motor_inc), ctx->_sim_delay, sae_estimate_move_time(ctx->_sim_motor_pos_rpm));
            } else {
                fprintf(sim_log, SELF_CAN_WCB "*** Motor1::DEC\n");
            }
            ctx->_sim_motor_pos_status = MotorPosDirection_DEC;
            ctx->_sim_motor_pos_inc = -motor_inc;
            ctx->_sim_motor_pos_ts = get_ts();
        } else {
            fprintf(sim_log, SELF_CAN_WCB "Warning! Unhandled motor status: 0x%02X\n", motor1_dir);
        }

        ctx->_sim_motor_tilt_rpm = motor3_rpm;
        int motor2_inc = sae_tilt_increment(ctx);
        if (motor3_dir == MotorTiltDirection_OFF) {
            fprintf(sim_log, SELF_CAN_WCB "*** Motor2::OFF\n");
            ctx->_sim_motor_tilt_status = MotorTiltDirection_OFF;
            ctx->_sim_motor_tilt_rpm = 0; // make sure it is zero
            ctx->_sim_motor_tilt_ts = -1; // reset move timestamp
            ctx->_sim_motor_tilt_inc = 0;
        } else if (motor3_dir == MotorTiltDirection_INC && motor3_rpm > 0) {
            if (_sae_debug) {
                fprintf(sim_log, SELF_CAN_WCB "*** Motor2::INC [ step:%f, delay:%d, move_time:%d ] \n",
                        sae_pos_fp(motor2_inc), ctx->_sim_delay, sae_estimate_move_time(ctx->_sim_motor_height_rpm));
            } else {
                fprintf(sim_log, SELF_CAN_WCB "*** Motor2::INC\n");
            }
            ctx->_sim_motor_tilt_status = RecMotorTiltDirection_INC;
            ctx->_sim_motor_tilt_inc = motor2_inc;
            ctx->_sim_motor_tilt_ts = get_ts();
            // todo : calculate increment per sim_delay cycle
        } else if (motor3_dir == MotorTiltDirection_DEC && motor3_rpm > 0) {
            if (_sae_debug) {
                fprintf(sim_log, SELF_CAN_WCB "*** Motor2::DEC [ step:%f, delay:%d, move_time:%d ] \n",
                        sae_pos_fp(-motor2_inc), ctx->_sim_delay, sae_estimate_move_time(ctx->_sim_motor_tilt_rpm));
            } else {
                fprintf(sim_log, SELF_CAN_WCB "*** Motor2::DEC\n");
            }
            ctx->_sim_motor_tilt_status = RecMotorTiltDirection_DEC;
            ctx->_sim_motor_tilt_inc = -motor2_inc;
            ctx->_sim_motor_tilt_ts = get_ts();
        } else {
            fprintf(sim_log, SELF_CAN_WCB "Warning! Unhandled motor status: 0x%02X\n", motor3_dir);
        }
    }
    if (cf->can_id == 0x705) { // CAN_SECU1_CMD_1_FRAME_ID
        unsigned char motor1_dir = (cf->data[0] >> 0) & 0x3;
        unsigned char motor2_dir = (cf->data[0] >> 2) & 0x3;
        unsigned char motor3_dir = (cf->data[0] >> 4) & 0x3;
        unsigned char motor4_dir = (cf->data[0] >> 6) & 0x3;

        unsigned char motor1_rpm = cf->data[1];
        unsigned char motor2_rpm = cf->data[2];
        unsigned char motor3_rpm = cf->data[3];
        unsigned char motor4_rpm = cf->data[4];

        fprintf(sim_log, SELF_CAN_WCB "SECU1_CMD_1 { m1_dir:%d, m1_rpm:%d,  m2_dir:%d, m2_rpm:%d,  m3_dir:%d, m3_rpm:%d,  m4_dir:%d, m4_rpm:%d }\n",
                motor1_dir, motor1_rpm, motor2_dir, motor2_rpm, motor3_dir, motor3_rpm, motor4_dir, motor4_rpm);

        ctx->_sim_motor_height_rpm = motor1_rpm;

        int motor_inc = sae_height_increment(ctx); // calculate increase per read 'tick' (defined by sim_delay)
        if (motor1_dir == MotorHeightDirection_OFF) {
            fprintf(sim_log, SELF_CAN_WCB "*** Motor3::OFF\n");
            ctx->_sim_motor_height_status = MotorHeightDirection_OFF;
            ctx->_sim_motor_height_rpm = 0; // make sure it is zero
            ctx->_sim_motor_height_ts = -1; // reset move timestamp
            ctx->_sim_motor_height_inc = 0;
        } else if (motor1_dir == MotorHeightDirection_INC && motor1_rpm > 0) {
            if (_sae_debug) {
                fprintf(sim_log, SELF_CAN_WCB "*** Motor3::INC [ step:%f, delay:%d, move_time:%d ] \n",
                        sae_pos_fp(motor_inc), ctx->_sim_delay, sae_estimate_move_time(ctx->_sim_motor_height_rpm));
            } else {
                fprintf(sim_log, SELF_CAN_WCB "*** Motor3::INC\n");
            }
            ctx->_sim_motor_height_status = MotorHeightDirection_INC;
            ctx->_sim_motor_height_inc = motor_inc;
            ctx->_sim_motor_height_ts = get_ts();
            // todo : calculate increment per sim_delay cycle
        } else if (motor1_dir == MotorHeightDirection_DEC && motor1_rpm > 0) {
            if (_sae_debug) {
                fprintf(sim_log, SELF_CAN_WCB "*** Motor3::DEC [ step:%f, delay:%d, move_time:%d ] \n",
                        sae_pos_fp(-motor_inc), ctx->_sim_delay, sae_estimate_move_time(ctx->_sim_motor_height_rpm));
            } else {
                fprintf(sim_log, SELF_CAN_WCB "*** Motor3::DEC\n");
            }
            ctx->_sim_motor_height_status = MotorHeightDirection_DEC;
            ctx->_sim_motor_height_inc = -motor_inc;
            ctx->_sim_motor_height_ts = get_ts();
        } else {
            fprintf(sim_log, SELF_CAN_WCB "Warning! Unhandled motor status: 0x%02X\n", motor1_dir);
        }
    }
    return len;
}
