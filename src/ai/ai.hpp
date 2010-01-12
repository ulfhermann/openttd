/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file ai.hpp Base functions for all AIs. */

#ifndef AI_HPP
#define AI_HPP

#include "api/ai_event_types.hpp"
#include "../date_type.h"
#include "../core/string_compare_type.hpp"
#include <map>

typedef std::map<const char *, class AIInfo *, StringCompare> AIInfoList;


class AI {
public:
	/**
	 * The default months AIs start after eachother.
	 */
	enum StartNext {
		START_NEXT_EASY   = DAYS_IN_YEAR * 2,
		START_NEXT_MEDIUM = DAYS_IN_YEAR,
		START_NEXT_HARD   = DAYS_IN_YEAR / 2,
		START_NEXT_MIN    = 1,
		START_NEXT_MAX    = 3600,
		START_NEXT_DEVIATION = 60,
	};

	/**
	 * Is it possible to start a new AI company?
	 * @return True if a new AI company can be started.
	 */
	static bool CanStartNew();

	/**
	 * Start a new AI company.
	 * @param company At which slot the AI company should start.
	 * @param rerandomise_ai Whether to rerandomise the configured AI.
	 */
	static void StartNew(CompanyID company, bool rerandomise_ai = true);

	/**
	 * Called every game-tick to let AIs do something.
	 */
	static void GameLoop();

	/**
	 * Get the current AI tick.
	 */
	static uint GetTick();

	/**
	 * Stop a company to be controlled by an AI.
	 * @param company The company from which the AI needs to detach.
	 * @pre Company::IsValidAiID(company)
	 */
	static void Stop(CompanyID company);

	/**
	 * Kill any and all AIs we manage.
	 */
	static void KillAll();

	/**
	 * Initialize the AI system.
	 */
	static void Initialize();

	/**
	 * Uninitialize the AI system
	 * @param keepConfig Should we keep AIConfigs, or can we free that memory?
	 */
	static void Uninitialize(bool keepConfig);

	/**
	 * Reset all AIConfigs, and make them reload their AIInfo.
	 * If the AIInfo could no longer be found, an error is reported to the user.
	 */
	static void ResetConfig();

	/**
	 * Queue a new event for an AI.
	 */
	static void NewEvent(CompanyID company, AIEvent *event);

	/**
	 * Broadcast a new event to all active AIs.
	 */
	static void BroadcastNewEvent(AIEvent *event, CompanyID skip_company = MAX_COMPANIES);

	/**
	 * Save data from an AI to a savegame.
	 */
	static void Save(CompanyID company);

	/**
	 * Load data for an AI from a savegame.
	 */
	static void Load(CompanyID company, int version);

	/**
	 * Get the number of days before the next AI should start.
	 */
	static int GetStartNextTime();

	static char *GetConsoleList(char *p, const char *last);
	static const AIInfoList *GetInfoList();
	static const AIInfoList *GetUniqueInfoList();
	static AIInfo *FindInfo(const char *name, int version);
	static bool ImportLibrary(const char *library, const char *class_name, int version, HSQUIRRELVM vm);
	static void Rescan();
#if defined(ENABLE_NETWORK)
	static bool HasAI(const struct ContentInfo *ci, bool md5sum);
#endif
private:
	static uint frame_counter;
	static class AIScanner *ai_scanner;
};

#endif /* AI_HPP */
