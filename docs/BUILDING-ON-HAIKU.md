# Building and testing the driver on Haiku

> **Status (November 2026):** the driver builds on Haiku (route A) and
> prints to a job file. A job it produced for an A4 page of an e-mail was
> decoded and checked: the page image is correct, and the job is byte for
> byte what SpliX produces for the same page image. **Nothing has been
> printed on paper yet.**

## What you need

- Haiku, 64-bit recommended. A virtual machine is fine for everything except
  the final test on the printer.
- This package, unpacked. Open a Terminal in its folder.

The standard Haiku image already contains the compiler and `make`.

## Route A: plain make (try this first)

    make -f Makefile.haiku
    make -f Makefile.haiku install-driver

The first command compiles the driver together with the bundled copy of
Haiku's libprint. The second copies two files to your add-ons folder:

    ~/config/non-packaged/add-ons/Print/SPL Compatible
    ~/config/non-packaged/add-ons/Print/transport/Save To Desktop

The second file is a connection type for testing: it writes every print job
to a new file on the Desktop. Restart Haiku once after installing, so that it
shows up in the list of connections.

For a build that logs what it does to `~/libprint.log`, use
`rm -rf objects.*` followed by `make -f Makefile.haiku DEFINES=DBG` instead
of the first command.

If `make` complains that it cannot find `makefile-engine`, install it with
`pkgman install makefile_engine` and try again.

To remove the driver: `make -f Makefile.haiku remove-driver`.

## Route B: inside a Haiku source tree (jam)

This is how Haiku's own printer drivers are built. Use it if route A fails
in a way that looks like a problem with the makefile rather than the code.

    git clone --depth 1 https://review.haiku-os.org/haiku
    sh haiku/install-into-haiku-tree.sh /path/to/the/haiku/checkout
    cd /path/to/the/haiku/checkout
    mkdir generated.x86_64 && cd generated.x86_64
    ../configure --target-arch x86_64
    jam -q "SPL Compatible"

Then locate the result with `find . -name "SPL Compatible"` and copy it to
`~/config/non-packaged/add-ons/Print/`.

## Route C: install a package instead

Releases on GitHub may come with a ready-built package,
`SPLprinterDriver-<version>-x86_64.hpkg`. Installing it needs no build at all:

    pkgman install ./SPLprinterDriver-0.2-1-x86_64.hpkg

(or double-click the file). It puts the driver and the Save To Desktop
connection into the system folder; `pkgman uninstall SPLprinterDriver`
removes them again. If a copy from route A is still installed, remove it
first with `make -f Makefile.haiku remove-driver`, because Haiku looks in
`~/config/non-packaged` before the system folder and would keep using the
old copy.

To build the package yourself, after route A's first command:

    make -f Makefile.haiku package

The package file appears in the project folder. `packaging/PackageInfo` holds
its description; the version is set in `Makefile.haiku`.

As with route A, restart `print_server` (or Haiku) once after installing, so
that the Save To Desktop connection is listed.

## Adding the printer

1. Open **Preferences > Printers** and choose **Add**.
2. Give it a name. As printer type choose **SPL Compatible**.
   For the first tests choose **Save To Desktop** as the connection.
   (Haiku's own "Print to file" froze in its save dialog when it was tried
   with this driver; whether that is a problem of this driver or of Haiku
   is not known yet.)
3. A list of printer models appears. Choose **Xerox Phaser 3140** (or the
   model you have) and confirm.

Paper size and resolution are in the application's **Page setup** dialog.
Paper tray, duplex, paper type, toner save, density and the other printer
options are in the **Print** dialog.

## Test 1: without a printer (done once, passed)

1. Print a page from any application (StyledEdit will do) to the new
   printer. A file named `spl-job-<date>-<time>.prn` appears on the Desktop.
   The text editor Pe did not hand over a job in one test (the log shows the
   Print dialog but no job); the e-mail client did.
2. Build the command line tools and decode the job:

       make
       ./qpdl2pbm -b ~/Desktop/spl-job-*.prn page

   This prints the job's settings, checks its structure and checksums, and
   writes `page-1.bmp`, `page-2.bmp`, ...
3. Open the BMP files in ShowImage. Each one should show the page as it
   would be printed. The image starts at the corner of the printable area,
   so the unprintable border of the sheet is not part of it.

What to report: the text `qpdl2pbm` printed, whether the images look right,
and ideally the `.prn` file itself.

Worth trying as well: a page with a photo, landscape orientation, 1200x600
dpi, two copies, and "Duplex (manual)" with a three page document.

## Test 2: on the printer

Two ways, and both are worth doing.

**A. A job file, sent from any computer.** `testpages-A4-ph3140.prn` in the
package is a three page A4 job: a page of text in several fonts, a page with
frames at 0, 1, 7, 8, 64, 96, 97, 125, 126 and 200 pixels (600 dpi) from the
paper edge, and a page of dithered gradients and shapes. It contains exactly
what SpliX sends for these pages. Sent straight to the printer from

- Linux: `cat testpages-A4-ph3140.prn > /dev/usb/lp0` (as root, or a member
  of the `lp` group), or `lp -d <queue name> -o raw testpages-A4-ph3140.prn`
  if the printer is set up in CUPS;
- macOS: `lpr -o raw -P <queue name> testpages-A4-ph3140.prn`.

A job saved with "Save To Desktop" on Haiku can be sent the same way.
`pbm2qpdl --paper Letter` makes a Letter version of the test pages
(see README.md).

**B. From Haiku.** Add the printer again, this time connected to **USB**
(or however it is attached), with the same model, and print one page of text.

What to report, for either way: what came out of the printer (a photo is
ideal, so that the position of the frames can be checked), and whether any
light on the printer blinked or stayed red.

Since the encoder sends exactly what the Linux driver SpliX sends for the same
page image, a job that decodes correctly in test 1 is expected to print. What
test 2 really adds is confirmation that Haiku's USB transport and this
printer get along, and that the page position on the paper is right.

## Known rough edges

- Haiku's own "Print to file" connection froze in its save dialog twice with
  this driver (once with no dialog at all, once with a half-drawn one), but
  not with Haiku's PCL5 driver in one try. The half-drawn dialog looks like
  the file-panel race condition that Haiku fixed in mid-2026, so a Haiku
  newer than that may not show it; "Save To Desktop" avoids the dialog
  altogether. Not understood yet.
- The first job printed with the driver had black text at 50% grey. That was
  Haiku's "Ink usage" slider in the Print dialog, which flipped its setting
  on every visit (a bug in Haiku's libprint, fixed in the copy bundled here
  from this version on). If an old build is still installed, moving the
  slider to "Max" has the same effect.

- Every paper size SpliX knows is offered, including sizes the printer
  cannot physically take.
- Haiku's default dithering (Floyd-Steinberg) makes photo pages large,
  several megabytes each. The pattern dither types in the Print dialog give
  much smaller jobs.
- With "Duplex (manual)" the driver sends the back sides first, last sheet
  first, then asks for the same sheets again through the manual feed for the
  front sides. This follows SpliX; how the Phaser 3140 prompts for the
  second pass has not been observed yet.
