/*
 * Copyright (C) 2010 Skrilax_CZ
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

// uses sh hijack

// fairly simple - determine if to bootstrap into open recovery

// if booting into or bootstrap:
// patch the ramdisk (init, recovery, updater, init.rc script and init.menu)
// call the 2nd-init - to restart the /init

// if booting normally:
// exec a file "/system/bin/sh_hijack"
// !note that the file must restore "sh" by mounting it correctly

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <string.h>
#include <linux/input.h>
#include <ctype.h>

#define SH_INIT_SCRIPT "/system/bin/sh_hijack"
#define OR_BOOTSTRAP_DIR "/system/orbootstrap"

#define OR_DO_BOOTSTRAP_MARK "/cache/.boot_to_or"

static int check_boot_to_or()
{
	//check if supposed to boot to or
	struct stat buf;
 	int fstat = stat(OR_DO_BOOTSTRAP_MARK, &buf);
 	
 	if (!fstat)
	{
		unlink(OR_DO_BOOTSTRAP_MARK);
		return 1;
	}

	//check if key volume up is pressed
	
	char key_states[KEY_MAX/8 + 1];
	memset(key_states, 0, sizeof(key_states));
	
	// /dev/input/event4 is the kdb
	int fd = open("/dev/input/event4", O_RDWR);
	int res = 0;
	if ( ioctl(fd, EVIOCGKEY(sizeof(key_states)), key_states) )
		res = (key_states[KEY_VOLUMEUP/8] & (1<<(KEY_VOLUMEUP%8))) != 0;
	
	close(fd);
	return res;
}

static void get_hardware_name(char* hardware)
{
	char data[1024];
	int fd, n;
	char *x, *hw;

	hardware[0] = '\0';

	fd = open("/proc/cpuinfo", O_RDONLY);
	if (fd < 0) 
		return;

	n = read(fd, data, 1023);
	close(fd);
	if (n < 0) 
		return;

	data[n] = 0;
	hw = strstr(data, "\nHardware");

	if (hw) 
	{
		x = strstr(hw, ": ");
		if (x) 
		{
			x += 2;
			n = 0;
			while (*x && *x != '\n') 
			{
				if (!isspace(*x))
					hardware[n++] = tolower(*x);
				x++;
				if (n == 31) 
					break;
			}
			hardware[n] = 0;
		}
	}
}

static int install_bootstrap()
{
	pid_t my_pid;
  int waitstat;
  
   
 	my_pid = vfork(); 
  
  if (my_pid == 0)
  {
		
		_exit(2);
  }
  
  if (my_pid > 0)
  	my_pid = waitpid(my_pid, (int *)&waitstat, 0);
  	
  return (my_pid == -1 ? -1 : waitstat);

}


int main(int argc, char** argv)
{
	if (check_boot_to_or())
	{
		//special shell script
		char buff[512];
		char hardware[32];
		
		get_hardware_name(hardware);
		snprintf(buff, sizeof(buff), "/system/orbootstrap/utils/install.%s.btsh", hardware);

		//restarts the init
		execl(buff, buff, NULL );	}
	else
	{
		execl(SH_INIT_SCRIPT, SH_INIT_SCRIPT, NULL);
	}
		
	return 2;
}



