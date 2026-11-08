/*
 * SPLCap.h - what the selected SPL printer model can do
 *
 * Part of the Haiku SPL printer driver.  Distributed under the GNU General
 * Public License, version 2.
 */
#ifndef SPL_CAP_H
#define SPL_CAP_H

#include <vector>

#include "PrinterCap.h"

#include "spl.h"


class SPLCap : public PrinterCap {
public:
								SPLCap(const PrinterData* printerData);
	virtual						~SPLCap();

	virtual	int					CountCap(CapID category) const;
	virtual	bool				Supports(CapID category) const;
	virtual	const BaseCap**		GetCaps(CapID category) const;

	// Driver specific job settings, shown in the job setup dialog.
	enum {
		kTonerSave = kDriverSpecificCapabilitiesBegin,
		kTonerDensity,
		kMediaType,
		kPowerSave,
		kJamRecovery,
		kHighAltitude,
		kSRTMode,
		kDuplexBinding
	};

	// Keys under which those settings are stored in the job data.
	static	const char* const	kTonerSaveKey;
	static	const char* const	kTonerDensityKey;
	static	const char* const	kMediaTypeKey;
	static	const char* const	kPowerSaveKey;
	static	const char* const	kJamRecoveryKey;
	static	const char* const	kHighAltitudeKey;
	static	const char* const	kSRTModeKey;
	static	const char* const	kDuplexBindingKey;

	// Conversions between libprint's identifiers and the encoder's.
	static	const spl::Model*	ModelForProtocolClass(int protocolClass);
	static	const spl::PaperSize* PaperSizeFor(JobData::Paper paper);
	static	uint8				PaperSourceCodeFor(
									JobData::PaperSource source);

	/*!	Border, in whole points, that libprint must keep clear on each side
		so that nothing is rendered into the printer's hardware margins at
		any of the model's resolutions.
	*/
	static	void				GetMargins(const spl::Model& model,
									int32& leftRight, int32& topBottom);

private:
	typedef std::vector<BaseCap*> CapList;

			void				_Update() const;
			void				_BuildProtocolClasses();
			void				_BuildFor(const spl::Model& model);
			void				_AddOption(const char* label, const char* key,
									int32 category,
									DriverSpecificCap::Type type);
			const CapList*		_ListFor(CapID category) const;
	static	void				_Clear(CapList& list);

			int					fBuiltFor;	// protocol class, -1 = nothing yet

			CapList				fProtocolClasses;
			CapList				fPapers;
			CapList				fPaperSources;
			CapList				fResolutions;
			CapList				fColors;
			CapList				fPrintStyles;
			CapList				fOptions;
			CapList				fTonerSave;
			CapList				fTonerDensity;
			CapList				fMediaType;
			CapList				fPowerSave;
			CapList				fJamRecovery;
			CapList				fHighAltitude;
			CapList				fSRTMode;
			CapList				fDuplexBinding;
};

#endif	// SPL_CAP_H
