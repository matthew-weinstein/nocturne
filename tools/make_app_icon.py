"""Draw the Nocturne app icon: a lit crescent on a night-blue squircle.

    cd tools
    python make_app_icon.py

Writes nocturne_pc/app_icon.png (1024 px) and nocturne_pc/app_icon.ico,
which build_app.py gives the app, its window and the taskbar. The tile is a
superellipse, deep blue with a lighter pool behind the moon; the crescent is
lit from the top left, with a shaded inner rim, a soft shadow and a faint
--acc glow. Shading only appears from 64 px, so small sizes stay crisp. The
crescent is centred by its outline, leaning a fifth of the way toward its
centre of mass. Each icon size is drawn at that size. Needs Pillow and numpy.
"""

from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter

TOOLS = Path(__file__).resolve().parent
PNG = TOOLS / "nocturne_pc" / "app_icon.png"
ICO = TOOLS / "nocturne_pc" / "app_icon.ico"
SIZE = 1024
SUPERSAMPLE = 4
ICO_SIZES = [16, 20, 24, 32, 40, 48, 64, 128, 256]
SHADING_MIN = 64
MEASURE_SIZE = 2048
MASS_LEAN = 0.2

SQUIRCLE_POWER = 5.0
MOON_RADIUS = 0.30
BITE_OFFSET = (0.135, -0.085)
BITE_RADIUS = 0.255
TIP_ROUNDING = 0.012

def colour(hex_code):
    channels = []
    for start in (1, 3, 5):
        channels.append(int(hex_code[start:start + 2], 16) / 255)
    return np.array(channels, dtype=np.float32)

TILE_EDGE = colour("#0A0E1F")
TILE_POOL = colour("#26305C")
SHADOW = colour("#03050D")
GLOW = colour("#8FA5FF")
MOON_SHADE = colour("#D9DDF0")
MOON_LIGHT = colour("#FFFFFF")
MOON_RIM = colour("#AEB8E0")

def coverage(signed_distance, pixel):
    return np.clip(0.5 - signed_distance / pixel, 0, 1)

def unit_grid(size):
    steps = (np.arange(size, dtype=np.float32) + 0.5) / size - 0.5
    return np.meshgrid(steps, steps)

def bite_distance(x, y, centre):
    bite_x = centre[0] + BITE_OFFSET[0]
    bite_y = centre[1] + BITE_OFFSET[1]
    return np.hypot(x - bite_x, y - bite_y)

def crescent_distance(x, y, centre):
    moon = np.hypot(x - centre[0], y - centre[1]) - (MOON_RADIUS - TIP_ROUNDING)
    bite = bite_distance(x, y, centre) - (BITE_RADIUS + TIP_ROUNDING)
    return np.maximum(moon, -bite) - TIP_ROUNDING

def optical_centre():
    """Where the moon's circle goes so the crescent looks centred in the tile."""
    x, y = unit_grid(MEASURE_SIZE)
    inside = crescent_distance(x, y, (0.0, 0.0)) <= 0
    xs = x[inside]
    ys = y[inside]
    box_x = (xs.min() + xs.max()) / 2
    box_y = (ys.min() + ys.max()) / 2
    lean_x = MASS_LEAN * (xs.mean() - box_x)
    return (-(box_x + lean_x), -box_y)

def squircle(x, y, pixel):
    half = 0.5 - pixel
    level = (np.abs(x) / half) ** SQUIRCLE_POWER + (np.abs(y) / half) ** SQUIRCLE_POWER
    distance = (level ** (1 / SQUIRCLE_POWER) - 1) * half
    return coverage(distance, pixel)

def blurred(mask, radius):
    image = Image.fromarray((np.clip(mask, 0, 1) * 255).astype(np.uint8))
    softened = image.filter(ImageFilter.GaussianBlur(radius))
    return np.asarray(softened, dtype=np.float32) / 255

def mix(base, layer, amount):
    weight = amount[..., None]
    return base * (1 - weight) + layer * weight

def filled(x, value):
    return np.ones(x.shape + (3,), dtype=np.float32) * value

def draw(size, centre, shaded):
    pixel = 1.0 / size
    x, y = unit_grid(size)

    pool = np.clip(1 - np.hypot(x * 0.9, y + 0.22) * 1.45, 0, 1)
    image = mix(filled(x, TILE_EDGE), TILE_POOL, pool)

    distance = crescent_distance(x, y, centre)
    mark = coverage(distance, pixel)
    if shaded:
        lowered = coverage(crescent_distance(x, y - 0.025, centre), pixel)
        image = mix(image, SHADOW, blurred(lowered, 0.03 * size) * 0.55)
        image = mix(image, GLOW, blurred(mark, 0.07 * size) * 0.22)

    light = np.clip((x - centre[0]) * -0.9 + (y - centre[1]) * -1.1 + 0.5, 0, 1)
    surface = mix(filled(x, MOON_SHADE), MOON_LIGHT, light)
    if shaded:
        rim = np.clip(1 - np.abs(bite_distance(x, y, centre) - BITE_RADIUS) / 0.06, 0, 1)
        surface = mix(surface, MOON_RIM, rim * 0.35)
    image = mix(image, surface, mark)

    rgba = np.dstack([np.clip(image, 0, 1), squircle(x, y, pixel)])
    return Image.fromarray((rgba * 255).round().astype(np.uint8))

def icon(size, centre):
    shaded = size >= SHADING_MIN
    return draw(size * SUPERSAMPLE, centre, shaded).resize((size, size), Image.BOX)

def main():
    centre = optical_centre()
    icon(SIZE, centre).save(PNG)

    images = []
    sizes = []
    for size in ICO_SIZES:
        images.append(icon(size, centre))
        sizes.append((size, size))
    images[-1].save(ICO, sizes=sizes, append_images=images[:-1])
    print(f"moon centre {centre[0]:+.3f}, {centre[1]:+.3f}; wrote {PNG} and {ICO}")

if __name__ == "__main__":
    main()
