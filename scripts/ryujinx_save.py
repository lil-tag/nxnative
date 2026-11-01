#!/usr/bin/env python3
# Saves for a Mac build: bring in the game's save from Ryujinx, start an empty one, or switch between kept ones. The game plays the save in <id>_build/run/save (DM_SAVE in
# start_mac.sh), plus run/save_device for a game that mounts both account and device save data (hle_fs.c MountDeviceSaveData: Mario Kart 8 Deluxe). Kept saves wait in
# <id>_build/run/saves/<name>/, each holding its save/ (and save_device/). Whatever is replaced is first kept there as "before <what> <date time>", never deleted.
# Ryujinx keeps a save per game, user and type in bis/user/save/<16 hex>/0/; its ExtraData0 (or ExtraData1) starts with the save's attribute: program ID (u64) at 0,
# user ID (two u64) at 8, system save ID (u64) at 0x18, type at 0x20 (1 account, 2 BCAT cache, 3 device). Its files are laid out as nxnative lays out a save (Smash: save_data/system_data.bin, ...).
# A game with both an account and a device save in Ryujinx mounts both, so the device save goes to save_device; a game with only one of them has it in save.
# Usage: python3 scripts/ryujinx_save.py list <game folder or <id>_build>        the Ryujinx saves found for the game, and the kept ones
#        python3 scripts/ryujinx_save.py import <game folder> [--user <name>]    Ryujinx's save (the profile marked Open, else the newest account save)
#        python3 scripts/ryujinx_save.py new <game folder>                       an empty save; the game starts as on a new console
#        python3 scripts/ryujinx_save.py use <game folder> <kept name>           a kept save
#   --ryujinx <folder>: Ryujinx's data folder (default ~/Library/Application Support/Ryujinx). --self-test: checks the steps on a made-up Ryujinx folder
import argparse, datetime, json, re, shutil, struct, subprocess, sys, tempfile
from pathlib import Path

RYU = Path.home() / 'Library/Application Support/Ryujinx'
ACCOUNT, BCAT, DEVICE = 1, 2, 3
PARTS = ('save', 'save_device')   # the folders of one save set in run/ and in a kept save


def build_dir(p):
    """The <title ID>_build folder: the path itself, or the one inside a game folder"""
    p = Path(p)
    if re.fullmatch(r'[0-9A-Fa-f]{16}_build', p.name): return p
    found = sorted(p.glob('*_build'))
    if len(found) != 1: sys.exit(f'{p}: expected one <title ID>_build folder, found {len(found)}')
    return found[0]


def ryujinx_saves(ryu, title):
    """[(folder, type, user ID as Ryujinx writes it, modified)] of the game's saves; title is the 16-hex base title ID"""
    out = []
    for d in sorted((ryu / 'bis/user/save').glob('*')):
        extra = next((d / n for n in ('ExtraData0', 'ExtraData1') if (d / n).is_file()), None)
        if not extra or not (d / '0').is_dir(): continue
        b = extra.read_bytes()[:0x30]
        if len(b) < 0x21: continue
        prog, lo, hi = struct.unpack_from('<QQQ', b, 0)
        if prog != int(title, 16): continue
        files = [f for f in (d / '0').rglob('*') if f.is_file() and f.name != '.DS_Store']
        out.append((d / '0', b[0x20], f'{lo:016x}{hi:016x}', max((f.stat().st_mtime for f in files), default=0)))
    return out


def profiles(ryu):
    """{user ID: (name, open)} from Ryujinx's Profiles.json"""
    try: j = json.loads((ryu / 'system/Profiles.json').read_text())
    except (OSError, ValueError): return {}
    return {p['user_id']: (p.get('name', '?'), p.get('account_state') == 'Open') for p in j.get('profiles', [])}


def pick(saves, users, name=None):
    """The account save to import, and the device save (each None when there is none)"""
    acct = [s for s in saves if s[1] == ACCOUNT]
    if name: acct = [s for s in acct if users.get(s[2], ('',))[0].lower() == name.lower()]
    elif any(users.get(s[2], ('', False))[1] for s in acct): acct = [s for s in acct if users.get(s[2], ('', False))[1]]
    acct.sort(key=lambda s: s[3])
    return (acct[-1] if acct else None), next((s for s in saves if s[1] == DEVICE), None)


def running(build):
    return subprocess.run(['pgrep', '-f', f'{build}/tools/dmhost'], capture_output=True).returncode == 0


def has_files(d):
    return d.is_dir() and any(f.is_file() and f.name != '.DS_Store' for f in d.rglob('*'))


def replace_active(build, fill, what):
    """Keeps the current save set in saves/ (when it has files), clears run/, then fill(run) puts the new set in"""
    if running(build): sys.exit('the game is running: quit it first')
    run = build / 'run'; kept = run / 'saves'
    if any(has_files(run / p) for p in PARTS):
        stamp, n = datetime.datetime.now().strftime('%Y-%m-%d %H%M%S'), 2
        dst = kept / f'before {what} {stamp}'
        while dst.exists(): dst, n = kept / f'before {what} {stamp} ({n})', n + 1   # two changes within a second
        dst.mkdir(parents=True)
        for p in PARTS:
            if (run / p).exists(): (run / p).rename(dst / p)
        print(f'Your previous save is kept as "{dst.name}".')
    for p in PARTS:
        for d in (run / p, run / f'{p}.work', run / f'{p}.new'):   # the empty save and the host's transaction folders (hle_fs.c clears those at mount too)
            if d.exists(): shutil.rmtree(d)
    (run / 'save').mkdir(parents=True)
    fill(run)


def copy_save(src, dst):
    shutil.copytree(src, dst, dirs_exist_ok=True, ignore=shutil.ignore_patterns('.DS_Store'))


def cmd_list(build, ryu):
    users = profiles(ryu)
    saves = ryujinx_saves(ryu, build.name[:16])
    kinds = {ACCOUNT: 'account', BCAT: 'download cache (not imported)', DEVICE: 'device'}
    print(f'Ryujinx saves for {build.name[:16]}:' if saves else f'no Ryujinx save for {build.name[:16]} in {ryu}')
    for d, t, u, m in saves:
        who = users.get(u, ('?', False))[0] if t == ACCOUNT else ''
        print(f'  {d.parent.name}  {kinds.get(t, t)}{" of " + who if who else ""}, changed {datetime.datetime.fromtimestamp(m):%Y-%m-%d %H:%M}' if m else f'  {d.parent.name}  {kinds.get(t, t)}, empty')
    kept = sorted(p.name for p in (build / 'run/saves').glob('*') if p.is_dir())
    print('kept saves: ' + (', '.join(kept) if kept else 'none'))


def cmd_import(build, ryu, user):
    acct, dev = pick(ryujinx_saves(ryu, build.name[:16]), profiles(ryu), user)
    if not acct and (user or not dev): sys.exit(f'no Ryujinx account save for {build.name[:16]}{" of " + user if user else ""} in {ryu}')
    def fill(run):
        if acct: copy_save(acct[0], run / 'save')
        if dev: copy_save(dev[0], run / ('save_device' if acct else 'save'))
    replace_active(build, fill, 'Ryujinx import')
    who = profiles(ryu).get(acct[2], ('?',))[0] if acct else None
    when = datetime.datetime.fromtimestamp((acct or dev)[3]).strftime('%-d %b %Y')
    print(f'Now playing the Ryujinx save' + (f' of {who}' if who else '') + f', last played in Ryujinx on {when}' + (' (replays and other shared data included)' if acct and dev else '') + '.')


def cmd_use(build, name):
    src = build / 'run/saves' / name
    if not src.is_dir(): sys.exit(f'no kept save named "{name}" in {src.parent}')
    tmp = src.with_name(src.name + '.using')
    src.rename(tmp)   # out of saves/ first, so keeping the current one cannot take its name
    def fill(run):
        (run / 'save').rmdir()
        for p in PARTS:
            if (tmp / p).exists(): (tmp / p).rename(run / p)
        if not (run / 'save').exists(): (run / 'save').mkdir()
        shutil.rmtree(tmp)
    replace_active(build, fill, f'switching to {name}')
    print(f'Now playing the save kept as "{name}".')


def self_test():
    with tempfile.TemporaryDirectory() as t:
        t = Path(t); ryu, build = t / 'ryu', t / 'Game/01006A800016E000_build'
        def mk(n, prog, lo, hi, typ, files):
            d = ryu / 'bis/user/save' / f'{n:016x}'; (d / '0').mkdir(parents=True)
            (d / 'ExtraData0').write_bytes(struct.pack('<QQQQ', prog, lo, hi, 0) + bytes([typ]) + bytes(0x1df))
            for f, data in files.items(): (d / '0' / f).parent.mkdir(parents=True, exist_ok=True); (d / '0' / f).write_text(data)
        smash, kart = 0x01006A800016E000, 0x0100152000022000
        mk(1, smash, 0x9f13e20f46484823, 0x9b37f5b6db71706c, ACCOUNT, {'save_data/system_data.bin': 'tag'})
        mk(2, smash, 1, 0, ACCOUNT, {'save_data/system_data.bin': 'other'})
        mk(3, smash, 0, 0, BCAT, {'cache.bin': 'cache'})
        mk(4, kart, 0x9f13e20f46484823, 0x9b37f5b6db71706c, ACCOUNT, {'userdata.dat': 'kart'})
        mk(5, kart, 0, 0, DEVICE, {'replay.dat': 'replay'})
        (ryu / 'system').mkdir(); (ryu / 'system/Profiles.json').write_text(json.dumps({'profiles': [
            {'user_id': '9f13e20f464848239b37f5b6db71706c', 'name': 'TAG', 'account_state': 'Open'},
            {'user_id': '00000000000000010000000000000000', 'name': 'RyuPlayer', 'account_state': 'Closed'}]}))
        (build / 'run/save/save_data').mkdir(parents=True); (build / 'run/save/save_data/system_data.bin').write_text('fresh')
        (build / 'run/save.work').mkdir()
        assert build_dir(build.parent) == build
        assert [s[0].parent.name[-1] for s in ryujinx_saves(ryu, '01006A800016E000')] == ['1', '2', '3'], 'only the game\'s saves'
        cmd_import(build, ryu, None)
        run = build / 'run'
        assert (run / 'save/save_data/system_data.bin').read_text() == 'tag' and not (run / 'save/cache.bin').exists(), 'the Open profile\'s save, without the download cache'
        assert not (run / 'save.work').exists() and not (run / 'save_device').exists(), 'old transaction folder cleared; no device save for this game'
        kept = list((run / 'saves').glob('before Ryujinx import *'))
        assert len(kept) == 1 and (kept[0] / 'save/save_data/system_data.bin').read_text() == 'fresh', 'the save it replaced is kept'
        cmd_import(build, ryu, 'RyuPlayer')
        assert (run / 'save/save_data/system_data.bin').read_text() == 'other', '--user picks that profile'
        replace_active(build, lambda r: None, 'new save')
        assert (run / 'save').is_dir() and not has_files(run / 'save'), 'a new save is empty'
        assert len(list((run / 'saves').glob('*'))) == 3, 'each replaced save is kept (fresh, TAG, RyuPlayer), an empty one never is'
        cmd_use(build, kept[0].name)
        assert (run / 'save/save_data/system_data.bin').read_text() == 'fresh' and not kept[0].exists(), 'use brings a kept save back'
        kb = t / 'Kart/0100152000022000_build'; (kb / 'run').mkdir(parents=True)
        cmd_import(kb, ryu, None)
        assert (kb / 'run/save/userdata.dat').read_text() == 'kart' and (kb / 'run/save_device/replay.dat').read_text() == 'replay', 'account and device saves each in their folder'
        replace_active(kb, lambda r: None, 'new save')
        k = next((kb / 'run/saves').glob('*'))
        assert (k / 'save_device/replay.dat').exists() and not (kb / 'run/save_device').exists(), 'a kept save holds its device save too'
        cmd_use(kb, k.name)
        assert (kb / 'run/save_device/replay.dat').exists(), 'and brings it back'
    print('ryujinx_save.py self-test: ok')


def main():
    a = argparse.ArgumentParser(description='Ryujinx saves, new saves and kept saves for a Mac build')
    a.add_argument('cmd', choices=['list', 'import', 'new', 'use'], nargs='?')
    a.add_argument('game', nargs='?'); a.add_argument('name', nargs='?')
    a.add_argument('--user'); a.add_argument('--ryujinx', type=Path, default=RYU); a.add_argument('--self-test', action='store_true')
    o = a.parse_args()
    if o.self_test: return self_test()
    if not o.cmd or not o.game: a.error('a command and a game folder are needed')
    b = build_dir(o.game)
    if o.cmd == 'list': cmd_list(b, o.ryujinx)
    elif o.cmd == 'import': cmd_import(b, o.ryujinx, o.user)
    elif o.cmd == 'new': replace_active(b, lambda r: None, 'new save'); print('Now playing a new empty save: the game starts as on a new console.')
    elif not o.name: a.error('use needs the kept save\'s name (list shows them)')
    else: cmd_use(b, o.name)


if __name__ == '__main__':
    main()
