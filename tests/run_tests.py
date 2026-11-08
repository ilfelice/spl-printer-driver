#!/usr/bin/env python3
"""Compare this project's encoder with SpliX, byte for byte.

For every test case the same 1-bit pages are sent through

    mkraster | SpliX rastertoqpdl (with the model's PPD)      -> reference
    pbm2qpdl                                                  -> ours

and the two print jobs must be identical.

Usage:
    run_tests.py --splix /path/to/rastertoqpdl --ppd-dir DIR [--quick]
                 [--work DIR] [--only SUBSTRING]

--ppd-dir holds the English PPDs compiled from SpliX's *.drv.in files.
"""
import argparse
import datetime
import itertools
import math
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
PBM2QPDL = os.path.join(ROOT, 'pbm2qpdl')
MKRASTER = os.path.join(HERE, 'mkraster')
GEN_PAGES = os.path.join(HERE, 'gen_pages.py')

PAPERS = {  # id: (width pt, height pt)
    'Letter': (612, 792), 'Legal': (612, 1008), 'A4': (595, 842),
    'Executive': (522, 756), 'Ledger': (1224, 792), 'A3': (842, 1191),
    'Env10': (297, 684), 'Monarch': (279, 540), 'C5': (459, 649),
    'DL': (312, 624), 'B4': (729, 1032), 'B5': (516, 729),
    'EnvISOB5': (499, 709), 'Postcard': (284, 419),
    'DoublePostcardRotated': (420, 567), 'A5': (420, 595), 'A6': (297, 420),
    'B6': (363, 516), 'C6': (323, 459), 'Folio': (595, 935),
    'EnvPersonal': (261, 468), 'Env9': (279, 639), 'Oficio': (612, 972),
}
ALL_PAGES = ['text', 'graphics', 'fsdither', 'edges', 'fine', 'noise',
             'sparse', 'blank', 'black']


class Case:
    def __init__(self, name, model, pages, paper='Letter', res=(600, 600),
                 margins=(10, 15), copies=1, raster='full', **options):
        self.name = name
        self.model = model
        self.pages = pages
        self.paper = paper
        self.res = res
        self.margins = margins
        self.copies = copies
        self.raster = raster      # full | printable | oversize
        self.options = options    # SpliX option names


def cups_options(case):
    opts = ['PageSize=%s' % case.paper]
    opts += ['%s=%s' % kv for kv in sorted(case.options.items())]
    return ' '.join(opts)


def our_arguments(case):
    args = ['--model', case.model, '--paper', case.paper,
            '--resolution', '%dx%d' % case.res,
            '--raster-margins', '%d,%d' % case.margins,
            '--copies', str(case.copies), '--splix-quirks']
    for key, value in case.options.items():
        if key == 'InputSlot':
            args += ['--source', value]
        elif key == 'MediaType':
            args += ['--media-type', value]
        elif key == 'EconoMode':
            args += ['--toner-save', {'ON': 'on', 'OFF': 'off', '0': 'default'}[value]]
        elif key == 'TonerDensity':
            args += ['--density', value]
        elif key == 'PowerSave':
            args += ['--power-save', 'off' if value == 'False' else value]
        elif key == 'JamRecovery':
            if value == 'True':
                args += ['--jam-recovery']
        elif key == 'Altitude':
            if value == 'HIGH':
                args += ['--high-altitude']
        elif key == 'SRTMode':
            args += ['--srt', value]
        elif key == 'Duplex':
            args += ['--duplex', {'DuplexNoTumble': 'long', 'DuplexTumble': 'short',
                                  'None': 'none'}[value]]
        else:
            raise ValueError(key)
    return args


def raster_size(case):
    """Pixel size of the raster handed to both encoders."""
    w_pt, h_pt = PAPERS[case.paper]
    xres, yres = case.res
    if case.raster == 'printable':     # only the area inside the margins
        w_pt -= 2 * case.margins[0]
        h_pt -= 2 * case.margins[1]
    w = int(w_pt * xres / 72.0 + 0.5)
    h = int(h_pt * yres / 72.0 + 0.5)
    if case.raster == 'oversize':
        w += 203
        h += 77
    return w, h


def page_files(work, case):
    w, h = raster_size(case)
    xscale = 2 if case.res[0] == 2 * case.res[1] else 1
    directory = os.path.join(work, 'pages-%dx%d-%d' % (w, h, xscale))
    os.makedirs(directory, exist_ok=True)
    missing = sorted(set(p for p in case.pages
                         if not os.path.exists(os.path.join(directory, p + '.pbm'))))
    if missing:
        subprocess.check_call([sys.executable, GEN_PAGES, str(w), str(h),
                               directory, str(xscale)] + missing)
    return [os.path.join(directory, p + '.pbm') for p in case.pages]


def run_case(case, args, today):
    files = page_files(args.work, case)
    w_pt, h_pt = PAPERS[case.paper]

    raster = os.path.join(args.work, 'job-%d.ras' % os.getpid())
    with open(raster, 'wb') as out:
        subprocess.check_call([MKRASTER, str(case.res[0]), str(case.res[1]),
                               str(w_pt), str(h_pt), str(case.margins[0]),
                               str(case.margins[1]), '17', str(case.copies)]
                              + files, stdout=out)

    env = dict(os.environ, PPD=os.path.join(args.ppd_dir, case.model + '.ppd'))
    ref = subprocess.run([args.splix, '1', 'tester', 'Test job', '1',
                          cups_options(case), raster], env=env,
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    os.unlink(raster)
    if ref.returncode != 0:
        return 'reference failed: ' + ref.stderr.decode('latin1')[-300:]

    ours = subprocess.run([PBM2QPDL, '--user', 'tester', '--title', 'Test job',
                           '--date', today] + our_arguments(case) + files,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if ours.returncode != 0:
        return 'pbm2qpdl failed: ' + ours.stderr.decode('latin1')[-300:]

    if ref.stdout != ours.stdout:
        a, b = ref.stdout, ours.stdout
        n = next((i for i in range(min(len(a), len(b))) if a[i] != b[i]),
                 min(len(a), len(b)))
        if args.keep:
            open(os.path.join(args.work, 'fail.ref.prn'), 'wb').write(a)
            open(os.path.join(args.work, 'fail.ours.prn'), 'wb').write(b)
        return 'output differs at byte %d (reference %d bytes, ours %d bytes)' % (
            n, len(a), len(b))
    case.size = len(ours.stdout)
    case.dropped = b'left out' in ours.stderr
    return None


def model_table():
    """id -> (resolutions, duplex capability, has SRT) from pbm2qpdl's view."""
    listing = subprocess.check_output([PBM2QPDL, '--list-models']).decode()
    return [line.split()[0] for line in listing.splitlines() if line.strip()]


def ppd_info(ppd_dir, model):
    """What SpliX's PPD says about a model, plus a signature of everything
    that influences the generated stream.  Models with equal signatures are
    encoded identically."""
    text = open(os.path.join(ppd_dir, model + '.ppd'), encoding='latin1').read()

    def attr(key):
        m = re.search(r'^\*' + re.escape(key) + r':\s*"?([^"\n]*)"?', text, re.M)
        return m.group(1) if m else None

    default = attr('DefaultResolution')
    res = []
    for m in re.finditer(r'^\*Resolution (\S+?)/[^:]*:\s*"<</HWResolution\[(\d+) (\d+)\]', text, re.M):
        entry = (int(m.group(2)), int(m.group(3)))
        if m.group(1) == default:
            res.insert(0, entry)
        else:
            res.append(entry)
    duplex = re.search(r'^\*OpenUI \*Duplex', text, re.M) is not None
    srt = re.search(r'^\*OpenUI \*SRTMode', text, re.M) is not None
    slots = re.findall(r'^\*InputSlot (\S+?)/', text, re.M)
    area = re.search(r'^\*ImageableArea \S+?/[^:]*:\s*"([\d.]+) ([\d.]+)', text, re.M)
    signature = (attr('QPDL QPDLVersion'), attr('General DocHeaderValues'),
                 attr('QPDL FixedBandWidth'), attr('QPDL SpecialBandWidth'),
                 attr('QPDL ManualDuplex'), attr('ReverseDuplex'), duplex, srt,
                 tuple(res), tuple(slots), area.group(1), area.group(2))
    return res, duplex, srt, slots, signature


def build_cases(args):
    cases = []
    m = 'ph3140'

    # --- Phaser 3140 in depth -------------------------------------------
    for page in ALL_PAGES:
        cases.append(Case('3140 single %s' % page, m, [page]))
    cases.append(Case('3140 all pages A4', m, ALL_PAGES, paper='A4'))
    cases.append(Case('3140 all pages Letter 1200x600', m, ALL_PAGES, res=(1200, 600)))
    cases.append(Case('3140 all pages A4 1200x600', m, ALL_PAGES, paper='A4', res=(1200, 600)))
    for paper in PAPERS:
        cases.append(Case('3140 paper %s' % paper, m, ['text', 'edges', 'graphics'], paper=paper))
        if not args.quick:
            cases.append(Case('3140 paper %s 1200x600' % paper, m, ['edges', 'fine'],
                              paper=paper, res=(1200, 600)))
    option_sets = [
        dict(EconoMode='ON'), dict(EconoMode='OFF'), dict(TonerDensity='1'),
        dict(TonerDensity='5'), dict(PowerSave='False'), dict(PowerSave='30'),
        dict(PowerSave='60'), dict(JamRecovery='True'), dict(Altitude='HIGH'),
        dict(MediaType='THICK'), dict(MediaType='ENV'), dict(MediaType='RECYCLED'),
        dict(InputSlot='Manual'),
        dict(EconoMode='ON', TonerDensity='5', PowerSave='15', JamRecovery='True',
             Altitude='HIGH', MediaType='CARD', InputSlot='Manual'),
    ]
    for opts in option_sets:
        label = ' '.join('%s=%s' % kv for kv in sorted(opts.items()))
        cases.append(Case('3140 options %s' % label, m, ['text'], paper='A4', **opts))
    cases.append(Case('3140 copies 3', m, ['text', 'graphics'], copies=3))
    cases.append(Case('3140 copies 300', m, ['text'], copies=300))
    for count in range(1, 8):
        pages = list(itertools.islice(itertools.cycle(['text', 'graphics', 'edges', 'fine', 'sparse']), count))
        for mode in ('DuplexNoTumble', 'DuplexTumble'):
            cases.append(Case('3140 manual duplex %s %d pages' % (mode, count), m, pages,
                              paper='A4', Duplex=mode))
    cases.append(Case('3140 manual duplex C5 (odd byte count)', m, ['text', 'edges', 'fine'],
                      paper='C5', Duplex='DuplexNoTumble'))
    cases.append(Case('3140 manual duplex Letter 1200x600', m, ['text', 'edges', 'fine', 'graphics'],
                      res=(1200, 600), Duplex='DuplexNoTumble'))
    for margins in ((0, 0), (10, 15), (11, 15), (18, 36)):
        for raster in ('full', 'printable', 'oversize'):
            cases.append(Case('3140 raster %s margins %d,%d' % ((raster,) + margins), m,
                              ['edges', 'text'], paper='A4', margins=margins, raster=raster))

    # Noise right up to the edges: on models with fixed-width bands this is
    # not compressible, and both encoders must drop the same bands.
    for model in ('ml1510', 'ml2250', 'ph3117'):
        cases.append(Case('%s incompressible noise' % model, model, ['noise', 'text'],
                          margins=(0, 0)))

    # --- every model: one job that pins down all its parameters ---------
    seen = {}
    for model in model_table():
        resolutions, duplex, srt, slots, signature = ppd_info(args.ppd_dir, model)
        opts = {'Duplex': 'DuplexNoTumble'} if duplex else {}
        cases.append(Case('%s basic' % model, model, ['text', 'edges', 'fine'],
                          paper='A4', res=resolutions[0], **opts))
        seen.setdefault(signature, model)

    # --- one model per distinct parameter set: the wider matrix ----------
    for signature, model in seen.items():
        if model == m:
            continue                 # the Phaser 3140 is covered above
        resolutions, duplex, srt, slots, _ = ppd_info(args.ppd_dir, model)
        for index, res in enumerate(resolutions):
            big = res[0] * res[1] > 1200 * 600
            for paper in (('Letter', 'A4') if index == 0 else ('Letter',)):
                cases.append(Case('%s %s %dx%d' % (model, paper, res[0], res[1]), model,
                                  ['text', 'edges'] if big else ['text', 'graphics', 'edges'],
                                  paper=paper, res=res))
        extra = {}
        if srt:
            extra['SRTMode'] = 'ENHANCE'
        if len(slots) > 2:
            extra['InputSlot'] = slots[-1]
        if duplex:
            for mode in ('DuplexNoTumble', 'DuplexTumble'):
                cases.append(Case('%s duplex %s' % (model, mode), model,
                                  ['text', 'edges', 'fine', 'graphics', 'sparse'],
                                  Duplex=mode, **extra))
        elif extra:
            cases.append(Case('%s options' % model, model, ['text'], paper='A4', **extra))
        # small, large and unlisted sizes exercise the band width special cases
        for paper in ('A5', 'Env10', 'DoublePostcardRotated', 'Legal', 'A3'):
            cases.append(Case('%s %s' % (model, paper), model, ['edges', 'text'],
                              paper=paper, res=resolutions[0]))
    args.signatures = len(seen)
    args.models = len(model_table())
    return cases


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--splix', required=True)
    parser.add_argument('--ppd-dir', required=True)
    parser.add_argument('--work', default='/tmp/spl-tests')
    parser.add_argument('--quick', action='store_true')
    parser.add_argument('--keep', action='store_true', help='keep the first failing pair')
    parser.add_argument('--only')
    args = parser.parse_args()
    os.makedirs(args.work, exist_ok=True)

    cases = build_cases(args)
    if args.only:
        cases = [c for c in cases if args.only in c.name]

    failures = []
    total_bytes = 0
    dropped = 0
    for index, case in enumerate(cases):
        today = datetime.date.today().strftime('%Y%m%d')
        error = run_case(case, args, today)
        if error and datetime.date.today().strftime('%Y%m%d') != today:
            error = run_case(case, args, datetime.date.today().strftime('%Y%m%d'))
        if error:
            failures.append((case, error))
            print('FAIL  %s: %s' % (case.name, error))
            if args.keep:
                break
        else:
            total_bytes += case.size
            dropped += 1 if case.dropped else 0
            print('ok    %-58s %9d bytes%s' % (case.name, case.size,
                  '  (bands dropped by both)' if case.dropped else ''))
        sys.stdout.flush()

    print()
    if not args.only:
        print('%d models, %d distinct parameter sets' % (args.models, args.signatures))
    print('%d of %d jobs identical to SpliX (%d bytes compared); %d failed'
          % (len(cases) - len(failures), len(cases), total_bytes, len(failures)))
    if dropped:
        print('%d jobs contained incompressible bands that both encoders left out'
              % dropped)
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
