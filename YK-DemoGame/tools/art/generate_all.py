"""Regenerates every generated asset of the demo game (art and audio) into ../../assets.

    python3 generate_all.py [output-assets-dir]

Everything is drawn or synthesized by the scripts in this folder from scratch: the demo ships no
third-party art or audio. Run it after changing a generator; the results are committed so the game
runs without Python. Needs Pillow and numpy.
"""
import os
import sys

import audio
import background
import characters
import decor
import hazards
import items
import mechanisms
import tiles

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.normpath(os.path.join(HERE, "..", "..", "assets"))
    for module in (characters, tiles, hazards, items, mechanisms, decor, background, audio):
        print("generating", module.__name__, "->", out)
        module.generate(out)


if __name__ == "__main__":
    main()
