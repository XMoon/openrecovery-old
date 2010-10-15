#!/sbin/sh

#create the menu item
if [ -d /system/startup/post ]
then
	echo "Custom Font:scripted_menu:custfont.menu:/app/custfont/menu.sh" >> "$APP_MENU_FILE"
fi
