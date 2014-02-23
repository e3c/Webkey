#!/system/bin/sh
su -c killall webkey
sleep 2
su -c /data/data/com.webkey/files/webkey $1&
