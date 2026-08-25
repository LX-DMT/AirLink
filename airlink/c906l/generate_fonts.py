#!/usr/bin/env python3
"""Generate the R27.5 production fonts.

All small UI text uses the selected B profile: Noto Sans SC 13px Medium,
enhanced-contrast 2bpp. Larger headings remain Noto Sans SC 4bpp.
"""
import argparse
import hashlib
import json
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


UI_TEXT = """
AirLink Wi-Fi USB HUB CH347 HDMI UART SPI I²C JTAG GHz dBm IP IPv4
无线模式 有线模式 USB共享已就绪 USB HUB已就绪 电脑已连接 HDMI · 网口可用 HDMI · 网口 · 耳机口可用 HDMI·网口·耳机口 已打开 耳机口可用
当前模式 引脚对照 屏幕方向 切换模式 双路 UART 双路UART SPI / I²C SPI / I²C 免驱 HID免驱 JTAG / UART
MODE0 MODE1 MODE2 MODE3 WI-FI 未连接 已连接 信号良好 信号一般 信号较弱 本机 IP 重新配置
电量正常 电量偏低 电量 估算 电压 电池采样未就绪
选择CH347模式 按住1秒确认 切换会中断远程CH347 切换会使USB设备重新枚举
UART0与UART1同时工作 UART1 + SPI、I²C及GPIO 使用系统HID免驱访问 JTAG调试 + UART1
准备无线共享服务 正在切换 等待重新枚举 切换成功 切换失败 已切换到
远程CH347将重新识别 USB设备将重新枚举
正在进入无线模式 正在进入有线模式 正在启动Wi-Fi共享 正在切换USB连接路径
无线模式已就绪 有线模式已就绪 USB共享服务可用 配网服务待接入 返回
无线共享已就绪 无线共享已启动 等待电脑连接 电脑已连接 配网页面将在后续版本接入 触摸返回 等待网络状态 请稍后重新尝试 切换失败：
Wi-Fi与远程共享已停止 Wi-Fi与远程共享已恢复 尚未保存Wi-Fi配置 连接Wi-Fi超时
远程共享启动失败 请检查系统日志 Linux系统未在8秒内响应 模式切换超时
正在连接已保存的Wi-Fi 正在切换系统服务 系统异常
正在启动配网热点 正在扫描周边Wi-Fi 连接配网热点 热点名称 密码 地址 点击屏幕返回 配网服务准备中
正在应用新配置 热点暂时断开 请等待圆屏显示结果 Wi-Fi配置成功 网络连接与保存完成
VirtualHere正在启动 连接失败，热点已恢复 等待配网服务
正在切换有线模式 正在停止无线服务 正在启动无线服务 正在准备网络
有线服务异常 无线服务异常 系统正在启动 系统启动中 正在等待Linux服务
请稍候 Wi-Fi未配置 请使用手机配置 远程共享正在启动 正在等待网络
系统切换中 暂不可用
"""
ASCII = "".join(chr(i) for i in range(32, 127))
CHARS = "".join(sorted((char for char in set(UI_TEXT + ASCII) if ord(char) >= 32), key=ord))
SPECS = [
    ("medium_17", 17, "Medium", 4),
    ("medium_18", 18, "Medium", 4),
    ("medium_20", 20, "Medium", 4),
    ("medium_21", 21, "Medium", 4),
    ("medium_27", 27, "Medium", 4),
]
SMALL_SPEC = ("small_13", 13, "Medium", 2, "contrast2")

# Static labels which must fit their LVGL containers. The generated metadata
# makes these checks independent of Pillow at normal firmware-build time.
LAYOUT_CHECKS = [
    ("status_100_percent", "100%", "small_13", 36),
    ("status_2_4g", "2.4G", "small_13", 38),
    ("status_wired", "有线", "small_13", 38),
    ("home_badge_wireless", "无线模式", "small_13", 76),
    ("home_waiting", "正在等待系统启动", "small_13", 164),
    ("home_disconnected", "Wi-Fi未连接", "small_13", 164),
    ("ch347_chip", "CH347", "small_13", 56),
    ("ch347_pin_button", "引脚对照", "small_13", 80),
    ("ch347_button", "切换模式", "small_13", 80),
    ("pinout_cell", "DTR0", "small_13", 35),
    ("pinout_screen_direction", "屏幕方向", "small_13", 84),
    ("wifi_state_waiting", "等待网络状态", "small_13", 231),
    ("wifi_state_connected", "5 GHz · 信号良好", "small_13", 231),
    ("wifi_ip_longest", "本机 IP 192.168.255.255", "small_13", 154),
    ("home_wired_features", "HDMI · 网口 · 耳机口可用", "small_13", 164),
    ("wired_features_primary", "HDMI·网口·耳机口", "medium_17", 190),
    ("wired_features_open", "已打开", "medium_17", 190),
    ("battery_not_ready", "电池采样未就绪", "small_13", 120),
    ("ch347_mode_note", "UART1 + SPI、I²C及GPIO", "small_13", 156),
    ("ch347_warning", "切换会使USB设备重新枚举", "small_13", 160),
    ("ch347_hold", "按住1秒确认", "small_13", 116),
    ("result_note", "远程CH347将重新识别", "small_13", 160),
    ("provision_note", "连接失败，热点已恢复", "small_13", 190),
    ("provision_password", "密码 12345678", "small_13", 180),
    ("provision_ssid", "热点名称 AirLink-FFFF", "small_13", 190),
    ("provision_return", "点击屏幕返回", "small_13", 160),
    ("inline_boot", "系统正在启动", "medium_18", 166),
    ("inline_boot_note", "正在等待Linux服务", "small_13", 164),
    ("inline_wired", "正在切换有线模式", "medium_18", 166),
    ("inline_wired_note", "正在停止无线服务", "small_13", 164),
    ("inline_wireless", "正在启动无线服务", "medium_18", 166),
    ("inline_wireless_note", "正在准备网络", "small_13", 164),
    ("inline_ch347_locked", "系统切换中", "small_13", 80),
    ("inline_wifi_locked", "暂不可用", "small_13", 94),
    ("saver_state", "无线共享已就绪", "small_13", 140),
    ("saver_wait_pc", "等待电脑连接", "small_13", 140),
    ("home_wait_pc", "等待电脑连接", "medium_18", 166),
]


def pack_bitmap(image, bpp):
    levels = (1 << bpp) - 1
    values = [int(round(pixel * levels / 255.0)) for pixel in image.getdata()]
    per_byte = 8 // bpp
    while len(values) % per_byte:
        values.append(0)
    output = bytearray()
    for offset in range(0, len(values), per_byte):
        value = 0
        for index in range(per_byte):
            value |= values[offset + index] << (8 - bpp * (index + 1))
        output.append(value)
    return bytes(output)


def load_font(path, size, style):
    font = ImageFont.truetype(str(path), size)
    if style and hasattr(font, "set_variation_by_name"):
        font.set_variation_by_name(style)
    return font


def runtime_width(path, size, style, text):
    font = load_font(path, size, style)
    return sum((max(1, int(round(font.getlength(char) * 16.0))) + 8) >> 4
               for char in text)


def render_glyph(font, char, left, top, width, height, render):
    if render == "native_mono":
        image = Image.new("1", (width, height), 0)
        draw = ImageDraw.Draw(image)
        draw.text((-left, -top), char, font=font, fill=1, anchor="ls")
        return image.convert("L")

    image = Image.new("L", (width, height), 0)
    draw = ImageDraw.Draw(image)
    draw.text((-left, -top), char, font=font, fill=255, anchor="ls")
    if render == "threshold":
        return image.point(lambda value: 255 if value >= 112 else 0)
    if render == "contrast2":
        return image.point(lambda value: max(0, min(255,
            (value - 32) * 255 // 192)))
    return image


def make_font(path, size, style, bpp, chars=CHARS, render="normal"):
    font = load_font(path, size, style)
    ascent, descent = font.getmetrics()
    bitmaps = bytearray()
    glyphs = []
    for char in chars:
        left, top, right, bottom = font.getbbox(char, anchor="ls")
        width = max(0, right - left)
        height = max(0, bottom - top)
        # LVGL native tables store 1/16 px, but a custom callback returns pixels.
        # Keep precision here and convert in the generated callback below.
        advance = max(1, int(round(font.getlength(char) * 16.0)))
        offset = len(bitmaps)
        if width and height:
            image = render_glyph(font, char, left, top, width, height, render)
            bitmaps.extend(pack_bitmap(image, bpp))
        glyphs.append({
            "cp": ord(char), "offset": offset, "adv": advance,
            "w": width, "h": height, "ofs_x": left, "ofs_y": -bottom,
        })
    return {
        "size": size, "style": style, "bpp": bpp, "render": render,
        "ascent": ascent, "descent": descent,
        "line_height": ascent + descent, "base_line": descent,
        "bitmaps": bytes(bitmaps), "glyphs": glyphs,
    }


def generated_width(font, text):
    glyphs = {glyph["cp"]: glyph for glyph in font["glyphs"]}
    return sum((glyphs[ord(char)]["adv"] + 8) >> 4 for char in text)


def emit_array(lines, data):
    for index in range(0, len(data), 16):
        lines.append("    " + ", ".join(
            "0x%02x" % value for value in data[index:index + 16]) + ",")


def emit_font(lines, name, font):
    prefix = "font_" + name
    lines += [f"static const uint8_t {prefix}_bitmap[] = {{"]
    emit_array(lines, font["bitmaps"])
    lines += ["};", f"static const struct airlink_glyph {prefix}_glyphs[] = {{"]
    for glyph in font["glyphs"]:
        lines.append(
            "    {0x%04xU, %dU, %dU, %dU, %dU, %d, %d}," % (
                glyph["cp"], glyph["offset"], glyph["adv"], glyph["w"],
                glyph["h"], glyph["ofs_x"], glyph["ofs_y"]))
    lines += [
        "};",
        f"static const struct airlink_glyph *{prefix}_find(uint32_t cp)",
        "{",
        f"    uint32_t lo=0U, hi=(uint32_t)(sizeof({prefix}_glyphs)/sizeof({prefix}_glyphs[0]));",
        f"    while(lo<hi){{uint32_t mid=lo+(hi-lo)/2U; if({prefix}_glyphs[mid].cp<cp) lo=mid+1U; else hi=mid;}}",
        f"    return lo<(uint32_t)(sizeof({prefix}_glyphs)/sizeof({prefix}_glyphs[0])) && {prefix}_glyphs[lo].cp==cp ? &{prefix}_glyphs[lo] : NULL;",
        "}",
        f"static bool {prefix}_dsc(const lv_font_t *font, lv_font_glyph_dsc_t *out, uint32_t cp, uint32_t next)",
        "{",
        f"    const struct airlink_glyph *g={prefix}_find(cp); (void)next; if(!g) return false;",
        "    out->resolved_font=font; out->adv_w=(uint16_t)((g->adv+8U)>>4); out->box_w=g->w; out->box_h=g->h;",
        f"    out->ofs_x=g->ofs_x; out->ofs_y=g->ofs_y; out->bpp={font['bpp']}; out->is_placeholder=0; return true;",
        "}",
        f"static const uint8_t *{prefix}_bitmap_cb(const lv_font_t *font, uint32_t cp)",
        "{",
        f"    const struct airlink_glyph *g={prefix}_find(cp); (void)font; return g ? {prefix}_bitmap+g->offset : NULL;",
        "}",
        f"const lv_font_t airlink_font_{name} = {{",
        f"    .get_glyph_dsc={prefix}_dsc, .get_glyph_bitmap={prefix}_bitmap_cb,",
        f"    .line_height={font['line_height']}, .base_line={font['base_line']},",
        "    .subpx=LV_FONT_SUBPX_NONE, .underline_position=-2, .underline_thickness=1,",
        "    .dsc=NULL, .fallback=&lv_font_montserrat_14",
        "};", "",
    ]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--font", required=True)
    parser.add_argument("--out-dir", required=True)
    args = parser.parse_args()
    source = Path(args.font)
    out = Path(args.out_dir)
    out.mkdir(parents=True, exist_ok=True)
    fonts = {name: make_font(source, size, style, bpp)
             for name, size, style, bpp in SPECS}
    small_name, small_size, small_style, small_bpp, small_render = SMALL_SPEC
    fonts[small_name] = make_font(
        source, small_size, small_style, small_bpp,
        chars=CHARS, render=small_render)

    header = [
        "#ifndef AIRLINK_FONTS_H", "#define AIRLINK_FONTS_H",
        '#include "lvgl.h"',
    ]
    for name in fonts:
        header.append(f"extern const lv_font_t airlink_font_{name};")
    header += ["#endif", ""]
    (out / "airlink_fonts.h").write_bytes("\n".join(header).encode("utf-8"))
    lines = [
        "/* Generated from Noto Sans SC. See FONT_UPSTREAM.txt and OFL.txt. */",
        '#include "lvgl.h"', '#include "airlink_fonts.h"', "#include <stddef.h>",
        "struct airlink_glyph { uint32_t cp, offset; uint16_t adv, w, h; int16_t ofs_x, ofs_y; };",
    ]
    for name in fonts:
        emit_font(lines, name, fonts[name])
    (out / "airlink_fonts.c").write_bytes("\n".join(lines).encode("utf-8"))
    (out / "airlink_fonts.chars.txt").write_bytes(CHARS.encode("utf-8"))
    metadata = {
        "family": "Noto Sans SC", "source": str(source),
        "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
        "glyph_count": len(CHARS),
        "fonts": {name: {key: value for key, value in data.items()
                           if key not in ("bitmaps", "glyphs")}
                  for name, data in fonts.items()},
        "checks": {
            "无线模式_small_13": generated_width(fonts["small_13"], "无线模式"),
            "100%_small_13": generated_width(fonts["small_13"], "100%"),
            "USB共享已就绪_medium_18": generated_width(fonts["medium_18"], "USB共享已就绪"),
            "192.168.31.80_small_13": generated_width(fonts["small_13"], "192.168.31.80"),
        },
        "runtime_width_px": {
            "无线模式_small_13": generated_width(fonts["small_13"], "无线模式"),
            "100%_small_13": generated_width(fonts["small_13"], "100%"),
            "USB共享已就绪_medium_18": generated_width(fonts["medium_18"], "USB共享已就绪"),
            "192.168.31.80_small_13": generated_width(fonts["small_13"], "192.168.31.80"),
        },
        "layout_width_px": {
            key: generated_width(fonts[font_name], text)
            for key, text, font_name, _ in LAYOUT_CHECKS
        },
        "layout_limit_px": {
            key: limit for key, _, _, limit in LAYOUT_CHECKS
        },
    }
    (out / "airlink_fonts.json").write_bytes((
        json.dumps(metadata, ensure_ascii=False, indent=2) + "\n").encode("utf-8"))
    print("generated", len(fonts), "fonts,", len(CHARS), "glyphs; small=small_13 enhanced-contrast 2bpp")


if __name__ == "__main__":
    main()
