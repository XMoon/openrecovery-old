/*
 * Copyright (C) 2007 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <linux/input.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/reboot.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <ctype.h>
#include <time.h>

#include "common.h"
#include "roots.h"
#include "minui/minui.h"
#include "recovery_ui.h"

#define MAX_COLS 64
#define MAX_ROWS 32

#define MENU_MAX_ROWS 100
#define MENU_MAX_VISIBLE_ROWS 28

#define CHAR_WIDTH 10
#define CHAR_HEIGHT 18

#define PROGRESSBAR_INDETERMINATE_STATES 6
#define PROGRESSBAR_INDETERMINATE_FPS 15

#define LED_OFF   			0x00
#define LED_ON					0x01
#define LED_BLINK				0x02
#define LED_BLINK_ONCE	0x03

#define LED_FILE_RED		"/sys/class/leds/red/brightness"
#define LED_FILE_GREEN	"/sys/class/leds/green/brightness"
#define LED_FILE_BLUE		"/sys/class/leds/blue/brightness"

#define KEYBOARD_BACKLIGHT_FILE 	"/sys/class/leds/keyboard-backlight/brightness"

//properties for the colors
#define COLOR_LED_PROP_NAME 								"color3.LED"
#define COLOR_BKGROUND_PROP_NAME						"color32.background"
#define COLOR_TITLE_PROP_NAME								"color32.title"
#define COLOR_MENU_PROP_NAME								"color32.menu"
#define COLOR_MENU_SEL_PROP_NAME						"color32.selection"
#define COLOR_SCRIPT_PROP_NAME							"color32.script"

// Console UI
#if OPEN_RECOVERY_HAVE_CONSOLE
#define CONSOLE_CHAR_WIDTH 10
#define CONSOLE_CHAR_HEIGHT 18

#define COLOR_CONSOLE_HEADER_PROP_NAME				"color24.console.header"
#define COLOR_CONSOLE_BACKGROUND_PROP_NAME		"color24.console.background"
#define COLOR_CONSOLE_FRONT_PROP_NAME					"color24.console.front"

// Console colors
#define COLOR_CONSOLE_TERMCLR_PROP_NAME_BASE	"color24.console.termclr"

//max rows per screen
#define CONSOLE_BUFFER_ROWS 26
#define CONSOLE_TOTAL_ROWS (1000 + CONSOLE_BUFFER_ROWS)

//max supported columns per screen
#define CONSOLE_MAX_COLUMNS 100

//characters
#define CONSOLE_BEEP 7
#define CONSOLE_ESC 27

#endif //OPEN_RECOVERY_HAVE_CONSOLE

enum 
{ 
	LEFT_SIDE, 
	CENTER_TILE, 
	RIGHT_SIDE, 
	NUM_SIDES 
};

//the structure for 24bit color
typedef struct
{
	unsigned char r;
	unsigned char g;
	unsigned char b;
} color24;

//the structure for 32bit color
typedef struct
{
	unsigned char r;
	unsigned char g;
	unsigned char b;
	unsigned char a;
} color32;

static pthread_mutex_t gUpdateMutex = PTHREAD_MUTEX_INITIALIZER;
static gr_surface gBackgroundIcon[NUM_BACKGROUND_ICONS];
static gr_surface gProgressBarIndeterminate[PROGRESSBAR_INDETERMINATE_STATES];
static gr_surface gProgressBarEmpty[NUM_SIDES];
static gr_surface gProgressBarFill[NUM_SIDES];

static pthread_cond_t led_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t led_mutex = PTHREAD_MUTEX_INITIALIZER;
static color24 led_color = {.r = 1, .g = 0, .b = 0}; //note: only 0 and 1 values are accepted
static volatile unsigned int led_sts;

static const struct { gr_surface* surface; const char *name; } BITMAPS[] = {
    { &gBackgroundIcon[BACKGROUND_ICON_INSTALLING], "icon_installing" },
    { &gBackgroundIcon[BACKGROUND_ICON_ERROR],      "icon_error" },
    { &gBackgroundIcon[BACKGROUND_ICON_FIRMWARE_INSTALLING],
        "icon_firmware_install" },
    { &gBackgroundIcon[BACKGROUND_ICON_FIRMWARE_ERROR],
        "icon_firmware_error" },
    { &gProgressBarIndeterminate[0],    "indeterminate1" },
    { &gProgressBarIndeterminate[1],    "indeterminate2" },
    { &gProgressBarIndeterminate[2],    "indeterminate3" },
    { &gProgressBarIndeterminate[3],    "indeterminate4" },
    { &gProgressBarIndeterminate[4],    "indeterminate5" },
    { &gProgressBarIndeterminate[5],    "indeterminate6" },
    { &gProgressBarEmpty[LEFT_SIDE],    "progress_bar_empty_left_round" },
    { &gProgressBarEmpty[CENTER_TILE],  "progress_bar_empty" },
    { &gProgressBarEmpty[RIGHT_SIDE],   "progress_bar_empty_right_round" },
    { &gProgressBarFill[LEFT_SIDE],     "progress_bar_left_round" },
    { &gProgressBarFill[CENTER_TILE],   "progress_bar_fill" },
    { &gProgressBarFill[RIGHT_SIDE],    "progress_bar_right_round" },
    { NULL,                             NULL },
};

static gr_surface gCurrentIcon = NULL;

static enum ProgressBarType {
    PROGRESSBAR_TYPE_NONE,
    PROGRESSBAR_TYPE_INDETERMINATE,
    PROGRESSBAR_TYPE_NORMAL,
} gProgressBarType = PROGRESSBAR_TYPE_NONE;

// Progress bar scope of current operation
static float gProgressScopeStart = 0, gProgressScopeSize = 0, gProgress = 0;
static time_t gProgressScopeTime, gProgressScopeDuration;

// Set to 1 when both graphics pages are the same (except for the progress bar)
static int gPagesIdentical = 0;

//colors
static color32 background_color = {.r = 0, .g = 0, .b = 0, .a = 160 };
static color32 title_color = {.r = 255, .g = 55, .b = 5, .a = 255};
static color32 menu_color = {.r = 255, .g = 55, .b = 5, .a = 255};
static color32 menu_sel_color = {.r = 255, .g = 255, .b = 255, .a = 255};
static color32 script_color = {.r = 255, .g = 255, .b = 0, .a = 255};

//backwards compatibility for the < 1.40 (title color = menu color by default)
static int title_color_set = 0;

// Log text overlay, displayed when a magic key is pressed
static char text[MAX_ROWS][MAX_COLS];
static int text_cols = 0, text_rows = 0;
static int text_col = 0, text_row = 0, text_top = 0;

#if OPEN_RCVR_VERSION_LITE
static int show_text = 0;
#else
static int show_text = 1;
#endif

static char menu[MENU_MAX_ROWS][MAX_COLS];
static int show_menu = 0;
static int menu_top = 0, menu_items = 0, menu_item_top = 0, menu_items_screen = 0, menu_sel = 0, menu_title_length = 0;

//console variables
#if OPEN_RECOVERY_HAVE_CONSOLE
static int console_screen_rows = 0;
static int console_screen_columns = 0;

//synced via gUpdateMutex
static volatile int console_cursor_sts = 1;
static volatile clock_t console_cursor_last_update_time = 0;

//console system colors
static color24 console_header_color = {.r = 255, .g = 255, .b = 0};
static color24 console_background_color =  {.r = 0, .g = 0, .b = 0};
static color24 console_front_color =  {.r = 229, .g = 229, .b = 229};

static color24 console_term_colors[] = 
{
	{ .r=0,   .g=0,   .b=0   }, //CLR30
	{ .r=205, .g=0,   .b=0   }, //CLR31
	{ .r=0,   .g=205, .b=0   }, //CLR32
	{ .r=205, .g=205, .b=0   }, //CLR33
	{ .r=0,   .g=0,   .b=238 }, //CLR34
	{ .r=205, .g=0,   .b=205 }, //CLR35
	{ .r=0,   .g=205, .b=205 }, //CLR36
	{ .r=229, .g=229, .b=229 }, //CLR37
	
	{ .r=127, .g=127, .b=127 }, //CLR90
	{ .r=255, .g=0,   .b=0   }, //CLR91
	{ .r=0,   .g=255, .b=0   }, //CLR92
	{ .r=255, .g=255, .b=0   }, //CLR93
	{ .r=92,  .g=91,  .b=255 }, //CLR94
	{ .r=255, .g=0,   .b=255 }, //CLR95
	{ .r=0,   .g=255, .b=255 }, //CLR96
	{ .r=255, .g=255, .b=255 }, //CLR97
};

//Rows increased for printing history
static char console_text[CONSOLE_TOTAL_ROWS][CONSOLE_MAX_COLUMNS];
static color24 console_text_color[CONSOLE_TOTAL_ROWS][CONSOLE_MAX_COLUMNS];
static color24 console_current_color;

static int console_top_row = 0;
static int console_force_top_row_on_text = 0;
static int console_force_top_row_reserve = 0;
static int console_cur_row = 0;
static int console_cur_column = 0;

static volatile int show_console = 0;
static volatile int console_refresh = 1;
static int console_escaped_state = 0;
static char console_escaped_buffer[64];
static char* console_escaped_sequence;

#endif //OPEN_RECOVERY_HAVE_CONSOLE

// Key event input queue
static pthread_mutex_t key_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t key_queue_cond = PTHREAD_COND_INITIALIZER;
static int key_queue[256], key_queue_len = 0;
static volatile char key_pressed[KEY_MAX + 1];

/* reads a file with properties, making sure it is terminated with \n \0 */
static void *read_file(const char *fn, unsigned *_sz)
{
    char *data;
    int sz;
    int fd;

    data = 0;
    fd = open(fn, O_RDONLY);
    if(fd < 0) return 0;

    sz = lseek(fd, 0, SEEK_END);
    if(sz < 0) goto oops;

    if(lseek(fd, 0, SEEK_SET) != 0) goto oops;

    data = (char*) malloc(sz + 2); 
    if(data == 0) goto oops;

    if(read(fd, data, sz) != sz) goto oops;
    close(fd);
    data[sz] = '\n';
    data[sz+1] = 0;
    if(_sz) *_sz = sz;
    return data;

oops:
    close(fd);
    if(data != 0) free(data);
    return 0;
}

static int read_byte_hex(const char* text)
{
	unsigned int value = 0;
	unsigned char c;
	
	if (text[0] > '9')
	{
		c = text[0] - 'A' + 0x0A;
		if (c > 0x0F)
			return -1;
	}
	else		
	{	
		c = text[0] - '0';
		if (c > 0x09)
			return -1;
	}

	value = c * 0x10;

	if (text[1] > '9')
	{
		c = text[1] - 'A' + 0x0A;
		if (c > 0x0F)
			return -1;
	}
	else		
	{	
		c = text[1] - '0';
		if (c > 0x09)
			return -1;
	}

	return value + c;
}

static int htoc(const char* text, unsigned char* clr_struct, unsigned int clr_bytes)
{
	int c;
	unsigned int i;
	
	for (i = 0; i < clr_bytes; i++)
	{
		c = read_byte_hex(text);
		if (c == -1)
			return 1;
		
		(*clr_struct) = c;
		clr_struct++;
		text+=2;
	}
	
	return 0;
}

static int htoc24(const char* text, color24* color)
{	
	if (strlen(text) < 7)
			return 1;
	
	if (text[0] != '#')
		return 1;
			
	text++;
			
	return htoc(text, (unsigned char*)color, 3);
}

static int htoc32(const char* text, color32* color)
{	
	if (strlen(text) < 9)
			return 1;
	
	if (text[0] != '#')
		return 1;
			
	text++;
			
	return htoc(text, (unsigned char*)color, 4);
}

static void evaluate_property(const char* data, const char* value)
{
	if (!strcmp(data, COLOR_LED_PROP_NAME))
	{
		if (strlen(value) < 4)
			return;
	
		//evaluate the 3bit LED color
		if (value[0] != '#')
			return;
		
		led_color.r = value[1] == '1' ? 1 : 0;
		led_color.g = value[2] == '1' ? 1 : 0;
		led_color.b = value[3] == '1' ? 1 : 0;
	}
	else if (!strcmp(data, COLOR_BKGROUND_PROP_NAME))
	{
		color32 c;
		if (!htoc32(value, &c))
		{
			background_color = c;
		
			fprintf(stderr, "evaluate_property: Background color set to:\n"
											"r = %d, g = %d, b = %d, a = %d\n", c.r, c.g, c.b, c.a);
		}	
	}
	else if (!strcmp(data, COLOR_TITLE_PROP_NAME))
	{
		color32 c;
		if (!htoc32(value, &c))
		{
			title_color = c;
			title_color_set = 1;
			
			fprintf(stderr, "evaluate_property: Title color set to:\n"
											"r = %d, g = %d, b = %d, a = %d\n", c.r, c.g, c.b, c.a);
		}
	}
	else if (!strcmp(data, COLOR_MENU_PROP_NAME))
	{
		color32 c;
		if (!htoc32(value, &c))
		{
			menu_color = c;
		
			if (!title_color_set)
				title_color = c;
			
			fprintf(stderr, "evaluate_property: Menu color set to:\n"
											"r = %d, g = %d, b = %d, a = %d\n", c.r, c.g, c.b, c.a);
		}
	}
	else if (!strcmp(data, COLOR_MENU_SEL_PROP_NAME))
	{
		color32 c;
		if (!htoc32(value, &c))
		{
			menu_sel_color = c;
			
			fprintf(stderr, "evaluate_property: Selection color set to:\n"
											"r = %d, g = %d, b = %d, a = %d\n", c.r, c.g, c.b, c.a);
		}
	}
	else if (!strcmp(data, COLOR_SCRIPT_PROP_NAME))
	{
		color32 c;
		if (!htoc32(value, &c))
		{
			script_color = c;
			
			fprintf(stderr, "evaluate_property: Script color set to:\n"
											"r = %d, g = %d, b = %d, a = %d\n", c.r, c.g, c.b, c.a);
		}
	}
#if OPEN_RECOVERY_HAVE_CONSOLE
	else if (!strcmp(data, COLOR_CONSOLE_HEADER_PROP_NAME))
	{
		color24 c;
		if (!htoc24(value, &c))
		{
			console_header_color = c;
			
			fprintf(stderr, "evaluate_property: Console header color set to:\n"
											"r = %d, g = %d, b = %d\n", c.r, c.g, c.b);
		}
	}
	else if (!strcmp(data, COLOR_CONSOLE_BACKGROUND_PROP_NAME))
	{
		color24 c;
		if (!htoc24(value, &c))
		{
			console_background_color = c;
			
			fprintf(stderr, "evaluate_property: Console background color set to:\n"
											"r = %d, g = %d, b = %d\n", c.r, c.g, c.b);
		}
	}
	else if (!strcmp(data, COLOR_CONSOLE_FRONT_PROP_NAME))
	{
		color24 c;
		if (!htoc24(value, &c))
		{
			console_front_color = c;
			
			fprintf(stderr, "evaluate_property: Console front color set to:\n"
											"r = %d, g = %d, b = %d\n", c.r, c.g, c.b);
		}
	}
	else if (!strncmp(data, COLOR_CONSOLE_TERMCLR_PROP_NAME_BASE, strlen(COLOR_CONSOLE_TERMCLR_PROP_NAME_BASE)))
	{
		color24 c;
		if (!htoc24(value, &c))
		{
			char* termclr_ascii = data + strlen(COLOR_CONSOLE_TERMCLR_PROP_NAME_BASE);
			int val = atoi(termclr_ascii);
			
			if (val >= 30 && val <= 37)
			{
				console_term_colors[val - 30] = c;
				fprintf(stderr, "evaluate_property: Console terminal front color no. %d set to:\n"
											"r = %d, g = %d, b = %d\n", val, c.r, c.g, c.b);
			}
			else if (val >= 90 && val <= 97)
			{
				console_term_colors[val - 90 + 8] = c;
				fprintf(stderr, "evaluate_property: Console terminal bright front color no. %d set to:\n"
											"r = %d, g = %d, b = %d\n", val, c.r, c.g, c.b);
			}
		}
		
	}
#endif //OPEN_RECOVERY_HAVE_CONSOLE
}

//copied from init
static void load_properties(char *data)
{
    char *key, *value, *eol, *sol, *tmp;

    sol = data;
    while((eol = strchr(sol, '\n'))) {
        key = sol;
        *eol++ = 0;
        sol = eol;

        value = strchr(key, '=');
        if(value == 0) continue;
        *value++ = 0;

        while(isspace(*key)) key++;
        if(*key == '#') continue;
        tmp = value - 2;
        while((tmp > key) && isspace(*tmp)) *tmp-- = 0;

        while(isspace(*value)) value++;
        tmp = eol - 2;
        while((tmp > value) && isspace(*tmp)) *tmp-- = 0;

        evaluate_property(key, value);
    }
}

// Clear the screen and draw the currently selected background icon (if any).
// Should only be called with gUpdateMutex locked.
static void draw_background_locked(gr_surface icon)
{
    gPagesIdentical = 0;
    gr_color(background_color.r, background_color.g, background_color.b, 255);
    gr_fill(0, 0, gr_fb_width(), gr_fb_height());

    if (icon) {
        int iconWidth = gr_get_width(icon);
        int iconHeight = gr_get_height(icon);
        int iconX = (gr_fb_width() - iconWidth) / 2;
        int iconY = (gr_fb_height() - iconHeight) / 2;
        gr_blit(icon, 0, 0, iconWidth, iconHeight, iconX, iconY);
    }
}

// Draw the progress bar (if any) on the screen.  Does not flip pages.
// Should only be called with gUpdateMutex locked.
static void draw_progress_locked()
{
    if (gProgressBarType == PROGRESSBAR_TYPE_NONE) return;

    int iconHeight = gr_get_height(gBackgroundIcon[BACKGROUND_ICON_INSTALLING]);
    int width = gr_get_width(gProgressBarIndeterminate[0]);
    int height = gr_get_height(gProgressBarIndeterminate[0]);

    int dx = (gr_fb_width() - width)/2;
    int dy = (3*gr_fb_height() + iconHeight - 2*height)/4;

    // Erase behind the progress bar (in case this was a progress-only update)
    gr_color(0, 0, 0, 255);
    gr_fill(dx, dy, width, height);

    if (gProgressBarType == PROGRESSBAR_TYPE_NORMAL) {
        float progress = gProgressScopeStart + gProgress * gProgressScopeSize;
        int pos = (int) (progress * width);

        gr_surface s = (pos ? gProgressBarFill : gProgressBarEmpty)[LEFT_SIDE];
        gr_blit(s, 0, 0, gr_get_width(s), gr_get_height(s), dx, dy);

        int x = gr_get_width(s);
        while (x + (int) gr_get_width(gProgressBarEmpty[RIGHT_SIDE]) < width) {
            s = (pos > x ? gProgressBarFill : gProgressBarEmpty)[CENTER_TILE];
            gr_blit(s, 0, 0, gr_get_width(s), gr_get_height(s), dx + x, dy);
            x += gr_get_width(s);
        }

        s = (pos > x ? gProgressBarFill : gProgressBarEmpty)[RIGHT_SIDE];
        gr_blit(s, 0, 0, gr_get_width(s), gr_get_height(s), dx + x, dy);
    }

    if (gProgressBarType == PROGRESSBAR_TYPE_INDETERMINATE) {
        static int frame = 0;
        gr_blit(gProgressBarIndeterminate[frame], 0, 0, width, height, dx, dy);
        frame = (frame + 1) % PROGRESSBAR_INDETERMINATE_STATES;
    }
}

static void 
draw_text_line(int row, const char* t) {
  if (t[0] != '\0') {
    gr_text(0, (row+1)*CHAR_HEIGHT-1, t);
  }
}

#if OPEN_RECOVERY_HAVE_CONSOLE
static void
draw_console_cursor(int row, int column, char letter)
{
	if (!console_cursor_sts)
		return;

	gr_color(console_front_color.r, console_front_color.g, console_front_color.b, 255);
	gr_fill_l(column * CONSOLE_CHAR_WIDTH, row * CONSOLE_CHAR_HEIGHT, 
					(column+1)*CONSOLE_CHAR_WIDTH, (row+1)*CONSOLE_CHAR_HEIGHT);
	
	if (letter != '\0')
	{
		gr_color(console_background_color.r, console_background_color.g, console_background_color.b, 255);
	
		char text[2];
		text[0] = letter;
		text[1] = '\0';
	
		gr_text_l(column * CONSOLE_CHAR_WIDTH, (row+1)*CONSOLE_CHAR_HEIGHT-1, text);
	}
}

static void 
draw_console_line(int row, const char* t, const color24* c) {
  
  char letter[2];
  letter[1] = '\0';
  
  int i = 0;
  
  while(t[i] != '\0') 
  {
  	letter[0] = t[i];
  	gr_color(c[i].r, c[i].g, c[i].b, 255);
    gr_text_l(i * CONSOLE_CHAR_WIDTH, (row+1)*CONSOLE_CHAR_HEIGHT-1, letter);
		i++;	
  }
}

static void
draw_console_locked()
{
	gr_color(console_background_color.r, console_background_color.g, console_background_color.b, 255);
  gr_fill(0, 0, gr_fb_width(), gr_fb_height());

	int draw_cursor = 0;
	
	int i;	
	for (i = console_top_row; i < console_top_row + console_screen_rows; i++)
	{
		draw_console_line(i - console_top_row, console_text[i], console_text_color[i]);
		
		if (i == console_cur_row)
		{
			draw_cursor = 1;
			break;
		}
	}
	
	if(draw_cursor)
		draw_console_cursor(console_cur_row-console_top_row, console_cur_column, console_text[console_cur_row][console_cur_column]);
}
#endif //OPEN_RECOVERY_HAVE_CONSOLE

// Redraw everything on the screen.  Does not flip pages.
// Should only be called with gUpdateMutex locked.
static void draw_screen_locked(void)
{
#if OPEN_RECOVERY_HAVE_CONSOLE
  if (show_console) 
  {
  	if (console_refresh)    
   		draw_console_locked();
   		
  	return;
  }
#endif //OPEN_RECOVERY_HAVE_CONSOLE    
    
  draw_background_locked(gCurrentIcon);
  draw_progress_locked();

  if (show_text) 
  {
    gr_color(background_color.r, background_color.g, background_color.b, background_color.a);
    gr_fill(0, 0, gr_fb_width(), gr_fb_height());

    int i = 0;
    if (show_menu) 
    {
    	gr_color(menu_color.r, menu_color.g, menu_color.b, menu_color.a);
	    gr_fill(0, (menu_top+menu_sel-menu_item_top) * CHAR_HEIGHT,
	            gr_fb_width(), (menu_top+menu_sel-menu_item_top+1)*CHAR_HEIGHT+1);
    
    	//first title - with title color
    	gr_color(title_color.r, title_color.g, title_color.b, title_color.a);
    	    
      for (; i < menu_title_length; ++i) 
      	draw_text_line(i, menu[i]);
      
      //then draw it till top -  with normal color
      gr_color(menu_color.r, menu_color.g, menu_color.b, menu_color.a);
      
      for (; i < menu_top; ++i) 
      	draw_text_line(i, menu[i]);
      
      if (menu_items != menu_items_screen)
      {
      	for (; i < menu_top + menu_items_screen; ++i)
		    { 	      
		      if (i + menu_item_top == menu_top + menu_sel) 
		      {
		        gr_color(menu_sel_color.r, menu_sel_color.g, menu_sel_color.b, menu_sel_color.a);
		        draw_text_line(i, menu[i + menu_item_top]);
		        gr_color(menu_color.r, menu_color.g, menu_color.b, menu_color.a);
		      } 
		      else 
		        draw_text_line(i, menu[i + menu_item_top]);
		    }
		    gr_fill(0, i*CHAR_HEIGHT+CHAR_HEIGHT/2-1,
		            gr_fb_width(), i*CHAR_HEIGHT+CHAR_HEIGHT/2+1);
		    ++i;
		    
      	//scrollbar
      	int width = CHAR_WIDTH;
      	int height = menu_items_screen*CHAR_HEIGHT+1;
      	
      	gr_color(menu_color.r, menu_color.g, menu_color.b, menu_color.a);
      	gr_fill(gr_fb_width() - width, menu_top*CHAR_HEIGHT,
		            gr_fb_width(), menu_top*CHAR_HEIGHT + height);
		     
		    //highlighting
				float fraction = height / (float)menu_items;
				float highlighted = fraction * menu_items_screen;
				float offset = menu_item_top * fraction;
				
				int offset_i;
				int highlighted_i;
				
				//ensure that if last menu_item is selected, there is no strip in the scrollbar
				if (menu_item_top + menu_items_screen == menu_items)
				{
					highlighted_i = (int)highlighted;
					offset_i = height - highlighted_i;
				}
				else
				{
					offset_i = (int)offset;
					highlighted_i = (int)highlighted;
				}
				
				gr_color(menu_sel_color.r, menu_sel_color.g, menu_sel_color.b, menu_sel_color.a);
				gr_fill(gr_fb_width() - width, menu_top*CHAR_HEIGHT + offset_i,
		            gr_fb_width(), menu_top*CHAR_HEIGHT + offset_i + highlighted_i);
      	
      }
      else
      {
      	gr_color(menu_color.r, menu_color.g, menu_color.b, menu_color.a);
		    gr_fill(0, (menu_top+menu_sel) * CHAR_HEIGHT,
		            gr_fb_width(), (menu_top+menu_sel+1)*CHAR_HEIGHT+1);
      
		    for (; i < menu_top + menu_items; ++i)
		    { 	      
		      if (i == menu_top + menu_sel) 
		      {
		        gr_color(menu_sel_color.r, menu_sel_color.g, menu_sel_color.b, menu_sel_color.a);
		        draw_text_line(i, menu[i]);
		        gr_color(menu_color.r, menu_color.g, menu_color.b, menu_color.a);
		      } 
		      else 
		        draw_text_line(i, menu[i]);
		    }
		    gr_fill(0, i*CHAR_HEIGHT+CHAR_HEIGHT/2-1,
		            gr_fb_width(), i*CHAR_HEIGHT+CHAR_HEIGHT/2+1);
		    ++i;
		  }
    }

    gr_color(script_color.r, script_color.g, script_color.b, script_color.a);

    for (; i < text_rows; ++i) 
      draw_text_line(i, text[(i+text_top) % text_rows]);  
  }
}

// Redraw everything on the screen and flip the screen (make it visible).
// Should only be called with gUpdateMutex locked.
static void update_screen_locked(void)
{
  draw_screen_locked();
  gr_flip();
}

// Updates only the progress bar, if possible, otherwise redraws the screen.
// Should only be called with gUpdateMutex locked.
static void update_progress_locked(void)
{
    if (show_text || !gPagesIdentical) {
        draw_screen_locked();    // Must redraw the whole screen
        gPagesIdentical = 1;
    } else {
        draw_progress_locked();  // Draw only the progress bar
    }
    gr_flip();
}

// Keeps the progress bar updated, even when the process is otherwise busy.
static void *progress_thread(void *cookie)
{
    for (;;) {
        usleep(1000000 / PROGRESSBAR_INDETERMINATE_FPS);
        pthread_mutex_lock(&gUpdateMutex);

        // update the progress bar animation, if active
        // skip this if we have a text overlay (too expensive to update)
        if (gProgressBarType == PROGRESSBAR_TYPE_INDETERMINATE && !show_text) {
            update_progress_locked();
        }

        // move the progress bar forward on timed intervals, if configured
        int duration = gProgressScopeDuration;
        if (gProgressBarType == PROGRESSBAR_TYPE_NORMAL && duration > 0) {
            int elapsed = time(NULL) - gProgressScopeTime;
            float progress = 1.0 * elapsed / duration;
            if (progress > 1.0) progress = 1.0;
            if (progress > gProgress) {
                gProgress = progress;
                update_progress_locked();
            }
        }

        pthread_mutex_unlock(&gUpdateMutex);
    }
    return NULL;
}

void ui_led_toggle(int state)
{
	pthread_mutex_lock(&led_mutex);

  if(state)
   led_sts = LED_ON;
  else
   led_sts = LED_OFF;
  
  pthread_cond_signal(&led_cond); 
  pthread_mutex_unlock(&led_mutex);
}

void ui_led_blink(int continuously)
{
	pthread_mutex_lock(&led_mutex);

  if(continuously)
   led_sts = LED_BLINK;
  else
   led_sts = LED_BLINK_ONCE;
  
  pthread_cond_signal(&led_cond); 
  pthread_mutex_unlock(&led_mutex);
  
}

static void
led_on(FILE* ledfp_r, FILE* ledfp_g, FILE* ledfp_b)
{
	if (led_color.r)
	{
		fwrite("1", 1, 1, ledfp_r);
		fflush(ledfp_r);
	}
	
	if (led_color.g)
	{
		fwrite("1", 1, 1, ledfp_g);
		fflush(ledfp_g);
	}
		
	if (led_color.b)
	{
		fwrite("1", 1, 1, ledfp_b);
		fflush(ledfp_b);
	}
}

static void
led_off(FILE* ledfp_r, FILE* ledfp_g, FILE* ledfp_b)
{
	fwrite("0", 1, 1, ledfp_r);
	fwrite("0", 1, 1, ledfp_g);
	fwrite("0", 1, 1, ledfp_b);
	
	fflush(ledfp_r);
	fflush(ledfp_g);
	fflush(ledfp_b);
}

static void*
led_thread(void *cookie)
{
  unsigned int state = 0;
  unsigned int waitperiod = 0;
  FILE *ledfp_r, *ledfp_g, *ledfp_b;
  
  ledfp_r = fopen(LED_FILE_RED, "w");
	ledfp_g = fopen(LED_FILE_GREEN, "w");
	ledfp_b = fopen(LED_FILE_BLUE, "w");

  while(1) 
  {
  	pthread_mutex_lock(&led_mutex);
  	
  	switch (led_sts)
  	{
  		case LED_OFF:
  			state = 0;
				led_off(ledfp_r, ledfp_g, ledfp_b);
  			
  			while (led_sts == LED_OFF) 
					pthread_cond_wait(&led_cond, &led_mutex);
					
				break;
				
			case LED_ON:
				state = 1;
				led_on(ledfp_r, ledfp_g, ledfp_b);	
  			
  			while (led_sts == LED_ON) 
					pthread_cond_wait(&led_cond, &led_mutex);
					
				break;
				
			case LED_BLINK_ONCE:
				state = 1;
			
				led_on(ledfp_r, ledfp_g, ledfp_b);	
				waitperiod = 500000;
				led_sts = LED_OFF;
				
				break;
				
			case LED_BLINK:
				state = state ? 0 : 1;
				
				if (state)
					led_on(ledfp_r, ledfp_g, ledfp_b);
				else
					led_off(ledfp_r, ledfp_g, ledfp_b);
			
				waitperiod = 500000;
				break;	
  	}
  	
  	pthread_mutex_unlock(&led_mutex);
  	
  	//when blinking, we want to finish it, not interrupt it
		if (waitperiod > 0)
		{
  		usleep(waitperiod);
  		waitperiod = 0;
  	}
  }
  
  fclose(ledfp_r);
  fclose(ledfp_g);
  fclose(ledfp_b);
  return NULL;
}

// Reads input events, handles special hot keys, and adds to the key queue.
static void*
input_thread(void *cookie)
{
    int rel_sum = 0;
    int fake_key = 0;
    for (;;) {
        // wait for the next key event
        struct input_event ev;
        do {
            ev_get(&ev, 0);

	    //fprintf(stderr, "Key: %d, %d, %d\n", ev.type, ev.code, ev.value);

            if (ev.type == EV_SYN) {
                continue;
            } else if (ev.type == EV_REL) {
                if (ev.code == REL_Y) {
                    // accumulate the up or down motion reported by
                    // the trackball.  When it exceeds a threshold
                    // (positive or negative), fake an up/down
                    // key event.
                    rel_sum += ev.value;
                    if (rel_sum > 3) {
                        fake_key = 1;
                        ev.type = EV_KEY;
                        ev.code = KEY_DOWN;
                        ev.value = 1;
                        rel_sum = 0;
                    } else if (rel_sum < -3) {
                        fake_key = 1;
                        ev.type = EV_KEY;
                        ev.code = KEY_UP;
                        ev.value = 1;
                        rel_sum = 0;
                    }
                }
            } else {
                rel_sum = 0;
            }
        } while (ev.type != EV_KEY || ev.code > KEY_MAX);

        pthread_mutex_lock(&key_queue_mutex);
        if (!fake_key) {
            // our "fake" keys only report a key-down event (no
            // key-up), so don't record them in the key_pressed
            // table.
            key_pressed[ev.code] = ev.value;
        }
        fake_key = 0;
        const int queue_max = sizeof(key_queue) / sizeof(key_queue[0]);
        if (ev.value > 0 && key_queue_len < queue_max) {
            key_queue[key_queue_len++] = ev.code;
            pthread_cond_signal(&key_queue_cond);
        }
        pthread_mutex_unlock(&key_queue_mutex);

				//only in lite version
#if OPEN_RCVR_VERSION_LITE
        if (ev.value > 0 && device_toggle_display(key_pressed, ev.code)) {
            pthread_mutex_lock(&gUpdateMutex);
            show_text = !show_text;
            update_screen_locked();
            pthread_mutex_unlock(&gUpdateMutex);
        }
#endif

        if (ev.value > 0 && device_reboot_now(key_pressed, ev.code)) {
            ensure_common_roots_unmounted();
            reboot(RB_AUTOBOOT);
        }
    }
    return NULL;
}

#if OPEN_RECOVERY_HAVE_CONSOLE

static void*
console_cursor_thread(void *cookie)
{
	while (show_console)
	{
		clock_t time_now = clock();
		double since_last_update = ((double)(time_now - console_cursor_last_update_time)) / CLOCKS_PER_SEC;
	
		if (since_last_update >= 0.5)
		{		
			pthread_mutex_lock(&gUpdateMutex);
			console_cursor_sts = console_cursor_sts ? 0 : 1;
			console_cursor_last_update_time = time_now;
			update_screen_locked();
			pthread_mutex_unlock(&gUpdateMutex);
		}
		usleep(20000);		
	}
	
	return NULL;
}

#endif //OPEN_RECOVERY_HAVE_CONSOLE

void ui_init(void)
{
    gr_init();
    ev_init();
    
    unsigned int sz;
    char* prop_data = read_file(PROPERTY_FILE, &sz);

		if(prop_data != 0) 
		{
      load_properties(prop_data);
      free(prop_data);
    }

    text_col = text_row = 0;
    text_rows = gr_fb_height() / CHAR_HEIGHT;
    if (text_rows > MAX_ROWS) text_rows = MAX_ROWS;
    text_top = 1;

    text_cols = gr_fb_width() / CHAR_WIDTH;
    if (text_cols > MAX_COLS - 1) text_cols = MAX_COLS - 1;

    int i;
    for (i = 0; BITMAPS[i].name != NULL; ++i) {
        int result = res_create_surface(BITMAPS[i].name, BITMAPS[i].surface);
        if (result < 0) {
            if (result == -2) {
                LOGI("Bitmap %s missing header\n", BITMAPS[i].name);
            } else {
                LOGE("Missing bitmap %s\n(Code %d)\n", BITMAPS[i].name, result);
            }
            *BITMAPS[i].surface = NULL;
        }
    }

		//enable keyboard backlight (only on console enabled phones)
#if OPEN_RECOVERY_HAVE_CONSOLE
		FILE* keyboardfd = fopen(KEYBOARD_BACKLIGHT_FILE, "w");
		fwrite("1", 1, 1, keyboardfd);
		fclose(keyboardfd);
#endif

    pthread_t t;
    pthread_create(&t, NULL, progress_thread, NULL);
    pthread_create(&t, NULL, input_thread, NULL);
    pthread_create(&t, NULL, led_thread, NULL);
}

char *ui_copy_image(int icon, int *width, int *height, int *bpp) 
{
    pthread_mutex_lock(&gUpdateMutex);
    draw_background_locked(gBackgroundIcon[icon]);
    *width = gr_fb_width();
    *height = gr_fb_height();
    *bpp = sizeof(gr_pixel) * 8;
    int size = *width * *height * sizeof(gr_pixel);
    char *ret = malloc(size);
    if (ret == NULL) {
        LOGE("Can't allocate %d bytes for image\n", size);
    } else {
        memcpy(ret, gr_fb_data(), size);
    }
    pthread_mutex_unlock(&gUpdateMutex);
    return ret;
}

void ui_set_background(int icon)
{
    pthread_mutex_lock(&gUpdateMutex);
    gCurrentIcon = gBackgroundIcon[icon];
    update_screen_locked();
    pthread_mutex_unlock(&gUpdateMutex);
}

void ui_show_indeterminate_progress()
{
    pthread_mutex_lock(&gUpdateMutex);
    if (gProgressBarType != PROGRESSBAR_TYPE_INDETERMINATE) {
        gProgressBarType = PROGRESSBAR_TYPE_INDETERMINATE;
        update_progress_locked();
    }
    pthread_mutex_unlock(&gUpdateMutex);
}

void ui_show_progress(float portion, int seconds)
{
    pthread_mutex_lock(&gUpdateMutex);
    gProgressBarType = PROGRESSBAR_TYPE_NORMAL;
    gProgressScopeStart += gProgressScopeSize;
    gProgressScopeSize = portion;
    gProgressScopeTime = time(NULL);
    gProgressScopeDuration = seconds;
    gProgress = 0;
    update_progress_locked();
    pthread_mutex_unlock(&gUpdateMutex);
}

void ui_set_progress(float fraction)
{
    pthread_mutex_lock(&gUpdateMutex);
    if (fraction < 0.0) fraction = 0.0;
    if (fraction > 1.0) fraction = 1.0;
    if (gProgressBarType == PROGRESSBAR_TYPE_NORMAL && fraction > gProgress) {
        // Skip updates that aren't visibly different.
        int width = gr_get_width(gProgressBarIndeterminate[0]);
        float scale = width * gProgressScopeSize;
        if ((int) (gProgress * scale) != (int) (fraction * scale)) {
            gProgress = fraction;
            update_progress_locked();
        }
    }
    pthread_mutex_unlock(&gUpdateMutex);
}

void ui_reset_progress()
{
    pthread_mutex_lock(&gUpdateMutex);
    gProgressBarType = PROGRESSBAR_TYPE_NONE;
    gProgressScopeStart = gProgressScopeSize = 0;
    gProgressScopeTime = gProgressScopeDuration = 0;
    gProgress = 0;
    update_screen_locked();
    pthread_mutex_unlock(&gUpdateMutex);
}

void ui_print(const char *fmt, ...)
{
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, 2048, fmt, ap);
    va_end(ap);

    fputs(buf, stderr);

    // This can get called before ui_init(), so be careful.
    pthread_mutex_lock(&gUpdateMutex);
    if (text_rows > 0 && text_cols > 0) {
        char *ptr;
        for (ptr = buf; *ptr != '\0'; ++ptr) {
            if (*ptr == '\n' || text_col >= text_cols) {
                text[text_row][text_col] = '\0';
                text_col = 0;
                text_row = (text_row + 1) % text_rows;
                if (text_row == text_top) text_top = (text_top + 1) % text_rows;
            }
            if (*ptr != '\n') text[text_row][text_col++] = *ptr;
        }
        text[text_row][text_col] = '\0';
        update_screen_locked();
    }
    pthread_mutex_unlock(&gUpdateMutex);
}

void ui_print_raw(const char *buf)
{
   fputs(buf, stderr);

    // This can get called before ui_init(), so be careful.
    pthread_mutex_lock(&gUpdateMutex);
    if (text_rows > 0 && text_cols > 0) {
        const char *ptr;
        for (ptr = buf; *ptr != '\0'; ++ptr) {
            if (*ptr == '\n' || text_col >= text_cols) {
                text[text_row][text_col] = '\0';
                text_col = 0;
                text_row = (text_row + 1) % text_rows;
                if (text_row == text_top) text_top = (text_top + 1) % text_rows;
            }
            if (*ptr != '\n') text[text_row][text_col++] = *ptr;
        }
        text[text_row][text_col] = '\0';
        update_screen_locked();
    }
    pthread_mutex_unlock(&gUpdateMutex);
}

void ui_start_menu(char** headers, char** items, int title_length, int start_sel) 
{
  int i;
  pthread_mutex_lock(&gUpdateMutex);
  
  //reset progress
  gProgressBarType = PROGRESSBAR_TYPE_NONE;
  gProgressScopeStart = gProgressScopeSize = 0;
  gProgressScopeTime = gProgressScopeDuration = 0;
  gProgress = 0;
  
  menu_title_length = title_length;
  
  if (text_rows > 0 && text_cols > 0)
  {
    for (i = 0; i < MENU_MAX_ROWS; ++i) 
    {
      if (headers[i] == NULL) break;
      strncpy(menu[i], headers[i], text_cols);
      menu[i][text_cols] = '\0';
    }
    menu_top = i;
    for (; i < MENU_MAX_ROWS; ++i) 
    {
      if (items[i-menu_top] == NULL) break;
      strncpy(menu[i], items[i-menu_top], text_cols);
      menu[i][text_cols] = '\0';
    }
    
    menu_items = i - menu_top;
    menu_items_screen = MENU_MAX_VISIBLE_ROWS - menu_top;
    if (menu_items < menu_items_screen)
    	menu_items_screen = menu_items;
    
    show_menu = 1;
    menu_sel = start_sel;
    if (menu_sel >= menu_items_screen)
    	menu_item_top = menu_sel - menu_items_screen + 1;
    else
    	menu_item_top = 0;
    	
    update_screen_locked();
  }
  pthread_mutex_unlock(&gUpdateMutex);
}

int ui_menu_select(int sel) 
{
  int old_sel;
  pthread_mutex_lock(&gUpdateMutex);
  if (show_menu > 0) 
  {
    old_sel = menu_sel;
    menu_sel = sel;
    
    if (menu_sel < 0)
    	menu_sel = 0;
    
    if (menu_sel >= menu_items) 
    	menu_sel = menu_items-1;
    	
    sel = menu_sel;
    
    if (menu_sel != old_sel) 
    {
    	if (menu_sel < menu_item_top)
    		menu_item_top = menu_sel;
    	else if (menu_sel >= menu_item_top + menu_items_screen)
    		menu_item_top = menu_sel - menu_items_screen + 1;
    
    	update_screen_locked();
    }
  }
  pthread_mutex_unlock(&gUpdateMutex);
  return sel;
}

void ui_end_menu() 
{
  int i;
  pthread_mutex_lock(&gUpdateMutex);
  if (show_menu > 0 && text_rows > 0 && text_cols > 0) 
  {
    show_menu = 0;
    update_screen_locked();
  }
  pthread_mutex_unlock(&gUpdateMutex);
}

int ui_text_visible()
{
  pthread_mutex_lock(&gUpdateMutex);
  int visible = show_text;
  pthread_mutex_unlock(&gUpdateMutex);
  return visible;
}

int ui_get_key()
{
	int key = -1;
	pthread_mutex_lock(&key_queue_mutex);
	
	if (key_queue_len != 0)
	{
		key = key_queue[0];
		memcpy(&key_queue[0], &key_queue[1], sizeof(int) * --key_queue_len);
	}
	
	pthread_mutex_unlock(&key_queue_mutex); 
	return key;
}

int ui_wait_key()
{
	pthread_mutex_lock(&key_queue_mutex);
	while (key_queue_len == 0) 
		pthread_cond_wait(&key_queue_cond, &key_queue_mutex);

	int key = key_queue[0];
	memcpy(&key_queue[0], &key_queue[1], sizeof(int) * --key_queue_len);
	pthread_mutex_unlock(&key_queue_mutex);
	return key;
}

int ui_key_pressed(int key)
{
	// This is a volatile static array, don't bother locking
	return key_pressed[key];
}

void ui_clear_key_queue() 
{
	pthread_mutex_lock(&key_queue_mutex);
	key_queue_len = 0;
	pthread_mutex_unlock(&key_queue_mutex);
}

int ui_get_num_columns()
{
	return text_cols;
}

#if OPEN_RECOVERY_HAVE_CONSOLE

void ui_console_begin()
{
	pthread_mutex_lock(&gUpdateMutex);
	show_console = 1;
	console_refresh = 1;
	console_cursor_sts = 1;
	console_cursor_last_update_time = clock();
	console_top_row = 0;
	console_cur_row = 0;
	console_cur_column = 0;
	console_escaped_state = 0;
	
	//calculate the number of columns and rows
	console_screen_rows = ui_console_get_height() / CONSOLE_CHAR_HEIGHT;
	console_screen_columns = ui_console_get_width() / CONSOLE_CHAR_WIDTH + 1; //+1 for null terminator
	
	console_force_top_row_on_text = 0;
	console_force_top_row_reserve = 1 - console_screen_rows;
	
	memset(console_text, ' ', (CONSOLE_TOTAL_ROWS) * (console_screen_columns));
	memset(console_text_color, 0, (CONSOLE_TOTAL_ROWS) * (console_screen_columns) * sizeof(color24));
	
	int i;
	for (i = 0; i < (CONSOLE_TOTAL_ROWS); i++)
		console_text[i][console_screen_columns - 1] = '\0'; 
	
	console_current_color = console_front_color;
	
	pthread_t t;
	pthread_create(&t, NULL, console_cursor_thread, NULL);
	
	update_screen_locked();
	pthread_mutex_unlock(&gUpdateMutex);
}

void ui_console_end()
{
	pthread_mutex_lock(&gUpdateMutex);
	show_console = 0;
	update_screen_locked();
	console_screen_rows = 0;
	console_screen_columns = 0;
	pthread_mutex_unlock(&gUpdateMutex);
}

void ui_console_begin_update()
{
	pthread_mutex_lock(&gUpdateMutex);
	console_refresh = 0;
	pthread_mutex_unlock(&gUpdateMutex);
}

void ui_console_end_update()
{
	pthread_mutex_lock(&gUpdateMutex);
	console_refresh = 1;
	update_screen_locked();
	pthread_mutex_unlock(&gUpdateMutex);
}

int ui_console_get_num_rows()
{
	return console_screen_rows;
}

int ui_console_get_num_columns()
{
	return console_screen_columns - 1;
}

//landscape, swap em
int ui_console_get_width()
{
	return gr_fb_height();
}

int ui_console_get_height()
{
	return gr_fb_width();
}

void ui_console_scroll_up(int num_rows)
{
	pthread_mutex_lock(&gUpdateMutex);
	
	if (console_top_row - num_rows < 0)
		console_top_row = 0;
	else
		console_top_row -= num_rows;
	
	update_screen_locked();
	pthread_mutex_unlock(&gUpdateMutex);	
}

void ui_console_scroll_down(int num_rows)
{
	int max_row_top = console_cur_row - console_screen_rows + 1;
	
	if (max_row_top < console_force_top_row_on_text)
		max_row_top = console_force_top_row_on_text;
	
	if (max_row_top < 0)
		max_row_top = 0;
	
	pthread_mutex_lock(&gUpdateMutex);
	if (console_top_row + num_rows > max_row_top)
		console_top_row = max_row_top;
	else
		console_top_row += num_rows;
		
	update_screen_locked();
	pthread_mutex_unlock(&gUpdateMutex);		
}

void ui_console_get_system_front_color(int which, unsigned char* r, unsigned char* g, unsigned char* b)
{
	color24 c = {.r = 0, .g = 0, .b = 0};
	
	switch(which)
	{
		case CONSOLE_HEADER_COLOR:
			c = console_header_color;
			break;
			
		case CONSOLE_DEFAULT_BACKGROUND_COLOR:
			c = console_background_color;
			break;
			
		case CONSOLE_DEFAULT_FRONT_COLOR:
			c = console_front_color;
			break;
	}
	
	*r = c.r;
	*g = c.g;
	*b = c.b;
}

void ui_console_set_system_front_color(int which)
{
	switch(which)
	{
		case CONSOLE_HEADER_COLOR:
			console_current_color = console_header_color;
			break;
			
		case CONSOLE_DEFAULT_BACKGROUND_COLOR:
			console_current_color = console_background_color;
			break;
			
		case CONSOLE_DEFAULT_FRONT_COLOR:
			console_current_color = console_front_color;
			break;
	}
}

void ui_console_get_front_color(unsigned char* r, unsigned char* g, unsigned char* b)
{
	*r = console_current_color.r;
	*g = console_current_color.g;
	*b = console_current_color.b;
}

void ui_console_set_front_color(unsigned char r, unsigned char g, unsigned char b)
{
	console_current_color.r = r;
	console_current_color.g = g;
	console_current_color.b = b;
}

void console_set_front_term_color(int ascii_code)
{
	if (ascii_code >= 30 && ascii_code < 37)
		ui_console_set_front_color(
			console_term_colors[ascii_code - 30].r,
			console_term_colors[ascii_code - 30].g,
			console_term_colors[ascii_code - 30].b);
	else if (ascii_code >= 90 && ascii_code < 97)
		ui_console_set_front_color(
			console_term_colors[ascii_code - 90 + 8].r,
			console_term_colors[ascii_code - 90 + 8].g,
			console_term_colors[ascii_code - 90 + 8].b);
}

static void
console_put_char(char c)
{
	switch(c)
	{
		case '\n':
			fprintf(stderr, "Row %d, Column %d, Char \"LINE BREAK\"\n", console_cur_row, console_cur_column);
			console_cur_row++;
			console_force_top_row_reserve++;
			break;
		
		case '\r':
			fprintf(stderr, "Row %d, Column %d, Char \"CARRIAGE RETURN\"\n", console_cur_row, console_cur_column);
			console_cur_column = 0;
			break;

		case '\t':
			fprintf(stderr, "Row %d, Column %d, Char \"TAB\"\n", console_cur_row, console_cur_column);
			//tab is per 5
			int end = console_cur_column + (5 - console_cur_column % 5);
		
			if (end >= console_screen_columns - 2)
			{
				int i;
				for (i = console_cur_column; i < console_screen_columns - 1; i++)
					console_text[console_cur_row][i] = ' ';
			
				console_cur_column = 0;
				console_cur_row++;
				console_force_top_row_reserve++;
			}
			else
			{
				int i;
				for (i = console_cur_column; i < end; i++)
					console_text[console_cur_row][i] = ' ';
				
				console_cur_column = end;
			}
			break;
		
		case '\b':
			fprintf(stderr, "Row %d, Column %d, Char \"BACKSPACE\"\n", console_cur_row, console_cur_column);
			if (console_cur_column == 0)
			{
				if (console_cur_row == 0)
					break;
				
				console_cur_column = console_screen_columns - 2;
				console_cur_row--;
			}
			else
				console_cur_column--;		
			break;
	
		case CONSOLE_BEEP: //BELL - use LED for that
			ui_led_blink(0);
			break;
		
		default:
			console_text[console_cur_row][console_cur_column] = c;
			console_text_color[console_cur_row][console_cur_column] = console_current_color;
			fprintf(stderr, "Row %d, Column %d, Char %d\n", console_cur_row, console_cur_column, c);
			console_cur_column++;

			if (console_cur_column > console_screen_columns - 2)
			{
				console_cur_column = 0;
				console_cur_row++;
				console_force_top_row_reserve++;
			}
			break;
	}

	if (console_cur_row == CONSOLE_TOTAL_ROWS)
	{
		int shift = console_cur_row - (CONSOLE_TOTAL_ROWS - CONSOLE_BUFFER_ROWS);
		fprintf(stderr, "Shifting the rows by %d.\n", shift);
		int j;

		for (j = 0; j < CONSOLE_TOTAL_ROWS - CONSOLE_BUFFER_ROWS; j++)
		{
			memcpy(console_text[j], console_text[j + shift], console_screen_columns);
			memcpy(console_text_color[j], console_text_color[j + shift], console_screen_columns * sizeof(color24));
		}

		for (j = CONSOLE_TOTAL_ROWS - CONSOLE_BUFFER_ROWS; j < CONSOLE_TOTAL_ROWS; j++)
		{
			memset(console_text[j], ' ', console_screen_columns);
			console_text[j][console_screen_columns - 1] = '\0';		
			memset(console_text_color[j], 0, console_screen_columns * sizeof(color24)); 
		}

		console_cur_row -= shift;
		console_force_top_row_on_text -= shift;
	}			
}

static void
console_unescape()
{
	int len = strlen(console_escaped_buffer);
	int was_unescaped = 0;
	int noSqrBrackets = 0;
	int noRoundBracketsLeft = 0;
	int noRoundBracketsRight = 0;
	int noQuestionMarks = 0;
	int parameters[32];
	int noParameters = 0;
	
	char argument = '\0';
	char *ptr;
	
	memset(parameters, 0, sizeof(int) * 32);
	
	//first parse it
	for (ptr = console_escaped_buffer; *ptr != '\0'; ++ptr)
	{
		if (*ptr == '[')
			noSqrBrackets++;
		else if (*ptr == '(')
			noRoundBracketsLeft++;
		else if (*ptr == ')')
			noRoundBracketsRight++;
		else if (*ptr == '?')
			noQuestionMarks++;
		else if (*ptr == ';')
			noParameters++;
		else if (*ptr >= '0' && *ptr <= '9')
			parameters[noParameters] = parameters[noParameters] * 10 + (*ptr - '0');
		else
		{
			argument = *ptr;
			break;
		}
	}
	
	//was used for indexing, so increment it
	noParameters++;
	
	fprintf(stderr, "ESCAPE: no. Brackets S%d RL%d RR%d, no Question Marks %d, no. params %d, argument %c,\n", 
		noSqrBrackets, noRoundBracketsLeft, noRoundBracketsRight, noQuestionMarks, noParameters, argument);
	fprintf(stderr, "PARAMS:");
	
	int i, j;
	for (i = 0; i < noParameters; i++)
		fprintf(stderr, " %d", parameters[i]);
		
	fprintf(stderr, "\n");
	
	if (noSqrBrackets == 1 && noRoundBracketsLeft == 0 && noRoundBracketsRight == 0 && noQuestionMarks == 0)
	{
		switch (argument)
		{
			//======================================================================
			// UPPERCASE
			//======================================================================
			
			//move up n lines, but not out of screen
			case 'A':
				console_cur_row -= parameters[0];
				if (console_force_top_row_on_text > console_cur_row)
					console_cur_row = console_force_top_row_on_text;			
				
				//set the top reserve
				console_force_top_row_reserve = 1 - (console_force_top_row_on_text + 
					console_screen_rows - console_cur_row);
				was_unescaped = 1;
				break;
			
			//move down n lines, but not out of screen
			case 'B':
				console_cur_row += parameters[0];
				if (console_cur_row >= console_force_top_row_on_text + console_screen_rows)
					console_cur_row = console_force_top_row_on_text + console_screen_rows - 1;
				
				//set the top reserve
				console_force_top_row_reserve = 1 - (console_force_top_row_on_text + 
					console_screen_rows - console_cur_row);
				was_unescaped = 1;
				break;
				
			//move right, but not out of the line
			case 'C':		
				console_cur_column += parameters[0];
				if ( console_cur_column >= (console_screen_columns - 1) )
					console_cur_column = console_screen_columns - 2;
			
				was_unescaped = 1;
				break;
				
			//move left, but not out of the line
			case 'D':
				console_cur_column -= parameters[0];
				
				if (console_cur_column < 0)
					console_cur_column = 0;
			
				was_unescaped = 1;
				break;
				
			//cursor to top-left + offset
			case 'H':						
				if (parameters[0] >= console_screen_rows)
					parameters[0] = console_screen_rows - 1;
				
				if (parameters[1] >= (console_screen_columns - 1) )
					parameters[1] = console_screen_columns - 2;
				
				console_cur_row = console_top_row + parameters[0];
				console_cur_column = parameters[1];
				console_force_top_row_on_text = console_top_row;
				console_force_top_row_reserve = 1 - (console_top_row + 
					console_screen_rows - console_cur_row);				
				
				was_unescaped = 1;
				break;
			
			//clear below cursor	
			case 'J':			
				for (j = console_cur_column; j < (console_screen_columns - 1); j++)
				{
					console_text[console_cur_row][j] = ' ';
					console_text_color[console_cur_row][j].r = 0;
					console_text_color[console_cur_row][j].g = 0;
					console_text_color[console_cur_row][j].b = 0;
				}
		
				for (j = console_cur_row + 1; j < CONSOLE_TOTAL_ROWS; j++)
				{
					memset(console_text[j], ' ', console_screen_columns);
					console_text[j][console_screen_columns - 1] = '\0';		
					memset(console_text_color[j], 0, console_screen_columns * sizeof(color24));
				}
				was_unescaped = 1;		
				break;
				
			//======================================================================
			// LOWERCASE
			//======================================================================
		
			//only coloring, ignore bolding, italic etc.
			//background color changing is not supported
			case 'm':
								
				//consider 'm' to be succesfully unescaped in all cases
				was_unescaped = 1;		
				
				for (i = 0; i < noParameters; i++)
				{
					switch (parameters[i])
					{
						case 0: //reset
						case 39: //default text color
							ui_console_set_system_front_color(CONSOLE_DEFAULT_FRONT_COLOR);
							break;
						case 30:
						case 31:
						case 32:
						case 33:
						case 34:
						case 35:
						case 36:
						case 37:
						case 90:
						case 91:
						case 92:
						case 93:
						case 94:
						case 95:
						case 96:
						case 97:
							console_set_front_term_color(parameters[i]);
							break;				
					}
				}
		}
	}
	
	if (!was_unescaped) //send it ala text then
	{
		console_put_char('^');
		int e;
		for (e = 0; console_escaped_buffer[e] != '\0'; e++)
			console_put_char(console_escaped_buffer[e]);
	}
}

static void
console_put_escape_sequence(char c)
{
	*console_escaped_sequence = c;
	console_escaped_sequence++;
	
	if (c != '[' && c != '(' && c != '?' && c != ')' && c != ';' && !(c >= '0' && c <= '9'))
	{
		*console_escaped_sequence = '\0';
		fprintf(stderr, "Escape character: %s\n", console_escaped_buffer);
		console_unescape();
		console_escaped_state = 0;	
	}
}

void ui_console_print(const char *text)
{	
	pthread_mutex_lock(&gUpdateMutex);
	const char *ptr;
	for (ptr = text; *ptr != '\0'; ++ptr)
	{
		//parse escape characters here
		if (*ptr == CONSOLE_ESC)
		{
			console_escaped_state = 1;
			console_escaped_buffer[0] = '\0';
			console_escaped_sequence = &(console_escaped_buffer[0]);
			continue;
		}
		else if (!console_escaped_state)
			console_put_char(*ptr);
		else
			console_put_escape_sequence(*ptr);
	}
	
	if (console_force_top_row_reserve > 0)
	{
		console_force_top_row_on_text += console_force_top_row_reserve;
		console_force_top_row_reserve = 0;
	}
	
	console_top_row = console_force_top_row_on_text;
	if (console_top_row < 0)
		console_top_row = 0;	
	
	console_cursor_sts = 1;
	console_cursor_last_update_time = clock();	
	update_screen_locked();
	pthread_mutex_unlock(&gUpdateMutex);	
}


#endif //OPEN_RECOVERY_HAVE_CONSOLE
