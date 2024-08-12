/*
 * Copyright (c) 2015-2020, Texas Instruments Incorporated
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * *  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * *  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * *  Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 *  ======== gpiointerrupt.c ========
 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* Driver Header files */
#include <ti/drivers/GPIO.h>
#include <ti/drivers/Timer.h>
#include <ti/drivers/I2C.h>
#include <ti/drivers/UART2.h>

/* Driver configuration */
#include "ti_drivers_config.h"

/* Definitions */
#define TIMER_PERIOD 100
#define NUM_TASKS 3
// DISPLAY macro definition reference: https://stackoverflow.com/questions/66304786/sprintf-with-elipses-in-c-for-macro-definition-results-in-compilation-error
#define DISPLAY(fmt, ...) do { \
    snprintf(output, sizeof(output), fmt, ##__VA_ARGS__);\
    UART2_write(UART2, output, strlen(output), &bytesToSend);\
} while (0)

// Driver Handles
UART2_Handle UART2;
Timer_Handle timer0;
I2C_Handle  i2c;

/*
 * ======== Task Struct ========
 */
typedef struct task {
    int state;
    unsigned long period;
    unsigned long elapsedTime;
    int (*TickFct)(int);
} task;

task tasks[NUM_TASKS];

/*
 * ======== Global Variables ========
 */
// Timer Global Variables
volatile unsigned char TimerFlag = 0;

// UART2 Global Variables
char  output[64];
size_t  bytesToSend;

// I2C Global Variables
static const struct {
    uint8_t address;
    uint8_t resultReg;
    char *id;
}
sensors[3] = {
    { 0x48, 0x0000, "11X" },
    { 0x49, 0x0000, "116" },
    { 0x41, 0x0001, "006" }
};
uint8_t txBuffer[1];
uint8_t rxBuffer[2];
I2C_Transaction i2cTransaction;

// Thermostat Global Variables
int setPointTemp = 20;
int16_t temperature = 0;
volatile bool heatOn = 0;
int seconds = 0;
volatile bool increaseTemp = 0;
volatile bool decreaseTemp = 0;

// Enum for States
enum BUTTON_STATES {INCREASE_TEMP, DECREASE_TEMP, BUTTON_WAIT} BUTTON_STATE;
enum HEAT_STATES {HEAT_ON, HEAT_OFF, HEAT_WAIT} HEAT_STATE;
enum UART2_STATES {UART2_UPDATE, UART2_WAIT} UART2_STATE;

/*
 *  ======== Callbacks ========
 */

// GPIO callback to increase temperature
void gpioIncreaseTempCallback(uint_least8_t index)
{
    increaseTemp = 1;
}

// GPIO callback to decrease temperature
void gpioDecreaseTempCallback(uint_least8_t index)
{
    decreaseTemp = 1;
}

// Timer callback
void timerCallback(Timer_Handle myHandle, int_fast16_t status){
   TimerFlag = 1;
}

/*
 * ======== Initialize Drivers ========
 */

// Initialize UART2
void initUART2(void) {
    UART2_Params UART2Params;

    // Configure the driver
    UART2_Params_init(&UART2Params);
    UART2Params.baudRate = 115200;
    UART2Params.readMode = UART2_Mode_BLOCKING;
    UART2Params.writeMode = UART2_Mode_BLOCKING;

    // Open the driver
    UART2 = UART2_open(CONFIG_UART2_0, &UART2Params);

    if (UART2 == NULL) {
        /* UART2_open() failed */
        while (1);
    }
}

// Initialize I2C
void initI2C(void) {
    int8_t  i, found;
    I2C_Params  i2cParams;

    DISPLAY("Initializing I2C Driver - ");

    // Init the driver
    I2C_init();

    // Configure the driver
    I2C_Params_init(&i2cParams);
    i2cParams.bitRate = I2C_400kHz;

    // Open the driver
    i2c = I2C_open(CONFIG_I2C_0, &i2cParams);
    if (i2c == NULL) {
        DISPLAY("Failed\n\r");
        while (1);
    }

    DISPLAY("Passed\n\r");

    // Boards were shipped with different sensors.
    // Welcome to the world of embedded systems.
    // Try to determine which sensor we have.
    // Scan through the possible sensor addresses

    /* Common I2C transaction setup */
    i2cTransaction.writeBuf   = txBuffer;
    i2cTransaction.writeCount = 1;
    i2cTransaction.readBuf = rxBuffer;
    i2cTransaction.readCount  = 0;

    found = false;
    for (i=0; i<3; ++i) {
        i2cTransaction.targetAddress = sensors[i].address;
        txBuffer[0] = sensors[i].resultReg;

        DISPLAY("Is this %s? ", sensors[i].id);
        if (I2C_transfer(i2c, &i2cTransaction)) {
            DISPLAY("Found\n\r");
            found = true;
            break;
        }
        DISPLAY("No\n\r");
    }
    if(found) {
        DISPLAY("Detected TMP%s I2C address: %x\n\r", sensors[i].id, i2cTransaction.targetAddress);
    } else {
        DISPLAY("Temperature sensor not found, contact professor\n\r");
    }
}

// Initialize GPIO
void initGPIO(void) {
    /* Init the driver */
    GPIO_init();

    /* Configure the LED and button pins */
    GPIO_setConfig(CONFIG_GPIO_LED_0, GPIO_CFG_OUT_STD | GPIO_CFG_OUT_LOW);
    GPIO_setConfig(CONFIG_GPIO_BUTTON_0, GPIO_CFG_IN_PU | GPIO_CFG_IN_INT_FALLING);

    /* Turn off LED */
    GPIO_write(CONFIG_GPIO_LED_0, CONFIG_GPIO_LED_OFF);

    /* Install Button callback */
    GPIO_setCallback(CONFIG_GPIO_BUTTON_0, gpioIncreaseTempCallback);

    /* Enable interrupts */
    GPIO_enableInt(CONFIG_GPIO_BUTTON_0);

    /*
     *  If more than one input pin is available for your device, interrupts
     *  will be enabled on CONFIG_GPIO_BUTTON1.
     */
    if (CONFIG_GPIO_BUTTON_0 != CONFIG_GPIO_BUTTON_1)
    {
        /* Configure BUTTON1 pin */
        GPIO_setConfig(CONFIG_GPIO_BUTTON_1, GPIO_CFG_IN_PU | GPIO_CFG_IN_INT_FALLING);

        /* Install Button callback */
        GPIO_setCallback(CONFIG_GPIO_BUTTON_1, gpioDecreaseTempCallback);
        GPIO_enableInt(CONFIG_GPIO_BUTTON_1);
    }

}

// Initialize timer
void initTimer(void) {
    Timer_Params params;

    // Init the driver
    Timer_init();

    // Configure the driver
    Timer_Params_init(&params);
    params.period = 100000;
    params.periodUnits = Timer_PERIOD_US;
    params.timerMode = Timer_CONTINUOUS_CALLBACK;
    params.timerCallback = timerCallback;

    // Open the driver
    timer0 = Timer_open(CONFIG_TIMER_0, &params);
    if (timer0 == NULL) {
        /* Failed to initialized timer */
        while (1) {}
    }

    if (Timer_start(timer0) == Timer_STATUS_ERROR) {
        /* Failed to start timer */ while (1) {}
    }
}

/*
 * ======== changeSetPointTemp ========
 */
int changeSetPointTemp(int state) {
    if (increaseTemp) {
        state = INCREASE_TEMP;
    } else if (decreaseTemp) {
        state = DECREASE_TEMP;
    } else {
        state = BUTTON_WAIT;
    }

    switch (state) {
    case INCREASE_TEMP:
        if (setPointTemp < 40) {
            setPointTemp +=1;
            increaseTemp = 0;
        }
        state = BUTTON_WAIT;
        break;
    case DECREASE_TEMP:
        if (setPointTemp > 10) {
            setPointTemp -=1;
            decreaseTemp = 0;
        }
        state = BUTTON_WAIT;
        break;
    default:
        state = BUTTON_WAIT;
        break;
    }
    BUTTON_STATE = state;
    return state;
}


/*
 *  ======== readTemp ========
 *  Reads current temp from sensor.
 */
int16_t readTemp(void) {
    int j;
    i2cTransaction.readCount  = 2;

    if (I2C_transfer(i2c, &i2cTransaction)) {
        /* * Extract degrees C from the received data; * see TMP sensor datasheet */
        temperature = (rxBuffer[0] << 8) | (rxBuffer[1]);
        temperature *= 0.0078125;

        /* * If the MSB is set '1', then we have a 2's complement * negative value which needs to be sign extended */
        if (rxBuffer[0] & 0x80) {
            temperature |= 0xF000;
        }
    } else {
        DISPLAY("Error reading temperature sensor (%d)\n\r",i2cTransaction.status);
        DISPLAY("Please power cycle your board by unplugging USB and plugging back in.\n\r");
    }
    return temperature;

}

/*
 * ======== adjustHeat ========
 */
int adjustHeat(int state) {
    temperature = readTemp();
    if (temperature >= setPointTemp) {
        heatOn = 0;
        GPIO_write(CONFIG_GPIO_LED_0, CONFIG_GPIO_LED_OFF); // Turn off LED
    }
    else {
        heatOn = 1;
        GPIO_write(CONFIG_GPIO_LED_0, CONFIG_GPIO_LED_ON); // Turn on LED
    }
    state = HEAT_WAIT;
    return state;

}

/*
 * ======== UART2Output ========
 */
int UART2Output(int state) {
    DISPLAY("<%02d, %02d, %d, %04d>\n\r", temperature, setPointTemp, heatOn, seconds);
    return state;
}

/*
 *  ======== mainThread ========
 */
void *mainThread(void *arg0)
{
    task tasks[NUM_TASKS] = {
                            // Task 0: Check button state, change set-point temp
                            {.state = BUTTON_WAIT,
                             .period = 200,
                             .elapsedTime = 200,
                             .TickFct = &changeSetPointTemp
                            },
                            // Task 1: Read temp sensor and adjust heat (update LED)
                            {.state = HEAT_WAIT,
                             .period = 500,
                             .elapsedTime = 500,
                             .TickFct = &adjustHeat
                            },
                            // Task 2: Update server
                            {.state = UART2_WAIT,
                             .period = 1000,
                             .elapsedTime = 1000,
                             .TickFct = &UART2Output
                            }
    };
    /* Call driver init functions */
    initUART2();
    initI2C();
    initGPIO();
    initTimer();

    while (1) {
        unsigned char i;
        for (i = 0; i < NUM_TASKS; ++i) {
            if (tasks[i].elapsedTime >= tasks[i].period) {
                tasks[i].state = tasks[i].TickFct(tasks[i].state);
                tasks[i].elapsedTime = 0;
            }
            tasks[i].elapsedTime += TIMER_PERIOD;
        }

        while (!TimerFlag){} // wait for timer period
        TimerFlag = 0;      // lower flag raised by timer
        ++seconds;
    }

    return (NULL);
}
