/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file debug.cpp Handling of printing debug messages. */

#include "stdafx.h"
#include <stdarg.h>
#include "console_func.h"
#include "debug.h"
#include "string_func.h"
#include "fileio_func.h"
#include "settings_type.h"

#include <time.h>

#if defined(ENABLE_NETWORK)
#include "network/core/os_abstraction.h"
SOCKET _debug_socket = INVALID_SOCKET;
#endif /* ENABLE_NETWORK */

int _debug_ai_level;
int _debug_driver_level;
int _debug_grf_level;
int _debug_map_level;
int _debug_misc_level;
int _debug_net_level;
int _debug_sprite_level;
int _debug_oldloader_level;
int _debug_npf_level;
int _debug_yapf_level;
int _debug_freetype_level;
int _debug_sl_level;
int _debug_gamelog_level;
int _debug_desync_level;
int _debug_console_level;


struct DebugLevel {
	const char *name;
	int *level;
};

#define DEBUG_LEVEL(x) { #x, &_debug_##x##_level }
	static const DebugLevel debug_level[] = {
	DEBUG_LEVEL(ai),
	DEBUG_LEVEL(driver),
	DEBUG_LEVEL(grf),
	DEBUG_LEVEL(map),
	DEBUG_LEVEL(misc),
	DEBUG_LEVEL(net),
	DEBUG_LEVEL(sprite),
	DEBUG_LEVEL(oldloader),
	DEBUG_LEVEL(npf),
	DEBUG_LEVEL(yapf),
	DEBUG_LEVEL(freetype),
	DEBUG_LEVEL(sl),
	DEBUG_LEVEL(gamelog),
	DEBUG_LEVEL(desync),
	DEBUG_LEVEL(console),
	};
#undef DEBUG_LEVEL

#if !defined(NO_DEBUG_MESSAGES)

static void debug_print(const char *dbg, const char *buf)
{
#if defined(ENABLE_NETWORK)
	if (_debug_socket != INVALID_SOCKET) {
		char buf2[1024 + 32];

		snprintf(buf2, lengthof(buf2), "%sdbg: [%s] %s\n", GetLogPrefix(), dbg, buf);
		send(_debug_socket, buf2, (int)strlen(buf2), 0);
		return;
	}
#endif /* ENABLE_NETWORK */
	if (strcmp(dbg, "desync") != 0) {
#if defined(WINCE)
		/* We need to do OTTD2FS twice, but as it uses a static buffer, we need to store one temporary */
		TCHAR tbuf[512];
		_sntprintf(tbuf, sizeof(tbuf), _T("%s"), OTTD2FS(dbg));
		NKDbgPrintfW(_T("dbg: [%s] %s\n"), tbuf, OTTD2FS(buf));
#else
		fprintf(stderr, "%sdbg: [%s] %s\n", GetLogPrefix(), dbg, buf);
#endif
		IConsoleDebug(dbg, buf);
	} else {
		static FILE *f = FioFOpenFile("commands-out.log", "wb", AUTOSAVE_DIR);
		if (f == NULL) return;

		fprintf(f, "%s%s\n", GetLogPrefix(), buf);
		fflush(f);
	}
}

void CDECL debug(const char *dbg, const char *format, ...)
{
	char buf[1024];

	va_list va;
	va_start(va, format);
	vsnprintf(buf, lengthof(buf), format, va);
	va_end(va);

	debug_print(dbg, buf);
}
#endif /* NO_DEBUG_MESSAGES */

void SetDebugString(const char *s)
{
	int v;
	char *end;
	const char *t;

	/* global debugging level? */
	if (*s >= '0' && *s <= '9') {
		const DebugLevel *i;

		v = strtoul(s, &end, 0);
		s = end;

		for (i = debug_level; i != endof(debug_level); ++i) *i->level = v;
	}

	/* individual levels */
	for (;;) {
		const DebugLevel *i;
		int *p;

		/* skip delimiters */
		while (*s == ' ' || *s == ',' || *s == '\t') s++;
		if (*s == '\0') break;

		t = s;
		while (*s >= 'a' && *s <= 'z') s++;

		/* check debugging levels */
		p = NULL;
		for (i = debug_level; i != endof(debug_level); ++i)
			if (s == t + strlen(i->name) && strncmp(t, i->name, s - t) == 0) {
				p = i->level;
				break;
			}

		if (*s == '=') s++;
		v = strtoul(s, &end, 0);
		s = end;
		if (p != NULL) {
			*p = v;
		} else {
			ShowInfoF("Unknown debug level '%.*s'", (int)(s - t), t);
			return;
		}
	}
}

/** Print out the current debug-level
 * Just return a string with the values of all the debug categorites
 * @return string with debug-levels
 */
const char *GetDebugString()
{
	const DebugLevel *i;
	static char dbgstr[150];
	char dbgval[20];

	memset(dbgstr, 0, sizeof(dbgstr));
	i = debug_level;
	snprintf(dbgstr, sizeof(dbgstr), "%s=%d", i->name, *i->level);

	for (i++; i != endof(debug_level); i++) {
		snprintf(dbgval, sizeof(dbgval), ", %s=%d", i->name, *i->level);
		strecat(dbgstr, dbgval, lastof(dbgstr));
	}

	return dbgstr;
}

/**
 * Get the prefix for logs; if show_date_in_logs is enabled it returns
 * the date, otherwise it returns nothing.
 * @return the prefix for logs (do not free), never NULL
 */
const char *GetLogPrefix()
{
	static char _log_prefix[24];
	if (_settings_client.gui.show_date_in_logs) {
		time_t cur_time = time(NULL);
		strftime(_log_prefix, sizeof(_log_prefix), "[%Y-%m-%d %H:%M:%S] ", localtime(&cur_time));
	} else {
		*_log_prefix = '\0';
	}
	return _log_prefix;
}

