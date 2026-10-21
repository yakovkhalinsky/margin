#!/usr/bin/env python3
"""Generate margin branding assets from the od3sa cyber bear source image.

Produces:
- MarginLogo.h: Large logo C header for boot/sleep screens (45-55% screen width)
- Logo120.h: Legacy 120x120 fallback
- margin-icon.svg: SVG icon for web UI favicon
- Preview images at X4 Pro resolution (800x480 landscape)
"""

import os
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image, ImageOps, ImageFilter, ImageDraw, ImageEnhance

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


def load_and_crop_bear(source_path: Path) -> Image.Image:
    """Load the cyber bear image and tightly crop to remove the dark rounded-rect background.
    
    The source has corner bracket artifacts from the rounded-square frame.
    We explicitly mask out the corner regions and crop tightly to the bear content.
    """
    img = Image.open(source_path).convert("RGB")
    w, h = img.size
    
    # Convert to grayscale for analysis
    gray = img.convert("L")
    
    # Step 1: Black out the corner bracket regions
    # The brackets are in the corners, extending about 80 pixels inward
    # They form L-shaped marks in each corner
    corner_mask_size = 80
    
    # Create a working copy of gray
    gray_masked = gray.copy()
    draw = ImageDraw.Draw(gray_masked)
    
    # Black out corner triangular/rectangular regions where brackets appear
    # Top-left corner
    draw.polygon([(0, 0), (corner_mask_size, 0), (0, corner_mask_size)], fill=0)
    # Top-right corner  
    draw.polygon([(w, 0), (w - corner_mask_size, 0), (w, corner_mask_size)], fill=0)
    # Bottom-left corner
    draw.polygon([(0, h), (corner_mask_size, h), (0, h - corner_mask_size)], fill=0)
    # Bottom-right corner
    draw.polygon([(w, h), (w - corner_mask_size, h), (w, h - corner_mask_size)], fill=0)
    
    # Step 2: Also mask out the outer frame edges (about 20 pixels from each edge)
    frame_margin = 25
    draw.rectangle([0, 0, w, frame_margin], fill=0)  # top edge
    draw.rectangle([0, h - frame_margin, w, h], fill=0)  # bottom edge
    draw.rectangle([0, 0, frame_margin, h], fill=0)  # left edge
    draw.rectangle([w - frame_margin, 0, w, h], fill=0)  # right edge
    
    # Step 3: Threshold to find the actual bear content
    threshold = 35
    mask = Image.eval(gray_masked, lambda x: 255 if x > threshold else 0)
    
    # Apply morphological operations to clean up
    mask = mask.filter(ImageFilter.MaxFilter(5))  # dilate to connect
    mask = mask.filter(ImageFilter.MinFilter(5))  # erode to clean
    
    # Step 4: Get tight bounding box
    bbox = mask.getbbox()
    if not bbox:
        # Fallback: use center crop
        margin = 100
        bbox = (margin, margin, w - margin, h - margin)
    
    x1, y1, x2, y2 = bbox
    
    # Add small padding
    pad = 10
    x1 = max(0, x1 - pad)
    y1 = max(0, y1 - pad)
    x2 = min(w, x2 + pad)
    y2 = min(h, y2 + pad)
    
    # Step 5: Crop and create final mask
    cropped = img.crop((x1, y1, x2, y2))
    cropped_gray = gray.crop((x1, y1, x2, y2))
    
    # Create alpha mask: transparent where very dark (background)
    alpha = Image.eval(cropped_gray, lambda x: 255 if x > 30 else 0)
    
    # Additional corner cleanup on the cropped image
    cw, ch = cropped.size
    corner_clean = 30  # Clean corners of cropped image too
    alpha_draw = ImageDraw.Draw(alpha)
    alpha_draw.polygon([(0, 0), (corner_clean, 0), (0, corner_clean)], fill=0)
    alpha_draw.polygon([(cw, 0), (cw - corner_clean, 0), (cw, corner_clean)], fill=0)
    alpha_draw.polygon([(0, ch), (corner_clean, ch), (0, ch - corner_clean)], fill=0)
    alpha_draw.polygon([(cw, ch), (cw - corner_clean, ch), (cw, ch - corner_clean)], fill=0)
    
    # Create RGBA
    rgba = cropped.convert("RGBA")
    r, g, b, _ = rgba.split()
    rgba = Image.merge("RGBA", (r, g, b, alpha))
    
    return rgba


def extract_neon_outlines(source: Image.Image) -> Image.Image:
    """Extract the bright neon outlines/circuit lines from the cyber bear.
    
    Returns a high-contrast image where neon becomes black strokes.
    Also ensures corners are clean (white/transparent).
    """
    w, h = source.size
    
    # Convert to RGB if RGBA, using alpha to mask background
    if source.mode == "RGBA":
        # Composite on white background, respecting alpha
        bg = Image.new("RGB", source.size, (255, 255, 255))
        bg.paste(source, mask=source.split()[3])
        rgb = bg
        alpha = source.split()[3]
    else:
        rgb = source.convert("RGB")
        alpha = None
    
    r, g, b = rgb.split()
    
    r_data = list(r.getdata())
    g_data = list(g.getdata())
    b_data = list(b.getdata())
    alpha_data = list(alpha.getdata()) if alpha else [255] * len(r_data)
    
    mask_data = []
    for i in range(len(r_data)):
        # Skip if transparent (from alpha mask - these are corners/background)
        if alpha_data[i] < 128:
            mask_data.append(255)  # White/background
            continue
            
        rv, gv, bv = r_data[i], g_data[i], b_data[i]
        brightness = max(rv, gv, bv)
        
        # Strong neon detection - look for saturated bright colors
        is_cyan = gv > 120 and bv > 120 and brightness > 150
        is_pink = rv > 120 and bv > 100 and brightness > 140
        is_bright = brightness > 100
        
        # Weight by saturation (how much the brightest channel exceeds others)
        saturation = brightness - min(rv, gv, bv)
        
        if (is_cyan or is_pink) and saturation > 50:
            # Strong neon - full black
            mask_data.append(0)
        elif is_bright and saturation > 30:
            # Moderate neon - dark gray
            mask_data.append(64)
        elif brightness > 80:
            # Subtle detail - light gray
            mask_data.append(160)
        else:
            # Background - white
            mask_data.append(255)
    
    result = Image.new("L", rgb.size)
    result.putdata(mask_data)
    
    # Extra corner cleanup - ensure corners are white
    corner_size = max(w, h) // 15  # ~7% of image size
    draw = ImageDraw.Draw(result)
    draw.polygon([(0, 0), (corner_size, 0), (0, corner_size)], fill=255)
    draw.polygon([(w, 0), (w - corner_size, 0), (w, corner_size)], fill=255)
    draw.polygon([(0, h), (corner_size, h), (0, h - corner_size)], fill=255)
    draw.polygon([(w, h), (w - corner_size, h), (w, h - corner_size)], fill=255)
    
    return result


def trace_to_svg(img: Image.Image, output_path: Path) -> bool:
    """Use potrace to convert bitmap to clean SVG vector."""
    try:
        with tempfile.NamedTemporaryFile(suffix='.pbm', delete=False) as tmp:
            # Convert to 1-bit PBM for potrace
            # Threshold to binary
            binary = img.point(lambda x: 0 if x < 128 else 255, '1')
            binary.save(tmp.name)
            
            # Run potrace
            result = subprocess.run(
                ['potrace', '-s', '-o', str(output_path), tmp.name],
                capture_output=True,
                timeout=30
            )
            
            os.unlink(tmp.name)
            return result.returncode == 0
    except Exception as e:
        print(f"Warning: potrace failed: {e}")
        return False


def create_lineart_logo(source: Image.Image, target_size: int, 
                        dark_on_white: bool = True) -> Image.Image:
    """Create a clean line-art logo with crisp edges.
    
    Uses edge detection and thresholding to extract clean outlines,
    with optional greyscale fills for accents.
    """
    # Extract neon outlines
    outlines = extract_neon_outlines(source)
    
    # Resize with high quality
    ratio = target_size / max(outlines.width, outlines.height)
    new_w = int(outlines.width * ratio)
    new_h = int(outlines.height * ratio)
    
    # Use LANCZOS for best quality resize
    resized = outlines.resize((new_w, new_h), Image.LANCZOS)
    
    # Apply edge enhancement for crisper lines
    resized = resized.filter(ImageFilter.SHARPEN)
    
    # Enhance contrast
    enhancer = ImageEnhance.Contrast(resized)
    resized = enhancer.enhance(1.5)
    
    # Create square canvas
    canvas = Image.new("L", (target_size, target_size), 255 if dark_on_white else 0)
    
    # Center the image
    paste_x = (target_size - new_w) // 2
    paste_y = (target_size - new_h) // 2
    
    if dark_on_white:
        # Dark outlines on white
        canvas.paste(resized, (paste_x, paste_y))
    else:
        # Invert for white outlines on dark
        inverted = ImageOps.invert(resized)
        canvas.paste(inverted, (paste_x, paste_y))
    
    return canvas


def create_4level_greyscale_logo(source: Image.Image, target_size: int,
                                  dark_on_white: bool = True) -> Image.Image:
    """Create a 4-level greyscale logo optimized for e-ink.
    
    Uses quantization to 4 levels: black, dark grey, light grey, white.
    Outlines stay solid black/white, fills use grey levels.
    Ensures corners are completely clean by scaling content within safe area.
    """
    # Extract neon as greyscale with levels
    outlines = extract_neon_outlines(source)
    
    # Calculate safe inner area - smaller sizes need more margin
    if target_size <= 150:
        margin_ratio = 0.58  # Content fills 58% for small logos
    else:
        margin_ratio = 0.90
    
    inner_size = int(target_size * margin_ratio)
    
    # Resize to fit in inner area
    ratio = inner_size / max(outlines.width, outlines.height)
    new_w = int(outlines.width * ratio)
    new_h = int(outlines.height * ratio)
    resized = outlines.resize((new_w, new_h), Image.LANCZOS)
    
    # Quantize to 4 levels
    # Levels: 0 (black), 85 (dark grey), 170 (light grey), 255 (white)
    def quantize_4level(val):
        if val < 43:
            return 0
        elif val < 128:
            return 85
        elif val < 213:
            return 170
        else:
            return 255
    
    quantized = Image.eval(resized, quantize_4level)
    
    # Create square canvas
    bg_color = 255 if dark_on_white else 0
    canvas = Image.new("L", (target_size, target_size), bg_color)
    
    # Center the content
    paste_x = (target_size - new_w) // 2
    paste_y = (target_size - new_h) // 2
    
    if dark_on_white:
        canvas.paste(quantized, (paste_x, paste_y))
    else:
        inverted = ImageOps.invert(quantized)
        canvas.paste(inverted, (paste_x, paste_y))
    
    # Corner cleanup - larger for small sizes
    if target_size <= 150:
        corner_size = target_size // 4
    else:
        corner_size = target_size // 8
    
    draw = ImageDraw.Draw(canvas)
    draw.polygon([(0, 0), (corner_size, 0), (0, corner_size)], fill=bg_color)
    draw.polygon([(target_size, 0), (target_size - corner_size, 0), (target_size, corner_size)], fill=bg_color)
    draw.polygon([(0, target_size), (corner_size, target_size), (0, target_size - corner_size)], fill=bg_color)
    draw.polygon([(target_size, target_size), (target_size - corner_size, target_size), 
                  (target_size, target_size - corner_size)], fill=bg_color)
    
    return canvas


def create_clean_1bit_logo(source: Image.Image, target_size: int) -> Image.Image:
    """Create a clean 1-bit logo without dithering noise.
    
    Uses hard threshold after contrast enhancement for crisp edges.
    Ensures corners are completely clean (white) by scaling content
    to fit within a safe inner region.
    """
    outlines = extract_neon_outlines(source)
    
    # Calculate safe inner area to keep corners clean
    # Smaller sizes need more margin because corner triangles are proportionally larger
    if target_size <= 150:
        margin_ratio = 0.58  # Content fills 58% for small logos (very generous corner margins)
    else:
        margin_ratio = 0.90  # Content fills 90% for larger logos
    
    inner_size = int(target_size * margin_ratio)
    
    # Resize to fit in inner area
    ratio = inner_size / max(outlines.width, outlines.height)
    new_w = int(outlines.width * ratio)
    new_h = int(outlines.height * ratio)
    resized = outlines.resize((new_w, new_h), Image.LANCZOS)
    
    # Enhance contrast strongly
    enhancer = ImageEnhance.Contrast(resized)
    resized = enhancer.enhance(2.0)
    
    # Hard threshold - no dithering
    threshold = 140
    binary = Image.eval(resized, lambda x: 255 if x > threshold else 0)
    
    # Create square canvas
    canvas = Image.new("L", (target_size, target_size), 255)
    
    # Center the content
    paste_x = (target_size - new_w) // 2
    paste_y = (target_size - new_h) // 2
    canvas.paste(binary, (paste_x, paste_y))
    
    # Corner cleanup - larger for small sizes
    if target_size <= 150:
        corner_size = target_size // 4  # 25% corner triangles for small logos
    else:
        corner_size = target_size // 8  # 12.5% for larger
    
    draw = ImageDraw.Draw(canvas)
    draw.polygon([(0, 0), (corner_size, 0), (0, corner_size)], fill=255)
    draw.polygon([(target_size, 0), (target_size - corner_size, 0), (target_size, corner_size)], fill=255)
    draw.polygon([(0, target_size), (corner_size, target_size), (0, target_size - corner_size)], fill=255)
    draw.polygon([(target_size, target_size), (target_size - corner_size, target_size), 
                  (target_size, target_size - corner_size)], fill=255)
    
    return canvas


def image_to_c_header(img: Image.Image, array_name: str, width: int, height: int) -> str:
    """Convert image to C header format for CrossPoint's drawImage().
    
    Format: 1-bit packed, MSB first, 1=white/transparent, 0=black/drawn.
    """
    # Resize to target dimensions
    if img.size != (width, height):
        img = img.resize((width, height), Image.LANCZOS)
    
    # Convert to 1-bit
    if img.mode != "1":
        img = img.point(lambda x: 255 if x > 128 else 0, "1")
    
    pixels = list(img.getdata())
    
    # Pack into bytes: 1=white, 0=black, MSB first
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
    
    # Format as C array
    lines = ['#pragma once', '#include <cstdint>', '', f'// Image dimensions: {width}x{height}']
    lines.append(f'static const uint8_t {array_name}[] = {{')
    
    # 19 bytes per line (matching CrossPoint format)
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
    """Create a simple SVG favicon from the bear image."""
    import base64
    import io
    
    # Process the source for favicon
    outlines = extract_neon_outlines(source)
    
    # Resize
    ratio = (size * 0.9) / max(outlines.width, outlines.height)
    new_w = int(outlines.width * ratio)
    new_h = int(outlines.height * ratio)
    resized = outlines.resize((new_w, new_h), Image.LANCZOS)
    
    # Create canvas with transparent background
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    
    # Convert outlines to RGBA with black as the color
    paste_x = (size - new_w) // 2
    paste_y = (size - new_h) // 2
    
    # Create RGBA version: black where outline is dark, transparent elsewhere
    outline_rgba = Image.new("RGBA", resized.size, (0, 0, 0, 0))
    for y in range(resized.height):
        for x in range(resized.width):
            gray = resized.getpixel((x, y))
            if gray < 200:  # Not background
                alpha = 255 - gray  # Darker = more opaque
                outline_rgba.putpixel((x, y), (0, 0, 0, min(255, int(alpha * 1.5))))
    
    canvas.paste(outline_rgba, (paste_x, paste_y), outline_rgba)
    
    # Encode as PNG data URI
    buf = io.BytesIO()
    canvas.save(buf, format='PNG')
    b64 = base64.b64encode(buf.getvalue()).decode('ascii')
    
    svg = f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {size} {size}" width="{size}" height="{size}">
  <image href="data:image/png;base64,{b64}" width="{size}" height="{size}"/>
</svg>'''
    
    return svg


def create_preview_splash(logo: Image.Image, logo_size: int) -> Image.Image:
    """Create preview of boot splash screen at device resolution."""
    screen = Image.new("L", (DISPLAY_WIDTH, DISPLAY_HEIGHT), 255)
    draw = ImageDraw.Draw(screen)
    
    # Center logo (slightly above center to leave room for text)
    logo_x = (DISPLAY_WIDTH - logo_size) // 2
    logo_y = (DISPLAY_HEIGHT - logo_size) // 2 - 50
    
    # Resize logo to target size
    logo_resized = logo.resize((logo_size, logo_size), Image.LANCZOS)
    screen.paste(logo_resized, (logo_x, logo_y))
    
    # Add "margin" title below logo
    text_y = logo_y + logo_size + 20
    draw.text((DISPLAY_WIDTH // 2, text_y), "margin", fill=0, anchor="mt")
    
    # "Booting..." below title
    draw.text((DISPLAY_WIDTH // 2, text_y + 30), "Booting...", fill=0, anchor="mt")
    
    # Version at bottom
    draw.text((DISPLAY_WIDTH // 2, DISPLAY_HEIGHT - 30), f"v{MARGIN_VERSION}", fill=0, anchor="mt")
    
    return screen


def create_preview_sleep(logo: Image.Image, logo_size: int, dark: bool = True) -> Image.Image:
    """Create preview of sleep screen at device resolution."""
    bg_color = 0 if dark else 255
    fg_color = 255 if dark else 0
    
    screen = Image.new("L", (DISPLAY_WIDTH, DISPLAY_HEIGHT), bg_color)
    draw = ImageDraw.Draw(screen)
    
    # Center logo
    logo_x = (DISPLAY_WIDTH - logo_size) // 2
    logo_y = (DISPLAY_HEIGHT - logo_size) // 2 - 30
    
    # Resize and invert if needed
    logo_resized = logo.resize((logo_size, logo_size), Image.LANCZOS)
    if dark:
        logo_resized = ImageOps.invert(logo_resized)
    
    screen.paste(logo_resized, (logo_x, logo_y))
    
    # "margin" and "Sleeping..." text
    text_y = logo_y + logo_size + 20
    draw.text((DISPLAY_WIDTH // 2, text_y), "margin", fill=fg_color, anchor="mt")
    draw.text((DISPLAY_WIDTH // 2, text_y + 30), "Sleeping...", fill=fg_color, anchor="mt")
    
    return screen


def create_preview_about() -> Image.Image:
    """Create preview of About screen at device resolution."""
    screen = Image.new("L", (DISPLAY_WIDTH, DISPLAY_HEIGHT), 255)
    draw = ImageDraw.Draw(screen)
    
    # Header
    draw.rectangle([0, 0, DISPLAY_WIDTH, 50], fill=240)
    draw.text((DISPLAY_WIDTH // 2, 25), "About", fill=0, anchor="mm")
    
    # Info lines
    info = [
        ("Device", "Xteink X4 Pro"),
        ("Firmware", f"margin v{MARGIN_VERSION}"),
        ("Resolution", f"{DISPLAY_WIDTH}x{DISPLAY_HEIGHT}"),
        ("Display", "SSD1677"),
        ("Touch", "GT911"),
    ]
    
    y = 80
    for label, value in info:
        draw.text((40, y), label, fill=100)
        draw.text((DISPLAY_WIDTH - 40, y), value, fill=0, anchor="rt")
        draw.line([(40, y + 30), (DISPLAY_WIDTH - 40, y + 30)], fill=220)
        y += 50
    
    return screen


def create_preview_favicon(logo: Image.Image) -> Image.Image:
    """Create preview showing the favicon in web browser context."""
    # Create a mock browser tab bar
    preview = Image.new("RGB", (600, 150), (245, 245, 245))
    draw = ImageDraw.Draw(preview)
    
    # Tab background
    draw.rectangle([10, 10, 250, 45], fill=(255, 255, 255), outline=(200, 200, 200))
    
    # Favicon (32x32)
    outlines = extract_neon_outlines(logo)
    ratio = 24 / max(outlines.width, outlines.height)
    favicon = outlines.resize((int(outlines.width * ratio), int(outlines.height * ratio)), Image.LANCZOS)
    
    # Create RGB version of favicon
    favicon_rgb = Image.new("RGB", (32, 32), (255, 255, 255))
    paste_x = (32 - favicon.width) // 2
    paste_y = (32 - favicon.height) // 2
    favicon_l = favicon.convert("L")
    favicon_rgb.paste(favicon_l, (paste_x, paste_y))
    
    preview.paste(favicon_rgb, (20, 14))
    
    # Tab title
    draw.text((60, 20), "margin - Web Interface", fill=(50, 50, 50))
    
    # URL bar below
    draw.rectangle([10, 55, 590, 85], fill=(255, 255, 255), outline=(200, 200, 200))
    draw.text((20, 62), "https://192.168.1.100/", fill=(100, 100, 100))
    
    # Content area label
    draw.rectangle([10, 95, 590, 140], fill=(250, 250, 250), outline=(220, 220, 220))
    draw.text((300, 117), "margin Web Interface Preview", fill=(150, 150, 150), anchor="mm")
    
    return preview


def main():
    print("=== margin Branding Generator v2 ===")
    print(f"Target resolution: {DISPLAY_WIDTH}x{DISPLAY_HEIGHT} (landscape)")
    print(f"margin version: {MARGIN_VERSION}")
    print()
    
    # Ensure directories exist
    GENERATED_DIR.mkdir(parents=True, exist_ok=True)
    DOCS_DIR.mkdir(parents=True, exist_ok=True)
    
    # Load and crop source
    source_path = SOURCE_DIR / "od3sa-cyber-bear.jpg"
    print(f"Loading source: {source_path}")
    cropped = load_and_crop_bear(source_path)
    print(f"Cropped to: {cropped.size} (corners removed)")
    
    # Save cropped for reference
    cropped_path = GENERATED_DIR / "cropped-bear.png"
    cropped.save(cropped_path)
    print(f"Saved: {cropped_path}")
    
    # Generate large logo (400px - 50% of 800px width)
    print(f"\nGenerating {TARGET_LOGO_WIDTH}px logo for splash/sleep...")
    
    # Create 4-level greyscale version (best for e-ink)
    logo_large = create_4level_greyscale_logo(cropped, TARGET_LOGO_WIDTH)
    logo_large_path = GENERATED_DIR / "margin-logo-400.png"
    logo_large.save(logo_large_path)
    print(f"Saved: {logo_large_path}")
    
    # Create clean 1-bit version for simpler displays
    logo_1bit = create_clean_1bit_logo(cropped, TARGET_LOGO_WIDTH)
    logo_1bit_path = GENERATED_DIR / "margin-logo-400-1bit.png"
    logo_1bit.save(logo_1bit_path)
    print(f"Saved: {logo_1bit_path}")
    
    # Generate inverted version for dark sleep screen
    logo_large_inv = create_4level_greyscale_logo(cropped, TARGET_LOGO_WIDTH, dark_on_white=False)
    logo_large_inv_path = GENERATED_DIR / "margin-logo-400-inverted.png"
    logo_large_inv.save(logo_large_inv_path)
    print(f"Saved: {logo_large_inv_path}")
    
    # Generate Logo120.h for backward compatibility (uses larger logo scaled down)
    print("\nGenerating Logo120.h (legacy 120x120)...")
    logo_120 = create_clean_1bit_logo(cropped, 120)
    header_120 = image_to_c_header(logo_120, "Logo120", 120, 120)
    header_path = GENERATED_DIR / "Logo120.h"
    header_path.write_text(header_120)
    print(f"Saved: {header_path}")
    
    # Save PNG preview of 120x120
    logo_120.save(GENERATED_DIR / "margin-logo-120.png")
    
    # Generate SVG favicon
    print("\nGenerating SVG favicon...")
    svg_content = create_svg_favicon(cropped, 128)
    svg_path = GENERATED_DIR / "margin-icon.svg"
    svg_path.write_text(svg_content)
    print(f"Saved: {svg_path}")
    
    # Generate previews
    print("\nGenerating preview images at 800x480...")
    
    # Splash screen
    preview_splash = create_preview_splash(logo_1bit, TARGET_LOGO_WIDTH)
    splash_path = DOCS_DIR / "preview-splash-screen.png"
    preview_splash.save(splash_path)
    print(f"Saved: {splash_path}")
    
    # Dark sleep screen
    preview_sleep_dark = create_preview_sleep(logo_1bit, TARGET_LOGO_WIDTH, dark=True)
    sleep_dark_path = DOCS_DIR / "preview-sleep-screen-dark.png"
    preview_sleep_dark.save(sleep_dark_path)
    print(f"Saved: {sleep_dark_path}")
    
    # Light sleep screen  
    preview_sleep_light = create_preview_sleep(logo_1bit, TARGET_LOGO_WIDTH, dark=False)
    sleep_light_path = DOCS_DIR / "preview-sleep-screen-light.png"
    preview_sleep_light.save(sleep_light_path)
    print(f"Saved: {sleep_light_path}")
    
    # About screen
    preview_about = create_preview_about()
    about_path = DOCS_DIR / "preview-about-screen.png"
    preview_about.save(about_path)
    print(f"Saved: {about_path}")
    
    # Favicon preview
    preview_favicon = create_preview_favicon(cropped)
    favicon_path = DOCS_DIR / "preview-web-favicon.png"
    preview_favicon.save(favicon_path)
    print(f"Saved: {favicon_path}")
    
    print("\n=== Generation complete! ===")
    print(f"Generated assets: {GENERATED_DIR}")
    print(f"Preview images: {DOCS_DIR}")
    
    return 0


if __name__ == "__main__":
    sys.exit(main())
