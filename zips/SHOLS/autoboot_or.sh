#!/system/bin/sh

if [ -f /cache/recovery/command ]; then
	
	#trick the whatever deleting the command
	#THIS WILL PREVENT AUTOMATIC OTA UPDATE!
	
	if [ ! -d /cache/recovery_stock ]; then
		mkdir /cache/recovery_stock
	fi
	
	mount -o bind /cache/recovery_stock /cache/recovery
	
	#keep it read only, so it fails to delete it
	mount -o remount,ro /cache/recovery	
	
fi

