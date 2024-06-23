#include "mcc_generated_files/system.h"
#include "mcc_generated_files/adc1.h"
#include "mcc_generated_files/delay.h"
#include "mcc_generated_files/led_red.h"
#include <stdio.h>
#include <stdint.h>
#include <libpic30.h>


volatile int conversion;
volatile int conversion_count;

void ADC1_CallBack(void) {
    conversion = ADC1_ConversionResultGet(LIGHT);
    conversion_count += 1;
}

#define DERIVATE_THRESHOLD 10
#define IDLE_HRESHOLD 300

typedef enum {
    Idle = 0, Negative = -1, Positive = 1
} State;

/* Measure with Exponential Moving Average*/
typedef struct {
    int prev;
    int curr;
} Measure;

/* Direction is the value changing direction */
typedef enum {
    Zero = 0, Up = 1, Down = -1
} Direction;

/* Push a value into measure, with Exponential Moving Average */
void Measure_Push(Measure *m, int i) {
    m->prev = (m->prev * 3 + m->curr) >> 2;
    m->curr = (m->curr * 3 + i) >> 2;
}

/* Calculate the change direction of recent measures */
Direction Measure_Direction(Measure *m, State s) {
    int derivate = m->curr - m->prev;

    if (abs(derivate) <= DERIVATE_THRESHOLD) {
        return Zero;
    }

    if (derivate > 0) {
        return Up;
    } else {
        return Down;
    }
}

#define TRACE_COUNT 5000

typedef struct {
    State state;
    Measure m;
    int fingers;
    int zeros;
    int output;
    int events;
} FingerCounter;

void FingerCounter_Reset(FingerCounter *fc, int output) {
    fc->state = Idle;
    fc->fingers = 0;
    fc->zeros = 0;
    fc->output = output;
    LED_RED_Off();
}

void FingerCounter_Update(FingerCounter *fc, int i) {
    Measure_Push(&fc->m, i);
    Direction d = Measure_Direction(&fc->m, fc->state);

    switch (fc->state) {
        case Idle:
            switch (d) {
                case Down: // finger enter                             
                    LED_RED_On();
                    fc->state = Negative;
                    fc->fingers = 1;
                    fc->zeros = 0;
                    break;
                case Up: // environment light increased, ignore
                    break;
                case Zero: // noise or no changes, ignore
                    break;
            }
            break;
        case Negative:
            switch (d) {
                case Up: // finger passed
                    fc->state = Positive;
                    fc->zeros = 0;

                    break;
                case Down: // already negative, ignore
                    break;
                case Zero: // noise or no changes, ignore
                    fc->zeros += 1;
                    if (fc->zeros >= 2 * IDLE_HRESHOLD) {
                        printf("error in neg\n");
                        FingerCounter_Reset(fc, 0);
                    }
                    break;
            }
            break;
        case Positive:
            switch (d) {
                case Down: // one finger enter
                    fc->state = Negative;
                    fc->fingers += 1;
                    fc->zeros = 0;
                    break;
                case Up: // already positive, ignore
                    break;
                case Zero: // noise or no changes
                    fc->zeros += 1;
                    if (fc->zeros >= IDLE_HRESHOLD) {
                        FingerCounter_Reset(fc, fc->fingers);
                    }
                    break;
            }
            break;
    }
}

int FingerCounter_ReadOutput(FingerCounter *fc) {
    int count = fc->output;
    if (count != 0) {
        fc->output = 0;
    }

    return count;
}

static FingerCounter fc;

int main(void) {
    // initialize the device
    SYSTEM_Initialize();

    // initialize ADC
    ADC1_Initialize();
    ADC1_Enable();
    ADC1_ChannelSelect(LIGHT);


    FingerCounter_Reset(&fc, 0);

    LED_RED_Off();

    int conv = 0;

    while (1) {
        conv = conversion;
        if (conv != 0) {
            conversion = 0;
            FingerCounter_Update(&fc, conv);
        }

        int count = FingerCounter_ReadOutput(&fc);
        if (count != 0) {
            printf("✅Fingers:%2d\n", count);
        }
    }

    return 1;
}
