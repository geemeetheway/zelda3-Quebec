#!/usr/bin/env python3
"""Generate an offline, searchable side-by-side dialogue review page."""

import argparse
import html
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parent.parent
TAG_RE = re.compile(r'(\[[^\]]+\])')
LONG_TEXT = 180


def load_messages(path):
  messages = {}
  for raw in path.read_text(encoding='utf-8-sig').splitlines():
    if ': ' in raw:
      key, text = raw.split(': ', 1)
      messages[int(key)] = text
  return messages


def main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument('--us', type=Path, default=ROOT / 'assets' / 'dialogue.txt')
  parser.add_argument('--fr', type=Path, default=ROOT / 'assets' / 'dialogue_fr.txt')
  parser.add_argument('--fr-ca', type=Path, default=ROOT / 'localization' / 'dialogue_fr_c.txt')
  parser.add_argument('--beauce', type=Path, default=ROOT / 'localization' / 'dialogue_fr_beauce.txt')
  parser.add_argument('--sacres', type=Path, default=ROOT / 'localization' / 'dialogue_fr_ca_sacres.txt')
  parser.add_argument('--output', type=Path, default=ROOT / 'localization' / 'dialogue-preview.html')
  args = parser.parse_args()
  paths = {'English (US)': args.us, 'Français (France)': args.fr,
           'Québécois familier': args.fr_ca, 'Beauce (slang)': args.beauce,
           'Québécois (sacres)': args.sacres}
  for path in paths.values():
    if not path.exists():
      parser.error(f'missing dialogue file: {path}')
  locales = {label: load_messages(path) for label, path in paths.items()}
  ids = sorted(set().union(*(messages.keys() for messages in locales.values())))
  rows = []
  for message_id in ids:
    texts = {label: messages.get(message_id, '') for label, messages in locales.items()}
    present = [bool(text.strip()) for text in texts.values()]
    long_text = any(len(TAG_RE.sub('', text)) > LONG_TEXT for text in texts.values())
    # Message 1 is an intentionally empty sentinel in each locale source.
    rows.append({'id': message_id, 'texts': texts,
                 'missing': any(present) and not all(present), 'long': long_text})

  data = json.dumps(rows, ensure_ascii=False).replace('</', '<\\/')
  columns = ''.join(f'<section><h2>{html.escape(label)}</h2><div class="column" data-locale="{i}"></div></section>'
                   for i, label in enumerate(paths))
  page = f'''<!doctype html>
<html lang="fr"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Aperçu des dialogues — Zelda3</title><style>
:root{{color-scheme:dark;background:#111820;color:#edf3f5;font:15px/1.45 system-ui,sans-serif}}
*{{box-sizing:border-box}}body{{margin:0}}header{{position:sticky;top:0;background:#111820f2;border-bottom:1px solid #34434e;padding:1rem;z-index:2}}
.controls{{display:flex;gap:.6rem;flex-wrap:wrap;align-items:center}}h1{{font-size:1.2rem;margin:0 auto 0 0}}input,select{{background:#1b2630;color:inherit;border:1px solid #52616c;border-radius:.4rem;padding:.55rem;font:inherit}}
main{{padding:1rem;display:grid;grid-template-columns:repeat(5,minmax(22rem,1fr));gap:1rem;align-items:start;overflow-x:auto}}section{{min-width:22rem}}h2{{position:sticky;top:5rem;background:#111820;padding:.6rem 0;margin:0;z-index:1}}
.message{{background:#1b2630;border:1px solid #34434e;border-radius:.5rem;padding:.65rem;margin:.5rem 0;scroll-margin-top:8rem}}.message[hidden]{{display:none}}.id{{font-weight:700;color:#8dde9b}}.tags{{color:#8bb4d1;font-family:ui-monospace,monospace;overflow-wrap:anywhere}}.text{{white-space:pre-wrap;overflow-wrap:anywhere}}.badge{{color:#ffcc80;font-size:.8rem}}
@media(max-width:900px){{main{{grid-template-columns:1fr}}section{{min-width:0}}h2{{top:6rem}}}}
</style></head><body><header><div class="controls"><h1>Aperçu des dialogues</h1><input id="search" type="search" placeholder="Rechercher (ID ou texte)" aria-label="Rechercher"><select id="filter" aria-label="Filtrer"><option value="all">Tous les messages</option><option value="missing">Messages manquants</option><option value="long">Textes longs (&gt; {LONG_TEXT} caractères)</option></select><label><input type="checkbox" data-show="0" checked> EN</label><label><input type="checkbox" data-show="1" checked> FR</label><label><input type="checkbox" data-show="2" checked> QC</label><label><input type="checkbox" data-show="3" checked> Beauce</label><label><input type="checkbox" data-show="4" checked> QC sacres</label><span id="count"></span></div></header>
<main>{columns}</main><script>
const rows={data};const sections=[...document.querySelectorAll('section')];const tagRe=/\\[[^\\]]+\\]/g;
function esc(s){{return s.replace(/[&<>"']/g,c=>({{'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}}[c]))}}
for(const row of rows){{for(let i=0;i<sections.length;i++){{const card=document.createElement('article');card.className='message';card.dataset.id=row.id;card.dataset.missing=row.missing;card.dataset.long=row.long;const parts=row.texts[Object.keys(row.texts)[i]]||'';const rendered=esc(parts).replace(tagRe,t=>'<span class="tags">'+t+'</span>');card.innerHTML='<span class="id">#'+row.id+'</span> '+(row.missing?'<span class="badge">manquant</span>':'')+'<div class="text">'+rendered+'</div>';sections[i].querySelector('.column').append(card)}}}}
function update(){{const q=document.getElementById('search').value.toLocaleLowerCase();const filter=document.getElementById('filter').value;let shown=0;for(const card of document.querySelectorAll('.message')){{const matchText=(card.textContent+' '+card.dataset.id).toLocaleLowerCase().includes(q);const matchFilter=filter==='all'||card.dataset[filter]==='true';const section=card.closest('section');const locale=section.querySelector('.column').dataset.locale;const checked=document.querySelector('[data-show="'+locale+'"]').checked;card.hidden=!(matchText&&matchFilter&&checked);if(!card.hidden)shown++}}document.getElementById('count').textContent=shown+' cellule(s)'}}
document.getElementById('search').addEventListener('input',update);document.getElementById('filter').addEventListener('change',update);document.querySelectorAll('[data-show]').forEach(e=>e.addEventListener('change',update));update();
</script></body></html>'''
  args.output.parent.mkdir(parents=True, exist_ok=True)
  args.output.write_text(page, encoding='utf-8')
  print(f'Wrote {args.output.relative_to(ROOT) if args.output.is_relative_to(ROOT) else args.output} '
        f'with {len(ids)} message rows and {len(paths)} locale columns.')
  return 0


if __name__ == '__main__':
  raise SystemExit(main())
