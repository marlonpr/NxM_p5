#include "ds18b20.h"
#include "led_panel.h"
#include "ds3231.h"
#include "logo.h"

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
	DISPLAY_LOGO,
	DISPLAY_LOGO2,
	DISPLAY_LOGO3,
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

            draw_text(8, 8, buf, 255, 255, 255); // green
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

			scroll_text(buf, 8, 255, 255, 0, 15);
            break;
        }
		case DISPLAY_TEMPERATURE: {
		    char buf[32];
		    if (temp_valid) {
		        snprintf(buf, sizeof(buf), "%d*C", current_temp);
		    } else {
		        snprintf(buf, sizeof(buf), "TEMP ERROR");
		    }
		    draw_text(66, 8, buf, 0, 255, 255);
		    break;
		}
		case DISPLAY_LOGO: {
			draw_bitmap_rgb(64,16,logo_bitmap, LOGO_WIDTH, LOGO_HEIGHT);
			break;
		}
		case DISPLAY_LOGO2: {
			draw_bitmap_rgb(64,16,logo2_bitmap, LOGO_WIDTH, LOGO_HEIGHT);
			break;
		}
		case DISPLAY_LOGO3: {
			draw_bitmap_rgb(64,16,logo3_bitmap, LOGO_WIDTH, LOGO_HEIGHT);
			break;
		}
    }
    swap_buffers();
}

void drawing_task(void *arg)
{
    ds3231_dev_t *rtc = (ds3231_dev_t *)arg;
    display_mode_t mode = DISPLAY_TIME;
    const int mode_interval_s = 7; // seconds per mode

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
                vTaskDelay(pdMS_TO_TICKS(mode_interval_s * 500));
                break;
            case DISPLAY_LOGO:
                draw_display(DISPLAY_LOGO, &now);
                vTaskDelay(pdMS_TO_TICKS(mode_interval_s * 200));
                break;
            case DISPLAY_LOGO2:
                draw_display(DISPLAY_LOGO2, &now);
                vTaskDelay(pdMS_TO_TICKS(mode_interval_s * 200));
                break;
            case DISPLAY_LOGO3:
                draw_display(DISPLAY_LOGO3, &now);
                vTaskDelay(pdMS_TO_TICKS(mode_interval_s * 300));
                break;
        }
        mode++; // switch to next mode
        if (mode > DISPLAY_LOGO3) mode = DISPLAY_TIME;
    }
}

// ---------------- example usage in app_main ----------------
void app_main(void)
{
    init_pins();

	init_oe_pwm();           // initialize OE PWM
	set_global_brightness(100);  // 50% brightness

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

	init_planes();

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

