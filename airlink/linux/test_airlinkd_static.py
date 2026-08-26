#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parent
daemon = (root / "airlinkd.c").read_text()
provision = (root / "airlink_provision.c").read_text()
header = (root.parent / "ipc" / "airlink_ipc_v4.h").read_text()
init = (root / "S29airlinkd").read_text()
pheader = (root / "airlink_provision.h").read_text()

begin = daemon[daemon.index("static void begin_wireless"):
               daemon.index("static void request_mode")]
service = daemon[daemon.index("static void service_wireless"):
                 daemon.index("static void publish_ui")]
checks = {
    "ABI4": "ABI_REVISION 4U" in daemon,
    "R27.6.6.22 version": '#define AIRLINKD_VERSION "R27.6.6.22"' in daemon,
    "R27.6.6.22 IDs": "0x37324e4cU" in daemon and "0x50373252U" in daemon,
    "IPC stage diagnostics": all(x in daemon for x in
                                 ("IPC FAIL stage=HEADER",
                                  "IPC FAIL stage=CSTATE_FW",
                                  "IPC FAIL stage=READY",
                                  "IPC FAIL stage=PING",
                                  "IPC peer=R27P")),
    "no stale errno diagnostic": 'START FAIL ipc=%s' not in daemon and
                                 "ipc_fail_errno" in daemon,
    "provision layout": "AIRLINK_IPC_PROVISION_STATUS_OFFSET 0x300U" in header
                        and "AIRLINK_IPC_LAYOUT_SIZE          0x380U" in header,
    "provision feature": "AIRLINK_IPC_FEATURE_WIFI_PROVISION" in header,
    "boot sync reads stable mode": "ctx.cstate.stable_level" in daemon,
    "wired stops provisioning": "airlink_provision_stop(&ctx->provision)" in daemon,
    "wired wlan down": 'up ? "up" : "down"' in daemon,
    "driver not unloaded": all(x not in daemon for x in ("rmmod", "modprobe -r")),
    "formal config only": '"/data/airlink/wifi.conf"' in daemon,
    "no boot credential fallback": all(x not in daemon for x in
                                       ('"/boot/wifi.sta"', '"/boot/wifi.ssid"',
                                        '"/boot/wifi.pass"',
                                        '"/etc/wpa_supplicant.conf"')),
    "candidate path": '"/run/airlink/wifi.candidate.conf"' in pheader,
    "mode services stopped before STA": begin.index("stop_mode_services(ctx)") <
                                        begin.index("start_wifi_sta_path"),
    "batch TERM": "stop_matching_many" in daemon and
                  "kill(pids[i], SIGTERM)" in daemon,
    "batch shared deadline": "deadline = monotonic_ms() + timeout_ms;" in daemon,
    "batch KILL": "kill(pids[i], SIGKILL)" in daemon,
    "mode batch includes VH": '"vhusbdriscv64", "wifi_config_web.py"' in daemon,
    "CH347 keeps dedicated VH stop": "stop_virtualhere(ctx)" in daemon,
    "mode timing log": all(x in daemon for x in
                           ("MODE timing stop_services_ms=",
                            "network_prepare_ms=%llu", "total_ms=%llu")),
    "candidate commit after routed link":
        service.index("if (linked && routed)") <
        service.index("commit_candidate_config()"),
    "AP-to-STA datapath reset only for provisioning":
        "#define AP_TO_STA_DOWN_MS 300U" in daemon and
        "#define AP_TO_STA_UP_SETTLE_MS 150U" in daemon and
        "if (!ctx->provisioning_sta_test)" in daemon and
        "WIFI AP-to-STA datapath-reset down_ms=%u up_settle_ms=%u" in daemon and
        daemon.index("wlan_command(false)",
                     daemon.index("reset_ap_to_sta_datapath")) <
        daemon.index("wlan_command(true)",
                     daemon.index("reset_ap_to_sta_datapath")) <
        daemon.index("spawn_process(wpa_argv[0]", daemon.index("start_wifi_sta_path")),
    "real IPv4 source": "Ignore wpa_cli ip_address" in daemon and
                        "if (run_capture(ip_argv" in daemon,
    "default route required": "default_route_is_up(WLAN_IF)" in service,
    "VH network settle": "#define VH_NETWORK_SETTLE_MS 2000ULL" in daemon and
                         "virtualhere_network_settled" in service,
    "VH after candidate commit": service.index("commit_candidate_config()") <
                                 service.index("start_virtualhere(ctx)"),
    "VH no pre-IP startup": "start_virtualhere(ctx)" not in begin and
                             "VH early-listener=DISABLED" in begin,
    "LAN activation after stable IP":
        service.index("virtualhere_network_settled") <
        service.index("start_virtualhere(ctx)") <
        service.index("begin_virtualhere_lan_activation(ctx, &net, now)") and
        "service_virtualhere_lan_activation" in service and
        "ARPOP_REQUEST" in daemon and "ARPOP_REPLY" in daemon and
        "SO_BROADCAST" in daemon and "SO_BINDTODEVICE" in daemon,
    "LAN activation schedule": all(x in daemon for x in
                                    ("0U, 1000U, 2000U, 4000U, 8000U, 16000U, 30000U",
                                     "VH_LAN_ACTIVATE_MAINTENANCE_MS 30000ULL",
                                     "virtualhere_client_connected",
                                     "client=CONNECTED",
                                     "udp_broadcast=%u/1")),
    "VirtualHere ABI4 three-state": all(x in daemon + header for x in
        ("AIRLINK_VIRTUALHERE_STOPPED", "AIRLINK_VIRTUALHERE_LISTENING",
         "AIRLINK_VIRTUALHERE_CLIENT_CONNECTED", "next.virtualhere_state = vh_state")),
    "VirtualHere debounce": all(x in daemon for x in
        ("VH_CLIENT_CONNECT_STABLE_MS 500ULL",
         "VH_CLIENT_DISCONNECT_STABLE_MS 1000ULL",
         "apply_virtualhere_client_sample",
         "VH client-state=CONNECTED", "VH client-state=DISCONNECTED")),
    "Wi-Fi power save off": all(x in daemon for x in
        ("set_wifi_power_save_off", '"set", "power_save", "off"',
         'set_wifi_power_save_off(ctx, "STA_START")',
         'set_wifi_power_save_off(ctx, "AP_TO_STA")',
         'service_wifi_power_save_off(ctx, now, "LINK_READY")')),
    "Wi-Fi power save retry throttled": all(x in daemon for x in
        ("WIFI_POWER_SAVE_RETRY_MS 2000ULL",
         "service_wifi_power_save_off",
         "now >= ctx->wifi_power_save_retry_deadline")) and
        "ctx->wifi_power_save_link_verified =\n                    set_wifi_power_save_off" not in daemon,
    "status network diagnostics": all(x in daemon for x in
        ("bssid", "default_route", "power_save", "requested_hz",
         "actual_hz", "client_connected", "client_ip", "network_hint",
         "check-pc-network-or-ap-isolation",
         "/sys/kernel/debug/mmc1/ios")),
    "diagnostic export safe network detail": all(x in daemon for x in
        ("wlan0 counters", "ARP neighbors", "TCP IPv4", "TCP IPv6",
         "diag_capture_section", "diag_file_section")),
    "no incomplete custom mDNS": all(x not in daemon for x in
                                      ("send_virtualhere_mdns_announcement",
                                       "dns_name_append",
                                       "224.0.0.251")),
    "VH retry stays wait-link": "AIRLINK_SYSTEM_WIRELESS_WAIT_LINK" in
                                 daemon[daemon.index("static void note_vh_failure"):
                                        daemon.index("static void service_wireless")],
    "failed candidate restores AP": "restore_provision_ap" in service and "airlink_provision_restart_ap" in daemon,
    "manual timeout": "airlink_provision_take_timeout" in service,
    "async scan": "scan_pid" in pheader and "scan_start" in provision and "WNOHANG" in provision,
    "scan security from iw privacy": all(x in provision for x in
        ("scan_parse_security", "\"capability:\"", "\"Privacy\"",
         "\"RSN:\"", "\"WPA:\"", "\"WEP:\"")),
    "unknown scan security is safe": "if (!network->security_known)" in provision and
                                     "network->secured = 1U;" in provision and
                                     "security_known" in pheader,
    "duplicate SSID preserves security": "stored->secured || network->secured" in provision,
    "HTTP limits": "AIRLINK_PROVISION_MAX_CLIENTS 4" in pheader
                   and "AIRLINK_PROVISION_BODY_MAX" in provision,
    "captive redirect": 'Location: http://' in provision and 'APIP' in provision,
    "hostapd WPA2 CCMP": "wpa=2" in provision and
                         "rsn_pairwise=CCMP" in provision,
    "dnsmasq wildcard DNS": "address=/#/" in provision,
    "channel 1": "channel=1" in provision,
    "fixed AP password": '#define FIXED_AP_PASSWORD "12345678"' in provision,
    "external portal include": '#include "airlink_portal.inc"' in provision
                               and "static const char html[]" not in provision,
    "AP password migration": "ap_credentials_match" in provision and
                             "chmod(APCONF, 0600) == 0" in provision,
    "no random AP password": "/dev/urandom" not in provision and
                             "RANDOMPASS" in provision,
    "atomic config": "fsync" in daemon and "rename(temp" in daemon,
    "management commands": all(x in daemon for x in
                               ("wifi provision", "wifi cancel", "wifi forget")),
    "no shell": all(x not in daemon + provision for x in ("system(", "popen(")),
    "no target password IPC": "target_password" not in header,
    "no password logging": all(x not in daemon for x in
                               ('log_msg("password', 'wifi.pass')),
    "init managed cleanup": "cleanup_stale" in init and
                            "/usr/sbin/hostapd" in init and
                            "/usr/sbin/dnsmasq" in init,
}
failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit("R27.6.6.22 static test FAIL: " + ", ".join(failed))
print("R27.6.6.22 airlinkd IPC/provisioning fixed-AP static tests: PASS")
