/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file console.cpp Handling of the in-game console. */

#include "stdafx.h"
#include "console_internal.h"
#include "network/network.h"
#include "network/network_func.h"
#include "debug.h"
#include "console_func.h"
#include "settings_type.h"

#include <stdarg.h>

#define ICON_BUFFER 79
#define ICON_HISTORY_SIZE 20
#define ICON_LINE_HEIGHT 12
#define ICON_RIGHT_BORDERWIDTH 10
#define ICON_BOTTOM_BORDERWIDTH 12
#define ICON_MAX_ALIAS_LINES 40
#define ICON_TOKEN_COUNT 20

/* console parser */
IConsoleCmd   *_iconsole_cmds;    ///< list of registered commands
IConsoleAlias *_iconsole_aliases; ///< list of registered aliases

FILE *_iconsole_output_file;

void IConsoleInit()
{
	_iconsole_output_file = NULL;
#ifdef ENABLE_NETWORK /* Initialize network only variables */
	_redirect_console_to_client = INVALID_CLIENT_ID;
#endif

	IConsoleGUIInit();

	IConsoleStdLibRegister();
}

static void IConsoleWriteToLogFile(const char *string)
{
	if (_iconsole_output_file != NULL) {
		/* if there is an console output file ... also print it there */
		const char *header = GetLogPrefix();
		if ((strlen(header) != 0 && fwrite(header, strlen(header), 1, _iconsole_output_file) != 1) ||
				fwrite(string, strlen(string), 1, _iconsole_output_file) != 1 ||
				fwrite("\n", 1, 1, _iconsole_output_file) != 1) {
			fclose(_iconsole_output_file);
			_iconsole_output_file = NULL;
			IConsolePrintF(CC_DEFAULT, "cannot write to log file");
		}
	}
}

bool CloseConsoleLogIfActive()
{
	if (_iconsole_output_file != NULL) {
		IConsolePrintF(CC_DEFAULT, "file output complete");
		fclose(_iconsole_output_file);
		_iconsole_output_file = NULL;
		return true;
	}

	return false;
}

void IConsoleFree()
{
	IConsoleGUIFree();
	CloseConsoleLogIfActive();
}

/**
 * Handle the printing of text entered into the console or redirected there
 * by any other means. Text can be redirected to other clients in a network game
 * as well as to a logfile. If the network server is a dedicated server, all activities
 * are also logged. All lines to print are added to a temporary buffer which can be
 * used as a history to print them onscreen
 * @param colour_code the colour of the command. Red in case of errors, etc.
 * @param string the message entered or output on the console (notice, error, etc.)
 */
void IConsolePrint(ConsoleColour colour_code, const char *string)
{
	char *str;
#ifdef ENABLE_NETWORK
	if (_redirect_console_to_client != INVALID_CLIENT_ID) {
		/* Redirect the string to the client */
		NetworkServerSendRcon(_redirect_console_to_client, colour_code, string);
		return;
	}
#endif

	/* Create a copy of the string, strip if of colours and invalid
	 * characters and (when applicable) assign it to the console buffer */
	str = strdup(string);
	str_strip_colours(str);
	str_validate(str, str + strlen(str));

	if (_network_dedicated) {
		fprintf(stdout, "%s%s\n", GetLogPrefix(), str);
		fflush(stdout);
		IConsoleWriteToLogFile(str);
		free(str); // free duplicated string since it's not used anymore
		return;
	}

	IConsoleWriteToLogFile(str);
	IConsoleGUIPrint(colour_code, str);
}

/**
 * Handle the printing of text entered into the console or redirected there
 * by any other means. Uses printf() style format, for more information look
 * at IConsolePrint()
 */
void CDECL IConsolePrintF(ConsoleColour colour_code, const char *format, ...)
{
	va_list va;
	char buf[ICON_MAX_STREAMSIZE];

	va_start(va, format);
	vsnprintf(buf, sizeof(buf), format, va);
	va_end(va);

	IConsolePrint(colour_code, buf);
}

/**
 * It is possible to print debugging information to the console,
 * which is achieved by using this function. Can only be used by
 * debug() in debug.cpp. You need at least a level 2 (developer) for debugging
 * messages to show up
 * @param dbg debugging category
 * @param string debugging message
 */
void IConsoleDebug(const char *dbg, const char *string)
{
	if (_settings_client.gui.developer <= 1) return;
	IConsolePrintF(CC_DEBUG, "dbg: [%s] %s", dbg, string);
}

/**
 * It is possible to print warnings to the console. These are mostly
 * errors or mishaps, but non-fatal. You need at least a level 1 (developer) for
 * debugging messages to show up
 */
void IConsoleWarning(const char *string)
{
	if (_settings_client.gui.developer == 0) return;
	IConsolePrintF(CC_WARNING, "WARNING: %s", string);
}

/**
 * It is possible to print error information to the console. This can include
 * game errors, or errors in general you would want the user to notice
 */
void IConsoleError(const char *string)
{
	IConsolePrintF(CC_ERROR, "ERROR: %s", string);
}

/**
 * Change a string into its number representation. Supports
 * decimal and hexadecimal numbers as well as 'on'/'off' 'true'/'false'
 * @param *value the variable a successful conversion will be put in
 * @param *arg the string to be converted
 * @return Return true on success or false on failure
 */
bool GetArgumentInteger(uint32 *value, const char *arg)
{
	char *endptr;

	if (strcmp(arg, "on") == 0 || strcmp(arg, "true") == 0) {
		*value = 1;
		return true;
	}
	if (strcmp(arg, "off") == 0 || strcmp(arg, "false") == 0) {
		*value = 0;
		return true;
	}

	*value = strtoul(arg, &endptr, 0);
	return arg != endptr;
}

/**
 * Add an item to an alphabetically sorted list.
 * @param base first item of the list
 * @param item_new the item to add
 */
template<class T>
void IConsoleAddSorted(T **base, T *item_new)
{
	if (*base == NULL) {
		*base = item_new;
		return;
	}

	T *item_before = NULL;
	T *item = *base;
	/* The list is alphabetically sorted, insert the new item at the correct location */
	while (item != NULL) {
		if (strcmp(item->name, item_new->name) > 0) break; // insert here

		item_before = item;
		item = item->next;
	}

	if (item_before == NULL) {
		*base = item_new;
	} else {
		item_before->next = item_new;
	}

	item_new->next = item;
}

/**
 * Register a new command to be used in the console
 * @param name name of the command that will be used
 * @param proc function that will be called upon execution of command
 */
void IConsoleCmdRegister(const char *name, IConsoleCmdProc *proc, IConsoleHook *hook)
{
	IConsoleCmd *item_new = MallocT<IConsoleCmd>(1);
	item_new->name = strdup(name);
	item_new->next = NULL;
	item_new->proc = proc;
	item_new->hook = hook;

	IConsoleAddSorted(&_iconsole_cmds, item_new);
}

/**
 * Find the command pointed to by its string
 * @param name command to be found
 * @return return Cmdstruct of the found command, or NULL on failure
 */
IConsoleCmd *IConsoleCmdGet(const char *name)
{
	IConsoleCmd *item;

	for (item = _iconsole_cmds; item != NULL; item = item->next) {
		if (strcmp(item->name, name) == 0) return item;
	}
	return NULL;
}

/**
 * Register a an alias for an already existing command in the console
 * @param name name of the alias that will be used
 * @param cmd name of the command that 'name' will be alias of
 */
void IConsoleAliasRegister(const char *name, const char *cmd)
{
	if (IConsoleAliasGet(name) != NULL) {
		IConsoleError("an alias with this name already exists; insertion aborted");
		return;
	}

	char *new_alias = strdup(name);
	char *cmd_aliased = strdup(cmd);
	IConsoleAlias *item_new = MallocT<IConsoleAlias>(1);

	item_new->next = NULL;
	item_new->cmdline = cmd_aliased;
	item_new->name = new_alias;

	IConsoleAddSorted(&_iconsole_aliases, item_new);
}

/**
 * Find the alias pointed to by its string
 * @param name alias to be found
 * @return return Aliasstruct of the found alias, or NULL on failure
 */
IConsoleAlias *IConsoleAliasGet(const char *name)
{
	IConsoleAlias *item;

	for (item = _iconsole_aliases; item != NULL; item = item->next) {
		if (strcmp(item->name, name) == 0) return item;
	}

	return NULL;
}

/** copy in an argument into the aliasstream */
static inline int IConsoleCopyInParams(char *dst, const char *src, uint bufpos)
{
	/* len is the amount of bytes to add excluding the '\0'-termination */
	int len = min(ICON_MAX_STREAMSIZE - bufpos - 1, (uint)strlen(src));
	strecpy(dst, src, dst + len);

	return len;
}

/**
 * An alias is just another name for a command, or for more commands
 * Execute it as well.
 * @param *alias is the alias of the command
 * @param tokencount the number of parameters passed
 * @param *tokens are the parameters given to the original command (0 is the first param)
 */
static void IConsoleAliasExec(const IConsoleAlias *alias, byte tokencount, char *tokens[ICON_TOKEN_COUNT])
{
	const char *cmdptr;
	char *aliases[ICON_MAX_ALIAS_LINES], aliasstream[ICON_MAX_STREAMSIZE];
	uint i;
	uint a_index, astream_i;

	memset(&aliases, 0, sizeof(aliases));
	memset(&aliasstream, 0, sizeof(aliasstream));

	DEBUG(console, 6, "Requested command is an alias; parsing...");

	aliases[0] = aliasstream;
	for (cmdptr = alias->cmdline, a_index = 0, astream_i = 0; *cmdptr != '\0'; cmdptr++) {
		if (a_index >= lengthof(aliases) || astream_i >= lengthof(aliasstream)) break;

		switch (*cmdptr) {
		case '\'': // ' will double for ""
			aliasstream[astream_i++] = '"';
			break;
		case ';': // Cmd seperator, start new command
			aliasstream[astream_i] = '\0';
			aliases[++a_index] = &aliasstream[++astream_i];
			cmdptr++;
			break;
		case '%': // Some or all parameters
			cmdptr++;
			switch (*cmdptr) {
			case '+': { // All parameters seperated: "[param 1]" "[param 2]"
				for (i = 0; i != tokencount; i++) {
					aliasstream[astream_i++] = '"';
					astream_i += IConsoleCopyInParams(&aliasstream[astream_i], tokens[i], astream_i);
					aliasstream[astream_i++] = '"';
					aliasstream[astream_i++] = ' ';
				}
			} break;
			case '!': { // Merge the parameters to one: "[param 1] [param 2] [param 3...]"
				aliasstream[astream_i++] = '"';
				for (i = 0; i != tokencount; i++) {
					astream_i += IConsoleCopyInParams(&aliasstream[astream_i], tokens[i], astream_i);
					aliasstream[astream_i++] = ' ';
				}
				aliasstream[astream_i++] = '"';

			} break;
				default: { // One specific parameter: %A = [param 1] %B = [param 2] ...
				int param = *cmdptr - 'A';

				if (param < 0 || param >= tokencount) {
					IConsoleError("too many or wrong amount of parameters passed to alias, aborting");
					IConsolePrintF(CC_WARNING, "Usage of alias '%s': %s", alias->name, alias->cmdline);
					return;
				}

				aliasstream[astream_i++] = '"';
				astream_i += IConsoleCopyInParams(&aliasstream[astream_i], tokens[param], astream_i);
				aliasstream[astream_i++] = '"';
			} break;
			} break;

		default:
			aliasstream[astream_i++] = *cmdptr;
			break;
		}
	}

	for (i = 0; i <= a_index; i++) IConsoleCmdExec(aliases[i]); // execute each alias in turn
}

/**
 * Execute a given command passed to us. First chop it up into
 * individual tokens (seperated by spaces), then execute it if possible
 * @param cmdstr string to be parsed and executed
 */
void IConsoleCmdExec(const char *cmdstr)
{
	IConsoleCmd   *cmd    = NULL;
	IConsoleAlias *alias  = NULL;

	const char *cmdptr;
	char *tokens[ICON_TOKEN_COUNT], tokenstream[ICON_MAX_STREAMSIZE];
	uint t_index, tstream_i;

	bool longtoken = false;
	bool foundtoken = false;

	if (cmdstr[0] == '#') return; // comments

	for (cmdptr = cmdstr; *cmdptr != '\0'; cmdptr++) {
		if (!IsValidChar(*cmdptr, CS_ALPHANUMERAL)) {
			IConsoleError("command contains malformed characters, aborting");
			IConsolePrintF(CC_ERROR, "ERROR: command was: '%s'", cmdstr);
			return;
		}
	}

	DEBUG(console, 4, "Executing cmdline: '%s'", cmdstr);

	memset(&tokens, 0, sizeof(tokens));
	memset(&tokenstream, 0, sizeof(tokenstream));

	/* 1. Split up commandline into tokens, seperated by spaces, commands
	 * enclosed in "" are taken as one token. We can only go as far as the amount
	 * of characters in our stream or the max amount of tokens we can handle */
	for (cmdptr = cmdstr, t_index = 0, tstream_i = 0; *cmdptr != '\0'; cmdptr++) {
		if (t_index >= lengthof(tokens) || tstream_i >= lengthof(tokenstream)) break;

		switch (*cmdptr) {
		case ' ': // Token seperator
			if (!foundtoken) break;

			if (longtoken) {
				tokenstream[tstream_i] = *cmdptr;
			} else {
				tokenstream[tstream_i] = '\0';
				foundtoken = false;
			}

			tstream_i++;
			break;
		case '"': // Tokens enclosed in "" are one token
			longtoken = !longtoken;
			break;
		case '\\': // Escape character for ""
			if (cmdptr[1] == '"' && tstream_i + 1 < lengthof(tokenstream)) {
				tokenstream[tstream_i++] = *++cmdptr;
				break;
			}
			/* fallthrough */
		default: // Normal character
			tokenstream[tstream_i++] = *cmdptr;

			if (!foundtoken) {
				tokens[t_index++] = &tokenstream[tstream_i - 1];
				foundtoken = true;
			}
			break;
		}
	}

	for (uint i = 0; tokens[i] != NULL; i++) {
		DEBUG(console, 8, "Token %d is: '%s'", i, tokens[i]);
	}

	if (tokens[0] == '\0') return; // don't execute empty commands
	/* 2. Determine type of command (cmd or alias) and execute
	 * First try commands, then aliases. Execute
	 * the found action taking into account its hooking code
	 */
	cmd = IConsoleCmdGet(tokens[0]);
	if (cmd != NULL) {
		ConsoleHookResult chr = (cmd->hook == NULL ? CHR_ALLOW : cmd->hook(true));
		switch (chr) {
			case CHR_ALLOW:
				if (!cmd->proc(t_index, tokens)) { // index started with 0
					cmd->proc(0, NULL); // if command failed, give help
				}
				return;

			case CHR_DISALLOW: return;
			case CHR_HIDE: break;
		}
	}

	t_index--;
	alias = IConsoleAliasGet(tokens[0]);
	if (alias != NULL) {
		IConsoleAliasExec(alias, t_index, &tokens[1]);
		return;
	}

	IConsoleError("command not found");
}
