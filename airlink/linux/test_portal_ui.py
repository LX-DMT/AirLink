#!/usr/bin/env python3
from pathlib import Path
import re

root = Path(__file__).resolve().parent
html = (root.parent / "portal" / "index.html").read_text(encoding="utf-8")
provision = (root / "airlink_provision.c").read_text(encoding="utf-8")
build = (root / "build_linux.sh").read_text(encoding="utf-8")

required = (
    "#F5F7FA", "#FFFFFF", "#2563EB", "#142033", "#667085", "#E4EAF2",
    'class="brand-mark" aria-hidden="true"',
    '<svg viewBox="0 0 32 32" focusable="false">',
    'class="icon-frame"', 'class="icon-detail"',
    'fill="none" stroke="currentColor"',
    'stroke-linecap="round" stroke-linejoin="round"',
    'd="M8.1 11.35V8.2h4.25v3.15"',
    'd="M7 11.35h6.45v3.8a3.23 3.23 0 0 1-6.45 0z"',
    'd="M10.22 18.38v1.15a5.75 5.75 0 0 0 5.75 5.75',
    'd="M13.95 10.2a7.15 7.15 0 0 1 9.8 0"',
    'd="M15.75 13.35a4.55 4.55 0 0 1 6.2 0"',
    'd="M18.02 16.48a1.45 1.45 0 0 1 1.72 0"',
    'class="brand-name">AirLink',
    'class="brand-caption">轻松连接，随时共享',
    "width:30px", "height:30px", "gap:7px",
    'id="networkList"',
    'button.className="wifi-row"', "wifi-row.is-selected",
    'button.setAttribute("aria-selected","false")',
    'selectedButton.setAttribute("aria-selected","true")',
    'selectedButton.setAttribute("aria-selected","false")',
    'id="manualToggle"', "找不到网络？手动输入",
    'id="connectButton"', "正在连接…", "配置已提交",
    "selectedButton.classList.remove", "selectedButton.classList.add",
    'textContent=network.ssid', '$("#password").value=""',
    "viewport-fit=cover", "env(safe-area-inset-top)", "min-height:58px",
)
for marker in required:
    assert marker in html, marker

for forbidden in (
    "#07111d", "#10263d", "#071827", "http://", "https://",
    "innerHTML", "document.write", "eval(", "cdn.", "@import",
    'id="selectedCard"', "selected-card", "updateSelectedCard",
    "已选择网络", "linear-gradient(145deg", "M5 9.3a10",
    "M5.4 18.6 11.1 5.7",
    "M7.4 15.1C10.1 10.8 14.1 9.1 16.8 10.4",
    '<circle cx="6.2" cy="16"', '<circle cx="18" cy="10.8"',
    "<image", 'fill="currentColor"',
    'class="brand-mark" aria-hidden="true">A</div>',
    'class="brand-name">irLink',
):
    assert forbidden not in html, forbidden

assert html.count('class="brand-mark"') == 1
assert html.count('class="brand-name">AirLink') == 1
assert html.count("<svg ") == 1 and html.count("</svg>") == 1
assert html.count('class="icon-frame"') == 1
assert html.count('class="icon-detail"') == 1
assert html.count('button.className="wifi-row"') == 1
assert html.count('selectedButton.classList.add("is-selected")') == 1
assert html.count('selectedButton.setAttribute("aria-selected","true")') == 1
assert "new TextEncoder().encode(selected.ssid).length<=32" in html
assert "password:selected.secured?" in html
assert "open:selected.secured?" in html
assert "document.querySelectorAll(\"button,input\")" in html
assert "airlink_portal_html" in provision
assert "static const char html[]" not in provision
assert "embed_portal.py" in build and "../portal/index.html" in build

select = html[html.index("function selectNetwork("):
              html.index("function buildSignal(")]
assert select.index('$("#password").value=""') < select.index(
    'selectedButton.classList.add("is-selected")')
assert 'setAttribute("aria-selected","false")' in select
assert 'setAttribute("aria-selected","true")' in select

style = html[html.index("<style>"):html.index("</style>")]
brand = style[style.index(".brand-mark{"):style.index(".brand-name{")]
assert "width:32px" in brand and "height:32px" in brand
assert "background:transparent" in brand and "border:1px solid #DBEAFE" not in brand
assert "linear-gradient" not in brand and "box-shadow" not in brand
assert ".brand-mark svg{width:30px;height:30px" in brand
assert ".icon-frame{stroke-width:2.35}" in brand
assert ".icon-detail{stroke-width:2.15}" in brand
assert ".brand{display:flex;align-items:center;gap:7px" in style
assert "padding:calc(12px + env(safe-area-inset-top))" in style
assert "max-width:480px" in style
assert "@media(max-width:350px)" in style
assert "*{box-sizing:border-box}" in style
assert re.search(r"\.wifi-row\{[^}]*min-height:58px", style, re.S)
assert re.search(r"\.wifi-row\.is-selected\{[^}]*border:2px solid", style, re.S)
print("R27.6.6.22 captive portal blue USB-Wi-Fi-link icon tests: PASS")
