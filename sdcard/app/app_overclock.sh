#!/sbin/sh

#create the menu item
if [ -d /system/startup/pre ]
then
	echo "Overclock:scripted_menu:overclock.menu:/app/overclock/menu.sh" >> "$APP_MENU_FILE"
fi
