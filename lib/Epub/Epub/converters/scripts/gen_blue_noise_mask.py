#!/usr/bin/env python3
"""
Generate BlueNoiseMask.h: a blue-noise dither threshold mask via Ulichney's
void-and-cluster method.

Produces an NxN array where each cell's rank determines the gray level at
which that pixel turns "on" during ordered dithering. Unlike the 4x4 Bayer
matrix in DitherUtils.h, the resulting pattern has no periodic structure --
its energy is concentrated in high spatial frequencies (hence "blue" noise),
which is what makes the dither pattern far less visible as a repeating tile
at normal reading distance.

Requires numpy (and, optionally, Pillow for the preview PNG):
    python3 -m venv .venv && source .venv/bin/activate
    pip install numpy pillow

Usage:
    python3 gen_blue_noise_mask.py

Regenerating with a different N or seed will change every value in the mask
-- only do this deliberately, since it changes the rendered dither pattern
for every image on-device.
"""
import sys
from pathlib import Path

import numpy as np

N = 64
SIGMA = 1.5
SEED = 20260701  # fixed seed -- reproducible mask, not re-randomized per run

SCRIPT_DIR = Path(__file__).resolve().parent
HEADER_OUT = SCRIPT_DIR.parent / "BlueNoiseMask.h"
PREVIEW_OUT = SCRIPT_DIR / "blue_noise_preview.png"


def toroidal_gaussian_energy(pattern, sigma):
    """Filtered energy at every cell using a toroidal (wraparound) Gaussian
    kernel, computed via FFT (equivalent to a wraparound convolution)."""
    size = pattern.shape[0]
    ax = np.arange(size)
    dx = np.minimum(ax, size - ax)
    d2 = dx[:, None] ** 2 + dx[None, :] ** 2
    kernel = np.exp(-d2 / (2 * sigma * sigma))
    kernel /= kernel.sum()
    return np.real(np.fft.ifft2(np.fft.fft2(pattern.astype(np.float64)) * np.fft.fft2(kernel)))


def tightest_cluster(ones_mask, energy):
    masked = np.where(ones_mask, energy, -np.inf)
    return np.unravel_index(np.argmax(masked), masked.shape)


def largest_void(ones_mask, energy):
    masked = np.where(~ones_mask, energy, np.inf)
    return np.unravel_index(np.argmin(masked), masked.shape)


def initial_binary_pattern(n, rng, fill_fraction=0.1):
    total = n * n
    count = max(1, int(total * fill_fraction))
    pattern = np.zeros((n, n), dtype=bool)
    idx = rng.choice(total, size=count, replace=False)
    pattern.flat[idx] = True
    return pattern


def refine_to_prototype(pattern, sigma, max_iters=10000):
    """Iteratively swap the tightest cluster for the largest void until the
    pattern is stable (the removed and added positions converge to the same
    cell) -- this is the Prototype Binary Pattern (PBP)."""
    pattern = pattern.copy()
    for _ in range(max_iters):
        energy = toroidal_gaussian_energy(pattern, sigma)
        cluster_pos = tightest_cluster(pattern, energy)
        pattern[cluster_pos] = False
        energy = toroidal_gaussian_energy(pattern, sigma)
        void_pos = largest_void(pattern, energy)
        pattern[void_pos] = True
        if cluster_pos == void_pos:
            return pattern
    raise RuntimeError("void-and-cluster did not converge")


def rank_pattern(prototype, sigma):
    """Three-phase ranking (Ulichney 1993): rank the PBP's own cells by
    removal order (phase 1), then grow ranks upward by repeatedly filling
    the largest void (phases 2-3) until every cell is ranked."""
    n = prototype.shape[0]
    total = n * n
    rank = np.full((n, n), -1, dtype=np.int64)

    pbp = prototype.copy()
    n0 = int(pbp.sum())
    r = n0 - 1
    while pbp.any():
        energy = toroidal_gaussian_energy(pbp, sigma)
        pos = tightest_cluster(pbp, energy)
        pbp[pos] = False
        rank[pos] = r
        r -= 1

    pbp = prototype.copy()
    r = n0
    half = total // 2
    while r < half:
        energy = toroidal_gaussian_energy(pbp, sigma)
        pos = largest_void(pbp, energy)
        pbp[pos] = True
        rank[pos] = r
        r += 1

    while r < total:
        energy = toroidal_gaussian_energy(~pbp, sigma)
        masked = np.where(pbp, -np.inf, energy)
        pos = np.unravel_index(np.argmax(masked), masked.shape)
        pbp[pos] = True
        rank[pos] = r
        r += 1

    assert (rank >= 0).all(), "every cell must be ranked"
    assert len(set(rank.flatten().tolist())) == total, "ranks must be unique"
    return rank


def radial_power_spectrum(mask01):
    """Radially-averaged FFT magnitude -- verifies the blue-noise signature:
    energy should rise with frequency, not be flat (white noise) or spiked
    at specific frequencies (periodic, like a Bayer matrix)."""
    n = mask01.shape[0]
    centered = mask01 - mask01.mean()
    spectrum = np.abs(np.fft.fftshift(np.fft.fft2(centered))) ** 2
    cy, cx = n // 2, n // 2
    y, x = np.indices((n, n))
    r = np.sqrt((x - cx) ** 2 + (y - cy) ** 2).astype(int)
    r_max = r.max()
    radial_mean = np.zeros(r_max + 1)
    for i in range(r_max + 1):
        vals = spectrum[r == i]
        radial_mean[i] = vals.mean() if len(vals) else 0
    return radial_mean


def write_header(mask, path):
    n = mask.shape[0]
    lines = [
        "#pragma once",
        "",
        "#include <stdint.h>",
        "",
        "// NxN blue-noise threshold mask, generated by",
        "// lib/Epub/Epub/converters/scripts/gen_blue_noise_mask.py via Ulichney's",
        "// void-and-cluster method. Unlike the 4x4 Bayer matrix in DitherUtils.h, this",
        "// has no periodic structure -- energy is concentrated in high spatial",
        "// frequencies, which makes the dither pattern far less visible as a repeating",
        "// tile at normal reading distance. Values are 0-255, a full permutation of the",
        "// range (every value appears exactly once), not a random distribution.",
        "// Regenerate with: python3 scripts/gen_blue_noise_mask.py",
        f"inline const uint8_t blueNoise64x64[{n}][{n}] = {{",
    ]
    for row in mask:
        row_str = ", ".join(str(int(v)) for v in row)
        lines.append(f"    {{{row_str}}},")
    lines.append("};")
    lines.append("")
    path.write_text("\n".join(lines))


def main():
    rng = np.random.default_rng(SEED)
    print(f"Generating {N}x{N} void-and-cluster blue-noise mask (sigma={SIGMA}, seed={SEED})...")
    ibp = initial_binary_pattern(N, rng)
    print(f"  initial pattern: {int(ibp.sum())} of {N * N} cells set")
    prototype = refine_to_prototype(ibp, SIGMA)
    print(f"  prototype binary pattern converged: {int(prototype.sum())} cells")
    rank = rank_pattern(prototype, SIGMA)
    print(f"  ranking complete, all {N * N} cells uniquely ranked")

    mask255 = (rank.astype(np.float64) * 255.0 / (N * N - 1)).round().astype(np.uint8)

    spectrum = radial_power_spectrum(mask255.astype(np.float64) / 255.0)
    low = spectrum[1:6].mean()
    mid = spectrum[10:20].mean()
    high = spectrum[25:32].mean()
    print("\nRadial power spectrum check (want low << mid < high for blue noise):")
    print(f"  low-freq  (r=1-5):   {low:.6f}")
    print(f"  mid-freq  (r=10-19): {mid:.6f}")
    print(f"  high-freq (r=25-31): {high:.6f}")
    is_blue = high > mid > low
    print(f"  Blue noise signature confirmed: {is_blue}")
    if not is_blue:
        print("FAILED validation -- spectrum does not show blue-noise characteristics", file=sys.stderr)
        sys.exit(1)

    write_header(mask255, HEADER_OUT)
    print(f"\nWrote {HEADER_OUT}")

    try:
        from PIL import Image

        img = Image.fromarray(mask255, mode="L").resize((512, 512), Image.NEAREST)
        img.save(PREVIEW_OUT)
        print(f"Wrote {PREVIEW_OUT} (visual sanity check)")
    except ImportError:
        print("(Pillow not installed -- skipped preview PNG, header generation is unaffected)")


if __name__ == "__main__":
    main()
