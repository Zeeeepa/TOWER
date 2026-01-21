#!/usr/bin/env python3
"""
Generate text-based CAPTCHA images with distortion effects.
Creates alphanumeric images with various fonts, wave distortion, rotation, noise.
Filename = CAPTCHA text for validation.
"""

import os
import random
import math
from PIL import Image, ImageDraw, ImageFont, ImageFilter, ImageOps
import string

# Output directory
OUTPUT_DIR = 'statics/signin_form/images/captcha'

# CAPTCHA settings
NUM_CAPTCHAS = 100  # Generate 100 different CAPTCHAs
CAPTCHA_LENGTH = 5  # 5 characters per CAPTCHA
IMAGE_WIDTH = 200
IMAGE_HEIGHT = 80

# Character set (avoiding ambiguous characters: O/0, I/l/1, etc.)
CHAR_SET = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789'

# Try to find system fonts - these work on macOS
FONT_PATHS = [
    '/System/Library/Fonts/Supplemental/Arial.ttf',
    '/System/Library/Fonts/Supplemental/Arial Bold.ttf',
    '/System/Library/Fonts/Supplemental/Courier New.ttf',
    '/System/Library/Fonts/Supplemental/Courier New Bold.ttf',
    '/System/Library/Fonts/Supplemental/Georgia.ttf',
    '/System/Library/Fonts/Supplemental/Georgia Bold.ttf',
    '/System/Library/Fonts/Supplemental/Times New Roman.ttf',
    '/System/Library/Fonts/Supplemental/Times New Roman Bold.ttf',
    '/System/Library/Fonts/Supplemental/Verdana.ttf',
    '/System/Library/Fonts/Supplemental/Verdana Bold.ttf',
]

# Color palettes for variety - BETTER CONTRAST
BG_COLORS = [
    (250, 250, 252),  # Very light gray
    (252, 252, 255),  # Almost white
    (255, 252, 250),  # Light cream
    (250, 252, 255),  # Light blue tint
    (255, 255, 255),  # Pure white
]

TEXT_COLORS = [
    (30, 30, 50),     # Dark blue-gray - DARKER for better contrast
    (40, 40, 65),     # Dark blue-gray
    (50, 30, 30),     # Dark red-brown
    (30, 50, 30),     # Dark green
    (30, 30, 60),     # Dark blue
    (35, 35, 35),     # Dark gray
]

NOISE_COLORS = [
    (210, 210, 220),  # Very light noise
    (200, 200, 210),  # Light noise
    (220, 220, 230),  # Even lighter noise
]


def create_output_dir():
    """Create output directory if it doesn't exist."""
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    print(f"✓ Created output directory: {OUTPUT_DIR}")


def generate_captcha_text():
    """Generate random alphanumeric text for CAPTCHA."""
    return ''.join(random.choice(CHAR_SET) for _ in range(CAPTCHA_LENGTH))


def get_random_font(size=40):
    """Get a random font from available system fonts."""
    available_fonts = [f for f in FONT_PATHS if os.path.exists(f)]

    if not available_fonts:
        # Fallback to default font
        return ImageFont.load_default()

    font_path = random.choice(available_fonts)
    try:
        return ImageFont.truetype(font_path, size)
    except:
        return ImageFont.load_default()


def apply_wave_distortion(image):
    """Apply GENTLE horizontal wave distortion to the image."""
    width, height = image.size

    # Create a new image for the distorted result
    distorted = Image.new('RGB', (width, height), (255, 255, 255))
    pixels = image.load()
    distorted_pixels = distorted.load()

    # Wave parameters - MUCH gentler than before
    amplitude = random.randint(1, 3)  # Wave height (was 3-8, too much!)
    frequency = random.uniform(0.02, 0.06)  # Wave frequency (was 0.05-0.15)

    for x in range(width):
        for y in range(height):
            # Calculate wave offset
            offset = int(amplitude * math.sin(2 * math.pi * frequency * x))
            new_y = y + offset

            # Boundary check
            if 0 <= new_y < height:
                distorted_pixels[x, y] = pixels[x, new_y]
            else:
                distorted_pixels[x, y] = (255, 255, 255)

    return distorted


def add_noise_lines(draw, width, height):
    """Add MINIMAL random noise lines to the image."""
    num_lines = random.randint(1, 3)  # Reduced from 3-6

    for _ in range(num_lines):
        x1 = random.randint(0, width)
        y1 = random.randint(0, height)
        x2 = random.randint(0, width)
        y2 = random.randint(0, height)

        color = random.choice(NOISE_COLORS)
        width_line = 1  # Always thin lines

        draw.line([(x1, y1), (x2, y2)], fill=color, width=width_line)


def add_noise_dots(draw, width, height):
    """Add MINIMAL random noise dots to the image."""
    num_dots = random.randint(10, 25)  # Reduced from 20-50

    for _ in range(num_dots):
        x = random.randint(0, width)
        y = random.randint(0, height)
        color = random.choice(NOISE_COLORS)
        radius = 1  # Always small dots

        draw.ellipse([(x-radius, y-radius), (x+radius, y+radius)], fill=color)


def generate_captcha_image(text):
    """Generate a single CAPTCHA image with distortion effects."""

    # Random background color
    bg_color = random.choice(BG_COLORS)
    text_color = random.choice(TEXT_COLORS)

    # Create base image
    image = Image.new('RGB', (IMAGE_WIDTH, IMAGE_HEIGHT), bg_color)
    draw = ImageDraw.Draw(image)

    # Add background noise lines first
    add_noise_lines(draw, IMAGE_WIDTH, IMAGE_HEIGHT)

    # Draw each character with individual styling
    x_offset = 20
    char_spacing = (IMAGE_WIDTH - 40) // len(text)

    for i, char in enumerate(text):
        # Random font for each character (but larger and more consistent)
        font_size = random.randint(40, 48)  # Larger fonts
        font = get_random_font(font_size)

        # GENTLE rotation for each character
        rotation = random.randint(-12, 12)  # Reduced from -25 to 25

        # Create temporary image for this character
        char_img = Image.new('RGBA', (70, 70), (255, 255, 255, 0))
        char_draw = ImageDraw.Draw(char_img)

        # Draw character
        char_draw.text((15, 8), char, font=font, fill=text_color)

        # Rotate character
        char_img = char_img.rotate(rotation, expand=False, fillcolor=(255, 255, 255, 0))

        # Paste onto main image
        y_offset = random.randint(5, 15)  # Less vertical variation
        image.paste(char_img, (x_offset + i * char_spacing, y_offset), char_img)

    # Add foreground noise dots
    draw = ImageDraw.Draw(image)
    add_noise_dots(draw, IMAGE_WIDTH, IMAGE_HEIGHT)

    # Apply effects - MUCH MORE SELECTIVE
    # Only apply wave distortion 60% of the time, and very lightly
    if random.random() < 0.6:
        try:
            image = apply_wave_distortion(image)
        except:
            pass

    # Very minimal blur (only 30% of images, very light)
    if random.random() < 0.3:
        try:
            image = image.filter(ImageFilter.GaussianBlur(radius=random.uniform(0.2, 0.4)))
        except:
            pass

    # Skip posterize effect - it's too harsh

    return image


def generate_all_captchas():
    """Generate all CAPTCHA images."""
    print(f"\n{'='*60}")
    print(f"Text-Based CAPTCHA Generator")
    print(f"{'='*60}\n")

    generated = []

    for i in range(NUM_CAPTCHAS):
        # Generate unique text
        while True:
            text = generate_captcha_text()
            if text not in generated:
                generated.append(text)
                break

        # Generate image
        image = generate_captcha_image(text)

        # Save with text as filename
        filename = f"{text}.png"
        filepath = os.path.join(OUTPUT_DIR, filename)
        image.save(filepath, 'PNG')

        if (i + 1) % 10 == 0:
            print(f"  Generated {i + 1}/{NUM_CAPTCHAS} CAPTCHAs...")

    print(f"\n{'='*60}")
    print(f"✓ Generated {NUM_CAPTCHAS} CAPTCHA images")
    print(f"  Output directory: {OUTPUT_DIR}")
    print(f"  Image size: {IMAGE_WIDTH}x{IMAGE_HEIGHT}")
    print(f"  Character set: {CHAR_SET}")
    print(f"  Text length: {CAPTCHA_LENGTH} characters")
    print(f"{'='*60}\n")

    return generated


def create_metadata(captcha_texts):
    """Create metadata file listing all CAPTCHA images."""
    import json

    metadata = {
        'total_count': len(captcha_texts),
        'image_width': IMAGE_WIDTH,
        'image_height': IMAGE_HEIGHT,
        'character_length': CAPTCHA_LENGTH,
        'character_set': CHAR_SET,
        'captchas': captcha_texts
    }

    metadata_path = os.path.join(OUTPUT_DIR, 'metadata.json')
    with open(metadata_path, 'w') as f:
        json.dump(metadata, f, indent=2)

    print(f"✓ Created metadata: {metadata_path}\n")


def main():
    create_output_dir()
    captcha_texts = generate_all_captchas()
    create_metadata(captcha_texts)


if __name__ == '__main__':
    main()
