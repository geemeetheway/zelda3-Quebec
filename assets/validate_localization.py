#!/usr/bin/env python3
"""Validate locale dialogue IDs, control tags, glyphs, and estimated line widths."""

import re
import sys
import unicodedata
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'assets'))
import text_compression

TAG_RE = re.compile(r'\[[^\]]+\]')
LINE_BREAK_TAGS = {'[1]', '[2]', '[3]', '[Scroll]', '[Waitkey]'}
VARIABLE_TAGS = {'[Name]'}


def read_dialogue(path):
  messages = {}
  errors = []
  for lineno, raw in enumerate(path.read_text(encoding='utf-8-sig').splitlines(), 1):
    if not raw.strip():
      continue
    if ': ' not in raw:
      errors.append(f'{path.relative_to(ROOT)}:{lineno}: expected "ID: text"')
      continue
    key, text = raw.split(': ', 1)
    try:
      message_id = int(key)
    except ValueError:
      errors.append(f'{path.relative_to(ROOT)}:{lineno}: invalid message ID {key!r}')
      continue
    if message_id in messages:
      errors.append(f'{path.relative_to(ROOT)}:{lineno}: duplicate message ID {message_id}')
    messages[message_id] = text
  return messages, errors


def font_widths():
  font_path = ROOT / 'assets' / 'font_fr.png'
  if not font_path.exists():
    return None
  image = Image.open(font_path)
  pixels = image.load()
  widths = []
  for index in range(len(text_compression.kTextAlphabet_FR)):
    x = (index % 16) * 9
    y = (index // 16) * 17
    width = 8
    for offset in range(8):
      if pixels[x + offset, y] == 255:
        width = offset + 1
        break
    widths.append(width)
  return dict(zip(text_compression.kTextAlphabet_FR, widths))


def segment_widths(text, widths):
  alphabet = text_compression.kTextAlphabet_FR
  token_to_width = {token: widths[token] for token in alphabet if len(token) > 1}
  pieces = TAG_RE.split(text)
  tags = TAG_RE.findall(text)
  current = 0
  for index, piece in enumerate(pieces):
    for char in piece:
      if char in widths:
        current += widths[char]
    if index == len(tags):
      break
    tag = tags[index]
    if tag in LINE_BREAK_TAGS:
      yield current
      current = 0
    elif tag in VARIABLE_TAGS:
      # The game can substitute a player-selected name; six average glyphs is
      # a conservative estimate for the default name and most custom names.
      current += widths.get('L', 6) + widths.get('i', 6) + widths.get('n', 6) + widths.get('k', 6)
    elif tag.startswith('[Number '):
      current += 2 * widths.get('0', 6)
    elif tag in token_to_width:
      current += token_to_width[tag]
  yield current


def main():
  parser = __import__('argparse').ArgumentParser(description=__doc__)
  parser.add_argument('--us', type=Path, default=ROOT / 'assets' / 'dialogue.txt')
  parser.add_argument('--fr', type=Path, default=ROOT / 'assets' / 'dialogue_fr.txt')
  parser.add_argument('--fr-ca', type=Path, default=ROOT / 'localization' / 'dialogue_fr_c.txt')
  parser.add_argument('--beauce', type=Path, default=ROOT / 'localization' / 'dialogue_fr_beauce.txt')
  parser.add_argument('--sacres', type=Path, default=ROOT / 'localization' / 'dialogue_fr_ca_sacres.txt')
  parser.add_argument('--max-line-width', type=int, default=224,
                      help='pixel warning threshold for dialogue line estimates (default: 224)')
  args = parser.parse_args()

  loaded = {}
  errors = []
  for locale, path in (('us', args.us), ('fr', args.fr), ('fr-c', args.fr_ca),
                       ('fr-beauce', args.beauce), ('fr-ca-sacres', args.sacres)):
    if not path.exists():
      errors.append(f'{locale}: file not found: {path}')
      continue
    loaded[locale], file_errors = read_dialogue(path)
    errors.extend(file_errors)

  if errors:
    for error in errors:
      print(f'ERROR: {error}')
    return 1
  reference_ids = sorted(loaded['us'])
  expected_ids = list(range(1, len(reference_ids) + 1))
  if reference_ids != expected_ids:
    errors.append(f'us: IDs are not sequential from 1 ({len(reference_ids)} messages)')

  for locale in ('fr', 'fr-c', 'fr-beauce', 'fr-ca-sacres'):
    ids = sorted(loaded[locale])
    if ids != reference_ids:
      missing = sorted(set(reference_ids) - set(ids))
      extra = sorted(set(ids) - set(reference_ids))
      errors.append(f'{locale}: message coverage differs; missing={missing[:12]} extra={extra[:12]}')
      continue
    for message_id in reference_ids:
      source_tags = TAG_RE.findall(loaded['fr'][message_id])
      translated_tags = TAG_RE.findall(loaded[locale][message_id])
      if source_tags != translated_tags:
        common = min(len(source_tags), len(translated_tags))
        mismatch = next((i for i in range(common) if source_tags[i] != translated_tags[i]), common)
        errors.append(f'{locale} message {message_id}: command tag sequence differs '
                      f'at token {mismatch + 1}; expected={source_tags[mismatch:mismatch + 4]} '
                      f'found={translated_tags[mismatch:mismatch + 4]}')

  widths = font_widths()
  if widths:
    for locale in ('fr-c', 'fr-beauce', 'fr-ca-sacres'):
      unsupported = sorted({char for text in loaded[locale].values()
                            for char in TAG_RE.sub('', text) if char not in widths})
      if unsupported:
        descriptions = [f'U+{ord(ch):04X} {unicodedata.name(ch, "UNKNOWN")}' for ch in unsupported]
        errors.append(f'{locale}: unsupported glyphs: ' + ', '.join(descriptions))
      width_warnings = []
      for message_id, text in loaded[locale].items():
        max_width = max(segment_widths(text, widths), default=0)
        if max_width > args.max_line_width:
          width_warnings.append((message_id, max_width))
      for message_id, width in width_warnings:
        print(f'WARNING: {locale} message {message_id}: estimated line width {width}px '
              f'exceeds {args.max_line_width}px')
      print(f'Checked {locale} with French font metrics: {len(widths)} glyphs; '
            f'{len(width_warnings)} line-width warning(s).')
  else:
    print('WARNING: assets/font_fr.png is absent; glyph and width checks skipped.')

  if errors:
    for error in errors:
      print(f'ERROR: {error}')
    return 1
  print(f'Localization IDs and ordered command tags match across '
        f'{len(reference_ids)} messages (US, France French, Quebec familiar, Beauce slang, Quebec sacres).')
  return 0


if __name__ == '__main__':
  raise SystemExit(main())
