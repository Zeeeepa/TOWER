#!/usr/bin/env python3
"""
AI Browser Comparison Chart Generator
Creates beautiful comparison charts for social media sharing
"""

import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import numpy as np
from matplotlib.patches import FancyBboxPatch
import textwrap

# Set style for beautiful charts
plt.style.use('seaborn-v0_8-darkgrid')
plt.rcParams['font.family'] = 'sans-serif'
plt.rcParams['font.sans-serif'] = ['Arial', 'Helvetica', 'DejaVu Sans']
plt.rcParams['font.size'] = 11

# Color scheme
COLORS = {
    'owl': '#00D9FF',      # Cyan - fast, modern
    'atlas': '#10A37F',     # OpenAI green
    'comet': '#6366F1',     # Perplexity purple
    'gemini': '#4285F4',    # Google blue
    'bg': '#1a1a2e',        # Dark background
    'text': '#ffffff',      # White text
    'grid': '#2d2d44',      # Grid lines
    'success': '#10b981',   # Green for checkmarks
    'fail': '#ef4444'       # Red for X marks
}

def create_speed_comparison():
    """Generate speed comparison chart"""
    fig, ax = plt.subplots(figsize=(14, 8), facecolor=COLORS['bg'])
    ax.set_facecolor(COLORS['bg'])

    # Data
    browsers = ['Owl Browser', 'ChatGPT Atlas', 'Perplexity Comet', 'Gemini 2.5\nComputer Use']
    times = [6, 32, 28, 30]
    colors = [COLORS['owl'], COLORS['atlas'], COLORS['comet'], COLORS['gemini']]

    # Create horizontal bar chart
    bars = ax.barh(browsers, times, color=colors, height=0.6, alpha=0.9,
                   edgecolor='white', linewidth=1.5)

    # Add value labels on bars
    for i, (bar, time) in enumerate(zip(bars, times)):
        width = bar.get_width()
        label = f'{time}s'
        # Place label inside bar for longer bars, outside for short bar
        if time > 10:
            ax.text(width - 2, bar.get_y() + bar.get_height()/2,
                    label, ha='right', va='center', color='white',
                    fontsize=16, fontweight='bold')
        else:
            ax.text(width + 0.5, bar.get_y() + bar.get_height()/2,
                    label, ha='left', va='center', color=COLORS['text'],
                    fontsize=16, fontweight='bold')

    # Styling
    ax.set_xlabel('Time to Navigate & Login (seconds)',
                  fontsize=15, color=COLORS['text'], fontweight='bold', labelpad=10)
    ax.set_title('SPEED COMPARISON\nNavigate to xyz.com & Complete Login Task',
                 fontsize=20, color=COLORS['text'], fontweight='bold', pad=20)
    ax.tick_params(colors=COLORS['text'], labelsize=13)
    ax.spines['bottom'].set_color(COLORS['grid'])
    ax.spines['left'].set_color(COLORS['grid'])
    ax.spines['top'].set_visible(False)
    ax.spines['right'].set_visible(False)
    ax.grid(axis='x', alpha=0.3, color=COLORS['grid'], linewidth=0.5)
    ax.set_xlim(0, 35)

    # Add "5.3x faster" annotation with arrow
    ax.annotate('5.3x\nFASTER!',
                xy=(6, 0), xytext=(16, 0.8),
                fontsize=18, fontweight='bold', color='#fbbf24',
                bbox=dict(boxstyle='round,pad=0.5', facecolor='#1a1a2e',
                         edgecolor='#fbbf24', linewidth=2),
                arrowprops=dict(arrowstyle='->', color='#fbbf24', lw=3,
                               connectionstyle='arc3,rad=0.3'))

    # Disclaimer at bottom
    disclaimer = ("* Benchmark results from internal testing. Performance may vary based on network conditions, hardware,\n"
                  "website complexity, and other factors. Results are representative of typical use cases.")
    fig.text(0.5, 0.02, disclaimer, ha='center', fontsize=9,
             color=COLORS['text'], alpha=0.7, style='italic')

    plt.tight_layout(rect=[0, 0.06, 1, 1])

    import os
    output_dir = os.path.join(os.path.dirname(os.path.dirname(__file__)), 'charts')
    os.makedirs(output_dir, exist_ok=True)
    output_path = os.path.join(output_dir, 'speed_comparison.png')

    plt.savefig(output_path, dpi=300, facecolor=COLORS['bg'],
                edgecolor='none', bbox_inches='tight')
    print(f"✓ Created: {output_path}")
    plt.close()

def create_features_table():
    """Generate features comparison table"""
    fig, ax = plt.subplots(figsize=(16, 12), facecolor=COLORS['bg'])
    ax.set_facecolor(COLORS['bg'])
    ax.axis('off')

    # Features data - using YES/NO instead of checkmarks to avoid font issues
    features = [
        ('On-Device AI (No Cloud)', 'YES', 'NO', 'NO', 'NO'),
        ('On-Device Vision Model', 'YES', 'NO', 'NO', 'NO'),
        ('Cloud Vision Support', 'NO', 'YES', 'YES', 'YES'),
        ('Native C++ Performance', 'YES', 'NO', 'NO', 'NO'),
        ('Built-in Ad Blocker', 'YES', 'NO', 'NO', 'NO'),
        ('LLM Guardrails (Security)', 'YES', 'NO', 'NO', 'NO'),
        ('Stealth Mode (Anti-Detection)', 'YES', 'NO', 'NO', 'NO'),
        ('No WebDriver Artifacts', 'YES', 'NO', 'NO', 'NO'),
        ('Offline AI Capabilities', 'YES', 'NO', 'NO', 'NO'),
        ('Privacy (No Cloud Upload)', 'YES', 'NO', 'NO', 'NO'),
        ('Metal GPU Acceleration', 'YES', 'N/A', 'N/A', 'N/A'),
        ('Natural Language Selectors', 'YES', 'YES', 'YES', 'YES'),
        ('Agent Mode', 'YES', 'YES', 'YES', 'YES'),
        ('Enterprise Ready', 'YES', 'NO', 'NO', 'NO'),
        ('Free Tier Available', 'YES', 'YES', 'YES', 'API'),
        ('Cross-Platform', 'Soon', 'YES', 'YES', 'API'),
    ]

    headers = ['Feature', 'Olib\nBrowser', 'ChatGPT\nAtlas', 'Perplexity\nComet', 'Gemini 2.5\nComputer Use']

    # Table dimensions
    n_rows = len(features) + 1  # +1 for header
    n_cols = len(headers)
    cell_height = 0.8
    cell_width = 2.8

    # Draw table
    for i in range(n_rows):
        for j in range(n_cols):
            # Get cell content
            if i == 0:
                text = headers[j]
                bg_color = COLORS['grid']
                text_color = COLORS['text']
                weight = 'bold'
                size = 11
            else:
                text = features[i-1][j]
                # Color code based on value
                if j == 0:
                    bg_color = COLORS['bg']
                    text_color = COLORS['text']
                    weight = 'normal'
                elif text == 'YES':
                    bg_color = COLORS['success'] + '30'  # Transparent green
                    text_color = COLORS['success']
                    weight = 'bold'
                elif text == 'NO':
                    bg_color = COLORS['fail'] + '30'  # Transparent red
                    text_color = COLORS['fail']
                    weight = 'bold'
                else:
                    bg_color = COLORS['bg']
                    text_color = COLORS['text']
                    weight = 'normal'
                size = 11

            # Draw cell background
            rect = FancyBboxPatch(
                (j * cell_width, (n_rows - i - 1) * cell_height),
                cell_width, cell_height,
                boxstyle="round,pad=0.05",
                facecolor=bg_color,
                edgecolor=COLORS['grid'],
                linewidth=0.5
            )
            ax.add_patch(rect)

            # Add text
            ax.text(
                j * cell_width + cell_width/2,
                (n_rows - i - 1) * cell_height + cell_height/2,
                text,
                ha='center', va='center',
                color=text_color,
                fontsize=size,
                fontweight=weight,
                wrap=True
            )

    # Title
    title = fig.text(0.5, 0.96, 'AI BROWSER FEATURE COMPARISON',
                     ha='center', fontsize=22, color=COLORS['text'],
                     fontweight='bold')

    subtitle = fig.text(0.5, 0.93, 'Owl Browser vs Commercial AI Browsers (2025)',
                        ha='center', fontsize=13, color=COLORS['text'],
                        alpha=0.8)

    # Legend
    legend_y = 0.04
    legend_items = [
        ('YES = Supported', COLORS['success']),
        ('NO = Not Available', COLORS['fail']),
        ('N/A = Not Applicable', COLORS['text']),
    ]

    legend_x_start = 0.15
    for i, (label, color) in enumerate(legend_items):
        fig.text(legend_x_start + i * 0.25, legend_y, label,
                ha='left', fontsize=11, color=color, fontweight='bold')

    # Disclaimer
    disclaimer = ("* Feature comparison based on publicly available documentation as of January 2025.\n"
                  "Features and availability subject to change. Visit official websites for the latest information.")
    fig.text(0.5, 0.01, disclaimer, ha='center', fontsize=8,
             color=COLORS['text'], alpha=0.6, style='italic')

    # Set axis limits
    ax.set_xlim(0, n_cols * cell_width)
    ax.set_ylim(0, n_rows * cell_height)
    ax.set_aspect('equal')

    import os
    output_dir = os.path.join(os.path.dirname(os.path.dirname(__file__)), 'charts')
    os.makedirs(output_dir, exist_ok=True)
    output_path = os.path.join(output_dir, 'features_comparison.png')

    plt.savefig(output_path, dpi=300, facecolor=COLORS['bg'],
                edgecolor='none', bbox_inches='tight')
    print(f"✓ Created: {output_path}")
    plt.close()

def create_performance_radar():
    """Generate radar chart comparing key metrics"""
    fig, ax = plt.subplots(figsize=(11, 11), subplot_kw=dict(projection='polar'),
                          facecolor=COLORS['bg'])
    ax.set_facecolor(COLORS['bg'])

    # Metrics (higher is better, normalized to 0-10)
    categories = ['Speed', 'Privacy', 'Features', 'Cost\n(Lower=Better)', 'Offline\nCapability']
    N = len(categories)

    # Scores (0-10 scale)
    owl_scores = [10, 10, 10, 10, 10]     # Fast, private, feature-rich, free, offline
    atlas_scores = [3, 4, 8, 6, 0]         # Slow, less private, good features, paid tiers, cloud-only
    comet_scores = [4, 4, 7, 7, 0]         # Slow, less private, good features, paid tiers, cloud-only
    gemini_scores = [4, 5, 6, 5, 0]        # Slow, cloud-based, fewer features, API costs, cloud-only

    # Compute angle for each axis
    angles = [n / float(N) * 2 * np.pi for n in range(N)]
    owl_scores += owl_scores[:1]
    atlas_scores += atlas_scores[:1]
    comet_scores += comet_scores[:1]
    gemini_scores += gemini_scores[:1]
    angles += angles[:1]

    # Plot - Olib with thicker line and more prominent
    ax.plot(angles, owl_scores, 'o-', linewidth=3.5, label='Owl Browser',
            color=COLORS['owl'], markersize=10, zorder=5)
    ax.fill(angles, owl_scores, alpha=0.3, color=COLORS['owl'], zorder=5)

    # Others with thinner lines
    ax.plot(angles, atlas_scores, 'o-', linewidth=2, label='ChatGPT Atlas',
            color=COLORS['atlas'], markersize=7, alpha=0.8, zorder=4)
    ax.fill(angles, atlas_scores, alpha=0.12, color=COLORS['atlas'], zorder=4)

    ax.plot(angles, comet_scores, 'o-', linewidth=2, label='Perplexity Comet',
            color=COLORS['comet'], markersize=7, alpha=0.8, zorder=3)
    ax.fill(angles, comet_scores, alpha=0.12, color=COLORS['comet'], zorder=3)

    ax.plot(angles, gemini_scores, 'o-', linewidth=2, label='Gemini 2.5 CU',
            color=COLORS['gemini'], markersize=7, alpha=0.8, zorder=2)
    ax.fill(angles, gemini_scores, alpha=0.12, color=COLORS['gemini'], zorder=2)

    # Fix axis to go in the right order and start at 12 o'clock
    ax.set_theta_offset(np.pi / 2)
    ax.set_theta_direction(-1)

    # Draw axis lines for each angle and label
    ax.set_xticks(angles[:-1])
    ax.set_xticklabels(categories, color=COLORS['text'], size=12, fontweight='bold')

    # Set y-axis
    ax.set_ylim(0, 10)
    ax.set_yticks([2, 4, 6, 8, 10])
    ax.set_yticklabels(['2', '4', '6', '8', '10'], color=COLORS['text'], size=10)
    ax.grid(color=COLORS['grid'], linewidth=0.5, alpha=0.5)

    # Styling
    ax.spines['polar'].set_color(COLORS['grid'])
    ax.tick_params(colors=COLORS['text'], labelsize=11)

    # Title
    plt.title('AI BROWSER PERFORMANCE METRICS\n(Higher is Better)',
              y=1.08, fontsize=20, color=COLORS['text'], fontweight='bold')

    # Legend - make it more prominent
    legend = ax.legend(loc='upper right', bbox_to_anchor=(1.35, 1.05),
                      fontsize=12, framealpha=0.95, facecolor=COLORS['bg'],
                      edgecolor=COLORS['owl'])
    legend.get_frame().set_linewidth(2)
    plt.setp(legend.get_texts(), color=COLORS['text'], fontweight='bold')

    # Disclaimer
    disclaimer = ("* Normalized scores (0-10 scale) based on feature analysis and benchmark testing.\nResults from internal evaluation.")
    fig.text(0.5, 0.04, disclaimer, ha='center', fontsize=9,
             color=COLORS['text'], alpha=0.7, style='italic')

    import os
    output_dir = os.path.join(os.path.dirname(os.path.dirname(__file__)), 'charts')
    os.makedirs(output_dir, exist_ok=True)
    output_path = os.path.join(output_dir, 'performance_radar.png')

    plt.savefig(output_path, dpi=300, facecolor=COLORS['bg'],
                edgecolor='none', bbox_inches='tight')
    print(f"✓ Created: {output_path}")
    plt.close()

def create_privacy_comparison():
    """Generate privacy & data comparison chart"""
    fig, ax = plt.subplots(figsize=(14, 8), facecolor=COLORS['bg'])
    ax.set_facecolor(COLORS['bg'])

    # Privacy metrics - changed "Open Source" to "Enterprise Security"
    metrics = ['Data Stays\nOn Device', 'No Cloud\nDependency', 'Works\nOffline',
               'No Usage\nTracking', 'Enterprise\nSecurity']
    browsers_list = ['Olib', 'Atlas', 'Comet', 'Gemini']

    # Data (1 = yes, 0 = no)
    data = np.array([
        [1, 1, 1, 1, 1],  # Olib - all privacy features + enterprise security
        [0, 0, 0, 0, 0],  # Atlas - cloud-based, no enterprise features
        [0, 0, 0, 0, 0],  # Comet - cloud-based, no enterprise features
        [0, 0, 0, 0, 0],  # Gemini - API-based, no enterprise features
    ])

    # Create grouped bar chart
    x = np.arange(len(metrics))
    width = 0.2

    for i, (browser, color) in enumerate(zip(browsers_list,
                                             [COLORS['owl'], COLORS['atlas'],
                                              COLORS['comet'], COLORS['gemini']])):
        offset = width * (i - 1.5)
        bars = ax.bar(x + offset, data[i], width, label=browser, color=color,
                     alpha=0.85, edgecolor='white', linewidth=1.5)

        # Add YES or NO text instead of symbols
        for j, (bar, value) in enumerate(zip(bars, data[i])):
            text = 'YES' if value == 1 else 'NO'
            text_color = COLORS['success'] if value == 1 else COLORS['fail']
            ax.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 0.05,
                   text, ha='center', va='bottom', color=text_color,
                   fontsize=13, fontweight='bold')

    # Styling
    ax.set_ylabel('Privacy Protection', fontsize=15, color=COLORS['text'], fontweight='bold', labelpad=10)
    ax.set_title('PRIVACY & DATA CONTROL COMPARISON\nWho Controls Your Data?',
                 fontsize=20, color=COLORS['text'], fontweight='bold', pad=20)
    ax.set_xticks(x)
    ax.set_xticklabels(metrics, fontsize=12, color=COLORS['text'], fontweight='bold')
    ax.set_ylim(0, 1.35)
    ax.set_yticks([])
    ax.tick_params(colors=COLORS['text'])
    ax.spines['bottom'].set_color(COLORS['grid'])
    ax.spines['left'].set_visible(False)
    ax.spines['top'].set_visible(False)
    ax.spines['right'].set_visible(False)
    ax.grid(axis='y', alpha=0.3, color=COLORS['grid'])

    # Legend - with border
    legend = ax.legend(loc='upper right', fontsize=13, framealpha=0.95,
                      facecolor=COLORS['bg'], edgecolor=COLORS['owl'])
    legend.get_frame().set_linewidth(2)
    plt.setp(legend.get_texts(), color=COLORS['text'], fontweight='bold')

    # Disclaimer
    disclaimer = ("* Analysis based on architecture and public documentation. Cloud-based browsers may offer privacy modes\n"
                  "but require internet connectivity and server communication for AI features.")
    fig.text(0.5, 0.02, disclaimer, ha='center', fontsize=9,
             color=COLORS['text'], alpha=0.6, style='italic')

    plt.tight_layout(rect=[0, 0.06, 1, 1])

    import os
    output_dir = os.path.join(os.path.dirname(os.path.dirname(__file__)), 'charts')
    os.makedirs(output_dir, exist_ok=True)
    output_path = os.path.join(output_dir, 'privacy_comparison.png')

    plt.savefig(output_path, dpi=300, facecolor=COLORS['bg'],
                edgecolor='none', bbox_inches='tight')
    print(f"✓ Created: {output_path}")
    plt.close()

def create_summary_infographic():
    """Create a summary infographic highlighting Olib's advantages"""
    fig = plt.figure(figsize=(12, 10), facecolor=COLORS['bg'])

    # Title - no emoji, properly centered
    fig.text(0.5, 0.95, 'Why Choose Owl Browser?',
             ha='center', va='center', fontsize=26, color=COLORS['text'],
             fontweight='bold', transform=fig.transFigure)

    fig.text(0.5, 0.91, 'The AI-First Browser Built BY AI FOR AI',
             ha='center', va='center', fontsize=15, color=COLORS['text'],
             alpha=0.8, transform=fig.transFigure)

    # Key advantages - no emojis
    advantages = [
        ('5x Faster', 'Native C++ performance\nvs cloud-based alternatives'),
        ('Privacy First', 'Your data stays on your device\nNo cloud uploads'),
        ('On-Device AI', 'Qwen3-VL-2B vision model\nWorks completely offline'),
        ('Secure by Design', 'Built-in guardrails prevent\nprompt injection attacks'),
        ('Maximum Stealth', 'No WebDriver detection\nUndetectable automation'),
        ('Enterprise Ready', 'Professional features\nFree tier available'),
        ('Vision Support', 'Multimodal AI with image\nunderstanding capabilities'),
        ('Metal GPU', 'Hardware acceleration\n50-200ms inference'),
    ]

    # Create grid layout - better positioning
    rows, cols = 4, 2

    # Calculate positions more carefully
    box_width = 0.38
    box_height = 0.14
    margin_left = 0.08
    margin_top = 0.80
    spacing_x = 0.44
    spacing_y = 0.165

    for i, (title, desc) in enumerate(advantages):
        row = i // cols
        col = i % cols

        # Calculate center position for each box
        x_center = margin_left + box_width/2 + col * spacing_x
        y_top = margin_top - row * spacing_y

        # Box position (top-left corner)
        box_x = x_center - box_width/2
        box_y = y_top - box_height

        # Box
        box = FancyBboxPatch((box_x, box_y), box_width, box_height,
                            boxstyle="round,pad=0.01",
                            facecolor=COLORS['grid'],
                            edgecolor=COLORS['owl'],
                            linewidth=2,
                            transform=fig.transFigure)
        fig.add_artist(box)

        # Title (centered in box, upper portion)
        fig.text(x_center, y_top - 0.04, title,
                ha='center', va='center', fontsize=15,
                color=COLORS['owl'], fontweight='bold',
                transform=fig.transFigure)

        # Description (centered in box, lower portion)
        fig.text(x_center, y_top - 0.09, desc,
                ha='center', va='center', fontsize=10,
                color=COLORS['text'], alpha=0.95,
                transform=fig.transFigure)

    # Bottom comparison - no emojis, properly centered
    fig.text(0.5, 0.08, 'Traditional AI Browsers',
             ha='center', va='center', fontsize=13, color=COLORS['text'],
             alpha=0.7, fontweight='bold', transform=fig.transFigure)

    fig.text(0.5, 0.05, 'Cloud Dependent  •  Slow Response  •  Privacy Concerns  •  Subscription Costs',
             ha='center', va='center', fontsize=11, color=COLORS['fail'],
             alpha=0.9, fontweight='bold', transform=fig.transFigure)

    # Call to action - properly centered
    fig.text(0.5, 0.01, 'github.com/Olib-AI/owl-browser  •  Built with Chromium + llama.cpp + Qwen3-VL-2B',
             ha='center', va='center', fontsize=9, color=COLORS['text'],
             alpha=0.7, style='italic', transform=fig.transFigure)

    # Save without tight bbox to preserve exact centering
    import os
    output_dir = os.path.join(os.path.dirname(os.path.dirname(__file__)), 'charts')
    os.makedirs(output_dir, exist_ok=True)
    output_path = os.path.join(output_dir, 'summary_infographic.png')

    plt.savefig(output_path, dpi=300, facecolor=COLORS['bg'],
                edgecolor='none')
    print(f"✓ Created: {output_path}")
    plt.close()

def main():
    """Generate all comparison charts"""
    print("\n🎨 Generating AI Browser Comparison Charts...\n")

    try:
        create_speed_comparison()
        create_features_table()
        create_performance_radar()
        create_privacy_comparison()
        create_summary_infographic()

        import os
        output_dir = os.path.join(os.path.dirname(os.path.dirname(__file__)), 'charts')

        print("\n✅ All charts generated successfully!")
        print(f"\n📁 Output directory: {output_dir}")
        print("\n📊 Charts created:")
        print("   • speed_comparison.png")
        print("   • features_comparison.png")
        print("   • performance_radar.png")
        print("   • privacy_comparison.png")
        print("   • summary_infographic.png")
        print("\n💡 These images are optimized for social media sharing (high DPI)")
        print("📌 Remember: Comparisons are for illustrative purposes with legal disclaimers included")

    except Exception as e:
        print(f"\n❌ Error generating charts: {e}")
        import traceback
        traceback.print_exc()

if __name__ == '__main__':
    main()
