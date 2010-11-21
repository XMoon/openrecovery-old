/*
 * Copyright (C) 2008 The Android Open Source Project
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

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <errno.h>

#include "sysdeps.h"

#define  TRACE_TAG  TRACE_ADB
#include "adb.h"


static int system_ro = 1;


/* Init mounts /system as read only, remount to enable writes. */
static int remount_system()
{
    if (system_ro == 0) {
        return 0;
    }
    system_ro = mount(NULL, "/system", NULL, MS_REMOUNT, NULL);
   
    return system_ro;
}

static void write_string(int fd, const char* str)
{
    writex(fd, str, strlen(str));
}

void remount_service(int fd, void *cookie)
{
    int ret = remount_system();

    if (!ret)
       write_string(fd, "remount succeeded\n");
    else {
        char    buffer[200];
        snprintf(buffer, sizeof(buffer), "remount failed: %s\n", strerror(errno));
        write_string(fd, buffer);
    }

    adb_close(fd);
}

