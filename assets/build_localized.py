#!/usr/bin/env python3
"""Extract France French dialogue and build English, France, Quebec, and Beauce locales."""

import argparse
from pathlib import Path
import subprocess
import sys


REPO_ROOT = Path(__file__).resolve().parent.parent
RESTOOL = REPO_ROOT / 'assets' / 'restool.py'
DEFAULT_US_ROM = REPO_ROOT / 'zelda3.sfc'
DEFAULT_FR_ROM = REPO_ROOT / 'roms' / 'Legend_of_Zelda_The_A_Link_to_the_Past_(Fr).sfc'
QUEBEC_DIALOGUE = REPO_ROOT / 'localization' / 'dialogue_fr_c.txt'
BEAUCE_DIALOGUE = REPO_ROOT / 'localization' / 'dialogue_fr_beauce.txt'
SACRES_DIALOGUE = REPO_ROOT / 'localization' / 'dialogue_fr_ca_sacres.txt'
FR_DIALOGUE = REPO_ROOT / 'assets' / 'dialogue_fr.txt'
FR_FONT = REPO_ROOT / 'assets' / 'font_fr.png'


def run_restool(arguments):
  command = [sys.executable, str(RESTOOL)] + [str(arg) for arg in arguments]
  subprocess.run(command, cwd=REPO_ROOT, check=True)


def validate_rom(path, expected_language, parser):
  sys.path.insert(0, str(REPO_ROOT / 'assets'))
  import util
  try:
    rom = util.load_rom(str(path.resolve()), expected_language != 'us')
  except Exception as error:
    parser.error(str(error).strip())
  if rom.language != expected_language:
    parser.error('%s must be a %s ROM (detected %s).' %
                 (path, expected_language, rom.language or 'unsupported'))


def main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument('--us-rom', default=str(DEFAULT_US_ROM), help='US ROM used for game assets')
  parser.add_argument('--fr-rom', default=str(DEFAULT_FR_ROM), help='France French ROM used for dialogue extraction')
  parser.add_argument('--no-extract', action='store_true', help='reuse previously extracted France dialogue and font')
  parser.add_argument('--extract-only', action='store_true', help='only extract France dialogue and font')
  args = parser.parse_args()

  us_rom = Path(args.us_rom)
  fr_rom = Path(args.fr_rom)
  if not us_rom.is_absolute():
    us_rom = REPO_ROOT / us_rom
  if not fr_rom.is_absolute():
    fr_rom = REPO_ROOT / fr_rom

  if args.extract_only:
    if args.no_extract:
      parser.error('--extract-only cannot be combined with --no-extract')
    if not fr_rom.is_file():
      parser.error('France French ROM not found: %s' % fr_rom)
    validate_rom(fr_rom, 'fr', parser)
    run_restool(['--rom', fr_rom.resolve(), '--extract-dialogue'])
    print('France French dialogue and font extracted under assets/.')
    return 0

  if not us_rom.is_file():
    parser.error('US base ROM not found: %s' % us_rom)
  if not QUEBEC_DIALOGUE.is_file():
    parser.error('Quebec dialogue is not ready: create localization/dialogue_fr_c.txt before building.')
  if not BEAUCE_DIALOGUE.is_file():
    parser.error('Beauce slang dialogue is not ready: create localization/dialogue_fr_beauce.txt before building.')
  if not SACRES_DIALOGUE.is_file():
    parser.error('Quebec sacres dialogue is not ready: create localization/dialogue_fr_ca_sacres.txt before building.')
  validate_rom(us_rom, 'us', parser)

  if args.no_extract:
    if not FR_DIALOGUE.is_file() or not FR_FONT.is_file():
      parser.error('--no-extract requires assets/dialogue_fr.txt and assets/font_fr.png')
  else:
    if not fr_rom.is_file():
      parser.error('France French ROM not found: %s' % fr_rom)
    validate_rom(fr_rom, 'fr', parser)
    run_restool(['--rom', fr_rom.resolve(), '--extract-dialogue'])

  run_restool(['--rom', us_rom.resolve(), '--languages', 'fr,fr-c,fr-beauce,fr-ca-sacres'])
  print('Built zelda3_assets.dat with English, France French, familiar Quebec, Beauce slang, and Quebec sacres.')
  return 0


if __name__ == '__main__':
  try:
    sys.exit(main())
  except subprocess.CalledProcessError as error:
    sys.exit(error.returncode)
