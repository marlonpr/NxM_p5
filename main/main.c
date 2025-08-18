#include "led_panel.h"

void drawing_task(void *arg)
{
	while(1)
	{
	    // Draw
	    clear_back_buffer();
	 	scroll_text("ABCDEFGHIJKLMNOPQRSTUVWXYZ! : 1 2 3 4 5 6 7 8 9 0", 10, 0, 1, 0, 20);	

	    clear_back_buffer();
		draw_text(50, 10, " 4:37", 1, 0, 0);
		swap_buffers();
        vTaskDelay(pdMS_TO_TICKS(3000));
	}
}

// ---------------- example usage in app_main ----------------
void app_main(void)
{
    init_pins();

	//init_oe_pwm();
	//set_global_brightness(255); //0 - 255

    // Clear both buffers first time
    memset((void*)fbA, 0, sizeof(fbA));
    memset((void*)fbB, 0, sizeof(fbB));

    // Start refresh task (pin-driving) on core 0
	xTaskCreatePinnedToCore(refresh_task, "refresh_task", 2048, NULL, 1, NULL, 0);

	xTaskCreatePinnedToCore(drawing_task,         "Draw",    4096, NULL, 1, NULL, 1);
	//xTaskCreatePinnedToCore(background_task,      "BG",      1024, NULL, 1, NULL, 1);


    while (true) 
	{
        vTaskDelay(pdMS_TO_TICKS(1));

    }
}