#!/sbin/bash

# $1 DIRECTORY
# $2 TYPE

echo "Deleting ${1}..."

if [ "$2" == "OPEN_RCVR" ]; then
	OPEN_RCVR_BKP=1;
else 
	OPEN_RCVR_BKP=0;
fi

if [ $OPEN_RCVR_BKP -eq 1 ]; then
	rm -fr /sdcard/nandroid/openrecovery/$1
else
	rm -fr /sdcard/nandroid/adbrecovery/$1
fi
