# tools/generate_gamepad_textures.py
# Gera texturas ultra-polidas, anti-aliased (supersampling 4x) com visual estilo vidro/console
import os
import struct
from PIL import Image, ImageDraw, ImageFont

FONT_PATH = "C:/Windows/Fonts/segoeuib.ttf"
if not os.path.exists(FONT_PATH):
    FONT_PATH = "C:/Windows/Fonts/arialbd.ttf"

def save_rgba(img, out_path):
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    w, h = img.size
    raw_data = img.tobytes("raw", "RGBA")
    with open(out_path, "wb") as f:
        # Header: uint32 width, uint32 height
        f.write(struct.pack("<II", w, h))
        f.write(raw_data)
    # Salva também cópia PNG para visualização
    png_path = os.path.splitext(out_path)[0] + ".png"
    img.save(png_path)

def make_dpad_base(final_size=256):
    s = 4
    size = final_size * s
    img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    cx, cy = size // 2, size // 2
    r_outer = size // 2 - 8 * s

    # 1. Sombra suave externa
    for i in range(12 * s, 0, -2 * s):
        alpha = int(12 * (1.0 - i / (12 * s)))
        d.ellipse([cx - r_outer - i, cy - r_outer - i, cx + r_outer + i, cy + r_outer + i],
                  fill=(0, 0, 0, alpha))

    # 2. Anel externo metálico / vidro escuro
    d.ellipse([cx - r_outer, cy - r_outer, cx + r_outer, cy + r_outer],
              fill=(26, 28, 38, 235), outline=(70, 85, 110, 240), width=4*s)

    # 3. Chanfro de reflexo superior
    d.arc([cx - r_outer + 3*s, cy - r_outer + 3*s, cx + r_outer - 3*s, cy + r_outer - 3*s],
          210, 330, fill=(160, 195, 245, 180), width=3*s)

    # 4. Braços em cruz do D-Pad
    arm_w = int(r_outer * 0.70)
    arm_l = int(r_outer * 0.90)
    # Braço horizontal
    d.rounded_rectangle([cx - arm_l, cy - arm_w // 2, cx + arm_l, cy + arm_w // 2],
                        radius=8*s, fill=(16, 18, 26, 240), outline=(50, 65, 85, 200), width=2*s)
    # Braço vertical
    d.rounded_rectangle([cx - arm_w // 2, cy - arm_l, cx + arm_w // 2, cy + arm_l],
                        radius=8*s, fill=(16, 18, 26, 240), outline=(50, 65, 85, 200), width=2*s)

    # 5. Concavidade central
    r_center = int(r_outer * 0.32)
    d.ellipse([cx - r_center, cy - r_center, cx + r_center, cy + r_center],
              fill=(10, 12, 18, 255), outline=(60, 75, 95, 180), width=2*s)

    # 6. Setas direcionais gravadas (Up, Down, Left, Right)
    arrow_dist = int(r_outer * 0.62)
    arr_sz = 14 * s
    # Cima
    d.polygon([(cx, cy - arrow_dist - arr_sz),
               (cx - arr_sz, cy - arrow_dist + arr_sz // 2),
               (cx + arr_sz, cy - arrow_dist + arr_sz // 2)], fill=(180, 200, 230, 220))
    # Baixo
    d.polygon([(cx, cy + arrow_dist + arr_sz),
               (cx - arr_sz, cy + arrow_dist - arr_sz // 2),
               (cx + arr_sz, cy + arrow_dist - arr_sz // 2)], fill=(180, 200, 230, 220))
    # Esquerda
    d.polygon([(cx - arrow_dist - arr_sz, cy),
               (cx - arrow_dist + arr_sz // 2, cy - arr_sz),
               (cx - arrow_dist + arr_sz // 2, cy + arr_sz)], fill=(180, 200, 230, 220))
    # Direita
    d.polygon([(cx + arrow_dist + arr_sz, cy),
               (cx + arrow_dist - arr_sz // 2, cy - arr_sz),
               (cx + arrow_dist - arr_sz // 2, cy + arr_sz)], fill=(180, 200, 230, 220))

    return img.resize((final_size, final_size), Image.Resampling.LANCZOS)

def make_dpad_arrow_active(final_size=64):
    s = 4
    size = final_size * s
    img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    cx, cy = size // 2, size // 2
    r = size // 2 - 4 * s

    # Brilho radial ciano
    for i in range(r, 0, -2 * s):
        alpha = int(180 * (1.0 - i / r))
        d.ellipse([cx - i, cy - i, cx + i, cy + i], fill=(0, 210, 255, alpha))

    # Seta branca brilhante
    arr_sz = int(r * 0.6)
    d.polygon([(cx, cy - arr_sz),
               (cx - arr_sz, cy + arr_sz // 2),
               (cx + arr_sz, cy + arr_sz // 2)], fill=(255, 255, 255, 255))

    return img.resize((final_size, final_size), Image.Resampling.LANCZOS)

def make_round_button(label, sublabel="", final_size=128, accent_color=(70, 85, 110), is_primary=False):
    s = 4
    size = final_size * s
    img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    cx, cy = size // 2, size // 2
    r_outer = size // 2 - 6 * s

    # 1. Sombra suave externa
    for i in range(10 * s, 0, -2 * s):
        alpha = int(15 * (1.0 - i / (10 * s)))
        d.ellipse([cx - r_outer - i, cy - r_outer - i, cx + r_outer + i, cy + r_outer + i],
                  fill=(0, 0, 0, alpha))

    # 2. Anel externo com cor de destaque
    ring_color = (0, 180, 255, 240) if is_primary else (*accent_color, 240)
    bg_color = (20, 24, 34, 235) if is_primary else (24, 26, 36, 230)
    d.ellipse([cx - r_outer, cy - r_outer, cx + r_outer, cy + r_outer],
              fill=bg_color, outline=ring_color, width=4*s)

    # 3. Reflexo de brilho superior
    d.arc([cx - r_outer + 3*s, cy - r_outer + 3*s, cx + r_outer - 3*s, cy + r_outer - 3*s],
          210, 330, fill=(200, 225, 255, 180), width=3*s)

    # 4. Círculo interno fosco
    r_inner = r_outer - 6 * s
    inner_color = (14, 18, 26, 240) if is_primary else (16, 18, 24, 240)
    d.ellipse([cx - r_inner, cy - r_inner, cx + r_inner, cy + r_inner],
              fill=inner_color)

    # 5. Texto centralizado com sombra para legibilidade máxima
    font_size = int(size * 0.42) if not sublabel else int(size * 0.35)
    font = ImageFont.truetype(FONT_PATH, font_size)
    text_color = (255, 255, 255, 255) if is_primary else (235, 245, 255, 250)

    bbox = d.textbbox((0, 0), label, font=font)
    tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
    y_offset = -int(size * 0.08) if sublabel else 0
    # Sombra do texto
    d.text((cx - tw // 2 + 2*s, cy - th // 2 + y_offset - bbox[1] + 2*s), label, fill=(0, 0, 0, 220), font=font)
    d.text((cx - tw // 2, cy - th // 2 + y_offset - bbox[1]), label, fill=text_color, font=font)

    if sublabel:
        sub_font_size = int(size * 0.16)
        sub_font = ImageFont.truetype(FONT_PATH, sub_font_size)
        sub_bbox = d.textbbox((0, 0), sublabel, font=sub_font)
        stw, sth = sub_bbox[2] - sub_bbox[0], sub_bbox[3] - sub_bbox[1]
        sub_color = (0, 230, 255, 250) if is_primary else (160, 190, 225, 230)
        d.text((cx - stw // 2 + s, cy + int(size * 0.18) - sub_bbox[1] + s), sublabel, fill=(0, 0, 0, 220), font=sub_font)
        d.text((cx - stw // 2, cy + int(size * 0.18) - sub_bbox[1]), sublabel, fill=sub_color, font=sub_font)

    return img.resize((final_size, final_size), Image.Resampling.LANCZOS)

def make_pill_button(label, final_w=240, final_h=100, accent_color=(80, 140, 240)):
    s = 4
    w, h = final_w * s, final_h * s
    img = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    rad = h // 2
    pad = 4 * s
    # Sombra externa suave
    for i in range(8 * s, 0, -2 * s):
        alpha = int(16 * (1.0 - i / (8 * s)))
        d.rounded_rectangle([pad - i, pad - i, w - pad + i, h - pad + i],
                            radius=rad + i, fill=(0, 0, 0, alpha))

    # Pílula principal
    d.rounded_rectangle([pad, pad, w - pad, h - pad],
                        radius=rad, fill=(20, 24, 34, 235), outline=(*accent_color, 255), width=4*s)

    # Chanfro de brilho superior
    d.arc([pad + 3*s, pad + 3*s, pad + rad*2, pad + rad*2],
          180, 270, fill=(200, 230, 255, 210), width=3*s)
    d.line([pad + rad, pad + 3*s, w - pad - rad, pad + 3*s],
           fill=(200, 230, 255, 180), width=3*s)

    # Texto com sombra 3D (tamanho maior para labels curtas como "R")
    font_size = int(h * 0.48) if len(label) <= 2 else int(h * 0.38)
    font = ImageFont.truetype(FONT_PATH, font_size)
    bbox = d.textbbox((0, 0), label, font=font)
    tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
    # Sombra
    d.text(((w - tw) // 2 + 2*s, (h - th) // 2 - bbox[1] + 2*s), label, fill=(0, 0, 0, 240), font=font)
    # Texto
    d.text(((w - tw) // 2, (h - th) // 2 - bbox[1]), label, fill=(255, 255, 255, 255), font=font)

    return img.resize((final_w, final_h), Image.Resampling.LANCZOS)

def make_arrow_button(direction="left", final_size=108, accent_color=(0, 200, 255)):
    s = 4
    size = final_size * s
    img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    cx, cy = size // 2, size // 2
    r_outer = size // 2 - 6 * s

    # Sombra externa
    for i in range(10 * s, 0, -2 * s):
        alpha = int(18 * (1.0 - i / (10 * s)))
        d.ellipse([cx - r_outer - i, cy - r_outer - i, cx + r_outer + i, cy + r_outer + i],
                  fill=(0, 0, 0, alpha))

    # Anel externo com cor de destaque
    d.ellipse([cx - r_outer, cy - r_outer, cx + r_outer, cy + r_outer],
              fill=(22, 26, 36, 235), outline=(*accent_color, 255), width=4*s)

    # Reflexo superior
    d.arc([cx - r_outer + 3*s, cy - r_outer + 3*s, cx + r_outer - 3*s, cy + r_outer - 3*s],
          210, 330, fill=(200, 235, 255, 200), width=3*s)

    # Círculo interno
    r_inner = r_outer - 6 * s
    d.ellipse([cx - r_inner, cy - r_inner, cx + r_inner, cy + r_inner],
              fill=(16, 18, 26, 240))

    # Seta triangular sólida estilizada
    arr_h = int(r_inner * 0.70)
    arr_w = int(r_inner * 0.55)

    if direction == "left":
        pts = [
            (cx - arr_w // 2, cy),
            (cx + arr_w // 2, cy - arr_h // 2),
            (cx + arr_w // 2, cy + arr_h // 2)
        ]
        pts_shadow = [
            (cx - arr_w // 2 + 2*s, cy + 2*s),
            (cx + arr_w // 2 + 2*s, cy - arr_h // 2 + 2*s),
            (cx + arr_w // 2 + 2*s, cy + arr_h // 2 + 2*s)
        ]
    else: # right
        pts = [
            (cx + arr_w // 2, cy),
            (cx - arr_w // 2, cy - arr_h // 2),
            (cx - arr_w // 2, cy + arr_h // 2)
        ]
        pts_shadow = [
            (cx + arr_w // 2 + 2*s, cy + 2*s),
            (cx - arr_w // 2 + 2*s, cy - arr_h // 2 + 2*s),
            (cx - arr_w // 2 + 2*s, cy + arr_h // 2 + 2*s)
        ]

    d.polygon(pts_shadow, fill=(0, 0, 0, 220))
    d.polygon(pts, fill=(255, 255, 255, 255))

    return img.resize((final_size, final_size), Image.Resampling.LANCZOS)

def make_glow_overlay(final_size=128):
    s = 4
    size = final_size * s
    img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    cx, cy = size // 2, size // 2
    r = size // 2 - 2 * s

    for i in range(r, 0, -2 * s):
        alpha = int(180 * (1.0 - (i / r) ** 1.5))
        d.ellipse([cx - i, cy - i, cx + i, cy + i], fill=(0, 215, 255, alpha))

    return img.resize((final_size, final_size), Image.Resampling.LANCZOS)

def make_eye_icon(final_size=96, closed=False):
    import math
    s = 4
    size = final_size * s
    img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    cx, cy = size // 2, size // 2
    r_outer = size // 2 - 6 * s

    # Sombra
    for i in range(8 * s, 0, -2 * s):
        d.ellipse([cx - r_outer - i, cy - r_outer - i, cx + r_outer + i, cy + r_outer + i],
                  fill=(0, 0, 0, int(15 * (1.0 - i / (8 * s)))))

    # Base circular de vidro escuro
    d.ellipse([cx - r_outer, cy - r_outer, cx + r_outer, cy + r_outer],
              fill=(20, 24, 34, 230), outline=(70, 85, 110, 230), width=3*s)
    # Chanfro superior
    d.arc([cx - r_outer + 2*s, cy - r_outer + 2*s, cx + r_outer - 2*s, cy + r_outer - 2*s],
          210, 330, fill=(160, 195, 245, 180), width=2*s)

    # Contorno do olho
    ew = int(r_outer * 0.70)
    eh = int(r_outer * 0.42)
    eye_pts_top = []
    eye_pts_bot = []
    steps = 40
    for step in range(steps + 1):
        t = step / steps
        x = cx - ew + 2 * ew * t
        y_offset = eh * math.sin(t * math.pi)
        eye_pts_top.append((x, cy - y_offset))
        eye_pts_bot.append((x, cy + y_offset))

    eye_poly = eye_pts_top + eye_pts_bot[::-1]
    d.polygon(eye_poly, fill=(35, 42, 58, 240), outline=(180, 205, 235, 240))
    for i in range(len(eye_poly)):
        p1 = eye_poly[i]
        p2 = eye_poly[(i + 1) % len(eye_poly)]
        d.line([p1, p2], fill=(200, 225, 255, 255), width=3*s)

    # Pupila / Íris
    r_iris = int(eh * 0.75)
    d.ellipse([cx - r_iris, cy - r_iris, cx + r_iris, cy + r_iris],
              fill=(0, 200, 255, 255), outline=(255, 255, 255, 220), width=2*s)
    r_pupil = int(r_iris * 0.50)
    d.ellipse([cx - r_pupil, cy - r_pupil, cx + r_pupil, cy + r_pupil], fill=(10, 15, 25, 255))
    d.ellipse([cx - r_pupil // 2, cy - r_pupil // 2 - 2*s, cx, cy - 2*s], fill=(255, 255, 255, 255))

    if closed:
        slash_len = int(r_outer * 0.75)
        d.line([(cx - slash_len, cy - slash_len), (cx + slash_len, cy + slash_len)],
               fill=(255, 60, 80, 255), width=4*s)

    return img.resize((final_size, final_size), Image.Resampling.LANCZOS)

def make_rotate_icon(final_size=96):
    import math
    s = 4
    size = final_size * s
    img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    cx, cy = size // 2, size // 2
    r_outer = size // 2 - 6 * s

    # Sombra
    for i in range(8 * s, 0, -2 * s):
        d.ellipse([cx - r_outer - i, cy - r_outer - i, cx + r_outer + i, cy + r_outer + i],
                  fill=(0, 0, 0, int(15 * (1.0 - i / (8 * s)))))

    # Base circular de vidro escuro
    d.ellipse([cx - r_outer, cy - r_outer, cx + r_outer, cy + r_outer],
              fill=(20, 24, 34, 230), outline=(70, 85, 110, 230), width=3*s)
    # Chanfro superior
    d.arc([cx - r_outer + 2*s, cy - r_outer + 2*s, cx + r_outer - 2*s, cy + r_outer - 2*s],
          210, 330, fill=(160, 195, 245, 180), width=2*s)

    # Smartphone outline no centro
    pw = int(r_outer * 0.46)
    ph = int(r_outer * 0.76)
    d.rounded_rectangle([cx - pw//2, cy - ph//2, cx + pw//2, cy + ph//2], radius=4*s,
                        fill=(35, 42, 58, 240), outline=(0, 210, 255, 255), width=3*s)
    d.rounded_rectangle([cx - pw//2 + 3*s, cy - ph//2 + 6*s, cx + pw//2 - 3*s, cy + ph//2 - 6*s], radius=2*s,
                        fill=(10, 15, 25, 255))
    # Seta circular indicando rotação
    r_arc = int(r_outer * 0.72)
    d.arc([cx - r_arc, cy - r_arc, cx + r_arc, cy + r_arc], -30, 80, fill=(255, 255, 255, 240), width=3*s)
    d.polygon([(cx + int(r_arc * math.cos(80*math.pi/180)) - 6*s, cy + int(r_arc * math.sin(80*math.pi/180)) - 4*s),
               (cx + int(r_arc * math.cos(80*math.pi/180)) + 6*s, cy + int(r_arc * math.sin(80*math.pi/180)) + 8*s),
               (cx + int(r_arc * math.cos(80*math.pi/180)) - 8*s, cy + int(r_arc * math.sin(80*math.pi/180)) + 12*s)],
              fill=(255, 255, 255, 255))

    return img.resize((final_size, final_size), Image.Resampling.LANCZOS)

def make_aspect_icon(final_size=96):
    s = 4
    size = final_size * s
    img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    cx, cy = size // 2, size // 2
    r_outer = size // 2 - 6 * s

    # Sombra
    for i in range(8 * s, 0, -2 * s):
        d.ellipse([cx - r_outer - i, cy - r_outer - i, cx + r_outer + i, cy + r_outer + i],
                  fill=(0, 0, 0, int(15 * (1.0 - i / (8 * s)))))

    # Base circular de vidro escuro
    d.ellipse([cx - r_outer, cy - r_outer, cx + r_outer, cy + r_outer],
              fill=(20, 24, 34, 230), outline=(0, 190, 240, 230), width=3*s)
    # Chanfro superior
    d.arc([cx - r_outer + 2*s, cy - r_outer + 2*s, cx + r_outer - 2*s, cy + r_outer - 2*s],
          210, 330, fill=(160, 215, 255, 180), width=2*s)

    # Frame widescreen (16:9)
    fw = int(r_outer * 1.10)
    fh = int(fw * (9.0 / 16.0))
    d.rounded_rectangle([cx - fw // 2, cy - fh // 2, cx + fw // 2, cy + fh // 2],
                        radius=4*s, fill=(12, 16, 24, 230), outline=(0, 210, 255, 240), width=2*s)

    # Texto "16:9" no interior
    font_size = int(fh * 0.58)
    font = ImageFont.truetype(FONT_PATH, font_size)
    txt = "16:9"
    bbox = d.textbbox((0, 0), txt, font=font)
    tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
    d.text((cx - tw // 2 + s, cy - th // 2 - bbox[1] + s), txt, fill=(0, 0, 0, 240), font=font)
    d.text((cx - tw // 2, cy - th // 2 - bbox[1]), txt, fill=(255, 255, 255, 255), font=font)

    return img.resize((final_size, final_size), Image.Resampling.LANCZOS)

def make_fps_icon(final_size=96):
    s = 4
    size = final_size * s
    img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    cx, cy = size // 2, size // 2
    r_outer = size // 2 - 6 * s

    # Sombra
    for i in range(8 * s, 0, -2 * s):
        d.ellipse([cx - r_outer - i, cy - r_outer - i, cx + r_outer + i, cy + r_outer + i],
                  fill=(0, 0, 0, int(15 * (1.0 - i / (8 * s)))))

    # Base circular de vidro escuro com outline ambar turbo
    d.ellipse([cx - r_outer, cy - r_outer, cx + r_outer, cy + r_outer],
              fill=(20, 24, 34, 230), outline=(255, 170, 40, 230), width=3*s)
    # Chanfro superior
    d.arc([cx - r_outer + 2*s, cy - r_outer + 2*s, cx + r_outer - 2*s, cy + r_outer - 2*s],
          210, 330, fill=(255, 220, 160, 180), width=2*s)

    # Arco de velocimetro (150 a 390 graus)
    r_arc = int(r_outer * 0.72)
    d.arc([cx - r_arc, cy - r_arc - 2*s, cx + r_arc, cy + r_arc - 2*s],
          150, 390, fill=(255, 140, 30, 240), width=3*s)

    # Texto "FPS" no centro
    font_size = int(r_outer * 0.50)
    font = ImageFont.truetype(FONT_PATH, font_size)
    txt = "FPS"
    bbox = d.textbbox((0, 0), txt, font=font)
    tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
    d.text((cx - tw // 2 + s, cy - th // 2 - bbox[1] + 3*s), txt, fill=(0, 0, 0, 240), font=font)
    d.text((cx - tw // 2, cy - th // 2 - bbox[1] + 2*s), txt, fill=(255, 240, 200, 255), font=font)

    return img.resize((final_size, final_size), Image.Resampling.LANCZOS)

def main():
    dest_dirs = [
        "android/app/src/main/assets/ui",
        "assets/ui",
        "reference/extracted/ui",
        "build/assets/ui",
    ]

    textures = {
        "dpad_base": make_dpad_base(256),
        "dpad_arrow_active": make_dpad_arrow_active(64),
        "btn_5": make_round_button("5", "ATK", final_size=128, accent_color=(0, 210, 255), is_primary=True),
        "btn_1": make_round_button("1", "SKILL", final_size=96, accent_color=(255, 140, 40)),
        "btn_3": make_round_button("3", "SKILL", final_size=96, accent_color=(255, 80, 100)),
        "btn_7": make_round_button("7", "POT", final_size=96, accent_color=(80, 220, 120)),
        "btn_9": make_round_button("9", "ITEM", final_size=96, accent_color=(220, 180, 50)),
        "btn_menu": make_pill_button("MENU", 240, 100, accent_color=(70, 130, 240)),
        "btn_map": make_pill_button("MAPA", 240, 100, accent_color=(0, 200, 255)),
        "btn_rsk": make_pill_button("R", 240, 100, accent_color=(240, 110, 80)),
        "btn_rsk_round": make_round_button("R", "BACK", final_size=96, accent_color=(240, 110, 80)),
        "btn_prev": make_arrow_button("left", 108, accent_color=(0, 200, 255)),
        "btn_next": make_arrow_button("right", 108, accent_color=(0, 200, 255)),
        "btn_eye_open": make_eye_icon(96, False),
        "btn_eye_closed": make_eye_icon(96, True),
        "btn_rotate": make_rotate_icon(96),
        "btn_aspect": make_aspect_icon(96),
        "btn_fps": make_fps_icon(96),
        "btn_glow": make_glow_overlay(128),
    }

    for name, img in textures.items():
        for d in dest_dirs:
            save_rgba(img, os.path.join(d, f"{name}.rgba"))
        print(f"Gerado: {name} ({img.size[0]}x{img.size[1]})")

if __name__ == "__main__":
    main()
