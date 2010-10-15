#ifndef __COPY_CP_H__

#define __COPY_CP_H__

enum {  /* DO NOT CHANGE THESE VALUES!  cp.c depends on them. */
        FILEUTILS_PRESERVE_STATUS = 1,
        FILEUTILS_DEREFERENCE = 2,
        FILEUTILS_RECUR = 4,
        FILEUTILS_FORCE = 8,
        FILEUTILS_INTERACTIVE = 16
};

extern char *concat_subpath_fine(const char *path, const char *f);


#endif
