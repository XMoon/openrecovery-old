#!/sbin/sh

echo "Keyboard Layout Settings" > "$MENU_FILE"
echo "Go Back:menu:.." >> "$MENU_FILE"

if [ -d /sdcard/OpenRecovery/keychars ]
then
 cd /sdcard/OpenRecovery/keychars
 for files in $( ls -c . )
 do
  echo "$files:shell:change_keyboard_layout.sh $files" >> "$MENU_FILE"
 done
fi
