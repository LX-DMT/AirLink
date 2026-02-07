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
        
        # Move STA credentials to a backup location so AP mode doesn't use them
        [ -f "$WIFI_SSID" ] && mv "$WIFI_SSID" "${WIFI_SSID}.bak"
        [ -f "$WIFI_PASS" ] && mv "$WIFI_PASS" "${WIFI_PASS}.bak"
        
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
        # If NOT provided, try to restore from backup or use existing.
        if [ ! -z "$SSID" ]; then
            echo "$SSID" > $WIFI_SSID
            echo "$PASS" > $WIFI_PASS
            rm -f "${WIFI_SSID}.bak" "${WIFI_PASS}.bak"
        elif [ -f "${WIFI_SSID}.bak" ]; then
            mv "${WIFI_SSID}.bak" "$WIFI_SSID"
            mv "${WIFI_PASS}.bak" "$WIFI_PASS"
            echo "Restored saved configuration from backup..."
        elif [ -f "$WIFI_SSID" ] && [ -s "$WIFI_SSID" ]; then
            echo "Using existing saved configuration..."
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
