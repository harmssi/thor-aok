All Thor ports: [toravo.de/en/thor](https://toravo.de/en/thor/)

## Thor AoK 0.1.0: first release

**Age of Empires II on the AYN Thor, native, with a command deck on the second screen.** Based on [OpenAoK](https://github.com/Zaydme/openaok) by Zaydme. No game data included.

### What you need
- An AYN Thor (or another arm64 Android device; without a second screen the original interface is used).
- **Age of Empires II HD** from Steam (your own copy) and a PC or Mac with Python 3 to convert it once (see the README's Quick start).

### What's in it
- **Top screen:** the map, full screen, 60 fps.
- **Bottom screen:** wood, food, gold, stone, population and age · mini-map (tap or drag) · selection with hit points and queue · idle villager, idle army, Town Center, next age, delete (asks first), menu · the command grid as big tiles with the original icons.
- **HD converter:** `python3 android/package.py --hd <Age2HD>` turns the HD edition's files into the data folder the app imports.
- Launcher and deck in English, German, Spanish and French.

### Known limitations
- The Age of Kings civilisations and rules only (Conquerors content is not playable yet).
- No default hotkeys, no campaigns.

`ThorAoK-0.1.0.apk` · SHA-256 in `ThorAoK-0.1.0.apk.sha256`
