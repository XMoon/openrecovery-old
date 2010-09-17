/*
 * Copyright (C) 2007 Skrilax_CZ
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

#include "envfix.h"

void *read_file(const char *fn, unsigned *_sz)
{
	char *data;
	int sz;
	int fd;

	data = 0;
	fd = open(fn, O_RDONLY);
	if(fd < 0) 
		return 0;

	sz = lseek(fd, 0, SEEK_END);
	if(sz < 0) 
		goto oops;

	if(lseek(fd, 0, SEEK_SET) != 0) 
		goto oops;

	data = (char*) malloc(sz + 2); 
	if(data == 0) 
		goto oops;

	if(read(fd, data, sz) != sz) 
		goto oops;
		
	close(fd);
	data[sz] = '\n';
	data[sz+1] = 0;
	
	if(_sz) 
		*_sz = sz;
		
	return data;

oops:
  close(fd);
  if(data != 0) 
  	free(data);
  return 0;
}

static void load_properties(char *data)
{
  char *key, *value, *eol, *sol, *tmp;

  sol = data;
  while((eol = strchr(sol, '\n'))) 
  {
    key = sol;
    *eol++ = 0;
    sol = eol;

    value = strchr(key, '=');
    if(value == 0) 
    	continue;
    	
    *value++ = 0;

    while(isspace(*key)) 
    	key++;
    if(*key == '#') 
    	continue;
    tmp = value - 2;
    
    while((tmp > key) && isspace(*tmp)) 
    	*tmp-- = 0;

    while(isspace(*value)) 
    	value++;
    tmp = eol - 2;
    
    while((tmp > value) && isspace(*tmp)) 
    	*tmp-- = 0;

    setenv(key, value, 1);
  }
}

int fix_enviroment(const char* source_file)
{
	unsigned int sz;
	char* prop_data = read_file(source_file, &sz);

	if(prop_data != 0) 
	{
	  load_properties(prop_data);
	  free(prop_data);
	  return 0;
	}
	
	return 1;
}

