#!/sbin/bash

stop adbd

RPID=`ps | grep /sbin/adbd | awk '{print $1}'`
kill -9 $RPID
