/*
 * Copyright (C) 2009 The Android Open Source Project
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <linux/input.h>

#include "recovery_ui.h"
#include "common.h"


int device_recovery_start() {
    return 0;
}

int device_toggle_display(volatile char* key_pressed, int key_code) 
{
    return (key_pressed[KEY_VOLUMEUP] && key_code == KEY_HP)
    				|| (key_pressed[KEY_HP] && key_code == KEY_VOLUMEUP);
}

int device_reboot_now(volatile char* key_pressed, int key_code) 
{
    return 0;
}

int device_handle_key(int key_code, int visible) {
    if (visible) {
        switch (key_code) {
            case KEY_DOWN:
            case KEY_VOLUMEDOWN:
                return HIGHLIGHT_DOWN;

            case KEY_UP:
            case KEY_VOLUMEUP:
                return HIGHLIGHT_UP;

            case KEY_CENTER:
            case KEY_CAMERA:
            case KEY_ENTER:
                return SELECT_ITEM;
        }
    }

    return NO_ACTION;
}

int device_perform_action(int which) {
	return which;
}

int device_wipe_data() {
	return 0;
}


