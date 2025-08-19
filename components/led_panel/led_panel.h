#include <stdint.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


// ------------ CONFIG: panel + layout ------------
#define PANEL_WIDTH    64
#define PANEL_HEIGHT   32    // must be divisible by 4 (1/4 scan)
#define N_HOR          3     // logical panels horizontally
#define N_VER          2     // logical panels vertically

// Virtual drawing size (what you use in set_pixel)
#define VIRT_WIDTH   (PANEL_WIDTH  * N_HOR)
#define VIRT_HEIGHT  (PANEL_HEIGHT * N_VER)

// Physical chain: all panels wired as one long horizontal row
#define PHYS_PANELS  (N_HOR * N_VER)
#define PHY_WIDTH    (PANEL_WIDTH  * PHYS_PANELS)
#define PHY_HEIGHT   (PANEL_HEIGHT)

// ------------ GPIO PINS (adjust as needed) ------------
#define PIN_R1  GPIO_NUM_2
#define PIN_G1  GPIO_NUM_4
#define PIN_B1  GPIO_NUM_5
#define PIN_R2  GPIO_NUM_18
#define PIN_G2  GPIO_NUM_19
#define PIN_B2  GPIO_NUM_25
#define PIN_CLK GPIO_NUM_13
#define PIN_LAT GPIO_NUM_12
#define PIN_OE  GPIO_NUM_14
#define PIN_A   GPIO_NUM_15
#define PIN_B   GPIO_NUM_26
#define PIN_C   GPIO_NUM_23

//-------------------------------------------//-------------------------------------------


// Choose a high-speed channel/timer so duty updates are as fast as possible
#define OE_SPEED_MODE    LEDC_HIGH_SPEED_MODE
#define OE_TIMER_NUM     LEDC_TIMER_0
#define OE_CHANNEL       LEDC_CHANNEL_0
#define OE_DUTY_RES      LEDC_TIMER_8_BIT    // 256 steps
#define OE_FREQUENCY_HZ  1000000             // 1 MHz PWM freq

// ------------ Pixel + double buffers -------------
// Packed RGB: bit0=R, bit1=G, bit2=B
typedef uint8_t pix_t;

// Two physical-layout framebuffers: [y][x] = 32 x (64*PHYS_PANELS)
//static pix_t fbA[PHY_HEIGHT][PHY_WIDTH];
//static pix_t fbB[PHY_HEIGHT][PHY_WIDTH];

//static volatile pix_t (*front_buf)[PHY_WIDTH] = fbA; // scanned by refresh task
//static volatile pix_t (*back_buf)[PHY_WIDTH]  = fbB; // drawn by your code

// ===== Brightness / PWM config =====
#define COLOR_DEPTH   3              // 3 bits per color (0..7). You can try 4 later.
#define BASE_US       20             // base OE time per LSB slice (tune 15–30us)


// Plane buffers: same physical size, but one per PWM plane
// Each cell packs R,G,B bits like your old buffer: bit0=R, bit1=G, bit2=B
static uint8_t (*front_planes[COLOR_DEPTH])[PHY_WIDTH];
static uint8_t (*back_planes [COLOR_DEPTH])[PHY_WIDTH];

// Allocate concrete storage (A/B for double buffering)
static uint8_t fbA[COLOR_DEPTH][PHY_HEIGHT][PHY_WIDTH];
static uint8_t fbB[COLOR_DEPTH][PHY_HEIGHT][PHY_WIDTH];

typedef struct {
    const char *text;   // text to scroll (can include '\n' for multiple lines)
    int  pos_x;         // current horizontal offset in pixels
    int  speed;         // pixels per frame
    int  lines;         // number of lines
	int color;
	int done;
} scroll_text_t;

void init_pins(void);
void init_oe_pwm(void);
void set_global_brightness(uint8_t level);
void refresh_task(void *arg);
void clear_back_buffer(void);
void swap_buffers(void);
void draw_text(int x, int y, const char *s, int r, int g, int b);
//void scroll_text_update(scroll_text_t *scroll);

void scroll_text(const char *text, int y, int r, int g, int b, int speed_ms);


void draw_char(int x, int y, char c, int r, int g, int b);


void init_planes(void);

void draw_bitmap_rgb(int x0, int y0, const uint32_t *bmp, int w, int h);





