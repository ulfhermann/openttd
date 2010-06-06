/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file genworld.cpp Functions to generate a map. */

#include "stdafx.h"
#include "openttd.h"
#include "landscape.h"
#include "company_func.h"
#include "variables.h"
#include "thread/thread.h"
#include "command_func.h"
#include "genworld.h"
#include "gfxinit.h"
#include "window_func.h"
#include "network/network.h"
#include "heightmap.h"
#include "viewport_func.h"
#include "gfx_func.h"
#include "date_func.h"
#include "engine_func.h"
#include "newgrf_storage.h"
#include "water.h"
#include "blitter/factory.hpp"
#include "tilehighlight_func.h"
#include "saveload/saveload.h"
#include "void_map.h"
#include "town.h"
#include "newgrf.h"
#include "core/random_func.hpp"
#include "core/backup_type.hpp"

#include "table/sprites.h"

void GenerateClearTile();
void GenerateIndustries();
void GenerateUnmovables();
void GenerateTrees();

void StartupEconomy();
void StartupCompanies();
void StartupDisasters();

void InitializeGame(uint size_x, uint size_y, bool reset_date, bool reset_settings);

/**
 * Please only use this variable in genworld.h and genworld.cpp and
 *  nowhere else. For speed improvements we need it to be global, but
 *  in no way the meaning of it is to use it anywhere else besides
 *  in the genworld.h and genworld.cpp! -- TrueLight
 */
GenWorldInfo _gw;

/** Rights for the map generation */
ThreadMutex *_genworld_mapgen_mutex = ThreadMutex::New();
/** Rights for the painting */
ThreadMutex *_genworld_paint_mutex = ThreadMutex::New();

/**
 * Tells if the world generation is done in a thread or not.
 * @return the 'threaded' status
 */
bool IsGenerateWorldThreaded()
{
	return _gw.threaded && !_gw.quit_thread;
}

/**
 * Clean up the 'mess' of generation. That is show windows again, reset
 * thread variables and delete the progress window.
 */
static void CleanupGeneration()
{
	_generating_world = false;

	if (_cursor.sprite == SPR_CURSOR_ZZZ) SetMouseCursor(SPR_CURSOR_MOUSE, PAL_NONE);
	/* Show all vital windows again, because we have hidden them */
	if (_gw.threaded && _game_mode != GM_MENU) ShowVitalWindows();
	_gw.active   = false;
	_gw.proc     = NULL;
	_gw.abortp   = NULL;
	_gw.threaded = false;

	DeleteWindowById(WC_GENERATE_PROGRESS_WINDOW, 0);
	MarkWholeScreenDirty();
	_genworld_mapgen_mutex->EndCritical();
}

/**
 * The internal, real, generate function.
 */
static void _GenerateWorld(void *)
{
	/* Make sure everything is done via OWNER_NONE. */
	Backup<CompanyByte> _cur_company(_current_company, OWNER_NONE, FILE_LINE);

	try {
		_generating_world = true;
		_genworld_mapgen_mutex->BeginCritical();
		if (_network_dedicated) DEBUG(net, 0, "Generating map, please wait...");
		/* Set the Random() seed to generation_seed so we produce the same map with the same seed */
		if (_settings_game.game_creation.generation_seed == GENERATE_NEW_SEED) _settings_game.game_creation.generation_seed = _settings_newgame.game_creation.generation_seed = InteractiveRandom();
		_random.SetSeed(_settings_game.game_creation.generation_seed);
		SetGeneratingWorldProgress(GWP_MAP_INIT, 2);
		SetObjectToPlace(SPR_CURSOR_ZZZ, PAL_NONE, HT_NONE, WC_MAIN_WINDOW, 0);

		IncreaseGeneratingWorldProgress(GWP_MAP_INIT);
		/* Must start economy early because of the costs. */
		StartupEconomy();

		/* Don't generate landscape items when in the scenario editor. */
		if (_gw.mode == GWM_EMPTY) {
			SetGeneratingWorldProgress(GWP_UNMOVABLE, 1);

			/* Make sure the tiles at the north border are void tiles if needed. */
			if (_settings_game.construction.freeform_edges) {
				for (uint row = 0; row < MapSizeY(); row++) MakeVoid(TileXY(0, row));
				for (uint col = 0; col < MapSizeX(); col++) MakeVoid(TileXY(col, 0));
			}

			/* Make the map the height of the setting */
			if (_game_mode != GM_MENU) FlatEmptyWorld(_settings_game.game_creation.se_flat_world_height);

			ConvertGroundTilesIntoWaterTiles();
			IncreaseGeneratingWorldProgress(GWP_UNMOVABLE);
		} else {
			GenerateLandscape(_gw.mode);
			GenerateClearTile();

			/* only generate towns, tree and industries in newgame mode. */
			if (_game_mode != GM_EDITOR) {
				if (!GenerateTowns(_settings_game.economy.town_layout)) {
					_cur_company.Restore();
					HandleGeneratingWorldAbortion();
					return;
				}
				GenerateIndustries();
				GenerateUnmovables();
				GenerateTrees();
			}
		}

		ClearStorageChanges(true);

		/* These are probably pointless when inside the scenario editor. */
		SetGeneratingWorldProgress(GWP_GAME_INIT, 3);
		StartupCompanies();
		IncreaseGeneratingWorldProgress(GWP_GAME_INIT);
		StartupEngines();
		IncreaseGeneratingWorldProgress(GWP_GAME_INIT);
		StartupDisasters();
		_generating_world = false;

		/* No need to run the tile loop in the scenario editor. */
		if (_gw.mode != GWM_EMPTY) {
			uint i;

			SetGeneratingWorldProgress(GWP_RUNTILELOOP, 0x500);
			for (i = 0; i < 0x500; i++) {
				RunTileLoop();
				IncreaseGeneratingWorldProgress(GWP_RUNTILELOOP);
			}
		}

		ResetObjectToPlace();
		_cur_company.Trash();
		_current_company = _local_company = _gw.lc;

		SetGeneratingWorldProgress(GWP_GAME_START, 1);
		/* Call any callback */
		if (_gw.proc != NULL) _gw.proc();
		IncreaseGeneratingWorldProgress(GWP_GAME_START);

		CleanupGeneration();

		ShowNewGRFError();

		if (_network_dedicated) DEBUG(net, 0, "Map generated, starting game");
		DEBUG(desync, 1, "new_map: %08x", _settings_game.game_creation.generation_seed);

		if (_debug_desync_level > 0) {
			char name[MAX_PATH];
			snprintf(name, lengthof(name), "dmp_cmds_%08x_%08x.sav", _settings_game.game_creation.generation_seed, _date);
			SaveOrLoad(name, SL_SAVE, AUTOSAVE_DIR);
		}
	} catch (...) {
		if (_cur_company.IsValid()) _cur_company.Restore();
		_generating_world = false;
		_genworld_mapgen_mutex->EndCritical();
		throw;
	}
}

/**
 * Set here the function, if any, that you want to be called when landscape
 * generation is done.
 * @param proc callback procedure
 */
void GenerateWorldSetCallback(GWDoneProc *proc)
{
	_gw.proc = proc;
}

/**
 * Set here the function, if any, that you want to be called when landscape
 * generation is aborted.
 * @param proc callback procedure
 */
void GenerateWorldSetAbortCallback(GWAbortProc *proc)
{
	_gw.abortp = proc;
}

/**
 * This will wait for the thread to finish up his work. It will not continue
 * till the work is done.
 */
void WaitTillGeneratedWorld()
{
	if (_gw.thread == NULL) return;

	_genworld_mapgen_mutex->EndCritical();
	_genworld_paint_mutex->EndCritical();
	_gw.quit_thread = true;
	_gw.thread->Join();
	delete _gw.thread;
	_gw.thread   = NULL;
	_gw.threaded = false;
	_genworld_mapgen_mutex->BeginCritical();
	_genworld_paint_mutex->BeginCritical();
}

/**
 * Initializes the abortion process
 */
void AbortGeneratingWorld()
{
	_gw.abort = true;
}

/**
 * Is the generation being aborted?
 * @return the 'aborted' status
 */
bool IsGeneratingWorldAborted()
{
	return _gw.abort;
}

/**
 * Really handle the abortion, i.e. clean up some of the mess
 */
void HandleGeneratingWorldAbortion()
{
	/* Clean up - in SE create an empty map, otherwise, go to intro menu */
	_switch_mode = (_game_mode == GM_EDITOR) ? SM_EDITOR : SM_MENU;

	if (_gw.abortp != NULL) _gw.abortp();

	CleanupGeneration();

	if (_gw.thread != NULL) _gw.thread->Exit();

	extern void SwitchToMode(SwitchMode new_mode);
	SwitchToMode(_switch_mode);
}

/**
 * Generate a world.
 * @param mode The mode of world generation (see GenWorldMode).
 * @param size_x The X-size of the map.
 * @param size_y The Y-size of the map.
 * @param reset_settings Whether to reset the game configuration (used for restart)
 */
void GenerateWorld(GenWorldMode mode, uint size_x, uint size_y, bool reset_settings)
{
	if (_gw.active) return;
	_gw.mode   = mode;
	_gw.size_x = size_x;
	_gw.size_y = size_y;
	_gw.active = true;
	_gw.abort  = false;
	_gw.abortp = NULL;
	_gw.lc     = _local_company;
	_gw.quit_thread   = false;
	_gw.threaded      = true;

	/* This disables some commands and stuff */
	SetLocalCompany(COMPANY_SPECTATOR);

	/* Set the date before loading sprites as some newgrfs check it */
	SetDate(ConvertYMDToDate(_settings_game.game_creation.starting_year, 0, 1));

	InitializeGame(_gw.size_x, _gw.size_y, false, reset_settings);
	PrepareGenerateWorldProgress();

	/* Load the right landscape stuff */
	GfxLoadSprites();
	LoadStringWidthTable();

	/* Re-init the windowing system */
	ResetWindowSystem();

	/* Create toolbars */
	SetupColoursAndInitialWindow();
	SetObjectToPlace(SPR_CURSOR_ZZZ, PAL_NONE, HT_NONE, WC_MAIN_WINDOW, 0);

	if (_gw.thread != NULL) {
		_gw.thread->Join();
		delete _gw.thread;
		_gw.thread = NULL;
	}

	if (BlitterFactoryBase::GetCurrentBlitter()->GetScreenDepth() == 0 ||
			!ThreadObject::New(&_GenerateWorld, NULL, &_gw.thread)) {
		DEBUG(misc, 1, "Cannot create genworld thread, reverting to single-threaded mode");
		_gw.threaded = false;
		_genworld_mapgen_mutex->EndCritical();
		_GenerateWorld(NULL);
		_genworld_mapgen_mutex->BeginCritical();
		return;
	}

	/* Remove any open window */
	DeleteAllNonVitalWindows();
	/* Hide vital windows, because we don't allow to use them */
	HideVitalWindows();

	/* Don't show the dialog if we don't have a thread */
	ShowGenerateWorldProgress();

	/* Centre the view on the map */
	if (FindWindowById(WC_MAIN_WINDOW, 0) != NULL) {
		ScrollMainWindowToTile(TileXY(MapSizeX() / 2, MapSizeY() / 2), true);
	}
}
