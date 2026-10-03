"""PLAY MODES on the stock effects: the first test image.

Copy the playmodes/ directory to sdk/octabam/modules/playmodes/ and this
file to sdk/octabam/remixes/playmodes-test/remix.py, then from sdk/octabam:

    python3 modules/playmodes/generate.py
    make image REMIX=playmodes-test BUILD=12

No DSP module: both FX choosers stay stock's. static_stock keeps the stock
DSP code built in, as for the TapeHead test image.
"""
from remix.schema import Remix

REMIX = Remix(
    name="playmodes-test",
    doc="PLAY MODES (playhead direction per track) on the stock effects",
    modules=("PLAY MODES",
             "FILTER", "EQUALIZER", "DJ EQ", "PHASER", "FLANGER", "CHORUS",
             "SPATIALIZER", "COMB FILTER", "COMPRESSOR", "LO-FI", "DELAY",
             "PLATE REV", "SPRING REV", "DARK REV"),
    fallback="NONE",
    static_stock=True,
)
