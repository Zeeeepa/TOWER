#!/usr/bin/env python3
import re
import os
import sys

# List ALL icons currently in use - add existing ones here too!
all_icons = [
    'angle-left', 'angle-right', 'arrows-rotate', 'home', 'search', 'magic-wand-sparkles',
    'bars', 'close', 'gear', 'circle-info', 'globe', 'sun', 'cloud', 'clock',
    'location-dot', 'chart-simple', 'building', 'map', 'droplet', 'lightbulb', 'bolt',
    'gamepad', 'shield', 'link', 'gauge', 'database',
    # Developer Playground icons
    'play', 'pause', 'stop', 'plus', 'trash', 'code', 'check',
    'xmark', 'arrow-up', 'arrow-down', 'pen', 'arrow-up-from-bracket',
    # Homepage task icons
    'circle', 'comments', 'triangle-exclamation',
    # Weather icons
    'snowflake',
    # Developer Console icons
    'bug',
    # Element Picker icons
    'hand', 'redo', 'arrow-rotate-left',
    # Position Picker icons
    'location-arrow',
    # Automation Overlay icons
    'times',
    # Loading icons
    'circle-half-stroke', 'hourglass',
    # Proxy/VPN icons
    'shield-blank',
    # Output tab icons
    'clipboard',
    'arrow-down-to-line'
]

# Path to FontAwesome icons
FA_PATH = "/Users/ahstanin/Downloads/fontawesome-pro-plus-7.1.0-desktop/svgs-full/thumbprint-light"

def clean_svg_content(svg_content):
    """Clean and optimize SVG content for inline use"""
    # Remove XML declaration
    svg_content = re.sub(r'<\?xml[^>]*\?>', '', svg_content)
    # Remove Font Awesome comments (they're too long)
    svg_content = re.sub(r'<!--.*?-->', '', svg_content, flags=re.DOTALL)
    # Remove excessive whitespace but keep structure
    svg_content = re.sub(r'>\s+<', '><', svg_content)
    svg_content = svg_content.strip()
    return svg_content

def generate_icons_header(icons_list, output_file='icons.h'):
    """Generate complete icons.h file"""

    header_content = """#ifndef OLIB_ICONS_H_
#define OLIB_ICONS_H_

#include <string>

// FontAwesome Pro Plus 7.1.0 Thumbprint Light Icons
// Embedded as inline SVG for UI overlay

namespace OlibIcons {

"""

    # Add each icon
    for icon_name in icons_list:
        svg_file = os.path.join(FA_PATH, f"{icon_name}.svg")

        try:
            with open(svg_file, 'r') as f:
                svg_content = f.read()

            # Clean the SVG content
            svg_content = clean_svg_content(svg_content)

            # Convert to constant name (e.g., gamepad -> GAMEPAD, arrow-left -> ARROW_LEFT)
            const_name = icon_name.upper().replace('-', '_')

            # Create the C++ constant
            header_content += f'const std::string {const_name} = R"({svg_content})";\n\n'

            print(f"✓ Added {icon_name} as {const_name}")

        except FileNotFoundError:
            print(f"✗ Warning: {icon_name}.svg not found at {svg_file}")
            header_content += f'// {const_name} - file not found\n\n'

    # Close namespace and header guard
    header_content += """} // namespace OlibIcons

#endif // OLIB_ICONS_H_
"""

    # Write to output file
    with open(output_file, 'w') as f:
        f.write(header_content)

    print(f"\n✓ Generated {output_file}")
    return output_file

if __name__ == "__main__":
    # Check if output path is provided
    output_path = sys.argv[1] if len(sys.argv) > 1 else 'icons.h'
    generate_icons_header(all_icons, output_path)
