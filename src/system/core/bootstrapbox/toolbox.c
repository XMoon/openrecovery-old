#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int, char **);
static int bootstrapbox_main(int argc, char **argv);

#define TOOL(name) int name##_main(int, char**);
#include "tools.h"
#undef TOOL

static struct 
{
    const char *name;
    int (*func)(int, char**);
} tools[] = {
    { "bootstrapbox", bootstrapbox_main },
#define TOOL(name) { #name, name##_main },
#include "tools.h"
#undef TOOL
    { 0, 0 },
};

static int bootstrapbox_main(int argc, char **argv)
{
		int i;
		
    // "toolbox foo ..." is equivalent to "foo ..."
    if (argc > 1) {
        return main(argc - 1, argv + 1);
    } else {
        printf("B00T$TR@PB0X!\n");
        printf("Supported tools:");
        printf(" %s", tools[1].name);
    
        for(i = 2; tools[i].name; i++)
            printf(", %s", tools[i].name);
    
        printf(".\n");
        return 0;
    }
}


int main(int argc, char **argv)
{
    int i;
    char *name = argv[0];

    if((argc > 1) && (argv[1][0] == '@')) {
        name = argv[1] + 1;
        argc--;
        argv++;
    } else {
        char *cmd = strrchr(argv[0], '/');
        if (cmd)
            name = cmd + 1;
    }

    for(i = 0; tools[i].name; i++){
        if(!strcmp(tools[i].name, name)){
            return tools[i].func(argc, argv);
        }
    }

    printf("%s: no such tool\n", argv[0]);
    return -1;
}
