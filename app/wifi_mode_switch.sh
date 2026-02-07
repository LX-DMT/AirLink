#!/bin/sh

MODE=$1
SSID=$2
PASS=$3

BOOT_DIR="/boot"
WIFI_STA="$BOOT_DIR/wifi.sta"
WIFI_AP="$BOOT_DIR/wifi.ap"
WIFI_SSID="$BOOT_DIR/wifi.ssid"
WIFI_PASS="$BOOT_DIR/wifi.pass"

case "$MODE" in
    ap)
        echo "Switching to AP mode..."
        rm -f $WIFI_STA
        touch $WIFI_AP
        # Note: We do NOT delete WIFI_SSID and WIFI_PASS here anymore.
        # This allows switching back to STA mode using previous credentials if the user cancels.
        
        /etc/init.d/S30wifi restart
        if [ -x /etc/init.d/S31wifi_config ]; then
            /etc/init.d/S31wifi_config restart
        fi
        ;;
    sta)
        echo "Switching to STA mode..."
        
        rm -f $WIFI_AP
        touch $WIFI_STA

        # If SSID is provided, update credentials. 
        # If NOT provided, try to use existing credentials stored in files.
        if [ ! -z "$SSID" ]; then
            echo "$SSID" > $WIFI_SSID
            echo "$PASS" > $WIFI_PASS
        elif [ -f "$WIFI_SSID" ] && [ -s "$WIFI_SSID" ]; then
            echo "No new SSID provided, using saved configuration..."
        else
            echo "Error: No SSID provided and no saved configuration found."
            exit 1
        fi

        if [ -x /etc/init.d/S31wifi_config ]; then
            /etc/init.d/S31wifi_config stop
        fi
        /etc/init.d/S30wifi restart
        ;;
    *)
        echo "Usage: $0 {ap|sta [ssid] [password]}"
        exit 1
        ;;
esac
