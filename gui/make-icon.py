"""
Generate NeuralXP icon - stylized N with XP Luna blue gradient
Outputs neuralxp.ico with 16, 32, 48, 64, 128, 256 sizes
"""
import math
import os
from PIL import Image, ImageDraw

TOP_COLOR = (0, 88, 208)  # matches XP_HEADER_TOP in ai-assistant.cpp
BOT_COLOR = (4, 48, 136)  # matches XP_HEADER_BOT in ai-assistant.cpp


def _lerp_color(c1, c2, t):
    return tuple(int(a + t * (b - a)) for a, b in zip(c1, c2))


def _corner_x_clip(y, size, radius):
    """Return (x_min, x_max) column range for row y after corner rounding."""
    if y < radius:
        dy = radius - y
    elif y >= size - radius:
        dy = y - (size - radius - 1)
    else:
        return 0, size - 1
    span = int(math.sqrt(max(0, radius * radius - dy * dy)))
    return radius - span, (size - radius - 1) + span


def make_icon(size):
    img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    radius = max(2, size // 8)
    margin = max(1, size // 32)

    for y in range(margin, size - margin):
        color = _lerp_color(TOP_COLOR, BOT_COLOR, y / size) + (255,)
        x0_corner, x1_corner = _corner_x_clip(y, size, radius)
        x0 = max(margin, x0_corner)
        x1 = min(size - margin - 1, x1_corner)
        if x0 <= x1:
            draw.line([(x0, y), (x1, y)], fill=color)

    nw = max(2, size // 8)
    pad = size // 4
    top = size // 5
    bot = size - size // 5
    white = (255, 255, 255, 255)

    draw.rectangle([pad, top, pad + nw, bot], fill=white)
    draw.rectangle([size - pad - nw, top, size - pad, bot], fill=white)
    draw.polygon([(pad, top), (pad + nw, top), (size - pad, bot), (size - pad - nw, bot)], fill=white)

    # dot above right bar: neural-node accent, only meaningful at 32px+
    if size >= 32:
        dot_r = max(1, size // 16)
        dx = size - pad - nw // 2
        dy = top - dot_r * 2
        if dy > margin + dot_r:
            draw.ellipse([dx - dot_r, dy - dot_r, dx + dot_r, dy + dot_r], fill=white)

    return img


def main():
    sizes = [16, 32, 48, 64, 128, 256]
    script_dir = os.path.dirname(os.path.abspath(__file__))
    images = []
    for s in sizes:
        img = make_icon(s)
        images.append(img)
        img.save(os.path.join(script_dir, f"icon_{s}.png"))
        print(f"Generated {s}x{s}")

    output = os.path.join(script_dir, "neuralxp.ico")
    images[0].save(output, format='ICO',
                   sizes=[(s, s) for s in sizes],
                   append_images=images[1:])
    print(f"Saved: {output}")


if __name__ == "__main__":
    main()
