#!/usr/bin/env python3
"""Generate margin branding assets from the od3sa cyber bear source image.

Produces:
- Logo120.h: 120x120 1-bit C header for boot/sleep screens
- margin-logo-120.png: PNG preview of the 120x120 logo
- margin-icon.svg: SVG icon for web UI favicon
- Preview images at X4 Pro resolution (800x480)
"""

import os
import sys
from pathlib import Path

from PIL import Image, ImageOps, ImageFilter, ImageDraw, ImageEnhance

SCRIPT_DIR = Path(__file__).parent
SOURCE_DIR = SCRIPT_DIR / "source"
GENERATED_DIR = SCRIPT_DIR / "generated"
DOCS_DIR = SCRIPT_DIR.parent.parent / "docs" / "branding"

# E-ink display resolution for X4 Pro
DISPLAY_WIDTH = 800
DISPLAY_HEIGHT = 480


def load_and_crop_bear(source_path: Path) -> Image.Image:
    """Load the cyber bear image and crop out the dark rounded-square background.
    
    The source image has a cyberpunk bear on a near-black rounded rectangle.
    We want just the bear itself.
    """
    img = Image.open(source_path).convert("RGBA")
    
    # The bear is in the center, surrounded by a very dark background with rounded corners
    # Find the actual bear content by looking at brightness
    gray = img.convert("L")
    
    # The background is the dark rounded rectangle (< 25)
    # The bear has bright cyan and pink neon elements (> 40)
    threshold = 25
    
    # Create mask of content
    mask = Image.eval(gray, lambda x: 255 if x > threshold else 0)
    
    # Get bounding box of non-background content
    bbox = mask.getbbox()
    if bbox:
        # Add a small margin (5% of image size)
        margin = int(img.width * 0.02)
        x1, y1, x2, y2 = bbox
        x1 = max(0, x1 - margin)
        y1 = max(0, y1 - margin)
        x2 = min(img.width, x2 + margin)
        y2 = min(img.height, y2 + margin)
        
        # Crop to content
        cropped = img.crop((x1, y1, x2, y2))
    else:
        cropped = img
    
    return cropped


def create_eink_logo(source: Image.Image, size: int = 120, invert: bool = False) -> Image.Image:
    """Convert the cropped bear to a high-contrast e-ink optimized logo.
    
    For e-ink: dark lines on white background (or inverted for dark sleep screens).
    The cyberpunk bear has cyan/pink neon colors that need to become dark outlines.
    """
    # Resize while maintaining aspect ratio, then pad to square
    source = source.copy()
    
    # Convert to RGBA if not already
    if source.mode != "RGBA":
        source = source.convert("RGBA")
    
    # Calculate resize dimensions to fit in target while maintaining aspect
    ratio = min(size / source.width, size / source.height)
    new_size = (int(source.width * ratio), int(source.height * ratio))
    resized = source.resize(new_size, Image.LANCZOS)
    
    # Create square canvas (white background initially)
    canvas = Image.new("RGBA", (size, size), (255, 255, 255, 255))
    
    # Calculate paste position to center
    paste_x = (size - resized.width) // 2
    paste_y = (size - resized.height) // 2
    
    # Paste with alpha compositing
    canvas.paste(resized, (paste_x, paste_y), resized)
    
    # Convert to grayscale
    gray = canvas.convert("L")
    
    # Enhance contrast significantly for e-ink
    enhancer = ImageEnhance.Contrast(gray)
    gray = enhancer.enhance(2.5)
    
    # Apply a slight sharpening
    gray = gray.filter(ImageFilter.SHARPEN)
    
    # For e-ink: convert to high-contrast black and white
    # The neon colors become dark on white
    # We want the bear outline/details to be dark
    
    # First invert so bright neon becomes dark
    gray = ImageOps.invert(gray)
    
    # Apply threshold for clean 1-bit output
    # Adjust threshold to capture the bear details
    threshold = 100
    bw = Image.eval(gray, lambda x: 0 if x > threshold else 255)
    
    if invert:
        bw = ImageOps.invert(bw)
    
    return bw


def create_line_art_logo(source: Image.Image, size: int = 120, dark_on_white: bool = True) -> Image.Image:
    """Create a clean line-art style logo from the cyber bear.
    
    Uses edge detection to extract the bear's outlines.
    """
    # Convert to grayscale first
    if source.mode != "L":
        gray = source.convert("L")
    else:
        gray = source.copy()
    
    # Resize
    ratio = min(size / gray.width, size / gray.height)
    new_size = (int(gray.width * ratio * 0.9), int(gray.height * ratio * 0.9))  # 90% to leave padding
    resized = gray.resize(new_size, Image.LANCZOS)
    
    # Edge detection using Sobel-like filter
    edges = resized.filter(ImageFilter.FIND_EDGES)
    
    # Enhance edges
    enhancer = ImageEnhance.Contrast(edges)
    edges = enhancer.enhance(3.0)
    
    # Threshold to clean binary
    threshold = 40
    if dark_on_white:
        # Dark lines on white - edges become dark
        bw = Image.eval(edges, lambda x: 0 if x > threshold else 255)
    else:
        # White lines on dark (for inverted sleep screens)
        bw = Image.eval(edges, lambda x: 255 if x > threshold else 0)
    
    # Create square canvas
    bg_color = 255 if dark_on_white else 0
    canvas = Image.new("L", (size, size), bg_color)
    
    # Center the edges
    paste_x = (size - bw.width) // 2
    paste_y = (size - bw.height) // 2
    canvas.paste(bw, (paste_x, paste_y))
    
    return canvas


def extract_neon_mask(source: Image.Image) -> Image.Image:
    """Extract the bright neon elements (cyan and pink) from the cyber bear.
    
    The bear has bright cyan (#00FFFF-ish) and pink/magenta (#FF00FF-ish) neon elements
    on a dark background. We want to capture these as the logo.
    """
    rgb = source.convert("RGB")
    r, g, b = rgb.split()
    
    # Create masks for different color types
    # Cyan-ish: high green and blue, lower red
    # Pink/magenta: high red and blue, lower green
    # Any bright element: overall brightness
    
    # Convert channels to arrays for threshold operations
    r_data = list(r.getdata())
    g_data = list(g.getdata())
    b_data = list(b.getdata())
    
    # Create mask where pixels are bright neon
    mask_data = []
    for i in range(len(r_data)):
        rv, gv, bv = r_data[i], g_data[i], b_data[i]
        brightness = (rv + gv + bv) / 3
        
        # Check for cyan-ish (high G and B)
        is_cyan = gv > 100 and bv > 100 and brightness > 80
        # Check for pink/magenta (high R and B)  
        is_pink = rv > 100 and bv > 80 and brightness > 80
        # Check for any significantly bright pixel
        is_bright = brightness > 60
        
        if is_cyan or is_pink or is_bright:
            mask_data.append(255)
        else:
            mask_data.append(0)
    
    mask = Image.new("L", rgb.size)
    mask.putdata(mask_data)
    return mask


def create_dithered_logo(source: Image.Image, size: int = 120) -> Image.Image:
    """Create a dithered grayscale logo optimized for e-ink.
    
    Uses the bright neon elements as the logo content on white background.
    """
    # Resize source first
    source_rgba = source.convert("RGBA") if source.mode != "RGBA" else source.copy()
    
    ratio = min(size / source_rgba.width, size / source_rgba.height)
    new_size = (int(source_rgba.width * ratio * 0.92), int(source_rgba.height * ratio * 0.92))
    resized = source_rgba.resize(new_size, Image.LANCZOS)
    
    # Extract the neon mask from resized image
    neon_mask = extract_neon_mask(resized)
    
    # Enhance the mask
    enhancer = ImageEnhance.Contrast(neon_mask)
    neon_mask = enhancer.enhance(1.5)
    
    # Apply slight blur then sharpen to clean up edges
    neon_mask = neon_mask.filter(ImageFilter.GaussianBlur(radius=0.5))
    neon_mask = neon_mask.filter(ImageFilter.SHARPEN)
    
    # Create white canvas
    canvas = Image.new("L", (size, size), 255)
    
    # Center the mask
    paste_x = (size - neon_mask.width) // 2
    paste_y = (size - neon_mask.height) // 2
    
    # Paste neon mask as black-on-white (invert mask so bright becomes dark)
    neon_inverted = ImageOps.invert(neon_mask)
    
    # Composite: where neon_inverted is dark (was bright neon), make canvas dark
    canvas.paste(neon_inverted, (paste_x, paste_y))
    
    # Apply dithering for clean e-ink output
    dithered = canvas.convert("1", dither=Image.FLOYDSTEINBERG)
    
    return dithered.convert("L")


def image_to_c_header(img: Image.Image, array_name: str) -> str:
    """Convert a 1-bit image to C header format matching CrossPoint's Logo120.h style."""
    # Ensure image is in mode "1" (1-bit)
    if img.mode != "1":
        img = img.convert("1")
    
    width, height = img.size
    pixels = list(img.getdata())
    
    # Pack pixels into bytes: 1=white, 0=black, MSB first
    packed = []
    for y in range(height):
        for x in range(0, width, 8):
            byte = 0
            for b in range(8):
                if x + b < width:
                    # In mode "1": 0=black, 255=white
                    pixel = pixels[y * width + x + b]
                    bit = 1 if pixel else 0
                    byte |= (bit << (7 - b))
            packed.append(byte)
    
    # Format as C array
    lines = ['#pragma once', '#include <cstdint>', '', f'// Image dimensions: {width}x{height}']
    lines.append(f'static const uint8_t {array_name}[] = {{')
    
    # Write 19 bytes per line (matching CrossPoint's format)
    bytes_per_line = 19
    for i in range(0, len(packed), bytes_per_line):
        chunk = packed[i:i + bytes_per_line]
        hex_values = ', '.join(f'0x{b:02x}' for b in chunk)
        lines.append(f'    {hex_values},')
    
    # Remove trailing comma from last line and close
    if lines[-1].endswith(','):
        lines[-1] = lines[-1][:-1] + '};'
    else:
        lines.append('};')
    
    return '\n'.join(lines)


def create_svg_icon(source: Image.Image) -> str:
    """Create an SVG icon from the bear image for web UI.
    
    Generates a monochrome SVG suitable for favicon use.
    """
    # Process to get clean edges
    size = 512
    gray = source.convert("L")
    
    ratio = min(size / gray.width, size / gray.height)
    new_size = (int(gray.width * ratio * 0.85), int(gray.height * ratio * 0.85))
    resized = gray.resize(new_size, Image.LANCZOS)
    
    # For SVG, we'll create a simple monochrome version
    # Invert and threshold
    inverted = ImageOps.invert(resized)
    
    # Create a path-based SVG from the image
    # For simplicity, embed the image as a data URI
    import base64
    import io
    
    # Create final composited image
    canvas = Image.new("RGBA", (size, size), (255, 255, 255, 0))  # Transparent background
    
    # Process the bear for SVG
    rgba = source.convert("RGBA")
    rgba_resized = rgba.resize(new_size, Image.LANCZOS)
    
    paste_x = (size - rgba_resized.width) // 2
    paste_y = (size - rgba_resized.height) // 2
    canvas.paste(rgba_resized, (paste_x, paste_y), rgba_resized)
    
    # Convert to grayscale with alpha preserved for transparent areas
    # Make it work well in both light and dark contexts
    r, g, b, a = canvas.split()
    gray = canvas.convert("L")
    
    # Create high-contrast version
    enhancer = ImageEnhance.Contrast(gray)
    gray_enhanced = enhancer.enhance(2.0)
    
    # Invert for dark icon on transparent background
    gray_inverted = ImageOps.invert(gray_enhanced)
    
    # Threshold
    threshold = 80
    bw = Image.eval(gray_inverted, lambda x: 0 if x > threshold else 255)
    
    # Apply the original alpha
    final = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    bw_rgba = Image.merge("RGBA", (bw.convert("L"), bw.convert("L"), bw.convert("L"), a))
    final.paste(bw_rgba, (paste_x, paste_y))
    
    # Save to buffer
    buf = io.BytesIO()
    final.save(buf, format='PNG')
    b64_data = base64.b64encode(buf.getvalue()).decode('ascii')
    
    svg = f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {size} {size}" width="{size}" height="{size}">
  <image href="data:image/png;base64,{b64_data}" width="{size}" height="{size}"/>
</svg>'''
    
    return svg


def create_simple_svg_icon(source: Image.Image) -> str:
    """Create a simpler monochrome SVG that works well as favicon."""
    import base64
    import io
    
    size = 512
    
    # Process source
    rgba = source.convert("RGBA") if source.mode != "RGBA" else source.copy()
    
    ratio = min(size / rgba.width, size / rgba.height)
    new_size = (int(rgba.width * ratio * 0.9), int(rgba.height * ratio * 0.9))
    resized = rgba.resize(new_size, Image.LANCZOS)
    
    # Create canvas
    canvas = Image.new("RGBA", (size, size), (255, 255, 255, 255))
    paste_x = (size - resized.width) // 2
    paste_y = (size - resized.height) // 2
    canvas.paste(resized, (paste_x, paste_y), resized)
    
    # Convert to grayscale and process for e-ink style
    gray = canvas.convert("L")
    gray = ImageOps.invert(gray)
    
    enhancer = ImageEnhance.Contrast(gray)
    gray = enhancer.enhance(2.0)
    
    # Threshold for clean look
    threshold = 100
    bw = Image.eval(gray, lambda x: 0 if x > threshold else 255)
    
    # Convert back to RGBA with white becoming transparent
    bw_rgba = bw.convert("RGBA")
    pixels = bw_rgba.load()
    for y in range(size):
        for x in range(size):
            r, g, b, a = pixels[x, y]
            if r > 200:  # White -> transparent
                pixels[x, y] = (0, 0, 0, 0)
            else:  # Black stays black
                pixels[x, y] = (0, 0, 0, 255)
    
    # Save to buffer
    buf = io.BytesIO()
    bw_rgba.save(buf, format='PNG')
    b64_data = base64.b64encode(buf.getvalue()).decode('ascii')
    
    svg = f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {size} {size}" width="{size}" height="{size}">
  <image href="data:image/png;base64,{b64_data}" width="{size}" height="{size}"/>
</svg>'''
    
    return svg


def create_preview_splash(logo: Image.Image, width: int = 800, height: int = 480) -> Image.Image:
    """Create a preview of the boot splash screen at device resolution."""
    # Create white background (e-ink default)
    screen = Image.new("L", (width, height), 255)
    
    # Calculate logo position (centered)
    logo_x = (width - logo.width) // 2
    logo_y = (height - logo.height) // 2 - 20  # Slightly above center
    
    # Paste logo
    screen.paste(logo, (logo_x, logo_y))
    
    # Add text placeholders
    draw = ImageDraw.Draw(screen)
    
    # "margin" text below logo
    text_y = logo_y + logo.height + 10
    draw.text((width // 2, text_y), "margin", fill=0, anchor="mt")
    
    # "Booting..." below that
    draw.text((width // 2, text_y + 25), "Booting...", fill=0, anchor="mt")
    
    # Version at bottom
    draw.text((width // 2, height - 30), "v1.0.0", fill=0, anchor="mt")
    
    return screen


def create_preview_sleep(logo: Image.Image, width: int = 800, height: int = 480, dark: bool = True) -> Image.Image:
    """Create a preview of the sleep screen at device resolution."""
    if dark:
        # Dark sleep screen - white on black
        screen = Image.new("L", (width, height), 0)
        logo_inverted = ImageOps.invert(logo.convert("L"))
    else:
        # Light sleep screen
        screen = Image.new("L", (width, height), 255)
        logo_inverted = logo.convert("L")
    
    # Center the logo
    logo_x = (width - logo_inverted.width) // 2
    logo_y = (height - logo_inverted.height) // 2
    
    screen.paste(logo_inverted, (logo_x, logo_y))
    
    return screen


def create_preview_about(width: int = 800, height: int = 480) -> Image.Image:
    """Create a preview of the About screen (no logo, just info)."""
    screen = Image.new("L", (width, height), 255)
    draw = ImageDraw.Draw(screen)
    
    # Header
    draw.text((width // 2, 30), "About", fill=0, anchor="mt")
    
    # Info lines
    info_lines = [
        ("Device", "Xteink X4 Pro"),
        ("Firmware", "margin v1.0.0"),
        ("Resolution", f"{width}x{height}"),
    ]
    
    y = 80
    for label, value in info_lines:
        draw.text((50, y), label, fill=128)
        draw.text((width - 50, y), value, fill=0, anchor="rt")
        y += 40
    
    return screen


def main():
    print("=== margin Branding Generator ===")
    print()
    
    # Ensure output directories exist
    GENERATED_DIR.mkdir(parents=True, exist_ok=True)
    DOCS_DIR.mkdir(parents=True, exist_ok=True)
    
    # Load and crop the source image
    source_path = SOURCE_DIR / "od3sa-cyber-bear.jpg"
    print(f"Loading source: {source_path}")
    cropped = load_and_crop_bear(source_path)
    print(f"Cropped to: {cropped.size}")
    
    # Save cropped source for reference
    cropped_path = GENERATED_DIR / "cropped-bear.png"
    cropped.save(cropped_path)
    print(f"Saved cropped source: {cropped_path}")
    
    # Generate 120x120 logo (dithered version - best for e-ink)
    print("\nGenerating Logo120...")
    logo_120 = create_dithered_logo(cropped, 120)
    
    # Save as PNG
    logo_120_path = GENERATED_DIR / "margin-logo-120.png"
    logo_120.save(logo_120_path)
    print(f"Saved: {logo_120_path}")
    
    # Generate C header
    logo_120_1bit = logo_120.convert("1")
    header_content = image_to_c_header(logo_120_1bit, "Logo120")
    header_path = GENERATED_DIR / "Logo120.h"
    header_path.write_text(header_content)
    print(f"Saved: {header_path}")
    
    # Generate inverted version for dark sleep screens
    logo_120_inverted = ImageOps.invert(logo_120)
    logo_120_inverted_path = GENERATED_DIR / "margin-logo-120-inverted.png"
    logo_120_inverted.save(logo_120_inverted_path)
    print(f"Saved: {logo_120_inverted_path}")
    
    # Generate SVG icon for web UI
    print("\nGenerating SVG icon...")
    svg_content = create_simple_svg_icon(cropped)
    svg_path = GENERATED_DIR / "margin-icon.svg"
    svg_path.write_text(svg_content)
    print(f"Saved: {svg_path}")
    
    # Generate preview images
    print("\nGenerating preview images...")
    
    # Splash screen preview
    splash_preview = create_preview_splash(logo_120)
    splash_path = DOCS_DIR / "preview-splash-screen.png"
    splash_preview.save(splash_path)
    print(f"Saved: {splash_path}")
    
    # Dark sleep screen preview
    sleep_dark_preview = create_preview_sleep(logo_120, dark=True)
    sleep_dark_path = DOCS_DIR / "preview-sleep-screen-dark.png"
    sleep_dark_preview.save(sleep_dark_path)
    print(f"Saved: {sleep_dark_path}")
    
    # Light sleep screen preview
    sleep_light_preview = create_preview_sleep(logo_120, dark=False)
    sleep_light_path = DOCS_DIR / "preview-sleep-screen-light.png"
    sleep_light_preview.save(sleep_light_path)
    print(f"Saved: {sleep_light_path}")
    
    # About screen preview (no logo)
    about_preview = create_preview_about()
    about_path = DOCS_DIR / "preview-about-screen.png"
    about_preview.save(about_path)
    print(f"Saved: {about_path}")
    
    print("\n=== Branding generation complete! ===")
    print(f"\nGenerated assets in: {GENERATED_DIR}")
    print(f"Preview images in: {DOCS_DIR}")
    
    return 0


if __name__ == "__main__":
    sys.exit(main())
