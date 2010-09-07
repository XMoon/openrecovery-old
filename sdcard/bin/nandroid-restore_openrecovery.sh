#!/sbin/bash

# $1 DIRECTORY
# $2 TYPE
# $3 --all

if [ "$2" == "OPEN_RCVR" ]; then
	OPEN_RCVR_BKP=1;
else 
	OPEN_RCVR_BKP=0;
fi

if [ $OPEN_RCVR_BKP -eq 1 ]; then
	RESTOREPATH="/sdcard/nandroid/openrecovery/$1"
else
	RESTOREPATH="/sdcard/nandroid/adbrecovery/$1"
fi

REBOOT=0
NOTHING=1

REST_BOOT=0
REST_BPSW=0
REST_LBL=0
REST_LOGO=0

REST_SYSTEM=0
REST_DATA=0
REST_CACHE=0
REST_CDROM=0
REST_CUST=0

REST_EXT2=0

TAGPREFIX="/tags/."

#amount of space remaining
FREEBLOCKS="`df -k /sdcard| grep sdcard | awk '{ print $4 }'`"

echo "+----------------------------------------------+"
echo "+                                              +"
echo "+        Open Recovery Nandroid Restore        +"
echo "+                                              +"
echo "+----------------------------------------------+"
sleep 2

#===============================================================================
# Parse the arguments
#===============================================================================

if [ -f "$TAGPREFIX"nand_rest_autoreboot ]; then
	REBOOT=1
fi

if [ "$3" == "--all" ]; then
	REST_BOOT=1
	REST_BPSW=1
	REST_LBL=1
	REST_LOGO=1
	REST_SYSTEM=1
	REST_DATA=1
	REST_CACHE=1
	REST_CDROM=1
	REST_CUST=1
	REST_EXT2=1
	NOTHING=0
else

	if [ -f "$TAGPREFIX"nand_rest_system ]; then
		REST_SYSTEM=1
		NOTHING=0
	fi
	
	if [ -f "$TAGPREFIX"nand_rest_data ]; then
		REST_DATA=1
		NOTHING=0
	fi
	
	if [ -f "$TAGPREFIX"nand_rest_boot ]; then
		REST_BOOT=1
		NOTHING=0
	fi
	
	if [ -f "$TAGPREFIX"nand_rest_cache ]; then
		REST_CACHE=1
		NOTHING=0
	fi
	
	if [ -f "$TAGPREFIX"nand_rest_bpsw ]; then
		REST_BPSW=1
		NOTHING=0
	fi
	
	if [ -f "$TAGPREFIX"nand_rest_lbl ]; then
		REST_LBL=1
		NOTHING=0
	fi
	
	if [ -f "$TAGPREFIX"nand_rest_logo ]; then
		REST_LOGO=1
		NOTHING=0
	fi
	
	if [ -f "$TAGPREFIX"nand_rest_cdrom ]; then
		REST_CDROM=1
		NOTHING=0
	fi
	
	if [ -f "$TAGPREFIX"nand_rest_cust ]; then
		REST_CUST=1
		NOTHING=0
	fi
	
	if [ -f "$TAGPREFIX"nand_rest_ext2 ]; then
		REST_EXT2=1
		NOTHING=0
	fi
fi

if [ $NOTHING -eq 1 ]; then
	echo "E:There is nothing to restore."
	exit 1
fi

#===============================================================================
# Prepare the restoration
#===============================================================================

#check availbility of the utilities
erase_image=`which erase_image`
if [ "$erase_image" == "" ]; then
	erase_image=`which erase_image-or`
	if [ "$erase_image" == "" ]; then
		echo "E:erase_image or erase_image-or not found in path."
		exit 1
	fi
fi

flash_image=`which flash_image`
if [ "$flash_image" == "" ]; then
	flash_image=`which flash_image-or`
	if [ "$flash_image" == "" ]; then
		echo "E:flash_image or flash_image-or not found in path."
		exit 1
	fi
fi

unyaffs=`which unyaffs`
if [ "$unyaffs" == "" ]; then
	unyaffs=`which unyaffs-or`
	if [ "$unyaffs" == "" ]; then
		echo "E:unyaffs or unyaffs-or not found in path."
		exit 1
	fi
fi


#check battery
if [ "$COMPRESS" == 1 ]; then
	ENERGY=`cat /sys/class/power_supply/battery/capacity`
	if [ "`cat /sys/class/power_supply/battery/status`" == "Charging" ]; then
		ENERGY=100
	fi
	if [ ! $ENERGY -ge 30 ]; then
		echo "E:Not enough battery power to perform compression."      
		exit 1
	fi
fi

#===============================================================================
# Check compression
#===============================================================================

COMPRESSED=0

CWD=$PWD
cd $RESTOREPATH

if [ `ls *.bz2 2>/dev/null|wc -l` -ge 1 ]; then
	echo "This backup is compressed."
	COMPRESSED=1
	
	if [ $FREEBLOCKS -le 262144 ]; then
		echo "E:Not enough free space available on sdcard for decompression operation (need 256 MiB)."
		cd $CWD
		exit 1
	else
		echo "There is at least 256 MiB on the sdcard."
	fi
fi

#===============================================================================
# If old format, verify checksums now
#===============================================================================

if [ $OPEN_RCVR_BKP -eq 0 ]; then
	if [ ! -f nandroid.md5 ]; then
		echo "E:Failed to find the checksum file."
		exit 1
	fi 
	
	echo "Verifying MD5..."
	
	md5sum -c nandroid.md5
	if [ $? -eq 1 ]; then
		echo "E:Failed verifying checksums."
		exit 1
	fi	
fi

#===============================================================================
# Restore the non-filesystem partitions
#===============================================================================

for image in boot bpsw lbl logo; do
	if [ ! -f $image.img* ]; then
		echo "Partition $image not backed up, skipping."
		continue
	fi
	
	case $image in
		boot)
			if [ $REST_BOOT -eq 0 ]; then
				echo "Skipping partition boot."
				continue
			fi
			;;
			
		bpsw)
			if [ $REST_BPSW -eq 0 ]; then
				echo "Skipping partition bpsw."
				continue
			fi
			;;
			
		lbl)
			if [ $REST_LBL -eq 0 ]; then
				echo "Skipping partition lbl."
				continue
			fi
			;;
			
		logo)
			if [ $REST_LOGO -eq 0 ]; then
				echo "Skipping partition logo."
				continue
			fi
			;;
	esac
	
	if [ $OPEN_RCVR_BKP -eq 1 ]; then  
	
		if [ $COMPRESSED -eq 1 ]; then
			echo -n "Decompressing $image..."
			bunzip2 -c $image.img.bz2 > $image.img
			echo "done"
		fi
		
		if [ ! -f $image.md5 ]; then
			echo "Partition $image checksum file missing, skipping."
			
			if [ $COMPRESSED -eq 1 ]; then
				#delete the uncompressed part
				rm $image.img
			fi
			
			continue
		fi
		
		echo -n "Verifying MD5..."  	
		md5sum -c $image.md5 > /dev/null
		
		if [ $? -eq 1 ]; then
			echo "failed"
			echo "Partition $image checksum mismatch, skipping."
			
			if [ $COMPRESSED -eq 1 ]; then
				#delete the uncompressed part
				rm $image.img
			fi
			
			continue
		fi
		
		echo "done"
	fi
	
	echo -n "Restoring $image..."
	$flash_image $image $image.img > /dev/null 2> /dev/null
	echo "done"
	
	if [ $COMPRESSED -eq 1 ]; then
		#delete the uncompressed part
		rm $image.img
	fi
	
done

#===============================================================================
# Restore the yaffs2 filesystem partitions
#===============================================================================

for image in system data cache cust cdrom; do
	if [ ! -f $image.img* ]; then
		echo "Partition $image not backed up, skipping."
		continue
	fi
	
	case $image in
		system)
			if [ $REST_SYSTEM -eq 0 ]; then
				echo "Skipping partition system."
				continue
			fi
		  ;;
	    
		data)
			if [ $REST_DATA -eq 0 ]; then
				echo "Skipping partition data."
				continue
			fi
			;;
	    
		cache)
			if [ $REST_CACHE -eq 0 ]; then
				echo "Skipping partition cache."
				continue
			fi
			;;
	
		cust)
			if [ $REST_CUST -eq 0 ]; then
				echo "Skipping partition cust."
				continue
			fi
			;;
	
		cdrom)
			if [ $REST_CDROM -eq 0 ]; then
				echo "Skipping partition cdrom."
				continue
			fi
			;;
	esac
	
	umount /$image 2> /dev/null
	mount /$image || FAIL=1
	
	if [ $FAIL -eq 1 ]; then
		echo "E:Cannot mount properly /$image."
		echo "Not restoring $image."
		continue
	fi
	
	if [ $OPEN_RCVR_BKP -eq 1 ]; then
	
		if [ $COMPRESSED -eq 1 ]; then
			echo -n "Decompressing $image..."
			bunzip2 -c $image.img.bz2 > $image.img
			echo "done"
		fi
		
		if [ ! -f $image.md5 ]; then
			echo "Partition $image checksum file missing, skipping."		
			
			if [ $COMPRESSED -eq 1 ]; then
				#delete the uncompressed part
				rm $image.img
			fi
			
			continue
		fi
		
		echo -n "Verifying MD5..."
		md5sum -c $image.md5 > /dev/null
		
		if [ $? -eq 1 ]; then
			echo "failed"
			echo "Partition $image checksum mismatch, skipping."
			
			if [ $COMPRESSED -eq 1 ]; then
				#delete the uncompressed part
				rm $image.img
			fi
			
			continue
		fi
		echo "done"
	fi
	
	umount /$image 2> /dev/null
	echo -n "Erasing $image..."
	$erase_image $image > /dev/null 2> /dev/null
	echo "done"
	mount /$image
	echo -n "Restoring $image..."
	$unyaffs $image.img /$image	> /dev/null 2> /dev/null
	echo "done"
	
	if [ $COMPRESSED -eq 1 ]; then
		#delete the uncompressed part
		rm $image.img
	fi
	
done

#===============================================================================
# EXT2
#===============================================================================

if [ ! -f ext2.tar ]; then
	echo "Partition ext2 not backed up, skipping."
else 
	if [ $REST_EXT2 -eq 0 ]; then
		echo "Skipping partition ext2."
	else	
		if [ ! -f ext2.md5 ]; then
			echo "Partition ext2 checksum file missing, skipping."		
			
			if [ $COMPRESSED -eq 1 ]; then
				#delete the uncompressed part
				rm ext2.tar
			fi
			
		else	
			echo -n "Verifying MD5..."
			md5sum -c ext2.md5 > /dev/null
			
			if [ $? -eq 1 ]; then
				echo "failed"
				echo "Partition ext2 checksum mismatch, skipping."
				
				if [ $COMPRESSED -eq 1 ]; then
					#delete the uncompressed part
					rm ext2.tar
				fi
				
			else
				echo "done"
				echo -n "Erasing ext2..."
				umount /sddata 2> /dev/null
				mkfs.ext2 -c /dev/block/mmcblk0p2 > /dev/null
				echo "done"
				
				echo -n "Restoring ext2..."
				mount /sddata
				CW2=$PWD
				cd /sddata
				tar -xvf $RESTOREPATH/ext2.tar ./ > /dev/null
				cd $CW2
				echo "done"
				
				if [ $COMPRESSED -eq 1 ]; then
					#delete the uncompressed part
					rm ext2.tar
				fi
			fi
		fi
	fi
fi

#===============================================================================
# Exit
#===============================================================================

cd $CWD
echo "Restoring finished."

if [ $REBOOT -eq 1 ]; then
	reboot
fi
