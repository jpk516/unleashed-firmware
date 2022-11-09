#ifndef DHT_H_
#define DHT_H_

#include <furi_hal_resources.h>

/* Settings */
#define DHT_TIMEOUT 65534 //Number of iterations after which the function will return empty values
#define DHT_POLLING_CONTROL 1 //Enable sensor polling rate check
#define DHT_POLLING_INTERVAL_DHT11 \
    2000 //DHT11 polling interval (0.5 Hz according to datasheet). You can put 1500, it will work
//Toggle, temporarily 2 seconds for AM2302 sensor
#define DHT_POLLING_INTERVAL_DHT22 2000 //DHT22 polling interval (1 Hz according to datasheet)
#define DHT_IRQ_CONTROL //Disable interrupts while communicating with the sensor
/* The structure of the data returned by the sensor */
typedef struct {
    float hum;
    float temp;
} DHT_data;

/* Type of sensor used */
typedef enum { DHT11, DHT22 } DHT_type;

/* Sensor object structure */
typedef struct {
    char name[11];
    const GpioPin* GPIO; //Sensor Pin
    DHT_type type; //Sensor type (DHT11 or DHT22)

//Sensor polling frequency control. Do not fill in the values!
#if DHT_POLLING_CONTROL == 1
    uint32_t lastPollingTime; //Time of the last sensor poll
    float lastTemp; //Last temperature value
    float lastHum; //Last humidity value
#endif
} DHT_sensor;

/* Function prototypes */
DHT_data DHT_getData(DHT_sensor* sensor); //Get data from the sensor

#endif
