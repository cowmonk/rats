#!/bin/sh
# EXAMPLE CONFIGURATION
# rc.boot: early system bring-up, run by origo before serva starts.
# origo already mounts: /proc /sys /dev /dev/pts

# hostname
hostname mybox

# optional filesystems — uncomment as needed
#mount -t tmpfs tmpfs /run
#mount -t tmpfs tmpfs /tmp

# /etc/fstab
mount -a

# some distros might need this for applications like steam
#ln -sf /proc/self/fd /dev/fd

# ensure /run exists for serva.sock
mkdir -p /run

# load keymap, set console font, etc.
#loadkeys us
#setfont Lat2-Terminus16

# network — adjust to taste
#ip link set lo up
#ip link set eth0 up
#sdhcp -i eth0

# smdev/mdev
#smdev -s
#echo /bin/smdev > /proc/sys/kernel/hotplug

# source local customisation
[ -x /etc/rc.local ] && . /etc/rc.local
