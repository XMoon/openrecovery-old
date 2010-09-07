/*
 * definitions copied from yaffs project
 */

#ifndef __YAFFS_DEF_H__
#define __YAFFS_DEF_H__

#define CHUNK_SIZE 2048
#define SPARE_SIZE 64
#define MAX_OBJECTS 50000

#define YAFFS_MAX_NAME_LENGTH       255
#define YAFFS_MAX_ALIAS_LENGTH      159

#define YAFFS_OBJECTID_ROOT		1
#define YAFFS_OBJECTID_LOSTNFOUND	2
#define YAFFS_OBJECTID_UNLINKED		3
#define YAFFS_OBJECTID_DELETED		4

/* Sequence numbers are used in YAFFS2 to determine block allocation order.
 * The range is limited slightly to help distinguish bad numbers from good.
 * This also allows us to perhaps in the future use special numbers for
 * special purposes.
 * EFFFFF00 allows the allocation of 8 blocks per second (~1Mbytes) for 15 years, 
 * and is a larger number than the lifetime of a 2GB device.
 */
#define YAFFS_LOWEST_SEQUENCE_NUMBER	0x00001000
#define YAFFS_HIGHEST_SEQUENCE_NUMBER	0xEFFFFF00

#define YAFFS_NOBJECT_BUCKETS		256

typedef struct {
    unsigned sequenceNumber;
    unsigned objectId;
    unsigned chunkId;
    unsigned byteCount;
} yaffs_PackedTags2TagsPart;

typedef struct {
    unsigned char colParity;
    unsigned lineParity;
    unsigned lineParityPrime; 
} yaffs_ECCOther;

typedef struct {
    yaffs_PackedTags2TagsPart t;
    yaffs_ECCOther ecc;
} yaffs_PackedTags2;

typedef enum {
    YAFFS_ECC_RESULT_UNKNOWN,
    YAFFS_ECC_RESULT_NO_ERROR,
    YAFFS_ECC_RESULT_FIXED,
    YAFFS_ECC_RESULT_UNFIXED
} yaffs_ECCResult;

typedef enum {
    YAFFS_OBJECT_TYPE_UNKNOWN,
    YAFFS_OBJECT_TYPE_FILE,
    YAFFS_OBJECT_TYPE_SYMLINK,
    YAFFS_OBJECT_TYPE_DIRECTORY,
    YAFFS_OBJECT_TYPE_HARDLINK,
    YAFFS_OBJECT_TYPE_SPECIAL
} yaffs_ObjectType;


typedef struct {

    unsigned validMarker0;
    unsigned chunkUsed; /*  Status of the chunk: used or unused */
    unsigned objectId;  /* If 0 then this is not part of an object (unused) */
    unsigned chunkId;   /* If 0 then this is a header, else a data chunk */
    unsigned byteCount; /* Only valid for data chunks */

    /* The following stuff only has meaning when we read */
    yaffs_ECCResult eccResult;
    unsigned blockBad;

    /* YAFFS 1 stuff */
    unsigned chunkDeleted;  /* The chunk is marked deleted */
    unsigned serialNumber;  /* Yaffs1 2-bit serial number */

    /* YAFFS2 stuff */
    unsigned sequenceNumber;    /* The sequence number of this block */

    /* Extra info if this is an object header (YAFFS2 only) */

    unsigned extraHeaderInfoAvailable;  /* There is extra info available if this is not zero */
    unsigned extraParentObjectId;   /* The parent object */
    unsigned extraIsShrinkHeader;   /* Is it a shrink header? */
    unsigned extraShadows;      /* Does this shadow another object? */

    yaffs_ObjectType extraObjectType;   /* What object type? */

    unsigned extraFileLength;       /* Length if it is a file */
    unsigned extraEquivalentObjectId;   /* Equivalent object Id if it is a hard link */

    unsigned validMarker1;

} yaffs_ExtendedTags;

/* -------------------------- Object structure -------------------------------*/
/* This is the object structure as stored on NAND */

typedef struct {
    yaffs_ObjectType type;

    /* Apply to everything  */
    int parentObjectId;
		__u16 sum__NoLongerUsed;        /* checksum of name. No longer used */
		char name[YAFFS_MAX_NAME_LENGTH + 1];

		/* The following apply to directories, files, symlinks - not hard links */
		__u32 yst_mode;         /* protection */
    __u32 yst_uid;
    __u32 yst_gid;
    __u32 yst_atime;
    __u32 yst_mtime;
    __u32 yst_ctime;

    /* File size  applies to files only */
    int fileSize;

    /* Equivalent object id applies to hard links only. */
    int equivalentObjectId;

    /* Alias is for symlinks only. */
    char alias[YAFFS_MAX_ALIAS_LENGTH + 1];

    __u32 yst_rdev;     /* device stuff for block and char devices (major/min) */
    __u32 roomToGrow[10];

    int shadowsObject;  /* This object header shadows the specified object if > 0 */

    /* isShrink applies to object headers written when we shrink the file (ie resize) */
    __u32 isShrink;

} yaffs_ObjectHeader;

//tags
void yaffs_PackTags2(yaffs_PackedTags2 * pt, const yaffs_ExtendedTags * t);
void yaffs_UnpackTags2(yaffs_ExtendedTags * t, yaffs_PackedTags2 * pt);

#endif
