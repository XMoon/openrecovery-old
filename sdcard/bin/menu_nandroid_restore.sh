#!/sbin/bash

echo "Nandroid Restore" > "$MENU_FILE"
echo "Go Back:menu:.." >> "$MENU_FILE"

if [ -d /sdcard/nandroid/adbrecovery ]
then
 cd /sdcard/nandroid/adbrecovery
 for dirs in $( ls -c . )
 do
  echo "$dirs:scripted_menu:nandroid_restore_confirm.menu:menu_nandroid_restore_confirm.sh $dirs ADB_RCVR" >> "$MENU_FILE"
 done
fi

if [ -d /sdcard/nandroid/openrecovery ]
then
 cd /sdcard/nandroid/openrecovery
 for dirs in $( ls -c . )
 do
  echo "$dirs:scripted_menu:nandroid_restore_confirm.menu:menu_nandroid_restore_confirm.sh $dirs OPEN_RCVR" >> "$MENU_FILE"
 done
fi

