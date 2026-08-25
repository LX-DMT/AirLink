#!/usr/bin/env python3
import json
from pathlib import Path

root = Path(__file__).resolve().parent
meta = json.loads((root / "airlink_fonts.json").read_text(encoding="utf-8"))
chars = set((root / "airlink_fonts.chars.txt").read_text(encoding="utf-8"))
ui = (root / "airlink_ui.c").read_text(encoding="utf-8")
font_c = (root / "airlink_fonts.c").read_text(encoding="utf-8")
generator = (root / "generate_fonts.py").read_text(encoding="utf-8")

font = meta["fonts"]["small_13"]
assert (font["size"], font["style"], font["bpp"], font["render"],
        font["line_height"], font["base_line"]) == (
            13, "Medium", 2, "contrast2", 16, 3)
samples = (
    "无线模式", "Wi-Fi未连接", "正在等待Linux服务",
    "切换会使USB设备重新枚举", "本机 IP 192.168.255.255",
    "连接配网热点", "热点名称 AirLink-FFFF", "密码 12345678",
    "点击屏幕返回", "系统切换中", "无线共享已就绪", "耳机口可用",
    "等待电脑连接", "电脑已连接", "无线共享已启动",
    "引脚对照",
)
missing = sorted({char for text in samples for char in text if char not in chars})
assert not missing, missing
assert "airlink_font_small_13" in font_c
assert 'render == "contrast2"' in generator
assert font_c.count("out->bpp=2") == 1
assert ui.count("&airlink_font_small_13") >= 20
for stale in ("airlink_font_micro_11", "airlink_font_small_12",
              "airlink_font_font_ab_", "airlink_font_fusion_12",
              "FONT TEST profile="):
    assert stale not in ui
print("R27.5 Noto Sans SC 13px enhanced-contrast 2bpp profile: PASS")
