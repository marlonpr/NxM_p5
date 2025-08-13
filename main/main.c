#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
//#include "esp_rom/ets_sys.h"
#include <string.h>
#include <stdint.h>

// ---------------- CONFIG ----------------
#define PANEL_WIDTH   64
#define PANEL_HEIGHT  32

// logical grid size (change as you need)
#define N_HOR   3   // number of panels horizontally in the logical grid
#define N_VER   2   // number of panels vertically in the logical grid

// Derived sizes
#define VIRT_WIDTH   (PANEL_WIDTH * N_HOR)     // logical drawing width
#define VIRT_HEIGHT  (PANEL_HEIGHT * N_VER)    // logical drawing height

// Physical chain: panels wired horizontally as a single row
#define PHYS_PANELS  (N_HOR * N_VER)           // number of panels in the physical horizontal chain
#define PHY_WIDTH    (PANEL_WIDTH * PHYS_PANELS) // width of physical framebuffer
#define PHY_HEIGHT   (PANEL_HEIGHT)            // physical height (one panel high)

// Pins (adjust to your board)
#define PIN_R1 GPIO_NUM_2
#define PIN_G1 GPIO_NUM_4
#define PIN_B1 GPIO_NUM_5
#define PIN_R2 GPIO_NUM_18
#define PIN_G2 GPIO_NUM_19
#define PIN_B2 GPIO_NUM_25
#define PIN_CLK GPIO_NUM_13
#define PIN_LAT GPIO_NUM_12
#define PIN_OE  GPIO_NUM_14
#define PIN_A   GPIO_NUM_15
#define PIN_B   GPIO_NUM_33
#define PIN_C   GPIO_NUM_23
// ----------------------------------------

// Pixel packed as 3 bits (bit0=R, bit1=G, bit2=B)
typedef uint8_t pixel_t;

// Double buffers in physical layout [PHY_HEIGHT][PHY_WIDTH]
static pixel_t framebufferA[PHY_HEIGHT][PHY_WIDTH];
static pixel_t framebufferB[PHY_HEIGHT][PHY_WIDTH];

static volatile pixel_t (*front_buffer)[PHY_WIDTH] = framebufferA;
static volatile pixel_t (*back_buffer)[PHY_WIDTH]  = framebufferB;

// ----------------- PIN INIT -----------------
void init_pins(void)
{
    uint64_t mask = (1ULL<<PIN_R1) | (1ULL<<PIN_G1) | (1ULL<<PIN_B1)
                  | (1ULL<<PIN_R2) | (1ULL<<PIN_G2) | (1ULL<<PIN_B2)
                  | (1ULL<<PIN_A)  | (1ULL<<PIN_B)  | (1ULL<<PIN_C)
                  | (1ULL<<PIN_CLK)| (1ULL<<PIN_LAT)| (1ULL<<PIN_OE);

    gpio_config_t io_conf = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = 0,
        .pull_up_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    gpio_set_level(PIN_OE, 1);
    gpio_set_level(PIN_LAT, 0);
    gpio_set_level(PIN_CLK, 0);
}

// --------------- BUFFER HELPERS ----------------
void clear_back_buffer(void)
{
    memset((void *)back_buffer, 0, sizeof(framebufferA));
}

void swap_buffers(void)
{
    pixel_t (*tmp)[PHY_WIDTH] = (pixel_t (*)[PHY_WIDTH])front_buffer;
    front_buffer = back_buffer;
    back_buffer = tmp;
}

// ---------------- COORD TRANSLATION ----------------
// Virtual coordinates: x:[0..VIRT_WIDTH-1], y:[0..VIRT_HEIGHT-1]
// They map to physical chain coordinates:
// panel_col = x / PANEL_WIDTH
// panel_row = y / PANEL_HEIGHT
// phys_panel_index = panel_row * N_HOR + panel_col
// phys_x = phys_panel_index * PANEL_WIDTH + (x % PANEL_WIDTH)
// phys_y = y % PANEL_HEIGHT
void set_pixel(int x, int y, int r, int g, int b)
{
    if (x < 0 || x >= VIRT_WIDTH || y < 0 || y >= VIRT_HEIGHT) return;

    int panel_col = x / PANEL_WIDTH;
    int panel_row = y / PANEL_HEIGHT;
    int phys_panel_index = panel_row * N_HOR + panel_col; // index in physical chain

    int local_x = x % PANEL_WIDTH;
    int local_y = y % PANEL_HEIGHT;

    int phys_x = phys_panel_index * PANEL_WIDTH + local_x;
    int phys_y = local_y; // physical y is inside single panel height

    if (phys_x < 0 || phys_x >= PHY_WIDTH || phys_y < 0 || phys_y >= PHY_HEIGHT) return;

    pixel_t pix = (r ? 0x01 : 0) | (g ? 0x02 : 0) | (b ? 0x04 : 0);
    back_buffer[phys_y][phys_x] = pix;
}

// ---------------- REFRESH TASK ----------------
// Generalized for any PANEL_HEIGHT (must be multiple of 4)
void refresh_display_task(void *arg)
{
    const int scan_rows = PANEL_HEIGHT / 4; // 1/4 scan
    const int total_cols = PANEL_WIDTH * PHYS_PANELS * 2; // doubled for interleaving

    while (true) {
        for (int row = 0; row < scan_rows; row++) {
            gpio_set_level(PIN_OE, 1);
            esp_rom_delay_us(5);

            gpio_set_level(PIN_A, row & 0x01);
            gpio_set_level(PIN_B, (row >> 1) & 0x01);
            gpio_set_level(PIN_C, (row >> 2) & 0x01);

            for (int col = 0; col < total_cols; col++) {
                int panel_index = col / PANEL_WIDTH; // 0 .. 2*PHYS_PANELS-1
                int local_x = col % PANEL_WIDTH;
                int panel_in_chain = panel_index / 2; // which physical panel (0..PHYS_PANELS-1)

                int framebuffer_x = panel_in_chain * PANEL_WIDTH + local_x;

                // determine whether this half is upper or lower half
                int y1 = (panel_index % 2) ? row : row + scan_rows; // row or row+scan_rows
                int y2 = y1 + 2 * scan_rows; // second mapped row (16 for 32px panels)

                uint8_t pix1 = 0;
                uint8_t pix2 = 0;
                if (y1 >= 0 && y1 < PHY_HEIGHT && framebuffer_x >= 0 && framebuffer_x < PHY_WIDTH)
                    pix1 = front_buffer[y1][framebuffer_x];
                if (y2 >= 0 && y2 < PHY_HEIGHT && framebuffer_x >= 0 && framebuffer_x < PHY_WIDTH)
                    pix2 = front_buffer[y2][framebuffer_x];

                gpio_set_level(PIN_R1, (pix1 >> 0) & 1);
                gpio_set_level(PIN_G1, (pix1 >> 1) & 1);
                gpio_set_level(PIN_B1, (pix1 >> 2) & 1);

                gpio_set_level(PIN_R2, (pix2 >> 0) & 1);
                gpio_set_level(PIN_G2, (pix2 >> 1) & 1);
                gpio_set_level(PIN_B2, (pix2 >> 2) & 1);

                // clock the shift register
                gpio_set_level(PIN_CLK, 1);
                gpio_set_level(PIN_CLK, 0);
            }

            // latch and enable row
            gpio_set_level(PIN_LAT, 1);
            gpio_set_level(PIN_LAT, 0);
            gpio_set_level(PIN_OE, 0);

            // allow visible time for this scan row
            esp_rom_delay_us(120);
        }
    }
}

// ---------------- example usage in app_main ----------------
void app_main(void)
{
    init_pins();

    // Clear both buffers initially
    memset((void *)framebufferA, 0, sizeof(framebufferA));
    memset((void *)framebufferB, 0, sizeof(framebufferB));

    // Start refresh task (pin-driving) on core 0
    xTaskCreate(refresh_display_task, "refresh_task", 4096, NULL, 1, NULL);



    

    // Example animation loop on main core
    int x = 0;
	int y = 0;
    while (true) 
	{
        clear_back_buffer();
	
	    // --- Example: draw in virtual coordinates ---
	    // (with N_HOR=2, N_VER=2)
	    // Virtual grid: VIRT_WIDTH = 128, VIRT_HEIGHT = 64
	    // Example mapping expectation:
	    // set_pixel(64,32) maps to phys_x = 3*64 + 0 = 192, phys_y = 0
	    //set_pixel(64, 32, 1, 0, 0);   // should appear at physical (192,0) on the chain	
	    set_pixel(5, 5, 0, 1, 0);    // somewhere in panel 0
		set_pixel(5, 37, 0, 1, 1);    // somewhere in panel 0
	    set_pixel(20, 10, 1, 0, 0);   // somewhere in panel 1
		set_pixel(20, 42, 1, 0, 1);   // somewhere in panel 1



        // moving dot across the virtual grid
        set_pixel(x, 0, 1, 0, 1);
        x++;
		if(x > 191)
			x=0;



        // moving dot across the virtual grid
        set_pixel(0, y, 1, 1, 0);    
        y++;
		if(y > 63)
			y=0;


		swap_buffers();


        vTaskDelay(pdMS_TO_TICKS(80));
    }
}
