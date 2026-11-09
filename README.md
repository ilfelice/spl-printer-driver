# SPL/QPDL printer driver for Haiku

A native Haiku driver for "SPL" laser printers (Samsung Printer Language,
also called QPDL): the inexpensive Samsung, Xerox, Dell and HP models that
understand neither PostScript nor PCL. It is modelled on
[SpliX](https://github.com/OpenPrinting/splix), the CUPS driver for the same
printers, and aims to be on par with it. The first target is the
**Xerox Phaser 3140**.

## Status

| Step | What | State |
|------|------|-------|
| 1 | Portable encoder core, model table, test tools | Done. Output is byte-for-byte identical to SpliX in all 375 test jobs. |
| 2 | Haiku printer add-on (libprint glue, model list, settings) | Builds and runs on Haiku. A job it produced was verified to be byte for byte what SpliX sends for the same page image. |
| 3 | Printing on real hardware | Not done. `testpages-A4-ph3140.prn` is ready to be sent to a printer; see `docs/BUILDING-ON-HAIKU.md`. |

**No printer has printed a page produced by this code yet.** What has been
shown is that, given the same page image and settings, the driver sends the
printer exactly the bytes SpliX sends, both for the encoder's test pages
and for a real page printed on Haiku. That is why every model in the table
is still flagged "untested".

## What is in the package

    haiku/SPLEntry.cpp             Haiku add-on: entry point
    haiku/SPLCap.cpp, .h           Haiku add-on: model list, paper sizes, trays, options
    haiku/SPLDriver.cpp, .h        Haiku add-on: turns libprint's page bands into a job
    haiku/SPL.rdef, haiku/Jamfile  resources; build file for use inside a Haiku tree
    haiku/SaveToDesktop.cpp        test connection type: writes each job to a Desktop file
    haiku/install-into-haiku-tree.sh
    packaging/PackageInfo          description of the .hpkg package (version and
                                   architecture are filled in by Makefile.haiku)
    Makefile.haiku                 builds the add-on on a stock Haiku installation
    libprint/                      unmodified copy of Haiku's libprint (MIT licence)
    docs/BUILDING-ON-HAIKU.md      how to build, install and test on Haiku
    testpages-A4-ph3140.prn        three A4 test pages, ready to send to a Phaser 3140
    spl.h                          public API of the encoder core
    spl_encoder.cpp                page geometry, bands, QPDL framing, PJL job header,
                                   manual-duplex page ordering
    spl_algo0x11.cpp               compression algorithm 0x11
    spl_models.cpp                 paper sizes, paper sources, option tables, lookups
    spl_models_generated.inc       model table, generated from SpliX's definitions
    tools/pbm2qpdl.cpp             command line encoder: PBM pages -> print job
    tools/qpdl2pbm.cpp             decoder: print job -> PBM pages (with checks)
    tools/gen_models.py            regenerates the model table from SpliX's PPDs
    tests/run_tests.py             byte-for-byte comparison against SpliX
    tests/roundtrip.py             encode -> decode -> compare, independent of SpliX
    tests/gen_pages.py             test page generator
    tests/mkraster.c               wraps PBM pages as a CUPS raster for SpliX
    docs/test-results.txt          log of the comparison run described below

The core (the `spl_*` files) is plain C++98 without STL, exceptions or OS-specific calls.
It compiles without warnings with
`g++ -std=c++98 -pedantic -Wall -Wextra -Wshadow -Wconversion`.

## Installing on Haiku

`docs/BUILDING-ON-HAIKU.md` describes three ways: building with `make` on a
stock Haiku installation, building inside a Haiku source tree, and installing
a ready-built `SPLprinterDriver-<version>-<arch>.hpkg` package with `pkgman`.
`make -f Makefile.haiku package` builds that package.

## Building and using the tools (Linux or Haiku)

    make
    ./pbm2qpdl --list-models
    ./pbm2qpdl --model ph3140 --paper A4 page1.pbm page2.pbm > job.prn
    ./qpdl2pbm job.prn decoded        # writes decoded-1.pbm, decoded-2.pbm, ...
    ./qpdl2pbm -b job.prn decoded     # the same as BMP files, for any image viewer

`pbm2qpdl --help` lists the options (resolution, paper source and type, toner
save, density, power save, duplex, copies, ...).

`qpdl2pbm` reads any job this driver (or SpliX, for the same models) produces,
verifies its structure and checksums and writes the pages back as images. Once
the Haiku add-on exists, this is how its output can be inspected without a
printer: print to a file with Haiku's "Print to file" transport, then decode it.

## Supported models

73 monochrome models: every model SpliX defines that uses a single black plane
and compression algorithm 0x11, which is the code path of the Phaser 3140.
`./pbm2qpdl --list-models` prints them. They fall into 20 distinct parameter
sets (protocol version, band width rules, margins, duplex capability, ...).

Not supported yet (listed at the end of `spl_models_generated.inc`):

- 14 monochrome models using algorithms 0x0D/0x0E (Samsung SCX-4x00, Dell 1100, ...)
- 3 monochrome models using JBIG, algorithm 0x15 (Samsung ML-1670, ML-1860, ML-1865)
- 14 colour models

## How the output was verified

Reference: SpliX at commit 7a80218 (2026-08-19, version 2.0.0), built from
source, with PPD files freshly compiled from its `*.drv.in` definitions.
Environment: Ubuntu 24.04, x86_64, gcc 13.3, glibc 2.39.

For every test job the same 1-bit page images go through both drivers:

    mkraster | SpliX rastertoqpdl (model's PPD + options)   -> reference job
    pbm2qpdl (same model + options)                         -> our job

and the two jobs must be identical byte for byte.

Results (full log in `docs/test-results.txt`):

- **375 of 375 jobs identical, 259,633,815 bytes compared, no mismatch.**
  The run was repeated, with the same result, after the additions made for
  the Haiku add-on (stable model numbers, cancelling a job).
- 102 jobs for the Phaser 3140: nine kinds of page content (text,
  dithered graphics, error-diffused images, hairline patterns, noise, sparse
  dots, edge markers, blank, solid black); all 23 paper sizes at 600 and
  1200x600 dpi; toner save, density, power save, jam recovery, altitude, paper
  type and paper source options; copies; manual duplex on both edges with 1 to
  7 pages; input rasters that are smaller or larger than the page, with
  different margins.
- 73 jobs, one for each of the 73 models, that pin down every model's
  parameters; plus a wider matrix (all resolutions, duplex modes, small, large
  and unlisted paper sizes) for one model of each of the other 19 parameter sets.
- 3 jobs with content that cannot be compressed on models with fixed-width
  bands: both drivers leave out the same bands (see "Known limitations").

Independently of SpliX, `tests/roundtrip.py` encodes 16 pages, decodes the jobs
with `qpdl2pbm` and compares the images with the input: all pass. A build with
AddressSanitizer and UndefinedBehaviorSanitizer ran 10 varied jobs cleanly.

To repeat the comparison:

    git clone https://github.com/OpenPrinting/splix
    # build SpliX's rastertoqpdl (needs the CUPS and jbigkit development files)
    cd splix/ppd
    for d in samsung dell xerox lexmark toshiba hp; do
        ./compile.sh $d.drv.in drv && ppdc -d /tmp/splix-ppd $d.drv
    done
    cd /path/to/this/package
    make && make tests/mkraster
    python3 tests/run_tests.py --splix /path/to/rastertoqpdl --ppd-dir /tmp/splix-ppd
    python3 tests/roundtrip.py

The test scripts need Python 3 with NumPy and Pillow.

## Deliberate differences from SpliX

None of these change the output in the comparison above.

1. The distance table of algorithm 0x11 is chosen with an explicit, stable
   selection. SpliX uses `qsort()`, whose order for equal keys depends on the
   C library; the explicit version gives the glibc result on every platform.
2. Multi-byte values in the compressed data are always written little-endian
   (SpliX writes them in host byte order; the stream announces which one is used).
3. Page geometry is computed with integers instead of floating point.
4. User name and job title are cut to 127 bytes, and quotes and control
   characters in them are replaced, so that they cannot break the PJL header.
5. The 180 degree rotation used for manual long-edge duplex also mirrors the
   centre byte of pages with an odd byte count (SpliX leaves it unmirrored;
   this affects 8 pixels on a few envelope sizes). `--splix-quirks` switches
   back to SpliX's behaviour, which the comparison uses.

## Known limitations

- A band whose compressed form is not smaller than its raw form is left out,
  as in SpliX, and that part of the page prints blank. `Job::DroppedBands()`
  reports it. On models with normal band widths, the Phaser 3140 included, the
  blank margin columns in every band make this impossible in practice (a page
  of pure noise encodes completely); it can happen on the older models with
  fixed-width bands.
- Only SpliX's 23 paper sizes; no custom sizes.
- Manual duplex keeps the compressed pages in memory until the job ends.
- Error-diffused images compress poorly with algorithm 0x11 (several megabytes
  per page). The Haiku add-on should default to an ordered dither.

## The Haiku add-on

The add-on is a normal Haiku printer driver built on libprint, like Haiku's
own PCL and PostScript drivers, so it uses the standard Page setup and Print
dialogs.

- **Model list.** When a printer is added, the 73 models are offered in
  libprint's "protocol class" list, each with its resolutions, duplex
  capability and test status. The choice is stored with the printer.
- **Capabilities.** Paper sizes, paper sources, resolutions and duplex come
  from the model table. Paper type, toner save, toner density, power save,
  jam recovery, altitude, resolution enhancement and duplex binding appear as
  driver settings in the Print dialog.
- **Pages.** libprint renders each page in bands. The driver dithers them to
  one bit per pixel with libprint's halftoning, places them on a bitmap of the
  whole sheet and hands that to the encoder core, which writes the job to
  the transport (USB, network, or a file).
- **Copies and duplex.** Uncollated copies use the printer's own copy count;
  collated copies are sent page by page by libprint. For models without a
  duplex unit, "Duplex (manual)" sorts the pages the way SpliX does.

How it was checked: the add-on was built on Haiku with `Makefile.haiku`,
installed, and used to print an A4 page of an HTML e-mail (Japanese and
Latin text, images, grey backgrounds) to a job file with the "Save To
Desktop" connection. `qpdl2pbm` decoded the job without errors into a
correct 4960 x 6784 pixel page image. The page image was then put back into
a full-sheet bitmap and encoded by both `pbm2qpdl` and SpliX's
`rastertoqpdl` (commit 7a80218, PPD `ph3140`): all three jobs were identical,
395,961 bytes each.

## Licence

GNU General Public License, version 2 (see `COPYING`). The wire format,
algorithms and model data are derived from SpliX, (C) 2006-2008 Aurelien Croc
(AP2C) and contributors.

`libprint/` is part of Haiku and is distributed under the MIT licence (see
`libprint/LICENSE-Haiku.md` and `libprint/README.txt`).
