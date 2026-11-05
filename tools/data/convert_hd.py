"""Lays out Age of Empires II HD's files (Steam, 2013) as the engine reads them.

HD keeps the archives' members as loose files under resources/_common/drs/
and draws its terrain from PNG textures. This writes, all ignored by git:

  assets/original/Data/{graphics,interfac,sounds,gamedata,terrain}.drs
  assets/original/Data/{blendomatic.dat,BlkEdge.Dat,TileEdge.Dat}
  original_game_1999/SYSTEM/FONTS/        HD's fonts, in the CD's upper case

The dat (empires2_x1_p1.dat, The Conquerors' format) and the strings
(key-value-strings-utf8.txt) are read where HD keeps them; see
prepare_data.py --hd.

Usage:  python3 tools/data/convert_hd.py <Age2HD folder>
"""
import os
import re
import shutil
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
DATA = os.path.join(ROOT, 'assets', 'original', 'Data')
FONTS = os.path.join(ROOT, 'original_game_1999', 'SYSTEM', 'FONTS')

# gamedata.drs ids the engine and extractors look for.
RANDOM_MAP_DEF = 54000
FIRST_SCRIPT = 54201
FIRST_AI = 60001
# src/ai/Brain.cpp kFiles, in id order from 60001.
AI_FILES = [
    'ai petersen rules', 'petersen deathmatch', 'petersen upgrades',
    'petersen fishboat', 'petersen dip liar', 'petersen warboat',
    'petersen supplement', 'petersen difficulty loads', 'petersen full tech',
    'petersen map loads', 'petersen civ loads', 'petersen tower',
    'petersen diplomacy', 'petersen castle', 'petersen resign',
    'petersen wonder', 'petersen market', 'petersen dip boomer',
    'petersen gather', 'petersen dip feeder', 'petersen groups',
    'petersen dip insult', 'petersen dip bully', 'petersen warboat island',
    'petersen rush', 'randomgame', 'petersen constants',
]
# Terrain art: each texture tiles every PERIOD tiles, cut into PERIOD^2
# 97x49 diamonds in the order TerrainRenderer::PatternCell reads them.
PERIOD = 8
TILE_W, TILE_H = 97, 49
TYPES = {'slp': b' pls', 'bina': b'anib', 'wav': b' vaw'}


def write_drs(path, files):
    """files: {id: (ext, bytes)} -> a DRS archive, one table per type."""
    tables = {}
    for fid in sorted(files):
        tables.setdefault(files[fid][0], []).append(fid)
    header = 64 + 12 * len(tables)
    offset = header + 12 * len(files)
    table_rows, entries, blobs = b'', b'', []
    entry_at = header
    for ext, ids in tables.items():
        table_rows += struct.pack('<4sII', TYPES.get(ext, b' txt'), entry_at, len(ids))
        for fid in ids:
            data = files[fid][1]
            entries += struct.pack('<III', fid, offset, len(data))
            blobs.append(data)
            offset += len(data)
        entry_at += 12 * len(ids)
    with open(path, 'wb') as fh:
        fh.write(b'Copyright (c) 1997 Ensemble Studios.\x1a\0\0\0'[:40].ljust(40, b'\0'))
        fh.write(b'1.00' + b'tribe'.ljust(12, b'\0'))
        fh.write(struct.pack('<II', len(tables), header + 12 * len(files)))
        fh.write(table_rows + entries)
        for blob in blobs:
            fh.write(blob)


def loose(folder, exts):
    """{id: (ext, bytes)} of the numbered files in `folder`."""
    out = {}
    for name in os.listdir(folder):
        stem, _, ext = name.partition('.')
        if stem.isdigit() and ext.lower() in exts:
            with open(os.path.join(folder, name), 'rb') as fh:
                out[int(stem)] = (ext.lower(), fh.read())
    return out


def gamedata(src):
    """The map and AI scripts under the ids the 1999 archive uses."""
    files = {}
    text = {}
    for name in os.listdir(src):
        with open(os.path.join(src, name), 'rb') as fh:
            text[name.lower()] = fh.read()
    names = {n.lower(): n for n in os.listdir(src)}
    # The scripts name their own ids (`#include_drs Arabia.rms 54201`).
    ids = {}
    for data in text.values():
        for m in re.finditer(rb'#include_drs\s+(\S+)\s+(\d+)', data):
            ids.setdefault(m.group(1).decode('latin1').lower(), int(m.group(2)))
    listing = []
    next_id = max([FIRST_SCRIPT] + [i for n, i in ids.items() if n.endswith('.rms')]) + 1
    for name in sorted(text):
        if name.endswith('.rms') or name.endswith('.inc'):
            fid = ids.get(name)
            if fid is None or fid in files:
                fid, next_id = next_id, next_id + 1
            files[fid] = ('rms', text[name])
            if name.endswith('.rms'):
                # HD includes random_map.def by itself; the CD's scripts say so.
                files[fid] = ('rms', b'#include_drs random_map.def %d\r\n' % RANDOM_MAP_DEF
                              + text[name])
                listing.append('%s %d' % (names[name], fid))
    # random_map.def opens with the list of scripts and their ids, as on the CD.
    files[RANDOM_MAP_DEF] = ('rms', ('/* %s */\n' % '\n'.join(listing)).encode('latin1') +
                             text['random_map.def'])
    # The Conquerors' computer player loads "elite petersen ..." files; the
    # engine knows them by the 1999 names.
    for i, name in enumerate(AI_FILES):
        data = text.get('elite %s.per' % name, text.get('%s.per' % name, b''))
        files[FIRST_AI + i] = ('per', data.replace(b'"elite ', b'"'))
    return files


def palette(path):
    lines = open(path, encoding='latin1').read().split()
    values = [int(v) for v in lines[3:3 + 256 * 3]]
    return [tuple(values[i:i + 3]) for i in range(0, 768, 3)]


def diamond():
    """(left, width) of each row of a terrain tile."""
    rows = []
    for y in range(TILE_H):
        width = 1 + 4 * y if y < TILE_H // 2 else 2 * TILE_H - 1 - 4 * (y - TILE_H // 2)
        rows.append(((TILE_W - width) // 2, width))
    return rows


def encode_slp(frames):
    """frames: list of rows of palette indices (diamond rows) -> SLP 2.0N."""
    rows = diamond()
    out = bytearray(b'2.0N' + struct.pack('<HH', len(frames), 0) + b'ArtDesk 1.00 SLP Writer\0')
    body_at = 32 + 32 * len(frames)
    bodies = bytearray()
    headers = bytearray()
    for frame in frames:
        base = body_at + len(bodies)
        outline = bytearray()
        for left, width in rows:
            outline += struct.pack('<HH', left, TILE_W - left - width)
        cmd_at = base + len(outline)
        cmds = bytearray()
        offsets = []
        for indices in frame:
            offsets.append(cmd_at + 4 * TILE_H + len(cmds))
            for i in range(0, len(indices), 63):
                run = indices[i:i + 63]
                cmds.append(len(run) << 2)
                cmds += bytes(run)
            cmds.append(0x0F)
        headers += struct.pack('<IIIIiiii', cmd_at, base, 0, 0x10, TILE_W, TILE_H, TILE_W // 2, 0)
        bodies += outline + b''.join(struct.pack('<I', o) for o in offsets) + cmds
    return bytes(out + headers + bodies)


def terrain(textures, dat_terrains, pal):
    """terrain.drs: each dat terrain's texture cut into diamonds."""
    import numpy as np
    from PIL import Image
    colours = np.array(pal, dtype=np.int32)
    side = int(PERIOD * 48.5)
    rows = diamond()
    found = {n.lower().split('_00_')[0]: n for n in os.listdir(textures)}
    files = {}
    for slp_id, name in sorted(dat_terrains.items()):
        if name.lower() not in found:
            print('  no texture for %s (%d)' % (name, slp_id))
            continue
        image = Image.open(os.path.join(textures, found[name.lower()])).convert('RGB')
        texels = np.asarray(image.resize((side, side), Image.LANCZOS), dtype=np.int32)
        frames = []
        for column in range(PERIOD):
            for row in range(PERIOD):
                tile_y = (PERIOD - row) % PERIOD
                frame = []
                for y, (left, width) in enumerate(rows):
                    px = np.arange(left, left + width) + 0.5 - TILE_W / 2
                    py = y + 0.5
                    u = column + (px / 48.5 + py / 24.5) / 2
                    v = tile_y + (py / 24.5 - px / 48.5) / 2
                    tx = (np.floor(u / PERIOD * side).astype(int)) % side
                    ty = (np.floor(v / PERIOD * side).astype(int)) % side
                    rgb = texels[ty, tx]
                    nearest = ((rgb[:, None, :] - colours[None, :, :]) ** 2).sum(-1).argmin(1)
                    frame.append(nearest.astype(np.uint8).tolist())
                frames.append(frame)
        files[slp_id] = ('slp', encode_slp(frames))
    return files


def music(sound):
    """HD's music under the CD's names in GAME/SOUND/STREAM (src/audio/Music)."""
    out = os.path.join(ROOT, 'original_game_1999', 'GAME', 'SOUND', 'STREAM')
    os.makedirs(out, exist_ok=True)
    pairs = [('stream/' + n, n.upper()) for n in ('open.mp3', 'credits.mp3', 'won1.mp3', 'lost.mp3')]
    pairs += [('civ/' + n + '.mp3', n.upper() + '.MP3') for n in (
        'random', 'british', 'french', 'goth', 'teuton', 'japanese', 'chinese', 'byzantin', 'persian',
        'saracen', 'turk', 'viking', 'mongol', 'celt')]
    # The in-game playlist: the 1999 score, then The Conquerors' first tracks.
    pairs += [('music/music1.mp3', 'MUSIC1.MP3')]
    pairs += [('music/xmusic%d.mp3' % i, 'MUSIC%d.MP3' % (i + 1)) for i in range(1, 8)]
    for src, dst in pairs:
        path = os.path.join(sound, *src.split('/'))
        if os.path.exists(path):
            shutil.copyfile(path, os.path.join(out, dst))


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    common = os.path.join(sys.argv[1], 'resources', '_common')
    drs = os.path.join(common, 'drs')
    os.makedirs(DATA, exist_ok=True)

    print('  graphics.drs, interfac.drs, sounds.drs')
    write_drs(os.path.join(DATA, 'graphics.drs'), loose(os.path.join(drs, 'graphics'), {'slp'}))
    interface = loose(os.path.join(drs, 'interface'), {'slp', 'bina', 'wav'})
    # HD has one interface panel per screen size, the CD one per civilisation
    # (51101 + civ - 1, and so on): every civilisation gets HD's.
    for base in (51101, 51121, 51141):
        for civ in range(1, 18):
            interface.setdefault(base + civ, interface[base])
    write_drs(os.path.join(DATA, 'interfac.drs'), interface)
    write_drs(os.path.join(DATA, 'sounds.drs'), loose(os.path.join(drs, 'sounds'), {'wav'}))
    print('  gamedata.drs')
    write_drs(os.path.join(DATA, 'gamedata.drs'), gamedata(os.path.join(drs, 'gamedata_x1')))

    for src, dst in (('blendomatic_x1.dat', 'blendomatic.dat'),
                     ('BlkEdge.Dat', 'BlkEdge.Dat'), ('TileEdge.Dat', 'TileEdge.Dat')):
        shutil.copyfile(os.path.join(common, 'dat', src), os.path.join(DATA, dst))

    print('  terrain.drs (from the textures)')
    sys.path.insert(0, HERE)
    import datfile  # noqa: E402
    dat_terrains = {t['slp']: t['file'] for t in datfile.load()['terrains'].values()
                    if t['slp'] >= 0 and t['file'] not in ('', 'None')}
    pal = palette(os.path.join(drs, 'interface', '50500.bina'))
    write_drs(os.path.join(DATA, 'terrain.drs'),
              terrain(os.path.join(common, 'terrain', 'textures'), dat_terrains, pal))

    print('  music')
    music(os.path.join(common, 'sound'))

    os.makedirs(FONTS, exist_ok=True)
    for name in os.listdir(os.path.join(common, 'fonts')):
        shutil.copyfile(os.path.join(common, 'fonts', name), os.path.join(FONTS, name.upper()))


if __name__ == '__main__':
    main()
