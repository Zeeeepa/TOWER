#!/usr/bin/env python3
"""
Download and process royalty-free images for CAPTCHA testing.
Uses Pexels API for high-quality, free images.
Applies CAPTCHA-like degradation effects.
"""

import os
import requests
import random
import time
from PIL import Image, ImageFilter, ImageEnhance
from io import BytesIO
from dotenv import load_dotenv

# Load environment variables from .env file
load_dotenv()

# Pexels API key from environment
PEXELS_API_KEY = os.getenv('PEXELS_API_KEY')

if not PEXELS_API_KEY:
    print("Error: PEXELS_API_KEY not found in .env file")
    print("Get your free API key from: https://www.pexels.com/api/")
    exit(1)

# Image categories for CAPTCHA challenges - highly distinct objects with better Pexels queries
# Each category has 4 specific queries for variety
CATEGORIES = {
    'cars': ['red car close', 'blue car street', 'white car parked', 'sports car front'],
    'buses': ['yellow school bus', 'city bus street', 'double decker bus', 'public bus'],
    'traffic_lights': ['traffic light pole', 'red traffic signal', 'traffic light street', 'stoplight close'],
    'fire_hydrants': ['red fire hydrant', 'yellow fire hydrant street', 'fire hydrant close', 'hydrant sidewalk'],
    'boats': ['white sailboat ocean', 'yacht harbor', 'boat water', 'sailing boat blue'],
    'airplanes': ['airplane flying sky', 'jet aircraft', 'plane blue sky', 'commercial airplane'],
    'crosswalks': ['zebra crossing street', 'crosswalk stripes', 'pedestrian crossing road', 'white crosswalk lines'],
    'stairs': ['outdoor stairs concrete', 'stone steps', 'staircase outside', 'stairs building'],
}

# Generic background images (no specific objects)
# Reduced to stay under API limit
BACKGROUND_QUERIES = [
    'grass texture',
    'wooden texture',
    'concrete wall',
    'fabric background',
]

OUTPUT_DIR = '../statics/user_form/images/captcha'  # Relative to scripts/ folder
IMAGES_PER_CATEGORY = 12  # Download multiple images per category
IMAGE_SIZE = 300  # Base size before degradation


def create_output_dir():
    """Create output directory if it doesn't exist."""
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    print(f"✓ Created output directory: {OUTPUT_DIR}")


def search_pexels(query, per_page=15):
    """Search Pexels for images."""
    try:
        url = "https://api.pexels.com/v1/search"
        headers = {
            "Authorization": PEXELS_API_KEY
        }
        params = {
            "query": query,
            "per_page": per_page,
            "page": 1,
            "orientation": "square"  # Better for CAPTCHA tiles
        }

        response = requests.get(url, headers=headers, params=params, timeout=10)
        response.raise_for_status()

        data = response.json()
        return data.get('photos', [])

    except Exception as e:
        print(f"  ✗ Error searching Pexels: {e}")
        return []


def download_image_from_url(image_url):
    """Download image from URL."""
    try:
        response = requests.get(image_url, timeout=10)
        response.raise_for_status()

        img = Image.open(BytesIO(response.content))

        # Ensure RGB mode
        if img.mode != 'RGB':
            img = img.convert('RGB')

        return img

    except Exception as e:
        print(f"  ✗ Error downloading image: {e}")
        return None


def apply_captcha_effects(img):
    """Apply realistic CAPTCHA-like degradation - low quality like real CAPTCHAs."""

    # 1. Resize to CAPTCHA tile size
    img = img.resize((200, 200), Image.Resampling.LANCZOS)

    # 2. Significant quality reduction (like real CAPTCHAs)
    buffer = BytesIO()
    quality = random.randint(40, 60)  # Low quality for realistic CAPTCHA look
    img.save(buffer, format='JPEG', quality=quality)
    buffer.seek(0)
    img = Image.open(buffer)

    # 3. Noticeable blur for CAPTCHA effect
    if random.random() > 0.5:  # 50% of images get blur
        blur_radius = random.uniform(0.3, 0.7)  # Moderate blur
        img = img.filter(ImageFilter.GaussianBlur(radius=blur_radius))

    # 4. Reduce sharpness
    if random.random() > 0.5:  # 50% of images
        enhancer = ImageEnhance.Sharpness(img)
        img = enhancer.enhance(random.uniform(0.7, 0.9))  # Noticeable reduction

    # 5. Moderate brightness/contrast variation
    if random.random() > 0.4:  # 60% of images
        brightness = ImageEnhance.Brightness(img)
        img = brightness.enhance(random.uniform(0.85, 1.15))  # Noticeable change

        contrast = ImageEnhance.Contrast(img)
        img = contrast.enhance(random.uniform(0.85, 1.15))  # Noticeable change

    # 6. Visible noise like real CAPTCHAs
    pixels = img.load()
    width, height = img.size

    for _ in range(int(width * height * 0.015)):  # 1.5% noise (realistic CAPTCHA level)
        x = random.randint(0, width - 1)
        y = random.randint(0, height - 1)
        noise = random.randint(-20, 20)  # Noticeable noise

        r, g, b = pixels[x, y]
        pixels[x, y] = (
            max(0, min(255, r + noise)),
            max(0, min(255, g + noise)),
            max(0, min(255, b + noise))
        )

    return img


def download_category_images():
    """Download images for each category."""
    downloaded_count = 0

    for category_name, queries in CATEGORIES.items():
        print(f"\n📥 Downloading {category_name}...")

        category_dir = os.path.join(OUTPUT_DIR, category_name)
        os.makedirs(category_dir, exist_ok=True)

        # Search Pexels for images
        all_photos = []
        for query in queries:
            photos = search_pexels(query, per_page=10)
            all_photos.extend(photos)
            time.sleep(0.5)  # Rate limiting

        if not all_photos:
            print(f"  ✗ No images found for {category_name}")
            continue

        # Shuffle for variety
        random.shuffle(all_photos)

        # Download and process
        images_downloaded = 0
        for photo in all_photos:
            if images_downloaded >= IMAGES_PER_CATEGORY:
                break

            # Get medium-sized image from Pexels
            image_url = photo['src'].get('medium') or photo['src'].get('small')

            print(f"  Downloading image {images_downloaded + 1}...")

            img = download_image_from_url(image_url)

            if img:
                # Apply CAPTCHA effects
                img = apply_captcha_effects(img)

                # Save with lower quality for realistic CAPTCHA appearance
                output_path = os.path.join(category_dir, f"{category_name}_{images_downloaded:02d}.jpg")
                img.save(output_path, 'JPEG', quality=50)

                images_downloaded += 1
                downloaded_count += 1
                print(f"  ✓ Saved: {output_path}")

                time.sleep(0.3)  # Rate limiting

        print(f"✓ Downloaded {images_downloaded} images for {category_name}")

    return downloaded_count


def download_background_images():
    """Download background/distractor images (no specific objects)."""
    print(f"\n📥 Downloading background images...")

    bg_dir = os.path.join(OUTPUT_DIR, 'backgrounds')
    os.makedirs(bg_dir, exist_ok=True)

    # Search for background images
    all_photos = []
    for query in BACKGROUND_QUERIES:
        photos = search_pexels(query, per_page=8)
        all_photos.extend(photos)
        time.sleep(0.5)

    random.shuffle(all_photos)

    downloaded = 0
    target = 25

    for photo in all_photos:
        if downloaded >= target:
            break

        image_url = photo['src'].get('medium') or photo['src'].get('small')
        print(f"  Downloading background {downloaded + 1}...")

        img = download_image_from_url(image_url)

        if img:
            img = apply_captcha_effects(img)
            output_path = os.path.join(bg_dir, f"bg_{downloaded:02d}.jpg")
            img.save(output_path, 'JPEG', quality=50)
            downloaded += 1
            print(f"  ✓ Saved: {output_path}")

            time.sleep(0.3)

    print(f"✓ Downloaded {downloaded} background images")
    return downloaded


def create_metadata():
    """Create metadata file listing all downloaded images."""
    metadata = {
        'categories': {},
        'backgrounds': []
    }

    # List category images
    for category_name in CATEGORIES.keys():
        category_dir = os.path.join(OUTPUT_DIR, category_name)
        if os.path.exists(category_dir):
            images = sorted([f for f in os.listdir(category_dir) if f.endswith('.jpg')])
            metadata['categories'][category_name] = images

    # List background images
    bg_dir = os.path.join(OUTPUT_DIR, 'backgrounds')
    if os.path.exists(bg_dir):
        metadata['backgrounds'] = sorted([f for f in os.listdir(bg_dir) if f.endswith('.jpg')])

    # Write metadata
    import json
    metadata_path = os.path.join(OUTPUT_DIR, 'metadata.json')
    with open(metadata_path, 'w') as f:
        json.dump(metadata, f, indent=2)

    print(f"\n✓ Created metadata: {metadata_path}")


def main():
    print("=" * 60)
    print("CAPTCHA Image Downloader (Pexels)")
    print("=" * 60)

    create_output_dir()

    # Download category images (objects to find)
    category_count = download_category_images()

    # Download background images (distractors)
    bg_count = download_background_images()

    # Create metadata
    create_metadata()

    print("\n" + "=" * 60)
    print(f"✓ Download complete!")
    print(f"  - Category images: {category_count}")
    print(f"  - Background images: {bg_count}")
    print(f"  - Total: {category_count + bg_count}")
    print(f"  - Output directory: {OUTPUT_DIR}")
    print("=" * 60)


if __name__ == '__main__':
    main()
