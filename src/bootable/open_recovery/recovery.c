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

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <linux/input.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/reboot.h>
#include <sys/types.h>
#include <signal.h>

#include "bootloader.h"
#include "common.h"
#include "cutils/properties.h"
#include "firmware.h"
#include "install.h"
#include "minui/minui.h"
#include "minzip/DirUtil.h"
#include "roots.h"
#include "recovery_ui.h"

/*
 * The recovery tool communicates with the main system through /cache files.
 *   /cache/recovery/command - INPUT - command line for tool, one arg per line
 *   /cache/recovery/log - OUTPUT - combined log file from recovery run(s)
 *   /cache/recovery/intent - OUTPUT - intent that was passed in
 *
 * The arguments which may be supplied in the recovery.command file:
 *   --send_intent=anystring - write the text out to recovery.intent
 *   --update_package=root:path - verify install an OTA package file
 *   --wipe_data - erase user data (and cache), then reboot
 *   --wipe_cache - wipe cache (but not user data), then reboot
 *
 * After completing, we remove /cache/recovery/command and reboot.
 * Arguments may also be supplied in the bootloader control block (BCB).
 * These important scenarios must be safely restartable at any point:
 *
 * FACTORY RESET
 * 1. user selects "factory reset"
 * 2. main system writes "--wipe_data" to /cache/recovery/command
 * 3. main system reboots into recovery
 * 4. get_args() writes BCB with "boot-recovery" and "--wipe_data"
 *    -- after this, rebooting will restart the erase --
 * 5. erase_root() reformats /data
 * 6. erase_root() reformats /cache
 * 7. finish_recovery() erases BCB
 *    -- after this, rebooting will restart the main system --
 * 8. main() calls reboot() to boot main system
 *
 * OTA INSTALL
 * 1. main system downloads OTA package to /cache/some-filename.zip
 * 2. main system writes "--update_package=CACHE:some-filename.zip"
 * 3. main system reboots into recovery
 * 4. get_args() writes BCB with "boot-recovery" and "--update_package=..."
 *    -- after this, rebooting will attempt to reinstall the update --
 * 5. install_package() attempts to install the update
 *    NOTE: the package install must itself be restartable from any point
 * 6. finish_recovery() erases BCB
 *    -- after this, rebooting will (try to) restart the main system --
 * 7. ** if install failed **
 *    7a. prompt_and_wait() shows an error icon and waits for the user
 *    7b; the user reboots (pulling the battery, etc) into the main system
 * 8. main() calls maybe_install_firmware_update()
 *    ** if the update contained radio/hboot firmware **:
 *    8a. m_i_f_u() writes BCB with "boot-recovery" and "--wipe_cache"
 *        -- after this, rebooting will reformat cache & restart main system --
 *    8b. m_i_f_u() writes firmware image into raw cache partition
 *    8c. m_i_f_u() writes BCB with "update-radio/hboot" and "--wipe_cache"
 *        -- after this, rebooting will attempt to reinstall firmware --
 *    8d. bootloader tries to flash firmware
 *    8e. bootloader writes BCB with "boot-recovery" (keeping "--wipe_cache")
 *        -- after this, rebooting will reformat cache & restart main system --
 *    8f. erase_root() reformats /cache
 *    8g. finish_recovery() erases BCB
 *        -- after this, rebooting will (try to) restart the main system --
 * 9. main() calls reboot() to boot main system
 */
  
//menu items
#define MAX_MENU_ITEMS 50
#define MAX_MENU_HEADERS 5
#define MAX_MENU_LEVEL 50 

#define ITEM_ERROR            -1
#define ITEM_REBOOT            0
#define ITEM_APPLY_SDCARD      1
#define ITEM_WIPE_DATA         2
#define ITEM_WIPE_CACHE        3
#define ITEM_TAG							 4
#define ITEM_SHELL_SCRIPT      5
#define ITEM_NEW_MENU          6
#define ITEM_NEW_MENU_SCRIPTED 7
#define ITEM_CONSOLE					 8
#define ITEM_MENU_BREAK				 9
#define ITEM_MENU_LABEL				 10

#define ITEM_NAME_REBOOT            "reboot"
#define ITEM_NAME_APPLY_SDCARD      "update"
#define ITEM_NAME_WIPE_DATA         "wipe_data"  
#define ITEM_NAME_WIPE_CACHE        "wipe_cache"
#define ITEM_NAME_TAG      					"tag"
#define ITEM_NAME_SHELL_SCRIPT      "shell"
#define ITEM_NAME_NEW_MENU          "menu"
#define ITEM_NAME_NEW_MENU_SCRIPTED "scripted_menu"
#define ITEM_NAME_CONSOLE 					"console"
#define ITEM_NAME_MENU_BREAK				"break"
#define ITEM_NAME_MENU_LABEL				"label"

//common
#define CUSTOM_SHELL_SCRIPT_PATH		"/bin"
#define CUSTOM_MENU_PATH						"/menu"
#define TAGS_PATH										"/tags"
#define MAIN_MENU_FILE 							"init.menu"

#if OPEN_RECOVERY_HAVE_CONSOLE
#include "console.h"
#endif 

static const struct option OPTIONS[] = {
  { "send_intent", required_argument, NULL, 's' },
  { "update_package", required_argument, NULL, 'u' },
  { "wipe_data", no_argument, NULL, 'w' },
  { "wipe_cache", no_argument, NULL, 'c' },
  { NULL, 0, NULL, 0 },
};

static const char *COMMAND_FILE = "CACHE:recovery/command";
static const char *INTENT_FILE = "CACHE:recovery/intent";
static const char *LOG_FILE = "CACHE:recovery/open_recovery_log";
static const char *FULL_PACKAGE_FILE = "SDCARD:OpenRecovery.zip";
static const char *TEMPORARY_LOG_FILE = "/tmp/open_recovery.log";

//yeah, CamelCase fail
static char* BASE_MENU_TITLE[] = {	"Motorola "OPEN_RECOVERY_PHONE" Open Recovery",
																		"Version "OPEN_RECOVERY_VERSION,
																		"Created by Skrilax_CZ",
																		"",
																		OPEN_RECOVERY_NAVIG,
#ifdef OPEN_RECOVERY_NAVIG2
																		OPEN_RECOVERY_NAVIG2,
#endif
																		"",
																		NULL };

//allow mod header, mod version
static char** MENU_TITLE;

static char* MENU_HEADERS[MAX_MENU_ITEMS] = { NULL };
static char* MENU_ITEMS[MAX_MENU_ITEMS] = { NULL };
static unsigned char MENU_ITEMS_TAG[MAX_MENU_ITEMS] = { 0xFF };
static char* MENU_ITEMS_ACTION[MAX_MENU_ITEMS] = { NULL };
static char* MENU_ITEMS_TARGET[MAX_MENU_ITEMS] = { NULL };
static unsigned char MENU_ITEMS_SELECTABLE[MAX_MENU_ITEMS] = { 0xFF };

// open a file given in root:path format, mounting partitions as necessary
static FILE*
fopen_root_path(const char *root_path, const char *mode) 
{
	if (ensure_root_path_mounted(root_path) != 0) 
	{
	  LOGE("Can't mount %s\n", root_path);
	  return NULL;
	}

	char path[PATH_MAX] = "";
	if (translate_root_path(root_path, path, sizeof(path)) == NULL) 
	{
	  LOGE("Bad path %s\n", root_path);
	  return NULL;
	}

	// When writing, try to create the containing directory, if necessary.
	// Use generous permissions, the system (init.rc) will reset them.
	if (strchr("wa", mode[0])) 
		dirCreateHierarchy(path, 0777, NULL, 1);

	FILE *fp = fopen(path, mode);
	return fp;
}

// close a file, log an error if the error indicator is set
static void
check_and_fclose(FILE *fp, const char *name) 
{
  fflush(fp);
  if (ferror(fp))
  	LOGE("Error in %s\n(%s)\n", name, strerror(errno));
  fclose(fp);
}

#if OPEN_RCVR_VERSION_LITE
//used only by lite version

// command line args come from, in decreasing precedence:
//   - the actual command line
//   - the bootloader control block (one per line, after "recovery")
//   - the contents of COMMAND_FILE (one per line)
static void
get_args(int *argc, char ***argv) 
{
  struct bootloader_message boot;
  memset(&boot, 0, sizeof(boot));
  get_bootloader_message(&boot);  // this may fail, leaving a zeroed structure

  if (boot.command[0] != 0 && boot.command[0] != 255) 
    LOGI("Boot command: %.*s\n", sizeof(boot.command), boot.command);
  

  if (boot.status[0] != 0 && boot.status[0] != 255) 
    LOGI("Boot status: %.*s\n", sizeof(boot.status), boot.status);
  

  // --- if arguments weren't supplied, look in the bootloader control block
  if (*argc <= 1) 
  {
    boot.recovery[sizeof(boot.recovery) - 1] = '\0';  // Ensure termination
    const char *arg = strtok(boot.recovery, "\n");
    if (arg != NULL && !strcmp(arg, "recovery")) 
    {
      *argv = (char **) malloc(sizeof(char *) * MAX_ARGS);
      (*argv)[0] = strdup(arg);
      for (*argc = 1; *argc < MAX_ARGS; ++*argc) 
      {
        if ((arg = strtok(NULL, "\n")) == NULL) 
        	break;
        (*argv)[*argc] = strdup(arg);
      }
      LOGI("Got arguments from boot message\n");
    } 
    else if (boot.recovery[0] != 0 && boot.recovery[0] != 255) 
      LOGE("Bad boot message\n\"%.20s\"\n", boot.recovery);
    
  }

  // --- if that doesn't work, try the command file
  if (*argc <= 1) 
  {
    FILE *fp = fopen_root_path(COMMAND_FILE, "r");
    if (fp != NULL) 
    {
      char *argv0 = (*argv)[0];
      *argv = (char **) malloc(sizeof(char *) * MAX_ARGS);
      (*argv)[0] = argv0;  // use the same program name

      char buf[MAX_LINE_LENGTH];
      for (*argc = 1; *argc < MAX_ARGS; ++*argc) 
      {
        if (!fgets(buf, sizeof(buf), fp)) 
        	break;
        (*argv)[*argc] = strdup(strtok(buf, "\r\n"));  // Strip newline.
      }

      check_and_fclose(fp, COMMAND_FILE);
      LOGI("Got arguments from %s\n", COMMAND_FILE);
    }
  }

  // --> write the arguments we have back into the bootloader control block
  // always boot into recovery after this (until finish_recovery() is called)
  strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
  strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
  int i;
  for (i = 1; i < *argc; ++i) 
  {
		strlcat(boot.recovery, (*argv)[i], sizeof(boot.recovery));
		strlcat(boot.recovery, "\n", sizeof(boot.recovery));
  }
  set_bootloader_message(&boot);
}

#endif

static void
set_sdcard_update_bootloader_message()
{
  struct bootloader_message boot;
  memset(&boot, 0, sizeof(boot));
  strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
  strlcpy(boot.recovery, "recovery\n", sizeof(boot.recovery));
  set_bootloader_message(&boot);
}

// clear the recovery command and prepare to boot a (hopefully working) system,
// copy our log file to cache as well (for the system to read), and
// record any intent we were asked to communicate back to the system.
// this function is idempotent: call it as many times as you like.
static void
finish_recovery(const char *send_intent)
{
    // By this point, we're ready to return to the main system...
    if (send_intent != NULL) {
        FILE *fp = fopen_root_path(INTENT_FILE, "w");
        if (fp == NULL) {
            LOGE("Can't open %s\n", INTENT_FILE);
        } else {
            fputs(send_intent, fp);
            check_and_fclose(fp, INTENT_FILE);
        }
    }

    // Copy logs to cache so the system can find out what happened.
    FILE *log = fopen_root_path(LOG_FILE, "a");
    if (log == NULL) {
        LOGE("Can't open %s\n", LOG_FILE);
    } else {
        FILE *tmplog = fopen(TEMPORARY_LOG_FILE, "r");
        if (tmplog == NULL) {
            LOGE("Can't open %s\n", TEMPORARY_LOG_FILE);
        } else {
            static long tmplog_offset = 0;
            fseek(tmplog, tmplog_offset, SEEK_SET);  // Since last write
            char buf[4096];
            while (fgets(buf, sizeof(buf), tmplog)) fputs(buf, log);
            tmplog_offset = ftell(tmplog);
            check_and_fclose(tmplog, TEMPORARY_LOG_FILE);
        }
        check_and_fclose(log, LOG_FILE);
    }

    // Reset the bootloader message to revert to a normal main system boot.
    // This is done in full recovery as well; as the lite (or vulnerable) versions
    // are interrupted prior reaching here
    struct bootloader_message boot;
    memset(&boot, 0, sizeof(boot));
    set_bootloader_message(&boot);

#if OPEN_RCVR_VERSION_LITE
    // Remove the command file, so recovery won't repeat indefinitely.
    char path[PATH_MAX] = "";
    if (ensure_root_path_mounted(COMMAND_FILE) != 0 ||
        translate_root_path(COMMAND_FILE, path, sizeof(path)) == NULL ||
        (unlink(path) && errno != ENOENT)) {
        LOGW("Can't unlink %s\n", COMMAND_FILE);
    }
#endif
    sync();  // For good measure.
}

static int
erase_root(const char *root)
{
  ui_set_background(BACKGROUND_ICON_INSTALLING);
  ui_show_indeterminate_progress();
  ui_print("Formatting %s...\n", root);
  return format_root_device(root);
}

static char**
prepend_title(char** headers, int* title_length) 
{
  // count the number of lines in our title, plus the
  // caller-provided headers.
  int count = 0;
  char** p;
  
  for (p = MENU_TITLE; *p; ++p, ++count);
  
  //set the title length
  (*title_length) = count;
  
  for (p = headers; *p; ++p, ++count);

  char** new_headers = malloc((count+1) * sizeof(char*));
  char** h = new_headers;
  for (p = MENU_TITLE; *p; ++p, ++h) *h = *p;
  for (p = headers; *p; ++p, ++h) *h = *p;
  *h = NULL;

  return new_headers;
}

static int
get_menu_selection(char** headers, char** items, int title_length, int start_sel, int menu_only, int ignore_selectability) 
{
  // throw away keys pressed previously, so user doesn't
  // accidentally trigger menu items.
  ui_clear_key_queue();

	//get no items
	int num_items = 0;
	while (items[num_items] != NULL)
		num_items++;

	if (start_sel >= num_items)
		start_sel = num_items - 1;
		
	if (start_sel < 0)
		start_sel = 0;

  ui_start_menu(headers, items, title_length, start_sel);
  int selected = start_sel;
  int chosen_item = -1;

  while (chosen_item < 0) 
  {
    int key = ui_wait_key();
    int visible = ui_text_visible();

    int action = device_handle_key(key, visible);
    if (action < 0) 
    {
      switch (action) 
      {
        case HIGHLIGHT_UP:
          while(1)
          {
          	--selected;
          
          	//wrap            
            if (selected < 0)
            	selected = num_items - 1;
            	
            if (ignore_selectability || MENU_ITEMS_SELECTABLE[selected])
            	break;
           }
          
          selected = ui_menu_select(selected);
          break;
          
        case HIGHLIGHT_DOWN:
      		while(1)
          {
            ++selected;
            
            //wrap
            if (selected >= num_items)
            	selected = 0;
            	
         		if (ignore_selectability || MENU_ITEMS_SELECTABLE[selected])
            	break;
          }
          
          selected = ui_menu_select(selected);
          break;
          
        case SELECT_ITEM:
          chosen_item = selected;
          break;
          
        case NO_ACTION:
          break;
      }
    } 
    else if (!menu_only) 
      chosen_item = action;
    
  }

	//check first if to hide menu (don't do on menu changes or tags),
	//because the flicker is annoying
  //that is implemented elsewhere    
  //ui_end_menu();
  return chosen_item;
}

static void
hide_menu_selection()
{
	ui_end_menu();
}

static void
wipe_data(int confirm) 
{
  if (confirm) 
  {
    static char** title_headers = NULL;
    static int title_length = 0;

    if (title_headers == NULL) 
    {
      char* headers[] = { "Confirm wipe of all user data?",
                          "  THIS CAN NOT BE UNDONE.",
                          "",
                          NULL };
      title_headers = prepend_title(headers, &title_length);
    }

    char* items[] = { " No",
                      " No",
                      " No",
                      " No",
                      " No",
                      " No",
                      " No",
                      " Yes -- delete all user data",   // [7]
                      " No",
                      " No",
                      " No",
                      NULL };

    int chosen_item = get_menu_selection(title_headers, items, title_length, 0, 1, 1);
    hide_menu_selection();
    if (chosen_item != 7) {
        return;
    }
  }

  ui_print("\n-- Wiping data...\n");
  device_wipe_data();
  erase_root("DATA:");
  erase_root("CACHE:");
  ensure_common_roots_mounted();
  ui_print("Data wipe complete.\n");
}

static void 
delete_menu() {
	int idx;
	
	for(idx=0; idx<MAX_MENU_ITEMS; idx++)
	{
		if(MENU_ITEMS[idx])
		{
			free(MENU_ITEMS[idx]);
      MENU_ITEMS[idx] = NULL;
    }
    
    if(MENU_ITEMS_ACTION[idx])
		{  
			free(MENU_ITEMS_ACTION[idx]);
			MENU_ITEMS_ACTION[idx] = NULL;
		}
		
		if(MENU_ITEMS_TARGET[idx])
		{	
			free(MENU_ITEMS_TARGET[idx]);
			MENU_ITEMS_TARGET[idx] = NULL;		
		}
		
		MENU_ITEMS_TAG[idx] = 0xFF;
		MENU_ITEMS_SELECTABLE[idx] = 0xFF;
	}
	
	for(idx=0; idx<MAX_MENU_HEADERS; idx++)
	{	
		if(MENU_HEADERS[idx])
		{
			free(MENU_HEADERS[idx]);
			MENU_HEADERS[idx] = NULL;
		}
	}
}

static int
create_menu(const char *fname, const char *shellcmd) 
{
	FILE *fp;
	long fidx;
	unsigned int idx = 0;
	unsigned long tlen;
	char buf[MAX_LINE_LENGTH];
	char *pbuf;
	unsigned int readTitle = 1;
	unsigned int headerLine = 0;
 
	//call the script if available
	if (shellcmd != NULL)
	{
		char menu_file_var[256];	
		char* extra_env_vars[] = 
		{ 
			menu_file_var, 
			NULL 
		};
		
		fprintf(stderr, "Running menu script %s.\n", shellcmd);		
		sprintf(menu_file_var, "MENU_FILE="CUSTOM_MENU_PATH"/%s", fname);
		run_shell_script(shellcmd, 0, extra_env_vars);
	}	

	sprintf(buf, CUSTOM_MENU_PATH"/%s", fname);
	fp = fopen(buf, "r");

	if(fp == NULL)
	{
		fprintf(stderr, "Unable to open %s.\n", buf);
		LOGE("Failed to open the menu %s.\n", fname);
		return -1;
	}
	else
		fprintf(stderr, "Opening %s.\n", buf);
		
	delete_menu();	

	while((idx<MAX_MENU_ITEMS)&&(fgets(buf, sizeof(buf), fp)!=NULL)) 
	{
		if((*buf == '\0') || (*buf == '#')) 
			continue;
	
		if (readTitle)
		{
			fidx = strlen(buf) - 1;
			
			while(fidx >= 0 && (buf[fidx] == ' ' || buf[fidx] == '\r' || buf[fidx] == '\n'))
			{
		 		buf[fidx] = '\0';
		 		fidx--;
			}
		
			//right now, just one line is used
			MENU_HEADERS[0] = malloc(strlen(buf) + 1);
			MENU_HEADERS[1] = malloc(1);
			MENU_HEADERS[2] = NULL;
			strcpy(MENU_HEADERS[0], buf);
			strcpy(MENU_HEADERS[1], "");
			fprintf(stderr, "Menu Title: %s.\n", MENU_HEADERS[0]);
			readTitle = 0;
			continue;
		}

		if(strstr(buf, ":") == NULL) 
			continue;

		pbuf = strtok(buf, ":");
		if(pbuf == NULL) 
			continue;
		
		MENU_ITEMS[idx] = malloc(strlen(pbuf) + 1);
		strcpy(MENU_ITEMS[idx], pbuf);

		pbuf = strtok(NULL, ":");
		if(pbuf == NULL) 
		{
			free(MENU_ITEMS[idx]);
			MENU_ITEMS[idx] = NULL;
			
			fprintf(stderr, "Invalid line: %s", buf);
			continue;
		}
		
		MENU_ITEMS_ACTION[idx] = malloc(strlen(pbuf) + 1);
		strcpy(MENU_ITEMS_ACTION[idx], pbuf);

		pbuf = strtok(NULL, "\n");
		if(pbuf == NULL) 
		{
			free(MENU_ITEMS[idx]);
			MENU_ITEMS[idx] = NULL;
			
			free(MENU_ITEMS_ACTION[idx]);
			MENU_ITEMS_ACTION[idx] = NULL;
			
			fprintf(stderr, "Invalid line: %s", buf);
			continue;
		}

		if(strlen(pbuf) > 0) 
		{
			fidx = strlen(pbuf) - 1;

			while(fidx >= 0 && (pbuf[fidx] == ' ' || pbuf[fidx] == '\r'))
			{
		 		pbuf[fidx] = '\0';
		 		fidx--;
			}

			MENU_ITEMS_SELECTABLE[idx] = 0x01; 

			if(!strcmp(MENU_ITEMS_ACTION[idx], ITEM_NAME_SHELL_SCRIPT))
			{
				if (pbuf[0] == '/' || (pbuf[0] == '\"' && pbuf[1] == '/'))
				{			
					MENU_ITEMS_TARGET[idx] = malloc(strlen(pbuf)+1);
					strcpy(MENU_ITEMS_TARGET[idx], pbuf);
				}
				else
				{
					MENU_ITEMS_TARGET[idx] = malloc(strlen(CUSTOM_SHELL_SCRIPT_PATH) + strlen(pbuf) + 2);
					
					if (pbuf[0] == '\"')
						sprintf(MENU_ITEMS_TARGET[idx], "\"%s/%s", CUSTOM_SHELL_SCRIPT_PATH, pbuf+1);	
					else
						sprintf(MENU_ITEMS_TARGET[idx], "%s/%s", CUSTOM_SHELL_SCRIPT_PATH, pbuf);					
				}
			}
			else if(!strcmp(MENU_ITEMS_ACTION[idx], ITEM_NAME_TAG))
			{
				MENU_ITEMS_TARGET[idx] = malloc(strlen(TAGS_PATH) + strlen(pbuf) + 3);
				sprintf(MENU_ITEMS_TARGET[idx], "%s/.%s", TAGS_PATH, pbuf);
			
				//Add the "[ ] " before the name, but first check if the status
				struct stat buf;
 				int fstat = stat(MENU_ITEMS_TARGET[idx], &buf);
 				MENU_ITEMS_TAG[idx] = fstat ? 0x00 : 0x01;
 				char tag = fstat ? ' ' : 'X';	   									
				char* oldItem = MENU_ITEMS[idx];
				MENU_ITEMS[idx] = malloc(strlen(oldItem) + 5);
				sprintf(MENU_ITEMS[idx], "[%c] %s", tag, oldItem);
				free(oldItem);
			}
			else if(!strcmp(MENU_ITEMS_ACTION[idx], ITEM_NAME_MENU_BREAK))
			{
				MENU_ITEMS_SELECTABLE[idx] = 0x00;
				int len = strlen(MENU_ITEMS[idx]);
				char* oldItem = MENU_ITEMS[idx];
				int i = 0;
				int num_cols = ui_get_num_columns();
				MENU_ITEMS[idx] = malloc(num_cols + 1);
				
				if (len == 1 && oldItem[0] == '*')
				{
					//fill it all
					while(i < num_cols)
					{
						MENU_ITEMS[idx][i] = '-';
						i++;
					}
				}
				else
				{					
					if (len > num_cols - 4)
						len = num_cols - 4;
				
					//start with '-'	
					while(i < (num_cols - len - 2) / 2)
					{
						MENU_ITEMS[idx][i] = '-';
						i++;
					}
				
					//now space
					MENU_ITEMS[idx][i] = ' ';
					i++;
				
					//now the string, max 44 characters
					strncpy(&(MENU_ITEMS[idx][i]), oldItem, len);
					i += len;
				
					//now space
					MENU_ITEMS[idx][i] = ' ';
					i++;
				
					//end with '-'
					while(i < num_cols)
					{
						MENU_ITEMS[idx][i] = '-';
						i++;
					}
				
					//null terminator
					MENU_ITEMS[idx][num_cols] = '\0';
					free(oldItem);
				
					MENU_ITEMS_TARGET[idx] = malloc(2);
				 	strcpy(MENU_ITEMS_TARGET[idx], "*");
				} 
			}
			else 
			{
				MENU_ITEMS_TARGET[idx] = malloc(strlen(pbuf) + 1);
			 	strcpy(MENU_ITEMS_TARGET[idx], pbuf);	
			 	
			 	if (!strcmp(MENU_ITEMS_ACTION[idx], ITEM_NAME_MENU_LABEL))
			 		MENU_ITEMS_SELECTABLE[idx] = 0x00;
			}
		}

		fprintf(stderr, "ITEM[%d]: %s - %s - %s\n", idx, MENU_ITEMS[idx], MENU_ITEMS_ACTION[idx], MENU_ITEMS_TARGET[idx]);
		idx++;
	} 

	fclose(fp);
	return 0;
}

int show_interactive_menu(char** headers, char** items)
{
	int title_length;
	char** title;
	
	title = prepend_title(headers, &title_length);
	int chosen_item = get_menu_selection(title, items, title_length, 0, 1, 1);
  hide_menu_selection();
  free(title);
  return chosen_item + 1;
}

static int
select_action(int which)
{
	if(!strcmp(MENU_ITEMS_ACTION[which], ITEM_NAME_REBOOT))
		return ITEM_REBOOT;
	else if(!strcmp(MENU_ITEMS_ACTION[which], ITEM_NAME_APPLY_SDCARD))
		return ITEM_APPLY_SDCARD;
	else if(!strcmp(MENU_ITEMS_ACTION[which], ITEM_NAME_WIPE_DATA))
		return ITEM_WIPE_DATA;
	else if(!strcmp(MENU_ITEMS_ACTION[which], ITEM_NAME_WIPE_CACHE))
		return ITEM_WIPE_CACHE;
	else if(!strcmp(MENU_ITEMS_ACTION[which], ITEM_NAME_TAG))
		return ITEM_TAG;
	else if(!strcmp(MENU_ITEMS_ACTION[which], ITEM_NAME_SHELL_SCRIPT))
		return ITEM_SHELL_SCRIPT;
	else if(!strcmp(MENU_ITEMS_ACTION[which], ITEM_NAME_NEW_MENU))
		return ITEM_NEW_MENU;
	else if(!strcmp(MENU_ITEMS_ACTION[which], ITEM_NAME_NEW_MENU_SCRIPTED))
		return ITEM_NEW_MENU_SCRIPTED;
	else if(!strcmp(MENU_ITEMS_ACTION[which], ITEM_NAME_CONSOLE))
		return ITEM_CONSOLE;
	else
		return ITEM_ERROR;
}

static void
prompt_and_wait()
{
	char* menu_files[MAX_MENU_LEVEL] = { NULL };
	char* menu_scripts[MAX_MENU_LEVEL] = { NULL };
	int menu_sel[MAX_MENU_LEVEL] = { 0 };
	unsigned int menu_level = 0;  
	int menu_result;
  
  //always ensure if mountings are good
  ensure_common_roots_mounted();
  
	//initialize the recovery -> either call the script for initalization, or switch to full version
#if !OPEN_RCVR_VERSION_LITE
	run_shell_script("/bin/init_recovery.sh "OPEN_RECOVERY_PHONE_SWITCH, 1, NULL);
#endif 

  menu_files[0] = malloc(strlen(MAIN_MENU_FILE)+1);
  strcpy(menu_files[0], MAIN_MENU_FILE);
  ui_led_blink(1);
  create_menu(menu_files[0], menu_scripts[0]);
  ui_led_toggle(0);
  int title_length;  
	char** headers;
	headers = prepend_title(MENU_HEADERS, &title_length);
	int override_initial_selection = -1;	
	
	finish_recovery(NULL);
		
  for (;;) {  

		int menu_item;		
		
		if (override_initial_selection != -1)
		{
			menu_item = get_menu_selection(headers, MENU_ITEMS, title_length, override_initial_selection, 0, 0);
			override_initial_selection = -1;
		}
		else
    	menu_item = get_menu_selection(headers, MENU_ITEMS, title_length, 0, 0, 0);

    // Parse open recovery commands
    int chosen_item = select_action(menu_item);
    chosen_item = device_perform_action(chosen_item);

		//if tag, menu, or scripted menu -> don't hide menu
		//do it here explicitly, it would be a mess in the switch
		if (chosen_item != ITEM_TAG && chosen_item != ITEM_NEW_MENU 
		    && chosen_item != ITEM_NEW_MENU_SCRIPTED)
			hide_menu_selection();

		fprintf(stderr, "Menu: %d, %d, %s\n", menu_item, chosen_item, MENU_ITEMS_TARGET[menu_item]);

    switch (chosen_item) {
    	case ITEM_REBOOT:
       	return;

      case ITEM_WIPE_DATA:
				ui_led_blink(1);
        wipe_data(ui_text_visible());
				ui_led_toggle(0);
        if (!ui_text_visible()) 
        	return;
        ui_set_background(BACKGROUND_ICON_ERROR);
        break;

      case ITEM_WIPE_CACHE:
        ui_print("\n-- Wiping cache...\n");
				ui_led_blink(1);
        erase_root("CACHE:");
        ensure_common_roots_mounted();
				ui_led_toggle(0);
        ui_print("Cache wipe complete.\n");
        if (!ui_text_visible()) 
        	return;
        ui_set_background(BACKGROUND_ICON_ERROR);
        break;

      case ITEM_APPLY_SDCARD:
      
      	//HACK for "error: a label can only be part of a statement and a declaration is not a statement"
      	//the rule is retarted, the follwing "dummy" statement is cut in optimization
      	override_initial_selection=override_initial_selection; 
      
      	//confirm it	
      	char* confirm_headers[] = { "Confirm installing update package?",
      															 MENU_ITEMS[menu_item],
      															 "",
      															 NULL
      														};
      	char* confirm_items[] = { "Yes", "No", NULL };
      	int confirm_item = show_interactive_menu(confirm_headers, confirm_items);
      	
      	if (confirm_item == 1) //YES!
      	{      	
		      ui_print("\n-- Install from sdcard...\n");
		      ui_led_blink(1);
		      ensure_common_roots_mounted();
		      set_sdcard_update_bootloader_message();
		      int status = install_package(MENU_ITEMS_TARGET[menu_item]);
		      if (status != INSTALL_SUCCESS) 
		      {
						ui_set_background(BACKGROUND_ICON_ERROR);
						ui_print("Installation aborted.\n");
		      } 
		      else if (!ui_text_visible()) 
	          return;  // reboot if logs aren't visible
		      else 
		      {
		        if (firmware_update_pending()) 
		        {
	        		ui_set_background(BACKGROUND_ICON_ERROR);
	            ui_print("\nReboot via menu to complete\n"
	                     "installation.\n");
		        } 
		        else 
		        {
	        		ui_set_background(BACKGROUND_ICON_ERROR);
	            ui_print("\nInstall from sdcard complete.\n");
		        }
		      }
		      
		      create_menu(menu_files[menu_level], menu_scripts[menu_level]);
		      free(headers);
		      headers = prepend_title(MENU_HEADERS, &title_length);
		      ui_led_toggle(0);
		    }
        
        override_initial_selection = menu_item;											
 				break; 

      case ITEM_SHELL_SCRIPT:
        ui_print("\n-- Shell script...\n");
        ui_print("%s\n", MENU_ITEMS_TARGET[menu_item]);
				ui_led_blink(1);
				ensure_common_roots_mounted();
				
				run_shell_script(MENU_ITEMS_TARGET[menu_item], 1, NULL);
				create_menu(menu_files[menu_level], menu_scripts[menu_level]);
				free(headers);
				headers = prepend_title(MENU_HEADERS, &title_length);
				
				ui_led_toggle(0);
        ui_print("Done.\n");
				
				override_initial_selection = menu_item;											
 				break; 
				
			case ITEM_TAG:
			
				if (MENU_ITEMS_TAG[menu_item] == 0x00)
				{
					int tag_fd = creat(MENU_ITEMS_TARGET[menu_item], 0644);
					if (tag_fd < 0)
						LOGE("Failed to set the tag.\n");
					else
					{
						MENU_ITEMS_TAG[menu_item] = 0x01;
						MENU_ITEMS[menu_item][1] = 'X';
						close(tag_fd);
					}					 
				}
				else if (MENU_ITEMS_TAG[menu_item] == 0x01) //just a little check if 0xFF aint there if bug is present
				{
					remove(MENU_ITEMS_TARGET[menu_item]);
					MENU_ITEMS_TAG[menu_item] = 0x00;
					MENU_ITEMS[menu_item][1] = ' '; 
				}
			
				override_initial_selection = menu_item;											
 				break; 

      case ITEM_NEW_MENU:
				if (menu_level + 1 >= MAX_MENU_LEVEL)
				{
					//too much menus, ignore
					break;
				}
							
				ui_led_blink(1);
				if (!strcmp(MENU_ITEMS_TARGET[menu_item], ".."))
				{
					if (menu_level)
					{
						free(menu_files[menu_level]);
						menu_files[menu_level] = NULL;
						
						if (menu_scripts[menu_level])
						{
							free(menu_scripts[menu_level]);
							menu_scripts[menu_level] = NULL;
						}
						
						menu_sel[menu_level] = 0;
						menu_level--;
						override_initial_selection = menu_sel[menu_level];
					}
				}
				else
				{
					menu_sel[menu_level] = menu_item;
					menu_level++;
					menu_files[menu_level] = malloc(strlen(MENU_ITEMS_TARGET[menu_item]) + 1);
					strcpy(menu_files[menu_level], MENU_ITEMS_TARGET[menu_item]);				
				}
					
				ensure_common_roots_mounted();	
				menu_result = create_menu(menu_files[menu_level], menu_scripts[menu_level]);
				
				//if fail, remove the new menu
				if (menu_result && menu_level)
				{
					free(menu_files[menu_level]);
					menu_files[menu_level] = NULL;
					
					if (menu_scripts[menu_level])
					{
						free(menu_scripts[menu_level]);
						menu_scripts[menu_level] = NULL;
					}
					
					menu_level--;
				}
				
				free(headers);
				headers = prepend_title(MENU_HEADERS, &title_length);
				ui_led_toggle(0);
				break;
			
			case ITEM_NEW_MENU_SCRIPTED:
			
				if (menu_level + 1 >= MAX_MENU_LEVEL)
				{
					//too much menus, ignore
					break;
				}
			
				ui_led_blink(1);
				char cmdBuff[MAX_LINE_LENGTH];
				strcpy(cmdBuff, MENU_ITEMS_TARGET[menu_item]);
				char* ptr;
										
				//menufile:script
				ptr = strtok(cmdBuff, ":");
				if (ptr != NULL)
				{
					menu_sel[menu_level] = menu_item;
					menu_level++;
					menu_files[menu_level] = malloc(strlen(ptr) + 1);
					strcpy(menu_files[menu_level], ptr);						
					
					ptr = strtok(NULL, "");
					if (ptr != NULL)
					{
						if (ptr[0] == '/' || (ptr[0] == '\"' && ptr[1] == '/'))
						{
							menu_scripts[menu_level] = malloc(strlen(ptr) + 1);
							strcpy(menu_scripts[menu_level], ptr);					
						}
						else
						{
							menu_scripts[menu_level] = malloc(strlen(CUSTOM_SHELL_SCRIPT_PATH) + strlen(ptr) + 2);
						
							if (ptr[0] == '\"')
								sprintf(menu_scripts[menu_level], "\"%s/%s", CUSTOM_SHELL_SCRIPT_PATH, ptr+1);
							else
								sprintf(menu_scripts[menu_level], "%s/%s", CUSTOM_SHELL_SCRIPT_PATH, ptr);

						}
					}
					
					ensure_common_roots_mounted();
					menu_result = create_menu(menu_files[menu_level], menu_scripts[menu_level]);
					
					//if fail, remove the new menu
					if (menu_result && menu_level)
					{
						free(menu_files[menu_level]);
						menu_files[menu_level] = NULL;
						
						if (menu_scripts[menu_level])
						{
							free(menu_scripts[menu_level]);
							menu_scripts[menu_level] = NULL;
						}
						
						menu_level--;
					}
					
					free(headers);
					headers = prepend_title(MENU_HEADERS, &title_length);
				}
					
				ui_led_toggle(0);
				break;
			
			case ITEM_CONSOLE:
#if OPEN_RECOVERY_HAVE_CONSOLE
				ensure_common_roots_mounted();
				
				ui_print("Opening console...\n");
				int console_error = run_console(NULL);
				
				if (console_error)
					if (console_error == CONSOLE_FORCE_QUIT)
						ui_print("Console was forcibly closed.\n");
					else if (console_error == CONSOLE_FAILED_START)
						ui_print("Console failed to start.\n");
					else
					{
						ui_print("Closing console...\n"); //don't bother printing error to UI
						fprintf(stderr, "Console closed with error %d.\n", console_error);
					}		
				else
					ui_print("Closing console...\n");
					
				create_menu(menu_files[menu_level], menu_scripts[menu_level]);
				free(headers);
				headers = prepend_title(MENU_HEADERS, &title_length);
#else
				LOGE("This phone doesn't support console.\n");
#endif
				override_initial_selection = menu_item;											
 				break; 
			
			case ITEM_ERROR:
				LOGE("Unknown command: %s.\n", MENU_ITEMS_ACTION[menu_item]);
				break;
    }
  }
}

static void
print_property(const char *key, const char *name, void *cookie)
{
  fprintf(stderr, "%s=%s\n", key, name);
}

int
main(int argc, char **argv)
{
  if (getppid() != 1)
  {
  	fprintf(stderr, "Parent process must be init.\n"); 
		return EXIT_FAILURE;
  }
  
	time_t start = time(NULL);

	// If these fail, there's not really anywhere to complain...
	freopen(TEMPORARY_LOG_FILE, "a", stdout); setbuf(stdout, NULL);
	freopen(TEMPORARY_LOG_FILE, "a", stderr); setbuf(stderr, NULL);
	fprintf(stderr, "Starting Open Recovery on %s", ctime(&start));

	//menu title
	//lite - just the base
	//full - look for mod
	
#if OPEN_RCVR_VERSION_LITE
	MENU_TITLE = BASE_MENU_TITLE;
#else
	//keep'em malloced even if empty :p
	char* mod_author = malloc(81);
	char* mod_version = malloc(81);
	
	char mod_author_prop[81];
	char mod_version_prop[81];
	
	property_get(MOD_AUTHOR_PROP, mod_author_prop, "");
	property_get(MOD_VERSION_PROP, mod_version_prop, "");
	
	//assume there are some mod properties, leave NULL if there ain't
	MENU_TITLE = malloc((sizeof(BASE_MENU_TITLE) + 3) * sizeof(char*));
	memset(MENU_TITLE, 0, (sizeof(BASE_MENU_TITLE) + 3) * sizeof(char*));
	
	char** b = BASE_MENU_TITLE;
	char** m = MENU_TITLE;
	while (*b)
	{
		*m = *b;
		m++;
		b++;
	}
	
	//append "" if one of the props exist
	int append_empty = 0;	
	
	if (mod_author_prop[0] != '\0')
	{
		//mod version only if mod author
		if (mod_version_prop[0] != '\0')
		{
			snprintf(mod_version, 80, MOD_VERSION_BASE, mod_version_prop);
			*m = mod_version;
			m++;
		}

		snprintf(mod_author, 80, MOD_AUTHOR_BASE, mod_author_prop);
		*m = mod_author;
		m++;
		append_empty = 1;
	}		
			
	if (append_empty)
	{
		*m = malloc(1);
		(*m)[0] = '\0';
		m++;
	}
	
#endif

	ui_init();

	//only lite recovery reacts on the command
#if OPEN_RCVR_VERSION_LITE

	get_args(&argc, &argv);

	int previous_runs = 0;
	const char *send_intent = NULL;
	const char *update_package = NULL;
	int wipe_data = 0, wipe_cache = 0;

  int arg;
  while ((arg = getopt_long(argc, argv, "", OPTIONS, NULL)) != -1) 
  {
    switch (arg) 
    {
      case 'p': previous_runs = atoi(optarg); break;
      case 's': send_intent = optarg; break;
      case 'u': update_package = optarg; break;
      case 'w': wipe_data = wipe_cache = 1; break;
      case 'c': wipe_cache = 1; break;
      case '?':
        LOGE("Invalid command argument\n");
        continue;
    }
  }
#endif

	device_recovery_start();

#if OPEN_RCVR_VERSION_LITE
  fprintf(stderr, "Command:");
  for (arg = 0; arg < argc; arg++) 
    fprintf(stderr, " \"%s\"", argv[arg]);
  
  fprintf(stderr, "\n\n");
#endif

  property_list(print_property, NULL);
  fprintf(stderr, "\n");

	//only lite recovery reacts on the command
#if OPEN_RCVR_VERSION_LITE
  int status = INSTALL_SUCCESS;

  if (update_package != NULL) 
  {
      status = install_package(update_package);
      if (status != INSTALL_SUCCESS) 
      	ui_print("Installation aborted.\n");
  } 
  else if (wipe_data) 
  {
    if (device_wipe_data()) 
    	status = INSTALL_ERROR;
    if (erase_root("DATA:")) 
    	status = INSTALL_ERROR;
    if (wipe_cache && erase_root("CACHE:")) 
    	status = INSTALL_ERROR;
    if (status != INSTALL_SUCCESS)
    	ui_print("Data wipe failed.\n");
  } 
  else if (wipe_cache) 
  {
    if (wipe_cache && erase_root("CACHE:")) 
    	status = INSTALL_ERROR;
    if (status != INSTALL_SUCCESS) 
    	ui_print("Cache wipe failed.\n");
  } 
  else 
    status = INSTALL_ERROR;  // No command specified
 
	int or_up_sts = install_package(FULL_PACKAGE_FILE);
  if (or_up_sts != INSTALL_SUCCESS) 
  {
		ui_print("Failed to switch into the full version.\n");
		ui_print("Running Lite version only.\n");
  } 

  if (status != INSTALL_SUCCESS) 
  	ui_set_background(BACKGROUND_ICON_ERROR);
  if (status != INSTALL_SUCCESS || ui_text_visible())
  	prompt_and_wait();

  // If there is a radio image pending, reboot now to install it.
  maybe_install_firmware_update(send_intent);

  // Otherwise, get ready to boot the main system...
  finish_recovery(send_intent);
#else
	ui_set_background(BACKGROUND_ICON_ERROR);
	prompt_and_wait();
#endif
  ui_print("Rebooting...\n");
  sync();
  ensure_common_roots_unmounted();
  reboot(RB_AUTOBOOT);
  return EXIT_SUCCESS;
}



