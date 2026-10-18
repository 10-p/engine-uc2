#!/usr/bin/env python3
"""cook-movies.py: cook Unreal Championship 2's Xbox movies (XMV: WMV2 video, Xbox ADPCM or PCM audio) into
Ogg Theora/Vorbis files the portable movie player (Source/UC2Movie) plays.

Input (read only): <epic-sources>/Movies/*.xmv and <epic-sources>/Textures/Movies/*.xmv.

Output (never committed; default <repo>/../assets/uc2-cooked):
  Movies/<Name>.ogv               Theora video + the international (first, English) audio track as Vorbis
  Movies/<Name>_<lang>.oga        Vorbis only: the deu/fra/ita/spa track of a multi-language movie, written
                                  only when it differs from the international track
  Textures/Movies/<Name>.ogv      Theora only (the MovieMaterial clips have no audio)
  Movies/movie-manifest.json      what was cooked, per file: frames and samples in/out, quality, warnings
  Movies/cook-report.txt          the same as a readable table, plus every ffmpeg warning
  Movies/.cook/<Name>.json        per-file results; a re-cook skips files whose source and settings are unchanged

The format (ours) is specified in docs/porting/analysis/video.md ("Cooked format"). Usage:

  tools/cook-movies.py [--epic <epic-sources>] [--out <dir>] [--jobs 4] [--only UC2_Logos,Portrait_Char_Anubis]
                      [--video-quality 7] [--keyint 150] [--audio-quality 4] [--pcm-audio-quality 5]
                      [--no-measure] [--force]

Every ffmpeg/ffprobe process runs inside <repo>/../bin/capped 2G (at most --jobs of them at once, default 4).
"""

import argparse
import concurrent.futures
import hashlib
import json
import os
import re
import struct
import subprocess
import sys
import threading

FORMAT_VERSION = 1
# Audio stream index -> language, from the Xbox player: XMVHelper.cpp:160-189 (GetLocalizedAudioStream):
# German 1, French 2, Italian 3, Spanish 4, every other language 0. Language codes as the engine names them
# (UObject::GetLanguage(), GLanguageExt in Core/Src/UnObj.cpp), as the audio cook names its banks.
TRACK_LANGS = ('int', 'deu', 'fra', 'ita', 'spa')

here = os.path.dirname(os.path.abspath(__file__))
work = os.path.abspath(os.path.join(here, '..', '..'))
capped = os.path.join(work, 'bin', 'capped')

print_lock = threading.Lock()


def log(msg):
    with print_lock:
        print(msg, flush=True)


def run(cmd, timeout=3600, mem='2G', binary=False):
    """Run cmd under the memory ceiling. Returns (rc, stdout, stderr lines)."""
    full = [capped, mem, str(timeout), '--'] + cmd
    r = subprocess.run(full, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    err = r.stderr.decode('utf-8', 'replace').splitlines()
    out = r.stdout if binary else r.stdout.decode('utf-8', 'replace')
    return r.returncode, out, err


def sha1_file(path):
    h = hashlib.sha1()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


# The xmv demuxer reports AVERROR(EIO) when it tries to read a packet header at the exact end of the file
# (checked: the last packet ends at the file size). Kept in the warnings, classified as known.
KNOWN_WARNINGS = (
    (re.compile(r'\[in#0/xmv @ 0x[0-9a-f]+\] Error during demuxing: Input/output error'),
     'xmv demuxer: EIO at the end of the file (after the last packet)'),
    (re.compile(r'Guessed Channel Layout: stereo'), 'audio track without a channel layout: stereo assumed'),
)


def classify(lines):
    """ffmpeg stderr lines -> list of (text, known-explanation or None). Progress lines are dropped."""
    out = []
    for l in lines:
        l = l.strip()
        if not l or l.startswith('capped:'):
            if l.startswith('capped:'):
                out.append((l, None))
            continue
        expl = None
        for rx, e in KNOWN_WARNINGS:
            if rx.search(l):
                expl = e
                break
        out.append((re.sub(r'0x[0-9a-f]+', '0x..', l), expl))
    return out


def probe(src):
    rc, out, err = run(['ffprobe', '-v', 'error', '-show_entries',
                        'stream=index,codec_name,codec_type,width,height,pix_fmt,sample_rate,channels',
                        '-of', 'json', src])
    if rc:
        raise RuntimeError('ffprobe failed on %s: %s' % (src, err[-3:]))
    streams = json.loads(out)['streams']
    rc, out, err = run(['ffprobe', '-v', 'error', '-select_streams', 'v:0', '-show_entries', 'packet=pts_time',
                        '-of', 'csv=p=0', src])
    pts = [float(x.split(',')[0]) for x in out.split() if x.strip() and x.split(',')[0] not in ('', 'N/A')]
    return streams, pts


def frame_rate(pts):
    """The XMV stores millisecond times; 33/34 ms steps are 30 fps, 66/67 ms steps 15 fps."""
    steps = sorted(round((b - a) * 1000) for a, b in zip(pts, pts[1:]) if b > a)
    med = steps[len(steps) // 2]
    for fps in (30, 15, 25, 60):
        if abs(med - 1000.0 / fps) <= 1.0:
            return fps
    raise RuntimeError('cannot tell the frame rate from a median step of %d ms' % med)


def pcm_stats(src, stream, warnings, wav=None):
    """Decode one audio stream to s16le: sample frames and SHA-1 of the PCM. With wav, also write the PCM
    there as a WAV file (the encoder's input: see the note on PCM timestamps in cook_one)."""
    full = [capped, '2G', '3600', '--', 'ffmpeg', '-v', 'warning', '-i', src, '-map', '0:a:%d' % stream,
            '-f', 's16le', '-']
    p = subprocess.Popen(full, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    h = hashlib.sha1()
    n = 0
    out = open(wav + '.pcm', 'wb') if wav else None
    while True:
        b = p.stdout.read(1 << 20)
        if not b:
            break
        h.update(b)
        n += len(b)
        if out:
            out.write(b)
    err = p.stderr.read().decode('utf-8', 'replace').splitlines()
    p.wait()
    warnings.extend(('decode a:%d' % stream, t, e) for t, e in classify(err))
    if out:
        out.close()
    return n // 4, h.hexdigest()


def write_wav(pcm_path, wav_path, rate, channels):
    size = os.path.getsize(pcm_path)
    with open(wav_path, 'wb') as w, open(pcm_path, 'rb') as r:
        w.write(b'RIFF' + struct.pack('<I', 36 + size) + b'WAVEfmt ' +
                struct.pack('<IHHIIHH', 16, 1, channels, rate, rate * channels * 2, channels * 2, 16) +
                b'data' + struct.pack('<I', size))
        for block in iter(lambda: r.read(1 << 20), b''):
            w.write(block)
    os.remove(pcm_path)


def ogg_pages(path):
    """(serial, granule, header-type, first-packet-bytes) of every page of an Ogg file."""
    pages = []
    with open(path, 'rb') as f:
        d = f.read()
    i = 0
    while True:
        i = d.find(b'OggS', i)
        if i < 0 or i + 27 > len(d):
            break
        htype, granule, serial = d[i + 5], struct.unpack_from('<q', d, i + 6)[0], struct.unpack_from('<I', d, i + 14)[0]
        nseg = d[i + 26]
        lacing = d[i + 27:i + 27 + nseg]
        body = i + 27 + nseg
        size = sum(lacing)
        pages.append((serial, granule, htype, d[body:body + 8]))
        i = body + size
    return pages


def ogg_summary(path):
    """Per logical stream: codec, last granule position. Theora granule -> frame count (keyframe + offset: from
    bitstream 3.2.1 on the granule position of frame n is n+1); Vorbis granule -> sample frames."""
    streams = {}
    for serial, granule, htype, first in ogg_pages(path):
        s = streams.setdefault(serial, {'codec': None, 'last_granule': -1, 'shift': None})
        if htype & 2:
            if first.startswith(b'\x80theora'):
                s['codec'] = 'theora'
            elif first.startswith(b'\x01vorbis'):
                s['codec'] = 'vorbis'
        if granule >= 0:
            s['last_granule'] = granule
    # The Theora granule shift is in the identification header (byte 40, bits 5..9 of a 16-bit field).
    with open(path, 'rb') as f:
        d = f.read(65536)
    k = d.find(b'\x80theora')
    shift = None
    if k >= 0:
        shift = ((d[k + 40] & 0x03) << 3) | (d[k + 41] >> 5)
    out = {}
    for serial, s in streams.items():
        if s['codec'] == 'theora' and shift is not None and s['last_granule'] >= 0:
            g = s['last_granule']
            out['theora_frames'] = (g >> shift) + (g & ((1 << shift) - 1))
            out['theora_shift'] = shift
        elif s['codec'] == 'vorbis':
            out.setdefault('vorbis_samples', []).append(s['last_granule'])
    return out


def count_frames(path, warnings, what):
    # Packets, not decoded frames: libtheora writes a held (repeated) picture as a zero-byte packet, which
    # ffmpeg's decoder does not turn into a frame.
    rc, out, err = run(['ffprobe', '-v', 'warning', '-count_frames', '-count_packets', '-select_streams', 'v:0',
                        '-show_entries', 'stream=nb_read_frames,nb_read_packets,width,height,pix_fmt,r_frame_rate',
                        '-of', 'json', path])
    warnings.extend((what, t, e) for t, e in classify(err))
    return json.loads(out)['streams'][0]


def measure(src, fps, out, warnings):
    """PSNR and SSIM of the cooked video against the XMV decoded through the same frame-rate filter."""
    res = {}
    for metric in ('psnr', 'ssim'):
        # -threads 1 for the Theora side: ffmpeg's frame-threaded Theora decoder is not deterministic
        # (TVDisplaySelket2 decoded differently in two runs; single-threaded it agrees with libtheora).
        rc, _, err = run(['ffmpeg', '-v', 'info', '-nostats', '-i', src, '-threads', '1', '-i', out, '-lavfi',
                          '[0:v]fps=%d[r];[r][1:v]%s' % (fps, metric), '-f', 'null', '-'])
        line = [l for l in err if ('PSNR y:' in l or 'SSIM Y:' in l)]
        if line:
            l = line[-1]
            if metric == 'psnr':
                m = re.search(r'y:([\d.inf]+) u:([\d.inf]+) v:([\d.inf]+) average:([\d.inf]+) min:([\d.inf]+)', l)
                res['psnr_y'], res['psnr_u'], res['psnr_v'], res['psnr_avg'], res['psnr_min'] = m.groups()
            else:
                m = re.search(r'Y:([\d.]+) .*U:([\d.]+) .*V:([\d.]+) .*All:([\d.]+)', l)
                res['ssim_y'], res['ssim_u'], res['ssim_v'], res['ssim_all'] = m.groups()
        warnings.extend(('measure %s' % metric, t, e) for t, e in classify(
            [l for l in err if ('rror' in l or 'arning' in l) and 'Parsed_' not in l]))
    return res


def cook_one(src, kind, outdir, args):
    name = os.path.splitext(os.path.basename(src))[0]
    rel = 'Movies' if kind == 'movie' else os.path.join('Textures', 'Movies')
    dst = os.path.join(outdir, rel)
    state_dir = os.path.join(outdir, 'Movies', '.cook')
    os.makedirs(dst, exist_ok=True)
    os.makedirs(state_dir, exist_ok=True)
    state_path = os.path.join(state_dir, name + '.json')
    settings = {'format': FORMAT_VERSION, 'vq': args.video_quality, 'keyint': args.keyint,
                'aq': args.audio_quality, 'pcmaq': args.pcm_audio_quality, 'measure': not args.no_measure}
    src_sha1 = sha1_file(src)
    if not args.force and os.path.exists(state_path):
        old = json.load(open(state_path))
        if old.get('source_sha1') == src_sha1 and old.get('settings') == settings and \
                all(os.path.exists(os.path.join(outdir, o['file'])) for o in old['outputs']):
            log('%-28s unchanged' % name)
            return old

    warnings = []
    streams, pts = probe(src)
    v = [s for s in streams if s['codec_type'] == 'video']
    a = [s for s in streams if s['codec_type'] == 'audio']
    if len(v) != 1:
        raise RuntimeError('%s: %d video streams' % (name, len(v)))
    v = v[0]
    fps = frame_rate(pts)
    slots = int(round(pts[-1] * fps)) + 1
    step = 1000.0 / fps
    gaps = [(round(x, 3), round((y - x) * 1000)) for x, y in zip(pts, pts[1:]) if (y - x) * 1000 > step * 1.5]

    # Audio: decode every track (sample count + PCM hash) to find tracks identical to the first.
    # The encoder reads the audio from a WAV of the decoded track, not from the XMV: ffmpeg's xmv demuxer
    # stamps PCM packets with the ADPCM time base (16/11025 s per tick, 93 ticks per packet), so a PCM track
    # muxed straight from the XMV gets granule positions 3.55 times too large (UC2_Logos: 3,124,828 for
    # 880,500 samples). ADPCM tracks are stamped correctly, but every track goes the same way.
    tmp = os.path.join(state_dir, 'tmp-' + name)
    os.makedirs(tmp, exist_ok=True)
    tracks = []
    for i, s in enumerate(a):
        wav = os.path.join(tmp, 'a%d.wav' % i)
        n, h = pcm_stats(src, i, warnings, wav)
        write_wav(wav + '.pcm', wav, int(s['sample_rate']), 2)
        tracks.append({'index': i, 'codec': s['codec_name'], 'rate': int(s['sample_rate']),
                       'channels': s['channels'], 'samples_in': n, 'pcm_sha1': h, 'wav': wav})
    for t in tracks:
        t['lang'] = TRACK_LANGS[t['index']] if t['index'] < len(TRACK_LANGS) else None
        t['same_as_int'] = t['index'] > 0 and t['pcm_sha1'] == tracks[0]['pcm_sha1']

    common = ['-map_metadata', '-1', '-fflags', '+bitexact', '-flags', '+bitexact']
    outputs = []
    ogv = os.path.join(dst, name + '.ogv')
    cmd = ['ffmpeg', '-v', 'warning', '-nostats', '-y', '-i', src]
    if tracks:
        cmd += ['-i', tracks[0]['wav']]
    cmd += ['-map', '0:v:0']
    if tracks:
        aq = args.pcm_audio_quality if tracks[0]['codec'].startswith('pcm') else args.audio_quality
        cmd += ['-map', '1:a:0', '-c:a', 'libvorbis', '-q:a', str(aq)]
    cmd += ['-vf', 'fps=%d' % fps, '-pix_fmt', 'yuv420p', '-c:v', 'libtheora', '-q:v', str(args.video_quality),
            '-g', str(args.keyint)] + common + [ogv]
    rc, _, err = run(cmd)
    warnings.extend(('encode video', t, e) for t, e in classify(err))
    if rc:
        raise RuntimeError('%s: encode failed (%d): %s' % (name, rc, err[-3:]))
    outputs.append({'file': os.path.join(rel, name + '.ogv'), 'lang': 'int' if tracks else None,
                    'track': 0 if tracks else None})
    for t in tracks[1:]:
        if t['same_as_int'] or t['lang'] is None:
            continue
        oga = os.path.join(dst, '%s_%s.oga' % (name, t['lang']))
        aq = args.pcm_audio_quality if t['codec'].startswith('pcm') else args.audio_quality
        rc, _, err = run(['ffmpeg', '-v', 'warning', '-nostats', '-y', '-i', t['wav'], '-map', '0:a:0',
                          '-c:a', 'libvorbis', '-q:a', str(aq)] + common + [oga])
        warnings.extend(('encode a:%d' % t['index'], tt, e) for tt, e in classify(err))
        if rc:
            raise RuntimeError('%s: encode of track %d failed' % (name, t['index']))
        outputs.append({'file': os.path.join(rel, '%s_%s.oga' % (name, t['lang'])), 'lang': t['lang'],
                        'track': t['index']})

    # Verify what was written.
    for o in outputs:
        path = os.path.join(outdir, o['file'])
        o['bytes'] = os.path.getsize(path)
        o['sha1'] = sha1_file(path)
        summ = ogg_summary(path)
        if o['file'].endswith('.ogv'):
            info = count_frames(path, warnings, 'count frames')
            o['frames_out'] = int(info['nb_read_packets'])
            o['frames_nonempty'] = int(info['nb_read_frames'])
            o['frames_granule'] = summ.get('theora_frames')
            o['granule_shift'] = summ.get('theora_shift')
            o['size'] = '%dx%d' % (info['width'], info['height'])
            o['pix_fmt'] = info['pix_fmt']
            o['fps'] = info['r_frame_rate']
        if o['track'] is not None:
            vs = summ.get('vorbis_samples', [])
            o['samples_granule'] = vs[0] if vs else None
            # ffmpeg's Vorbis decoder does not cut the last packet at the end-of-stream granule position, so
            # this count may exceed the granule by up to one block; the granule is what a player honours.
            n, _ = pcm_stats(path, 0, warnings)
            o['samples_decoded_ffmpeg'] = n
            o['samples_in'] = tracks[o['track']]['samples_in']
    quality = measure(src, fps, ogv, warnings) if not args.no_measure else {}
    for t in tracks:
        os.remove(t.pop('wav'))
    os.rmdir(tmp)

    res = {
        'name': name, 'kind': kind, 'source': os.path.relpath(src, args.epic), 'source_bytes': os.path.getsize(src),
        'source_sha1': src_sha1, 'settings': settings,
        'video': {'codec': v['codec_name'], 'size': '%dx%d' % (v['width'], v['height']), 'pix_fmt': v['pix_fmt'],
                  'fps': fps, 'packets_in': len(pts), 'last_pts': pts[-1], 'slots': slots,
                  'duration': slots / float(fps), 'held_frames': gaps,
                  'pow2': all(x & (x - 1) == 0 for x in (v['width'], v['height']))},
        'audio': tracks, 'outputs': outputs, 'quality': quality,
        'warnings': [{'step': s, 'text': t, 'known': e} for s, t, e in warnings],
    }
    ov = outputs[0]
    res['checks'] = {
        'frames': ov['frames_out'] == slots and ov['frames_granule'] == slots,
        'samples': all(o.get('samples_granule') == o.get('samples_in') for o in outputs if o['track'] is not None),
        'av_diff_s': (round(tracks[0]['samples_in'] / float(tracks[0]['rate']) - slots / float(fps), 4)
                      if tracks else None),
    }
    json.dump(res, open(state_path + '.tmp', 'w'), indent=1)
    os.replace(state_path + '.tmp', state_path)
    log('%-28s %s %2d fps %5d packets -> %5d frames (granule %s) %d audio track(s), %d language file(s), '
        '%s, PSNR-Y %s SSIM %s' % (name, res['video']['size'], fps, len(pts), ov['frames_out'], ov['frames_granule'],
                                   len(tracks), len(outputs) - 1, 'OK' if res['checks']['frames'] and
                                   res['checks']['samples'] else 'MISMATCH', quality.get('psnr_y'),
                                   quality.get('ssim_all')))
    return res


def report(results, outdir, args):
    results = sorted(results, key=lambda r: (r['kind'], r['name']))
    manifest = {'format': FORMAT_VERSION, 'tool': 'tools/cook-movies.py',
                'settings': results[0]['settings'] if results else None,
                'track_languages': list(TRACK_LANGS), 'movies': results}
    path = os.path.join(outdir, 'Movies', 'movie-manifest.json')
    json.dump(manifest, open(path + '.tmp', 'w'), indent=1)
    os.replace(path + '.tmp', path)

    L = []
    tin = sum(r['source_bytes'] for r in results)
    tout = sum(o['bytes'] for r in results for o in r['outputs'])
    nout = sum(len(r['outputs']) for r in results)
    L.append('cook-movies.py: %d XMV in (%.1f MB) -> %d files out (%.1f MB, %.1f%% of the XMV bytes)'
             % (len(results), tin / 1e6, nout, tout / 1e6, 100.0 * tout / tin if tin else 0))
    for kind in ('movie', 'texture'):
        rs = [r for r in results if r['kind'] == kind]
        if rs:
            a = sum(r['source_bytes'] for r in rs)
            b = sum(o['bytes'] for r in rs for o in r['outputs'])
            L.append('  %-8s %3d in %8.1f MB -> %8.1f MB' % (kind, len(rs), a / 1e6, b / 1e6))
    bad = [r['name'] for r in results if not (r['checks']['frames'] and r['checks']['samples'])]
    L.append('frame/sample accounting: %d of %d exact%s' % (len(results) - len(bad), len(results),
                                                             (' MISMATCH: ' + ', '.join(bad)) if bad else ''))
    ps = [float(r['quality']['psnr_y']) for r in results if r['quality'].get('psnr_y') not in (None, 'inf')]
    ss = [float(r['quality']['ssim_all']) for r in results if r['quality'].get('ssim_all')]
    if ps:
        L.append('quality vs decoded XMV: PSNR-Y min %.2f median %.2f max %.2f dB; SSIM(all) min %.4f median %.4f'
                 % (min(ps), sorted(ps)[len(ps) // 2], max(ps), min(ss), sorted(ss)[len(ss) // 2]))
    L.append('')
    L.append('%-28s %-8s %3s %6s %6s %6s %5s %-14s %9s %9s %8s %7s %7s %s' % (
        'name', 'size', 'fps', 'pkts', 'slots', 'out', 'held', 'audio', 'samp_in', 'samp_out', 'A-V s',
        'PSNR-Y', 'SSIM', 'files'))
    for r in results:
        v, o = r['video'], r['outputs'][0]
        t = r['audio']
        langs = ','.join(x['lang'] for x in t if x['index'] == 0 or not x['same_as_int'])
        L.append('%-28s %-8s %3d %6d %6d %6d %5d %-14s %9s %9s %8s %7s %7s %s' % (
            r['name'], v['size'], v['fps'], v['packets_in'], v['slots'], o['frames_out'],
            len(v['held_frames']), ('%dx%s %s' % (len(t), t[0]['codec'].replace('adpcm_ima_xbox', 'adpcm')
                                                  .replace('pcm_s16le', 'pcm'), langs)) if t else '-',
            t[0]['samples_in'] if t else '-', o.get('samples_granule', '-') if t else '-',
            r['checks']['av_diff_s'] if t else '-', r['quality'].get('psnr_y', '-')[:6],
            r['quality'].get('ssim_all', '-')[:6], len(r['outputs'])))
    L.append('')
    L.append('language tracks: a track identical (PCM SHA-1) to the international one gets no file of its own')
    for r in results:
        same = [x['lang'] for x in r['audio'] if x['same_as_int']]
        if same:
            L.append('  %s: %s identical to int' % (r['name'], ','.join(same)))
    L.append('')
    L.append('held frames (XMV timestamp gaps longer than 1.5 frame; the cooked stream repeats the picture):')
    for r in results:
        if r['video']['held_frames']:
            L.append('  %s: %s' % (r['name'], ', '.join('%.3fs+%dms' % tuple(g) for g in r['video']['held_frames'])))
    L.append('')
    L.append('ffmpeg warnings (every line; "known:" gives the explanation of a recognised one):')
    counts = {}
    for r in results:
        for w in r['warnings']:
            key = (w['text'], w['known'])
            counts.setdefault(key, []).append('%s/%s' % (r['name'], w['step']))
    for (text, known), where in sorted(counts.items(), key=lambda x: -len(x[1])):
        L.append('  %4dx %s%s' % (len(where), text, ('   [known: %s]' % known) if known else ''))
        if not known:
            L.append('        in: %s' % ', '.join(where[:20]))
    open(os.path.join(outdir, 'Movies', 'cook-report.txt'), 'w').write('\n'.join(L) + '\n')
    print('\n'.join(L[:6]))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--epic', default=os.path.join(work, 'epic-sources'))
    ap.add_argument('--out', default=os.path.join(work, 'assets', 'uc2-cooked'))
    ap.add_argument('--jobs', type=int, default=4)
    ap.add_argument('--only', default='')
    ap.add_argument('--video-quality', type=int, default=7)
    ap.add_argument('--keyint', type=int, default=150)
    ap.add_argument('--audio-quality', type=int, default=4)
    ap.add_argument('--pcm-audio-quality', type=int, default=5)
    ap.add_argument('--no-measure', action='store_true')
    ap.add_argument('--force', action='store_true')
    args = ap.parse_args()
    args.jobs = max(1, min(args.jobs, 4))

    srcs = []
    for kind, sub in (('movie', 'Movies'), ('texture', os.path.join('Textures', 'Movies'))):
        d = os.path.join(args.epic, sub)
        for f in sorted(os.listdir(d)):
            if f.lower().endswith('.xmv'):
                srcs.append((os.path.join(d, f), kind))
    only = set(x for x in args.only.split(',') if x)
    if only:
        srcs = [s for s in srcs if os.path.splitext(os.path.basename(s[0]))[0] in only]
    # Longest first, so the four workers finish together.
    srcs.sort(key=lambda s: -os.path.getsize(s[0]))
    results, failed = [], []
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as ex:
        futs = {ex.submit(cook_one, s, k, args.out, args): s for s, k in srcs}
        for f in concurrent.futures.as_completed(futs):
            try:
                results.append(f.result())
            except Exception as e:
                failed.append((futs[f][0], str(e)))
                log('FAILED %s: %s' % (futs[f][0], e))
    if not only:
        report(results, args.out, args)
    else:
        # A partial cook refreshes the manifest from every per-file result on disk.
        state_dir = os.path.join(args.out, 'Movies', '.cook')
        allres = [json.load(open(os.path.join(state_dir, f))) for f in sorted(os.listdir(state_dir))
                  if f.endswith('.json')]
        report(allres, args.out, args)
    if failed:
        print('%d failed' % len(failed))
        sys.exit(1)


if __name__ == '__main__':
    main()
