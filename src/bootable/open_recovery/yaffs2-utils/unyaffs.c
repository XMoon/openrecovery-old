/*
 * unyaffs: extract files from yaffs2 file system image to current directory
 *
 * Created by Kai Wei <kai.wei.cn@gmail.com>
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "yaffs_def.h"

char *obj_list[MAX_OBJECTS];
unsigned char data[CHUNK_SIZE + SPARE_SIZE];
unsigned char *chunk_data = data;
unsigned char *spare_data = data + CHUNK_SIZE;
int img_file;

int read_chunk()
{
	ssize_t s;
	int ret = -1;
	memset(chunk_data, 0xff, sizeof(chunk_data));
	s = read(img_file, data, CHUNK_SIZE + SPARE_SIZE);
	if (s == -1) {
		perror("read image file\n");
	} else if (s == 0) {
		printf("end of image\n");
	} else if ((s == (CHUNK_SIZE + SPARE_SIZE))) {
		ret = 0;
	} else {
		fprintf(stderr, "broken image file\n");
	}
	return ret;
}

int process_chunk()
{
	int out_file, s;
	unsigned int remain;
	char *full_path_name;
	int do_chmod;
	
	yaffs_PackedTags2 *pt = (yaffs_PackedTags2 *)spare_data;
	if (pt->t.byteCount == 0xffff)  {	//a new object 

		yaffs_ObjectHeader *oh = (yaffs_ObjectHeader *)malloc(sizeof(yaffs_ObjectHeader));
		memcpy(oh, chunk_data, sizeof(yaffs_ObjectHeader));

		full_path_name = (char *)malloc(strlen(oh->name) + strlen(obj_list[oh->parentObjectId]) + 2);
		if (full_path_name == NULL) {
			perror("malloc full path name\n");
		}
		strcpy(full_path_name, obj_list[oh->parentObjectId]);
		strcat(full_path_name, "/");
		strcat(full_path_name, oh->name);
		obj_list[pt->t.objectId] = full_path_name;
		do_chmod = 1;

		switch(oh->type) {
			case YAFFS_OBJECT_TYPE_FILE:
				remain = oh->fileSize;
				out_file = creat(full_path_name, 0777); //set the owner & permissions later
				printf("File: %s %u %u %o\n", oh->name, oh->yst_uid, oh->yst_gid, oh->yst_mode);
				while(remain > 0) {
					if (read_chunk())
						return -1;
					s = (remain < pt->t.byteCount) ? remain : pt->t.byteCount;	
					if (write(out_file, chunk_data, s) == -1)
						return -1;
					remain -= s;
				}
				close(out_file);						
				break;
			case YAFFS_OBJECT_TYPE_SYMLINK:
				symlink(oh->alias, full_path_name);
				printf("Symlink: %s %u %u %o\n", oh->name, oh->yst_uid, oh->yst_gid, oh->yst_mode);
				do_chmod = 0;
				break;
			case YAFFS_OBJECT_TYPE_DIRECTORY:
				mkdir(full_path_name, 0777); //set the owner & permissions later
				printf("Directory: %s %u %u %o\n", oh->name, oh->yst_uid, oh->yst_gid, oh->yst_mode);
				break;
			case YAFFS_OBJECT_TYPE_HARDLINK:
				link(obj_list[oh->equivalentObjectId], full_path_name);
				printf("Hardlink: %s %u %u %o\n", oh->name, oh->yst_uid, oh->yst_gid, oh->yst_mode);
				break;
			case YAFFS_OBJECT_TYPE_SPECIAL:
				printf("Special object skipped.\n");
				return 1;
			case YAFFS_OBJECT_TYPE_UNKNOWN:
				printf("Unknown object skipped.\n");
				return 1;			
		}
		
		if (chown(full_path_name, oh->yst_uid, oh->yst_gid))
			printf("Failure setting the owner.\n");
		
		if (do_chmod)
			if (chmod(full_path_name, oh->yst_mode))
				printf("Failure setting the permissions.\n");
	}
	
	return 0;
}

int main(int argc, char **argv)
{
	if (argc != 3) 
	{
		printf("Usage: unyaffs image_file_name dir_to_extract\n");
		exit(1);
	}
	
	img_file = open(argv[1], O_RDONLY);
	if (img_file == -1) 
	{
		printf("open image file failed\n");
		exit(1);
	}

	char* dirName = argv[2];
	char rootPath[100];
	
	if (dirName[0] == '.' && dirName[1] == '/' )
		dirName+=2;
	
	int dirCreat = mkdir(dirName, 0777);
	if (dirCreat == -1 && errno != EEXIST)
	{
		printf("create dir to extract failed\n");
		exit(1);
	}
	
	if (dirName[0] != '/')
		sprintf(rootPath, "./%s", argv[2]);
	else
		strcpy(rootPath, dirName);
		
	obj_list[YAFFS_OBJECTID_ROOT] = rootPath;
	
	while(1) 
	{
		if (read_chunk() == -1)
			break;
		process_chunk();
	}
	
	close(img_file);
	return 0;
}
