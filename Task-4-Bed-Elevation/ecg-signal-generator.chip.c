#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "wokwi-api.h"


typedef struct
{
    pin_t vcc;
    pin_t gnd;
    pin_t out;

    timer_t timer;

    float phase;

} chip_state_t;


// =====================================================
// ECG WAVEFORM
// =====================================================

static float generate_ecg(float phase)
{
    float value = 1.65f;


    // -------------------------------------------------
    // P WAVE
    // -------------------------------------------------

    float p = expf(
        -powf(
            (phase - 0.20f) / 0.035f,
            2.0f
        )
    );

    value += 0.20f * p;


    // -------------------------------------------------
    // Q WAVE
    // -------------------------------------------------

    float q = expf(
        -powf(
            (phase - 0.30f) / 0.010f,
            2.0f
        )
    );

    value -= 0.25f * q;


    // -------------------------------------------------
    // R WAVE
    // -------------------------------------------------

    float r = expf(
        -powf(
            (phase - 0.32f) / 0.006f,
            2.0f
        )
    );

    value += 1.20f * r;


    // -------------------------------------------------
    // S WAVE
    // -------------------------------------------------

    float s = expf(
        -powf(
            (phase - 0.345f) / 0.009f,
            2.0f
        )
    );

    value -= 0.50f * s;


    // -------------------------------------------------
    // T WAVE
    // -------------------------------------------------

    float t = expf(
        -powf(
            (phase - 0.55f) / 0.060f,
            2.0f
        )
    );

    value += 0.35f * t;


    // -------------------------------------------------
    // LIMIT OUTPUT
    // -------------------------------------------------

    if (value < 0.0f)
        value = 0.0f;

    if (value > 3.3f)
        value = 3.3f;


    return value;
}


// =====================================================
// TIMER CALLBACK
// =====================================================

static void timer_callback(void *user_data)
{
    chip_state_t *chip =
        (chip_state_t *)user_data;


    /*
     * Simulated heart rate:
     *
     * 75 BPM
     */

    const float heart_rate = 75.0f;

    const float sample_rate = 1000.0f;

    const float heart_period =
        60.0f / heart_rate;


    // Advance waveform

    chip->phase +=
        1.0f /
        (sample_rate * heart_period);


    if (chip->phase >= 1.0f)
    {
        chip->phase -= 1.0f;
    }


    // Generate voltage

    float voltage =
        generate_ecg(
            chip->phase
        );


    // Output analog voltage

    pin_dac_write(
        chip->out,
        voltage
    );
}


// =====================================================
// CHIP INITIALIZATION
// =====================================================

void chip_init(void)
{
    chip_state_t *chip =
        malloc(
            sizeof(chip_state_t)
        );


    if (!chip)
        return;


    // Initialize pins

    chip->vcc =
        pin_init(
            "VCC",
            INPUT
        );

    chip->gnd =
        pin_init(
            "GND",
            INPUT
        );

    chip->out =
        pin_init(
            "OUT",
            ANALOG
        );


    // Initial waveform phase

    chip->phase = 0.0f;


    // Timer configuration

    const timer_config_t timer_config =
    {
        .callback = timer_callback,
        .user_data = chip
    };


    chip->timer =
        timer_init(
            &timer_config
        );


    // Run every 1 ms

    timer_start(
        chip->timer,
        1000,
        true
    );


    printf(
        "ECG Signal Generator started\n"
    );
}