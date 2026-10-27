"""Resolve the retained repaired sprites used by the current pixel-art exporter."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REFERENCE = ROOT / 'assets/castle/reference'
ASSETS = {asset['id']: asset for asset in json.loads((REFERENCE / 'manifest.json').read_text())['assets']}
REPAIRED_ALIASES = {
    'laundry_basin_2': 'laundry_basin_1',
    'laundry_basin_3': 'laundry_basin_1',
    'laundry_basin_4': 'laundry_basin_1',
    'infirmary_candle_stool': 'workshop_candle_stool',
    'south_dorm_candle_stool': 'workshop_candle_stool',
}


def repaired_texture(key):
    key = REPAIRED_ALIASES.get(key, key)
    repaired = f'assets/castle/props/{key}.png'
    if (ROOT / repaired).is_file():
        return repaired
    return 'assets/castle/reference/' + ASSETS[key]['file']
