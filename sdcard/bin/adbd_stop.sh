#!/sbin/bash

stop adbd

RPID=`ps | grep /sbin/adbd | awk '{print $1}'`
echo "kill process $RPID..."
kill -9 $RPID
