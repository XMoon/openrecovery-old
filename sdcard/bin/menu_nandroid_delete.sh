#!/sbin/bash

echo "Nandroid Delete" > "$MENU_FILE"
echo "Go Back:menu:.." >> "$MENU_FILE"

if [ -d /sdcard/nandroid/adbrecovery ]
then
 cd /sdcard/nandroid/adbrecovery
 for dirs in $( ls -c . )
 do
  echo "$dirs:shell:nandroid-delete_openrecovery.sh $dirs ADB_RCVR" >> "$MENU_FILE"
 done
fi

if [ -d /sdcard/nandroid/openrecovery ]
then
 cd /sdcard/nandroid/openrecovery
 for dirs in $( ls -c . )
 do
  echo "$dirs:shell:nandroid-delete_openrecovery.sh $dirs OPEN_RCVR" >> "$MENU_FILE"
 done
fi
