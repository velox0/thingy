---
layout: page
title: Documentation
description: thingy documentation — keybinds, features, and usage.
---

## Keybinds

| Key | Action |
|:----|:-------|
| `^S` | Backup. Real warriors prepare for the worst. |
| `^R` | Launch. Let 'em have it. |
| `^F` | Camouflage. Hide the mess. |
| `^O` | Intel. See what went wrong. |
| `^L` | Language select. Choose your weapon. |
| `^Q` | Retreat. Live to fight another day. |
| `Tab` | Insert 4 spaces. Indentation is discipline. |

## Output Panel

- `^O` opens/closes the output panel
- Scroll with mouse wheel or `Up`/`Down` keys when panel is focused
- Focus switches automatically based on mouse position
- Long lines wrap like a real terminal

## Language Selection

Press `^L` to pick a language (auto, c, python, node, ruby, php, perl, sh). Auto-detect works from file extension. Use `--lang` to override.

## Remote Files

thingy can fetch and run code from any URL:

```bash
thingy https://example.com/main.txt
```

Or from inside the editor, just `^R` on a URL-opened file. Streams the content live while fetching.

## CLI Flags

| Flag | Description |
|:-----|:------------|
| `--run` | Skip the TUI, run a file or URL directly |
| `--lang` | Force a specific language for execution |

## Features

### Sakura Palette

Soft pinks for visual intimidation. Psychological warfare at its finest.

### Aggressive Run

Hit `^R`. thingy forces your code to execute. It doesn't ask. It commands.

### Tactical Folds

`^F` collapses code blocks. Hide your weaknesses.

### The Pit

A dedicated output panel where your errors go to die. In leaf-green, obviously.
