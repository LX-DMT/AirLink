#!/usr/bin/env python3
from pathlib import Path

src = (Path(__file__).resolve().parent / "airlinkd.c").read_text()
required = [
    '#define AIRLINKD_VERSION "R27.6.6.22"',
    '#define VH_TCP_PORT 7575U',
    '#define VH_NETWORK_SETTLE_MS 2000ULL',
    '#define VH_LAN_ACTIVATE_MAINTENANCE_MS 30000ULL',
    '0U, 1000U, 2000U, 4000U, 8000U, 16000U, 30000U',
    'socket(AF_PACKET, SOCK_RAW | SOCK_CLOEXEC, htons(ETH_P_ARP))',
    'build_gratuitous_arp(&frame, mac, ipv4, ARPOP_REQUEST)',
    'build_gratuitous_arp(&frame, mac, ipv4, ARPOP_REPLY)',
    'socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0)',
    'SO_BROADCAST',
    'SO_BINDTODEVICE',
    'SIOCGIFNETMASK',
    'AIRLINK-VH-READY',
    'VH early-listener=DISABLED wait=ASSOCIATED+IP+ROUTE+2000MS',
    'VirtualHere listener-ready pid=%ld ip=%s port=%u ',
    'VH lan-activate begin ipv4=%s schedule_ms=0,1000,2000,',
    'VH lan-activate step=%u phase=%s ipv4=%s garp=%u/2 ',
    'udp_broadcast=%u/1 client=WAIT',
    'VH lan-activate client=CONNECTED ipv4=%s attempts=%u ',
    '#define VH_CONFIG_PATH "/run/airlink/vhusbd.ini"',
    '#define VH_LOG_PATH "/tmp/vhusbd.log"',
    'ServerName=AirLink',
    '(char *)"-c", (char *)VH_CONFIG_PATH',
    '(char *)"-r", (char *)VH_LOG_PATH',
    'tcp_port_state_file("/proc/net/tcp"',
    'tcp_port_state_file("/proc/net/tcp6"',
    'virtualhere_client_endpoint',
    'apply_virtualhere_client_sample',
    '#define VH_CLIENT_CONNECT_STABLE_MS 500ULL',
    '#define VH_CLIENT_DISCONNECT_STABLE_MS 1000ULL',
    'VirtualHere stale process without TCP listener; restarting',
    'VirtualHere start FAIL reason=%s process=%u listener=%u',
    'default_route_is_up(WLAN_IF)',
    'VH network-ready ipv4=%s route=YES settle_ms=%u',
    'VH start after-network-settle elapsed_ms=%llu',
    'VH retry=%u reason=START_FAIL',
]
for marker in required:
    assert marker in src, marker

for obsolete in [
    'send_virtualhere_mdns_announcement',
    'dns_name_append',
    'VirtualHere USB Sharing._vhusb._tcp.local',
    '224.0.0.251',
    'post_listener_announce',
    'announce_virtualhere_listener',
    'VH post-ip-garp',
    'VH_NEIGHBOR_BURSTS',
    'VH_NEIGHBOR_INTERVAL_US',
]:
    assert obsolete not in src, obsolete

assert 'NetworkInterface=' not in src
assert 'TCPPort=%u' not in src
assert 'ip_address=' not in src

begin = src[src.index("static void begin_wireless"):
            src.index("static void request_mode")]
assert begin.index("start_wifi_sta_path(ctx, WPA_RUN_CONF)") < begin.index(
    "VH early-listener=DISABLED")
assert "start_virtualhere(ctx)" not in begin
assert "link_is_up(&ignored)" not in begin

service = src[src.index("static void service_wireless"):
              src.index("static void publish_ui")]
assert service.index("linked = link_is_up(&net)") < service.index(
    "default_route_is_up(WLAN_IF)")
assert service.index("default_route_is_up(WLAN_IF)") < service.index(
    "virtualhere_network_settled")
assert service.index("virtualhere_network_settled") < service.index(
    "start_virtualhere(ctx)")
assert service.index("start_virtualhere(ctx)") < service.index(
    "begin_virtualhere_lan_activation(ctx, &net, now)")
assert service.index("begin_virtualhere_lan_activation(ctx, &net, now)") < service.index(
    "service_virtualhere_lan_activation(ctx, &net, now)")
assert service.index("commit_candidate_config()") < service.index(
    "start_virtualhere(ctx)")
assert "ctx->vh_retry_only &&" in service
assert "now < ctx->retry_deadline" in service

activation = src[src.index("static void begin_virtualhere_lan_activation"):
                 src.index("static bool stop_virtualhere")]
assert "usleep(" not in activation
assert "virtualhere_client_connected()" in activation
assert "send_gratuitous_arp(WLAN_IF, net->ipv4)" in activation
assert "send_lan_activation_broadcast(WLAN_IF, net->ipv4)" in activation
assert "now + VH_LAN_ACTIVATE_MAINTENANCE_MS" in activation

starter = src[src.index("static bool start_virtualhere"):
              src.index("static void reset_network_process_state")]
assert "begin_virtualhere_lan_activation" not in starter
spawn = starter.index("spawn_process(VH_PATH, argv)")
listener_check = starter.index("if (virtualhere_ready())", spawn)
assert spawn < listener_check < starter.index(
    "VirtualHere listener-ready pid=%ld")

old_ui = '''if (process_running("vhusbdriscv64"))
        next.flags |= AIRLINK_UI_STATUS_VIRTUALHERE_RUNNING;'''
new_ui = '''if (vh_state >= AIRLINK_VIRTUALHERE_LISTENING)
        next.flags |= AIRLINK_UI_STATUS_VIRTUALHERE_RUNNING;'''
assert old_ui not in src
assert new_ui in src
assert "next.virtualhere_state = vh_state;" in src

print("PASS: VirtualHere never starts before association, IPv4 and route settle")
print("PASS: listener activation uses non-blocking staged GARP plus UDP broadcast")
print("PASS: activation stops after a real TCP client connection")
print("PASS: 30-second maintenance activation covers late client startup")
print("PASS: process/listener readiness, retry lockout and UI truthfulness retained")
