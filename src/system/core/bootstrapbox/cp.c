/* vi: set sw=4 ts=4: */
/*
 * Mini copy_file implementation for busybox
 *
 * Copyright (C) 2009-2010 Motorola 
 * Copyright (C) 2001 by Matt Kraai <kraai@alumni.carnegiemellon.edu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <utime.h>
#include <errno.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "cp.h"

/* The hardlinks is not supported yet */
#ifdef CONFIG_FEATURE_PRESERVE_HARDLINKS
#undef CONFIG_FEATURE_PRESERVE_HARDLINKS
#endif

#ifdef EMULATE_BASENAME
#undef EMULATE_BASENAME
#endif


typedef int (*stat_func)(const char *fn, struct stat *ps);

char * last_char_is(const char *s, int c)
{
        char *sret = (char *)s;
        if (sret) {
                sret = strrchr(sret, c);
                if(sret != NULL && *(sret+1) != 0)
                        sret = NULL;
        }
        return sret;
}


char *concat_path_file(const char *path, const char *filename)
{
	char *outbuf;
	char *lc;
	outbuf = (char *)malloc(512);

	if(outbuf == NULL) {
		fprintf(stderr,"malloc error \n");
		exit(-1);
	}
	
	if (!path)
		path = "";
	lc = last_char_is(path, '/');
	while (*filename == '/')
		filename++;
		sprintf(outbuf, "%s%s%s", path, (lc==NULL ? "/" : ""), filename);
	return outbuf;
}


char *concat_subpath_file(const char *path, const char *f)
{
	if(f && *f == '.' && (!f[1] || (f[1] == '.' && !f[2])))
		return NULL;
	return concat_path_file(path, f);
}


int cp_mv_stat2(const char *fn, struct stat *fn_stat, stat_func sf)
{
	if (sf(fn, fn_stat) < 0) {
		if (errno != ENOENT) {
			fprintf(stderr,"unable to stat `%s'", fn);
			return -1;
		}
		return 0;
	} else if (S_ISDIR(fn_stat->st_mode)) {
		return 3;
	}
	return 1;
}

extern int cp_mv_stat(const char *fn, struct stat *fn_stat)
{
	return cp_mv_stat2(fn, fn_stat, stat);
}


char *get_last_path_component(char *path)
{
#if EMULATE_BASENAME
	static const char null_or_empty[] = ".";
#endif
	char *first = path;
	char *last;

#if EMULATE_BASENAME
	if (!path || !*path) {
		return (char *) null_or_empty;
	}
#endif

	last = path - 1;

	while (*path) {
		if ((*path != '/') && (path > ++last)) {
			last = first = path;
		}
		++path;
	}

	if (*first == '/') {
		last = first;
	}
	last[1] = 0;

	return first;

}


char *xreadlink(const char *path)
{   
	static const int GROWBY = 80; /* how large we will grow strings by */

	char *buf = NULL;
	int bufsize = 0, readsize = 0;

	do {
		buf = realloc(buf, bufsize += GROWBY);
		if(buf == NULL) {
			 fprintf(stderr,"realloc error\n");
			 exit(-1);
		}
		readsize = readlink(path, buf, bufsize); /* 1st try */
		if (readsize == -1) {
			fprintf(stderr,"%s,%s", path,strerror(errno));
			free(buf);
			return NULL;
		}
	}
	while (bufsize < readsize + 1);

	buf[readsize] = '\0';

	return buf;

}

int copy_file(const char *source, const char *dest, int flags)
{
	struct stat source_stat;
	struct stat dest_stat;
	int dest_exists = 0;
	int status = 0;


	if ((!(flags & FILEUTILS_DEREFERENCE) &&
			lstat(source, &source_stat) < 0) ||
			((flags & FILEUTILS_DEREFERENCE) &&
			 stat(source, &source_stat) < 0)) {
			fprintf(stderr,"%s,%s ", source,strerror(errno));
		return -1;
	}

	if (lstat(dest, &dest_stat) < 0) {
		if (errno != ENOENT) {
			fprintf(stderr,"unable to stat `%s'", dest);
			return -1;
		}
	} else {
		if (source_stat.st_dev == dest_stat.st_dev &&
			source_stat.st_ino == dest_stat.st_ino) {
		fprintf(stderr,"`%s' and `%s' are the same file", source, dest);
		return -1;
	}
		dest_exists = 1;
	}

	if (S_ISDIR(source_stat.st_mode)) {
		DIR *dp;
		struct dirent *d;
		mode_t saved_umask = 0;

		if (!(flags & FILEUTILS_RECUR)) {
			fprintf(stderr,"%s: omitting directory", source);
			return -1;
		}

		/* Create DEST.  */
		if (dest_exists) {
			if (!S_ISDIR(dest_stat.st_mode)) {
				fprintf(stderr,"`%s' is not a directory", dest);
				return -1;
			}
		} else {
			mode_t mode;
			saved_umask = umask(0);

			mode = source_stat.st_mode;
			if (!(flags & FILEUTILS_PRESERVE_STATUS))
				mode = source_stat.st_mode & ~saved_umask;
			mode |= S_IRWXU;

			if (mkdir(dest, mode) < 0) {
				umask(saved_umask);
				fprintf(stderr,"\ncannot create directory `%s'", dest);
				return -1;
			}

			umask(saved_umask);
		}

		/* Recursively copy files in SOURCE.  */
		if ((dp = opendir(source)) == NULL) {
			fprintf(stderr,"\nunable to open directory `%s'", source);
			status = -1;
			goto end;
		}

		while ((d = readdir(dp)) != NULL) {
			char *new_source, *new_dest;

			new_source = concat_subpath_file(source, d->d_name);
			if(new_source == NULL)
				continue;

			new_dest = concat_path_file(dest, d->d_name);

			if (copy_file(new_source, new_dest,flags) < 0)
				status = -1;
			free(new_source);
			free(new_dest);
		}
		/* closedir have only EBADF error, but "dp" not changes */
		closedir(dp);

		if (!dest_exists &&
				chmod(dest, source_stat.st_mode & ~saved_umask) < 0) {
			fprintf(stderr,"\nunable to change permissions of `%s'", dest);
			status = -1;
		}
	} else if (S_ISREG(source_stat.st_mode)) {
		int src_fd;
		int dst_fd;
#ifdef CONFIG_FEATURE_PRESERVE_HARDLINKS
		char *link_name;

		if (!(flags & FILEUTILS_DEREFERENCE) &&
				is_in_ino_dev_hashtable(&source_stat, &link_name)) {
			if (link(link_name, dest) < 0) {
				fprintf(stderr,"unable to link `%s'", dest);
				return -1;
			}

			return 0;
		}
#endif
		src_fd = open(source, O_RDONLY);
		if (src_fd == -1) {
			fprintf(stderr,"\nunable to open `%s'", source);
			return(-1);
		}

		if (dest_exists) {
			
			if(! (flags& FILEUTILS_FORCE)) {
				fprintf(stderr, "\nwarnning`%s'exists,use -f to overwrite ", dest);
			}
#if 0
			if (flags & FILEUTILS_INTERACTIVE) {
				fprintf(stderr, "%s: overwrite `%s'? ", bb_applet_name, dest);
				if (!bb_ask_confirmation()) {
					close (src_fd);
					return 0;
				}
			}
	
#endif
			dst_fd = open(dest, O_WRONLY|O_TRUNC);
			if (dst_fd == -1) {
				if (!(flags & FILEUTILS_FORCE)) {
					fprintf(stderr,"\nunable to open `%s'", dest);
					close(src_fd);
					return -1;
				}

				if (unlink(dest) < 0) {
					fprintf(stderr,"\nunable to remove `%s'", dest);
					close(src_fd);
					return -1;
				}

				dest_exists = 0;
			}
		}

		if (!dest_exists) {
			dst_fd = open(dest, O_WRONLY|O_CREAT, source_stat.st_mode);
			if (dst_fd == -1) {
				fprintf(stderr,"\nunable to open `%s'", dest);
				close(src_fd);
				return(-1);
			}
		}

		if (bb_copyfd_eof(src_fd, dst_fd) == -1)
			status = -1;

		if (close(dst_fd) < 0) {
			fprintf(stderr,"\nunable to close `%s'", dest);
			status = -1;
		}

		if (close(src_fd) < 0) {
			fprintf(stderr,"\nunable to close `%s'", source);
			status = -1;
		}
			}
	else if (S_ISBLK(source_stat.st_mode) || S_ISCHR(source_stat.st_mode) ||
	    S_ISSOCK(source_stat.st_mode) || S_ISFIFO(source_stat.st_mode) ||
	    S_ISLNK(source_stat.st_mode)) {

		if (dest_exists) {
			if((flags & FILEUTILS_FORCE) == 0) {
				fprintf(stderr, "\n`%s' exists\n", dest);
				return -1;
			}
			if(unlink(dest) < 0) {
				fprintf(stderr,"\nunable to remove `%s'", dest);
				return -1;
			}
		}
	} else {
		fprintf(stderr,"\ninternal error: unrecognized file type");
		return -1;
		}
	if (S_ISBLK(source_stat.st_mode) || S_ISCHR(source_stat.st_mode) ||
	    S_ISSOCK(source_stat.st_mode)) {
		if (mknod(dest, source_stat.st_mode, source_stat.st_rdev) < 0) {
			fprintf(stderr,"\nunable to create `%s'", dest);
			return -1;
		}
	} else if (S_ISFIFO(source_stat.st_mode)) {
		if (mkfifo(dest, source_stat.st_mode) < 0) {
			fprintf(stderr,"\ncannot create fifo `%s'", dest);
			return -1;
		}
	} else if (S_ISLNK(source_stat.st_mode)) {
		char *lpath;

		lpath = xreadlink(source);
		if (symlink(lpath, dest) < 0) {
			fprintf(stderr,"\ncannot create symlink `%s'", dest);
			free(lpath);
			return -1;
		}
		free(lpath);

#if (__GLIBC__ >= 2) && (__GLIBC_MINOR__ >= 1)
		if (flags & FILEUTILS_PRESERVE_STATUS)
			if (lchown(dest, source_stat.st_uid, source_stat.st_gid) < 0)
				fprintf(stderr,"\nunable to preserve ownership of `%s'", dest);
#endif

#ifdef CONFIG_FEATURE_PRESERVE_HARDLINKS
		add_to_ino_dev_hashtable(&source_stat, dest);
#endif

		return 0;
	}

#ifdef CONFIG_FEATURE_PRESERVE_HARDLINKS
	if (! S_ISDIR(source_stat.st_mode)) {
		add_to_ino_dev_hashtable(&source_stat, dest);
	}
#endif

end:

	if (flags & FILEUTILS_PRESERVE_STATUS) {
		struct utimbuf times;

		times.actime = source_stat.st_atime;
		times.modtime = source_stat.st_mtime;
		if (utime(dest, &times) < 0)
			fprintf(stderr,"\nunable to preserve times of `%s'", dest);
		if (chown(dest, source_stat.st_uid, source_stat.st_gid) < 0) {
			source_stat.st_mode &= ~(S_ISUID | S_ISGID);
			fprintf(stderr,"\nunable to preserve ownership of `%s'", dest);
		}
		if (chmod(dest, source_stat.st_mode) < 0)
			fprintf(stderr,"\nunable to preserve permissions of `%s'", dest);
	}

	return status;
}


void usage()
{
        printf("\n Mini cp for Android, ported from busybox\n ");
        printf("\n Usage: cp [-adHpfr]... SOURCE DEST\n");
        printf("\n Copy SOURCE to DEST, or multiple SOURCES(s) to DIRECTORY\n");
        printf("\n Options:\
                \n      -a	Archive,Same as -dpr\
                \n      -d	Preserver links, not follow the links(default)\
                \n      -H	Follow links\
                \n      -p	Preserver file attributes if possible\
                \n      -f	force overwirte\
                \n      -r	Recurse directories\
		\n");

        exit(-1);
}


int cp_main(int argc, char *argv[])
{


	struct stat source_stat;
	struct stat dest_stat;
	const char *last;
	const char *dest;
	int s_flags;
	int d_flags;
	int flags = 0 ;
	int status = 0;
	int ch = 0;
	int i = 0;
	int opt = 0;

	if(argc < 3) {
		usage();
	}


	while ((ch = getopt(argc, argv, "-adHpfr")) != -1)
		switch (ch) {
		case 'a':
			opt=1;			
			flags |=  FILEUTILS_RECUR | FILEUTILS_PRESERVE_STATUS;
			break;
		case 'd':
			opt=1;
			flags &= ~FILEUTILS_DEREFERENCE;
			break;
		case 'H':
			opt=1;
			flags |= FILEUTILS_DEREFERENCE;
			break;
		case 'p':
			opt=1;			
			flags |= FILEUTILS_PRESERVE_STATUS;
			break;
		case 'f':
			opt=1;			
			flags |= FILEUTILS_FORCE;
			break;
		case 'r':
			opt=1;			
			flags |= FILEUTILS_RECUR;
			break;
		case '?':
			opt=0;
			usage();
			break;
		}


	if(opt + 2  == argc)
		usage();

	/* dest */
	last = argv[argc-1]; 
	
	
	/*src lists */
	argv += opt+1;

	/* If there are only two arguments and...  */
	if ( opt + 3 == argc ) {
		s_flags = cp_mv_stat2(*argv, &source_stat,
					 (flags & FILEUTILS_DEREFERENCE) ? stat : lstat);
		if ((s_flags < 0) || ((d_flags = cp_mv_stat(last, &dest_stat)) < 0)) {
			exit(-1);
		}
		/* ...if neither is a directory or...  */
		if ( !((s_flags | d_flags) & 2) ||
			/* ...recursing, the 1st is a directory, and the 2nd doesn't exist... */
			/* ((flags & FILEUTILS_RECUR) && (s_flags & 2) && !d_flags) */
			/* Simplify the above since FILEUTILS_RECUR >> 1 == 2. */
			((((flags & FILEUTILS_RECUR) >> 1) & s_flags) && !d_flags)
		) {
			/* ...do a simple copy.  */
				dest = last;
				goto DO_COPY; /* Note: optind+2==argc implies argv[1]==last below. */
		}
	}

	do {
		dest = concat_path_file(last, get_last_path_component(*argv));
	DO_COPY:
		if (copy_file(*argv, dest, flags) < 0) {
			status = 1;
		}
		if (*++argv == last) {
			break;
		}
		free((void *) dest);
	} while (1);

	exit(status);

}
