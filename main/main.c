#include "led_panel.h"

scroll_text_t my_scroll = {
    .text  = "HELLO WORLD!\n123 ABC xyz",
    .pos_x = N_HOR * 64,  // start off-screen right
    .speed = 1,
    .lines = 1,             // number of lines in text
	.color = 1,
	.done = 0
};

void drawing_task(void *arg)
{
	while(1)
	{

    // Draw
    clear_back_buffer();
 	scroll_text_20x40("HELLO! 1 2 3 4 5 6 7 8 9 0", 10, 0, 1, 0, 20);	





	    clear_back_buffer();
		draw_text_20x40(50, 10, " 4:37", 1, 0, 0);
		swap_buffers();
        vTaskDelay(pdMS_TO_TICKS(3000));
	}
}

// ---------------- example usage in app_main ----------------
void app_main(void)
{
    init_pins();

	init_oe_pwm();
	set_global_brightness(255); //0 - 255

    // Clear both buffers first time
    memset((void*)fbA, 0, sizeof(fbA));
    memset((void*)fbB, 0, sizeof(fbB));

    // Start refresh task (pin-driving) on core 0
	xTaskCreatePinnedToCore(refresh_task, "refresh_task", 2048, NULL, 1, NULL, 0);

	xTaskCreatePinnedToCore(drawing_task,         "Draw",    4096, NULL, 1, NULL, 1);
	//xTaskCreatePinnedToCore(background_task,      "BG",      1024, NULL, 1, NULL, 1);


	my_scroll.text = "HELLO! 0 1 2 3 4 5 6 7 8 9 0";
	my_scroll.color = 2;

    while (true) 
	{
        vTaskDelay(pdMS_TO_TICKS(1));

    }
}





/*
Color mapping (1–7 for RGB combinations):
Color	RGB Bits	Description
1	001	Blue
2	010	Green
3	011	Green + Blue
4	100	Red
5	101	Red + Blue
6	110	Red + Green
7	111	Red + Green + Blue (white)
*/