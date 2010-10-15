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
#include <limits.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include "common.h"
#include "install.h"
#include "mincrypt/rsa.h"
#include "minui/minui.h"
#include "minzip/SysUtil.h"
#include "minzip/Zip.h"
#include "mtdutils/mounts.h"
#include "mtdutils/mtdutils.h"
#include "roots.h"
#include "firmware.h"
#include "imenu/imenu.h"

#define ASSUMED_UPDATE_BINARY_NAME  		"META-INF/com/google/android/update-binary"
#define DEFAULT_UPDATE_BINARY_NAME  		"/sbin/updater"
#define ASSUMED_OR_UPDATE_SCRIPT_NAME 	"META-INF/update-or.sh"
#define OR_UPDATE_EXTRACT_DIR_NAME			"/sdcard/OpenRecovery/tmp/update"
#define OR_SCRIPT_UPDATER_NAME					"/sbin/script-updater"
#define PUBLIC_KEYS_FILE "/res/keys"

static interactive_menu_struct* interactive_menu;
static const char *SHELL_FILE = PHONE_SHELL;

// The update binary ask us to install a firmware file on reboot.  Set
// that up.  Takes ownership of type and filename.
static int
handle_firmware_update(char* type, char* filename, ZipArchive* zip) {
    unsigned int data_size;
    const ZipEntry* entry = NULL;

    if (strncmp(filename, "PACKAGE:", 8) == 0) {
        entry = mzFindZipEntry(zip, filename+8);
        if (entry == NULL) {
            LOGE("Failed to find \"%s\" in package", filename+8);
            return INSTALL_ERROR;
        }
        data_size = entry->uncompLen;
    } else {
        struct stat st_data;
        if (stat(filename, &st_data) < 0) {
            LOGE("Error stat'ing %s: %s\n", filename, strerror(errno));
            return INSTALL_ERROR;
        }
        data_size = st_data.st_size;
    }

    LOGI("type is %s; size is %d; file is %s\n",
         type, data_size, filename);

    char* data = malloc(data_size);
    if (data == NULL) {
        LOGI("Can't allocate %d bytes for firmware data\n", data_size);
        return INSTALL_ERROR;
    }

    if (entry) {
        if (mzReadZipEntry(zip, entry, data, data_size) == false) {
            LOGE("Failed to read \"%s\" from package", filename+8);
            return INSTALL_ERROR;
        }
    } else {
        FILE* f = fopen(filename, "rb");
        if (f == NULL) {
            LOGE("Failed to open %s: %s\n", filename, strerror(errno));
            return INSTALL_ERROR;
        }
        if (fread(data, 1, data_size, f) != data_size) {
            LOGE("Failed to read firmware data: %s\n", strerror(errno));
            return INSTALL_ERROR;
        }
        fclose(f);
    }

    if (remember_firmware_update(type, data, data_size)) {
        LOGE("Can't store %s image\n", type);
        free(data);
        return INSTALL_ERROR;
    }
    free(filename);

    return INSTALL_SUCCESS;
}

static int
mkdir_recursive(const char *path)
{
	char *path_loc, *path_inc, *tok;
  int len;
  struct stat sb;

  len = strlen(path);
  
  if (!len)
  	return 2;
  
  path_loc = malloc(len + 1);
  path_inc = malloc(len + 1);
  path_inc[0] = '\0';
  strcpy(path_loc, path);
  
  tok = strtok(path_loc, "/"); 
  strncat(path_inc, "/", len);
  strncat(path_inc, tok, len);
  
  while(1)
  {    	
 		if(stat(path_inc, &sb) < 0)
			mkdir(path_inc, 0755); // The path element didn't exist
    else if(!S_ISDIR(sb.st_mode))
			return 1; //not a directory
      
    tok = strtok(NULL, "/");

		if (!tok)
  		break;
		
    strncat(path_inc, "/", len);
    strncat(path_inc, tok, len);
  }
  
  free(path_loc);
  free(path_inc);
  return 0;
}

static int
handle_or_update(const char *path, ZipArchive *zip)
{
	//first, extract everything
	ui_print("Extracting archive...\n");
	
	char cmd[512];
	
	//ensure the target directory on the sdcard exists and is clean
	sprintf(cmd, "rm -rf %s", OR_UPDATE_EXTRACT_DIR_NAME); 
  system(cmd);
	
	if (mkdir_recursive(OR_UPDATE_EXTRACT_DIR_NAME))
	{
		LOGE("Failed creating: "OR_UPDATE_EXTRACT_DIR_NAME"\n");
		return INSTALL_ERROR;
	}
	
	bool ok = mzExtractRecursive(zip, "", OR_UPDATE_EXTRACT_DIR_NAME, 0, NULL, NULL, NULL);

	if (!ok) 
	{
    LOGE("Failed extracting the archive.\n");
    
    //clear it
    sprintf(cmd, "rm -rf %s", OR_UPDATE_EXTRACT_DIR_NAME); 
    system(cmd);
    return 1;
	}
		
	
	sprintf(cmd, "%s \"%s/%s\" \"%s\"", OR_SCRIPT_UPDATER_NAME, 
			OR_UPDATE_EXTRACT_DIR_NAME, ASSUMED_OR_UPDATE_SCRIPT_NAME, OR_UPDATE_EXTRACT_DIR_NAME);

	run_shell_script(cmd, 1, NULL);
	
	//clear it
	sprintf(cmd, "rm -rf %s", OR_UPDATE_EXTRACT_DIR_NAME); 
  system(cmd);
	return INSTALL_SUCCESS;
}

// If the package contains an update binary, extract it and run it.
static int
try_update_binary(const char *path, ZipArchive *zip) 
{
	int	custom_binary = 0;

	const ZipEntry* or_script_entry =
		      mzFindZipEntry(zip, ASSUMED_OR_UPDATE_SCRIPT_NAME);
		      
	if (or_script_entry != NULL)
	{
		ui_print("Using shell script...\n");
		return handle_or_update(path, zip);
	}

	const ZipEntry* binary_entry =
		      mzFindZipEntry(zip, ASSUMED_UPDATE_BINARY_NAME);

	if (binary_entry == NULL)
		ui_print("Using default updater...\n");
	else
		custom_binary = 1;
  
  char* binary = "/tmp/update_binary";
  unlink(binary);
  
  if (custom_binary)
  {
		int fd = creat(binary, 0755);
		if (fd < 0) 
		{
		  LOGE("Can't make %s\n", binary);
		  return 1;
		}
  
		bool ok = mzExtractZipEntryToFile(zip, binary_entry, fd);
		close(fd);

		if (!ok) 
		{
	    LOGE("Can't copy %s\n", ASSUMED_UPDATE_BINARY_NAME);
	    return 1;
		}
	}
	else
		binary = DEFAULT_UPDATE_BINARY_NAME;

	ui_show_indeterminate_progress();
  int pipefd[2];
  pipe(pipefd);

  // When executing the update binary contained in the package, the
  // arguments passed are:
  //
  //   - the version number for this interface
  //
  //   - an fd to which the program can write in order to update the
  //     progress bar.  The program can write single-line commands:
  //
  //        progress <frac> <secs>
  //            fill up the next <frac> part of of the progress bar
  //            over <secs> seconds.  If <secs> is zero, use
  //            set_progress commands to manually control the
  //            progress of this segment of the bar
  //
  //        set_progress <frac>
  //            <frac> should be between 0.0 and 1.0; sets the
  //            progress bar within the segment defined by the most
  //            recent progress command.
  //
  //        firmware <"hboot"|"radio"> <filename>
  //            arrange to install the contents of <filename> in the
  //            given partition on reboot.  (API v2: <filename> may
  //            start with "PACKAGE:" to indicate taking a file from
  //            the OTA package.)
  //
  //        ui_print <string>
  //            display <string> on the screen.
  //
  //   - the name of the package zip file.
  //

  char** args = malloc(sizeof(char*) * 5);
  args[0] = binary;
  args[1] = EXPAND(RECOVERY_API_VERSION);   // defined in Android.mk
  args[2] = malloc(10);
  sprintf(args[2], "%d", pipefd[1]);
  args[3] = (char*)path;
  args[4] = NULL;

  pid_t pid = fork();
  if (pid == 0) 
  {
    close(pipefd[0]);
    execv(binary, args);
    fprintf(stderr, "E:Can't run %s (%s)\n", binary, strerror(errno));
    _exit(-1);
  }
  close(pipefd[1]);

  char* firmware_type = NULL;
  char* firmware_filename = NULL;

  char buffer[1024];
  FILE* from_child = fdopen(pipefd[0], "r");
  
  ui_set_progress(0.0);
  
  while (fgets(buffer, sizeof(buffer), from_child) != NULL) 
  {
    char* command = strtok(buffer, " \n");
    if (command == NULL) 
    	continue;
   	else if (strcmp(command, "progress") == 0) 
   	{
      char* fraction_s = strtok(NULL, " \n");
      char* seconds_s = strtok(NULL, " \n");

      float fraction = strtof(fraction_s, NULL);
      int seconds = strtol(seconds_s, NULL, 10);

      ui_show_progress(fraction, seconds);
    } 
    else if (strcmp(command, "set_progress") == 0) 
    {
      char* fraction_s = strtok(NULL, " \n");
      float fraction = strtof(fraction_s, NULL);
      ui_set_progress(fraction);
    } 
    else if (strcmp(command, "firmware") == 0) 
    {
      char* type = strtok(NULL, " \n");
      char* filename = strtok(NULL, " \n");

      if (type != NULL && filename != NULL) 
      {
        if (firmware_type != NULL) 
            LOGE("ignoring attempt to do multiple firmware updates");
        else 
        {
          firmware_type = strdup(type);
          firmware_filename = strdup(filename);
        }
      }
    } 
    else if (strcmp(command, "ui_print") == 0) 
    {
      char* str = strtok(NULL, "\n");
      if (str) 
        ui_print(str);
      else
        ui_print("\n");
        
    } 
    else 
      LOGE("unknown command [%s]\n", command);   
  }
  
  fclose(from_child);

  int status;
  waitpid(pid, &status, 0);
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) 
  {
    LOGE("Error in %s\n(Status %d)\n", path, WEXITSTATUS(status));
    return INSTALL_ERROR;
  }

  if (firmware_type != NULL) 
    return handle_firmware_update(firmware_type, firmware_filename, zip);
  else 
    return INSTALL_SUCCESS;
  
}

static int
handle_update_package(const char *path, ZipArchive *zip)
{
    // Update should take the rest of the progress bar.
    ui_print("Installing update...\n");

    int result = try_update_binary(path, zip);
    register_package_root(NULL, NULL);  // Unregister package root
    return result;
}

void run_shell_script(const char *command, int stdoutToUI, char** extra_env_variables) 
{
	char *argp[] = {PHONE_SHELL, "-c", NULL, NULL};
	
	//check it
	if (!command)
		return;
	
	//add the command
	argp[2] = (char *)command;
	fprintf(stderr, "Running Shell Script: \"%s\"\n", command);
	
	//pipes
	int script_pipefd[2];
		
	if (stdoutToUI)
  	pipe(script_pipefd);

	pid_t child = fork();
	if (child == 0) 
	{
		if (stdoutToUI)
		{
			//put stdout to the pipe only
			close(script_pipefd[0]);
			dup2(script_pipefd[1], 1); 
		}
		
		if (extra_env_variables != NULL)
		{
			while (*extra_env_variables)
			{
				fprintf(stderr, "run_shell_script: child env variable %s\n", *extra_env_variables);
				putenv(*extra_env_variables);
				extra_env_variables++;
			}
		}
		
		execv(PHONE_SHELL, argp);
		fprintf(stderr, "run_shell_script: execv failed: %s\n", strerror(errno));
		_exit(1);
	}
	
	//status for the waitpid	
	int sts;
	
	//interactive menu shared memory node
	int imenu_fd = 0;
	
	if (stdoutToUI)
	{				
		char buffer[1024+1];
		
		//nonblocking mode
		int f = fcntl(script_pipefd[0], F_GETFL, 0);
  	// Set bit for non-blocking flag
  	f |= O_NONBLOCK;
  	// Change flags on fd
  	fcntl(script_pipefd[0], F_SETFL, f);
  	
		if((imenu_fd = open(INTERACTIVE_MENU_SHM, (O_CREAT | O_RDWR),
				             666)) < 0 ) 
		{
			LOGE("Failed opening the shared memory node for interactive menu.\n");
			LOGE("Interactive menu disabled.\n");
		}
		else
		{
			ftruncate(imenu_fd, sizeof(interactive_menu_struct));
			if ((interactive_menu = ((interactive_menu_struct*) mmap(0, sizeof(interactive_menu_struct), (PROT_READ | PROT_WRITE),
                   MAP_SHARED, imenu_fd, 0))) == MAP_FAILED)
			{
				LOGE("Failed opening the shared memory node for interactive menu.\n");
				LOGE("Interactive menu disabled.\n");
			}
			else
			{
				interactive_menu->in_trigger = 0;
				interactive_menu->out_trigger = 0;
				interactive_menu->header[0] = '\0';
				interactive_menu->items[0][0] = '\0';
			}
		}

								
  	while (1)
  	{
  		//maybe use signalling
  		if (interactive_menu != NULL && interactive_menu->in_trigger)
  		{
  			interactive_menu->in_trigger = 0;
  			//first print the rest, but don't bother if there is an error
  			int rv = read(script_pipefd[0], buffer, 1024);	
  			if (rv > 0)
  			{
  				buffer[rv] = 0;	
					ui_print_raw(buffer);
				}
				
				//parse the name and headers
				char* headers[3];
				char* items[20];
				
				headers[0] = interactive_menu->header;
				headers[1] = " ";
				headers[2] = NULL;
				
				int i;
				for (i = 0; interactive_menu->items[i][0] && i < 20; i++)
					items[i] = interactive_menu->items[i];
			
				items[i] = NULL;
				
				//show the menu
				ui_led_toggle(0);
				int chosen_item = show_interactive_menu(headers, items);
        ui_led_blink(1);
        
        interactive_menu->header[0] = '\0';
				interactive_menu->items[0][0] = '\0';
        interactive_menu->out_trigger = chosen_item;   
  		}
  	
  		int rv = read(script_pipefd[0], buffer, 1024);		
  		
  		if (rv <= 0)
  		{
  			if(errno == EAGAIN)
  			{
					if (waitpid(child, &sts, WNOHANG))
					{
						//do one last check (race)
						int rv = read(script_pipefd[0], buffer, 1024);
						
						if (rv > 0)
						{
							buffer[rv] = 0;	
							ui_print_raw(buffer);
						}
						
						break;
					}
  					
  				continue;
  			}
  			
  			fprintf(stderr, "run_shell_script: there was a read error %d.\n", errno);
  			waitpid(child, &sts, 0);
  			break;
  		}
  	  		
  		//if the string is read only partially, it won't be null terminated		  		
			buffer[rv] = 0;	
			ui_print_raw(buffer);
  	}
	}
	else
		waitpid(child, &sts, 0);
		
	//check exit status
	if (WIFEXITED(sts)) 
	{
		if (WEXITSTATUS(sts) != 0) 
		{
			fprintf(stderr, "run_shell_script: child exited with status %d.\n",
			WEXITSTATUS(sts));
		}
	} 
	else if (WIFSIGNALED(sts)) 
	{
		fprintf(stderr, "run_shell_script: child terminated by signal %d.\n",
		WTERMSIG(sts));
	}

	if (stdoutToUI)
	{
		//close the pipe here after killing the process
		close(script_pipefd[1]);
		close(script_pipefd[0]);
		
		if (imenu_fd > 0)
		{
			close(imenu_fd);
			remove(INTERACTIVE_MENU_SHM);
		}
	}
}

int
install_package(const char *root_path)
{
    ui_set_background(BACKGROUND_ICON_INSTALLING);
    ui_print("Finding update package...\n");
    LOGI("Update location: %s\n", root_path);

    if (ensure_root_path_mounted(root_path) != 0) {
        LOGE("Can't mount %s\n", root_path);
        return INSTALL_CORRUPT;
    }

    char path[PATH_MAX] = "";
    if (translate_root_path(root_path, path, sizeof(path)) == NULL) {
        LOGE("Bad path %s\n", root_path);
        return INSTALL_CORRUPT;
    }

    ui_print("Opening update package...\n");
    LOGI("Update file path: %s\n", path);

    /* Try to open the package.
     */
    int err;
    ZipArchive zip;
    err = mzOpenZipArchive(path, &zip);
    if (err != 0) {
        LOGE("Can't open %s\n(%s)\n", path, err != -1 ? strerror(err) : "bad");
        return INSTALL_CORRUPT;
    }

    /* Verify and install the contents of the package.
     */
    int status = handle_update_package(path, &zip);
    mzCloseZipArchive(&zip);
    return status;
}
