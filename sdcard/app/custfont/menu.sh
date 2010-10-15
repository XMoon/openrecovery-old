#!/sbin/sh

if [ -f /system/startup/post/.nocustomfont ]; then
	echo "Custom Font (Disabled)" > "$MENU_FILE"
	echo "Go Back:menu:.." >> "$MENU_FILE"
	echo "Enable:shell:/app/custfont/enable.sh" >> "$MENU_FILE"
else
	echo "Custom Font (Enabled)" > "$MENU_FILE"
	echo "Go Back:menu:.." >> "$MENU_FILE"
	echo "Disable:shell:/app/custfont/disable.sh" >> "$MENU_FILE"
fi
