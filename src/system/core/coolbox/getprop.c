#include <stdio.h>
#include <cutils/properties.h>
#include <sys/system_properties.h>

static void proplist(const char *key, const char *name, 
                     void *user __attribute__((unused)))
{
	printf("[%s]: [%s]\n", key, name);
}

int __system_property_wait(prop_info *pi);

int getprop_main(int argc, char *argv[])
{
  int n = 0;

  if (argc == 1) 
		property_list(proplist, NULL);
  else
  {
    char value[PROPERTY_VALUE_MAX];
    char *default_value;
    if(argc > 2) 
			default_value = argv[2];
    else 
			default_value = "";
    
    property_get(argv[1], value, default_value);
    if (value[0] == '\0' && default_value[0] == '\0')
    {
    	printf("The property \"%s\" doesn't exist.\n", argv[1]);
    	return 1;
    }
    printf("%s\n", value);
  }
  
  return 0;
}
