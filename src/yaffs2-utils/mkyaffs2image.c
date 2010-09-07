/*
 * YAFFS: Yet another FFS. A NAND-flash specific file system.
 *
 * makeyaffsimage.c 
 *
 * Makes a YAFFS file system image that can be used to load up a file system.
 *
 * Copyright (C) 2002 Aleph One Ltd.
 *   for Toby Churchill Ltd and Brightstar Engineering
 *
 * Created by Charles Manning <charles@aleph1.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 * Nick Bane modifications flagged NCB
 *
 * Endian handling patches by James Ng.
 * 
 * mkyaffs2image hacks by NCB
 *
 */

/*** HCV867 =========================================================================

Generail idea to have this utility built for target is to be able to construct YAFFS2
image on the live system. We need it to take a snapshot of data partition after Android
and Blur have finished their initialization and ready to run. Then this snapshot can
be flashed in new phone's userdata partition to eliminate 1st boot packages install.

Tool uses exceptions to prevent certain subdirectories from being scanned. Whole 
directory listed in exceptions file with all it's subdirectories (if they exist)
will be excluded from image building.

Typical exceptions file is as follows:

/data/lost+found
/data/kernelpanics/
/data/panicreport/
/data/local/tmp

Trailing slesh '/' in /data/kernelpnics plays an important role. In fact, the difference 
between directories /data/lost+found and /data/kernelpanics is that unlike 
/data/lost+found, directory /data/kernelpanics will be created in the image.

================================================================================== ***/

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "yaffs_def.h"

#define EXTRA_HEADER_INFO_FLAG	0x80000000
#define EXTRA_SHRINK_FLAG	0x40000000
#define EXTRA_SHADOWS_FLAG	0x20000000
#define EXTRA_SPARE_FLAGS	0x10000000

#define EXTRA_OBJECT_TYPE_SHIFT (28)
#define EXTRA_OBJECT_TYPE_MASK  ((0x0F) << EXTRA_OBJECT_TYPE_SHIFT)

unsigned source_path_len = 0;
unsigned yaffs_traceMask=0;

typedef struct
{
	dev_t dev;
	ino_t ino;
	int   obj;
} objItem;


//#define printf (fmt,args...)	do { printf (fmt,## args); } while(0)
//#define fprintf (stderr,fmt,args...)	do { fprintf (stderr,fmt,## args); } while(0)

static objItem obj_list[MAX_OBJECTS];
static int n_obj = 0;
static int obj_id = YAFFS_NOBJECT_BUCKETS + 1;
static int nObjects, nDirectories, nPages;
static int outFile;
static int error;
static int nExcls;
static char *excl_list[50];

static int read_exceptions (FILE *fp)
{
	char buffer[256];
	while (fgets (buffer, 255, fp) != NULL)
	{
		if (buffer[ strlen(buffer)-1 ] == '\n')
			buffer[ strlen(buffer)-1 ] = 0;
		if (strlen (buffer)) 
		{
			excl_list[ nExcls ] = strdup (buffer);
			printf ("Excluded path %s\n", buffer);
			nExcls++;
		}
		
	}
  printf ("Found %d exceptions\n", nExcls);
  return nExcls;
}

static int is_entry_exception (char *direntry)
{
    int i, it_is=0;
    for (i=0; excl_list[i]; i++) {
	//printf ("Comparising \"%s\" with \"%s\" rc=%d\n", direntry, excl_list[i],
	//		strncmp (direntry, excl_list[i], strlen(excl_list[i])));
	if (! strncmp (direntry, excl_list[i], strlen(excl_list[i])))
	{
		//printf ("Path %s is an exception\n", direntry); 
		it_is = 1;
		break;    
	}
    }
    return it_is;
}


#ifdef HAVE_BIG_ENDIAN
static int convert_endian = 1;
#elif defined(HAVE_LITTLE_ENDIAN)
static int convert_endian = 0;
#endif

static int obj_compare(const void *a, const void * b)
{
  objItem *oa, *ob;
  
  oa = (objItem *)a;
  ob = (objItem *)b;
  
  if(oa->dev < ob->dev) return -1;
  if(oa->dev > ob->dev) return 1;
  if(oa->ino < ob->ino) return -1;
  if(oa->ino > ob->ino) return 1;
  
  return 0;
}


static void add_obj_to_list(dev_t dev, ino_t ino, int obj)
{
	if(n_obj < MAX_OBJECTS)
	{
		obj_list[n_obj].dev = dev;
		obj_list[n_obj].ino = ino;
		obj_list[n_obj].obj = obj;
		n_obj++;
		qsort(obj_list,n_obj,sizeof(objItem),obj_compare);		
	}
	else
	{
		// oops! not enough space in the object array
		fprintf (stderr,"Not enough space in object array\n");
		exit(2);
	}
}


static int find_obj_in_list(dev_t dev, ino_t ino)
{
	objItem *i = NULL;
	objItem test;

	test.dev = dev;
	test.ino = ino;
	
	if(n_obj > 0)
	{
		i = bsearch(&test,obj_list,n_obj,sizeof(objItem),obj_compare);
	}

	if(i)
	{
		return i->obj;
	}
	return -1;
}

#define SWAP32(x)   ((((x) & 0x000000FF) << 24) | \
                     (((x) & 0x0000FF00) << 8 ) | \
                     (((x) & 0x00FF0000) >> 8 ) | \
                     (((x) & 0xFF000000) >> 24))

#define SWAP16(x)   ((((x) & 0x00FF) << 8) | \
                     (((x) & 0xFF00) >> 8))
        
// This one is easier, since the types are more standard. No funky shifts here.
static void object_header_little_to_big_endian(yaffs_ObjectHeader* oh)
{
    oh->type = SWAP32(oh->type); // GCC makes enums 32 bits.
    oh->parentObjectId = SWAP32(oh->parentObjectId); // int
    oh->sum__NoLongerUsed = SWAP16(oh->sum__NoLongerUsed); // __u16 - Not used, but done for completeness.
    // name = skip. Char array. Not swapped.
    oh->yst_mode = SWAP32(oh->yst_mode);

    // Regular POSIX.
    oh->yst_uid = SWAP32(oh->yst_uid);
    oh->yst_gid = SWAP32(oh->yst_gid);
    oh->yst_atime = SWAP32(oh->yst_atime);
    oh->yst_mtime = SWAP32(oh->yst_mtime);
    oh->yst_ctime = SWAP32(oh->yst_ctime);

    oh->fileSize = SWAP32(oh->fileSize); // Aiee. An int... signed, at that!
    oh->equivalentObjectId = SWAP32(oh->equivalentObjectId);
    // alias  - char array.
    oh->yst_rdev = SWAP32(oh->yst_rdev);
    oh->roomToGrow[0] = SWAP32(oh->roomToGrow[0]);
    oh->roomToGrow[1] = SWAP32(oh->roomToGrow[1]);
    oh->roomToGrow[2] = SWAP32(oh->roomToGrow[2]);
    oh->roomToGrow[3] = SWAP32(oh->roomToGrow[3]);
    oh->roomToGrow[4] = SWAP32(oh->roomToGrow[4]);
    oh->roomToGrow[5] = SWAP32(oh->roomToGrow[5]);
    oh->roomToGrow[6] = SWAP32(oh->roomToGrow[6]);
    oh->roomToGrow[7] = SWAP32(oh->roomToGrow[7]);
    oh->roomToGrow[8] = SWAP32(oh->roomToGrow[8]);
    oh->roomToGrow[9] = SWAP32(oh->roomToGrow[9]);
    oh->shadowsObject = SWAP32(oh->shadowsObject);
    oh->isShrink = SWAP32(oh->isShrink);
}

/* This little function converts a little endian tag to a big endian tag.
 * NOTE: The tag is not usable after this other than calculating the CRC
 * with.
 */
static void little_to_big_endian(yaffs_PackedTags2 *pt)
{
	pt->t.sequenceNumber = SWAP32(pt->t.sequenceNumber);
	pt->t.objectId = SWAP32(pt->t.objectId);
	pt->t.chunkId = SWAP32(pt->t.chunkId);
	pt->t.byteCount = SWAP32(pt->t.byteCount);
}

static void initialiseTags(yaffs_ExtendedTags * tags)
{
	memset(tags, 0, sizeof(yaffs_ExtendedTags));
	tags->validMarker0 = 0xAAAAAAAA;
	tags->validMarker1 = 0x55555555;
}

void packTags2(yaffs_PackedTags2 * pt, const yaffs_ExtendedTags * t)
{
	pt->t.chunkId = t->chunkId;
	pt->t.sequenceNumber = t->sequenceNumber;
	pt->t.byteCount = t->byteCount;
	pt->t.objectId = t->objectId;

	if (t->chunkId == 0 && t->extraHeaderInfoAvailable) {
		/* Store the extra header info instead */
		/* We save the parent object in the chunkId */
		pt->t.chunkId = EXTRA_HEADER_INFO_FLAG
			| t->extraParentObjectId;
		if (t->extraIsShrinkHeader) {
			pt->t.chunkId |= EXTRA_SHRINK_FLAG;
		}
		if (t->extraShadows) {
			pt->t.chunkId |= EXTRA_SHADOWS_FLAG;
		}

		pt->t.objectId &= ~EXTRA_OBJECT_TYPE_MASK;
		pt->t.objectId |=
		    (t->extraObjectType << EXTRA_OBJECT_TYPE_SHIFT);

		if (t->extraObjectType == YAFFS_OBJECT_TYPE_HARDLINK) {
			pt->t.byteCount = t->extraEquivalentObjectId;
		} else if (t->extraObjectType == YAFFS_OBJECT_TYPE_FILE) {
			pt->t.byteCount = t->extraFileLength;
		} else {
			pt->t.byteCount = 0;
		}
	}
}	

static int write_chunk(__u8 *data, __u32 objId, __u32 chunkId, __u32 nBytes)
{
	yaffs_ExtendedTags t;
	yaffs_PackedTags2 pt;

	error = write(outFile,data,CHUNK_SIZE);
	if(error < 0) return error;

	initialiseTags(&t);
	
	t.chunkId = chunkId;
//	t.serialNumber = 0;
	t.serialNumber = 1;	// **CHECK**
	t.byteCount = nBytes;
	t.objectId = objId;
	
	t.sequenceNumber = YAFFS_LOWEST_SEQUENCE_NUMBER;

// added NCB **CHECK**
	t.chunkUsed = 1;

	nPages++;

	packTags2(&pt,&t);

	if (convert_endian)
	{
		little_to_big_endian(&pt);
	}
	
//	return write(outFile,&pt,sizeof(yaffs_PackedTags2));
	return write(outFile,&pt,SPARE_SIZE);
	
}

static int write_object_header(int objId, yaffs_ObjectType t, struct stat *s, int parent, const char *name, int equivalentObj, const char * alias, char *perm)
{
	__u8 bytes[CHUNK_SIZE];
	
	yaffs_ObjectHeader *oh = (yaffs_ObjectHeader *)bytes;
	
	memset(bytes,0xff,sizeof(bytes));
	oh->type = t;
	oh->parentObjectId = parent;
	strncpy(oh->name,name,YAFFS_MAX_NAME_LENGTH);
	
	if(t != YAFFS_OBJECT_TYPE_HARDLINK)
	{
		oh->yst_mode = s->st_mode;
		oh->yst_uid = s->st_uid;
		oh->yst_gid = s->st_gid;
		oh->yst_atime = s->st_atime;
		oh->yst_mtime = s->st_mtime;
		oh->yst_ctime = s->st_ctime;
		oh->yst_rdev  = s->st_rdev;
		if (perm != NULL)
			sprintf (perm, "%u %u %o", oh->yst_uid, oh->yst_gid, oh->yst_mode);
	}
	
	if(t == YAFFS_OBJECT_TYPE_FILE)
	{
		oh->fileSize = s->st_size;
	}
	
	if(t == YAFFS_OBJECT_TYPE_HARDLINK)
	{
		oh->equivalentObjectId = equivalentObj;
	}
	
	if(t == YAFFS_OBJECT_TYPE_SYMLINK)
	{
		strncpy(oh->alias,alias,YAFFS_MAX_ALIAS_LENGTH);
	}

	if (convert_endian)
	{
  	object_header_little_to_big_endian(oh);
	}
	
	return write_chunk(bytes,objId,0,0xffff);
	
}


static int process_directory(int parent, const char *path)
{
	char perm[64];
	DIR *dir;
	struct dirent *entry;

	nDirectories++;
	
	dir = opendir(path);
	
	if(dir)
	{
		while((entry = readdir(dir)) != NULL)
		{
		
			/* Ignore . and .. */
			if(strcmp(entry->d_name,".") &&
			   strcmp(entry->d_name,".."))
 			{
 				char full_name[500];
				struct stat stats;
				int equivalentObj;
				int newObj;
				
				sprintf(full_name,"%s/%s",path,entry->d_name);
				
				lstat(full_name,&stats);
				
				if(S_ISLNK(stats.st_mode) ||
				    S_ISREG(stats.st_mode) ||
				    S_ISDIR(stats.st_mode) ||
				    S_ISFIFO(stats.st_mode) ||
				    S_ISBLK(stats.st_mode) ||
				    S_ISCHR(stats.st_mode) ||
				    S_ISSOCK(stats.st_mode))
				{
					if (is_entry_exception (full_name)) {
						printf ("<---- Path \"%s\" is in exceptions list. Skipping...\n", full_name);
						continue;
					}

					newObj = obj_id++;
					nObjects++;


					printf ("+++> Object %d, %s is a ",newObj,full_name);
					/* We're going to create an object for it */
					if((equivalentObj = find_obj_in_list(stats.st_dev, stats.st_ino)) > 0)
					{
					 	/* we need to make a hard link */
						error =  write_object_header(newObj, YAFFS_OBJECT_TYPE_HARDLINK, &stats, parent, entry->d_name, equivalentObj, NULL, perm);
					 	printf ("hard link to object %d\n",equivalentObj);
					}
					else 
					{	
						add_obj_to_list(stats.st_dev,stats.st_ino,newObj);
						if(S_ISLNK(stats.st_mode))
						{
					
							char symname[500];
						
							memset(symname,0, sizeof(symname));
							readlink(full_name,symname,sizeof(symname) -1);
							error =  write_object_header(newObj, YAFFS_OBJECT_TYPE_SYMLINK, &stats, parent, entry->d_name, -1, symname, perm);
							printf ("symlink to \"%s\" %s\n", symname, perm);
						}
						else if(S_ISREG(stats.st_mode))
						{
							error =  write_object_header(newObj, YAFFS_OBJECT_TYPE_FILE, &stats, parent, entry->d_name, -1, NULL, perm);
							printf("file %s\n", perm);
							if(error >= 0)
							{
								int h;
								__u8 bytes[CHUNK_SIZE];
								int nBytes;
								int chunk = 0;
								unsigned int nTotal=0;
								
								h = open(full_name,O_RDONLY);
								if(h >= 0)
								{
									memset(bytes,0xff,sizeof(bytes));
									while((nBytes = read(h,bytes,sizeof(bytes))) > 0)
									{
										chunk++;
										write_chunk(bytes,newObj,chunk,nBytes);
										memset(bytes,0xff,sizeof(bytes));
										nTotal += nBytes;
									}
									if(nBytes < 0) 
									   error = nBytes;
									   
									printf ("    > %d data chunks written, %u bytes\n",chunk,nTotal);
								}
								else
								{
									fprintf (stderr,"Error opening file: %s", strerror(errno));
								}
								close(h);
								
							}							
														
						}
						else if(S_ISSOCK(stats.st_mode))
						{
							error =  write_object_header(newObj, YAFFS_OBJECT_TYPE_SPECIAL, &stats, parent, entry->d_name, -1, NULL, perm);
							printf ("socket %s\n", perm);
						}
						else if(S_ISFIFO(stats.st_mode))
						{
							error =  write_object_header(newObj, YAFFS_OBJECT_TYPE_SPECIAL, &stats, parent, entry->d_name, -1, NULL, perm);
							printf ("fifo %s\n", perm);
						}
						else if(S_ISCHR(stats.st_mode))
						{
							error =  write_object_header(newObj, YAFFS_OBJECT_TYPE_SPECIAL, &stats, parent, entry->d_name, -1, NULL, perm);
							printf ("character device %s\n", perm);
						}
						else if(S_ISBLK(stats.st_mode))
						{
							error =  write_object_header(newObj, YAFFS_OBJECT_TYPE_SPECIAL, &stats, parent, entry->d_name, -1, NULL, perm);
							printf ("block device %s\n", perm);
						}
						else if(S_ISDIR(stats.st_mode))
						{
							error =  write_object_header(newObj, YAFFS_OBJECT_TYPE_DIRECTORY, &stats, parent, entry->d_name, -1, NULL, perm);
							printf ("directory %s\n", perm);
// NCB modified 10/9/2001				process_directory(1,full_name);
							process_directory(newObj,full_name);
						}
					}
				}
				else
				{
					printf (" we don't handle this type\n");
				}
			}
		}

		closedir(dir);
	}
	
	return 0;

}

int main(int argc, char *argv[])
{
  FILE *exclfp;
  struct stat stats;

  if(argc < 3)
  {
		fprintf(stderr,"mkyaffs2image: image building tool for YAFFS2 built "__DATE__"\n");
		fprintf(stderr,"usage: mkyaffs2image [-f] dir image_file [convert] [exclude_file]\n");
		fprintf(stderr,"         dir          the directory tree to be converted\n");
		fprintf(stderr,"         image_file   the output file to hold the image\n");
		fprintf(stderr,"         'convert'    produce a big-endian image from a little-endian machine\n");
		fprintf(stderr,"         exclude_file file with directories to exclude from image\n");
		exit(1);
	}

  if ((argc == 4) && (!strncmp(argv[3], "convert", strlen("convert"))))
		convert_endian = 1;
	else 
		exclfp = fopen (argv[3], "r");
		
		
	if(exclfp == NULL)
		fprintf (stderr,"Could not open exceptions file %s\n", argv[3]);
	else 
	{
		read_exceptions (exclfp);
		fclose (exclfp);
	}
    
	if (stat (argv[1],&stats) < 0)
	{
		fprintf (stderr,"Could not stat %s\n",argv[1]);
		exit (1);
	}
	
	if (!S_ISDIR (stats.st_mode))
	{
		fprintf (stderr," %s is not a directory\n",argv[1]);
		exit (1);
	}
	
  outFile = open (argv[2], O_CREAT | O_TRUNC | O_WRONLY, S_IRUSR | S_IWUSR);	
  if(outFile < 0)
  {
		fprintf (stderr,"Could not open output file %s\n",argv[2]);
		exit (1);
	}
    
  printf ("Processing directory %s\n", argv[1]);
  if (nExcls ) printf ("Exceptions from %s\n", argv[3]);
  printf ("Result image file %s\n", argv[2]);

  error =  write_object_header (1, YAFFS_OBJECT_TYPE_DIRECTORY, &stats, 1,"", -1, NULL, NULL);
  if(error)
		error = process_directory (YAFFS_OBJECTID_ROOT,argv[1]);
	
	close(outFile);	
  if (error < 0)
  {
		fprintf (stderr,"operation incomplete: %s", strerror(errno));
		exit(1);
	} 
	else    
		printf ("Operation complete.\n"
		       "%d objects in %d directories\n"
		       "%d NAND pages\n",nObjects, nDirectories, nPages);
		       
  close (outFile);
  exit (0);
}	

