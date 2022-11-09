#include "quenon_dht_mon.h"

//I / O ports that were not indicated in the general list
const GpioPin SWC_10 = {.pin = LL_GPIO_PIN_14, .port = GPIOA};
const GpioPin SIO_12 = {.pin = LL_GPIO_PIN_13, .port = GPIOA};
const GpioPin TX_13 = {.pin = LL_GPIO_PIN_6, .port = GPIOB};
const GpioPin RX_14 = {.pin = LL_GPIO_PIN_7, .port = GPIOB};

//Number of available I/O ports
#define GPIO_ITEMS (sizeof(gpio_item) / sizeof(GpioItem))

//List of available I/O ports
static const GpioItem gpio_item[] = {
    {2, "2 (A7)", &gpio_ext_pa7},
    {3, "3 (A6)", &gpio_ext_pa6},
    {4, "4 (A4)", &gpio_ext_pa4},
    {5, "5 (B3)", &gpio_ext_pb3},
    {6, "6 (B2)", &gpio_ext_pb2},
    {7, "7 (C3)", &gpio_ext_pc3},
    {10, " 10(SWC) ", &SWC_10},
    {12, "12 (SIO)", &SIO_12},
    {13, "13 (TX)", &TX_13},
    {14, "14 (RX)", &RX_14},
    {15, "15 (C1)", &gpio_ext_pc1},
    {16, "16 (C0)", &gpio_ext_pc0},
    {17, "17 (1W)", &ibutton_gpio}};

//Plugin data
static PluginData* app;

uint8_t DHTMon_GPIO_to_int(const GpioPin* gpio) {
    if(gpio == NULL) return 255;
    for(uint8_t i = 0; i < GPIO_ITEMS; i++) {
        if(gpio_item[i].pin->pin == gpio->pin && gpio_item[i].pin->port == gpio->port) {
            return gpio_item[i].num;
        }
    }
    return 255;
}

const GpioPin* DHTMon_GPIO_form_int(uint8_t name) {
    for(uint8_t i = 0; i < GPIO_ITEMS; i++) {
        if(gpio_item[i].num == name) {
            return gpio_item[i].pin;
        }
    }
    return NULL;
}

const GpioPin* DHTMon_GPIO_from_index(uint8_t index) {
    if(index > GPIO_ITEMS) return NULL;
    return gpio_item[index].pin;
}

uint8_t DHTMon_GPIO_to_index(const GpioPin* gpio) {
    if(gpio == NULL) return 255;
    for(uint8_t i = 0; i < GPIO_ITEMS; i++) {
        if(gpio_item[i].pin->pin == gpio->pin && gpio_item[i].pin->port == gpio->port) {
            return i;
        }
    }
    return 255;
}

const char* DHTMon_GPIO_getName(const GpioPin* gpio) {
    if(gpio == NULL) return NULL;
    for(uint8_t i = 0; i < GPIO_ITEMS; i++) {
        if(gpio_item[i].pin->pin == gpio->pin && gpio_item[i].pin->port == gpio->port) {
            return gpio_item[i].name;
        }
    }
    return NULL;
}

void DHTMon_sensors_init(void) {
    // Turn on 5V if it is not on port 1 FZ
    if(furi_hal_power_is_otg_enabled() != true) {
        furi_hal_power_enable_otg();
    }

    //Configure GPIO loaded sensors
    for(uint8_t i = 0; i < app->sensors_count; i++) {
        //High default level
        furi_hal_gpio_write(app->sensors[i].GPIO, true);
        //Operating mode - OpenDrain, pull-up is turned on just in case
        furi_hal_gpio_init(
            app->sensors[i].GPIO, //FZ port
            GpioModeOutputOpenDrain, //Operating mode - open drain
            GpioPullUp, //Force pull up the data line to power
            GpioSpeedVeryHigh); //Working speed - maximum
    }
}

void DHTMon_sensors_deinit(void) {
    //Return initial state 5V
    if(app->last_OTG_State != true) {
        furi_hal_power_disable_otg();
    }

    //Translate GPIO ports to default state
    for(uint8_t i = 0; i < app->sensors_count; i++) {
        furi_hal_gpio_init(
            app->sensors[i].GPIO, //FZ port
            GpioModeAnalog, //Operating mode - analogue
            GpioPullNo, //Disable pullup
            GpioSpeedLow); //Speed - low
        //set low level
        furi_hal_gpio_write(app->sensors[i].GPIO, false);
    }
}

bool DHTMon_sensor_check(DHT_sensor* sensor) {
    /* Name check */
     //1) The string must be between 1 and 10 characters long
     //2) The first character of the string must be only 0-9, A-Z, a-z and _
    if(strlen(sensor->name) == 0 || strlen(sensor->name) > 10 ||
       (!(sensor->name[0] >= '0' && sensor->name[0] <= '9') &&
        !(sensor->name[0] >= 'A' && sensor->name[0] <= 'Z') &&
        !(sensor->name[0] >= 'a' && sensor->name[0] <= 'z') && !(sensor->name[0] == '_'))) {
        FURI_LOG_D(APP_NAME, "Sensor [%s] name check failed\r\n", sensor->name);
        return false;
    }
    //Check GPIO
    if(DHTMon_GPIO_to_int(sensor->GPIO) == 255) {
        FURI_LOG_D(
            APP_NAME,
            "Sensor [%s] GPIO check failed: %d\r\n",
            sensor->name,
            DHTMon_GPIO_to_int(sensor->GPIO));
        return false;
    }
    //Check sensor type
    if(sensor->type != DHT11 && sensor->type != DHT22) {
        FURI_LOG_D(APP_NAME, "Sensor [%s] type check failed: %d\r\n", sensor->name, sensor->type);
        return false;
    }

    // Return true if everything is ok
    FURI_LOG_D(APP_NAME, "Sensor [%s] all checks passed\r\n", sensor->name);
    return true;
}

void DHTMon_sensor_delete(DHT_sensor* sensor) {
    if(sensor == NULL) return;
    //Make sensor parameters invalid
    sensor->name[0] = '\0';
    sensor->type = 255;
    //Now save the current sensors. The saver will not save the faulty sensor
    DHTMon_sensors_save();
    // Reboot from SD card
    DHTMon_sensors_reload();
}

uint8_t DHTMon_sensors_save(void) {
    // Allocate memory for the thread
    app->file_stream = file_stream_alloc(app->storage);
    uint8_t savedSensorsCount = 0;
    //File path variable
    char filepath[sizeof(APP_PATH_FOLDER) + sizeof(APP_FILENAME)] = {0};
    // Compiling the path to the file
    strcpy(filepath, APP_PATH_FOLDER);
    strcat(filepath, "/");
    strcat(filepath, APP_FILENAME);

    //Opening a stream. If the stream has opened, then saving the sensors
    if(file_stream_open(app->file_stream, filepath, FSAM_READ_WRITE, FSOM_CREATE_ALWAYS)) {
        const char template[] =
            "#DHT monitor sensors file\n#Name - name of sensor. Up to 10 sumbols\n#Type - type of sensor. DHT11 - 0, DHT22 - 1\n#GPIO - connection port. May being 2-7, 10, 12-17\n#Name Type GPIO\n";
        stream_write(app->file_stream, (uint8_t*)template, strlen(template));
        //Save sensors
        for(uint8_t i = 0; i < app->sensors_count; i++) {
            //If the sensor parameters are correct, then save
            if(DHTMon_sensor_check(&app->sensors[i])) {
                stream_write_format(
                    app->file_stream,
                    "%s %d %d\n",
                    app->sensors[i].name,
                    app->sensors[i].type,
                    DHTMon_GPIO_to_int(app->sensors[i].GPIO));
                savedSensorsCount++;
            }
        }
    } else {
        //TODO: print the error to the screen
        FURI_LOG_E(APP_NAME, "cannot create sensors file\r\n");
    }
    stream_free(app->file_stream);

    return savedSensorsCount;
}

bool DHTMon_sensors_load(void) {
    //Resetting the number of sensors
    app->sensors_count = -1;
    //Clear previous sensors
    memset(app->sensors, 0, sizeof(app->sensors));

    //Open file on SD card
    // Allocate memory for the thread
    app->file_stream = file_stream_alloc(app->storage);
    //File path variable
    char filepath[sizeof(APP_PATH_FOLDER) + sizeof(APP_FILENAME)] = {0};
    // Compiling the path to the file
    strcpy(filepath, APP_PATH_FOLDER);
    strcat(filepath, "/");
    strcat(filepath, APP_FILENAME);
    //Open stream to file
    if(!file_stream_open(app->file_stream, filepath, FSAM_READ_WRITE, FSOM_OPEN_EXISTING)) {
        //If the file is missing, then create a blank
        FURI_LOG_W(APP_NAME, "Missing sensors file. Creating new file\r\n");
        app->sensors_count = 0;
        stream_free(app->file_stream);
        DHTMon_sensors_save();
        return false;
    }
    //Calculate file size
    size_t file_size = stream_size(app->file_stream);
    if(file_size == (size_t)0) {
        //Exit if file is empty
        FURI_LOG_W(APP_NAME, "Sensors file is empty\r\n");
        app->sensors_count = 0;
        stream_free(app->file_stream);
        return false;
    }

    //Allocate memory for file upload
    uint8_t* file_buf = malloc(file_size);
    // Empty the file buffer
    memset(file_buf, 0, file_size);
    //Upload file
    if(stream_read(app->file_stream, file_buf, file_size) != file_size) {
        //Exit on read error
        FURI_LOG_E(APP_NAME, "Error reading sensor file\r\n");
        app->sensors_count = 0;
        stream_free(app->file_stream);
        return false;
    }
    //Read file line by line
    char* line = strtok((char*)file_buf, "\n");
    while(line != NULL && app->sensors_count < MAX_SENSORS) {
        if(line[0] != '#') {
            DHT_sensor s = {0};
            int type, port;
            char name[11];
            sscanf(line, "%s %d %d", name, &type, &port);
            s.type = type;
            s.GPIO = DHTMon_GPIO_form_int(port);

            name[10] = '\0';
            strcpy(s.name, name);
            //If the data is correct, then
            if(DHTMon_sensor_check(&s) == true) {
                //Zero setting at the first probe
                if(app->sensors_count == -1) app->sensors_count = 0;
                //Adding a sensor to the general list
                app->sensors[app->sensors_count] = s;
                //Increase the number of loaded sensors
                app->sensors_count++;
            }
        }
        line = strtok((char*)NULL, "\n");
    }
    stream_free(app->file_stream);
    free(file_buf);

    //Resetting the number of sensors if none of them have been loaded
    if(app->sensors_count == -1) app->sensors_count = 0;

    //Initialization of sensor ports, if any
    if(app->sensors_count > 0) {
        DHTMon_sensors_init();
        return true;
    } else {
        return false;
    }
    return false;
}

bool DHTMon_sensors_reload(void) {
    DHTMon_sensors_deinit();
    return DHTMon_sensors_load();
}

/**
 * @brief Screen rendering handler
 * 
 * @param canvas Pointer to canvas
 * @param ctx Plugin data
 */
static void render_callback(Canvas* const canvas, void* ctx) {
    PluginData* app = acquire_mutex((ValueMutex*)ctx, 25);
    if(app == NULL) {
        return;
    }
    //Call to draw the main screen
    scene_main(canvas, app);

    release_mutex((ValueMutex*)ctx, app);
}

/**
 * @brief Home button click handler
 * 
 * @param input_event Event pointer
 * @param event_queue Event Queue Pointer
 */
static void input_callback(InputEvent* input_event, FuriMessageQueue* event_queue) {
    furi_assert(event_queue);

    PluginEvent event = {.type = EventTypeKey, .input = *input_event};
    furi_message_queue_put(event_queue, &event, FuriWaitForever);
}

/**
 * @brief Allocate space for plugin variables
 * 
 * @return true If everything went well
 * @return false If an error occurred during the download process
 */
static bool DHTMon_alloc(void) {
    //Allocate space for plugin data
    app = malloc(sizeof(PluginData));
    // Allocate space for the event queue
    app->event_queue = furi_message_queue_alloc(8, sizeof(PluginEvent));

    //Resetting the number of sensors
    app->sensors_count = -1;

    // Mutex initialization
    if(!init_mutex(&app->state_mutex, app, sizeof(PluginData))) {
        FURI_LOG_E(APP_NAME, "cannot create mutex\r\n");
        return false;
    }

    // Set system callbacks
    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, render_callback, &app->state_mutex);
    view_port_input_callback_set(app->view_port, input_callback, app->event_queue);

    // Open GUI and register view_port
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    app->view_dispatcher = view_dispatcher_alloc();

    sensorActions_sceneCreate(app);
    sensorEdit_sceneCreate(app);

    app->widget = widget_alloc();
    view_dispatcher_add_view(app->view_dispatcher, WIDGET_VIEW, widget_get_view(app->widget));

    app->text_input = text_input_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, TEXTINPUT_VIEW, text_input_get_view(app->text_input));

    view_dispatcher_enable_queue(app->view_dispatcher);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    //Notifications
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    //Preparing storage
    app->storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(app->storage, APP_PATH_FOLDER);
    app->file_stream = file_stream_alloc(app->storage);

    return true;
}

/**
 * @brief Freeing up memory after running the application
 */
static void DHTMon_free(void) {
    //Automatic backlight control
    notification_message(app->notifications, &sequence_display_backlight_enforce_auto);

    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_NOTIFICATION);

    text_input_free(app->text_input);
    widget_free(app->widget);
    sensorEdit_sceneRemove();
    sensorActions_screneRemove();
    view_dispatcher_free(app->view_dispatcher);

    furi_record_close(RECORD_GUI);

    view_port_enabled_set(app->view_port, false);
    gui_remove_view_port(app->gui, app->view_port);

    view_port_free(app->view_port);
    furi_message_queue_free(app->event_queue);
    delete_mutex(&app->state_mutex);

    free(app);
}

/**
 * @brief Application entry point
 * 
 * @return Error code
 */
int32_t quenon_dht_mon_app() {
    if(!DHTMon_alloc()) {
        DHTMon_free();
        return 255;
    }
    //Permanent illumination of the backlight
    notification_message(app->notifications, &sequence_display_backlight_enforce_on);
    //Save 5V state on port 1 FZ
    app->last_OTG_State = furi_hal_power_is_otg_enabled();

    //Load sensors from SD card
    DHTMon_sensors_load();

    app->currentSensorEdit = &app->sensors[0];

    PluginEvent event;
    for(bool processing = true; processing;) {
        FuriStatus event_status = furi_message_queue_get(app->event_queue, &event, 100);

        acquire_mutex_block(&app->state_mutex);

        if(event_status == FuriStatusOk) {
            // press events
            if(event.type == EventTypeKey) {
                if(event.input.type == InputTypePress) {
                    switch(event.input.key) {
                    case InputKeyUp:
                        break;
                    case InputKeyDown:
                        break;
                    case InputKeyRight:
                        break;
                    case InputKeyLeft:
                        break;
                    case InputKeyMAX:
                        break;
                    case InputKeyOk:
                        view_port_update(app->view_port);
                        release_mutex(&app->state_mutex, app);
                        mainMenu_scene(app);
                        break;
                    case InputKeyBack:
                        processing = false;
                        break;
                    }
                }
            }
        } else {
            FURI_LOG_D(APP_NAME, "FuriMessageQueue: event timeout");
            // event timeout
        }

        view_port_update(app->view_port);
        release_mutex(&app->state_mutex, app);
    }
    //Free memory and deinitialize
    DHTMon_sensors_deinit();
    DHTMon_free();

    return 0;
}
//TODO: Handling errors
//TODO: Skip used ports in the add sensors menu