# Startup menu artwork

The startup menu draws this original background illustration at the current display resolution, then layers its title, selection effects, and cover art separately:

- `startup_background.png` — original night-time kingdom map and stone-and-gold frame, generated for this menu; contains no text or logos.

Cover images remain separate resources and retain their original colors:

- `a-link-to-the-past.jpg` — https://m.media-amazon.com/images/I/71wfmH16Z8L.jpg
- `parallel-worlds.jpg` — https://retrogamersus.com/cdn/shop/products/ZeldaParallelWorlds3.jpg?v=1525472175

The images are loaded from this folder at startup. Keep their filenames and folder path in sync with `LoadStartupCovers()` in `src/main.c`.
