/*
 * spl_models.cpp - printer model, paper and option tables
 *
 * The data comes from SpliX's driver definitions (its ppd/ directory and
 * src/printer.cpp), (C) 2006-2008 Aurelien Croc (AP2C) and contributors.
 * Distributed under the GNU General Public License, version 2.
 */
#include "spl.h"

#include <string.h>
#include <strings.h>

namespace spl {


#include "spl_models_generated.inc"


// Paper sizes: QPDL paper codes from SpliX's printer.cpp, dimensions from
// its PPD files (whole points, exactly as SpliX uses them).
static const PaperSize kPaperSizes[] = {
	{ "Letter",		"US Letter",			0,	612,	792 },
	{ "Legal",		"US Legal",				1,	612,	1008 },
	{ "A4",			"A4",					2,	595,	842 },
	{ "Executive",	"Executive",			3,	522,	756 },
	{ "Ledger",		"US Ledger",			4,	1224,	792 },
	{ "A3",			"A3",					5,	842,	1191 },
	{ "Env10",		"Envelope #10",			6,	297,	684 },
	{ "Monarch",	"Envelope Monarch",		7,	279,	540 },
	{ "C5",			"Envelope C5",			8,	459,	649 },
	{ "DL",			"Envelope DL",			9,	312,	624 },
	{ "B4",			"JIS B4",				10,	729,	1032 },
	{ "B5",			"JIS B5",				11,	516,	729 },
	{ "EnvISOB5",	"Envelope B5",			12,	499,	709 },
	{ "Postcard",	"Postcard",				14,	284,	419 },
	{ "DoublePostcardRotated", "Postcard Double Long Edge", 15, 420, 567 },
	{ "A5",			"A5",					16,	420,	595 },
	{ "A6",			"A6",					17,	297,	420 },
	{ "B6",			"JIS B6",				18,	363,	516 },
	{ "C6",			"Envelope C6",			23,	323,	459 },
	{ "Folio",		"Folio",				24,	595,	935 },
	{ "EnvPersonal", "Envelope Personal",	25,	261,	468 },
	{ "Env9",		"Envelope #9",			26,	279,	639 },
	{ "Oficio",		"Oficio (216 x 340 mm)", 28, 612,	972 }
};

static const PaperSource kPaperSources[] = {
	{ "Auto",		"Automatic selection",	1 },
	{ "Manual",		"Manual feed",			2 },
	{ "Multi",		"Multi-purpose tray",	3 },
	{ "Upper",		"Tray 1",				4 },
	{ "Lower",		"Tray 2",				5 },
	{ "Envelope",	"Envelope tray",		6 },
	{ "Tray3",		"Tray 3",				7 }
};

const Choice kMediaTypes[] = {
	{ "OFF",		"Printer default" },
	{ "NORMAL",		"Plain paper" },
	{ "THICK",		"Thick paper" },
	{ "THIN",		"Thin paper" },
	{ "BOND",		"Bond" },
	{ "OHP",		"Transparency" },
	{ "CARD",		"Card stock" },
	{ "LABEL",		"Label" },
	{ "USED",		"Preprinted" },
	{ "COLOR",		"Colored paper" },
	{ "ENV",		"Envelope" },
	{ "COTTON",		"Cotton" },
	{ "RECYCLED",	"Recycled" },
	{ "ARCHIVE",	"Archive" }
};
const int kMediaTypeCount = sizeof(kMediaTypes) / sizeof(kMediaTypes[0]);

const Choice kSRTModes[] = {
	{ "NORMAL",		"Normal" },
	{ "ENHANCE",	"Text enhance" },
	{ "GRAY",		"Image enhance" }
};
const int kSRTModeCount = sizeof(kSRTModes) / sizeof(kSRTModes[0]);

const int kPowerSaveMinutes[] = { 0, 5, 10, 15, 30, 45, 60 };
const int kPowerSaveCount
	= sizeof(kPowerSaveMinutes) / sizeof(kPowerSaveMinutes[0]);

const int kTonerDensities[] = { 1, 3, 5 };
const int kTonerDensityCount
	= sizeof(kTonerDensities) / sizeof(kTonerDensities[0]);


// #pragma mark - lookups


int32_t
CountModels()
{
	return (int32_t)(sizeof(kModels) / sizeof(kModels[0]));
}


const Model*
ModelAt(int32_t index)
{
	if (index < 0 || index >= CountModels())
		return NULL;
	return &kModels[index];
}


const Model*
FindModel(const char* id)
{
	if (id == NULL)
		return NULL;

	for (int32_t i = 0; i < CountModels(); i++) {
		const Model& model = kModels[i];
		if (strcasecmp(id, model.id) == 0)
			return &model;

		// "Manufacturer Name"
		const size_t length = strlen(model.manufacturer);
		if (strncasecmp(id, model.manufacturer, length) == 0
			&& id[length] == ' '
			&& strcasecmp(id + length + 1, model.name) == 0) {
			return &model;
		}
	}
	return NULL;
}


int32_t
ModelNumber(const Model& model)
{
	// FNV-1a over the id, folded to a positive, non-zero 31 bit value
	uint32_t hash = 2166136261u;
	for (const char* c = model.id; *c != '\0'; c++) {
		hash ^= (uint8_t)*c;
		hash *= 16777619u;
	}
	hash &= 0x7fffffff;
	return hash != 0 ? (int32_t)hash : 1;
}


const Model*
FindModelByNumber(int32_t number)
{
	for (int32_t i = 0; i < CountModels(); i++) {
		if (ModelNumber(kModels[i]) == number)
			return &kModels[i];
	}
	return NULL;
}


int32_t
CountPaperSizes()
{
	return (int32_t)(sizeof(kPaperSizes) / sizeof(kPaperSizes[0]));
}


const PaperSize*
PaperSizeAt(int32_t index)
{
	if (index < 0 || index >= CountPaperSizes())
		return NULL;
	return &kPaperSizes[index];
}


const PaperSize*
FindPaperSize(const char* id)
{
	if (id == NULL)
		return NULL;
	for (int32_t i = 0; i < CountPaperSizes(); i++) {
		if (strcasecmp(id, kPaperSizes[i].id) == 0)
			return &kPaperSizes[i];
	}
	return NULL;
}


const PaperSize*
FindPaperSizeByCode(uint8_t code)
{
	for (int32_t i = 0; i < CountPaperSizes(); i++) {
		if (kPaperSizes[i].code == code)
			return &kPaperSizes[i];
	}
	return NULL;
}


int32_t
CountPaperSources()
{
	return (int32_t)(sizeof(kPaperSources) / sizeof(kPaperSources[0]));
}


const PaperSource*
PaperSourceAt(int32_t index)
{
	if (index < 0 || index >= CountPaperSources())
		return NULL;
	return &kPaperSources[index];
}


const PaperSource*
FindPaperSource(const char* id)
{
	if (id == NULL)
		return NULL;
	for (int32_t i = 0; i < CountPaperSources(); i++) {
		if (strcasecmp(id, kPaperSources[i].id) == 0)
			return &kPaperSources[i];
	}
	return NULL;
}


const PaperSource*
FindPaperSourceByCode(uint8_t code)
{
	for (int32_t i = 0; i < CountPaperSources(); i++) {
		if (kPaperSources[i].code == code)
			return &kPaperSources[i];
	}
	return NULL;
}


}	// namespace spl
