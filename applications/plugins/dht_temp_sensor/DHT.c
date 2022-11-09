#include "DHT.h"

#define lineDown() furi_hal_gpio_write(sensor->GPIO, false)
#define lineUp() furi_hal_gpio_write(sensor->GPIO, true)
#define getLine() furi_hal_gpio_read(sensor->GPIO)
#define Delay(d) furi_delay_ms(d)

DHT_data DHT_getData(DHT_sensor* sensor) {
    DHT_data data = {-128.0f, -128.0f};

#if DHT_POLLING_CONTROL == 1
    /* Limitation on the frequency of polling the sensor */
    //Determine the polling interval depending on the sensor
    uint16_t pollingInterval;
    if(sensor->type == DHT11) {
        pollingInterval = DHT_POLLING_INTERVAL_DHT11;
    } else {
        pollingInterval = DHT_POLLING_INTERVAL_DHT22;
    }

    //If the interval is small, then return the last known good value
    if((furi_get_tick() - sensor->lastPollingTime < pollingInterval) &&
       sensor->lastPollingTime != 0) {
        data.hum = sensor->lastHum;
        data.temp = sensor->lastTemp;
        return data;
    }
    sensor->lastPollingTime = furi_get_tick() + 1;
#endif

    //Down data line by 18ms
    lineDown();
#ifdef DHT_IRQ_CONTROL
    // Turn off interrupts so that nothing interferes with data processing
    __disable_irq();
#endif
    Delay(18);

    //Raise the line
    lineUp();

    /* Waiting for a response from the sensor */
    uint16_t timeout = 0;
    while(!getLine()) {
        timeout++;
        if(timeout > DHT_TIMEOUT) {
#ifdef DHT_IRQ_CONTROL
            __enable_irq();
#endif
            //If the sensor did not respond, then it definitely does not exist
            //Zero last good value so that
            //you don't get phantom values
            sensor->lastHum = -128.0f;
            sensor->lastTemp = -128.0f;

            return data;
        }
    }
    //Waiting for fall
    while(getLine()) {
        timeout++;
        if(timeout > DHT_TIMEOUT) {
#ifdef DHT_IRQ_CONTROL
            __enable_irq();
#endif
            //If the sensor did not respond, then it definitely does not exist
            //Zero last good value so that
            //don't get phantom values
            sensor->lastHum = -128.0f;
            sensor->lastTemp = -128.0f;

            return data;
        }
    }
    timeout = 0;
    //Waiting for rise
    while(!getLine()) {
        timeout++;
        if(timeout > DHT_TIMEOUT) {
            if(timeout > DHT_TIMEOUT) {
#ifdef DHT_IRQ_CONTROL
                __enable_irq();
#endif
                //If the sensor did not respond, then it definitely does not exist
                //Zero last good value so that
                //don't get phantom values
                sensor->lastHum = -128.0f;
                sensor->lastTemp = -128.0f;

                return data;
            }
        }
    }
    timeout = 0;
    //Waiting for fall
    while(getLine()) {
        timeout++;
        if(timeout > DHT_TIMEOUT) {
#ifdef DHT_IRQ_CONTROL
            __enable_irq();
#endif
            //If the sensor did not respond, then it definitely does not exist
            //Zero last good value so that
            //don't get phantom values
            sensor->lastHum = -128.0f;
            sensor->lastTemp = -128.0f;
            return data;
        }
    }

    /* Read the response from the sensor */
    uint8_t rawData[5] = {0, 0, 0, 0, 0};
    for(uint8_t a = 0; a < 5; a++) {
        for(uint8_t b = 7; b != 255; b--) {
            uint16_t hT = 0, lT = 0;
            //While the line is low, increment variable lT
            while(!getLine() && lT != 65535) lT++;
            //While the line is high, increment variable hT
            timeout = 0;
            while(getLine() && hT != 65535) hT++;
            //If hT is greater than lT, then one has arrived
            if(hT > lT) rawData[a] |= (1 << b);
        }
    }
#ifdef DHT_IRQ_CONTROL
    // Enable interrupts after receiving data
    __enable_irq();
#endif
    /* Data integrity check */
    if((uint8_t)(rawData[0] + rawData[1] + rawData[2] + rawData[3]) == rawData[4]) {
        //If the checksum matches, then convert and return the resulting values
        if(sensor->type == DHT22) {
            data.hum = (float)(((uint16_t)rawData[0] << 8) | rawData[1]) * 0.1f;
            //Check for negative temperature
            if(!(rawData[2] & (1 << 7))) {
                data.temp = (float)(((uint16_t)rawData[2] << 8) | rawData[3]) * 0.1f;
            } else {
                rawData[2] &= ~(1 << 7);
                data.temp = (float)(((uint16_t)rawData[2] << 8) | rawData[3]) * -0.1f;
            }
        }
        if(sensor->type == DHT11) {
            data.hum = (float)rawData[0];
            data.temp = (float)rawData[2]*1.8+32;
            //DHT11 manufactured by ASAIR have a fractional part in temperature
            //It also measures temperature from -20 to +60 *С
            //Here's a joke, right?
            if(rawData[3] != 0) {
                //Check sign
                if(!(rawData[3] & (1 << 7))) {
                    //Add positive fractional part
                    data.temp += rawData[3] * 0.1f;
                } else {
                    // And here we make a negative value
                    rawData[3] &= ~(1 << 7);
                    data.temp += rawData[3] * 0.1f;
                    data.temp *= -1;
                }
            }
        }
    }

#if DHT_POLLING_CONTROL == 1
    sensor->lastHum = data.hum;
    sensor->lastTemp = data.temp;
#endif

    return data;
}