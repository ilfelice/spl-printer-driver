/*
 * SPLDriver.cpp - the print job side of the Haiku SPL printer driver
 *
 * Distributed under the GNU General Public License, version 2.
 *
 * libprint renders each page into bitmap bands and hands them to NextBand().
 * The bands are dithered to one bit per pixel and collected in a bitmap of
 * the whole sheet; at the end of the page that bitmap goes to the encoder
 * core (spl::Job), which compresses it and writes the QPDL data to the
 * printer through the transport.
 */
#include "SPLDriver.h"

#include <new>
#include <vector>

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <Alert.h>
#include <Bitmap.h>

#include "DbgMsg.h"
#include "Halftone.h"
#include "JobData.h"
#include "PrinterData.h"
#include "SpoolMetaData.h"
#include "ValidRect.h"

#include "SPLCap.h"


class SPLDriver::TransportOutput : public spl::Output {
public:
	TransportOutput(SPLDriver* driver)
		:
		fDriver(driver)
	{
	}

	virtual bool Write(const void* buffer, size_t size)
	{
		return fDriver->WriteToTransport(buffer, size);
	}

private:
	SPLDriver* fDriver;
};


/*!	Returns the entry of \a choices whose value is \a value, so that the
	result stays valid after the job settings are gone.
*/
static const char*
FindChoice(const spl::Choice* choices, int count, const char* value,
	const char* fallback)
{
	if (value != NULL) {
		for (int i = 0; i < count; i++) {
			if (strcmp(choices[i].value, value) == 0)
				return choices[i].value;
		}
	}
	return fallback;
}


// #pragma mark -


SPLDriver::SPLDriver(BMessage* message, PrinterData* printerData,
	const PrinterCap* printerCap)
	:
	GraphicsDriver(message, printerData, printerCap),
	fModel(NULL),
	fJob(NULL),
	fOutput(NULL),
	fHalftone(NULL),
	fOriginX(0),
	fOriginY(0),
	fErrorReported(false)
{
}


SPLDriver::~SPLDriver()
{
	_Cleanup();
}


bool
SPLDriver::WriteToTransport(const void* buffer, size_t size)
{
	try {
		WriteSpoolData(buffer, size);
		return true;
	} catch (TransportException& error) {
		fTransportError = error.What();
		return false;
	}
}


/*!	Translates libprint's job data into the encoder's job settings. */
void
SPLDriver::_InitSettings()
{
	const JobData* job = GetJobData();
	spl::InitJobSettings(fSettings, *fModel);

	// Media
	const spl::PaperSize* paper = SPLCap::PaperSizeFor(job->GetPaper());
	if (paper != NULL)
		spl::SetPaper(fSettings, *paper);
	fSettings.paperSource = SPLCap::PaperSourceCodeFor(job->GetPaperSource());

	// Rendering.  Only resolutions the model really has are accepted.
	for (int i = 0; i < fModel->resolutionCount; i++) {
		if (fModel->resolutions[i].x == job->GetXres()
			&& fModel->resolutions[i].y == job->GetYres()) {
			fSettings.xResolution = fModel->resolutions[i].x;
			fSettings.yResolution = fModel->resolutions[i].y;
			break;
		}
	}
	const int32 copies = job->GetCopies();
	fSettings.copies = (uint16)(copies < 1 ? 1 : copies > 999 ? 999 : copies);

	// Driver specific settings
	const DriverSpecificSettings& options = job->Settings();

	fSettings.duplex = spl::kSimplex;
	if (job->GetPrintStyle() == JobData::kDuplex) {
		const bool shortEdge = options.HasString(SPLCap::kDuplexBindingKey)
			&& strcmp(options.GetString(SPLCap::kDuplexBindingKey), "short")
				== 0;
		// spl::Job turns this into manual duplex if the model has no
		// duplex unit.
		fSettings.duplex = shortEdge
			? spl::kDuplexShortEdge : spl::kDuplexLongEdge;
	}

	if (options.HasString(SPLCap::kMediaTypeKey)) {
		fSettings.mediaType = FindChoice(spl::kMediaTypes,
			spl::kMediaTypeCount, options.GetString(SPLCap::kMediaTypeKey),
			"OFF");
	}
	if (fModel->hasSRTMode && options.HasString(SPLCap::kSRTModeKey)) {
		fSettings.srtMode = FindChoice(spl::kSRTModes, spl::kSRTModeCount,
			options.GetString(SPLCap::kSRTModeKey), "NORMAL");
	}
	if (options.HasString(SPLCap::kTonerSaveKey)) {
		const char* value = options.GetString(SPLCap::kTonerSaveKey);
		if (strcmp(value, "on") == 0)
			fSettings.tonerSave = spl::kTonerSaveOn;
		else if (strcmp(value, "off") == 0)
			fSettings.tonerSave = spl::kTonerSaveOff;
	}
	if (options.HasString(SPLCap::kTonerDensityKey)) {
		const int density = atoi(options.GetString(SPLCap::kTonerDensityKey));
		if (density >= 1 && density <= 5)
			fSettings.tonerDensity = density;
	}
	if (options.HasString(SPLCap::kPowerSaveKey)) {
		const int minutes = atoi(options.GetString(SPLCap::kPowerSaveKey));
		if (minutes >= 0 && minutes <= 240)
			fSettings.powerSave = minutes;
	}
	if (options.HasBoolean(SPLCap::kJamRecoveryKey)) {
		fSettings.jamRecovery
			= options.GetBoolean(SPLCap::kJamRecoveryKey);
	}
	if (options.HasBoolean(SPLCap::kHighAltitudeKey)) {
		fSettings.highAltitude
			= options.GetBoolean(SPLCap::kHighAltitudeKey);
	}

	// Job identification
	const char* user = getenv("USER");
	fUserName = user != NULL && user[0] != '\0' ? user : "Haiku";
	const SpoolMetaData* metaData = GetSpoolMetaData();
	fJobTitle = metaData != NULL ? metaData->GetDescription() : "";
	if (fJobTitle.empty())
		fJobTitle = "Haiku print job";
	fSettings.userName = fUserName.c_str();
	fSettings.jobTitle = fJobTitle.c_str();

	time_t now = time(NULL);
	struct tm date;
	if (localtime_r(&now, &date) != NULL) {
		fSettings.year = (uint16)(date.tm_year + 1900);
		fSettings.month = (uint8)(date.tm_mon + 1);
		fSettings.day = (uint8)date.tm_mday;
	}
}


bool
SPLDriver::StartDocument()
{
	_Cleanup();
	fErrorReported = false;
	fTransportError = "";

	fModel = SPLCap::ModelForProtocolClass(GetProtocolClass());
	if (fModel == NULL)
		return false;

	_InitSettings();

	// libprint's page is the sheet minus the margins SPLCap announced.
	int32 marginX;
	int32 marginY;
	SPLCap::GetMargins(*fModel, marginX, marginY);
	fOriginX = (int)((marginX * fSettings.xResolution + 36) / 72);
	fOriginY = (int)((marginY * fSettings.yResolution + 36) / 72);

	const JobData* job = GetJobData();
	fHalftone = new(std::nothrow) Halftone(job->GetSurfaceType(),
		job->GetGamma(), job->GetInkDensity(), job->GetDitherType());
	fOutput = new(std::nothrow) TransportOutput(this);
	if (fHalftone == NULL || fOutput == NULL)
		return false;

	fJob = new(std::nothrow) spl::Job(*fModel, fSettings, *fOutput);
	return fJob != NULL;
}


bool
SPLDriver::StartPage(int)
{
	if (fJob == NULL)
		return false;
	// A fresh, white sheet
	return fPage.Allocate(*fModel, fSettings);
}


/*!	ORs \a width one-bit pixels into the sheet bitmap at pixel (\a x, \a y).
	Everything outside the bitmap is clipped away.
*/
void
SPLDriver::_BlitLine(const uint8* line, int width, int x, int y)
{
	if (x < 0 || y < 0 || y >= (int)fPage.Height() || width <= 0)
		return;

	const int bytesPerLine = (int)fPage.BytesPerLine();
	uint8* row = fPage.Bits() + (size_t)y * bytesPerLine;
	const int firstByte = x / 8;
	const int shift = x % 8;
	const int bytes = (width + 7) / 8;

	for (int i = 0; i < bytes; i++) {
		uint8 value = line[i];
		if (i == bytes - 1 && (width % 8) != 0)
			value &= (uint8)(0xff << (8 - width % 8));
		if (value == 0)
			continue;

		const int index = firstByte + i;
		if (index >= bytesPerLine)
			break;
		row[index] |= (uint8)(value >> shift);
		if (shift != 0 && index + 1 < bytesPerLine)
			row[index + 1] |= (uint8)(value << (8 - shift));
	}
}


bool
SPLDriver::NextBand(BBitmap* bitmap, BPoint* offset)
{
	DBGMSG(("> nextBand\n"));

	if (fJob == NULL || fHalftone == NULL || fPage.Bits() == NULL)
		return false;

	BRect bounds = bitmap->Bounds();
	RECT rc;
	rc.left = (int)bounds.left;
	rc.top = (int)bounds.top;
	rc.right = (int)bounds.right;
	rc.bottom = (int)bounds.bottom;

	int height = rc.bottom - rc.top + 1;
	const int x = (int)offset->x;
	const int y = (int)offset->y;
	const int pageHeight = GetPageHeight();
	if (y + height > pageHeight)
		height = pageHeight - y;
	rc.bottom = height - 1;

	// Only the part of the band that is not white needs any work.
	if (height > 0 && get_valid_rect(bitmap, &rc)) {
		const int width = rc.right - rc.left + 1;
		const int delta = bitmap->BytesPerRow();
		const uchar* source = static_cast<const uchar*>(bitmap->Bits())
			+ rc.top * delta + (rc.left * fHalftone->GetPixelDepth()) / 8;

		std::vector<uchar> line((width + 7) / 8 + 1);

		for (int row = rc.top; row <= rc.bottom; row++) {
			const int pageY = y + row;
			fHalftone->Dither(&line[0], source, x + rc.left, pageY, width);
			_BlitLine(&line[0], width, fOriginX + x + rc.left,
				fOriginY + pageY);
			source += delta;
		}
	} else
		DBGMSG(("band bitmap is clean.\n"));

	if (height <= 0 || y + height >= pageHeight) {
		// that was the last band of the page
		offset->x = -1.0;
		offset->y = -1.0;
	} else
		offset->y += height;

	DBGMSG(("< nextBand\n"));
	return true;
}


bool
SPLDriver::EndPage(int)
{
	if (fJob == NULL)
		return false;

	if (!fJob->AddPage(fPage)) {
		_ReportError();
		return false;
	}
	return true;
}


bool
SPLDriver::EndDocument(bool success)
{
	bool result = true;

	if (fJob != NULL) {
		if (success) {
			// For manual duplex this is where the pages are actually sent.
			result = fJob->Finish();
			if (!result)
				_ReportError();
		} else
			fJob->Cancel();
	}

	_Cleanup();
	return result;
}


void
SPLDriver::_ReportError()
{
	if (fErrorReported)
		return;
	fErrorReported = true;

	std::string text("Printing failed: ");
	if (!fTransportError.empty())
		text += fTransportError;
	else if (fJob != NULL && fJob->LastError() != NULL)
		text += fJob->LastError();
	else
		text += "unknown error";

	BAlert* alert = new BAlert("", text.c_str(), "OK");
	alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
	alert->Go();
}


void
SPLDriver::_Cleanup()
{
	delete fJob;
	fJob = NULL;
	delete fOutput;
	fOutput = NULL;
	delete fHalftone;
	fHalftone = NULL;
}
