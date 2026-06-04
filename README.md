# 🌸 thingy 🌸

<img src="./docs/header.png" alt="Header" width="280px" align="right" />

Listen up. **thingy** is here, and it's the toughest little blossom you'll ever cross. 👊

૮꒰ ˶• ༝ •˶꒱ა

You think pink is weak? Say that to ncurses' face. thingy is a heavyweight TUI editor packed into a tiny Sakura frame. It's built in C because we don't do that "high-level" garbage. It's cute, it's lethal, and it doesn't care about your feelings.

## 🎀 The Arsenal

- **Sakura Palette**: Visual intimidation via soft pinks. It's called psychological warfare. Look it up.
- **Aggressive Run**: Hit `^R`. thingy forces your code to execute. It doesn't ask. It commands.
- **Tactical Folds**: Hide your weaknesses. `^F` collapses code like a folding chair to the face.
- **The Pit**: A dedicated output panel where your errors go to die (in leaf-green, obviously).
- **Remote Fetch**: Paste a URL, thingy pulls the code and runs it. No questions asked.
- **Smart Language**: Auto-detects language or pick manually. thingy adapts.
- **Blossom Blitz**: Fast enough to flutter, hard enough to dent your terminal.

## 🌷 Deployment (Front toward enemy)

### Requirements

- `gcc`, `make`, `ncurses`, `libcurl`
- Linux or macOS

### Installation

1.  Secure the perimeter (clone the repo).
2.  Deploy to the objective (the project folder).
3.  Forge the weapon:
    ```bash
    make
    ```
4.  Execute:
    ```bash
    ./thingy your_file.c
    ```

### Run from CLI (no TUI)

Skip the editor, just run a file or URL directly:

```bash
./thingy --run your_file.c
./thingy --run https://example.com/main.txt
```

Force a specific language:

```bash
./thingy --run --lang python https://example.com/script.txt
```

## 🍬 Battle Stations

| Key  | Action                                       |
| :--- | :------------------------------------------- |
| `^S` | Backup. Real warriors prepare for the worst. |
| `^R` | Launch. Let 'em have it.                     |
| `^F` | Camouflage. Hide the mess.                   |
| `^O` | Intel. See what went wrong.                  |
| `^L` | Language select. Choose your weapon.         |
| `^Q` | Retreat. Live to fight another day.          |
| `Tab`| Insert 4 spaces. indentation is discipline.  |

### Output Panel

- `^O` opens/closes the output panel
- Scroll with mouse wheel or `Up`/`Down` keys when panel is focused
- Focus switches automatically based on mouse position
- Long lines wrap like a real terminal

### Language Selection

Press `^L` to pick a language (auto, c, python, node, ruby, php, perl, sh). Auto-detect works from file extension. Use `--lang` to override.

### Remote Files

thingy can fetch and run code from any URL:

```bash
./thingy https://example.com/main.txt
```

Or from inside the editor, just `^R` on a URL-opened file. Streams the content live while fetching.

---
