/*
 * spl.h - portable encoder for SPL2 / QPDL laser printers
 *
 * This is the platform-independent core of the Haiku SPL printer driver.  It
 * turns 1-bit page bitmaps into the byte stream that Samsung/Xerox/Dell "SPL"
 * (GDI) laser printers understand.
 *
 * The wire format and the per-model parameters come from SpliX
 * (https://github.com/OpenPrinting/splix), (C) 2006-2008 Aurelien Croc and
 * contributors.  This file is a derived work and is distributed under the
 * same licence: GNU General Public License, version 2.
 *
 * The code is deliberately plain C++98 with no STL and no OS dependencies,
 * so that it can be built on Haiku (including, hopefully, the old gcc2
 * toolchain) as well as on Linux.  So far it has only been compiled and
 * tested on Linux.
 */
#ifndef SPL_H
#define SPL_H

#include <stddef.h>
#include <stdint.h>

namespace spl {


// ---------------------------------------------------------------------------
// Static description of printers and media
// ---------------------------------------------------------------------------

struct Resolution {
	uint16_t	x;
	uint16_t	y;
};

/*! A paper size known to the QPDL protocol.  Dimensions are in PostScript
	points (1/72 inch), as in SpliX's PPD files.
*/
struct PaperSize {
	const char*	id;			// stable key, e.g. "A4"
	const char*	label;		// human readable, e.g. "A4"
	uint8_t		code;		// value sent in the QPDL page header
	uint16_t	width;		// points
	uint16_t	height;		// points
};

struct PaperSource {
	const char*	id;			// "Auto", "Manual", ...
	const char*	label;
	uint8_t		code;		// value sent in the QPDL page header
};

/*! A (PJL value, label) pair for the simple enumerated options. */
struct Choice {
	const char*	value;
	const char*	label;
};

enum DuplexCapability {
	kDuplexCapNone = 0,		// simplex only
	kDuplexCapManual,		// driver-assisted manual duplex
	kDuplexCapAuto			// printer has a duplex unit
};

/*! Everything the encoder needs to know about one printer model.  The table
	in spl_models.cpp is generated from SpliX's driver definitions.
*/
struct Model {
	const char*			id;				// SpliX PPD name, e.g. "ph3140"
	const char*			manufacturer;	// "Xerox"
	const char*			name;			// "Phaser 3140"
	const char*			deviceId;		// IEEE 1284 device id, or NULL

	uint8_t				qpdlVersion;	// 1, 2 or 3
	uint8_t				compression;	// QPDL algorithm number (0x11)
	uint16_t			bandHeight;		// lines per band (128)
	uint8_t				docHeader[3];	// model-specific page header bytes
	bool				fixedBandWidth;		// always send full-width bands
	bool				specialBandWidth;	// M2020-style band width table

	int32_t				marginX;		// hardware margin left/right, 1/1000 pt
	int32_t				marginY;		// hardware margin top/bottom, 1/1000 pt

	uint8_t				duplex;			// DuplexCapability
	bool				reverseDuplex;
	bool				hasSRTMode;

	const Resolution*	resolutions;	// first entry is the default
	uint8_t				resolutionCount;
	const uint8_t*		sources;		// QPDL paper source codes, first = default
	uint8_t				sourceCount;

	bool				verified;		// confirmed on real hardware with this driver
};

// Model table
int32_t			CountModels();
const Model*	ModelAt(int32_t index);
const Model*	FindModel(const char* id);
				// matches the id ("ph3140") or "Manufacturer Name"
				// ("Xerox Phaser 3140"), case-insensitively

/*! A positive number that identifies a model and stays the same when models
	are added to or removed from the table (it is derived from Model::id).
	Front ends can store it in their printer configuration.
*/
int32_t			ModelNumber(const Model& model);
const Model*	FindModelByNumber(int32_t number);

// Paper table
int32_t			CountPaperSizes();
const PaperSize* PaperSizeAt(int32_t index);
const PaperSize* FindPaperSize(const char* id);
const PaperSize* FindPaperSizeByCode(uint8_t code);

// Paper sources
int32_t			CountPaperSources();
const PaperSource* PaperSourceAt(int32_t index);
const PaperSource* FindPaperSource(const char* id);
const PaperSource* FindPaperSourceByCode(uint8_t code);

// Enumerated PJL options (identical for every supported model)
extern const Choice	kMediaTypes[];		// PAPERTYPE
extern const int	kMediaTypeCount;
extern const Choice	kSRTModes[];		// RET
extern const int	kSRTModeCount;
extern const int	kPowerSaveMinutes[];	// 0 = off
extern const int	kPowerSaveCount;
extern const int	kTonerDensities[];	// 1 = light, 3 = medium, 5 = dark
extern const int	kTonerDensityCount;


// ---------------------------------------------------------------------------
// Job settings
// ---------------------------------------------------------------------------

enum DuplexMode {
	kSimplex = 0,
	kDuplexLongEdge,
	kDuplexShortEdge,
	kManualDuplexLongEdge,
	kManualDuplexShortEdge
};

enum TonerSave {
	kTonerSavePrinterDefault = 0,
	kTonerSaveOn,
	kTonerSaveOff
};

struct JobSettings {
	// Media
	uint8_t		paperCode;			// PaperSize::code
	int32_t		paperWidth;			// 1/1000 pt
	int32_t		paperHeight;		// 1/1000 pt
	uint8_t		paperSource;		// PaperSource::code
	const char*	mediaType;			// one of kMediaTypes[].value

	// Rendering
	uint16_t	xResolution;
	uint16_t	yResolution;
	uint16_t	copies;
	DuplexMode	duplex;

	// Printer options
	TonerSave	tonerSave;
	int			tonerDensity;		// 1..5
	int			powerSave;			// minutes, 0 = off, < 0 = leave unchanged
	bool		jamRecovery;
	bool		highAltitude;
	const char*	srtMode;			// one of kSRTModes[].value

	// Job identification (shown on some printers' panels / status tools)
	const char*	userName;
	const char*	jobTitle;
	uint16_t	year;				// date stamped into the job header
	uint8_t		month;
	uint8_t		day;
};

/*! Fills in SpliX's defaults for \a model: first paper source and resolution,
	Letter paper, simplex, one copy, 5 minute power save, medium density.
*/
void	InitJobSettings(JobSettings& settings, const Model& model);
void	SetPaper(JobSettings& settings, const PaperSize& paper);


// ---------------------------------------------------------------------------
// Page bitmaps
// ---------------------------------------------------------------------------

/*! Pixel geometry of a page for given settings.  All values in device
	pixels unless noted.
*/
struct PageGeometry {
	uint32_t	width;				// full page width, rounded up to 8
	uint32_t	height;				// full page height
	uint32_t	bytesPerLine;		// width / 8
	uint32_t	marginLeft;			// unprintable border (hardware margin)
	uint32_t	marginTop;
	uint32_t	printableWidth;		// width - 2 * marginLeft
	uint32_t	printableHeight;	// height - 2 * marginTop
};

void	GetPageGeometry(const Model& model, const JobSettings& settings,
			PageGeometry& geometry);

/*! A full page, one bit per pixel.  Bit 7 of each byte is the leftmost pixel
	and a set bit means toner (black).  The bitmap always covers the whole
	sheet; pixels inside the hardware margins are simply never printed.
*/
class Page {
public:
						Page();
						~Page();

			bool		Allocate(const Model& model,
							const JobSettings& settings);
							// (re)allocates and clears the bitmap
			void		Clear();

			uint8_t*	Bits() { return fBits; }
	const	uint8_t*	Bits() const { return fBits; }
			uint32_t	Width() const { return fWidth; }
			uint32_t	Height() const { return fHeight; }
			uint32_t	BytesPerLine() const { return fWidth / 8; }
			size_t		Size() const
							{ return (size_t)(fWidth / 8) * fHeight; }

			/*! Copies a 1-bit raster into the page the way SpliX places a
				CUPS raster: centred, and clipped to the area inside
				\a marginLeft / \a marginBottom (in 1/1000 pt).
			*/
			void		PlaceRaster(const uint8_t* raster,
							uint32_t rasterBytesPerLine, uint32_t rasterHeight,
							int32_t marginLeft, int32_t marginBottom,
							const JobSettings& settings);

			/*! Rotates the page by 180 degrees (used for the back sides in
				long-edge manual duplex).  With \a splixQuirk set, a page with
				an odd number of bytes keeps its centre byte unmirrored, which
				is what SpliX does; only useful for byte-exact comparisons.
			*/
			void		Rotate180(bool splixQuirk = false);

private:
						Page(const Page&);
			Page&		operator=(const Page&);

			uint8_t*	fBits;
			uint32_t	fWidth;
			uint32_t	fHeight;
};


// ---------------------------------------------------------------------------
// Output and job
// ---------------------------------------------------------------------------

/*! Where the printer data goes.  The Haiku driver implements this on top of
	its transport; the test tools write to a file.
*/
class Output {
public:
	virtual				~Output();
	virtual	bool		Write(const void* buffer, size_t size) = 0;
};

class CompressedPage;

/*! One print job.  Feed it pages in reading order with AddPage() and call
	Finish() at the end.

	For simplex and automatic duplex each page is sent as soon as it has been
	compressed.  For manual duplex the compressed pages are kept in memory
	and sent by Finish() in the order the paper handling needs: back sides
	first (even pages, last to first), then the front sides.
*/
class Job {
public:
						Job(const Model& model, const JobSettings& settings,
							Output& output);
						~Job();

			/*! Compresses and queues/sends one page.  The page content may
				be modified (rotation for manual duplex).
			*/
			bool		AddPage(Page& page);
			bool		Finish();

			/*! Ends the job early, for instance when the user cancels it.
				Pages still waiting for a manual-duplex pass are discarded;
				if anything was sent, the printer is told the job is over.
			*/
			bool		Cancel();

			uint32_t	PageCount() const { return fPageCount; }

			/*! Number of bands that could not be compressed below their raw
				size and were therefore left out, exactly as SpliX does.  A
				non-zero value means part of a page will print blank.
			*/
			uint32_t	DroppedBands() const { return fDroppedBands; }

			const char*	LastError() const { return fError; }

			/*! Test hook: reproduce SpliX's byte-for-byte behaviour even
				where it is slightly wrong (see Page::Rotate180()).
			*/
			void		SetSplixQuirks(bool enable) { fSplixQuirks = enable; }

private:
						Job(const Job&);
			Job&		operator=(const Job&);

			bool		_SendJobHeader();
			bool		_SendJobFooter();
			CompressedPage* _CompressPage(const Page& page,
							uint32_t pageNumber);
			bool		_SendPage(const CompressedPage& page, bool lastPage);
			bool		_Write(const void* buffer, size_t size);
			bool		_Print(const char* format, ...);
			bool		_IsManualDuplex() const;

			const Model&		fModel;
			JobSettings			fSettings;
			Output&				fOutput;
			bool				fHeaderSent;
			bool				fFinished;
			bool				fSplixQuirks;
			uint32_t			fPageCount;
			uint32_t			fDroppedBands;
			const char*			fError;

			CompressedPage**	fPending;		// manual duplex only
			uint32_t			fPendingCount;
			uint32_t			fPendingCapacity;
};


// ---------------------------------------------------------------------------
// Low level: compression algorithm 0x11
// ---------------------------------------------------------------------------

/*! Compresses \a size bytes of band data.  \a output must have room for
	\a size bytes.  Returns the compressed length, or 0 if the data does not
	compress below its raw size (SpliX gives up on such bands, and so do we).
*/
size_t	CompressAlgo0x11(const uint8_t* data, size_t size, uint8_t* output);


}	// namespace spl

#endif	// SPL_H
