#!/sbin/sh

if [ -f /system/startup/pre/.nooverclock ]; then
	echo "Overclock (Disabled)" > "$MENU_FILE"
	echo "Go Back:menu:.." >> "$MENU_FILE"
	echo "Enable:shell:/app/overclock/enable.sh" >> "$MENU_FILE"
else
	echo "Overclock (Enabled)" > "$MENU_FILE"
	echo "Go Back:menu:.." >> "$MENU_FILE"
	echo "Disable:shell:/app/overclock/disable.sh" >> "$MENU_FILE"
fi
