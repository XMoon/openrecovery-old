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

#include "minzip/SysUtil.h"
#include "minzip/Zip.h"

#define ASSUMED_UPDATE_BINARY_NAME  		"META-INF/com/google/android/update-binary"
#define DEFAULT_UPDATE_BINARY_NAME  		"/sbin/updater"
#define MY_ZIP_UPDATE_LOCATION					"/sdcard/update-unsigned.zip" 

int main(int argc, char** argv)
{
  //check the arguments
  if (argc != 4)
  	return 3;
  
  /*ZipArchive zip;
  int err = mzOpenZipArchive(MY_ZIP_UPDATE_LOCATION, &zip);
  
  if (err != 0) 
  	return 1;
  
	const ZipEntry* binary_entry =
		      mzFindZipEntry(&zip, ASSUMED_UPDATE_BINARY_NAME);

	char* binary = "/tmp/update_binary";
  unlink(binary);

	if (binary_entry != NULL)
  {  
		int fd = creat(binary, 0755);
		if (fd < 0) 
		  return 1;
  
		bool ok = mzExtractZipEntryToFile(&zip, binary_entry, fd);
		close(fd);

		if (!ok) 
	    return 2;
	}
	else*/
	char*	binary = DEFAULT_UPDATE_BINARY_NAME;

  //update the arguments -- 5 arguments always  
  argv[0] = binary;
  argv[3] = MY_ZIP_UPDATE_LOCATION;
  
  //exec updater
  execv(binary, argv);
  return 3;
}
