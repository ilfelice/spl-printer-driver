libprint - Haiku's helper library for raster printer drivers

These files are a copy of

    src/libs/print/libprint/*.cpp
    headers/libs/print/libprint/*.h

from the Haiku source tree (https://git.haiku-os.org/haiku), commit b42b90f
of 2026-10-03.  They are (C) their authors (Y. Takagi, Michael Pfeiffer and
the Haiku project) and distributed under the terms of the MIT licence, like
the rest of Haiku.

Two lines differ from the original:

- DbgMsg.h: the "__PRINTFLIKE(1,2)" annotation of write_debug_stream() was
  removed.  No Haiku header defines that macro, so the debug build (-DDBG)
  did not compile with it.
- JobSetupDlg.cpp: the "Ink usage" slider is initialised with
  127 - <stored value> instead of the stored value.  The original stores
  127 - <slider>, so showing the raw value flipped the setting on every visit
  to the Print dialog: the first print after adding a printer came out with
  at most 50% ink (black text printed as medium grey), the next one with full
  ink, and so on.  Haiku's own libprint drivers (PCL5, PCL6) have the same
  behaviour as of November 2026.

Haiku does not install libprint, so it is bundled here to let the driver be
built with nothing but a stock Haiku installation (see Makefile.haiku).  When
the driver is built inside a Haiku source tree (haiku/Jamfile) this copy is
not used.
