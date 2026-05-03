"""
Generate NeuralXP icon - stylized N with XP Luna blue gradient
Outputs neuralxp.ico with 16, 32, 48, 256 sizes
"""
from PIL import Image, ImageDraw, ImageFont
import os

def make_icon(size):
    # Create transparent background
    img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    # XP Luna gradient blue (top to bottom)
    top_color = (0, 88, 208)      # XP_HEADER_TOP
    bot_color = (4, 48, 136)      # XP_HEADER_BOT

    # Rounded square background with gradient
    radius = max(2, size // 8)
    margin = max(1, size // 32)

    # Draw rounded background pixel by pixel for smooth gradient
    for y in range(size):
        t = y / size
        r = int(top_color[0] + t * (bot_color[0] - top_color[0]))
        g = int(top_color[1] + t * (bot_color[1] - top_color[1]))
        b = int(top_color[2] + t * (bot_color[2] - top_color[2]))
        for x in range(size):
            # Rounded square mask
            in_rect = True
            # Check corners for rounding
            if x < radius and y < radius:
                dx = radius - x
                dy = radius - y
                if dx*dx + dy*dy > radius*radius:
                    in_rect = False
            elif x >= size - radius and y < radius:
                dx = x - (size - radius - 1)
                dy = radius - y
                if dx*dx + dy*dy > radius*radius:
                    in_rect = False
            elif x < radius and y >= size - radius:
                dx = radius - x
                dy = y - (size - radius - 1)
                if dx*dx + dy*dy > radius*radius:
                    in_rect = False
            elif x >= size - radius and y >= size - radius:
                dx = x - (size - radius - 1)
                dy = y - (size - radius - 1)
                if dx*dx + dy*dy > radius*radius:
                    in_rect = False
            if x < margin or x >= size - margin or y < margin or y >= size - margin:
                in_rect = False
            if in_rect:
                img.putpixel((x, y), (r, g, b, 255))

    # Draw stylized "N" - bold, geometric
    # We'll use simple rectangles for crisp look at small sizes
    nw = max(2, size // 8)        # stroke width
    pad = size // 4               # horizontal padding
    top = size // 5               # vertical padding top
    bot = size - size // 5        # vertical padding bottom
    n_color = (255, 255, 255, 255)

    # Left vertical
    draw.rectangle([pad, top, pad + nw, bot], fill=n_color)
    # Right vertical
    draw.rectangle([size - pad - nw, top, size - pad, bot], fill=n_color)

    # Diagonal stroke from top-left to bottom-right
    # Using polygon for smooth diagonal
    diag_pts = [
        (pad, top),                              # top-left of left bar
        (pad + nw, top),                         # top-right of left bar
        (size - pad, bot),                       # bottom-right of right bar
        (size - pad - nw, bot),                  # bottom-left of right bar
    ]
    draw.polygon(diag_pts, fill=n_color)

    # Add a small dot at top-right of the N to suggest "neural node" / period
    if size >= 32:
        dot_r = max(1, size // 16)
        dx = size - pad - nw // 2
        dy = top - dot_r * 2
        if dy > margin + dot_r:
            draw.ellipse([dx - dot_r, dy - dot_r, dx + dot_r, dy + dot_r], fill=n_color)

    return img

def main():
    sizes = [16, 32, 48, 64, 128, 256]
    images = []
    for s in sizes:
        img = make_icon(s)
        images.append(img)
        # Save individual PNG for preview
        png_path = f"icon_{s}.png"
        img.save(png_path)
        print(f"Generated {s}x{s}")

    # Save as multi-resolution ICO
    output = os.path.join(os.path.dirname(os.path.abspath(__file__)), "neuralxp.ico")
    images[0].save(output, format='ICO',
                   sizes=[(s, s) for s in sizes],
                   append_images=images[1:])
    print(f"Saved: {output}")

if __name__ == "__main__":
    main()
