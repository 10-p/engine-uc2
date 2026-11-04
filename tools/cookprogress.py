#!/usr/bin/env python3
"""cookprogress.py: the cook's progress lines. Each step's tool prints them, tools/cook.py shows them on the console
(without the prefix) and keeps them in the step's log.

    import cookprogress
    cookprogress.line('part 1 of 2: the maps')
    items = cookprogress.Count('package', 412)
    items.start('XboxMaps/DM-Azure.LIN')            # progress: processing package 1 of 412: XboxMaps/DM-Azure.LIN
    items.end('Maps/DM-Azure.unr', 2.0)             # progress: processed package 1 of 412: Maps/DM-Azure.unr (2.0 s)

A line is written whole with one write, so the lines of parallel workers (threads or processes sharing the output)
do not mix.
"""
import os
import sys
import threading
import time

PREFIX = 'progress: '
_lock = threading.Lock()


def line(msg):
    """One progress line."""
    text = PREFIX + msg + '\n'
    with _lock:
        sys.stdout.write(text)
        sys.stdout.flush()


def duration(seconds):
    """'0.4 s', '42 s', '4 min 02 s', '1 h 03 min'."""
    if seconds < 10:
        return '%.1f s' % seconds
    s = int(round(seconds))
    if s < 60:
        return '%d s' % s
    if s < 3600:
        return '%d min %02d s' % (s // 60, s % 60)
    return '%d h %02d min' % (s // 3600, s % 3600 // 60)


def rel(path, root):
    """path below root with '/' separators, for the progress lines."""
    try:
        r = os.path.relpath(path, root)
    except ValueError:
        return path
    return path if r.startswith('..') else r.replace(os.sep, '/')


class Count:
    """Progress of a known number of items of one kind. start() numbers the items in the order they start, end() in
    the order they finish, so both counts only go up while workers finish items out of order."""

    def __init__(self, what, total):
        self.what, self.total = what, total
        self.started = self.ended = 0
        self.t0 = time.time()
        self._lock = threading.Lock()

    def start(self, name, number=None):
        """'processing <what> N of T: name'; number: the item's own number (a worker process that has no Count of
        its own)."""
        with self._lock:
            self.started += 1
            n = number or self.started
        line('processing %s %d of %d: %s' % (self.what, n, self.total, name))

    def end(self, name, seconds=None, note=''):
        """'processed <what> N of T: name (note, 2.0 s)'."""
        with self._lock:
            self.ended += 1
            n = self.ended
        extra = [x for x in (note, duration(seconds) if seconds is not None else '') if x]
        line('processed %s %d of %d: %s%s' % (self.what, n, self.total, name,
                                              ' (%s)' % ', '.join(extra) if extra else ''))

    def done(self):
        """'<what>s: N of T in 4 min 02 s': the items finished, or started where no item reports its end."""
        line('%ss: %d of %d in %s' % (self.what, self.ended or self.started, self.total,
                                      duration(time.time() - self.t0)))
