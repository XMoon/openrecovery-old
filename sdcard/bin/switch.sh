#!/sbin/bash

# arguments
# $1 the phone suffix
#
# SHOLS - Milestone (A853, XT702)
# STCU  - Sholes Tablet (XT701)
# STR		- Milestone XT720 (XT720)
# TITA  - Titanium (XT800)

#flags
#===============================================================================

if [ "$1" == "STCU" ]; then
	NOCUST=1
	INSTALL_COMMAND=0
else
	NOCUST=0
	
	if [ "$1" == "SHOLS" ]; then
		INSTALL_COMMAND=1
	fi
fi

#post-installation
#===============================================================================

#put the command into the cache - if it doesn't exist
if [ $INSTALL_COMMAND -eq 1 ]; then
	if [ ! -f /cache/recovery/command ]; then
		cp -f /sdcard/OpenRecovery/etc/command /cache/recovery/command
		chmod 0644 /cache/recovery/command
	fi
fi

#basic initialization
#===============================================================================

#dirs

mkdir /cdrom
chmod 0755 /cdrom

if [ $NOCUST -eq 0 ]; then
	mkdir /cust
	chmod 0755 /cust
fi

#enviroment override
cp -f /sdcard/OpenRecovery/etc/env /etc/env
chmod 0644 /etc/env

#fstab
cp -f "/sdcard/OpenRecovery/etc/fstab."$1 /etc/fstab
chmod 0644 /etc/fstab

cp -f /sdcard/OpenRecovery/etc/mtab /etc/mtab
chmod 0644 /etc/mtab

#profile
cp -f "/sdcard/OpenRecovery/etc/profile."$1 /etc/profile
chmod 0644 /etc/profile

#our little timezone hack
cp -f /sdcard/OpenRecovery/etc/timezone /etc/timezone
chmod 0644 /etc/timezone

#keyboard layout
cp -f /sdcard/OpenRecovery/etc/keyboard /etc/keyboard
chmod 0644 /etc/keyboard

# Patch fstab
MTDBLOCK_SYSTEM=$(/sbin/cat /proc/mtd | /sbin/grep "system")
MTDBLOCK_SYSTEM=${MTDBLOCK_SYSTEM%%:*}
MTDBLOCK_SYSTEM=${MTDBLOCK_SYSTEM##mtd}
MTDBLOCK_SYSTEM="\/dev\/block\/mtdblock$MTDBLOCK_SYSTEM"

MTDBLOCK_DATA=$(/sbin/cat /proc/mtd | /sbin/grep "userdata")
MTDBLOCK_DATA=${MTDBLOCK_DATA%%:*}
MTDBLOCK_DATA=${MTDBLOCK_DATA##mtd}
MTDBLOCK_DATA="\/dev\/block\/mtdblock$MTDBLOCK_DATA"

MTDBLOCK_CDROM=$(/sbin/cat /proc/mtd | /sbin/grep "cdrom")
MTDBLOCK_CDROM=${MTDBLOCK_CDROM%%:*}
MTDBLOCK_CDROM=${MTDBLOCK_CDROM##mtd}
MTDBLOCK_CDROM="\/dev\/block\/mtdblock$MTDBLOCK_CDROM"

MTDBLOCK_CACHE=$(/sbin/cat /proc/mtd | /sbin/grep "cache")
MTDBLOCK_CACHE=${MTDBLOCK_CACHE%%:*}
MTDBLOCK_CACHE=${MTDBLOCK_CACHE##mtd}
MTDBLOCK_CACHE="\/dev\/block\/mtdblock$MTDBLOCK_CACHE"

sed -i "s/MTDBLOCKSYSTEM/$MTDBLOCK_SYSTEM/g" /etc/fstab
sed -i "s/MTDBLOCKDATA/$MTDBLOCK_DATA/g" /etc/fstab
sed -i "s/MTDBLOCKCDROM/$MTDBLOCK_CDROM/g" /etc/fstab
sed -i "s/MTDBLOCKCACHE/$MTDBLOCK_CACHE/g" /etc/fstab

if [ $NOCUST -eq 0 ]; then
	MTDBLOCK_CUST=$(/sbin/cat /proc/mtd | /sbin/grep "cust")
	MTDBLOCK_CUST=${MTDBLOCK_CUST%%:*}
	MTDBLOCK_CUST=${MTDBLOCK_CUST##mtd}
	MTDBLOCK_CUST="\/dev\/block\/mtdblock$MTDBLOCK_CUST"
	
	sed -i "s/MTDBLOCKCUST/$MTDBLOCK_CUST/g" /etc/fstab
fi

#terminfo
cp -fR /sdcard/OpenRecovery/etc/terminfo/ /etc
chmod -R 0644 /etc/terminfo

#Nandroid
cp -f /sdcard/OpenRecovery/sbin/dump_image-or /sbin/dump_image-or
chmod 0755 /sbin/dump_image-or
ln -s /sbin/dump_image-or /sbin/dump_image
chmod 0755 dump_image

cp -f /sdcard/OpenRecovery/sbin/e2fsck-or /sbin/e2fsck-or
chmod 0755 /sbin/e2fsck-or
ln -s /sbin/e2fsck-or /sbin/e2fsck
chmod 0755 e2fsck

cp -f /sdcard/OpenRecovery/sbin/erase_image-or /sbin/erase_image-or
chmod 0755 /sbin/erase_image-or
ln -s /sbin/erase_image-or /sbin/erase_image
chmod 0755 erase_image

cp -f /sdcard/OpenRecovery/sbin/flash_image-or /sbin/flash_image-or
chmod 0755 /sbin/flash_image-or
ln -s /sbin/flash_image-or /sbin/flash_image
chmod 0755 flash_image

cp -f /sdcard/OpenRecovery/sbin/mkyaffs2image-or /sbin/mkyaffs2image-or
chmod 0755 /sbin/mkyaffs2image-or
ln -s /sbin/mkyaffs2image-or /sbin/mkyaffs2image
chmod 0755 mkyaffs2image

cp -f /sdcard/OpenRecovery/sbin/unyaffs-or /sbin/unyaffs-or
chmod 0755 /sbin/unyaffs-or
ln -s /sbin/unyaffs-or /sbin/unyaffs
chmod 0755 unyaffs

#Updater
cp -f /sdcard/OpenRecovery/sbin/updater-or /sbin/updater-or
chmod 0755 /sbin/updater-or
rm -f /sbin/updater
ln -s /sbin/updater-or /sbin/updater
chmod 0755 /sbin/updater
cp -f /sdcard/OpenRecovery/sbin/script-updater /sbin/script-updater
chmod 0755 /sbin/script-updater

#Interactive menu
cp -f /sdcard/OpenRecovery/sbin/imenu /sbin/imenu
chmod 0755 /sbin/imenu

#adbd
cp -f /sdcard/OpenRecovery/sbin/adbd_bash /sbin/adbd
chmod 0755 /sbin/adbd

#create a bin folder for the scripts
cp -fR /sdcard/OpenRecovery/bin/ /
chmod -R 0755 /bin

#remove self copy
rm /bin/switch.sh

#lib and modules
mkdir /lib
cp -fR /sdcard/OpenRecovery/lib/ /
chmod -R 0644 /lib

#ext2 partition on sdcard
if [ -b /dev/block/mmcblk0p2 ]; then
	mkdir /sddata
	insmod /lib/modules/ext2.ko
	echo "/dev/block/mmcblk0p2          /sddata         ext2            defaults        0 0" >> /etc/fstab
	e2fsck -p /dev/block/mmcblk0p2 
fi

#enable adbd
/bin/adbd_stop.sh
/bin/enable_adb_usbmode.sh
/bin/adbd_start.sh

#res - read the theme first
rm -fR /res

if [ -f /sdcard/OpenRecovery/etc/theme ]; then
	cp -f /sdcard/OpenRecovery/etc/theme /etc/theme
	THEME=`awk 'NR==1' /etc/theme`
	
	if [ -d /sdcard/OpenRecovery/$THEME ]; then
		cp -fR /sdcard/OpenRecovery/$THEME/ /
		mv -f /$THEME /res
	else
		cp -fR /sdcard/OpenRecovery/res.or/ /
		mv -f /res.or /res
	fi
else
	cp -fR /sdcard/OpenRecovery/res.or/ /
	mv -f /res.or /res
fi

chmod -R 0644 /res

#menus
mkdir /menu
chmod 0644 /menu
export MENU_DIR=/menu
cp -fR /sdcard/OpenRecovery/menu/ /

#tags
mkdir /tags
chmod 0644 /tags
cp -fR /sdcard/OpenRecovery/tags/ /

#Launch Open Recovery
#==============================================================================

cp -f "/sdcard/OpenRecovery/sbin/open_rcvr."$1 /sbin/recovery
chmod 0755 /sbin/recovery

RPID=`ps | grep /sbin/recovery | awk '{print $1}'`
echo "kill process $RPID..."
kill -9 $RPID
