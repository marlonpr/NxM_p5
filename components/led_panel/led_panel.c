#include "led_panel.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "font20x40.h"


#include "soc/gpio_struct.h"  // for GPIO register access

// Precalculate bitmasks for speed
#define BIT_R1 (1 << PIN_R1)
#define BIT_G1 (1 << PIN_G1)
#define BIT_B1 (1 << PIN_B1)
#define BIT_R2 (1 << PIN_R2)
#define BIT_G2 (1 << PIN_G2)
#define BIT_B2 (1 << PIN_B2)

#define BIT_A  (1 << PIN_A)
#define BIT_B  (1 << PIN_B)
#define BIT_C  (1 << PIN_C)

#define BIT_CLK (1 << PIN_CLK)
#define BIT_LAT (1 << PIN_LAT)
#define BIT_OE (1 << PIN_OE)


void init_oe_pwm(void)
{
    // Configure a high-speed LEDC timer for OE pin
    ledc_timer_config_t timer_conf = {
        .speed_mode       = LEDC_HIGH_SPEED_MODE,
        .duty_resolution  = LEDC_TIMER_6_BIT,
        .timer_num        = LEDC_TIMER_0,
        .freq_hz          = 1000000,        // 1 MHz
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_conf));

    // Map OE pin into that PWM channel
    ledc_channel_config_t chan_conf = {
        .speed_mode     = LEDC_HIGH_SPEED_MODE,
        .channel        = LEDC_CHANNEL_0,
        .timer_sel      = LEDC_TIMER_0,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = PIN_OE,
        .duty           = 0,                // start OFF
        .hpoint         = 0,
		.flags = {
            .output_invert = 1   // <-- invert the PWM signal
        }
    };
    ESP_ERROR_CHECK(ledc_channel_config(&chan_conf));
}




// Global brightness in percent (0–100)
static volatile uint8_t global_brightness = 255;

// Call this once after your LEDC timer/channel have been initialized
// to set initial duty according to global_brightness.
static void update_oe_duty(void)
{
    const uint32_t max_duty = (1 << OE_DUTY_RES) - 1;         // 255
    uint32_t duty = (max_duty * global_brightness) / 255;     // scale 0–255
    // Apply it immediately
    ESP_ERROR_CHECK( ledc_set_duty(OE_SPEED_MODE, OE_CHANNEL, duty) );
    ESP_ERROR_CHECK( ledc_update_duty(OE_SPEED_MODE, OE_CHANNEL) );
}

// Call this from wherever you want to change brightness (e.g. CLI, button handler)
void set_global_brightness(uint8_t level)
{
    if (level > 255) {
        level = 255;
    }
    global_brightness = level;
    update_oe_duty();
}


//---------------------------------------------//-------------------------------------------









// ------------ Pins init -------------
void init_pins(void) {
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

// ------------ Buffer helpers -------------
void clear_back_buffer(void) {
    // Clear whole physical surface: PHY_HEIGHT x PHY_WIDTH
    memset((void*)back_buf, 0, sizeof(fbA));
}

void swap_buffers(void) {
    // Instant pointer swap; no memcpy
    pix_t (*tmp)[PHY_WIDTH] = (pix_t (*)[PHY_WIDTH])front_buf;
    front_buf = back_buf;
    back_buf  = tmp;
}

// ------------ Virtual->Physical mapping set_pixel -------------
//
// You draw in a virtual grid N_HOR x N_VER:
// panel_col = x / PANEL_WIDTH
// panel_row = y / PANEL_HEIGHT
// phys_panel_index = panel_row * N_HOR + panel_col   (linear horizontal chain)
// phys_x = phys_panel_index * PANEL_WIDTH + (x % PANEL_WIDTH)
// phys_y = y % PANEL_HEIGHT
//
static inline void set_pixel(int x, int y, int r, int g, int b) {
    if ((unsigned)x >= (unsigned)VIRT_WIDTH || (unsigned)y >= (unsigned)VIRT_HEIGHT) return;

    int panel_col       = x / PANEL_WIDTH;
    int panel_row       = y / PANEL_HEIGHT;
    int phys_panel_idx  = panel_row * N_HOR + panel_col;

    int local_x = x % PANEL_WIDTH;
    int local_y = y % PANEL_HEIGHT;

    int phys_x = phys_panel_idx * PANEL_WIDTH + local_x;
    int phys_y = local_y;

    // Bounds check against physical buffer (defensive)
    if ((unsigned)phys_x >= (unsigned)PHY_WIDTH || (unsigned)phys_y >= (unsigned)PHY_HEIGHT) return;

    uint8_t v = (r ? 1 : 0) | (g ? 2 : 0) | (b ? 4 : 0);
    back_buf[phys_y][phys_x] = v;
}

// ------------ HUB75 refresh task (1/4 scan) -------------
//
// Keeps your column-scanning logic pattern: total_cols = PANEL_WIDTH * PHYS_PANELS * 2
// panel_index  = col / PANEL_WIDTH  -> 0..(2*PHYS_PANELS-1)
// panel_in_row = panel_index / 2     (which physical panel in the chain)
// y1 = (panel_index % 2) ? row : row + scan_rows
// y2 = y1 + 2*scan_rows
//
void refresh_task(void *arg) {
    const int scan_rows  = PANEL_HEIGHT / 4;              // e.g., 8 for 32px
    const int total_cols = PANEL_WIDTH * PHYS_PANELS * 2; // two halves (top/bottom)

    while (1) {
        for (int row = 0; row < scan_rows; row++) {

			GPIO.out_w1ts = BIT_OE; // set high
    		
			//gpio_set_level(PIN_OE, 1);
			// Duty = max → PWM is always HIGH → OE stays HIGH → panel off
			//ledc_set_duty(OE_SPEED_MODE, OE_CHANNEL, 0);
			//ledc_update_duty(OE_SPEED_MODE, OE_CHANNEL);
            
			//esp_rom_delay_us(5);

			uint32_t set_mask = 0;
		    uint32_t clr_mask = 0;
		
		    if (row & 0x01) set_mask |= BIT_A; else clr_mask |= BIT_A;
		    if (row & 0x02) set_mask |= BIT_B; else clr_mask |= BIT_B;
		    if (row & 0x04) set_mask |= BIT_C; else clr_mask |= BIT_C;
		
		    GPIO.out_w1ts = set_mask; // set high
		    GPIO.out_w1tc = clr_mask; // set low


            for (int col = 0; col < total_cols; col++) {
                int panel_index    = col / PANEL_WIDTH;      // 0..(2*PHYS_PANELS-1)
                int local_x        = col % PANEL_WIDTH;
                int panel_in_chain = panel_index / 2;        // 0..(PHYS_PANELS-1)
                int fb_x           = panel_in_chain * PANEL_WIDTH + local_x;
				uint32_t set_mask = 0;
    			uint32_t clr_mask = 0;

                // Upper/lower half rows for this scan step
                int y1 = (panel_index % 2) ? row : row + scan_rows;
                int y2 = y1 + 2 * scan_rows;

                uint8_t p1 = 0, p2 = 0;
                if ((unsigned)fb_x < (unsigned)PHY_WIDTH) {
                    if ((unsigned)y1 < (unsigned)PHY_HEIGHT) p1 = front_buf[y1][fb_x];
                    if ((unsigned)y2 < (unsigned)PHY_HEIGHT) p2 = front_buf[y2][fb_x];
                }

				// Row 1
			    if (p1 & 0x01) set_mask |= BIT_R1; else clr_mask |= BIT_R1;
			    if (p1 & 0x02) set_mask |= BIT_G1; else clr_mask |= BIT_G1;
			    if (p1 & 0x04) set_mask |= BIT_B1; else clr_mask |= BIT_B1;
			
			    // Row 2
			    if (p2 & 0x01) set_mask |= BIT_R2; else clr_mask |= BIT_R2;
			    if (p2 & 0x02) set_mask |= BIT_G2; else clr_mask |= BIT_G2;
			    if (p2 & 0x04) set_mask |= BIT_B2; else clr_mask |= BIT_B2;
			
			    // Apply all changes at once
			    GPIO.out_w1ts = set_mask; // set bits high
			    GPIO.out_w1tc = clr_mask; // set bits low

				GPIO.out_w1ts = BIT_CLK; // set high
    			GPIO.out_w1tc = BIT_CLK; // set low

            }
			GPIO.out_w1ts = BIT_LAT; // set high
    		GPIO.out_w1tc = BIT_LAT; // set low

            
			GPIO.out_w1tc = BIT_OE; // set low
			//gpio_set_level(PIN_OE, 0);
			// Duty = 0 → PWM is always LOW → OE stays LOW → panel on
			//ledc_set_duty(OE_SPEED_MODE, OE_CHANNEL, (1 << OE_DUTY_RES) - 1);
			//ledc_update_duty(OE_SPEED_MODE, OE_CHANNEL);
			//update_oe_duty();


            // Visible time per row; tune for brightness/ghosting
            esp_rom_delay_us(50);
        }
    }
}



// set_pixel(x, y, r, g, b) is your existing function
// VIRT_WIDTH, VIRT_HEIGHT are the virtual drawing dimensions

// ----------------- Draw a single character -----------------
void draw_char(int x, int y, char c, int r, int g, int b) {
    if (c < 32 || c > 126) c = '?';  // fallback
    int index = c - 32;

    for (int row = 0; row < FONT_HEIGHT; row++) {
        for (int col = 0; col < FONT_WIDTH; col++) {
            if (font20x40[index][row][col]) {
                int px = x + col;
                int py = y + row;
                if (px >= 0 && px < VIRT_WIDTH && py >= 0 && py < VIRT_HEIGHT) {
                    set_pixel(px, py, r, g, b);
                }
            }
        }
    }
}

// ----------------- Draw a string -----------------
void draw_text(int x, int y, const char *text, int r, int g, int b) {
    int cursor_x = x;
    while (*text) {
        draw_char(cursor_x, y, *text, r, g, b);
        cursor_x += FONT_WIDTH;  // advance cursor by font width
        text++;
    }
}

// ----------------- Scroll text horizontally -----------------
void scroll_text(const char *text, int y, int r, int g, int b, int speed_ms) {
    // compute total text width
    int len = strlen(text);
    int text_width = len * FONT_WIDTH;

    // temporary buffer for one row of virtual pixels (optional)
    for (int scroll_x = 0; scroll_x < text_width + VIRT_WIDTH; scroll_x++) {
        clear_back_buffer();  // clear virtual framebuffer

        int cursor_x = VIRT_WIDTH - scroll_x;  // starting position
        const char *p = text;
        while (*p) {
            draw_char(cursor_x, y, *p, r, g, b);
            cursor_x += FONT_WIDTH;
            p++;
        }

        swap_buffers();  // show frame
        vTaskDelay(pdMS_TO_TICKS(speed_ms));
    }
}
