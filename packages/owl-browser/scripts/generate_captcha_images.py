#!/home/ahstanin/jetson-venvs/extra_env/bin/python
"""
Generate CAPTCHA images using Qwen-Image diffusion model.
Runs on Nvidia Jetson device with GPU support.

Usage:
    ./generate_captcha_images.py                    # Generate all categories
    ./generate_captcha_images.py --category cars    # Generate only cars
    ./generate_captcha_images.py --backgrounds-only # Generate only backgrounds
    ./generate_captcha_images.py --list             # List available categories
"""

import os
import sys
import argparse

# Disable flash attention to avoid CUDA library conflicts on Jetson
os.environ['DIFFUSERS_DISABLE_FLASH_ATTN'] = '1'

import random
import torch
from PIL import Image, ImageFilter, ImageEnhance
from io import BytesIO
from diffusers import DiffusionPipeline

# Variation dictionaries for dynamic prompt generation
# Each category has multiple variation dimensions that can be combined

VARIATION_TEMPLATES = {
    'cars': {
        'types': ['sedan', 'SUV', 'sports car', 'hatchback', 'coupe', 'pickup truck', 'convertible', 'minivan'],
        'colors': ['red', 'blue', 'white', 'black', 'silver', 'gray', 'green', 'yellow'],
        'views': ['front view', 'side view', 'rear view', 'three-quarter view', 'angled view', 'close-up'],
        'settings': ['on street', 'parked in lot', 'on highway', 'in driveway', 'on road', 'at intersection'],
        'times': ['daylight', 'sunny day', 'overcast day', 'bright lighting', 'morning light', 'afternoon'],
        'details': ['detailed', 'sharp focus', 'photorealistic', 'realistic photo', 'professional photo', 'clear image']
    },
    'buses': {
        'types': ['school bus', 'city bus', 'double decker bus', 'public transit bus', 'tour bus', 'shuttle bus'],
        'colors': ['yellow', 'red', 'blue', 'white', 'orange', 'green'],
        'views': ['side view', 'front view', 'angled view', 'three-quarter view', 'rear view', 'close-up'],
        'settings': ['on road', 'at bus stop', 'on street', 'in traffic', 'on highway', 'in city'],
        'times': ['daylight', 'sunny day', 'afternoon', 'morning', 'bright day', 'clear weather'],
        'details': ['photorealistic', 'detailed', 'realistic photo', 'sharp focus', 'professional photo', 'clear image']
    },
    'traffic_lights': {
        'types': ['traffic light', 'traffic signal', 'stoplight', 'signal light', 'street light signal'],
        'states': ['with red light', 'with green light', 'with yellow light', 'showing red', 'showing green', 'three lights'],
        'views': ['front view', 'close-up', 'street view', 'angled view', 'side angle', 'straight on'],
        'settings': ['on pole', 'at intersection', 'on street corner', 'above road', 'on post', 'urban setting'],
        'times': ['daylight', 'daytime', 'sunny day', 'bright day', 'clear day', 'afternoon'],
        'details': ['photorealistic', 'detailed', 'realistic photo', 'sharp image', 'professional photo', 'clear']
    },
    'fire_hydrants': {
        'types': ['fire hydrant', 'hydrant', 'water hydrant', 'street hydrant'],
        'colors': ['red', 'yellow', 'red and white', 'bright red', 'orange', 'red with silver top'],
        'views': ['close-up', 'front view', 'side view', 'angled view', 'three-quarter view', 'street level'],
        'settings': ['on sidewalk', 'on street corner', 'next to curb', 'on pavement', 'by roadside', 'urban setting'],
        'times': ['daylight', 'sunny day', 'bright day', 'afternoon', 'clear day', 'daytime'],
        'details': ['photorealistic', 'detailed', 'realistic photo', 'sharp focus', 'professional photo', 'clear image']
    },
    'boats': {
        'types': ['sailboat', 'yacht', 'motorboat', 'fishing boat', 'speedboat', 'cabin cruiser', 'sailing vessel'],
        'colors': ['white', 'blue', 'red', 'white and blue', 'cream', 'blue and white'],
        'views': ['side view', 'angled view', 'three-quarter view', 'front view', 'distant view', 'close-up'],
        'settings': ['on ocean', 'in harbor', 'on water', 'at marina', 'on sea', 'on lake'],
        'times': ['sunny day', 'bright day', 'daylight', 'clear sky', 'blue sky', 'afternoon'],
        'details': ['photorealistic', 'detailed', 'realistic photo', 'sharp focus', 'professional photo', 'clear image']
    },
    'airplanes': {
        'types': ['commercial airplane', 'passenger plane', 'jet aircraft', 'airliner', 'jet plane', 'aircraft'],
        'colors': ['white', 'white and blue', 'white and red', 'silver', 'blue and white'],
        'views': ['side view', 'angled view', 'three-quarter view', 'from below', 'diagonal view', 'profile view'],
        'settings': ['flying in sky', 'in flight', 'in air', 'taking off', 'in blue sky', 'above clouds'],
        'times': ['daylight', 'clear sky', 'blue sky', 'sunny day', 'bright day', 'clear weather'],
        'details': ['photorealistic', 'detailed', 'realistic photo', 'sharp focus', 'professional photo', 'clear image']
    },
    'crosswalks': {
        'descriptions': [
            'white pedestrian crosswalk stripes on black asphalt road',
            'white zebra crossing lines on dark pavement',
            'bold white crossing stripes painted on street',
            'bright white crosswalk markings on gray road surface',
            'parallel white stripes of pedestrian crossing on asphalt',
            'white painted crosswalk lines on dark road',
            'pedestrian zebra crossing with thick white stripes on pavement',
            'white road markings for crosswalk on black asphalt'
        ],
        'views': ['top-down view', 'aerial view', 'overhead perspective', 'bird eye view', 'from above looking down', 'high angle view'],
        'lighting': ['daytime', 'bright daylight', 'clear sunny day', 'outdoor lighting', 'well-lit', 'natural light'],
        'details': ['photorealistic', 'detailed texture', 'realistic photo', 'sharp clear image', 'professional photography', 'high contrast']
    },
    'stairs': {
        'types': ['outdoor stairs', 'stone steps', 'concrete staircase', 'exterior stairs', 'outdoor stairway', 'building steps'],
        'materials': ['concrete', 'stone', 'gray concrete', 'cement', 'brick', 'granite'],
        'views': ['side view', 'angled view', 'front view', 'three-quarter view', 'diagonal view', 'perspective view'],
        'settings': ['outside building', 'outdoor', 'exterior', 'at entrance', 'on street', 'urban setting'],
        'features': ['with railings', 'with handrails', 'multiple steps', 'going up', 'ascending', 'steep'],
        'times': ['daylight', 'sunny day', 'afternoon', 'bright day', 'clear day', 'daytime'],
        'details': ['photorealistic', 'detailed texture', 'realistic photo', 'architectural photo', 'sharp focus', 'professional']
    },
}

def generate_dynamic_prompt(category, index):
    """
    Generate a dynamic prompt by combining variations from the template.
    Ensures variety across all images in a category.
    """
    if category not in VARIATION_TEMPLATES:
        return f"a {category}, photorealistic, detailed"

    template = VARIATION_TEMPLATES[category]

    # Use index to create deterministic but varied combinations
    # This ensures we don't repeat the same combination
    random.seed(index * 1000 + hash(category))

    # Build prompt by selecting one option from each variation dimension
    prompt_parts = []

    # Special handling for different categories
    if category == 'cars':
        car_type = random.choice(template['types'])
        color = random.choice(template['colors'])
        view = random.choice(template['views'])
        setting = random.choice(template['settings'])
        time = random.choice(template['times'])
        detail = random.choice(template['details'])
        prompt = f"a {color} {car_type} {setting}, {view}, {time}, {detail}"

    elif category == 'buses':
        bus_type = random.choice(template['types'])
        color = random.choice(template['colors'])
        view = random.choice(template['views'])
        setting = random.choice(template['settings'])
        time = random.choice(template['times'])
        detail = random.choice(template['details'])
        prompt = f"a {color} {bus_type} {setting}, {view}, {time}, {detail}"

    elif category == 'traffic_lights':
        light_type = random.choice(template['types'])
        state = random.choice(template['states'])
        view = random.choice(template['views'])
        setting = random.choice(template['settings'])
        time = random.choice(template['times'])
        detail = random.choice(template['details'])
        prompt = f"a {light_type} {state} {setting}, {view}, {time}, {detail}"

    elif category == 'fire_hydrants':
        hydrant_type = random.choice(template['types'])
        color = random.choice(template['colors'])
        view = random.choice(template['views'])
        setting = random.choice(template['settings'])
        time = random.choice(template['times'])
        detail = random.choice(template['details'])
        prompt = f"a {color} {hydrant_type} {setting}, {view}, {time}, {detail}"

    elif category == 'boats':
        boat_type = random.choice(template['types'])
        color = random.choice(template['colors'])
        view = random.choice(template['views'])
        setting = random.choice(template['settings'])
        time = random.choice(template['times'])
        detail = random.choice(template['details'])
        prompt = f"a {color} {boat_type} {setting}, {view}, {time}, {detail}"

    elif category == 'airplanes':
        plane_type = random.choice(template['types'])
        color = random.choice(template['colors'])
        view = random.choice(template['views'])
        setting = random.choice(template['settings'])
        time = random.choice(template['times'])
        detail = random.choice(template['details'])
        prompt = f"a {color} {plane_type} {setting}, {view}, {time}, {detail}"

    elif category == 'crosswalks':
        description = random.choice(template['descriptions'])
        view = random.choice(template['views'])
        lighting = random.choice(template['lighting'])
        detail = random.choice(template['details'])
        prompt = f"{description}, {view}, {lighting}, {detail}"

    elif category == 'stairs':
        stair_type = random.choice(template['types'])
        material = random.choice(template['materials'])
        view = random.choice(template['views'])
        setting = random.choice(template['settings'])
        feature = random.choice(template['features'])
        time = random.choice(template['times'])
        detail = random.choice(template['details'])
        prompt = f"{material} {stair_type} {feature} {setting}, {view}, {time}, {detail}"

    else:
        prompt = f"a {category}, photorealistic, detailed"

    # Reset random seed to current time for other random operations
    random.seed()

    return prompt

# Keep category list for iteration
CATEGORIES = list(VARIATION_TEMPLATES.keys())

# Background/distractor variations (no specific objects)
BACKGROUND_VARIATIONS = {
    'textures': ['grass', 'wooden planks', 'concrete wall', 'fabric', 'asphalt', 'brick wall', 'gravel', 'sand',
                 'pavement', 'tile', 'metal surface', 'glass', 'paper', 'leather', 'marble'],
    'colors': ['green', 'brown', 'gray', 'beige', 'white', 'dark gray', 'light gray', 'tan', 'neutral', 'natural'],
    'views': ['close-up', 'texture detail', 'surface view', 'flat view', 'overhead view', 'macro view'],
    'qualities': ['detailed', 'photorealistic', 'realistic photo', 'sharp focus', 'clear texture', 'natural lighting'],
    'descriptions': ['rough texture', 'smooth surface', 'fine grain', 'woven pattern', 'natural pattern', 'uniform texture']
}

def generate_background_prompt(index):
    """Generate dynamic background prompt with variations."""
    random.seed(index * 500 + 12345)

    texture = random.choice(BACKGROUND_VARIATIONS['textures'])
    color = random.choice(BACKGROUND_VARIATIONS['colors'])
    view = random.choice(BACKGROUND_VARIATIONS['views'])
    quality = random.choice(BACKGROUND_VARIATIONS['qualities'])
    description = random.choice(BACKGROUND_VARIATIONS['descriptions'])

    prompt = f"{color} {texture} {description}, {view}, {quality}"

    random.seed()
    return prompt

OUTPUT_DIR = '../statics/user_form/images/captcha'  # Relative to scripts/ folder
IMAGES_PER_CATEGORY = 12  # Generate multiple images per category
BACKGROUNDS_COUNT = 25  # Number of background images

# Generation parameters
INFERENCE_STEPS = 30  # Reduced for faster generation on Jetson
CFG_SCALE = 4.0
IMAGE_SIZE = 512  # Generation size (will be resized for CAPTCHA)


def create_output_dir():
    """Create output directory if it doesn't exist."""
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    print(f"✓ Created output directory: {OUTPUT_DIR}")


def load_model():
    """Load Qwen-Image model with GPU support."""
    print("\n🔄 Loading Qwen-Image model...")
    print("   This may take a few minutes on first run...")

    model_name = "Qwen/Qwen-Image"

    # Check for CUDA availability
    # Note: On Jetson, CUDA might not be detected by torch but the device still has a GPU
    if torch.cuda.is_available():
        # Try bfloat16 first, fallback to float16 if not supported
        try:
            torch_dtype = torch.bfloat16
            device = "cuda"
            print(f"   ✓ Using GPU (CUDA) with bfloat16")
        except:
            torch_dtype = torch.float16
            device = "cuda"
            print(f"   ✓ Using GPU (CUDA) with float16")
    else:
        torch_dtype = torch.float32
        device = "cpu"
        print(f"   ⚠ CUDA not detected - Using CPU (slower)")
        print(f"   Note: On Jetson, you may need to install a CUDA-enabled PyTorch version")

    try:
        pipe = DiffusionPipeline.from_pretrained(
            model_name,
            torch_dtype=torch_dtype,
            use_safetensors=True
        )
        pipe = pipe.to(device)

        # Enable memory optimizations
        if device == "cuda":
            pipe.enable_attention_slicing()
            pipe.enable_vae_slicing()
            print(f"   ✓ Enabled memory optimizations for GPU")

        print(f"✓ Model loaded successfully on {device}")
        return pipe, device

    except Exception as e:
        print(f"✗ Error loading model: {e}")
        print(f"\nTroubleshooting:")
        print(f"  1. Ensure you have enough disk space (model is ~10GB)")
        print(f"  2. Check internet connection for downloading model")
        print(f"  3. For GPU support, ensure CUDA PyTorch is installed")
        raise


def generate_image(pipe, prompt, device, seed=None):
    """Generate a single image from prompt."""
    try:
        # Set seed for reproducibility
        if seed is None:
            seed = random.randint(0, 2**32 - 1)

        generator = torch.Generator(device=device).manual_seed(seed)

        # Generate image
        result = pipe(
            prompt=prompt,
            num_inference_steps=INFERENCE_STEPS,
            guidance_scale=CFG_SCALE,
            generator=generator,
            height=IMAGE_SIZE,
            width=IMAGE_SIZE
        )

        # Extract image
        image = result.images[0]

        return image

    except Exception as e:
        print(f"  ✗ Error generating image: {e}")
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


def generate_category_images(pipe, device, categories_to_generate=None):
    """Generate images for each category using dynamic prompts.

    Args:
        pipe: The diffusion pipeline
        device: The device to use (cuda/cpu)
        categories_to_generate: List of category names to generate, or None for all
    """
    generated_count = 0

    # If no specific categories provided, generate all
    if categories_to_generate is None:
        categories_to_generate = CATEGORIES

    # Validate categories
    invalid_categories = [cat for cat in categories_to_generate if cat not in CATEGORIES]
    if invalid_categories:
        print(f"⚠ Warning: Invalid categories will be skipped: {', '.join(invalid_categories)}")
        categories_to_generate = [cat for cat in categories_to_generate if cat in CATEGORIES]

    if not categories_to_generate:
        print("✗ No valid categories to generate")
        return 0

    for category_name in categories_to_generate:
        print(f"\n🎨 Generating {category_name}...")

        category_dir = os.path.join(OUTPUT_DIR, category_name)
        os.makedirs(category_dir, exist_ok=True)

        # Generate images using dynamic prompt generation
        images_generated = 0

        for i in range(IMAGES_PER_CATEGORY):
            # Generate unique prompt for each image
            prompt = generate_dynamic_prompt(category_name, i)
            print(f"  Generating image {i + 1}/{IMAGES_PER_CATEGORY}...")
            print(f"  Prompt: {prompt[:80]}...")

            # Generate image
            img = generate_image(pipe, prompt, device)

            if img:
                # Ensure RGB mode
                if img.mode != 'RGB':
                    img = img.convert('RGB')

                # Apply CAPTCHA effects
                img = apply_captcha_effects(img)

                # Save with lower quality for realistic CAPTCHA appearance
                output_path = os.path.join(category_dir, f"{category_name}_{i:02d}.jpg")
                img.save(output_path, 'JPEG', quality=50)

                images_generated += 1
                generated_count += 1
                print(f"  ✓ Saved: {output_path}")
            else:
                print(f"  ✗ Failed to generate image {i + 1}")

        print(f"✓ Generated {images_generated} images for {category_name}")

    return generated_count


def generate_background_images(pipe, device):
    """Generate background/distractor images using dynamic prompts."""
    print(f"\n🎨 Generating background images...")

    bg_dir = os.path.join(OUTPUT_DIR, 'backgrounds')
    os.makedirs(bg_dir, exist_ok=True)

    generated = 0

    for i in range(BACKGROUNDS_COUNT):
        # Generate unique background prompt
        prompt = generate_background_prompt(i)
        print(f"  Generating background {i + 1}/{BACKGROUNDS_COUNT}...")
        print(f"  Prompt: {prompt[:80]}...")

        img = generate_image(pipe, prompt, device)

        if img:
            # Ensure RGB mode
            if img.mode != 'RGB':
                img = img.convert('RGB')

            img = apply_captcha_effects(img)
            output_path = os.path.join(bg_dir, f"bg_{i:02d}.jpg")
            img.save(output_path, 'JPEG', quality=50)
            generated += 1
            print(f"  ✓ Saved: {output_path}")
        else:
            print(f"  ✗ Failed to generate background {i + 1}")

    print(f"✓ Generated {generated} background images")
    return generated


def create_metadata():
    """Create metadata file listing all generated images."""
    import json

    metadata = {
        'categories': {},
        'backgrounds': [],
        'generation_info': {
            'model': 'Qwen-Image',
            'method': 'AI Generated',
            'inference_steps': INFERENCE_STEPS,
            'cfg_scale': CFG_SCALE
        }
    }

    # List category images
    for category_name in CATEGORIES:
        category_dir = os.path.join(OUTPUT_DIR, category_name)
        if os.path.exists(category_dir):
            images = sorted([f for f in os.listdir(category_dir) if f.endswith('.jpg')])
            metadata['categories'][category_name] = images

    # List background images
    bg_dir = os.path.join(OUTPUT_DIR, 'backgrounds')
    if os.path.exists(bg_dir):
        metadata['backgrounds'] = sorted([f for f in os.listdir(bg_dir) if f.endswith('.jpg')])

    # Write metadata
    metadata_path = os.path.join(OUTPUT_DIR, 'metadata.json')
    with open(metadata_path, 'w') as f:
        json.dump(metadata, f, indent=2)

    print(f"\n✓ Created metadata: {metadata_path}")


def parse_arguments():
    """Parse command line arguments."""
    parser = argparse.ArgumentParser(
        description='Generate CAPTCHA images using Qwen-Image AI model',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s                          # Generate all categories and backgrounds
  %(prog)s --category cars          # Generate only cars category
  %(prog)s --category cars buses    # Generate cars and buses categories
  %(prog)s --backgrounds-only       # Generate only background images
  %(prog)s --list                   # List all available categories
        """
    )

    parser.add_argument(
        '--category', '-c',
        nargs='+',
        metavar='NAME',
        help='Generate specific category/categories (space-separated)'
    )

    parser.add_argument(
        '--backgrounds-only', '-b',
        action='store_true',
        help='Generate only background images'
    )

    parser.add_argument(
        '--list', '-l',
        action='store_true',
        help='List all available categories and exit'
    )

    parser.add_argument(
        '--skip-backgrounds',
        action='store_true',
        help='Skip generating background images'
    )

    return parser.parse_args()


def main():
    args = parse_arguments()

    # Handle --list option
    if args.list:
        print("=" * 60)
        print("Available CAPTCHA Categories:")
        print("=" * 60)
        for i, category in enumerate(CATEGORIES, 1):
            print(f"  {i}. {category}")
        print(f"\nTotal: {len(CATEGORIES)} categories")
        print(f"Images per category: {IMAGES_PER_CATEGORY}")
        print(f"Background images: {BACKGROUNDS_COUNT}")
        print("=" * 60)
        return

    print("=" * 60)
    print("CAPTCHA Image Generator (Qwen-Image AI)")
    print("=" * 60)

    # Create output directory
    create_output_dir()

    # Determine what to generate
    generate_categories = not args.backgrounds_only
    generate_backgrounds = not args.skip_backgrounds and not (args.category and not args.backgrounds_only)

    if args.backgrounds_only:
        generate_backgrounds = True
        generate_categories = False

    # Load model
    pipe, device = load_model()

    category_count = 0
    bg_count = 0

    # Generate category images (objects to find)
    if generate_categories:
        if args.category:
            print(f"\n📋 Generating specific categories: {', '.join(args.category)}")
            category_count = generate_category_images(pipe, device, args.category)
        else:
            print(f"\n📋 Generating all {len(CATEGORIES)} categories")
            category_count = generate_category_images(pipe, device)

    # Generate background images (distractors)
    if generate_backgrounds:
        bg_count = generate_background_images(pipe, device)

    # Create metadata
    create_metadata()

    print("\n" + "=" * 60)
    print(f"✓ Generation complete!")
    if category_count > 0:
        print(f"  - Category images: {category_count}")
    if bg_count > 0:
        print(f"  - Background images: {bg_count}")
    print(f"  - Total: {category_count + bg_count}")
    print(f"  - Output directory: {OUTPUT_DIR}")
    print("=" * 60)


if __name__ == '__main__':
    main()
