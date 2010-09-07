chmod -R 750 open_recovery-ramdisk/sbin
chmod -R 644 open_recovery-ramdisk/menu
chown -R 0:0 open_recovery-ramdisk

./repack-bootimg open_recovery-kernel open_recovery-ramdisk open_recovery_lite.img
