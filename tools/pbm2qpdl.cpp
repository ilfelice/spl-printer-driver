/*
 * pbm2qpdl - turn 1-bit PBM pages into a QPDL print job
 *
 * Command line front end for the encoder core.  It serves two purposes:
 *   - regression testing: tests/compare.sh feeds the same pages to SpliX's
 *     rastertoqpdl filter and to this tool and requires identical output;
 *   - hardware testing without the Haiku driver: the output file can be sent
 *     straight to a printer (e.g. "cat job.prn > /dev/usb/lp0" on Linux).
 *
 * Distributed under the GNU General Public License, version 2.
 */
#include "spl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

using namespace spl;


class FileOutput : public Output {
public:
	FileOutput(FILE* file) : fFile(file) {}
	virtual bool Write(const void* buffer, size_t size)
	{
		return fwrite(buffer, 1, size, fFile) == size;
	}
private:
	FILE* fFile;
};


static uint8_t*
ReadPBM(const char* path, uint32_t& width, uint32_t& height)
{
	FILE* file = fopen(path, "rb");
	if (file == NULL) {
		perror(path);
		return NULL;
	}

	char magic[3] = { 0, 0, 0 };
	unsigned w = 0, h = 0;
	bool ok = fscanf(file, "%2s", magic) == 1 && strcmp(magic, "P4") == 0;
	while (ok) {
		// skip whitespace and comment lines in the header
		int c = fgetc(file);
		if (c == '#') {
			while ((c = fgetc(file)) != '\n' && c != EOF) {
			}
		} else if (c != ' ' && c != '\n' && c != '\r' && c != '\t') {
			ungetc(c, file);
			break;
		}
	}
	ok = ok && fscanf(file, "%u %u", &w, &h) == 2 && w > 0 && h > 0;
	if (ok)
		fgetc(file);

	uint8_t* data = NULL;
	if (ok) {
		const size_t size = (size_t)((w + 7) / 8) * h;
		data = (uint8_t*)malloc(size);
		if (data == NULL || fread(data, 1, size, file) != size) {
			free(data);
			data = NULL;
		}
	}
	fclose(file);

	if (data == NULL) {
		fprintf(stderr, "%s: not a readable raw PBM (P4) file\n", path);
		return NULL;
	}
	width = w;
	height = h;
	return data;
}


static void
Usage(const char* program)
{
	fprintf(stderr,
		"Usage: %s [options] page1.pbm [page2.pbm ...]\n"
		"\n"
		"  --model ID           printer model (default ph3140); see --list-models\n"
		"  --paper ID           Letter, A4, Legal, ... (default Letter)\n"
		"  --resolution XxY     e.g. 600x600 or 1200x600 (default: model's)\n"
		"  --source ID          Auto, Manual, Multi, Upper, Lower, ...\n"
		"  --media-type V       OFF, NORMAL, THICK, THIN, ...\n"
		"  --copies N\n"
		"  --duplex MODE        none, long, short\n"
		"  --toner-save V       default, on, off\n"
		"  --density N          1 (light) .. 5 (dark)\n"
		"  --power-save V       minutes, 'off', or 'keep'\n"
		"  --jam-recovery\n"
		"  --high-altitude\n"
		"  --srt V              NORMAL, ENHANCE, GRAY\n"
		"  --user NAME          --title TEXT\n"
		"  --date YYYYMMDD      date stamped into the job (default: today)\n"
		"  --raster-margins L,B clip the input to these margins, in points\n"
		"                       (default: 10,15 - what CUPS hands to SpliX)\n"
		"  --splix-quirks       reproduce SpliX even where it is slightly off\n"
		"  --output FILE        default: standard output\n"
		"  --list-models\n", program);
}


int
main(int argc, char** argv)
{
	const Model* model = FindModel("ph3140");
	const char* paperId = "Letter";
	const char* sourceId = NULL;
	const char* outputPath = NULL;
	const char* resolution = NULL;
	int marginLeft = 10, marginBottom = 15;
	bool quirks = false;

	JobSettings overrides;
	memset(&overrides, 0, sizeof(overrides));
	overrides.copies = 1;
	overrides.tonerDensity = 3;
	overrides.powerSave = 5;
	const char* duplex = "none";
	const char* tonerSave = "default";

	time_t now = time(NULL);
	struct tm* today = localtime(&now);
	unsigned date = (unsigned)(today->tm_year + 1900) * 10000
		+ (unsigned)(today->tm_mon + 1) * 100 + (unsigned)today->tm_mday;

	int i = 1;
	for (; i < argc && strncmp(argv[i], "--", 2) == 0; i++) {
		const char* option = argv[i];
		const char* value = i + 1 < argc ? argv[i + 1] : NULL;
#define TAKES_VALUE(name) (strcmp(option, name) == 0 && value != NULL && ++i)
		if (strcmp(option, "--list-models") == 0) {
			for (int32_t m = 0; m < CountModels(); m++) {
				const Model* entry = ModelAt(m);
				printf("%-10s %10d  %s %s%s\n", entry->id,
					(int)ModelNumber(*entry), entry->manufacturer,
					entry->name, entry->verified ? "" : "  (untested)");
			}
			return 0;
		} else if (strcmp(option, "--jam-recovery") == 0)
			overrides.jamRecovery = true;
		else if (strcmp(option, "--high-altitude") == 0)
			overrides.highAltitude = true;
		else if (strcmp(option, "--splix-quirks") == 0)
			quirks = true;
		else if (TAKES_VALUE("--model")) {
			model = FindModel(value);
			if (model == NULL) {
				fprintf(stderr, "unknown model '%s'\n", value);
				return 1;
			}
		} else if (TAKES_VALUE("--paper"))
			paperId = value;
		else if (TAKES_VALUE("--resolution"))
			resolution = value;
		else if (TAKES_VALUE("--source"))
			sourceId = value;
		else if (TAKES_VALUE("--media-type"))
			overrides.mediaType = value;
		else if (TAKES_VALUE("--copies"))
			overrides.copies = (uint16_t)atoi(value);
		else if (TAKES_VALUE("--duplex"))
			duplex = value;
		else if (TAKES_VALUE("--toner-save"))
			tonerSave = value;
		else if (TAKES_VALUE("--density"))
			overrides.tonerDensity = atoi(value);
		else if (TAKES_VALUE("--power-save")) {
			if (strcmp(value, "off") == 0)
				overrides.powerSave = 0;
			else if (strcmp(value, "keep") == 0)
				overrides.powerSave = -1;
			else
				overrides.powerSave = atoi(value);
		} else if (TAKES_VALUE("--srt"))
			overrides.srtMode = value;
		else if (TAKES_VALUE("--user"))
			overrides.userName = value;
		else if (TAKES_VALUE("--title"))
			overrides.jobTitle = value;
		else if (TAKES_VALUE("--date"))
			date = (unsigned)atoi(value);
		else if (TAKES_VALUE("--raster-margins")) {
			if (sscanf(value, "%d,%d", &marginLeft, &marginBottom) != 2) {
				fprintf(stderr, "--raster-margins wants LEFT,BOTTOM\n");
				return 1;
			}
		} else if (TAKES_VALUE("--output"))
			outputPath = value;
		else {
			Usage(argv[0]);
			return 1;
		}
#undef TAKES_VALUE
	}
	if (i >= argc) {
		Usage(argv[0]);
		return 1;
	}

	JobSettings settings;
	InitJobSettings(settings, *model);

	const PaperSize* paper = FindPaperSize(paperId);
	if (paper == NULL) {
		fprintf(stderr, "unknown paper size '%s'\n", paperId);
		return 1;
	}
	SetPaper(settings, *paper);

	if (sourceId != NULL) {
		const PaperSource* source = FindPaperSource(sourceId);
		if (source == NULL) {
			fprintf(stderr, "unknown paper source '%s'\n", sourceId);
			return 1;
		}
		settings.paperSource = source->code;
	}

	if (resolution != NULL) {
		unsigned x = 0, y = 0;
		const int fields = sscanf(resolution, "%ux%u", &x, &y);
		if (fields == 1)
			y = x;
		if (fields < 1 || x == 0 || y == 0) {
			fprintf(stderr, "bad resolution '%s'\n", resolution);
			return 1;
		}
		settings.xResolution = (uint16_t)x;
		settings.yResolution = (uint16_t)y;
	}

	if (strcmp(duplex, "long") == 0)
		settings.duplex = kDuplexLongEdge;
	else if (strcmp(duplex, "short") == 0)
		settings.duplex = kDuplexShortEdge;
	else if (strcmp(duplex, "none") != 0) {
		fprintf(stderr, "bad duplex mode '%s'\n", duplex);
		return 1;
	}

	if (strcmp(tonerSave, "on") == 0)
		settings.tonerSave = kTonerSaveOn;
	else if (strcmp(tonerSave, "off") == 0)
		settings.tonerSave = kTonerSaveOff;

	if (overrides.mediaType != NULL)
		settings.mediaType = overrides.mediaType;
	if (overrides.srtMode != NULL)
		settings.srtMode = overrides.srtMode;
	settings.copies = overrides.copies;
	settings.tonerDensity = overrides.tonerDensity;
	settings.powerSave = overrides.powerSave;
	settings.jamRecovery = overrides.jamRecovery;
	settings.highAltitude = overrides.highAltitude;
	settings.userName = overrides.userName;
	settings.jobTitle = overrides.jobTitle;
	settings.year = (uint16_t)(date / 10000);
	settings.month = (uint8_t)(date / 100 % 100);
	settings.day = (uint8_t)(date % 100);

	FILE* file = stdout;
	if (outputPath != NULL) {
		file = fopen(outputPath, "wb");
		if (file == NULL) {
			perror(outputPath);
			return 1;
		}
	}

	FileOutput output(file);
	Job job(*model, settings, output);
	job.SetSplixQuirks(quirks);

	Page page;
	bool ok = true;
	for (; ok && i < argc; i++) {
		uint32_t width, height;
		uint8_t* raster = ReadPBM(argv[i], width, height);
		if (raster == NULL) {
			ok = false;
			break;
		}
		ok = page.Allocate(*model, settings);
		if (ok) {
			page.PlaceRaster(raster, (width + 7) / 8, height,
				marginLeft * 1000, marginBottom * 1000, settings);
			ok = job.AddPage(page);
		}
		free(raster);
	}
	ok = ok && job.Finish();

	if (file != stdout)
		fclose(file);
	else
		fflush(stdout);

	if (!ok) {
		fprintf(stderr, "error: %s\n",
			job.LastError() != NULL ? job.LastError() : "failed");
		return 1;
	}
	if (job.DroppedBands() > 0) {
		fprintf(stderr, "warning: %u band(s) were not compressible and were "
			"left out\n", (unsigned)job.DroppedBands());
	}
	return 0;
}
