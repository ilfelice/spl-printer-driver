/*
 * SPLEntry.cpp - entry point of the Haiku SPL printer driver add-on
 *
 * Distributed under the GNU General Public License, version 2.
 */
#include "PrinterDriver.h"

#include "SPLCap.h"
#include "SPLDriver.h"


class SPLPrinterDriver : public PrinterDriver {
public:
	SPLPrinterDriver(BNode* printerFolder)
		:
		PrinterDriver(printerFolder)
	{
	}

	const char* GetSignature() const
	{
		return "application/x-vnd.spl-compatible";
	}

	const char* GetDriverName() const
	{
		return "SPL compatible";
	}

	const char* GetVersion() const
	{
		return "0.3";
	}

	const char* GetCopyright() const
	{
		return "SPL driver for Samsung, Xerox, Dell and HP laser printers "
			"that speak SPL2/QPDL.\n"
			"Based on SpliX, Copyright © 2006-2008 Aurélien Croc and "
			"contributors.\n"
			"Licensed under the GNU General Public License, version 2.\n";
	}

	PrinterCap* InstantiatePrinterCap(PrinterData* printerData)
	{
		return new SPLCap(printerData);
	}

	GraphicsDriver* InstantiateGraphicsDriver(BMessage* settings,
		PrinterData* printerData, PrinterCap* printerCap)
	{
		return new SPLDriver(settings, printerData, printerCap);
	}
};


PrinterDriver*
instantiate_printer_driver(BNode* printerFolder)
{
	return new SPLPrinterDriver(printerFolder);
}
