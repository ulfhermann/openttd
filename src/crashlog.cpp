/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file crashlog.cpp Implementation of generic function to be called to log a crash */

#include "stdafx.h"
#include "crashlog.h"
#include "gamelog.h"
#include "date_func.h"
#include "map_func.h"
#include "rev.h"
#include "strings_func.h"
#include "blitter/factory.hpp"
#include "base_media_base.h"
#include "music/music_driver.hpp"
#include "sound/sound_driver.hpp"
#include "video/video_driver.hpp"
#include "saveload/saveload.h"
#include "screenshot.h"
#include "gfx_func.h"

#include <squirrel.h>
#include "ai/ai_info.hpp"
#include "company_base.h"

#include <time.h>

/* static */ const char *CrashLog::message = NULL;
/* static */ char *CrashLog::gamelog_buffer = NULL;
/* static */ const char *CrashLog::gamelog_last = NULL;

/* virtual */ char *CrashLog::LogRegisters(char *buffer, const char *last) const
{
	/* Stub implementation; not all OSes support this. */
	return buffer;
}

/* virtual */ char *CrashLog::LogModules(char *buffer, const char *last) const
{
	/* Stub implementation; not all OSes support this. */
	return buffer;
}

char *CrashLog::LogOpenTTDVersion(char *buffer, const char *last) const
{
	return buffer + seprintf(buffer, last,
			"OpenTTD version:\n"
			" Version:    %s (%d)\n"
			" NewGRF ver: %08x\n"
			" Bits:       %d\n"
			" Endian:     %s\n"
			" Dedicated:  %s\n"
			" Build date: %s\n\n",
			_openttd_revision,
			_openttd_revision_modified,
			_openttd_newgrf_version,
#ifdef _SQ64
			64,
#else
			32,
#endif
#if (TTD_ENDIAN == TTD_LITTLE_ENDIAN)
			"little",
#else
			"big",
#endif
#ifdef DEDICATED
			"yes",
#else
			"no",
#endif
			_openttd_build_date
	);
}

char *CrashLog::LogConfiguration(char *buffer, const char *last) const
{
	buffer += seprintf(buffer, last,
			"Configuration:\n"
			" Blitter:      %s\n"
			" Graphics set: %s\n"
			" Language:     %s\n"
			" Music driver: %s\n"
			" Music set:    %s\n"
			" Sound driver: %s\n"
			" Sound set:    %s\n"
			" Video driver: %s\n\n",
			BlitterFactoryBase::GetCurrentBlitter() == NULL ? "none" : BlitterFactoryBase::GetCurrentBlitter()->GetName(),
			BaseGraphics::GetUsedSet() == NULL ? "none" : BaseGraphics::GetUsedSet()->name,
			StrEmpty(_dynlang.curr_file) ? "none" : _dynlang.curr_file,
			_music_driver == NULL ? "none" : _music_driver->GetName(),
			BaseMusic::GetUsedSet() == NULL ? "none" : BaseMusic::GetUsedSet()->name,
			_sound_driver == NULL ? "none" : _sound_driver->GetName(),
			BaseSounds::GetUsedSet() == NULL ? "none" : BaseSounds::GetUsedSet()->name,
			_video_driver == NULL ? "none" : _video_driver->GetName()
	);

	buffer += seprintf(buffer, last, "AI Configuration:\n");
	const Company *c;
	FOR_ALL_COMPANIES(c) {
		if (c->ai_info == NULL) {
			buffer += seprintf(buffer, last, " %2i: Human\n", (int)c->index);
		} else {
			buffer += seprintf(buffer, last, " %2i: %s (v%d)\n", (int)c->index, c->ai_info->GetName(), c->ai_info->GetVersion());
		}
	}
	buffer += seprintf(buffer, last, "\n");

	return buffer;
}

/* Include these here so it's close to where it's actually used. */
#ifdef WITH_ALLEGRO
#	include <allegro.h>
#endif /* WITH_ALLEGRO */
#ifdef WITH_FONTCONFIG
#	include <fontconfig/fontconfig.h>
#endif /* WITH_FONTCONFIG */
#ifdef WITH_PNG
	/* pngconf.h, included by png.h doesn't like something in the
	 * freetype headers. As such it's not alphabetically sorted. */
#	include <png.h>
#endif /* WITH_PNG */
#ifdef WITH_FREETYPE
#	include <ft2build.h>
#	include FT_FREETYPE_H
#endif /* WITH_FREETYPE */
#ifdef WITH_ICU
#	include <unicode/uversion.h>
#endif /* WITH_ICU */
#ifdef WITH_LZO
#include <lzo/lzo1x.h>
#endif
#ifdef WITH_SDL
#	include "sdl.h"
#	include <SDL.h>
#endif /* WITH_SDL */
#ifdef WITH_ZLIB
# include <zlib.h>
#endif

char *CrashLog::LogLibraries(char *buffer, const char *last) const
{
	buffer += seprintf(buffer, last, "Libraries:\n");

#ifdef WITH_ALLEGRO
	buffer += seprintf(buffer, last, " Allegro:    %s\n", allegro_id);
#endif /* WITH_ALLEGRO */

#ifdef WITH_FONTCONFIG
	int version = FcGetVersion();
	buffer += seprintf(buffer, last, " FontConfig: %d.%d.%d\n", version / 10000, (version / 100) % 100, version % 100);
#endif /* WITH_FONTCONFIG */

#ifdef WITH_FREETYPE
	FT_Library library;
	int major, minor, patch;
	FT_Init_FreeType(&library);
	FT_Library_Version(library, &major, &minor, &patch);
	FT_Done_FreeType(library);
	buffer += seprintf(buffer, last, " FreeType:   %d.%d.%d\n", major, minor, patch);
#endif /* WITH_FREETYPE */

#ifdef WITH_ICU
	/* 4 times 0-255, separated by dots (.) and a trailing '\0' */
	char buf[4 * 3 + 3 + 1];
	UVersionInfo ver;
	u_getVersion(ver);
	u_versionToString(ver, buf);
	buffer += seprintf(buffer, last, " ICU:        %s\n", buf);
#endif /* WITH_ICU */

#ifdef WITH_LZO
	buffer += seprintf(buffer, last, " LZO:        %s\n", lzo_version_string());
#endif

#ifdef WITH_PNG
	buffer += seprintf(buffer, last, " PNG:        %s\n", png_get_libpng_ver(NULL));
#endif /* WITH_PNG */

#ifdef WITH_SDL
#ifdef DYNAMICALLY_LOADED_SDL
	if (SDL_CALL SDL_Linked_Version != NULL) {
#else
	{
#endif
		const SDL_version *v = SDL_CALL SDL_Linked_Version();
		buffer += seprintf(buffer, last, " SDL:        %d.%d.%d\n", v->major, v->minor, v->patch);
	}
#endif /* WITH_SDL */

#ifdef WITH_ZLIB
	buffer += seprintf(buffer, last, " Zlib:       %s\n", zlibVersion());
#endif

	buffer += seprintf(buffer, last, "\n");
	return buffer;
}

/* static */ void CrashLog::GamelogFillCrashLog(const char *s)
{
	CrashLog::gamelog_buffer += seprintf(CrashLog::gamelog_buffer, CrashLog::gamelog_last, "%s\n", s);
}

char *CrashLog::LogGamelog(char *buffer, const char *last) const
{
	CrashLog::gamelog_buffer = buffer;
	CrashLog::gamelog_last = last;
	GamelogPrint(&CrashLog::GamelogFillCrashLog);
	return CrashLog::gamelog_buffer + seprintf(CrashLog::gamelog_buffer, last, "\n");
}

char *CrashLog::FillCrashLog(char *buffer, const char *last) const
{
	time_t cur_time = time(NULL);
	buffer += seprintf(buffer, last, "*** OpenTTD Crash Report ***\n\n");
	buffer += seprintf(buffer, last, "Crash at: %s", asctime(gmtime(&cur_time)));

	YearMonthDay ymd;
	ConvertDateToYMD(_date, &ymd);
	buffer += seprintf(buffer, last, "In game date: %i-%02i-%02i (%i)\n\n", ymd.year, ymd.month + 1, ymd.day, _date_fract);

	buffer = this->LogError(buffer, last, CrashLog::message);
	buffer = this->LogOpenTTDVersion(buffer, last);
	buffer = this->LogRegisters(buffer, last);
	buffer = this->LogStacktrace(buffer, last);
	buffer = this->LogOSVersion(buffer, last);
	buffer = this->LogConfiguration(buffer, last);
	buffer = this->LogLibraries(buffer, last);
	buffer = this->LogModules(buffer, last);
	buffer = this->LogGamelog(buffer, last);

	buffer += seprintf(buffer, last, "*** End of OpenTTD Crash Report ***\n");
	return buffer;
}

bool CrashLog::WriteCrashLog(const char *buffer, char *filename, const char *filename_last) const
{
	seprintf(filename, filename_last, "%scrash.log", _personal_dir);

	FILE *file = FioFOpenFile(filename, "w", NO_DIRECTORY);
	if (file == NULL) return false;

	size_t len = strlen(buffer);
	size_t written = fwrite(buffer, 1, len, file);

	FioFCloseFile(file);
	return len == written;
}

/* virtual */ int CrashLog::WriteCrashDump(char *filename, const char *filename_last) const
{
	/* Stub implementation; not all OSes support this. */
	return 0;
}

bool CrashLog::WriteSavegame(char *filename, const char *filename_last) const
{
	/* If the map array doesn't exist, saving will fail too. If the map got
	 * initialised, there is a big chance the rest is initialised too. */
	if (_m == NULL) return false;

	try {
		GamelogEmergency();

		seprintf(filename, filename_last, "%scrash.sav", _personal_dir);

		/* Don't do a threaded saveload. */
		return SaveOrLoad(filename, SL_SAVE, NO_DIRECTORY, false) == SL_OK;
	} catch (...) {
		return false;
	}
}

bool CrashLog::WriteScreenshot(char *filename, const char *filename_last) const
{
	/* Don't draw when we have invalid screen size */
	if (_screen.width < 1 || _screen.height < 1 || _screen.dst_ptr == NULL) return false;

	bool res = MakeScreenshot(SC_RAW, "crash");
	if (res) strecpy(filename, _full_screenshot_name, filename_last);
	return res;
}

bool CrashLog::MakeCrashLog() const
{
	/* Don't keep looping logging crashes. */
	static bool crashlogged = false;
	if (crashlogged) return false;
	crashlogged = true;

	char filename[MAX_PATH];
	char buffer[65536];
	bool ret = true;

	printf("Crash encountered, generating crash log...\n");
	this->FillCrashLog(buffer, lastof(buffer));
	printf("%s\n", buffer);
	printf("Crash log generated.\n\n");

	printf("Writing crash log to disk...\n");
	bool bret = this->WriteCrashLog(buffer, filename, lastof(filename));
	if (bret) {
		printf("Crash log written to %s. Please add this file to any bug reports.\n\n", filename);
	} else {
		printf("Writing crash log failed. Please attach the output above to any bug reports.\n\n");
		ret = false;
	}

	/* Don't mention writing crash dumps because not all platforms support it. */
	int dret = this->WriteCrashDump(filename, lastof(filename));
	if (dret < 0) {
		printf("Writing crash dump failed.\n\n");
		ret = false;
	} else if (dret > 0) {
		printf("Crash dump written to %s. Please add this file to any bug reports.\n\n", filename);
	}

	printf("Writing crash savegame...\n");
	bret = this->WriteSavegame(filename, lastof(filename));
	if (bret) {
		printf("Crash savegame written to %s. Please add this file and the last (auto)save to any bug reports.\n\n", filename);
	} else {
		ret = false;
		printf("Writing crash savegame failed. Please attach the last (auto)save to any bug reports.\n\n");
	}

	printf("Writing crash screenshot...\n");
	bret = this->WriteScreenshot(filename, lastof(filename));
	if (bret) {
		printf("Crash screenshot written to %s. Please add this file to any bug reports.\n\n", filename);
	} else {
		ret = false;
		printf("Writing crash screenshot failed.\n\n");
	}

	return ret;
}

/* static */ void CrashLog::SetErrorMessage(const char *message)
{
	CrashLog::message = message;
}

/* static */ void CrashLog::AfterCrashLogCleanup()
{
	if (_music_driver != NULL) _music_driver->Stop();
	if (_sound_driver != NULL) _sound_driver->Stop();
	if (_video_driver != NULL) _video_driver->Stop();
}
