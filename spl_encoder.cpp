/*
 * spl_encoder.cpp - QPDL stream generation
 *
 * Derived from SpliX (document.cpp, compress.cpp, qpdl.cpp, printer.cpp,
 * rendering.cpp, cache.cpp, page.cpp), (C) 2006-2008 Aurelien Croc (AP2C)
 * and contributors.  Distributed under the GNU General Public License,
 * version 2.
 *
 * Structure of a job as sent to the printer:
 *
 *   <ESC>%-12345X  PJL job header ... "@PJL ENTER LANGUAGE = QPDL"
 *   for every page:
 *     page header   (record 0x00, 17 bytes)
 *     for every non-blank band of 128 lines:
 *       band header (record 0x0C, 11 bytes)
 *       signature   (4 bytes, also announces the byte order)
 *       compressed band data
 *       checksum    (4 bytes)
 *     page footer   (record 0x01, 3 bytes)
 *   <TAB><ESC>%-12345X
 */
#include "spl.h"

#include <new>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace spl {


// #pragma mark - helpers


/*!	Converts a length in 1/1000 pt to device pixels, rounding up.
	Integer arithmetic keeps the result identical on every platform.
*/
static uint32_t
ToPixels(int32_t milliPoints, uint32_t resolution)
{
	if (milliPoints <= 0)
		return 0;
	const int64_t scaled = (int64_t)milliPoints * resolution;
	return (uint32_t)((scaled + 71999) / 72000);
}


static uint32_t
RoundUp8(uint32_t value)
{
	return (value + 7) & ~(uint32_t)7;
}


static void
PutBE32(uint8_t* buffer, uint32_t value)
{
	buffer[0] = (uint8_t)(value >> 24);
	buffer[1] = (uint8_t)(value >> 16);
	buffer[2] = (uint8_t)(value >> 8);
	buffer[3] = (uint8_t)value;
}


static bool
IsBlank(const uint8_t* data, size_t size)
{
	for (size_t i = 0; i < size; i++) {
		if (data[i] != 0)
			return false;
	}
	return true;
}


static uint8_t
ReverseBits(uint8_t value)
{
	value = (uint8_t)(((value & 0xf0) >> 4) | ((value & 0x0f) << 4));
	value = (uint8_t)(((value & 0xcc) >> 2) | ((value & 0x33) << 2));
	value = (uint8_t)(((value & 0xaa) >> 1) | ((value & 0x55) << 1));
	return value;
}


/*!	Band width, in bytes, that the Samsung M2020 family (and its HP and Xerox
	siblings) insists on for each paper size.  From SpliX, which took the
	numbers from Samsung's own Linux driver.  Returns 0 if the caller should
	use the natural page width.
*/
static uint32_t
SpecialBandWidthInBytes(uint8_t paperCode, uint32_t xResolution,
	uint32_t pageWidth)
{
	struct Entry {
		uint8_t		paperCode;
		uint16_t	width600;
		uint16_t	width1200;
	};
	static const Entry kWidths[] = {
		{ 0, 640, 1248 },	// Letter
		{ 1, 640, 1248 },	// Legal
		{ 2, 608, 1216 },	// A4
		{ 3, 544, 1056 },	// Executive
		{ 4, 800, 1600 },	// Ledger
		{ 5, 864, 1728 },	// A3
		{ 6, 288, 576 },	// Envelope #10
		{ 7, 288, 544 },	// Envelope Monarch
		{ 8, 480, 928 },	// Envelope C5
		{ 9, 320, 608 },	// Envelope DL
		{ 10, 736, 1472 },	// B4
		{ 11, 544, 1056 },	// B5
		{ 12, 512, 992 },	// Envelope B5
		{ 14, 288, 576 },	// Postcard
		{ 16, 416, 832 },	// A5
		{ 17, 288, 576 },	// A6
		{ 18, 384, 736 },	// B6
		{ 23, 320, 640 },	// Envelope C6
		{ 24, 640, 1248 },	// Folio
		{ 25, 256, 512 },	// Envelope Personal
		{ 26, 288, 544 },	// Envelope #9
		{ 27, 224, 416 },	// Index card
		{ 28, 640, 1248 },	// Oficio
		{ 30, 416, 800 },	// Statement
		{ 34, 800, 1568 },	// 8K
		{ 35, 576, 1120 }	// 16K
	};

	for (size_t i = 0; i < sizeof(kWidths) / sizeof(kWidths[0]); i++) {
		if (kWidths[i].paperCode != paperCode)
			continue;
		if (xResolution == 600)
			return kWidths[i].width600;
		if (xResolution == 1200)
			return kWidths[i].width1200;
		break;
	}

	// Unlisted size: trim a fixed margin and align to 256 pixels.
	int64_t divider = 1;
	if (xResolution == 600)
		divider = 2;
	else if (xResolution == 1200)
		divider = 4;
	const int64_t trimmed = ((int64_t)pageWidth / divider - 100) * divider;
	if (trimmed <= 0)
		return 0;
	const int64_t aligned = (trimmed + 255) / 256 * 256 / 8;
	return aligned >= 32 ? (uint32_t)aligned : 0;
}


// #pragma mark - settings


void
SetPaper(JobSettings& settings, const PaperSize& paper)
{
	settings.paperCode = paper.code;
	settings.paperWidth = (int32_t)paper.width * 1000;
	settings.paperHeight = (int32_t)paper.height * 1000;
}


void
InitJobSettings(JobSettings& settings, const Model& model)
{
	memset(&settings, 0, sizeof(settings));

	const PaperSize* letter = FindPaperSize("Letter");
	if (letter != NULL)
		SetPaper(settings, *letter);
	settings.paperSource = model.sourceCount > 0 ? model.sources[0] : 1;
	settings.mediaType = "OFF";

	if (model.resolutionCount > 0) {
		settings.xResolution = model.resolutions[0].x;
		settings.yResolution = model.resolutions[0].y;
	} else {
		settings.xResolution = 600;
		settings.yResolution = 600;
	}
	settings.copies = 1;
	settings.duplex = kSimplex;

	settings.tonerSave = kTonerSavePrinterDefault;
	settings.tonerDensity = 3;
	settings.powerSave = 5;
	settings.jamRecovery = false;
	settings.highAltitude = false;
	settings.srtMode = "NORMAL";

	settings.userName = NULL;
	settings.jobTitle = NULL;
	settings.year = 2000;
	settings.month = 1;
	settings.day = 1;
}


void
GetPageGeometry(const Model& model, const JobSettings& settings,
	PageGeometry& geometry)
{
	geometry.width = RoundUp8(ToPixels(settings.paperWidth,
		settings.xResolution));
	geometry.height = ToPixels(settings.paperHeight, settings.yResolution);
	geometry.bytesPerLine = geometry.width / 8;
	geometry.marginLeft = RoundUp8(ToPixels(model.marginX,
		settings.xResolution));
	geometry.marginTop = ToPixels(model.marginY, settings.yResolution);

	geometry.printableWidth = geometry.width > 2 * geometry.marginLeft
		? geometry.width - 2 * geometry.marginLeft : 0;
	geometry.printableHeight = geometry.height > 2 * geometry.marginTop
		? geometry.height - 2 * geometry.marginTop : 0;
}


// #pragma mark - Page


Page::Page()
	:
	fBits(NULL),
	fWidth(0),
	fHeight(0)
{
}


Page::~Page()
{
	free(fBits);
}


bool
Page::Allocate(const Model& model, const JobSettings& settings)
{
	PageGeometry geometry;
	GetPageGeometry(model, settings, geometry);
	if (geometry.width == 0 || geometry.height == 0)
		return false;

	const size_t size = (size_t)geometry.bytesPerLine * geometry.height;
	uint8_t* bits = (uint8_t*)realloc(fBits, size);
	if (bits == NULL)
		return false;

	fBits = bits;
	fWidth = geometry.width;
	fHeight = geometry.height;
	memset(fBits, 0, size);
	return true;
}


void
Page::Clear()
{
	if (fBits != NULL)
		memset(fBits, 0, Size());
}


void
Page::PlaceRaster(const uint8_t* raster, uint32_t rasterBytesPerLine,
	uint32_t rasterHeight, int32_t marginLeft, int32_t marginBottom,
	const JobSettings& settings)
{
	if (fBits == NULL || raster == NULL)
		return;

	const int64_t pageBytes = fWidth / 8;
	int64_t pageRows = fHeight;
	int64_t marginBytes = (ToPixels(marginLeft, settings.xResolution) + 7) / 8;
	const int64_t marginRows = ToPixels(marginBottom, settings.yResolution);
	if (2 * marginBytes >= pageBytes || 2 * marginRows >= pageRows)
		return;

	// Horizontally: clip a raster that is wider than the printable area,
	// centre one that is narrower.
	int64_t skipBytes;
	int64_t copyBytes;
	if (rasterBytesPerLine > pageBytes - 2 * marginBytes) {
		skipBytes = (rasterBytesPerLine - (pageBytes - 2 * marginBytes)) / 2;
		copyBytes = pageBytes - 2 * marginBytes;
	} else {
		skipBytes = 0;
		marginBytes = (pageBytes - rasterBytesPerLine) / 2;
		copyBytes = rasterBytesPerLine;
	}

	// Vertically: likewise.
	int64_t skipRows;
	int64_t row;
	int64_t rasterRows = rasterHeight;
	if (rasterRows > pageRows - 2 * marginRows) {
		skipRows = (rasterRows - (pageRows - 2 * marginRows)) / 2;
		row = marginRows;
	} else {
		skipRows = 0;
		row = (pageRows - rasterRows) / 2;
	}
	rasterRows -= skipRows;
	pageRows -= 2 * marginRows;

	const uint8_t* source = raster + skipRows * rasterBytesPerLine + skipBytes;
	uint8_t* target = fBits + row * pageBytes + marginBytes;
	while (pageRows > 0 && rasterRows > 0) {
		memcpy(target, source, (size_t)copyBytes);
		source += rasterBytesPerLine;
		target += pageBytes;
		pageRows--;
		rasterRows--;
	}
}


void
Page::Rotate180(bool splixQuirk)
{
	if (fBits == NULL)
		return;

	const size_t size = Size();
	const size_t half = size / 2;
	for (size_t i = 0; i < half; i++) {
		const uint8_t head = fBits[i];
		fBits[i] = ReverseBits(fBits[size - i - 1]);
		fBits[size - i - 1] = ReverseBits(head);
	}
	if ((size & 1) != 0 && !splixQuirk)
		fBits[half] = ReverseBits(fBits[half]);
}


// #pragma mark - Output


Output::~Output()
{
}


// #pragma mark - CompressedPage


struct CompressedBand {
	CompressedBand*	next;
	uint32_t		number;
	uint32_t		width;		// pixels
	uint32_t		height;		// lines
	uint8_t*		data;
	size_t			size;
	uint32_t		checksum;	// sum of all data bytes
};


class CompressedPage {
public:
	CompressedPage(uint32_t number)
		:
		fNumber(number),
		fFirst(NULL),
		fLast(NULL)
	{
	}

	~CompressedPage()
	{
		CompressedBand* band = fFirst;
		while (band != NULL) {
			CompressedBand* next = band->next;
			free(band->data);
			delete band;
			band = next;
		}
	}

	bool AddBand(uint32_t number, uint32_t width, uint32_t height,
		const uint8_t* data, size_t size)
	{
		CompressedBand* band = new(std::nothrow) CompressedBand;
		if (band == NULL)
			return false;
		band->data = (uint8_t*)malloc(size);
		if (band->data == NULL) {
			delete band;
			return false;
		}
		memcpy(band->data, data, size);
		band->next = NULL;
		band->number = number;
		band->width = width;
		band->height = height;
		band->size = size;
		band->checksum = 0;
		for (size_t i = 0; i < size; i++)
			band->checksum += data[i];

		if (fLast != NULL)
			fLast->next = band;
		else
			fFirst = band;
		fLast = band;
		return true;
	}

	uint32_t Number() const { return fNumber; }
	const CompressedBand* FirstBand() const { return fFirst; }

private:
	uint32_t		fNumber;
	CompressedBand*	fFirst;
	CompressedBand*	fLast;
};


// #pragma mark - Job


Job::Job(const Model& model, const JobSettings& settings, Output& output)
	:
	fModel(model),
	fSettings(settings),
	fOutput(output),
	fHeaderSent(false),
	fFinished(false),
	fSplixQuirks(false),
	fPageCount(0),
	fDroppedBands(0),
	fError(NULL),
	fPending(NULL),
	fPendingCount(0),
	fPendingCapacity(0)
{
	if (fSettings.copies == 0)
		fSettings.copies = 1;
	if (fSettings.mediaType == NULL)
		fSettings.mediaType = "OFF";
	if (fSettings.srtMode == NULL)
		fSettings.srtMode = "NORMAL";
	if (fSettings.userName == NULL)
		fSettings.userName = "Unknown";
	if (fSettings.jobTitle == NULL)
		fSettings.jobTitle = "Unknown job title";

	// A printer without the hardware cannot be asked for automatic duplex,
	// and vice versa; fall back the way SpliX's PPDs would.
	if (fModel.duplex == kDuplexCapNone) {
		fSettings.duplex = kSimplex;
	} else if (fModel.duplex == kDuplexCapManual) {
		if (fSettings.duplex == kDuplexLongEdge)
			fSettings.duplex = kManualDuplexLongEdge;
		else if (fSettings.duplex == kDuplexShortEdge)
			fSettings.duplex = kManualDuplexShortEdge;
	} else {
		if (fSettings.duplex == kManualDuplexLongEdge)
			fSettings.duplex = kDuplexLongEdge;
		else if (fSettings.duplex == kManualDuplexShortEdge)
			fSettings.duplex = kDuplexShortEdge;
	}
}


Job::~Job()
{
	for (uint32_t i = 0; i < fPendingCount; i++)
		delete fPending[i];
	free(fPending);
}


bool
Job::_IsManualDuplex() const
{
	return fSettings.duplex == kManualDuplexLongEdge
		|| fSettings.duplex == kManualDuplexShortEdge;
}


bool
Job::_Write(const void* buffer, size_t size)
{
	if (size == 0)
		return true;
	if (!fOutput.Write(buffer, size)) {
		fError = "writing to the printer failed";
		return false;
	}
	return true;
}


bool
Job::_Print(const char* format, ...)
{
	char buffer[256];
	va_list args;
	va_start(args, format);
	int length = vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);

	if (length < 0)
		return false;
	if ((size_t)length >= sizeof(buffer))
		length = sizeof(buffer) - 1;
	return _Write(buffer, (size_t)length);
}


/*!	Copies \a text for use inside a quoted PJL string: quotes and control
	characters would end the string or the command early.
*/
static void
SanitizeForPJL(const char* text, char* buffer, size_t bufferSize)
{
	size_t i = 0;
	for (; text[i] != '\0' && i + 1 < bufferSize; i++) {
		const unsigned char c = (unsigned char)text[i];
		if (c < 0x20 || c == 0x7f)
			buffer[i] = ' ';
		else if (c == '"')
			buffer[i] = '\'';
		else
			buffer[i] = (char)c;
	}
	buffer[i] = '\0';
}


bool
Job::_SendJobHeader()
{
	char user[128];
	char title[128];
	SanitizeForPJL(fSettings.userName, user, sizeof(user));
	SanitizeForPJL(fSettings.jobTitle, title, sizeof(title));

	bool ok = _Write("\033%-12345X", 9);

	// Job information
	ok = ok && _Print("@PJL DEFAULT SERVICEDATE=%04u%02u%02u\n",
		(unsigned)fSettings.year, (unsigned)fSettings.month,
		(unsigned)fSettings.day);
	ok = ok && _Print("@PJL SET USERNAME=\"%s\"\n", user);
	ok = ok && _Print("@PJL SET JOBNAME=\"%s\"\n", title);

	// Printer options
	if (fSettings.tonerSave == kTonerSaveOn)
		ok = ok && _Print("@PJL SET ECONOMODE=ON\n");
	else if (fSettings.tonerSave == kTonerSaveOff)
		ok = ok && _Print("@PJL SET ECONOMODE=OFF\n");

	if (fSettings.powerSave > 0) {
		ok = ok && _Print("@PJL DEFAULT POWERSAVE=ON\n");
		ok = ok && _Print("@PJL DEFAULT POWERSAVETIME=%d\n",
			fSettings.powerSave);
	} else if (fSettings.powerSave == 0)
		ok = ok && _Print("@PJL DEFAULT POWERSAVE=OFF\n");

	ok = ok && _Print("@PJL SET JAMRECOVERY=%s\n",
		fSettings.jamRecovery ? "ON" : "OFF");

	// Duplex
	const char* reverse = fModel.reverseDuplex ? "REVERSE_" : "";
	switch (fSettings.duplex) {
		case kSimplex:
			ok = ok && _Print("@PJL SET DUPLEX=OFF\n");
			break;
		case kDuplexLongEdge:
			ok = ok && _Print("@PJL SET DUPLEX=ON\n");
			ok = ok && _Print("@PJL SET BINDING=%sLONGEDGE\n", reverse);
			break;
		case kDuplexShortEdge:
			ok = ok && _Print("@PJL SET DUPLEX=ON\n");
			ok = ok && _Print("@PJL SET BINDING=%sSHORTEDGE\n", reverse);
			break;
		case kManualDuplexLongEdge:
			ok = ok && _Print("@PJL SET DUPLEX=MANUAL\n");
			ok = ok && _Print("@PJL SET BINDING=LONGEDGE\n");
			break;
		case kManualDuplexShortEdge:
			ok = ok && _Print("@PJL SET DUPLEX=MANUAL\n");
			ok = ok && _Print("@PJL SET BINDING=SHORTEDGE\n");
			break;
	}

	// Job options
	char value[32];
	SanitizeForPJL(fSettings.mediaType, value, sizeof(value));
	ok = ok && _Print("@PJL SET PAPERTYPE=%s\n", value);
	ok = ok && _Print("@PJL SET ALTITUDE=%s\n",
		fSettings.highAltitude ? "HIGH" : "LOW");
	ok = ok && _Print("@PJL SET DENSITY=%d\n", fSettings.tonerDensity);
	SanitizeForPJL(fSettings.srtMode, value, sizeof(value));
	ok = ok && _Print("@PJL SET RET=%s\n", value);

	ok = ok && _Print("@PJL ENTER LANGUAGE = QPDL\n");

	fHeaderSent = ok;
	return ok;
}


bool
Job::_SendJobFooter()
{
	return _Write("\t\033%-12345X", 10);
}


/*!	Cuts the page into bands and compresses each one.

	Band data is stored column by column (all 128 lines of the first byte
	column, then the second, ...) and inverted, so that white paper becomes
	long runs of 0xff, which algorithm 0x11 then squeezes down.  The image is
	shifted left and up by the hardware margins: the printer's origin is the
	corner of the printable area, not of the sheet.
*/
CompressedPage*
Job::_CompressPage(const Page& page, uint32_t pageNumber)
{
	PageGeometry geometry;
	GetPageGeometry(fModel, fSettings, geometry);

	const uint32_t marginBytes = geometry.marginLeft / 8;
	const uint32_t marginRows = geometry.marginTop;
	const uint32_t lineBytes = page.BytesPerLine();
	if (page.Bits() == NULL || marginBytes >= lineBytes
		|| marginRows >= page.Height()) {
		fError = "page bitmap does not match the job settings";
		return NULL;
	}

	uint32_t rowsLeft = page.Height() - marginRows;
	uint32_t bandHeight = fModel.bandHeight;
	if (fSettings.xResolution == 300 && fSettings.yResolution == 300)
		bandHeight /= 2;

	uint32_t bandBytes = lineBytes;
	if (fModel.specialBandWidth) {
		const uint32_t special = SpecialBandWidthInBytes(fSettings.paperCode,
			fSettings.xResolution, page.Width());
		if (special != 0)
			bandBytes = special;
	} else if (fModel.fixedBandWidth) {
		// Some QPDL 1 and 2 models want full-width bands (4960 pixels at
		// 600 dpi) whatever the paper size.
		bandBytes = (uint32_t)fSettings.xResolution * 4960 / 600 / 8;
	}
	if (bandHeight == 0 || bandBytes == 0) {
		fError = "invalid band geometry";
		return NULL;
	}

	const size_t bandSize = (size_t)bandBytes * bandHeight;
	uint8_t* band = (uint8_t*)malloc(bandSize);
	uint8_t* compressed = (uint8_t*)malloc(bandSize);
	CompressedPage* result = new(std::nothrow) CompressedPage(pageNumber);
	if (band == NULL || compressed == NULL || result == NULL) {
		free(band);
		free(compressed);
		delete result;
		fError = "out of memory";
		return NULL;
	}

	// Number of byte columns that actually come from the page; the rest of
	// the band (if any) is white.
	const uint32_t sourceBytes = lineBytes - marginBytes < bandBytes
		? lineBytes - marginBytes : bandBytes;

	const uint8_t* rows = page.Bits() + (size_t)marginRows * lineBytes;
	bool ok = true;

	for (uint32_t bandNumber = 0; rowsLeft > 0; bandNumber++) {
		const uint32_t localHeight = rowsLeft < bandHeight
			? rowsLeft : bandHeight;

		memset(band, 0, bandSize);
		for (uint32_t y = 0; y < localHeight; y++) {
			const uint8_t* line = rows + (size_t)y * lineBytes + marginBytes;
			for (uint32_t x = 0; x < sourceBytes; x++)
				band[(size_t)x * bandHeight + y] = line[x];
		}

		if (!IsBlank(band, bandSize)) {
			for (size_t i = 0; i < bandSize; i++)
				band[i] = (uint8_t)~band[i];

			const size_t size = CompressAlgo0x11(band, bandSize, compressed);
			if (size == 0) {
				// Not compressible below its raw size.  SpliX leaves such
				// bands out, and so do we; the caller can find out through
				// DroppedBands().
				fDroppedBands++;
			} else if (!result->AddBand(bandNumber, bandBytes * 8, bandHeight,
					compressed, size)) {
				fError = "out of memory";
				ok = false;
				break;
			}
		}

		rows += (size_t)lineBytes * bandHeight;
		rowsLeft -= localHeight;
	}

	free(band);
	free(compressed);
	if (!ok) {
		delete result;
		return NULL;
	}
	return result;
}


bool
Job::_SendPage(const CompressedPage& page, bool lastPage)
{
	uint8_t paperSource = fSettings.paperSource;
	uint8_t duplex = 0;
	uint8_t tumble = 0;

	switch (fSettings.duplex) {
		case kSimplex:
			duplex = 1;
			tumble = 0;
			break;
		case kDuplexLongEdge:
			duplex = 1;
			tumble = page.Number() % 2;
			break;
		case kDuplexShortEdge:
			duplex = 0;
			tumble = page.Number() % 2;
			break;
		case kManualDuplexLongEdge:
		case kManualDuplexShortEdge:
			duplex = 0;
			tumble = page.Number() % 2;
			// Front sides are printed on the sheets the user has just put
			// back, which go through the multi-purpose/manual feeder.
			if (tumble != 0 && !lastPage)
				paperSource = 3;
			break;
	}

	// Page size in 1/300 inch, rounded up.
	const uint32_t width300 = (uint32_t)(((int64_t)fSettings.paperWidth * 300
		+ 71999) / 72000);
	const uint32_t height300 = (uint32_t)(((int64_t)fSettings.paperHeight * 300
		+ 71999) / 72000);

	uint8_t header[17];
	header[0] = 0x00;									// record type
	header[1] = (uint8_t)(fSettings.yResolution / 100);
	header[2] = (uint8_t)(fSettings.copies >> 8);
	header[3] = (uint8_t)fSettings.copies;
	header[4] = fSettings.paperCode;
	header[5] = (uint8_t)(width300 >> 8);
	header[6] = (uint8_t)width300;
	header[7] = (uint8_t)(height300 >> 8);
	header[8] = (uint8_t)height300;
	header[9] = paperSource;
	header[10] = fModel.docHeader[0];
	header[11] = duplex;
	header[12] = tumble;
	header[13] = fModel.docHeader[1];
	header[14] = fModel.qpdlVersion;
	header[15] = fModel.docHeader[2];
	header[16] = (uint8_t)(fSettings.xResolution / 100);
	if (!_Write(header, sizeof(header)))
		return false;

	for (const CompressedBand* band = page.FirstBand(); band != NULL;
			band = band->next) {
		// Payload = signature + data (+ checksum from QPDL version 1 on).
		uint32_t payload = (uint32_t)band->size + 4;
		if (fModel.qpdlVersion > 0)
			payload += 4;

		uint8_t record[15];
		record[0] = 0x0c;								// record type
		record[1] = (uint8_t)band->number;
		record[2] = (uint8_t)(band->width >> 8);
		record[3] = (uint8_t)band->width;
		record[4] = (uint8_t)(band->height >> 8);
		record[5] = (uint8_t)band->height;
		record[6] = fModel.compression;
		PutBE32(record + 7, payload);

		// Signature 0x09abcdef, stored little-endian: this is how the
		// printer learns the byte order of the compressed data's header.
		record[11] = 0xef;
		record[12] = 0xcd;
		record[13] = 0xab;
		record[14] = 0x09;
		if (!_Write(record, sizeof(record)))
			return false;

		if (!_Write(band->data, band->size))
			return false;

		uint8_t checksum[4];
		PutBE32(checksum, band->checksum + 0xef + 0xcd + 0xab + 0x09);
		if (!_Write(checksum, sizeof(checksum)))
			return false;
	}

	uint8_t footer[3];
	footer[0] = 0x01;									// record type
	footer[1] = (uint8_t)(fSettings.copies >> 8);
	footer[2] = (uint8_t)fSettings.copies;
	return _Write(footer, sizeof(footer));
}


bool
Job::AddPage(Page& page)
{
	if (fFinished) {
		fError = "job already finished";
		return false;
	}

	const uint32_t pageNumber = fPageCount + 1;

	// Back sides are turned upside down for long-edge manual duplex, so
	// that the sheet can be re-fed without the user having to think.
	if (fSettings.duplex == kManualDuplexLongEdge && pageNumber % 2 == 0)
		page.Rotate180(fSplixQuirks);

	CompressedPage* compressed = _CompressPage(page, pageNumber);
	if (compressed == NULL)
		return false;
	fPageCount = pageNumber;

	if (_IsManualDuplex()) {
		if (fPendingCount == fPendingCapacity) {
			const uint32_t capacity = fPendingCapacity == 0
				? 16 : fPendingCapacity * 2;
			CompressedPage** pending = (CompressedPage**)realloc(fPending,
				capacity * sizeof(CompressedPage*));
			if (pending == NULL) {
				delete compressed;
				fError = "out of memory";
				return false;
			}
			fPending = pending;
			fPendingCapacity = capacity;
		}
		fPending[fPendingCount++] = compressed;
		return true;
	}

	bool ok = fHeaderSent || _SendJobHeader();
	ok = ok && _SendPage(*compressed, false);
	delete compressed;
	return ok;
}


bool
Job::Cancel()
{
	if (fFinished)
		return true;
	fFinished = true;

	for (uint32_t i = 0; i < fPendingCount; i++)
		delete fPending[i];
	fPendingCount = 0;

	return fHeaderSent ? _SendJobFooter() : true;
}


bool
Job::Finish()
{
	if (fFinished)
		return true;
	fFinished = true;

	bool ok = fHeaderSent || _SendJobHeader();

	if (_IsManualDuplex() && fPendingCount > 0) {
		const uint32_t count = fPendingCount;
		const bool oddCount = (count % 2) != 0;

		// Back sides first, last sheet first...
		for (uint32_t number = count & ~(uint32_t)1; ok && number >= 2;
				number -= 2) {
			ok = _SendPage(*fPending[number - 1], false);
		}
		// ...then the front sides in reading order.  A trailing front side
		// without a back side comes from the normal tray.
		for (uint32_t number = 1; ok && number <= count; number += 2) {
			ok = _SendPage(*fPending[number - 1],
				oddCount && number == count);
		}
	}

	ok = ok && _SendJobFooter();
	return ok;
}


}	// namespace spl
