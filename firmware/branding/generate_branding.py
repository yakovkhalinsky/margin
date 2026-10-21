#!/usr/bin/env python3
"""Generate margin branding assets from the od3sa cyber bear source image.

v3: Improved light-variant rendering with proper detail preservation.
- Treats neon outlines as strokes, dark fills as paper
- Uses ordered dithering for 4-level greyscale on fills
- Removes horizontal glitch streaks
- Adds ear margin
- Adds bear to About screen

Produces:
- Logo120.h: 120x120 1-bit C header for boot/sleep screens
- margin-icon.svg: SVG icon for web UI favicon
- Preview images at X4 Pro resolution (800x480 landscape)
- Comparison image showing before/after
"""

import os
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image, ImageOps, ImageFilter, ImageDraw, ImageEnhance
import numpy as np

SCRIPT_DIR = Path(__file__).parent
SOURCE_DIR = SCRIPT_DIR / "source"
GENERATED_DIR = SCRIPT_DIR / "generated"
DOCS_DIR = SCRIPT_DIR.parent.parent / "docs" / "branding"

# X4 Pro display: 800x480 landscape
DISPLAY_WIDTH = 800
DISPLAY_HEIGHT = 480

# margin version from platformio.margin.ini
MARGIN_VERSION = "0.1.0"

# Target logo width: 45-55% of screen width (360-440px), use 400px
TARGET_LOGO_WIDTH = 400


def load_source_image(source_path: Path) -> Image.Image:
    """Load source image as RGBA."""
    img = Image.open(source_path).convert("RGB")
    return img


def remove_glitch_streaks(rgb_array: np.ndarray) -> tuple[np.ndarray, int, int]:
    """Remove horizontal glitch/streak artifacts and return cleaned image with crop bounds.
    
    The source has horizontal glitch lines extending from the bear's sides.
    Strategy: Find the content bounding box, then crop the outer 12% from each side
    to remove the streak regions. Then mask any remaining streak pixels.
    """
    h, w = rgb_array.shape[:2]
    
    # Convert to brightness for analysis
    brightness = np.max(rgb_array, axis=2).astype(float)
    content_mask = brightness > 40
    
    # Find content bounding box
    cols = np.any(content_mask, axis=0)
    rows = np.any(content_mask, axis=1)
    
    if not np.any(cols) or not np.any(rows):
        return rgb_array.copy(), 0, 0
    
    x_indices = np.where(cols)[0]
    y_indices = np.where(rows)[0]
    
    content_left = x_indices[0]
    content_right = x_indices[-1]
    content_top = y_indices[0]
    content_bottom = y_indices[-1]
    
    content_width = content_right - content_left
    content_height = content_bottom - content_top
    
    # Crop 14% from left and right sides to remove streak regions
    # The bear body is in the central ~72% of the content width
    crop_pct = 0.14
    left_crop = int(content_width * crop_pct)
    right_crop = int(content_width * crop_pct)
    
    new_left = content_left + left_crop
    new_right = content_right - right_crop
    
    # Keep small top margin for ears
    top_margin = int(content_height * 0.02)
    new_top = max(0, content_top - top_margin)
    new_bottom = content_bottom + 5
    
    # Ensure bounds are valid
    new_left = max(0, new_left)
    new_right = min(w - 1, new_right)
    new_top = max(0, new_top)
    new_bottom = min(h - 1, new_bottom)
    
    # Crop
    result = rgb_array[new_top:new_bottom+1, new_left:new_right+1].copy()
    
    # Mask any remaining streak pixels at the edges
    result_h, result_w = result.shape[:2]
    result_brightness = np.max(result, axis=2).astype(float)
    
    edge_width = int(result_w * 0.10)  # Check outer 10% on each side
    
    for y in range(result_h):
        # Left edge
        for x in range(min(edge_width, result_w)):
            if result_brightness[y, x] < 50:
                continue
            # Count vertical extent of this pixel's connected component
            vert = 1
            for dy in range(1, 15):
                if y - dy >= 0 and result_brightness[y - dy, x] > 50:
                    vert += 1
                else:
                    break
            for dy in range(1, 15):
                if y + dy < result_h and result_brightness[y + dy, x] > 50:
                    vert += 1
                else:
                    break
            if vert < 12:  # Thin = streak
                result[y, x] = [0, 0, 0]
        
        # Right edge
        for x in range(max(0, result_w - edge_width), result_w):
            if result_brightness[y, x] < 50:
                continue
            vert = 1
            for dy in range(1, 15):
                if y - dy >= 0 and result_brightness[y - dy, x] > 50:
                    vert += 1
                else:
                    break
            for dy in range(1, 15):
                if y + dy < result_h and result_brightness[y + dy, x] > 50:
                    vert += 1
                else:
                    break
            if vert < 12:
                result[y, x] = [0, 0, 0]
    
    return result, new_left, new_top


def extract_bear_region(source: Image.Image) -> tuple[Image.Image, tuple]:
    """Extract just the bear, removing background, corner brackets, and glitch streaks.
    
    Returns cropped RGBA image and the bounding box used.
    """
    w, h = source.size
    rgb = np.array(source)
    
    # Convert to grayscale for analysis
    gray = np.mean(rgb, axis=2)
    
    # Create mask for non-background regions (bear content)
    # Background is very dark (< 35), bear content is brighter
    content_mask = gray > 35
    
    # Also exclude corner bracket regions
    corner_size = 80
    frame_margin = 25
    
    # Create corner masks
    for cy in range(h):
        for cx in range(w):
            # Top-left triangle
            if cx < corner_size and cy < corner_size and cx + cy < corner_size:
                content_mask[cy, cx] = False
            # Top-right triangle
            if cx > w - corner_size and cy < corner_size and (w - cx) + cy < corner_size:
                content_mask[cy, cx] = False
            # Bottom-left triangle
            if cx < corner_size and cy > h - corner_size and cx + (h - cy) < corner_size:
                content_mask[cy, cx] = False
            # Bottom-right triangle
            if cx > w - corner_size and cy > h - corner_size and (w - cx) + (h - cy) < corner_size:
                content_mask[cy, cx] = False
    
    # Remove edge frames
    content_mask[:frame_margin, :] = False
    content_mask[h-frame_margin:, :] = False
    content_mask[:, :frame_margin] = False
    content_mask[:, w-frame_margin:] = False
    
    # Find bounding box of content
    rows = np.any(content_mask, axis=1)
    cols = np.any(content_mask, axis=0)
    y1, y2 = np.where(rows)[0][[0, -1]]
    x1, x2 = np.where(cols)[0][[0, -1]]
    
    # Add padding
    pad = 15
    x1 = max(0, x1 - pad)
    y1 = max(0, y1 - pad)
    x2 = min(w, x2 + pad)
    y2 = min(h, y2 + pad)
    
    # Initial crop
    cropped_rgb = rgb[y1:y2, x1:x2]
    cropped_mask = content_mask[y1:y2, x1:x2]
    
    # Remove glitch streaks by cropping to main body
    cleaned_rgb, left_off, top_off = remove_glitch_streaks(cropped_rgb)
    
    # Adjust mask to match cropped region
    ch, cw = cleaned_rgb.shape[:2]
    cleaned_mask = cropped_mask[top_off:top_off+ch, left_off:left_off+cw]
    
    # Update bounding box
    x1 += left_off
    y1 += top_off
    x2 = x1 + cw
    y2 = y1 + ch
    
    # Create RGBA with alpha from mask
    cropped_rgba = np.zeros((ch, cw, 4), dtype=np.uint8)
    cropped_rgba[:, :, :3] = cleaned_rgb
    cropped_rgba[:, :, 3] = (cleaned_mask * 255).astype(np.uint8)
    
    return Image.fromarray(cropped_rgba, 'RGBA'), (x1, y1, x2, y2)


def create_light_variant(source: Image.Image, target_size: int) -> Image.Image:
    """Create light variant (dark details on white) with proper detail preservation.
    
    Key insight: treat neon outlines as INK (black strokes) and
    dark body fills as PAPER (light grey with texture).
    
    Uses 4-level greyscale with ordered dithering for fills.
    """
    cropped, _ = extract_bear_region(source)
    rgb = np.array(cropped.convert('RGB'))
    alpha = np.array(cropped.split()[3]) if cropped.mode == 'RGBA' else np.ones(rgb.shape[:2]) * 255
    
    h, w = rgb.shape[:2]
    r, g, b = rgb[:, :, 0], rgb[:, :, 1], rgb[:, :, 2]
    
    # Calculate color properties
    brightness = np.maximum(np.maximum(r, g), b).astype(float)
    min_channel = np.minimum(np.minimum(r, g), b).astype(float)
    saturation = brightness - min_channel
    gray = (r.astype(float) * 0.299 + g.astype(float) * 0.587 + b.astype(float) * 0.114)
    
    # Detect different regions:
    # 1. Neon outlines (high saturation, high brightness) - will be BLACK strokes
    # 2. Body fills (medium brightness, lower saturation) - will be LIGHT GREY
    # 3. Dark fills/shadows (low brightness) - will be MEDIUM GREY  
    # 4. Background (very low brightness) - will be WHITE
    
    # Neon detection: cyan (G+B high) or pink (R+B high), high brightness, high saturation
    is_cyan = (g > 100) & (b > 100) & (saturation > 40)
    is_pink = (r > 100) & (b > 80) & (saturation > 40)
    is_neon = (is_cyan | is_pink) & (brightness > 100)
    
    # Create output with 4 levels
    # Levels: 0 (black), 85 (dark grey), 170 (light grey), 255 (white)
    output = np.full((h, w), 255, dtype=np.uint8)  # Start white
    
    # 2x2 Bayer dithering matrix for ordered dithering
    bayer2 = np.array([[0, 2], [3, 1]]) / 4.0
    
    for y in range(h):
        for x in range(w):
            # Skip transparent pixels (background)
            if alpha[y, x] < 128:
                output[y, x] = 255  # White
                continue
            
            br = brightness[y, x]
            sat = saturation[y, x]
            gr = gray[y, x]
            
            # Very dark background → white
            if br < 40:
                output[y, x] = 255
                continue
            
            # Neon outlines → black strokes
            if is_neon[y, x]:
                # Strong neon = black, fading neon = dark grey
                if sat > 80 and br > 140:
                    output[y, x] = 0  # Black
                else:
                    output[y, x] = 85  # Dark grey
                continue
            
            # Circuit traces and details (medium-high brightness lines)
            if br > 80 and sat > 30:
                # Likely a detail line - make it visible
                if br > 150:
                    output[y, x] = 85  # Dark grey for bright details
                else:
                    output[y, x] = 128  # Medium grey
                continue
            
            # Body fills - use ordered dithering between light grey levels
            # Map brightness to output level with dithering
            # Body areas (br 40-120) should map to light-medium grey
            if br < 70:
                # Dark body area - light grey
                threshold = bayer2[y % 2, x % 2]
                normalized = (br - 40) / 30.0  # 0 to 1
                if normalized < threshold:
                    output[y, x] = 200  # Very light grey
                else:
                    output[y, x] = 170  # Light grey
            elif br < 100:
                # Medium body - medium grey with dithering
                threshold = bayer2[y % 2, x % 2]
                normalized = (br - 70) / 30.0
                if normalized < threshold:
                    output[y, x] = 170  # Light grey
                else:
                    output[y, x] = 140  # Medium-light grey
            else:
                # Brighter areas - preserve as distinct
                output[y, x] = 120  # Medium grey
    
    # Convert to PIL Image
    result = Image.fromarray(output, 'L')
    
    # Apply edge enhancement to sharpen outlines
    result = result.filter(ImageFilter.SHARPEN)
    
    # Resize to target with ear margin
    # Add 10% top margin so ears don't touch edge
    inner_size = int(target_size * 0.88)
    top_margin = int(target_size * 0.06)
    
    ratio = inner_size / max(result.width, result.height)
    new_w = int(result.width * ratio)
    new_h = int(result.height * ratio)
    
    resized = result.resize((new_w, new_h), Image.LANCZOS)
    
    # Create canvas
    canvas = Image.new("L", (target_size, target_size), 255)
    paste_x = (target_size - new_w) // 2
    paste_y = top_margin + (inner_size - new_h) // 2
    canvas.paste(resized, (paste_x, paste_y))
    
    # Clean corners
    corner_size = target_size // 8
    draw = ImageDraw.Draw(canvas)
    draw.polygon([(0, 0), (corner_size, 0), (0, corner_size)], fill=255)
    draw.polygon([(target_size, 0), (target_size - corner_size, 0), (target_size, corner_size)], fill=255)
    draw.polygon([(0, target_size), (corner_size, target_size), (0, target_size - corner_size)], fill=255)
    draw.polygon([(target_size, target_size), (target_size - corner_size, target_size), 
                  (target_size, target_size - corner_size)], fill=255)
    
    return canvas


def create_dark_variant(source: Image.Image, target_size: int) -> Image.Image:
    """Create dark variant (light details on black) with proper detail.
    
    For dark background, we want:
    - Neon outlines as WHITE strokes
    - Body texture visible as grey levels
    - Background black
    """
    cropped, _ = extract_bear_region(source)
    rgb = np.array(cropped.convert('RGB'))
    alpha = np.array(cropped.split()[3]) if cropped.mode == 'RGBA' else np.ones(rgb.shape[:2]) * 255
    
    h, w = rgb.shape[:2]
    r, g, b = rgb[:, :, 0], rgb[:, :, 1], rgb[:, :, 2]
    
    brightness = np.maximum(np.maximum(r, g), b).astype(float)
    min_channel = np.minimum(np.minimum(r, g), b).astype(float)
    saturation = brightness - min_channel
    
    # Neon detection
    is_cyan = (g > 100) & (b > 100) & (saturation > 40)
    is_pink = (r > 100) & (b > 80) & (saturation > 40)
    is_neon = (is_cyan | is_pink) & (brightness > 100)
    
    output = np.zeros((h, w), dtype=np.uint8)  # Start black
    
    for y in range(h):
        for x in range(w):
            if alpha[y, x] < 128:
                output[y, x] = 0  # Black background
                continue
            
            br = brightness[y, x]
            sat = saturation[y, x]
            
            # Very dark → black
            if br < 40:
                output[y, x] = 0
                continue
            
            # Neon → white strokes
            if is_neon[y, x]:
                if sat > 80 and br > 140:
                    output[y, x] = 255  # White
                else:
                    output[y, x] = 200  # Light grey
                continue
            
            # Details and body
            if br > 80 and sat > 30:
                output[y, x] = 200 if br > 150 else 150
                continue
            
            # Body fills - show as grey
            if br < 70:
                output[y, x] = 60  # Dark grey
            elif br < 100:
                output[y, x] = 90  # Medium-dark grey
            else:
                output[y, x] = 120  # Medium grey
    
    result = Image.fromarray(output, 'L')
    result = result.filter(ImageFilter.SHARPEN)
    
    # Resize with ear margin
    inner_size = int(target_size * 0.88)
    top_margin = int(target_size * 0.06)
    
    ratio = inner_size / max(result.width, result.height)
    new_w = int(result.width * ratio)
    new_h = int(result.height * ratio)
    
    resized = result.resize((new_w, new_h), Image.LANCZOS)
    
    canvas = Image.new("L", (target_size, target_size), 0)  # Black background
    paste_x = (target_size - new_w) // 2
    paste_y = top_margin + (inner_size - new_h) // 2
    canvas.paste(resized, (paste_x, paste_y))
    
    # Clean corners (black)
    corner_size = target_size // 8
    draw = ImageDraw.Draw(canvas)
    draw.polygon([(0, 0), (corner_size, 0), (0, corner_size)], fill=0)
    draw.polygon([(target_size, 0), (target_size - corner_size, 0), (target_size, corner_size)], fill=0)
    draw.polygon([(0, target_size), (corner_size, target_size), (0, target_size - corner_size)], fill=0)
    draw.polygon([(target_size, target_size), (target_size - corner_size, target_size), 
                  (target_size, target_size - corner_size)], fill=0)
    
    return canvas


def quantize_to_4_levels(img: Image.Image, for_dark_bg: bool = False) -> Image.Image:
    """Quantize greyscale image to 4 levels for e-ink display.
    
    Levels: 0, 85, 170, 255
    """
    arr = np.array(img)
    
    def quantize_value(v):
        if v < 43:
            return 0
        elif v < 128:
            return 85
        elif v < 213:
            return 170
        else:
            return 255
    
    vectorized = np.vectorize(quantize_value)
    quantized = vectorized(arr).astype(np.uint8)
    
    return Image.fromarray(quantized, 'L')


def create_1bit_logo(logo: Image.Image, dark_on_white: bool = True) -> Image.Image:
    """Convert greyscale logo to clean 1-bit for header file.
    
    Uses adaptive threshold to preserve detail.
    """
    arr = np.array(logo)
    
    if dark_on_white:
        # Dark on white: values < threshold become black (0)
        threshold = 160
        binary = (arr > threshold).astype(np.uint8) * 255
    else:
        # Light on dark: values > threshold become white (255)
        threshold = 80
        binary = (arr > threshold).astype(np.uint8) * 255
    
    return Image.fromarray(binary, 'L')


def image_to_c_header(img: Image.Image, array_name: str, width: int, height: int) -> str:
    """Convert image to C header format for CrossPoint's drawImage().
    
    Format: 1-bit packed, MSB first, 1=white/transparent, 0=black/drawn.
    """
    if img.size != (width, height):
        img = img.resize((width, height), Image.LANCZOS)
    
    if img.mode != "1":
        img = img.point(lambda x: 255 if x > 128 else 0, "1")
    
    pixels = list(img.getdata())
    
    packed = []
    for y in range(height):
        for x in range(0, width, 8):
            byte = 0
            for b in range(8):
                if x + b < width:
                    pixel = pixels[y * width + x + b]
                    bit = 1 if pixel else 0
                    byte |= (bit << (7 - b))
            packed.append(byte)
    
    lines = ['#pragma once', '#include <cstdint>', '', f'// Image dimensions: {width}x{height}']
    lines.append(f'static const uint8_t {array_name}[] = {{')
    
    for i in range(0, len(packed), 19):
        chunk = packed[i:i + 19]
        hex_values = ', '.join(f'0x{b:02x}' for b in chunk)
        lines.append(f'    {hex_values},')
    
    if lines[-1].endswith(','):
        lines[-1] = lines[-1][:-1] + '};'
    else:
        lines.append('};')
    
    return '\n'.join(lines)


def create_svg_favicon(source: Image.Image, size: int = 128) -> str:
    """Create SVG favicon with proper detail preservation."""
    import base64
    import io
    
    # Use light variant for favicon (dark details on white/transparent)
    logo = create_light_variant(source, size)
    
    # Quantize to 4 levels
    logo = quantize_to_4_levels(logo)
    
    # Create RGBA with transparency for white areas
    rgba = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    for y in range(size):
        for x in range(size):
            gray = logo.getpixel((x, y))
            if gray < 250:  # Not white background
                alpha = min(255, int((255 - gray) * 1.2))
                rgba.putpixel((x, y), (0, 0, 0, alpha))
    
    buf = io.BytesIO()
    rgba.save(buf, format='PNG')
    b64 = base64.b64encode(buf.getvalue()).decode('ascii')
    
    svg = f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {size} {size}" width="{size}" height="{size}">
  <image href="data:image/png;base64,{b64}" width="{size}" height="{size}"/>
</svg>'''
    
    return svg


def create_preview_splash(logo: Image.Image, logo_size: int) -> Image.Image:
    """Create preview of boot splash screen at device resolution."""
    screen = Image.new("L", (DISPLAY_WIDTH, DISPLAY_HEIGHT), 255)
    draw = ImageDraw.Draw(screen)
    
    logo_x = (DISPLAY_WIDTH - logo_size) // 2
    logo_y = (DISPLAY_HEIGHT - logo_size) // 2 - 50
    
    logo_resized = logo.resize((logo_size, logo_size), Image.LANCZOS)
    screen.paste(logo_resized, (logo_x, logo_y))
    
    text_y = logo_y + logo_size + 20
    draw.text((DISPLAY_WIDTH // 2, text_y), "margin", fill=0, anchor="mt")
    draw.text((DISPLAY_WIDTH // 2, text_y + 30), "Booting...", fill=0, anchor="mt")
    draw.text((DISPLAY_WIDTH // 2, DISPLAY_HEIGHT - 30), f"v{MARGIN_VERSION}", fill=0, anchor="mt")
    
    return screen


def create_preview_sleep(logo: Image.Image, logo_size: int, dark: bool = True) -> Image.Image:
    """Create preview of sleep screen at device resolution."""
    bg_color = 0 if dark else 255
    fg_color = 255 if dark else 0
    
    screen = Image.new("L", (DISPLAY_WIDTH, DISPLAY_HEIGHT), bg_color)
    draw = ImageDraw.Draw(screen)
    
    logo_x = (DISPLAY_WIDTH - logo_size) // 2
    logo_y = (DISPLAY_HEIGHT - logo_size) // 2 - 30
    
    logo_resized = logo.resize((logo_size, logo_size), Image.LANCZOS)
    screen.paste(logo_resized, (logo_x, logo_y))
    
    text_y = logo_y + logo_size + 20
    draw.text((DISPLAY_WIDTH // 2, text_y), "margin", fill=fg_color, anchor="mt")
    draw.text((DISPLAY_WIDTH // 2, text_y + 30), "Sleeping...", fill=fg_color, anchor="mt")
    
    return screen


def create_preview_about(logo_small: Image.Image, logo_size: int = 80) -> Image.Image:
    """Create preview of About screen with small bear logo."""
    screen = Image.new("L", (DISPLAY_WIDTH, DISPLAY_HEIGHT), 255)
    draw = ImageDraw.Draw(screen)
    
    # Header with logo
    draw.rectangle([0, 0, DISPLAY_WIDTH, 60], fill=240)
    
    # Small logo in header
    small_logo = logo_small.resize((40, 40), Image.LANCZOS)
    screen.paste(small_logo, (20, 10))
    
    draw.text((70, 30), "About margin", fill=0, anchor="lm")
    
    # Info lines
    info = [
        ("Device", "Xteink X4 Pro"),
        ("Firmware", f"margin v{MARGIN_VERSION}"),
        ("Resolution", f"{DISPLAY_WIDTH}x{DISPLAY_HEIGHT}"),
        ("Display", "SSD1677"),
        ("Touch", "GT911"),
    ]
    
    y = 90
    for label, value in info:
        draw.text((40, y), label, fill=100)
        draw.text((DISPLAY_WIDTH - 40, y), value, fill=0, anchor="rt")
        draw.line([(40, y + 35), (DISPLAY_WIDTH - 40, y + 35)], fill=220)
        y += 55
    
    # Centered bear logo at bottom
    logo_bottom = logo_small.resize((logo_size, logo_size), Image.LANCZOS)
    logo_x = (DISPLAY_WIDTH - logo_size) // 2
    logo_y = DISPLAY_HEIGHT - logo_size - 20
    screen.paste(logo_bottom, (logo_x, logo_y))
    
    return screen


def create_preview_favicon(logo: Image.Image) -> Image.Image:
    """Create preview showing the favicon in web browser context."""
    preview = Image.new("RGB", (600, 150), (245, 245, 245))
    draw = ImageDraw.Draw(preview)
    
    draw.rectangle([10, 10, 250, 45], fill=(255, 255, 255), outline=(200, 200, 200))
    
    # Create small favicon
    favicon = logo.resize((24, 24), Image.LANCZOS)
    favicon_rgb = Image.new("RGB", (32, 32), (255, 255, 255))
    favicon_l = favicon.convert("L")
    favicon_rgb.paste(favicon_l, (4, 4))
    
    preview.paste(favicon_rgb, (20, 12))
    draw.text((60, 20), "margin - Web Interface", fill=(50, 50, 50))
    
    draw.rectangle([10, 55, 590, 85], fill=(255, 255, 255), outline=(200, 200, 200))
    draw.text((20, 62), "https://192.168.1.100/", fill=(100, 100, 100))
    
    draw.rectangle([10, 95, 590, 140], fill=(250, 250, 250), outline=(220, 220, 220))
    draw.text((300, 117), "margin Web Interface Preview", fill=(150, 150, 150), anchor="mm")
    
    return preview


def create_comparison_image(light_old: Image.Image, light_new: Image.Image, 
                           dark_new: Image.Image, size: int = 400) -> Image.Image:
    """Create side-by-side comparison of light before/after and dark variant."""
    # Canvas: 3 images side by side with labels
    margin = 20
    label_height = 40
    total_width = size * 3 + margin * 4
    total_height = size + margin * 2 + label_height
    
    comparison = Image.new("RGB", (total_width, total_height), (240, 240, 240))
    draw = ImageDraw.Draw(comparison)
    
    # Resize images
    light_old_resized = light_old.resize((size, size), Image.LANCZOS)
    light_new_resized = light_new.resize((size, size), Image.LANCZOS)
    dark_new_resized = dark_new.resize((size, size), Image.LANCZOS)
    
    # Position images
    y = label_height
    
    # Light (before)
    x1 = margin
    comparison.paste(Image.merge("RGB", (light_old_resized, light_old_resized, light_old_resized)), (x1, y))
    draw.text((x1 + size // 2, 20), "Light (BEFORE)", fill=(100, 100, 100), anchor="mt")
    
    # Light (after)
    x2 = margin * 2 + size
    comparison.paste(Image.merge("RGB", (light_new_resized, light_new_resized, light_new_resized)), (x2, y))
    draw.text((x2 + size // 2, 20), "Light (AFTER)", fill=(0, 100, 0), anchor="mt")
    
    # Dark
    x3 = margin * 3 + size * 2
    comparison.paste(Image.merge("RGB", (dark_new_resized, dark_new_resized, dark_new_resized)), (x3, y))
    draw.text((x3 + size // 2, 20), "Dark variant", fill=(100, 100, 100), anchor="mt")
    
    return comparison


def load_old_light_logo() -> Image.Image:
    """Load the old light logo for comparison (if it exists)."""
    old_path = GENERATED_DIR / "margin-logo-400-1bit.png"
    if old_path.exists():
        return Image.open(old_path).convert("L")
    return None


def main():
    print("=== margin Branding Generator v3 ===")
    print(f"Target resolution: {DISPLAY_WIDTH}x{DISPLAY_HEIGHT} (landscape)")
    print(f"margin version: {MARGIN_VERSION}")
    print()
    
    GENERATED_DIR.mkdir(parents=True, exist_ok=True)
    DOCS_DIR.mkdir(parents=True, exist_ok=True)
    
    # Load old logo for comparison before we overwrite
    old_light_logo = load_old_light_logo()
    
    # Load source
    source_path = SOURCE_DIR / "od3sa-cyber-bear.jpg"
    print(f"Loading source: {source_path}")
    source = load_source_image(source_path)
    print(f"Source size: {source.size}")
    
    # Generate light variant (dark details on white)
    print(f"\nGenerating {TARGET_LOGO_WIDTH}px light variant...")
    logo_light = create_light_variant(source, TARGET_LOGO_WIDTH)
    logo_light_4level = quantize_to_4_levels(logo_light)
    logo_light_path = GENERATED_DIR / "margin-logo-400-light.png"
    logo_light_4level.save(logo_light_path)
    print(f"Saved: {logo_light_path}")
    
    # Generate dark variant (light details on black)
    print(f"Generating {TARGET_LOGO_WIDTH}px dark variant...")
    logo_dark = create_dark_variant(source, TARGET_LOGO_WIDTH)
    logo_dark_4level = quantize_to_4_levels(logo_dark)
    logo_dark_path = GENERATED_DIR / "margin-logo-400-dark.png"
    logo_dark_4level.save(logo_dark_path)
    print(f"Saved: {logo_dark_path}")
    
    # Generate 1-bit version for Logo120.h
    print("\nGenerating Logo120.h (120x120 1-bit)...")
    logo_120_light = create_light_variant(source, 120)
    logo_120_1bit = create_1bit_logo(logo_120_light, dark_on_white=True)
    header_content = image_to_c_header(logo_120_1bit, "Logo120", 120, 120)
    header_path = GENERATED_DIR / "Logo120.h"
    header_path.write_text(header_content)
    print(f"Saved: {header_path}")
    
    # Save 120px PNG preview
    logo_120_1bit.save(GENERATED_DIR / "margin-logo-120.png")
    
    # Generate SVG favicon
    print("\nGenerating SVG favicon...")
    svg_content = create_svg_favicon(source, 128)
    svg_path = GENERATED_DIR / "margin-icon.svg"
    svg_path.write_text(svg_content)
    print(f"Saved: {svg_path}")
    
    # Generate previews
    print("\nGenerating preview images at 800x480...")
    
    # Splash screen (light background)
    preview_splash = create_preview_splash(logo_light_4level, TARGET_LOGO_WIDTH)
    splash_path = DOCS_DIR / "preview-splash-screen.png"
    preview_splash.save(splash_path)
    print(f"Saved: {splash_path}")
    
    # Dark sleep screen
    preview_sleep_dark = create_preview_sleep(logo_dark_4level, TARGET_LOGO_WIDTH, dark=True)
    sleep_dark_path = DOCS_DIR / "preview-sleep-screen-dark.png"
    preview_sleep_dark.save(sleep_dark_path)
    print(f"Saved: {sleep_dark_path}")
    
    # Light sleep screen
    preview_sleep_light = create_preview_sleep(logo_light_4level, TARGET_LOGO_WIDTH, dark=False)
    sleep_light_path = DOCS_DIR / "preview-sleep-screen-light.png"
    preview_sleep_light.save(sleep_light_path)
    print(f"Saved: {sleep_light_path}")
    
    # About screen with bear
    preview_about = create_preview_about(logo_light_4level, 80)
    about_path = DOCS_DIR / "preview-about-screen.png"
    preview_about.save(about_path)
    print(f"Saved: {about_path}")
    
    # Favicon preview
    preview_favicon = create_preview_favicon(logo_light_4level)
    favicon_path = DOCS_DIR / "preview-web-favicon.png"
    preview_favicon.save(favicon_path)
    print(f"Saved: {favicon_path}")
    
    # Comparison image
    print("\nGenerating comparison image...")
    if old_light_logo is not None:
        comparison = create_comparison_image(old_light_logo, logo_light_4level, logo_dark_4level)
    else:
        # Use same image as "before" if no old version exists
        comparison = create_comparison_image(logo_light_4level, logo_light_4level, logo_dark_4level)
    comparison_path = DOCS_DIR / "comparison-light-dark.png"
    comparison.save(comparison_path)
    print(f"Saved: {comparison_path}")
    
    # Keep backward compatibility - copy light variant to old filenames
    logo_light_4level.save(GENERATED_DIR / "margin-logo-400.png")
    logo_light_4level.save(GENERATED_DIR / "margin-logo-400-1bit.png")
    logo_dark_4level.save(GENERATED_DIR / "margin-logo-400-inverted.png")
    
    print("\n=== Generation complete! ===")
    print(f"Generated assets: {GENERATED_DIR}")
    print(f"Preview images: {DOCS_DIR}")
    
    return 0


if __name__ == "__main__":
    sys.exit(main())
