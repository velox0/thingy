---
layout: page
title: Installation
description: How to install thingy — the pink TUI editor.
---

## Requirements

- `gcc`, `make`, `ncurses`, `libcurl`
- Linux or macOS

## Homebrew (macOS)

```bash
brew tap velox0/brew
brew install thingy
```

## Install from source

Clone the repo and build:

```bash
git clone https://github.com/velox0/thingy.git
cd thingy
make
```

Run it:

```bash
thingy your_file.c
```

## Install system-wide

Lock it into position:

```bash
sudo make install
```

Defaults to `/usr/local/bin`. Change the drop zone if you need to:

```bash
make install PREFIX=~/.local
```

## Uninstall

Pull it out:

```bash
sudo make uninstall
```

## CLI mode (no TUI)

Skip the editor, just run a file or URL directly:

```bash
thingy --run your_file.c
thingy --run https://example.com/main.txt
```

Force a specific language:

```bash
thingy --run --lang python https://example.com/script.txt
```

## Language support

Press `^L` to pick a language or let auto-detect handle it from the file extension. Supported languages: **c**, **python**, **node**, **ruby**, **php**, **perl**, **sh**.

## Remote files

thingy can fetch and run code from any URL:

```bash
thingy https://example.com/main.txt
```

Or from inside the editor, just `^R` on a URL-opened file. Streams the content live while fetching.
