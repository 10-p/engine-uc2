#!/usr/bin/env python3
"""cook-audio.py: cook Unreal Championship 2's XACT audio project into the files UC2ALAudio plays.

Input (read only): the XACT project as text (Sounds/Avaris.xap), the source WAVs it names (SoundData/,
Music/) and, for a cross-check only, the shipped Xbox sound banks (Sounds/*.xsb).

Output (never committed; default <WorkUC2>/assets/uc2-cooked):
  Sounds/<WaveBankFile>.uc2wb    wave bank index (text): one line per wave entry
  Sounds/<WaveBankFile>.ogp      the wave bank's waves, each a complete Ogg Vorbis stream, concatenated
  Sounds/<SoundBank>.uc2cues     cue table (text): cues -> sounds -> tracks -> events -> variations
  Music/<Track>.ogg              one streamed Ogg Vorbis file per music track (Music/*.wav)
  Sounds/cook-report.txt         everything the cook did not understand, missing files, the .xsb cross-check
  .cache/                        per-wave encoder output, reused by the next cook when the source is unchanged

The formats are specified in docs/porting/analysis/audio.md ("Cooked format"). Usage:

  tools/cook-audio.py [--epic <epic-sources>] [--out <dir>] [--jobs 6] [--banks A_Music,A_Weapon]
                      [--no-encode] [--quality 4] [--music-quality 5]

Run it under the memory ceiling: <WorkUC2>/bin/capped 4G 3600 -- tools/cook-audio.py
"""

import argparse
import collections
import concurrent.futures
import hashlib
import os
import re
import shutil
import struct
import subprocess
import sys
import threading

FORMAT_VERSION = 1
LANGS = ('deu', 'fra', 'ita', 'spa')


# ------------------------------------------------------------------------------------------------------------
# The .xap grammar
#
#   file      := item*
#   item      := assign | block
#   assign    := KEY '=' VALUE ';'          VALUE runs to the first line that ends with ';' (comments span lines)
#   block     := NAME NEWLINE '{' item* '}'  NAME and the braces each stand on their own line
#
# Keys repeat (Deviation appears three times in a Mapping), so a node keeps its assignments in order.
# ------------------------------------------------------------------------------------------------------------

class Node:
    __slots__ = ('name', 'line', 'items', 'children')

    def __init__(self, name, line):
        self.name, self.line, self.items, self.children = name, line, [], []

    def get(self, key, default=None):
        for k, v in self.items:
            if k == key:
                return v
        return default

    def getall(self, key):
        return [v for k, v in self.items if k == key]

    def kids(self, name):
        return [c for c in self.children if c.name == name]


class XapError(Exception):
    pass


def parse_xap(path):
    with open(path, 'rb') as f:
        text = f.read().decode('latin-1')
    lines = text.replace('\r\n', '\n').replace('\r', '\n').split('\n')
    root = Node('<root>', 0)
    stack = [root]
    pending = None          # (name, line) of a block name waiting for its '{'
    value = None            # (key, parts, line) of an assignment still waiting for its ';'
    for no, raw in enumerate(lines, 1):
        s = raw.strip()
        if value is not None:
            value[1].append(s)
            if s.endswith(';'):
                v = '\n'.join(value[1])[:-1].rstrip()
                stack[-1].items.append((value[0], v))
                value = None
            continue
        if not s:
            continue
        if s == '{':
            if pending is None:
                raise XapError('line %d: "{" without a block name' % no)
            node = Node(pending[0], pending[1])
            stack[-1].children.append(node)
            stack.append(node)
            pending = None
            continue
        if s == '}':
            if pending is not None:
                raise XapError('line %d: block name "%s" (line %d) without "{"' % (no, pending[0], pending[1]))
            if len(stack) == 1:
                raise XapError('line %d: unbalanced "}"' % no)
            stack.pop()
            continue
        if pending is not None:
            raise XapError('line %d: block name "%s" (line %d) not followed by "{"' % (no, pending[0], pending[1]))
        m = re.match(r'^([^=]+?)\s*=\s*(.*)$', s)
        if m:
            key, rest = m.group(1), m.group(2)
            if rest.endswith(';'):
                stack[-1].items.append((key, rest[:-1].rstrip()))
            else:
                value = (key, [rest], no)
            continue
        pending = (s, no)
    if value is not None:
        raise XapError('line %d: value of "%s" never ends with ";"' % (value[2], value[0]))
    if pending is not None or len(stack) != 1:
        raise XapError('end of file inside a block')
    return root


# What every block may contain. 'use' = the cook carries it into the cooked data; 'info' = bookkeeping of the
# authoring tool, deliberately dropped (timestamps, comments, alignment of the Xbox bank files).
SCHEMA = {
    '<root>': ({'Signature': 'use', 'Version': 'use'},
               {'Options', 'Wave Bank', 'Sound Bank', 'Sound Group', 'Sound Category', 'Sound Layer'}),
    'Options': ({}, set()),
    'Wave Bank': ({'Name': 'use', 'Bank File': 'use', 'Alignment': 'info', 'Streaming': 'use', 'Compact': 'info',
                   'Bank Last Modified Low': 'info', 'Bank Last Modified High': 'info',
                   'Header Last Modified Low': 'info', 'Header Last Modified High': 'info'}, {'Entry'}),
    'Wave Bank/Entry': ({'Name': 'use', 'File': 'use', 'ADPCM Filter': 'info', 'Format Tag': 'use', 'Channels': 'use',
                         'Sampling Rate': 'use', 'Bits Per Sample': 'use', 'Play Region Offset': 'use',
                         'Play Region Length': 'use', 'Loop Region Offset': 'use', 'Loop Region Length': 'use',
                         'File Type': 'use', 'Last Modified Low': 'info', 'Last Modified High': 'info'}, set()),
    'Sound Group': ({'Name': 'use', 'Volume': 'use'}, set()),
    'Sound Category': ({'Name': 'use'}, set()),
    'Sound Layer': ({'Name': 'use'}, set()),
    'Sound Bank': ({'Name': 'use', 'Bank File': 'use', 'Bank Last Modified Low': 'info',
                    'Bank Last Modified High': 'info', 'Header Last Modified Low': 'info',
                    'Header Last Modified High': 'info'}, {'Sound', 'Cue', 'Sound Template'}),
    'Sound Bank/Sound Template': ({'Name': 'info', '3D': 'info'}, {'3D'}),
    'Sound Bank/Sound Template/3D': ({k: 'info' for k in (
        'Mode', 'Inside Cone Angle', 'Outside Cone Angle', 'Cone Outside Volume', 'Minimum Distance',
        'Maximum Distance', 'Distance Factor', 'Rolloff Factor', 'Doppler Factor')}, set()),
    'Sound Bank/Cue': ({'Name': 'use', 'Comment': 'info', 'Queue': 'use'}, {'Sound'}),
    'Sound Bank/Cue/Sound': ({'Sound': 'use', 'Sound Index': 'use', 'Weight': 'use'}, set()),
    'Sound Bank/Sound': ({'Name': 'use', 'Priority': 'use', 'Volume': 'use', 'Pitch': 'use', 'Category': 'use',
                          'Group': 'use', 'Layer': 'use', 'Headroom Boost': 'use', '3D': 'use', 'I3DL2 Level': 'use',
                          'LFE Level': 'use', 'Parametric EQ': 'use', 'Comment': 'info'},
                         {'3D', 'Track', 'Parameter Control', 'Parametric EQ'}),
    'Sound Bank/Sound/3D': ({k: 'use' for k in (
        'Mode', 'Inside Cone Angle', 'Outside Cone Angle', 'Cone Outside Volume', 'Minimum Distance',
        'Maximum Distance', 'Distance Factor', 'Rolloff Factor', 'Doppler Factor')}, set()),
    'Sound Bank/Sound/Parametric EQ': ({'Gain': 'use', 'Q': 'use', 'Frequency': 'use'}, set()),
    'Sound Bank/Sound/Parameter Control': ({'Name': 'use'}, {'Mapping'}),
    'Sound Bank/Sound/Parameter Control/Mapping': ({'Type': 'use', 'Track Index': 'use', 'Deviation': 'use'},
                                                   {'Coordinates'}),
    'Sound Bank/Sound/Parameter Control/Mapping/Coordinates': ({'X': 'use', 'Y': 'use'}, set()),
    'Sound Bank/Sound/Track': ({'Name': 'use'}, {
        'Play with Pitch and Volume Variation', 'Set Volume', 'Set Pitch', 'Low-pass Filter',
        'Multi-function LFO', 'Pitch LFO', 'Pitch and Filter EG', 'Set MixBins', 'Loop Start'}),
    'Sound Bank/Sound/Track/Play with Pitch and Volume Variation': ({
        'Pitch Low': 'use', 'Pitch High': 'use', 'Volume Low': 'use', 'Volume High': 'use', 'Playlist Type': 'use',
        'Loop Count': 'use', 'Timestamp': 'use', 'New Variation on Loop': 'use', 'Variable Delay': 'use'},
        {'Registered Wave'}),
    'Sound Bank/Sound/Track/Play with Pitch and Volume Variation/Registered Wave': ({
        'Wave Bank': 'use', 'Wave Entry': 'use', 'Wave Entry Index': 'use', 'Weight': 'use'}, set()),
    'Sound Bank/Sound/Track/Set Volume': ({k: 'use' for k in (
        'Relative Value', 'Recurring', 'Loop Count', 'Type', 'Duration', 'Low', 'High', 'Timestamp', 'Variation')},
        set()),
    'Sound Bank/Sound/Track/Set Pitch': ({k: 'use' for k in (
        'Relative Value', 'Recurring', 'Loop Count', 'Type', 'Duration', 'Low', 'High', 'Timestamp', 'Variation')},
        set()),
    'Sound Bank/Sound/Track/Low-pass Filter': ({k: 'use' for k in (
        'Relative Value', 'Loop Count', 'Duration', 'Resonance High', 'Resonance Low', 'Frequency High',
        'Frequency Low', 'Timestamp', 'Recurring', 'Variation')}, set()),
    'Sound Bank/Sound/Track/Multi-function LFO': ({k: 'use' for k in (
        'Delay', 'Delta', 'Pitch Modulation', 'Filter Modulation', 'Amplitude Modulation', 'Timestamp')}, set()),
    'Sound Bank/Sound/Track/Pitch LFO': ({k: 'use' for k in ('Delay', 'Delta', 'Pitch Modulation', 'Timestamp')},
                                         set()),
    'Sound Bank/Sound/Track/Pitch and Filter EG': ({k: 'use' for k in (
        'Delay', 'Attack', 'Hold', 'Decay', 'Release', 'Sustain', 'Mode', 'Pitch Scale', 'Filter Cutoff',
        'Timestamp')}, set()),
    'Sound Bank/Sound/Track/Set MixBins': ({'Timestamp': 'use'}, {'MixBin'}),
    'Sound Bank/Sound/Track/Set MixBins/MixBin': ({'MixBin': 'use', 'Volume': 'use'}, set()),
    'Sound Bank/Sound/Track/Loop Start': ({'Loop Count': 'use', 'Outer Loop Count': 'use', 'Timestamp': 'use'},
                                          set()),
}


def check_schema(node, path, report, counts):
    known_keys, known_blocks = SCHEMA.get(path, (None, None))
    if known_keys is None:
        report.append('UNKNOWN block "%s" (line %d): not understood, its content is not cooked' % (path, node.line))
        return
    for k, _ in node.items:
        counts[(path, k)] += 1
        if k not in known_keys:
            report.append('UNKNOWN key "%s" in "%s" (line %d): not understood, not cooked' % (k, path, node.line))
    for c in node.children:
        cpath = c.name if path == '<root>' else path + '/' + c.name
        if c.name not in known_blocks:
            report.append('UNKNOWN block "%s" in "%s" (line %d): not understood, not cooked' % (c.name, path, c.line))
            continue
        check_schema(c, cpath, report, counts)


# ------------------------------------------------------------------------------------------------------------
# Values
# ------------------------------------------------------------------------------------------------------------

def num(v, default=0):
    if v is None:
        return default
    return int(v)


def fnum(v, default=0.0):
    if v is None:
        return default
    return float(v)


def fmtf(x):
    s = ('%.6f' % x).rstrip('0').rstrip('.')
    return s if s not in ('', '-0') else '0'


def esc(s):
    """Cooked text fields carry no spaces or '=': %XX-escape them (and '%' itself)."""
    out = []
    for ch in s:
        if ch in ' %=\t\n\r#' or ord(ch) < 32:
            out.append('%%%02X' % ord(ch))
        else:
            out.append(ch)
    return ''.join(out)


# ------------------------------------------------------------------------------------------------------------
# Source WAV/AIFF reading (header only)
# ------------------------------------------------------------------------------------------------------------

def read_wav_header(path):
    """Returns dict(kind, channels, rate, bits, frames, data_offset, data_bytes) or raises ValueError."""
    with open(path, 'rb') as f:
        d = f.read()
    if d[:4] == b'RIFF' and d[8:12] == b'WAVE':
        i = 12
        fmt = data = None
        while i + 8 <= len(d):
            cid = d[i:i + 4]
            ln = struct.unpack_from('<I', d, i + 4)[0]
            if cid == b'fmt ':
                tag, ch, rate, _, align, bits = struct.unpack_from('<HHIIHH', d, i + 8)
                fmt = (tag, ch, rate, align, bits)
            elif cid == b'data':
                data = (i + 8, min(ln, len(d) - i - 8))
            i += 8 + ln + (ln & 1)
        if not fmt or not data:
            raise ValueError('RIFF without fmt/data')
        tag, ch, rate, align, bits = fmt
        return dict(kind='wav', tag=tag, channels=ch, rate=rate, bits=bits,
                    frames=data[1] // max(1, align), data_offset=data[0], data_bytes=data[1], align=align)
    if d[:4] == b'FORM' and d[8:12] in (b'AIFF', b'AIFC'):
        i = 12
        comm = None
        ssnd = None
        while i + 8 <= len(d):
            cid = d[i:i + 4]
            ln = struct.unpack_from('>I', d, i + 4)[0]
            if cid == b'COMM':
                ch, frames, bits = struct.unpack_from('>hIh', d, i + 8)
                exp, mant = struct.unpack_from('>HQ', d, i + 16)
                rate = int(round(mant * 2.0 ** ((exp & 0x7fff) - 16383 - 63))) if mant else 0
                comm = (ch, frames, bits, rate)
            elif cid == b'SSND':
                ssnd = (i + 16, ln - 8)
            i += 8 + ln + (ln & 1)
        if not comm:
            raise ValueError('AIFF without COMM')
        ch, frames, bits, rate = comm
        return dict(kind='aiff', tag=0, channels=ch, rate=rate, bits=bits, frames=frames,
                    data_offset=ssnd[0] if ssnd else 0, data_bytes=ssnd[1] if ssnd else 0,
                    align=ch * ((bits + 7) // 8))
    raise ValueError('neither RIFF/WAVE nor AIFF')


class SourceIndex:
    """Case-insensitive file lookup below a root (the Xbox tools ran on Windows)."""

    def __init__(self, root):
        self.root = root
        self.map = {}
        for dirpath, _, files in os.walk(root):
            for fn in files:
                full = os.path.join(dirpath, fn)
                self.map[os.path.relpath(full, root).replace(os.sep, '/').lower()] = full

    def find(self, rel):
        return self.map.get(rel.replace('\\', '/').lower())


# ------------------------------------------------------------------------------------------------------------
# The model
# ------------------------------------------------------------------------------------------------------------

class WaveEntry:
    def __init__(self, bank, index, node):
        self.bank, self.index = bank, index
        self.name = node.get('Name')
        self.file = node.get('File')
        self.format_tag = num(node.get('Format Tag'))
        self.channels = num(node.get('Channels'))
        self.rate = num(node.get('Sampling Rate'))
        self.play_offset = num(node.get('Play Region Offset'))
        self.play_length = num(node.get('Play Region Length'))
        self.loop_offset = num(node.get('Loop Region Offset'))
        self.loop_length = num(node.get('Loop Region Length'))
        self.file_type = num(node.get('File Type'))
        self.source = None      # resolved path
        self.header = None
        self.missing = None     # reason
        self.loop = (0, 0)      # loop region in sample frames (start, length)
        self.ogg = None         # encoded cache file
        self.offset = self.size = 0


class WaveBank:
    def __init__(self, node):
        self.name = node.get('Name')
        self.bank_file = node.get('Bank File')
        self.stem = os.path.splitext(self.bank_file)[0]
        self.streaming = num(node.get('Streaming'))
        self.lang = ''
        for lang in LANGS:
            if self.stem.lower().endswith('_' + lang) and self.stem[:-4] == self.name:
                self.lang = lang
        self.entries = [WaveEntry(self, i, e) for i, e in enumerate(node.kids('Entry'))]
        self.music = all(e.file and e.file.replace('\\', '/').lower().startswith('../music/') for e in self.entries) \
            and bool(self.entries)


def build_sound(node, report, bankname):
    s = collections.OrderedDict()
    s['name'] = node.get('Name')
    s['priority'] = num(node.get('Priority'))
    s['volume'] = num(node.get('Volume'))
    s['pitch'] = num(node.get('Pitch'))
    s['category'] = node.get('Category', '-')
    s['group'] = node.get('Group', '-')
    s['layer'] = node.get('Layer', '-')
    s['headroomboost'] = num(node.get('Headroom Boost'))
    s['i3dl2'] = num(node.get('I3DL2 Level'))
    s['lfe'] = num(node.get('LFE Level'))
    s['is3d'] = num(node.get('3D'))
    s['eq'] = num(node.get('Parametric EQ'))
    d3 = node.kids('3D')
    if s['is3d'] and not d3:
        report.append('Sound %s\\%s: 3D = 1 without a 3D block (DirectSound defaults used)' % (bankname, s['name']))
    if d3 and not s['is3d']:
        report.append('Sound %s: a 3D block but 3D is not set (block cooked, sound stays 2D)' % s['name'])
    s['3d'] = None
    if d3:
        b = d3[0]
        s['3d'] = collections.OrderedDict([
            ('mode', num(b.get('Mode'))), ('cone_in', num(b.get('Inside Cone Angle'), 360)),
            ('cone_out', num(b.get('Outside Cone Angle'), 360)), ('cone_vol', num(b.get('Cone Outside Volume'))),
            ('min', fnum(b.get('Minimum Distance'), 1.0)), ('max', fnum(b.get('Maximum Distance'), 1e9)),
            ('distfactor', fnum(b.get('Distance Factor'), 1.0)), ('rolloff', fnum(b.get('Rolloff Factor'), 1.0)),
            ('doppler', fnum(b.get('Doppler Factor'), 1.0))])
    eq = node.kids('Parametric EQ')
    s['eqparams'] = (num(eq[0].get('Gain')), num(eq[0].get('Q')), num(eq[0].get('Frequency'))) if eq else None
    s['rpcs'] = []
    for pc in node.kids('Parameter Control'):
        for mp in pc.kids('Mapping'):
            pts = [(num(c.get('X')), num(c.get('Y'))) for c in mp.kids('Coordinates')]
            s['rpcs'].append(dict(name=pc.get('Name'), type=num(mp.get('Type')), track=num(mp.get('Track Index')),
                                  deviation=[num(x) for x in mp.getall('Deviation')], points=pts))
        if not pc.kids('Mapping'):
            report.append('Sound %s: parameter control "%s" without a mapping' % (s['name'], pc.get('Name')))
    s['tracks'] = []
    for t in node.kids('Track'):
        tr = dict(name=t.get('Name', '-'), events=[])
        for ev in t.children:
            e = collections.OrderedDict()
            ts = num(ev.get('Timestamp'))
            if ev.name == 'Play with Pitch and Volume Variation':
                e['kind'] = 'play'
                e['time'] = ts
                e['delay'] = num(ev.get('Variable Delay'))
                e['loops'] = num(ev.get('Loop Count'))
                e['newvar'] = num(ev.get('New Variation on Loop'))
                e['playlist'] = num(ev.get('Playlist Type'))
                e['pitch'] = (num(ev.get('Pitch Low')), num(ev.get('Pitch High')))
                e['vol'] = (num(ev.get('Volume Low')), num(ev.get('Volume High')))
                e['waves'] = []
                for rw in ev.kids('Registered Wave'):
                    e['waves'].append(dict(bank=rw.get('Wave Bank'), entry=num(rw.get('Wave Entry Index')),
                                           name=rw.get('Wave Entry'), weight=num(rw.get('Weight'), -1)))
                if not e['waves']:
                    report.append('Sound %s: play event without any wave' % s['name'])
            elif ev.name in ('Set Volume', 'Set Pitch'):
                e['kind'] = 'setvol' if ev.name == 'Set Volume' else 'setpitch'
                e['time'] = ts
                variation, fade = num(ev.get('Variation')), num(ev.get('Recurring'))
                lo, hi = num(ev.get('Low')), num(ev.get('High'))
                e['mode'] = 'random' if variation else ('fade' if fade else 'set')
                if variation and fade:
                    report.append('Sound %s: %s with both Variation and Recurring (taken as random)' % (s['name'], ev.name))
                if e['mode'] == 'set' and lo != hi:
                    report.append('Sound %s: %s sets %d but High is %d (High ignored)' % (s['name'], ev.name, lo, hi))
                e['relative'] = num(ev.get('Relative Value'))
                e['type'] = num(ev.get('Type'))
                e['steps'] = num(ev.get('Loop Count'))
                e['dur'] = num(ev.get('Duration'))
                e['lo'], e['hi'] = lo, hi
            elif ev.name == 'Low-pass Filter':
                e['kind'] = 'lpf'
                e['time'] = ts
                e['relative'] = num(ev.get('Relative Value'))
                e['mode'] = 'random' if num(ev.get('Variation')) else ('fade' if num(ev.get('Recurring')) else 'set')
                e['steps'] = num(ev.get('Loop Count'))
                e['dur'] = num(ev.get('Duration'))
                e['freq'] = (num(ev.get('Frequency Low')), num(ev.get('Frequency High')))
                e['res'] = (num(ev.get('Resonance Low')), num(ev.get('Resonance High')))
            elif ev.name == 'Multi-function LFO':
                e['kind'] = 'lfo'
                e['time'] = ts
                e['delay'] = num(ev.get('Delay'))
                e['delta'] = num(ev.get('Delta'))
                e['pitchmod'] = num(ev.get('Pitch Modulation'))
                e['filtermod'] = num(ev.get('Filter Modulation'))
                e['ampmod'] = num(ev.get('Amplitude Modulation'))
            elif ev.name == 'Pitch LFO':
                e['kind'] = 'pitchlfo'
                e['time'] = ts
                e['delay'] = num(ev.get('Delay'))
                e['delta'] = num(ev.get('Delta'))
                e['pitchmod'] = num(ev.get('Pitch Modulation'))
            elif ev.name == 'Pitch and Filter EG':
                e['kind'] = 'eg'
                e['time'] = ts
                for k in ('Delay', 'Attack', 'Hold', 'Decay', 'Release', 'Sustain', 'Mode', 'Pitch Scale',
                          'Filter Cutoff'):
                    e[k.lower().replace(' ', '')] = num(ev.get(k))
            elif ev.name == 'Set MixBins':
                e['kind'] = 'mixbins'
                e['time'] = ts
                e['bins'] = [(num(m.get('MixBin')), num(m.get('Volume'))) for m in ev.kids('MixBin')]
            elif ev.name == 'Loop Start':
                e['kind'] = 'loopstart'
                e['time'] = ts
                e['count'] = num(ev.get('Loop Count'))
                e['outer'] = num(ev.get('Outer Loop Count'))
            else:
                continue    # reported by the schema check
            tr['events'].append(e)
        plays = [e for e in tr['events'] if e['kind'] == 'play']
        if len(plays) != 1:
            report.append('Sound %s: track with %d play events' % (s['name'], len(plays)))
        s['tracks'].append(tr)
    if not s['tracks']:
        report.append('Sound %s\\%s: no track (silent sound)' % (bankname, s['name']))
    return s


class SoundBank:
    def __init__(self, node, report):
        self.name = node.get('Name')
        self.bank_file = node.get('Bank File')
        self.sounds = [build_sound(n, report, self.name) for n in node.kids('Sound')]
        self.by_name = {s['name']: i for i, s in enumerate(self.sounds)}
        self.cues = []
        for i, c in enumerate(node.kids('Cue')):
            refs = []
            for cs in c.kids('Sound'):
                idx = num(cs.get('Sound Index'), -1)
                nm = cs.get('Sound')
                if not (0 <= idx < len(self.sounds)) or self.sounds[idx]['name'] != nm:
                    fixed = self.by_name.get(nm)
                    report.append('Cue %s\\%s: Sound Index %d does not name sound "%s"; %s' % (
                        self.name, c.get('Name'), idx, nm,
                        'using the sound of that name (index %d)' % fixed if fixed is not None else 'dropped'))
                    if fixed is None:
                        continue
                    idx = fixed
                refs.append((idx, num(cs.get('Weight'), -1)))
            if not refs:
                report.append('Cue %s: no sound (cooked as a silent cue)' % c.get('Name'))
            self.cues.append(dict(name=c.get('Name'), sounds=refs, queue=num(c.get('Queue'))))


# ------------------------------------------------------------------------------------------------------------
# Durations (XACT_SOUNDCUE_PROPERTIES.dwLength in ms; 0xFFFFFFFF for infinite loops, written as -1)
# ------------------------------------------------------------------------------------------------------------

def sound_length_ms(sound, wavebanks_by_name):
    longest = 0
    for t in sound['tracks']:
        tlen = 0
        for e in t['events']:
            if e['kind'] != 'play':
                end = e['time'] + e.get('dur', 0)
                tlen = max(tlen, end)
                continue
            if e['loops'] == 65535:
                return -1
            wl = 0
            for w in e['waves']:
                wb = wavebanks_by_name.get(w['bank'])
                if wb and 0 <= w['entry'] < len(wb.entries):
                    ent = wb.entries[w['entry']]
                    if ent.header and ent.header['rate']:
                        wl = max(wl, 1000.0 * ent.header['frames'] / ent.header['rate'])
            tlen = max(tlen, e['time'] + e['delay'] + wl * (e['loops'] + 1))
        if any(e['kind'] == 'loopstart' and e['count'] == 65535 for e in t['events']):
            return -1
        longest = max(longest, tlen)
    return int(round(longest))


def sound_loops(sound):
    """XACT_SOUNDCUE_PROPERTIES.dwLoopCount: the highest loop count of all tracks."""
    hi = 0
    for t in sound['tracks']:
        for e in t['events']:
            if e['kind'] == 'play':
                hi = max(hi, e['loops'])
            if e['kind'] == 'loopstart':
                hi = max(hi, e['count'])
    return hi


# ------------------------------------------------------------------------------------------------------------
# .xsb name table (Xbox sound bank, format version 11) - cross-check only
#
#   0x00 'SDBK'  u16 version (11)  u16 checksum
#   0x08 u32 offset of the wave bank name table (16-byte names)    0x0C..0x14 u32 offsets of later tables
#   0x18 u16 0   u16 hash table size (>= 16)   u16 sound count   u16 cue count   u16 ?   u16 wave bank count
#   0x28 16-byte bank name
#   0x38 cue entries, 20 bytes each: u16 flags, u16 sound index, u32 name offset, ... (names NUL-terminated)
# ------------------------------------------------------------------------------------------------------------

def read_xsb(path):
    with open(path, 'rb') as f:
        d = f.read()
    magic, ver = struct.unpack_from('<4sH', d, 0)
    if magic != b'SDBK':
        raise ValueError('not an SDBK file')
    wbtab = struct.unpack_from('<I', d, 8)[0]
    _, hsize, nsounds, ncues, _, nwb = struct.unpack_from('<6H', d, 0x18)
    name = d[0x28:0x38].split(b'\0')[0].decode('latin-1')
    cues = []
    for i in range(ncues):
        fl, sidx, noff = struct.unpack_from('<HHI', d, 0x38 + 20 * i)
        if not (0 < noff < len(d)):
            raise ValueError('cue %d: name offset 0x%x outside the file' % (i, noff))
        cues.append((d[noff:d.index(b'\0', noff)].decode('latin-1'), sidx))
    wbs = [d[wbtab + 16 * i: wbtab + 16 * i + 16].split(b'\0')[0].decode('latin-1') for i in range(nwb)]
    return dict(version=ver, name=name, sounds=nsounds, cues=cues, wavebanks=wbs)


# ------------------------------------------------------------------------------------------------------------
# .xwb entry table (Xbox wave bank, XDK xactwb.h, version 3) - cross-check only; the runtime reader is M16.
# Gives per entry the format (PCM / Xbox ADPCM / WMA, channels, rate) and the byte length of its data, from
# which the length in sample frames follows (Xbox ADPCM: 36 bytes per 64 frames per channel).
# ------------------------------------------------------------------------------------------------------------

def read_xwb(path):
    with open(path, 'rb') as f:
        d = f.read()
    sig, ver = struct.unpack_from('<4sI', d, 0)
    if sig != b'WBND':
        raise ValueError('not a WBND file')
    segs = [struct.unpack_from('<II', d, 8 + 8 * i) for i in range(4)]
    bd = segs[0][0]
    flags, count = struct.unpack_from('<II', d, bd)
    name = d[bd + 8:bd + 24].split(b'\0')[0].decode('latin-1')
    mdsize, _, align, cfmt = struct.unpack_from('<IIII', d, bd + 24)

    def fmt(v):
        return dict(tag=v & 3, ch=(v >> 2) & 7, rate=(v >> 5) & ((1 << 26) - 1), bits16=v >> 31)
    ents = []
    if flags & 0x20000:     # WAVEBANK_FLAGS_COMPACT: offset in sectors + length deviation, one shared format
        offs = []
        for i in range(count):
            v = struct.unpack_from('<I', d, segs[1][0] + 4 * i)[0]
            offs.append(((v & ((1 << 21) - 1)) * align, v >> 21))
        for i, (o, dev) in enumerate(offs):
            nxt = offs[i + 1][0] if i + 1 < count else segs[3][1]
            ents.append(dict(length=nxt - o - dev, **fmt(cfmt)))
    else:
        for i in range(count):
            _, fv, _, pl, _, _ = struct.unpack_from('<6I', d, segs[1][0] + mdsize * i)
            ents.append(dict(length=pl, **fmt(fv)))
    for e in ents:
        if e['tag'] == 1:
            e['frames'] = e['length'] // (36 * max(1, e['ch'])) * 64
        elif e['tag'] == 0:
            e['frames'] = e['length'] // ((2 if e['bits16'] else 1) * max(1, e['ch']))
        else:
            e['frames'] = None
    return dict(version=ver, name=name, flags=flags, ents=ents)


# ------------------------------------------------------------------------------------------------------------
# Encoding
# ------------------------------------------------------------------------------------------------------------

def file_digest(path):
    h = hashlib.sha1()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def encode(src, dst, quality):
    """ffmpeg -> Ogg Vorbis, sample rate and channel count kept, no metadata, bit-exact container."""
    # Byte-identical sources (the same WAV copied into several folders) share one cache file and may be
    # encoded by two workers at once: each writes its own temporary file, the last rename wins (same bytes).
    tmp = '%s.%d.part' % (dst, threading.get_ident())
    cmd = ['ffmpeg', '-nostdin', '-hide_banner', '-loglevel', 'error', '-y', '-i', src,
           '-map_metadata', '-1', '-fflags', '+bitexact', '-flags:a', '+bitexact',
           '-c:a', 'libvorbis', '-q:a', str(quality), '-f', 'ogg', tmp]
    r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if r.returncode != 0 or not os.path.exists(tmp):
        return r.stderr.decode('latin-1', 'replace').strip() or 'ffmpeg failed (%d)' % r.returncode
    os.replace(tmp, dst)
    return None


def cached_encode(job):
    src, cache_dir, quality = job
    key = hashlib.sha1(('%s|q%s|v%d' % (file_digest(src), quality, FORMAT_VERSION)).encode()).hexdigest()
    dst = os.path.join(cache_dir, key[:2], key + '.ogg')
    if os.path.exists(dst) and os.path.getsize(dst) > 0:
        return src, dst, None, True
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    err = encode(src, dst, quality)
    return src, (None if err else dst), err, False


# ------------------------------------------------------------------------------------------------------------
# Writers
# ------------------------------------------------------------------------------------------------------------

def write_wavebank(wb, out_sounds, music_rel):
    lines = ['uc2wb %d' % FORMAT_VERSION]
    if wb.music:
        data = music_rel.get(wb.name, '-')
        lines.append('bank name=%s file=%s lang=%s data=%s stream=1 entries=%d' % (
            esc(wb.name), esc(wb.bank_file), wb.lang or '-', esc(data), len(wb.entries)))
    else:
        lines.append('bank name=%s file=%s lang=%s data=%s stream=0 entries=%d' % (
            esc(wb.name), esc(wb.bank_file), wb.lang or '-', esc(wb.stem + '.ogp'), len(wb.entries)))
    for e in wb.entries:
        src = os.path.basename(e.source) if e.source else esc(e.file.replace('\\', '/'))
        if e.missing:
            lines.append('wave index=%d name=%s missing=%s src=%s' % (e.index, esc(e.name), esc(e.missing), esc(src)))
            continue
        h = e.header
        lines.append('wave index=%d name=%s offset=%d size=%d rate=%d channels=%d frames=%d loop=%d,%d src=%s' % (
            e.index, esc(e.name), e.offset, e.size, h['rate'], h['channels'], h['frames'], e.loop[0], e.loop[1],
            esc(src)))
    with open(os.path.join(out_sounds, wb.stem + '.uc2wb'), 'w', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')


def write_cues(sb, out_sounds, wavebanks_by_name, groups):
    L = ['uc2cues %d' % FORMAT_VERSION,
         'bank name=%s sounds=%d cues=%d' % (esc(sb.name), len(sb.sounds), len(sb.cues))]
    used_groups = sorted({s['group'] for s in sb.sounds if s['group'] != '-'})
    for g in used_groups:
        L.append('group name=%s volume=%d' % (esc(g), groups.get(g, 0)))
    for i, s in enumerate(sb.sounds):
        L.append('sound index=%d name=%s priority=%d volume=%d pitch=%d category=%s group=%s layer=%s '
                 'headroomboost=%d i3dl2=%d lfe=%d 3d=%d length=%d loops=%d' % (
                     i, esc(s['name']), s['priority'], s['volume'], s['pitch'], esc(s['category']), esc(s['group']),
                     esc(s['layer']), s['headroomboost'], s['i3dl2'], s['lfe'], s['is3d'],
                     sound_length_ms(s, wavebanks_by_name), sound_loops(s)))
        if s['3d']:
            d = s['3d']
            L.append(' 3d mode=%d cone_in=%d cone_out=%d cone_vol=%d min=%s max=%s distfactor=%s rolloff=%s '
                     'doppler=%s' % (d['mode'], d['cone_in'], d['cone_out'], d['cone_vol'], fmtf(d['min']),
                                     fmtf(d['max']), fmtf(d['distfactor']), fmtf(d['rolloff']), fmtf(d['doppler'])))
        if s['eqparams']:
            L.append(' eq enabled=%d gain=%d q=%d freq=%d' % ((s['eq'],) + s['eqparams']))
        for r in s['rpcs']:
            L.append(' rpc name=%s track=%d type=%d deviation=%s points=%s' % (
                esc(r['name']), r['track'], r['type'], ','.join(str(x) for x in r['deviation']) or '-',
                ','.join('%d:%d' % p for p in r['points'])))
        for ti, t in enumerate(s['tracks']):
            L.append(' track index=%d name=%s' % (ti, esc(t['name'])))
            for e in t['events']:
                k = e['kind']
                if k == 'play':
                    L.append('  play time=%d delay=%d loops=%d newvar=%d playlist=%d pitch=%d,%d volume=%d,%d '
                             'waves=%d' % (e['time'], e['delay'], e['loops'], e['newvar'], e['playlist'],
                                           e['pitch'][0], e['pitch'][1], e['vol'][0], e['vol'][1], len(e['waves'])))
                    for w in e['waves']:
                        L.append('   wave bank=%s entry=%d weight=%d name=%s' % (
                            esc(w['bank']), w['entry'], w['weight'], esc(w['name'] or '-')))
                elif k in ('setvol', 'setpitch'):
                    L.append('  %s time=%d mode=%s relative=%d type=%d steps=%d dur=%d lo=%d hi=%d' % (
                        k, e['time'], e['mode'], e['relative'], e['type'], e['steps'], e['dur'], e['lo'], e['hi']))
                elif k == 'lpf':
                    L.append('  lpf time=%d mode=%s relative=%d steps=%d dur=%d freq=%d,%d res=%d,%d' % (
                        e['time'], e['mode'], e['relative'], e['steps'], e['dur'], e['freq'][0], e['freq'][1],
                        e['res'][0], e['res'][1]))
                elif k == 'lfo':
                    L.append('  lfo time=%d delay=%d delta=%d pitchmod=%d filtermod=%d ampmod=%d' % (
                        e['time'], e['delay'], e['delta'], e['pitchmod'], e['filtermod'], e['ampmod']))
                elif k == 'pitchlfo':
                    L.append('  pitchlfo time=%d delay=%d delta=%d pitchmod=%d' % (
                        e['time'], e['delay'], e['delta'], e['pitchmod']))
                elif k == 'eg':
                    L.append('  eg time=%d delay=%d attack=%d hold=%d decay=%d release=%d sustain=%d mode=%d '
                             'pitchscale=%d filtercutoff=%d' % (
                                 e['time'], e['delay'], e['attack'], e['hold'], e['decay'], e['release'],
                                 e['sustain'], e['mode'], e['pitchscale'], e['filtercutoff']))
                elif k == 'mixbins':
                    L.append('  mixbins time=%d bins=%s' % (e['time'], ','.join('%d:%d' % b for b in e['bins'])))
                elif k == 'loopstart':
                    L.append('  loopstart time=%d count=%d outer=%d' % (e['time'], e['count'], e['outer']))
    for i, c in enumerate(sb.cues):
        L.append('cue index=%d name=%s queue=%d sounds=%s' % (
            i, esc(c['name']), c['queue'],
            ','.join('%d:%d' % (si, w) for si, w in c['sounds']) if c['sounds'] else '-'))
    with open(os.path.join(out_sounds, os.path.splitext(sb.bank_file)[0] + '.uc2cues'), 'w', newline='\n') as f:
        f.write('\n'.join(L) + '\n')


# ------------------------------------------------------------------------------------------------------------
# Main
# ------------------------------------------------------------------------------------------------------------

def main():
    here = os.path.dirname(os.path.abspath(__file__))
    work = os.path.abspath(os.path.join(here, '..', '..'))
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--epic', default=os.path.join(work, 'epic-sources'), help='the pristine Epic tree (read only)')
    ap.add_argument('--out', default=os.path.join(work, 'assets', 'uc2-cooked'))
    ap.add_argument('--jobs', type=int, default=6, help='parallel ffmpeg encoders')
    ap.add_argument('--quality', default='4', help='libvorbis quality for sound effects and speech')
    ap.add_argument('--music-quality', default='5', help='libvorbis quality for music')
    ap.add_argument('--banks', default='', help='comma list of sound banks to cook (default: all)')
    ap.add_argument('--no-encode', action='store_true', help='parse, check and cross-check only')
    ap.add_argument('--relink-localised', action='store_true',
                    help='re-order localised wave banks to the base bank by wave name (repairs Epic data; '
                         'off by default because the Xbox played them in their own order)')
    args = ap.parse_args()

    xap = os.path.join(args.epic, 'Sounds', 'Avaris.xap')
    sounds_dir = os.path.join(args.epic, 'Sounds')
    out_sounds = os.path.join(args.out, 'Sounds')
    out_music = os.path.join(args.out, 'Music')
    cache = os.path.join(args.out, '.cache')
    for d in (out_sounds, out_music, cache):
        os.makedirs(d, exist_ok=True)

    report = []

    def say(msg):
        print(msg)
        report.append(msg)

    # --- parse + schema ----------------------------------------------------------------------------------
    root = parse_xap(xap)
    unknown = []
    counts = collections.Counter()
    check_schema(root, '<root>', unknown, counts)
    say('xap: %s, signature %s version %s' % (xap, root.get('Signature'), root.get('Version')))
    say('xap: %d wave banks, %d sound banks, %d sound groups, %d categories, %d layers' % (
        len(root.kids('Wave Bank')), len(root.kids('Sound Bank')), len(root.kids('Sound Group')),
        len(root.kids('Sound Category')), len(root.kids('Sound Layer'))))
    say('schema: %d things not understood' % len(unknown))
    report.extend(unknown)

    groups = {g.get('Name'): num(g.get('Volume')) for g in root.kids('Sound Group')}
    say('groups: ' + ' '.join('%s=%d' % kv for kv in sorted(groups.items())))
    say('categories: ' + ' '.join(c.get('Name') for c in root.kids('Sound Category')))
    say('layers: ' + ' '.join(c.get('Name') for c in root.kids('Sound Layer')))

    # --- wave banks + sources -------------------------------------------------------------------------------
    wavebanks = [WaveBank(n) for n in root.kids('Wave Bank')]
    by_file = {wb.stem: wb for wb in wavebanks}
    base_by_name = {}
    for wb in wavebanks:
        if not wb.lang:
            if wb.name in base_by_name:
                say('WAVE BANK name %s appears twice without a language suffix (%s, %s)' % (
                    wb.name, base_by_name[wb.name].bank_file, wb.bank_file))
            base_by_name[wb.name] = wb
    for wb in wavebanks:
        if wb.lang and wb.name not in base_by_name:
            say('WAVE BANK %s: localised bank without a base bank' % wb.bank_file)
        if not wb.lang and wb.stem != wb.name:
            say('WAVE BANK %s: file %s does not match its name' % (wb.name, wb.bank_file))

    src_index = SourceIndex(args.epic)
    missing = []
    for wb in wavebanks:
        for e in wb.entries:
            rel = os.path.normpath(os.path.join('Sounds', e.file.replace('\\', '/'))).replace(os.sep, '/')
            path = src_index.find(rel)
            if path is None and rel.startswith('../'):
                # "..\..\UC2Demo\SoundData\A_Liquid\X.wav": a file of the demo tree. The same relative path
                # below this tree's SoundData is tried (and reported) before calling it missing.
                m = re.search(r'(?i)/(SoundData/.*)$', rel)
                if m:
                    path = src_index.find(m.group(1))
                    if path:
                        say('SOURCE %s\\%s: %s is outside the tree; using %s' % (
                            wb.bank_file, e.name, e.file, os.path.relpath(path, args.epic)))
            if wb.music:
                # Music entries name ..\Music\<Track>.wma or .wav; the cook uses Music/<Track>.wav (the source).
                stem = os.path.splitext(os.path.basename(e.file.replace('\\', '/')))[0]
                path = src_index.find('Music/%s.wav' % stem)
            if path is None:
                e.missing = 'no-file'
                missing.append((wb.bank_file, e.index, e.name, e.file))
                continue
            e.source = path
            try:
                e.header = read_wav_header(path)
            except ValueError as ex:
                e.missing = 'unreadable'
                missing.append((wb.bank_file, e.index, e.name, '%s (%s)' % (e.file, ex)))
                e.source = None
                continue
            h = e.header
            if not wb.music:
                if h['channels'] != e.channels or h['rate'] != e.rate:
                    say('SOURCE %s\\%s: xap says %d ch %d Hz, file has %d ch %d Hz (file wins)' % (
                        wb.bank_file, e.name, e.channels, e.rate, h['channels'], h['rate']))
                if h['kind'] == 'wav' and (h['data_offset'] != e.play_offset or h['data_bytes'] != e.play_length):
                    say('SOURCE %s\\%s: play region %d+%d differs from the data chunk %d+%d' % (
                        wb.bank_file, e.name, e.play_offset, e.play_length, h['data_offset'], h['data_bytes']))
            if e.loop_length:
                align = h['align'] or 1
                start, length = e.loop_offset // align, e.loop_length // align
                if start + length > h['frames']:
                    say('LOOP %s\\%s: loop region %d+%d frames beyond the %d frames of the wave (clamped)' % (
                        wb.bank_file, e.name, start, length, h['frames']))
                    length = max(0, h['frames'] - start)
                e.loop = (start, length)
    say('wave entries: %d in %d banks, %d without a usable source file' % (
        sum(len(wb.entries) for wb in wavebanks), len(wavebanks), len(missing)))
    for m in missing:
        say('MISSING %s entry %d %s <- %s' % m)

    # Localised banks must line up entry by entry with their base bank: a sound bank names a wave by its index
    # in the wave bank, and on the Xbox the localised .xwb simply replaces the base one at the same indices.
    misaligned = 0
    for wb in wavebanks:
        if not wb.lang or wb.name not in base_by_name:
            continue
        base = base_by_name[wb.name]
        if len(base.entries) != len(wb.entries):
            say('LOCALISED %s: %d entries, base %s has %d' % (wb.bank_file, len(wb.entries), base.bank_file,
                                                               len(base.entries)))
        leaf = lambda e: e.name.split('\\')[-1].lower()
        diff = [(a.index, a.name, b.name) for a, b in zip(base.entries, wb.entries) if leaf(a) != leaf(b)]
        misaligned += len(diff)
        for d in diff:
            say('LOCALISED %s entry %d is %s, base has %s' % (wb.bank_file, d[0], d[2], d[1]))
        if diff and args.relink_localised:
            # Optional repair (not the Xbox behaviour): put each localised wave at the index of the base wave
            # with the same leaf name; base waves without a localised twin keep the localised bank's entry.
            by_leaf = {leaf(e): e for e in wb.entries}
            order = [by_leaf.get(leaf(a)) for a in base.entries]
            if None not in order and len(set(id(e) for e in order)) == len(order):
                for i, e in enumerate(order):
                    e.index = i
                wb.entries = order
                say('LOCALISED %s: re-linked to the base order by name (--relink-localised)' % wb.bank_file)
            else:
                say('LOCALISED %s: cannot re-link by name (names do not pair up); left in Xbox order' % wb.bank_file)
    say('localised wave banks: %d entries sit at a different index than the base wave of the same name '
        '(kept in the Xbox order: %s)' % (misaligned, 'no, re-linked' if args.relink_localised else 'yes'))

    # --- sound banks ----------------------------------------------------------------------------------------
    soundbanks = [SoundBank(n, report) for n in root.kids('Sound Bank')]
    if args.banks:
        want = set(args.banks.split(','))
        soundbanks = [sb for sb in soundbanks if sb.name in want]
    nsounds = sum(len(sb.sounds) for sb in soundbanks)
    ncues = sum(len(sb.cues) for sb in soundbanks)
    say('sound banks: %d, sounds: %d, cues: %d' % (len(soundbanks), nsounds, ncues))

    # Which wave banks the cooked sound banks need; every variation checked against its wave bank.
    needed = set()
    unresolved = []
    playlist_types = collections.Counter()
    for sb in soundbanks:
        for s in sb.sounds:
            for t in s['tracks']:
                for e in t['events']:
                    if e['kind'] != 'play':
                        continue
                    playlist_types[e['playlist']] += 1
                    for w in e['waves']:
                        wb = base_by_name.get(w['bank'])
                        if wb is None:
                            unresolved.append('%s\\%s: wave bank %s does not exist' % (sb.name, s['name'], w['bank']))
                            continue
                        needed.add(wb.name)
                        if not (0 <= w['entry'] < len(wb.entries)):
                            unresolved.append('%s\\%s: entry %d outside wave bank %s' % (
                                sb.name, s['name'], w['entry'], w['bank']))
                        elif w['name'] and wb.entries[w['entry']].name != w['name']:
                            unresolved.append('%s\\%s: entry %d of %s is %s, the sound names %s' % (
                                sb.name, s['name'], w['entry'], w['bank'], wb.entries[w['entry']].name, w['name']))
    say('play events by playlist type: %s' % dict(sorted(playlist_types.items())))
    say('variation references not resolved: %d' % len(unresolved))
    report.extend('UNRESOLVED ' + u for u in unresolved)

    # Cue resolution: every cue -> its sounds -> at least one variation with a cooked (non-missing) wave.
    cue_ok = cue_silent = cue_partial = 0
    for sb in soundbanks:
        for c in sb.cues:
            good = total = 0
            for si, _ in c['sounds']:
                for t in sb.sounds[si]['tracks']:
                    for e in t['events']:
                        if e['kind'] != 'play':
                            continue
                        for w in e['waves']:
                            total += 1
                            wb = base_by_name.get(w['bank'])
                            if wb and 0 <= w['entry'] < len(wb.entries) and not wb.entries[w['entry']].missing:
                                good += 1
            if good == 0:
                cue_silent += 1
                say('CUE %s: no playable wave (%d variations, none cooked)' % (c['name'], total))
            elif good < total:
                cue_partial += 1
                say('CUE %s: %d of %d variations playable' % (c['name'], good, total))
            else:
                cue_ok += 1
    say('cues resolved (base language): %d fully, %d partly, %d with no playable wave' % (
        cue_ok, cue_partial, cue_silent))

    # --- .xsb cross-check -----------------------------------------------------------------------------------
    xsb_files = {os.path.splitext(f)[0]: os.path.join(sounds_dir, f) for f in os.listdir(sounds_dir)
                 if f.lower().endswith('.xsb')}
    xap_banks = {os.path.splitext(sb.bank_file)[0]: sb for sb in (SoundBank(n, []) for n in root.kids('Sound Bank'))}
    agree = 0
    for stem in sorted(set(xsb_files) | set(xap_banks)):
        if stem not in xap_banks:
            x = read_xsb(xsb_files[stem])
            say('XSB %s.xsb: not in the project (%d cues, %d sounds; cues: %s)' % (
                stem, len(x['cues']), x['sounds'], ', '.join(n for n, _ in x['cues'][:8])))
            continue
        if stem not in xsb_files:
            say('XSB %s: in the project, no shipped .xsb' % stem)
            continue
        sb = xap_banks[stem]
        try:
            x = read_xsb(xsb_files[stem])
        except ValueError as ex:
            say('XSB %s.xsb: unreadable (%s)' % (stem, ex))
            continue
        names_x = [n for n, _ in x['cues']]
        names_p = [c['name'] for c in sb.cues]
        diffs = []
        if x['name'] != sb.name:
            diffs.append('bank name %s vs %s' % (x['name'], sb.name))
        if len(names_x) != len(names_p):
            diffs.append('%d cues in .xsb, %d in project' % (len(names_x), len(names_p)))
        if x['sounds'] != len(sb.sounds):
            diffs.append('%d sounds in .xsb, %d in project' % (x['sounds'], len(sb.sounds)))
        only_x = sorted(set(names_x) - set(names_p))
        only_p = sorted(set(names_p) - set(names_x))
        if only_x:
            diffs.append('cues only in .xsb: ' + ', '.join(only_x))
        if only_p:
            diffs.append('cues only in project: ' + ', '.join(only_p))
        ref_wbs = []
        for s in sb.sounds:
            for t in s['tracks']:
                for e in t['events']:
                    for w in e.get('waves', []):
                        if w['bank'] not in ref_wbs:
                            ref_wbs.append(w['bank'])
        if sorted(x['wavebanks']) != sorted(ref_wbs):
            diffs.append('wave banks .xsb %s vs project %s' % (','.join(x['wavebanks']), ','.join(ref_wbs)))
        if diffs:
            say('XSB %s: ' % stem + '; '.join(diffs))
        else:
            agree += 1
    say('xsb cross-check: %d of %d project sound banks agree in name, cue names, cue count, sound count and '
        'wave banks' % (agree, len(xap_banks)))

    # --- .xwb cross-check: the shipped wave banks have the project's entries in the project's order ----------
    xwb_files = {os.path.splitext(f)[0]: os.path.join(sounds_dir, f) for f in os.listdir(sounds_dir)
                 if f.lower().endswith('.xwb')}
    agree = 0
    for stem in sorted(set(xwb_files) - set(by_file)):
        say('XWB %s.xwb: not in the project' % stem)
    for wb in wavebanks:
        if wb.stem not in xwb_files:
            say('XWB %s: in the project, no shipped .xwb' % wb.bank_file)
            continue
        try:
            x = read_xwb(xwb_files[wb.stem])
        except ValueError as ex:
            say('XWB %s: unreadable (%s)' % (wb.bank_file, ex))
            continue
        diffs = []
        if x['name'] != wb.name:
            diffs.append('bank name %s' % x['name'])
        if len(x['ents']) != len(wb.entries):
            diffs.append('%d entries shipped, %d in project' % (len(x['ents']), len(wb.entries)))
        for e, xe in zip(wb.entries, x['ents']):
            h = e.header
            if not h:
                continue
            if xe['ch'] != h['channels'] or (xe['rate'] != h['rate'] and xe['rate'] != e.rate):
                diffs.append('entry %d %s: shipped %d ch %d Hz, source %d ch %d Hz' % (
                    e.index, e.name, xe['ch'], xe['rate'], h['channels'], h['rate']))
            elif xe['frames'] is not None and abs(xe['frames'] - h['frames']) > max(130, h['frames'] // 100) \
                    and not (wb.music):
                diffs.append('entry %d %s: shipped %d frames, source %d' % (e.index, e.name, xe['frames'],
                                                                         h['frames']))
        if diffs:
            say('XWB %s: ' % wb.bank_file + '; '.join(diffs[:6]) + (' (+%d more)' % (len(diffs) - 6)
                                                                     if len(diffs) > 6 else ''))
        else:
            agree += 1
    say('xwb cross-check: %d of %d project wave banks agree with the shipped .xwb in name, entry count and, '
        'entry by entry, channels, rate and length (within 1%%)' % (agree, len(wavebanks)))

    if args.no_encode:
        with open(os.path.join(out_sounds, 'cook-report.txt'), 'w', newline='\n') as f:
            f.write('\n'.join(report) + '\n')
        return 0

    # --- encode ---------------------------------------------------------------------------------------------
    cook_wbs = [wb for wb in wavebanks if wb.name in needed]
    jobs = {}
    for wb in cook_wbs:
        q = args.music_quality if wb.music else args.quality
        for e in wb.entries:
            if e.source:
                jobs[(e.source, q)] = None
    print('encoding %d distinct sources with %d encoders...' % (len(jobs), args.jobs))
    done = fresh = 0
    failures = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as ex:
        futs = {ex.submit(cached_encode, (src, cache, q)): (src, q) for (src, q) in jobs}
        for fut in concurrent.futures.as_completed(futs):
            src, dst, err, hit = fut.result()
            jobs[futs[fut]] = dst
            done += 1
            fresh += 0 if hit else 1
            if err:
                failures.append((src, err))
            if done % 500 == 0:
                print('  %d/%d (%d encoded now, the rest from the cache)' % (done, len(jobs), fresh))
    say('encoded: %d sources (%d now, %d from the cache), %d failures' % (len(jobs), fresh, len(jobs) - fresh,
                                                                         len(failures)))
    for src, err in failures:
        say('ENCODE FAILED %s: %s' % (os.path.relpath(src, args.epic), err))

    # --- pack + write ---------------------------------------------------------------------------------------
    music_rel = {}
    total_sound_bytes = total_music_bytes = 0
    for wb in cook_wbs:
        q = args.music_quality if wb.music else args.quality
        for e in wb.entries:
            if e.source and not jobs.get((e.source, q)):
                e.missing = 'encode-failed'
        if wb.music:
            ent = [e for e in wb.entries if not e.missing]
            if len(wb.entries) != 1:
                say('MUSIC bank %s has %d entries (one expected)' % (wb.bank_file, len(wb.entries)))
            for e in ent:
                track = os.path.splitext(os.path.basename(e.source))[0]
                dst = os.path.join(out_music, track + '.ogg')
                shutil.copyfile(jobs[(e.source, q)], dst)
                e.offset, e.size = 0, os.path.getsize(dst)
                music_rel[wb.name] = '../Music/%s.ogg' % track
                total_music_bytes += e.size
            write_wavebank(wb, out_sounds, music_rel)
            continue
        ogp = os.path.join(out_sounds, wb.stem + '.ogp')
        with open(ogp + '.part', 'wb') as f:
            for e in wb.entries:
                if e.missing:
                    continue
                with open(jobs[(e.source, q)], 'rb') as g:
                    blob = g.read()
                e.offset, e.size = f.tell(), len(blob)
                f.write(blob)
        os.replace(ogp + '.part', ogp)
        total_sound_bytes += os.path.getsize(ogp)
        write_wavebank(wb, out_sounds, music_rel)

    # Music tracks that no wave bank names (Music/*.wav has 23, the project 21) are cooked too.
    for fn in sorted(os.listdir(os.path.join(args.epic, 'Music'))):
        if not fn.lower().endswith('.wav'):
            continue
        dst = os.path.join(out_music, os.path.splitext(fn)[0] + '.ogg')
        if os.path.exists(dst):
            continue
        src, enc, err, _ = cached_encode((os.path.join(args.epic, 'Music', fn), cache, args.music_quality))
        if err:
            say('ENCODE FAILED Music/%s: %s' % (fn, err))
            continue
        shutil.copyfile(enc, dst)
        total_music_bytes += os.path.getsize(dst)
        say('MUSIC %s: not used by any wave bank of the project; cooked anyway' % fn)

    for sb in soundbanks:
        write_cues(sb, out_sounds, base_by_name, groups)

    xbox = sum(os.path.getsize(os.path.join(sounds_dir, f)) for f in os.listdir(sounds_dir)
               if f.lower().endswith(('.xwb', '.xsb')))
    say('cooked: %d wave banks (%.1f MB of sound packs), %d music tracks (%.1f MB), %d cue tables; '
        'Xbox banks (.xwb+.xsb): %.1f MB' % (len(cook_wbs), total_sound_bytes / 1e6,
                                              len([f for f in os.listdir(out_music) if f.endswith('.ogg')]),
                                              total_music_bytes / 1e6, len(soundbanks), xbox / 1e6))
    with open(os.path.join(out_sounds, 'cook-report.txt'), 'w', newline='\n') as f:
        f.write('\n'.join(report) + '\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
