/*
 * SPLDriver.h - the print job side of the Haiku SPL printer driver
 *
 * Distributed under the GNU General Public License, version 2.
 */
#ifndef SPL_DRIVER_H
#define SPL_DRIVER_H

#include <string>

#include "GraphicsDriver.h"

#include "spl.h"


class Halftone;


class SPLDriver : public GraphicsDriver {
public:
								SPLDriver(BMessage* message,
									PrinterData* printerData,
									const PrinterCap* printerCap);
	virtual						~SPLDriver();

			/*!	Sends data to the printer.  Returns false instead of throwing
				when the transport fails, and remembers why.
			*/
			bool				WriteToTransport(const void* buffer,
									size_t size);

protected:
	virtual	bool				StartDocument();
	virtual	bool				StartPage(int page);
	virtual	bool				NextBand(BBitmap* bitmap, BPoint* offset);
	virtual	bool				EndPage(int page);
	virtual	bool				EndDocument(bool success);

private:
	class TransportOutput;

			void				_InitSettings();
			void				_BlitLine(const uint8* line, int width,
									int x, int y);
			void				_ReportError();
			void				_Cleanup();

			const spl::Model*	fModel;
			spl::JobSettings	fSettings;
			spl::Page			fPage;
			spl::Job*			fJob;
			TransportOutput*	fOutput;
			Halftone*			fHalftone;

			int					fOriginX;	// where libprint's page starts
			int					fOriginY;	// within the full-sheet bitmap

			std::string			fUserName;
			std::string			fJobTitle;
			std::string			fTransportError;
			bool				fErrorReported;
};

#endif	// SPL_DRIVER_H
