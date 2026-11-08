/*
 * SaveToDesktop.cpp - "Save To Desktop" transport add-on
 *
 * A connection type for testing printer drivers without a printer: every
 * print job is written to a new file on the Desktop, named
 * spl-job-<date>-<time>.prn.  Unlike Haiku's "Print To File" it does not
 * open a save dialog, so it needs no user interaction while the job runs.
 *
 * Part of the Haiku SPL printer driver.  Distributed under the GNU General
 * Public License, version 2.
 */
#include <stdio.h>
#include <time.h>

#include <File.h>
#include <FindDirectory.h>
#include <Message.h>
#include <Path.h>


static BFile* sFile = NULL;


extern "C" _EXPORT void
exit_transport()
{
	delete sFile;
	sFile = NULL;
}


extern "C" _EXPORT BDataIO*
init_transport(BMessage* message)
{
	BPath path;
	if (find_directory(B_DESKTOP_DIRECTORY, &path) != B_OK)
		path.SetTo("/boot/home/Desktop");

	char name[64];
	time_t now = time(NULL);
	struct tm local;
	if (localtime_r(&now, &local) == NULL
		|| strftime(name, sizeof(name), "spl-job-%Y%m%d-%H%M%S.prn",
			&local) == 0) {
		snprintf(name, sizeof(name), "spl-job.prn");
	}
	path.Append(name);

	delete sFile;
	sFile = new BFile(path.Path(),
		B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (sFile->InitCheck() != B_OK) {
		delete sFile;
		sFile = NULL;
		return NULL;
	}

	if (message != NULL) {
		message->what = 'okok';
		message->AddString("path", path.Path());
	}
	return sFile;
}
