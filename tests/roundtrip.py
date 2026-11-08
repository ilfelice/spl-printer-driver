#!/usr/bin/env python3
"""Round trip: encode pages, decode the job again, compare with the input.

Independent of SpliX: this checks that what pbm2qpdl writes is a
self-consistent QPDL job (structure, checksums, compression) and that the
image the printer would reconstruct is the page we fed in, shifted by the
printer's hardware margins.

Usage: roundtrip.py [WORKDIR]
"""
import os
import subprocess
import sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
PBM2QPDL = os.path.join(ROOT, 'pbm2qpdl')
QPDL2PBM = os.path.join(ROOT, 'qpdl2pbm')
GEN_PAGES = os.path.join(HERE, 'gen_pages.py')

MARGIN_X, MARGIN_Y = 10.75, 15.0        # Phaser 3140 hardware margins, points


def ceil_div(a, b):
    return -(-a // b)


def read_pbm(path):
    data = open(path, 'rb').read()
    magic, size, body = data.split(b'\n', 2)
    assert magic == b'P4', path
    w, h = (int(v) for v in size.split())
    bpl = (w + 7) // 8
    return np.frombuffer(body, np.uint8, bpl * h).reshape(h, bpl)


def check(work, paper, w_pt, h_pt, xres, yres, pages):
    w = int(w_pt * xres / 72.0 + 0.5)
    h = int(h_pt * yres / 72.0 + 0.5)
    xscale = 2 if xres == 2 * yres else 1
    directory = os.path.join(work, 'rt-%dx%d' % (w, h))
    os.makedirs(directory, exist_ok=True)
    subprocess.check_call([sys.executable, GEN_PAGES, str(w), str(h), directory,
                           str(xscale)] + pages)
    files = [os.path.join(directory, p + '.pbm') for p in pages]
    job = os.path.join(directory, 'job.prn')
    subprocess.check_call([PBM2QPDL, '--paper', paper, '--resolution',
                           '%dx%d' % (xres, yres), '--raster-margins', '0,0',
                           '--output', job] + files)
    subprocess.check_call([QPDL2PBM, job, os.path.join(directory, 'decoded')],
                          stdout=subprocess.DEVNULL)

    page_bytes = (ceil_div(w_pt * xres, 72) + 7) // 8
    page_rows = ceil_div(h_pt * yres, 72)
    margin_bytes = (ceil_div(int(MARGIN_X * 1000) * xres, 72000) + 7) // 8
    margin_rows = ceil_div(int(MARGIN_Y * 1000) * yres, 72000)

    for number, (name, path) in enumerate(zip(pages, files), 1):
        source = read_pbm(path)
        # the page bitmap as the encoder builds it (raster centred, clipped)
        plane = np.zeros((page_rows, page_bytes), np.uint8)
        rows = min(page_rows, source.shape[0])
        cols = min(page_bytes, source.shape[1])
        r0 = (page_rows - rows) // 2
        c0 = (page_bytes - cols) // 2
        sr = (source.shape[0] - rows) // 2
        sc = (source.shape[1] - cols) // 2
        plane[r0:r0 + rows, c0:c0 + cols] = source[sr:sr + rows, sc:sc + cols]

        # what the printer is sent: origin moved to the printable corner
        expected = np.zeros((page_rows - margin_rows, page_bytes), np.uint8)
        expected[:, :page_bytes - margin_bytes] = plane[margin_rows:, margin_bytes:]

        decoded = read_pbm(os.path.join(directory, 'decoded-%d.pbm' % number))
        if not expected.any():
            ok = not decoded.any()
        else:
            got = decoded.shape[0]
            if got > expected.shape[0]:      # last band is padded to 128 lines
                expected = np.pad(expected, ((0, got - expected.shape[0]), (0, 0)))
            ok = (decoded.shape[1] == page_bytes
                  and np.array_equal(decoded, expected[:got])
                  and not expected[got:].any())
        print('%s  %-7s %4dx%-4d %-9s' % ('ok  ' if ok else 'FAIL', paper, xres, yres, name))
        if not ok:
            return False
    return True


def main():
    work = sys.argv[1] if len(sys.argv) > 1 else '/tmp/spl-tests'
    os.makedirs(work, exist_ok=True)
    ok = check(work, 'Letter', 612, 792, 600, 600,
               ['text', 'graphics', 'edges', 'fine', 'noise', 'sparse', 'black', 'blank', 'fsdither'])
    ok = check(work, 'A4', 595, 842, 1200, 600, ['text', 'edges', 'fsdither', 'fine']) and ok
    ok = check(work, 'A5', 420, 595, 600, 600, ['text', 'edges', 'black']) and ok
    print('round trip %s' % ('passed' if ok else 'FAILED'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
