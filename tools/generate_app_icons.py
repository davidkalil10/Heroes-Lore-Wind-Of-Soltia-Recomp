#!/usr/bin/env python3
"""
tools/generate_app_icons.py
Gera ícones nativos de alta fidelidade para todas as 4 plataformas (Switch, Android, Linux, Windows)
a partir da nova imagem de ícone oficial do Heroes Lore 1 (2048x2048).

Plataformas suportadas:
  - Nintendo Switch: switch/icon.jpg (256x256 JPEG de alta qualidade para o NRO)
  - Android: android/app/src/main/res/mipmap-*/ic_launcher.png e ic_launcher_round.png
             android/app/src/main/assets/icon.png
             android/app/src/main/assets/ui/app_icon.rgba
  - Linux / Steam Deck: dist_linux/heroes_lore.png (256x256 PNG para o AppImage/Desktop)
  - Windows: src/platform/heroes_lore.ico (multi-resolução 16, 24, 32, 48, 64, 128, 256)
             src/platform/heroes_lore.rc
  - Assets internos: assets/new_icon_hl1.jpg
                     assets/heroes_lore_icon_2048.jpg
                     assets/logo_512.png
                     assets/icon.png
                     assets/ui/app_icon.png
                     assets/ui/app_icon.rgba (para SDL_SetWindowIcon em runtime)
                     reference/extracted/icon.png
"""

import os
import sys
import struct
from PIL import Image, ImageDraw

def generate_all_icons(custom_src=None):
    candidates = [
        custom_src,
        "assets/new_icon_hl1.jpg",
        "assets/heroes_lore_icon_2048.jpg",
        r"C:\Users\david\Downloads\new icon HL1.jpg",
        "assets/logo_512.png"
    ]

    src_icon = None
    for c in candidates:
        if c and os.path.exists(c):
            src_icon = c
            break

    if not src_icon:
        print("[ERRO] Nenhuma imagem de ícone encontrada!")
        sys.exit(1)

    print(f"[Ícones] Carregando ícone base de: {src_icon}")
    orig = Image.open(src_icon).convert("RGBA")
    print(f"[Ícones] Dimensões originais: {orig.size[0]}x{orig.size[1]} ({orig.mode})")

    # Salva o ícone mestre no repositório se ainda não existir
    os.makedirs("assets", exist_ok=True)
    os.makedirs("assets/ui", exist_ok=True)

    # 1. Nintendo Switch: switch/icon.jpg (256x256 JPEG)
    os.makedirs("switch", exist_ok=True)
    switch_img = orig.resize((256, 256), Image.Resampling.LANCZOS)
    switch_bg = Image.new("RGB", (256, 256), (0, 0, 0))
    switch_bg.paste(switch_img, (0, 0), switch_img)
    switch_path = "switch/icon.jpg"
    switch_bg.save(switch_path, "JPEG", quality=95, optimize=True)
    print(f"  + Switch: {switch_path} (256x256 JPEG)")

    # 2. Linux / Steam Deck: dist_linux/heroes_lore.png (256x256 PNG)
    os.makedirs("dist_linux", exist_ok=True)
    linux_path = "dist_linux/heroes_lore.png"
    linux_img = orig.resize((256, 256), Image.Resampling.LANCZOS)
    linux_img.save(linux_path, "PNG", optimize=True)
    print(f"  + Linux: {linux_path} (256x256 PNG)")

    # 3. Android: mipmap-*/ic_launcher.png e ic_launcher_round.png
    densities = {
        'mipmap-mdpi': 48,
        'mipmap-hdpi': 72,
        'mipmap-xhdpi': 96,
        'mipmap-xxhdpi': 144,
        'mipmap-xxxhdpi': 192,
    }
    res_base = os.path.join("android", "app", "src", "main", "res")

    for folder, size in densities.items():
        out_dir = os.path.join(res_base, folder)
        os.makedirs(out_dir, exist_ok=True)

        # Ícone quadrado padrão
        resized = orig.resize((size, size), Image.Resampling.LANCZOS)
        resized.save(os.path.join(out_dir, "ic_launcher.png"), "PNG", optimize=True)

        # Ícone circular (ic_launcher_round.png) com máscara anti-serrilhada 4x
        mask = Image.new('L', (size * 4, size * 4), 0)
        draw = ImageDraw.Draw(mask)
        draw.ellipse((0, 0, size * 4, size * 4), fill=255)
        mask = mask.resize((size, size), Image.Resampling.LANCZOS)

        round_img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
        round_img.paste(resized, (0, 0))
        round_img.putalpha(mask)
        round_img.save(os.path.join(out_dir, "ic_launcher_round.png"), "PNG", optimize=True)
        print(f"  + Android {folder}: {size}x{size} (Square + Round)")

    # 4. Windows: src/platform/heroes_lore.ico e heroes_lore.rc
    os.makedirs("src/platform", exist_ok=True)
    ico_path = "src/platform/heroes_lore.ico"
    ico_sizes = [(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)]
    orig.save(ico_path, format="ICO", sizes=ico_sizes)
    print(f"  + Windows ICO: {ico_path} (Multi-res 16..256)")

    rc_path = "src/platform/heroes_lore.rc"
    with open(rc_path, "w", encoding="utf-8") as f:
        f.write('1 ICON "heroes_lore.ico"\n')
    print(f"  + Windows RC: {rc_path}")

    # 5. Assets internos e runtime RGBA para SDL_SetWindowIcon
    orig.resize((512, 512), Image.Resampling.LANCZOS).save("assets/logo_512.png", "PNG", optimize=True)
    orig.resize((128, 128), Image.Resampling.LANCZOS).save("assets/icon.png", "PNG", optimize=True)
    orig.resize((128, 128), Image.Resampling.LANCZOS).save("assets/ui/app_icon.png", "PNG", optimize=True)
    if os.path.exists("reference/extracted"):
        orig.resize((128, 128), Image.Resampling.LANCZOS).save("reference/extracted/icon.png", "PNG", optimize=True)

    # Gera assets/ui/app_icon.rgba (128x128 RGBA puro com cabeçalho uint32_t width, height)
    icon_128 = orig.resize((128, 128), Image.Resampling.LANCZOS).convert("RGBA")
    rgba_bytes = icon_128.tobytes()
    header = struct.pack("<II", 128, 128)
    rgba_data = header + rgba_bytes
    with open("assets/ui/app_icon.rgba", "wb") as f:
        f.write(rgba_data)
    print("  + Runtime RGBA: assets/ui/app_icon.rgba (128x128 RGBA)")

    # Sincroniza cópias para Android assets e Build assets se existirem
    if os.path.exists("android/app/src/main/assets"):
        os.makedirs("android/app/src/main/assets/ui", exist_ok=True)
        orig.resize((128, 128), Image.Resampling.LANCZOS).save("android/app/src/main/assets/icon.png", "PNG", optimize=True)
        with open("android/app/src/main/assets/ui/app_icon.rgba", "wb") as f:
            f.write(rgba_data)
        print("  + Android assets synced (icon.png & app_icon.rgba)")

    if os.path.exists("build/assets"):
        os.makedirs("build/assets/ui", exist_ok=True)
        orig.resize((128, 128), Image.Resampling.LANCZOS).save("build/assets/icon.png", "PNG", optimize=True)
        with open("build/assets/ui/app_icon.rgba", "wb") as f:
            f.write(rgba_data)
        print("  + Build assets synced (icon.png & app_icon.rgba)")

    print("\n[OK] Todos os ícones em alta definição foram gerados com sucesso para todas as plataformas!")

if __name__ == "__main__":
    src_arg = sys.argv[1] if len(sys.argv) > 1 else None
    generate_all_icons(src_arg)
