/*
 * SPLCap.cpp - what the selected SPL printer model can do
 *
 * Part of the Haiku SPL printer driver.  Distributed under the GNU General
 * Public License, version 2.
 *
 * libprint asks a PrinterCap object which paper sizes, resolutions, trays
 * and so on to offer in its dialogs.  Here all of that comes from the model
 * table of the encoder core.  The model itself is chosen when the printer is
 * added: libprint's "protocol class" list is used as the list of models.
 */
#include "SPLCap.h"

#include <stdio.h>
#include <string.h>

#include "PrinterData.h"


const char* const SPLCap::kTonerSaveKey = "spl:toner_save";
const char* const SPLCap::kTonerDensityKey = "spl:toner_density";
const char* const SPLCap::kMediaTypeKey = "spl:media_type";
const char* const SPLCap::kPowerSaveKey = "spl:power_save";
const char* const SPLCap::kJamRecoveryKey = "spl:jam_recovery";
const char* const SPLCap::kHighAltitudeKey = "spl:high_altitude";
const char* const SPLCap::kSRTModeKey = "spl:srt_mode";
const char* const SPLCap::kDuplexBindingKey = "spl:duplex_binding";

// The model offered first and used when a printer has no (valid) model set.
static const char* const kDefaultModel = "ph3140";
static const char* const kDefaultPaper = "A4";


struct PaperMapping {
	const char*		id;			// spl::PaperSize::id
	JobData::Paper	paper;
};

static const PaperMapping kPaperMappings[] = {
	{ "Letter",		JobData::kLetter },
	{ "Legal",		JobData::kLegal },
	{ "A4",			JobData::kA4 },
	{ "Executive",	JobData::kExecutive },
	{ "Ledger",		JobData::kLedger },
	{ "A3",			JobData::kA3 },
	{ "Env10",		JobData::kEnv10 },
	{ "Monarch",	JobData::kEnvMonarch },
	{ "C5",			JobData::kEnvC5 },
	{ "DL",			JobData::kEnvDL },
	{ "B4",			JobData::kB4 },
	{ "B5",			JobData::kB5 },
	{ "EnvISOB5",	JobData::kEnvB5 },
	{ "Postcard",	JobData::kJapanesePostcard },
	{ "DoublePostcardRotated", JobData::kDBLJapanesePostcardRotated },
	{ "A5",			JobData::kA5 },
	{ "A6",			JobData::kA6 },
	{ "B6",			JobData::kB6JIS },
	{ "C6",			JobData::kEnvC6 },
	{ "Folio",		JobData::kFolio },
	{ "EnvPersonal", JobData::kEnvPersonal },
	{ "Env9",		JobData::kEnv9 },
	{ "Oficio",		JobData::kUserDefined }
};

static const int kPaperMappingCount
	= sizeof(kPaperMappings) / sizeof(kPaperMappings[0]);


static JobData::PaperSource
PaperSourceFor(uint8 code)
{
	switch (code) {
		case 1:
			return JobData::kAuto;
		case 2:
			return JobData::kManual;
		case 4:
			return JobData::kUpper;
		case 5:
			return JobData::kLower;
		default:
			// multi-purpose tray, envelope tray, tray 3
			return (JobData::PaperSource)(JobData::kUser + code);
	}
}


static ListItemCap*
NewItem(const char* label, const char* key, bool isDefault, int32 id)
{
	ListItemCap* item = new ListItemCap(label, isDefault, id);
	item->fKey = key;
	return item;
}


// #pragma mark - conversions


const spl::Model*
SPLCap::ModelForProtocolClass(int protocolClass)
{
	const spl::Model* model = spl::FindModelByNumber(protocolClass);
	if (model == NULL)
		model = spl::FindModel(kDefaultModel);
	if (model == NULL)
		model = spl::ModelAt(0);
	return model;
}


const spl::PaperSize*
SPLCap::PaperSizeFor(JobData::Paper paper)
{
	for (int i = 0; i < kPaperMappingCount; i++) {
		if (kPaperMappings[i].paper == paper)
			return spl::FindPaperSize(kPaperMappings[i].id);
	}
	return spl::FindPaperSize(kDefaultPaper);
}


uint8
SPLCap::PaperSourceCodeFor(JobData::PaperSource source)
{
	for (int32 i = 0; i < spl::CountPaperSources(); i++) {
		const uint8 code = spl::PaperSourceAt(i)->code;
		if (PaperSourceFor(code) == source)
			return code;
	}
	return 1;	// automatic selection
}


void
SPLCap::GetMargins(const spl::Model& model, int32& leftRight, int32& topBottom)
{
	leftRight = 0;
	topBottom = 0;

	spl::JobSettings settings;
	spl::InitJobSettings(settings, model);

	for (int i = 0; i < model.resolutionCount; i++) {
		settings.xResolution = model.resolutions[i].x;
		settings.yResolution = model.resolutions[i].y;

		spl::PageGeometry geometry;
		spl::GetPageGeometry(model, settings, geometry);

		// hardware margin in points, rounded up
		const int32 x = (int32)((geometry.marginLeft * 72
			+ settings.xResolution - 1) / settings.xResolution);
		const int32 y = (int32)((geometry.marginTop * 72
			+ settings.yResolution - 1) / settings.yResolution);
		if (x > leftRight)
			leftRight = x;
		if (y > topBottom)
			topBottom = y;
	}
}


// #pragma mark - SPLCap


SPLCap::SPLCap(const PrinterData* printerData)
	:
	PrinterCap(printerData),
	fBuiltFor(-1)
{
	_BuildProtocolClasses();
}


SPLCap::~SPLCap()
{
	_Clear(fProtocolClasses);
	_Clear(fPapers);
	_Clear(fPaperSources);
	_Clear(fResolutions);
	_Clear(fColors);
	_Clear(fPrintStyles);
	_Clear(fOptions);
	_Clear(fTonerSave);
	_Clear(fTonerDensity);
	_Clear(fMediaType);
	_Clear(fPowerSave);
	_Clear(fJamRecovery);
	_Clear(fHighAltitude);
	_Clear(fSRTMode);
	_Clear(fDuplexBinding);
}


int
SPLCap::CountCap(CapID category) const
{
	const CapList* list = _ListFor(category);
	return list != NULL ? (int)list->size() : 0;
}


const BaseCap**
SPLCap::GetCaps(CapID category) const
{
	const CapList* list = _ListFor(category);
	if (list == NULL || list->empty())
		return NULL;
	return (const BaseCap**)&(*list)[0];
}


bool
SPLCap::Supports(CapID category) const
{
	switch (category) {
		case kPaper:
		case kPaperSource:
		case kResolution:
		case kColor:
		case kProtocolClass:
		case kDriverSpecificCapabilities:
		case kHalftone:
			return true;

		case kCopyCommand:
			// the page header carries a copy count
			return true;

		case kPrintStyle:
			// only worth showing if there is more than "simplex"
			return CountCap(kPrintStyle) > 1;

		default:
			return false;
	}
}


/*!	Returns the list for a category, (re)building the model dependent lists
	if the printer's model is not the one they were built for.  The model is
	not known yet while the printer is being added, hence the lazy approach.
*/
const SPLCap::CapList*
SPLCap::_ListFor(CapID category) const
{
	if (category == kProtocolClass)
		return &fProtocolClasses;

	_Update();

	switch ((int)category) {
		case kPaper:
			return &fPapers;
		case kPaperSource:
			return &fPaperSources;
		case kResolution:
			return &fResolutions;
		case kColor:
			return &fColors;
		case kPrintStyle:
			return &fPrintStyles;
		case kDriverSpecificCapabilities:
			return &fOptions;
		case kTonerSave:
			return &fTonerSave;
		case kTonerDensity:
			return &fTonerDensity;
		case kMediaType:
			return &fMediaType;
		case kPowerSave:
			return &fPowerSave;
		case kJamRecovery:
			return &fJamRecovery;
		case kHighAltitude:
			return &fHighAltitude;
		case kSRTMode:
			return &fSRTMode;
		case kDuplexBinding:
			return &fDuplexBinding;
		default:
			return NULL;
	}
}


void
SPLCap::_Update() const
{
	const int protocolClass = GetProtocolClass();
	if (protocolClass == fBuiltFor)
		return;

	const spl::Model* model = ModelForProtocolClass(protocolClass);
	if (model == NULL)
		return;

	SPLCap* self = const_cast<SPLCap*>(this);
	self->_BuildFor(*model);
	self->fBuiltFor = protocolClass;
}


void
SPLCap::_Clear(CapList& list)
{
	for (size_t i = 0; i < list.size(); i++)
		delete list[i];
	list.clear();
}


/*!	One entry per supported printer model; shown when a printer is added. */
void
SPLCap::_BuildProtocolClasses()
{
	for (int32 i = 0; i < spl::CountModels(); i++) {
		const spl::Model* model = spl::ModelAt(i);

		string label(model->manufacturer);
		label += " ";
		label += model->name;

		string description(label);
		description += "\n\nResolutions: ";
		for (int r = 0; r < model->resolutionCount; r++) {
			char text[32];
			if (model->resolutions[r].x == model->resolutions[r].y) {
				snprintf(text, sizeof(text), "%s%d", r > 0 ? ", " : "",
					(int)model->resolutions[r].x);
			} else {
				snprintf(text, sizeof(text), "%s%d x %d", r > 0 ? ", " : "",
					(int)model->resolutions[r].x,
					(int)model->resolutions[r].y);
			}
			description += text;
		}
		description += " dpi\nDuplex: ";
		if (model->duplex == spl::kDuplexCapAuto)
			description += "automatic";
		else if (model->duplex == spl::kDuplexCapManual)
			description += "manual (the driver sorts the pages)";
		else
			description += "no";

		if (model->verified) {
			description += "\n\nThis model has been tested with this driver.";
		} else {
			description += "\n\nNot yet tested on a real printer with this "
				"driver. The settings for this model are taken from the "
				"SpliX driver for Linux.";
		}

		fProtocolClasses.push_back(new ProtocolClassCap(label,
			strcmp(model->id, kDefaultModel) == 0,
			spl::ModelNumber(*model), description));
	}
}


void
SPLCap::_AddOption(const char* label, const char* key, int32 category,
	DriverSpecificCap::Type type)
{
	DriverSpecificCap* option = new DriverSpecificCap(label, category, type);
	option->fKey = key;
	fOptions.push_back(option);
}


void
SPLCap::_BuildFor(const spl::Model& model)
{
	_Clear(fPapers);
	_Clear(fPaperSources);
	_Clear(fResolutions);
	_Clear(fColors);
	_Clear(fPrintStyles);
	_Clear(fOptions);
	_Clear(fTonerSave);
	_Clear(fTonerDensity);
	_Clear(fMediaType);
	_Clear(fPowerSave);
	_Clear(fJamRecovery);
	_Clear(fHighAltitude);
	_Clear(fSRTMode);
	_Clear(fDuplexBinding);

	// Paper sizes.  The printable ("physical") rectangle leaves out the
	// hardware margins.
	int32 marginX;
	int32 marginY;
	GetMargins(model, marginX, marginY);

	for (int i = 0; i < kPaperMappingCount; i++) {
		const spl::PaperSize* size = spl::FindPaperSize(kPaperMappings[i].id);
		if (size == NULL || size->width <= 2 * marginX
			|| size->height <= 2 * marginY) {
			continue;
		}
		const BRect paperRect(0.0f, 0.0f, size->width, size->height);
		const BRect physicalRect(marginX, marginY, size->width - marginX,
			size->height - marginY);
		fPapers.push_back(new PaperCap(size->label,
			strcmp(size->id, kDefaultPaper) == 0, kPaperMappings[i].paper,
			paperRect, physicalRect));
	}

	// Paper sources
	for (int i = 0; i < model.sourceCount; i++) {
		const spl::PaperSource* source
			= spl::FindPaperSourceByCode(model.sources[i]);
		if (source == NULL)
			continue;
		fPaperSources.push_back(new PaperSourceCap(source->label, i == 0,
			PaperSourceFor(source->code)));
	}

	// Resolutions
	for (int i = 0; i < model.resolutionCount; i++) {
		const int x = model.resolutions[i].x;
		const int y = model.resolutions[i].y;
		char label[32];
		if (x == y)
			snprintf(label, sizeof(label), "%d DPI", x);
		else
			snprintf(label, sizeof(label), "%d x %d DPI", x, y);
		fResolutions.push_back(new ResolutionCap(label, i == 0,
			x * 10000 + y, x, y));
	}

	fColors.push_back(new ColorCap("Black and white", true,
		JobData::kMonochrome));

	fPrintStyles.push_back(new PrintStyleCap("Simplex", true,
		JobData::kSimplex));
	if (model.duplex == spl::kDuplexCapAuto) {
		fPrintStyles.push_back(new PrintStyleCap("Duplex", false,
			JobData::kDuplex));
	} else if (model.duplex == spl::kDuplexCapManual) {
		fPrintStyles.push_back(new PrintStyleCap("Duplex (manual)", false,
			JobData::kDuplex));
	}

	// Driver specific settings
	_AddOption("Paper type", kMediaTypeKey, kMediaType,
		DriverSpecificCap::kList);
	for (int i = 0; i < spl::kMediaTypeCount; i++) {
		fMediaType.push_back(NewItem(spl::kMediaTypes[i].label,
			spl::kMediaTypes[i].value, i == 0, i));
	}

	_AddOption("Toner save", kTonerSaveKey, kTonerSave,
		DriverSpecificCap::kList);
	fTonerSave.push_back(NewItem("Printer default", "default", true, 0));
	fTonerSave.push_back(NewItem("On", "on", false, 1));
	fTonerSave.push_back(NewItem("Off", "off", false, 2));

	_AddOption("Toner density", kTonerDensityKey, kTonerDensity,
		DriverSpecificCap::kList);
	fTonerDensity.push_back(NewItem("Light", "1", false, 1));
	fTonerDensity.push_back(NewItem("Medium", "3", true, 3));
	fTonerDensity.push_back(NewItem("Dark", "5", false, 5));

	if (model.hasSRTMode) {
		_AddOption("Resolution enhancement", kSRTModeKey, kSRTMode,
			DriverSpecificCap::kList);
		for (int i = 0; i < spl::kSRTModeCount; i++) {
			fSRTMode.push_back(NewItem(spl::kSRTModes[i].label,
				spl::kSRTModes[i].value, i == 0, i));
		}
	}

	if (model.duplex != spl::kDuplexCapNone) {
		_AddOption("Duplex binding", kDuplexBindingKey, kDuplexBinding,
			DriverSpecificCap::kList);
		fDuplexBinding.push_back(NewItem("Long edge", "long", true, 0));
		fDuplexBinding.push_back(NewItem("Short edge", "short", false, 1));
	}

	_AddOption("Power save after", kPowerSaveKey, kPowerSave,
		DriverSpecificCap::kList);
	for (int i = 0; i < spl::kPowerSaveCount; i++) {
		const int minutes = spl::kPowerSaveMinutes[i];
		char key[16];
		char label[32];
		snprintf(key, sizeof(key), "%d", minutes);
		if (minutes == 0)
			snprintf(label, sizeof(label), "Never");
		else if (minutes == 60)
			snprintf(label, sizeof(label), "1 hour");
		else
			snprintf(label, sizeof(label), "%d minutes", minutes);
		fPowerSave.push_back(NewItem(label, key, minutes == 5, minutes));
	}

	_AddOption("Reprint page after paper jam", kJamRecoveryKey, kJamRecovery,
		DriverSpecificCap::kBoolean);
	fJamRecovery.push_back(new BooleanCap("Reprint page after paper jam",
		false));

	_AddOption("High altitude (low air pressure)", kHighAltitudeKey,
		kHighAltitude, DriverSpecificCap::kBoolean);
	fHighAltitude.push_back(new BooleanCap("High altitude (low air pressure)",
		false));
}
