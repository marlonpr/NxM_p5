#include "ds18b20.h"
#include "led_panel.h"
#include "ds3231.h"
#include "esp_log.h"



static ds18b20_t sensor;

static int16_t current_temp = 0;
static bool temp_valid = false;

void temp_task(void *arg)
{
    while (1) {
        int16_t t;
        if (ds18b20_read_temperature_int(&sensor, &t) == ESP_OK) {
            current_temp = t;
            temp_valid = true;
        } else {
            temp_valid = false;
        }
        vTaskDelay(pdMS_TO_TICKS(5000)); // read every 5s
    }
}



typedef enum {
    DISPLAY_TIME = 0,
    DISPLAY_DATE,
	DISPLAY_TEMPERATURE,
    // Add more modes here later, e.g., DISPLAY_TEMPERATURE
} display_mode_t;

const char *dias_semana[] = {
    "DOMINGO", "LUNES", "MARTES", "MIERCOLES", "JUEVES", "VIERNES", "SABADO"
};

const char *meses[] = {
    "ENERO", "FEBRERO", "MARZO", "ABRIL", "MAYO", "JUNIO",
    "JULIO", "AGOSTO", "SEPTIEMBRE", "OCTUBRE", "NOVIEMBRE", "DICIEMBRE"
};

void draw_display(display_mode_t mode, ds3231_time_t *time)
{
    clear_back_buffer();

    switch (mode) {
        case DISPLAY_TIME: {
            // 12-hour time with left digit off if zero
            int hour12 = time->hour % 12;
            if (hour12 == 0) hour12 = 12;

            char buf[16];
            if (hour12 < 10) {
                snprintf(buf, sizeof(buf), " %1d:%02d:%02d", hour12, time->minute, time->second);
            } else {
                snprintf(buf, sizeof(buf), "%02d:%02d:%02d", hour12, time->minute, time->second);
            }

            draw_text(8, 8, buf, 1, 1, 1); // green
            break;
        }

        case DISPLAY_DATE: {
            int weekday_index = (time->day_of_week - 1) % 7;

            char buf[32];
            snprintf(buf, sizeof(buf), "%s %02d %s %04d",
                     dias_semana[weekday_index],
                     time->day,
                     meses[time->month - 1],
                     time->year);

			scroll_text(buf, 8, 1, 1, 1, 15);
            break;
        }

        // Add more cases here for future screens
		case DISPLAY_TEMPERATURE: {
		    char buf[32];
		    if (temp_valid) {
		        snprintf(buf, sizeof(buf), "%d*C", current_temp);
		    } else {
		        snprintf(buf, sizeof(buf), "TEMP ERROR");
		    }
		    draw_text(66, 8, buf, 1, 1, 1);
		    break;
		}

    }

    swap_buffers();
}

void drawing_task(void *arg)
{
    ds3231_dev_t *rtc = (ds3231_dev_t *)arg;
    display_mode_t mode = DISPLAY_TIME;
    const int mode_interval_s = 5; // seconds per mode

    while (1) {
        ds3231_time_t now;
        ESP_ERROR_CHECK(ds3231_get_time(rtc, &now));

        switch (mode) {
            case DISPLAY_TIME:
                // update every second
                for (int i = 0; i < mode_interval_s; i++) {
                    ESP_ERROR_CHECK(ds3231_get_time(rtc, &now));
                    draw_display(DISPLAY_TIME, &now);
                    vTaskDelay(pdMS_TO_TICKS(1000));
                }
                break;

            case DISPLAY_DATE:
                draw_display(DISPLAY_DATE, &now);
                //vTaskDelay(pdMS_TO_TICKS(mode_interval_s * 1000));
                break;

            case DISPLAY_TEMPERATURE:
                draw_display(DISPLAY_TEMPERATURE, &now);
                vTaskDelay(pdMS_TO_TICKS(mode_interval_s * 1000));
                break;
        }

        // switch to next mode
        mode++;
        if (mode > DISPLAY_TEMPERATURE) mode = DISPLAY_TIME;
    }
}




// ---------------- example usage in app_main ----------------
void app_main(void)
{
    init_pins();

	//init_oe_pwm();
	//set_global_brightness(255); //0 - 255

    ds3231_dev_t rtc;
    ESP_ERROR_CHECK(init_ds3231(&rtc));

    ds18b20_init(&sensor, GPIO_NUM_27); // Use GPIO4 with 4.7kΩ pull-up resistor

	ds3231_time_t now;
	ds3231_get_time(&rtc, &now);

	if(now.year < 2025)
	{
		ds3231_time_t set_time = {2025, 8, 18, 13, 52, 0, 2};
    	ESP_ERROR_CHECK(ds3231_set_time(&rtc, &set_time));
	}

    // Clear both buffers first time
    memset((void*)fbA, 0, sizeof(fbA));
    memset((void*)fbB, 0, sizeof(fbB));

    // Start refresh task (pin-driving) on core 0
	xTaskCreatePinnedToCore(refresh_task, "refresh_task", 2048, NULL, 1, NULL, 0);

	xTaskCreatePinnedToCore(drawing_task, "DrawTime", 4096, &rtc, 1, NULL, 1);
	
	xTaskCreatePinnedToCore(temp_task,      "TempTask",      1024, NULL, 2, NULL, 1);


    while (true) 
	{
        vTaskDelay(pdMS_TO_TICKS(1));

    }
}


