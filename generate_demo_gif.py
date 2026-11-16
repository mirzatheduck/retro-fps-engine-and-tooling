"""Render a deterministic CRT-style runtime and asset-pipeline demonstration."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import textwrap

from PIL import Image, ImageDraw, ImageFont
from rich.console import Console
from rich.progress import track

ROOT = Path(__file__).resolve().parent
GREEN = (51, 255, 102)
AMBER = (255, 176, 0)
DIM = (22, 86, 54)
MUTED = (108, 156, 130)
BG = (5, 12, 13)


def font(size: int) -> ImageFont.ImageFont:
    for name in ("consola.ttf", "cour.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            pass
    return ImageFont.load_default(size=size)


def pipeline_data() -> tuple[dict, str]:
    manifest = ROOT / "tooling_python/output/asset_manifest.json"
    if manifest.exists():
        data = json.loads(manifest.read_text(encoding="utf-8-sig"))
        assets = data["assets"]
        asset = next((a for a in assets if a["name"] == "retro_throwing_knife"), assets[0])
        return asset, "real JSON: tooling_python/output/asset_manifest.json"
    return {
        "tris_before": 14200, "tris_after": 480, "memory_saved_pct": 96.6,
        "compat_score": 98.0,
        "stages": [{"message": message} for message in (
            "[parse] retro_throwing_knife.obj: 14,200 tris",
            "[topology] duplicate verts: 36; degenerate faces: 12",
            "[topology] non-manifold edges: 0",
            "[clean] merged duplicates; removed degenerate faces",
            "[snap] PS1 vertex grid: 0.0625 units",
            "[decimate] triangle budget: 480; output: 480",
            "[memory] estimated memory saved: 96.6%",
            "[compat] compatibility score: 98.0 / 100",
            "[export] core_c/include/asset_manifest.h",
            "[export] JSON / Unreal CSV / Unity C# ready",
        )],
    }, "fallback simulation (manifest not present)"


def reticle(draw: ImageDraw.ImageDraw, x: float, y: float, color: tuple, lock=False):
    if lock:
        for sx in (-1, 1):
            for sy in (-1, 1):
                draw.line([(x + sx * 25, y + sy * 13),
                           (x + sx * 25, y + sy * 25),
                           (x + sx * 13, y + sy * 25)], fill=color, width=2)
    else:
        for sx, sy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            draw.line((x + sx * 7, y + sy * 7, x + sx * 18, y + sy * 18), color, 2)
        draw.ellipse((x - 2, y - 2, x + 2, y + 2), fill=color)


def crt_overlay() -> Image.Image:
    overlay = Image.new("RGBA", (1280, 480))
    d = ImageDraw.Draw(overlay)
    for y in range(0, 480, 2):
        d.line((0, y, 1279, y), fill=(0, 0, 0, 48))
    for inset in range(34):
        d.rectangle((inset, inset, 1279 - inset, 479 - inset),
                    outline=(0, 0, 0, int(62 * (1 - inset / 34))))
    return overlay


def render_frame(index: int, count: int, data: dict, source: str,
                 overlay: Image.Image) -> Image.Image:
    t = index / max(1, count - 1)
    im = Image.new("RGB", (1280, 480), BG)
    d = ImageDraw.Draw(im)
    small, normal, title = font(14), font(16), font(18)
    vx, vy = 320 + 12 * math.sin(t * math.tau), 235

    # Perspective tunnel: depth rings accelerate outward as the camera advances.
    for x, y in ((0, 34), (160, 34), (320, 34), (480, 34), (639, 34),
                 (0, 479), (160, 479), (320, 479), (480, 479), (639, 479)):
        d.line((vx, vy, x, y), DIM)
    for n in range(11):
        depth = ((n / 11 + t * 0.43) % 1) ** 2
        w, h = 18 + depth * 420, 12 + depth * 270
        # Clamp to the viewport so wireframe lines never bleed into the log.
        d.rectangle((max(0, vx-w), max(35, vy-h), min(639, vx+w), min(479, vy+h)),
                    outline=(22, int(65 + depth * 68), 59), width=1)

    tx, ty = 390 + 45 * math.sin(t * math.tau * 0.72), 237 + 24 * math.cos(t * math.tau)
    points = [(tx-18, ty-24), (tx+18, ty-24), (tx+18, ty+24), (tx-18, ty+24)]
    back = [(x+12, y-10) for x, y in points]
    d.polygon(points, outline=GREEN)
    d.polygon(back, outline=DIM)
    for p, q in zip(points, back):
        d.line((p, q), fill=GREEN)
    lock_blend = min(1, t * 5)
    lx, ly = 515 + (tx-515)*lock_blend, 180 + (ty-180)*lock_blend
    d.line((320, 250, lx, ly), fill=(120, 104, 33))
    reticle(d, 320, 250, AMBER)
    reticle(d, lx, ly, GREEN, lock=True)
    d.text((lx-24, ly+34), "LOCKED" if t >= .2 else "ACQUIRE", font=small, fill=GREEN)

    recall = t > .64
    phase = max(0, min(1, (t-.12)/.50)) if not recall else max(0, (1-t)/.36)

    def trajectory(u):
        return (565 + (tx-565)*u, 409 + (ty-409)*u - 100*math.sin(math.pi*u))

    for step in range(1, 12):
        u = phase + (step*.028 if recall else -step*.028)
        if 0 <= u <= 1:
            x, y = trajectory(u)
            d.ellipse((x-1, y-1, x+1, y+1), fill=AMBER if recall else MUTED)
    x, y = trajectory(phase)
    dx, dy = tx-565, ty-409-100*math.pi*math.cos(math.pi*phase)
    angle = math.atan2(dy, dx) + (math.pi if recall else 0)
    ca, sa = math.cos(angle), math.sin(angle)
    blade = [(x+a*ca-b*sa, y+a*sa+b*ca) for a, b in ((20, 0), (-5, -4), (-10, 0), (-5, 4))]
    d.polygon(blade, fill=AMBER, outline=(255, 226, 149))
    d.line((x-10*ca, y-10*sa, x-22*ca, y-22*sa), fill=GREEN, width=4)

    d.rectangle((12, 48, 358, 157), fill=(5, 16, 16), outline=DIM)
    combo = min(8, 1 + int(t*8))
    lines = ["STATE: BULLET_TIME_DASH", "TIME SCALE: 0.25x",
             f"COMBO: x{combo}" + (" ULTRA COMBO" if combo == 8 else " / CHAIN ACTIVE"),
             f"VELOCITY: {18 + 14*math.sin(t*math.pi):05.2f} m/s"]
    for row, line in enumerate(lines):
        d.text((22, 59+row*23), line, font=normal, fill=AMBER if row == 2 else GREEN)
    d.rectangle((12, 388, 332, 466), fill=(5, 16, 16), outline=DIM)
    d.text((22, 397), f"ARENA: {32.4*t:04.1f} KB / 1024 KB", font=normal, fill=GREEN)
    d.rectangle((22, 423, 318, 433), outline=DIM)
    d.rectangle((23, 424, 23+max(1, int(294*(32.4/1024)*t)), 432), fill=GREEN)
    d.text((22, 442), "RECALL <<" if recall else "KNIFE THROW >>", font=small, fill=AMBER)
    d.text((405, 446), f"60 FPS  FRAME {index+1:02}/{count:02}", font=small, fill=GREEN)

    # Terminal log uses the manifest's messages verbatim, wrapped to panel width.
    d.rectangle((640, 34, 1279, 479), fill=BG)
    d.text((660, 48), "$ mesh_and_config_pipeline.py", font=normal, fill=AMBER)
    messages = ["asset: retro_throwing_knife.obj"] + [s["message"] for s in data["stages"]]
    visible = min(len(messages), 1 + int(t*(len(messages)+2)))
    log = []
    for message in messages[:visible]:
        log.extend(textwrap.wrap(str(message), width=68, subsequent_indent="  ") or [""])
    for row, line in enumerate(log[-13:]):
        d.text((660, 79+row*18), line, font=small, fill=GREEN)
    if index % 2 == 0:
        cy = 79 + min(13, len(log))*18
        d.rectangle((660, cy, 668, cy+12), fill=GREEN)

    if t >= .66:
        d.rectangle((654, 337, 1265, 371), fill=(28, 49, 24), outline=GREEN)
        headline = f"{data['tris_before']:,} tris -> {data['tris_after']:,} tris (Low-Poly Ready)"
        d.text((665, 346), headline, font=normal, fill=AMBER)
        table = ["+----------------------+----------------------+",
                 "| MEMORY SAVED         | COMPAT SCORE         |",
                 f"| {data.get('memory_saved_pct', 0):>17.1f}%   | {data.get('compat_score', 0):>13.1f} / 100  |",
                 "+----------------------+----------------------+"]
        for row, line in enumerate(table):
            d.text((664, 380+row*17), line, font=small, fill=GREEN)
    d.text((660, 457), "SOURCE: MANIFEST JSON" if source.startswith("real") else "SOURCE: FALLBACK SIMULATION", font=small, fill=MUTED)
    for left, label in ((0, "RETRO FPS CORE // C99 + raylib"), (640, "ENGINE TOOLING // python pipeline")):
        d.rectangle((left, 0, left+639, 33), fill=(18, 37, 29))
        d.text((left+17, 8), label, font=title, fill=GREEN if left == 0 else AMBER)
    d.line((639, 0, 639, 479), fill=AMBER, width=2)
    return Image.alpha_composite(im.convert("RGBA"), overlay).convert("RGB")


def verify(path: Path, frames: int, ms: int) -> dict:
    with Image.open(path) as gif:
        duration = 0
        for frame in range(gif.n_frames):
            gif.seek(frame)
            duration += gif.info.get("duration", 0)
        result = {"is_animated": gif.is_animated, "n_frames": gif.n_frames,
                  "size": gif.size, "total_duration_ms": duration, "loop": gif.info.get("loop")}
        assert result["is_animated"] and result["n_frames"] == frames, result
        assert result["size"] == (1280, 480), result
        assert duration == frames*ms and result["loop"] == 0, result
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=ROOT / "assets/demo.gif")
    parser.add_argument("--frames", type=int, default=28)
    parser.add_argument("--ms", type=int, default=250)
    args = parser.parse_args()
    if args.frames < 2 or args.ms < 10 or args.ms % 10:
        parser.error("--frames must be >= 2; --ms must be a positive multiple of 10")
    console = Console()
    data, source = pipeline_data()
    console.print(f"[bold green]Data source:[/] {source}")
    overlay = crt_overlay()
    images = []
    for i in track(range(args.frames), description="Rendering CRT demonstration", console=console):
        frame = render_frame(i, args.frames, data, source, overlay)
        images.append(frame.convert("P", palette=Image.Palette.ADAPTIVE, colors=96))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    images[0].save(args.out, save_all=True, append_images=images[1:], duration=args.ms,
                   loop=0, optimize=True, disposal=2)
    result = verify(args.out, args.frames, args.ms)
    console.print("[bold green]Pillow verification:[/]", result)
    console.print(f"Saved {args.out}: {args.out.stat().st_size:,} bytes "
                  f"({args.out.stat().st_size / 1024**2:.2f} MiB)")


if __name__ == "__main__":
    main()
