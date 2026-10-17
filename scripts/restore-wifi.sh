#!/bin/sh

PLATFORM=freescale
if [ `dd if=/dev/mmcblk0 bs=512 skip=1024 count=1  2>/dev/null | grep -c "HW CONFIG" ` == 1 ]; then
        CPU=`ntx_hwconfig -s -p /dev/mmcblk0 CPU 2>/dev/null`
        PLATFORM=$CPU-ntx
        WIFI=`ntx_hwconfig -s -p /dev/mmcblk0 Wifi 2>/dev/null`
fi

INTERFACE=wlan0
WIFI_MODULE=ar6000
if [ $PLATFORM != freescale ]; then
        INTERFACE=eth0
        WIFI_MODULE=dhd
        if [ x$WIFI == "xRTL8189" ]; then
            WIFI_MODULE=8189fs
        fi
fi

# check whether PLATFORM & PRODUCT have a value assigned by rcS
if [ -z "${PRODUCT}" ]; then
    # shellcheck disable=SC2046
    export $(grep -s -e '^PRODUCT=' "/proc/$(pidof -s udevd)/environ")
fi

if [ -z "${PRODUCT}" ]; then
    PRODUCT="$(/bin/kobo_config.sh 2>/dev/null)"
    export PRODUCT
fi

# PLATFORM is used in koreader for the path to the Wi-Fi drivers (as well as when restarting nickel)
if [ -z "${PLATFORM}" ]; then
    # shellcheck disable=SC2046
    export $(grep -s -e '^PLATFORM=' "/proc/$(pidof -s udevd)/environ")
fi

if [ -z "${PLATFORM}" ]; then
    PLATFORM="freescale"
    if dd if="/dev/mmcblk0" bs=512 skip=1024 count=1 | grep -q "HW CONFIG"; then
        CPU="$(ntx_hwconfig -s -p /dev/mmcblk0 CPU 2>/dev/null)"
        PLATFORM="${CPU}-ntx"
    fi

    if [ "${PLATFORM}" != "freescale" ] && [ ! -e "/etc/u-boot/${PLATFORM}/u-boot.mmc" ]; then
        PLATFORM="ntx508"
    fi
    export PLATFORM
fi

echo "INTERFACE: ${INTERFACE} WIFI_MODULE: ${WIFI_MODULE} PLATFORM: ${PLATFORM} PRODUCT: ${PRODUCT}"

# NOTE: Close any non-standard fds, so that it doesn't come back to bite us in the ass with USBMS later...
for fd in /proc/"$$"/fd/*; do
    fd_id="$(basename "${fd}")"
    if [ -e "${fd}" ] && [ "${fd_id}" -gt 2 ]; then
        # NOTE: dash (meaning, in turn, busybox's ash, uses fd 10+ open to /dev/tty or $0 (w/ CLOEXEC))
        fd_path="$(readlink -f "${fd}")"
        if [ "${fd_path}" != "/dev/tty" ] && [ "${fd_path}" != "$(readlink -f "${0}")" ] && [ "${fd}" != "${fd_path}" ]; then
            eval "exec ${fd_id}>&-"
            echo "[enable-wifi.sh] Closed fd ${fd_id} -> ${fd_path}"
        fi
    fi
done

# Load wifi modules and enable wifi.
if ! grep -q "sdio_wifi_pwr" "/proc/modules"; then
    if [ -e "/drivers/${PLATFORM}/wifi/sdio_wifi_pwr.ko" ]; then
        # Handle the shitty DVFS switcheroo...
        if [ -n "${CPUFREQ_DVFS}" ]; then
            echo "userspace" >"/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"
            echo "1" >"/sys/devices/platform/mxc_dvfs_core.0/enable"
        fi

        insmod "/drivers/${PLATFORM}/wifi/sdio_wifi_pwr.ko"
    fi
fi
# Moar sleep!
usleep 250000
# NOTE: Used to be exported in WIFI_MODULE_PATH before FW 4.23
if ! grep -q "${WIFI_MODULE}" "/proc/modules"; then
    if [ -e "/drivers/${PLATFORM}/wifi/${WIFI_MODULE}.ko" ]; then
        insmod "/drivers/${PLATFORM}/wifi/${WIFI_MODULE}.ko"
    elif [ -e "/drivers/${PLATFORM}/${WIFI_MODULE}.ko" ]; then
        # NOTE: Modules are unsorted on Mk. 8
        insmod "/drivers/${PLATFORM}/${WIFI_MODULE}.ko"
    fi
fi
# Race-y as hell, don't try to optimize this!
sleep 1

ifconfig "${INTERFACE}" up
[ "${WIFI_MODULE}" = "dhd" ] && wlarm_le -i "${INTERFACE}" up

pkill -0 wpa_supplicant ||
    env -u LD_LIBRARY_PATH \
        wpa_supplicant -D wext -s -i "${INTERFACE}" -c /etc/wpa_supplicant/wpa_supplicant.conf -O /var/run/wpa_supplicant -B


# release ip


# Release IP and shutdown udhcpc.
# NOTE: Save our resolv.conf to avoid ending up with an empty one, in case the DHCP client wipes it on release (#6424).
cp -a "/etc/resolv.conf" "/tmp/resolv.ko"
old_hash="$(md5sum "/etc/resolv.conf" | cut -f1 -d' ')"

if [ -x "/sbin/dhcpcd" ]; then
    env -u LD_LIBRARY_PATH dhcpcd -d -k "${INTERFACE}"
    killall -q -TERM udhcpc default.script
else
    killall -q -TERM udhcpc default.script dhcpcd
    ifconfig "${INTERFACE}" 0.0.0.0
fi

# NOTE: dhcpcd -k waits for the signalled process to die, but busybox's killall doesn't have a -w, --wait flag,
#       so we have to wait for udhcpc to die ourselves...
# NOTE: But if all is well, there *isn't* any udhcpc process or script left to begin with...
kill_timeout=0
while pkill -0 udhcpc; do
    # Stop waiting after 5s
    if [ ${kill_timeout} -ge 20 ]; then
        break
    fi
    usleep 250000
    kill_timeout=$((kill_timeout + 1))
done

new_hash="$(md5sum "/etc/resolv.conf" | cut -f1 -d' ')"
# Restore our network-specific resolv.conf if the DHCP client wiped it when releasing the lease...
if [ "${new_hash}" != "${old_hash}" ]; then
    mv -f "/tmp/resolv.ko" "/etc/resolv.conf"
else
    rm -f "/tmp/resolv.ko"
fi


# obtain ip

# NOTE: Prefer dhcpcd over udhcpc if available. That's what Nickel uses,
#       and udhcpc appears to trip some insanely wonky corner cases on current FW (#6421)
if [ -x "/sbin/dhcpcd" ]; then
    env -u LD_LIBRARY_PATH dhcpcd -d -t 30 -w "${INTERFACE}"
else
    env -u LD_LIBRARY_PATH udhcpc -S -i "${INTERFACE}" -s /etc/udhcpc.d/default.script -b -q
fi
