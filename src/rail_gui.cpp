/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file rail_gui.cpp File for dealing with rail construction user interface */

#include "stdafx.h"
#include "gui.h"
#include "window_gui.h"
#include "station_gui.h"
#include "terraform_gui.h"
#include "viewport_func.h"
#include "command_func.h"
#include "waypoint_func.h"
#include "newgrf_station.h"
#include "company_base.h"
#include "strings_func.h"
#include "functions.h"
#include "window_func.h"
#include "date_func.h"
#include "sound_func.h"
#include "company_func.h"
#include "widgets/dropdown_type.h"
#include "tunnelbridge.h"
#include "tilehighlight_func.h"
#include "spritecache.h"
#include "core/geometry_func.hpp"

#include "station_map.h"
#include "tunnelbridge_map.h"

#include "table/sprites.h"
#include "table/strings.h"

static RailType _cur_railtype;               ///< Rail type of the current build-rail toolbar.
static bool _remove_button_clicked;          ///< Flag whether 'remove' toggle-button is currently enabled
static DiagDirection _build_depot_direction; ///< Currently selected depot direction
static byte _waypoint_count = 1;             ///< Number of waypoint types
static byte _cur_waypoint_type;              ///< Currently selected waypoint type
static bool _convert_signal_button;          ///< convert signal button in the signal GUI pressed
static SignalVariant _cur_signal_variant;    ///< set the signal variant (for signal GUI)
static SignalType _cur_signal_type;          ///< set the signal type (for signal GUI)

/* Map the setting: default_signal_type to the corresponding signal type */
static const SignalType _default_signal_type[] = {SIGTYPE_NORMAL, SIGTYPE_PBS, SIGTYPE_PBS_ONEWAY};

struct RailStationGUISettings {
	Axis orientation;                 ///< Currently selected rail station orientation

	bool newstations;                 ///< Are custom station definitions available?
	StationClassIDByte station_class; ///< Currently selected custom station class (if newstations is \c true )
	byte station_type;                ///< Station type within the currently selected custom station class (if newstations is \c true )
	byte station_count;               ///< Number of custom stations (if newstations is \c true )
};
static RailStationGUISettings _railstation; ///< Settings of the station builder GUI


static void HandleStationPlacement(TileIndex start, TileIndex end);
static void ShowBuildTrainDepotPicker(Window *parent);
static void ShowBuildWaypointPicker(Window *parent);
static void ShowStationBuilder(Window *parent);
static void ShowSignalBuilder(Window *parent);

void CcPlaySound1E(const CommandCost &result, TileIndex tile, uint32 p1, uint32 p2)
{
	if (result.Succeeded()) SndPlayTileFx(SND_20_SPLAT_2, tile);
}

static void GenericPlaceRail(TileIndex tile, int cmd)
{
	DoCommandP(tile, _cur_railtype, cmd,
		_remove_button_clicked ?
		CMD_REMOVE_SINGLE_RAIL | CMD_MSG(STR_ERROR_CAN_T_REMOVE_RAILROAD_TRACK) :
		CMD_BUILD_SINGLE_RAIL | CMD_MSG(STR_ERROR_CAN_T_BUILD_RAILROAD_TRACK),
		CcPlaySound1E
	);
}

static void PlaceRail_N(TileIndex tile)
{
	VpStartPlaceSizing(tile, VPM_FIX_VERTICAL | VPM_RAILDIRS, DDSP_PLACE_RAIL);
}

static void PlaceRail_NE(TileIndex tile)
{
	VpStartPlaceSizing(tile, VPM_FIX_Y | VPM_RAILDIRS, DDSP_PLACE_RAIL);
}

static void PlaceRail_E(TileIndex tile)
{
	VpStartPlaceSizing(tile, VPM_FIX_HORIZONTAL | VPM_RAILDIRS, DDSP_PLACE_RAIL);
}

static void PlaceRail_NW(TileIndex tile)
{
	VpStartPlaceSizing(tile, VPM_FIX_X | VPM_RAILDIRS, DDSP_PLACE_RAIL);
}

static void PlaceRail_AutoRail(TileIndex tile)
{
	VpStartPlaceSizing(tile, VPM_RAILDIRS, DDSP_PLACE_RAIL);
}

/**
 * Try to add an additional rail-track at the entrance of a depot
 * @param tile  Tile to use for adding the rail-track
 * @param extra Track to add
 * @see CcRailDepot()
 */
static void PlaceExtraDepotRail(TileIndex tile, uint16 extra)
{
	if (GetRailTileType(tile) != RAIL_TILE_NORMAL) return;
	if ((GetTrackBits(tile) & GB(extra, 8, 8)) == 0) return;

	DoCommandP(tile, _cur_railtype, extra & 0xFF, CMD_BUILD_SINGLE_RAIL);
}

/** Additional pieces of track to add at the entrance of a depot. */
static const uint16 _place_depot_extra[12] = {
	0x0604, 0x2102, 0x1202, 0x0505,  // First additional track for directions 0..3
	0x2400, 0x2801, 0x1800, 0x1401,  // Second additional track
	0x2203, 0x0904, 0x0A05, 0x1103,  // Third additional track
};


void CcRailDepot(const CommandCost &result, TileIndex tile, uint32 p1, uint32 p2)
{
	if (result.Failed()) return;

	DiagDirection dir = (DiagDirection)p2;

	SndPlayTileFx(SND_20_SPLAT_2, tile);
	if (!_settings_client.gui.persistent_buildingtools) ResetObjectToPlace();

	tile += TileOffsByDiagDir(dir);

	if (IsTileType(tile, MP_RAILWAY)) {
		PlaceExtraDepotRail(tile, _place_depot_extra[dir]);
		PlaceExtraDepotRail(tile, _place_depot_extra[dir + 4]);
		PlaceExtraDepotRail(tile, _place_depot_extra[dir + 8]);
	}
}

static void PlaceRail_Depot(TileIndex tile)
{
	DoCommandP(tile, _cur_railtype, _build_depot_direction,
		CMD_BUILD_TRAIN_DEPOT | CMD_MSG(STR_ERROR_CAN_T_BUILD_TRAIN_DEPOT),
		CcRailDepot);
}

static void PlaceRail_Waypoint(TileIndex tile)
{
	if (_remove_button_clicked) {
		VpStartPlaceSizing(tile, VPM_X_AND_Y, DDSP_REMOVE_STATION);
		return;
	}

	Axis axis = GetAxisForNewWaypoint(tile);
	if (IsValidAxis(axis)) {
		/* Valid tile for waypoints */
		VpStartPlaceSizing(tile, axis == AXIS_X ? VPM_FIX_X : VPM_FIX_Y, DDSP_BUILD_STATION);
	} else {
		/* Tile where we can't build rail waypoints. This is always going to fail,
		 * but provides the user with a proper error message. */
		DoCommandP(tile, 1 << 8 | 1 << 16, STAT_CLASS_WAYP | INVALID_STATION << 16, CMD_BUILD_RAIL_WAYPOINT | CMD_MSG(STR_ERROR_CAN_T_BUILD_TRAIN_WAYPOINT));
	}
}

void CcStation(const CommandCost &result, TileIndex tile, uint32 p1, uint32 p2)
{
	if (result.Failed()) return;

	SndPlayTileFx(SND_20_SPLAT_2, tile);
	/* Only close the station builder window if the default station and non persistent building is chosen. */
	if (_railstation.station_class == STAT_CLASS_DFLT && _railstation.station_type == 0 && !_settings_client.gui.persistent_buildingtools) ResetObjectToPlace();
}

static void PlaceRail_Station(TileIndex tile)
{
	if (_remove_button_clicked) {
		VpStartPlaceSizing(tile, VPM_X_AND_Y_LIMITED, DDSP_REMOVE_STATION);
		VpSetPlaceSizingLimit(-1);
	} else if (_settings_client.gui.station_dragdrop) {
		VpStartPlaceSizing(tile, VPM_X_AND_Y_LIMITED, DDSP_BUILD_STATION);
		VpSetPlaceSizingLimit(_settings_game.station.station_spread);
	} else {
		uint32 p1 = _cur_railtype | _railstation.orientation << 4 | _settings_client.gui.station_numtracks << 8 | _settings_client.gui.station_platlength << 16 | _ctrl_pressed << 24;
		uint32 p2 = _railstation.station_class | _railstation.station_type << 8 | INVALID_STATION << 16;

		int w = _settings_client.gui.station_numtracks;
		int h = _settings_client.gui.station_platlength;
		if (!_railstation.orientation) Swap(w, h);

		CommandContainer cmdcont = { tile, p1, p2, CMD_BUILD_RAIL_STATION | CMD_MSG(STR_ERROR_CAN_T_BUILD_RAILROAD_STATION), CcStation, "" };
		ShowSelectStationIfNeeded(cmdcont, TileArea(tile, w, h));
	}
}

/**
 * Build a new signal or edit/remove a present signal, use CmdBuildSingleSignal() or CmdRemoveSingleSignal() in rail_cmd.cpp
 *
 * @param tile The tile where the signal will build or edit
 */
static void GenericPlaceSignals(TileIndex tile)
{
	TrackBits trackbits = TrackStatusToTrackBits(GetTileTrackStatus(tile, TRANSPORT_RAIL, 0));

	if (trackbits & TRACK_BIT_VERT) { // N-S direction
		trackbits = (_tile_fract_coords.x <= _tile_fract_coords.y) ? TRACK_BIT_RIGHT : TRACK_BIT_LEFT;
	}

	if (trackbits & TRACK_BIT_HORZ) { // E-W direction
		trackbits = (_tile_fract_coords.x + _tile_fract_coords.y <= 15) ? TRACK_BIT_UPPER : TRACK_BIT_LOWER;
	}

	Track track = FindFirstTrack(trackbits);

	if (_remove_button_clicked) {
		DoCommandP(tile, track, 0, CMD_REMOVE_SIGNALS | CMD_MSG(STR_ERROR_CAN_T_REMOVE_SIGNALS_FROM), CcPlaySound1E);
	} else {
		const Window *w = FindWindowById(WC_BUILD_SIGNAL, 0);

		/* Map the setting cycle_signal_types to the lower and upper allowed signal type. */
		static const uint cycle_bounds[] = {SIGTYPE_NORMAL | (SIGTYPE_LAST_NOPBS << 3), SIGTYPE_PBS | (SIGTYPE_LAST << 3), SIGTYPE_NORMAL | (SIGTYPE_LAST << 3)};

		/* various bitstuffed elements for CmdBuildSingleSignal() */
		uint32 p1 = track;

		if (w != NULL) {
			/* signal GUI is used */
			SB(p1, 3, 1, _ctrl_pressed);
			SB(p1, 4, 1, _cur_signal_variant);
			SB(p1, 5, 3, _cur_signal_type);
			SB(p1, 8, 1, _convert_signal_button);
			SB(p1, 9, 6, cycle_bounds[_settings_client.gui.cycle_signal_types]);
		} else {
			SB(p1, 3, 1, _ctrl_pressed);
			SB(p1, 4, 1, (_cur_year < _settings_client.gui.semaphore_build_before ? SIG_SEMAPHORE : SIG_ELECTRIC));
			SB(p1, 5, 3, _default_signal_type[_settings_client.gui.default_signal_type]);
			SB(p1, 8, 1, 0);
			SB(p1, 9, 6, cycle_bounds[_settings_client.gui.cycle_signal_types]);
		}

		DoCommandP(tile, p1, 0, CMD_BUILD_SIGNALS |
			CMD_MSG((w != NULL && _convert_signal_button) ? STR_ERROR_SIGNAL_CAN_T_CONVERT_SIGNALS_HERE : STR_ERROR_CAN_T_BUILD_SIGNALS_HERE),
			CcPlaySound1E);
	}
}

static void PlaceRail_Bridge(TileIndex tile)
{
	VpStartPlaceSizing(tile, VPM_X_OR_Y, DDSP_BUILD_BRIDGE);
}

/** Command callback for building a tunnel */
void CcBuildRailTunnel(const CommandCost &result, TileIndex tile, uint32 p1, uint32 p2)
{
	if (result.Succeeded()) {
		SndPlayTileFx(SND_20_SPLAT_2, tile);
		if (!_settings_client.gui.persistent_buildingtools) ResetObjectToPlace();
	} else {
		SetRedErrorSquare(_build_tunnel_endtile);
	}
}

static void PlaceRail_Tunnel(TileIndex tile)
{
	DoCommandP(tile, _cur_railtype, 0, CMD_BUILD_TUNNEL | CMD_MSG(STR_ERROR_CAN_T_BUILD_TUNNEL_HERE), CcBuildRailTunnel);
}

static void PlaceRail_ConvertRail(TileIndex tile)
{
	VpStartPlaceSizing(tile, VPM_X_AND_Y, DDSP_CONVERT_RAIL);
}

static void PlaceRail_AutoSignals(TileIndex tile)
{
	VpStartPlaceSizing(tile, VPM_SIGNALDIRS, DDSP_BUILD_SIGNALS);
}


/** Enum referring to the widgets of the build rail toolbar */
enum RailToolbarWidgets {
	RTW_CAPTION,
	RTW_BUILD_NS,
	RTW_BUILD_X,
	RTW_BUILD_EW,
	RTW_BUILD_Y,
	RTW_AUTORAIL,
	RTW_DEMOLISH,
	RTW_BUILD_DEPOT,
	RTW_BUILD_WAYPOINT,
	RTW_BUILD_STATION,
	RTW_BUILD_SIGNALS,
	RTW_BUILD_BRIDGE,
	RTW_BUILD_TUNNEL,
	RTW_REMOVE,
	RTW_CONVERT_RAIL,
};


/** Toggles state of the Remove button of Build rail toolbar
 * @param w window the button belongs to
 */
static void ToggleRailButton_Remove(Window *w)
{
	DeleteWindowById(WC_SELECT_STATION, 0);
	w->ToggleWidgetLoweredState(RTW_REMOVE);
	w->SetWidgetDirty(RTW_REMOVE);
	_remove_button_clicked = w->IsWidgetLowered(RTW_REMOVE);
	SetSelectionRed(_remove_button_clicked);
}

/** Updates the Remove button because of Ctrl state change
 * @param w window the button belongs to
 * @return true iff the remove buton was changed
 */
static bool RailToolbar_CtrlChanged(Window *w)
{
	if (w->IsWidgetDisabled(RTW_REMOVE)) return false;

	/* allow ctrl to switch remove mode only for these widgets */
	for (uint i = RTW_BUILD_NS; i <= RTW_BUILD_STATION; i++) {
		if ((i <= RTW_AUTORAIL || i >= RTW_BUILD_WAYPOINT) && w->IsWidgetLowered(i)) {
			ToggleRailButton_Remove(w);
			return true;
		}
	}

	return false;
}


/**
 * The "rail N"-button click proc of the build-rail toolbar.
 * @param w Build-rail toolbar window
 * @see BuildRailToolbWndProc()
 */
static void BuildRailClick_N(Window *w)
{
	HandlePlacePushButton(w, RTW_BUILD_NS, GetRailTypeInfo(_cur_railtype)->cursor.rail_ns, HT_LINE | HT_DIR_VL, PlaceRail_N);
}

/**
 * The "rail NE"-button click proc of the build-rail toolbar.
 * @param w Build-rail toolbar window
 * @see BuildRailToolbWndProc()
 */
static void BuildRailClick_NE(Window *w)
{
	HandlePlacePushButton(w, RTW_BUILD_X, GetRailTypeInfo(_cur_railtype)->cursor.rail_swne, HT_LINE | HT_DIR_X, PlaceRail_NE);
}

/**
 * The "rail E"-button click proc of the build-rail toolbar.
 * @param w Build-rail toolbar window
 * @see BuildRailToolbWndProc()
 */
static void BuildRailClick_E(Window *w)
{
	HandlePlacePushButton(w, RTW_BUILD_EW, GetRailTypeInfo(_cur_railtype)->cursor.rail_ew, HT_LINE | HT_DIR_HL, PlaceRail_E);
}

/**
 * The "rail NW"-button click proc of the build-rail toolbar.
 * @param w Build-rail toolbar window
 * @see BuildRailToolbWndProc()
 */
static void BuildRailClick_NW(Window *w)
{
	HandlePlacePushButton(w, RTW_BUILD_Y, GetRailTypeInfo(_cur_railtype)->cursor.rail_nwse, HT_LINE | HT_DIR_Y, PlaceRail_NW);
}

/**
 * The "auto-rail"-button click proc of the build-rail toolbar.
 * @param w Build-rail toolbar window
 * @see BuildRailToolbWndProc()
 */
static void BuildRailClick_AutoRail(Window *w)
{
	HandlePlacePushButton(w, RTW_AUTORAIL, GetRailTypeInfo(_cur_railtype)->cursor.autorail, HT_RAIL, PlaceRail_AutoRail);
}

/**
 * The "demolish"-button click proc of the build-rail toolbar.
 * @param w Build-rail toolbar window
 * @see BuildRailToolbWndProc()
 */
static void BuildRailClick_Demolish(Window *w)
{
	HandlePlacePushButton(w, RTW_DEMOLISH, ANIMCURSOR_DEMOLISH, HT_RECT, PlaceProc_DemolishArea);
}

/**
 * The "build depot"-button click proc of the build-rail toolbar.
 * @param w Build-rail toolbar window
 * @see BuildRailToolbWndProc()
 */
static void BuildRailClick_Depot(Window *w)
{
	if (HandlePlacePushButton(w, RTW_BUILD_DEPOT, GetRailTypeInfo(_cur_railtype)->cursor.depot, HT_RECT, PlaceRail_Depot)) {
		ShowBuildTrainDepotPicker(w);
	}
}

/**
 * The "build waypoint"-button click proc of the build-rail toolbar.
 * If there are newGRF waypoints, also open a window to pick the waypoint type.
 * @param w Build-rail toolbar window
 * @see BuildRailToolbWndProc()
 */
static void BuildRailClick_Waypoint(Window *w)
{
	_waypoint_count = GetNumCustomStations(STAT_CLASS_WAYP);
	if (HandlePlacePushButton(w, RTW_BUILD_WAYPOINT, SPR_CURSOR_WAYPOINT, HT_RECT, PlaceRail_Waypoint) &&
			_waypoint_count > 1) {
		ShowBuildWaypointPicker(w);
	}
}

/**
 * The "build station"-button click proc of the build-rail toolbar.
 * @param w Build-rail toolbar window
 * @see BuildRailToolbWndProc()
 */
static void BuildRailClick_Station(Window *w)
{
	if (HandlePlacePushButton(w, RTW_BUILD_STATION, SPR_CURSOR_RAIL_STATION, HT_RECT, PlaceRail_Station)) ShowStationBuilder(w);
}

/**
 * The "build signal"-button click proc of the build-rail toolbar.
 * Start ShowSignalBuilder() and/or HandleAutoSignalPlacement().
 * @param w Build-rail toolbar window
 * @see BuildRailToolbWndProc()
 */
static void BuildRailClick_AutoSignals(Window *w)
{
	if (_settings_client.gui.enable_signal_gui != _ctrl_pressed) {
		if (HandlePlacePushButton(w, RTW_BUILD_SIGNALS, ANIMCURSOR_BUILDSIGNALS, HT_RECT, PlaceRail_AutoSignals)) ShowSignalBuilder(w);
	} else {
		HandlePlacePushButton(w, RTW_BUILD_SIGNALS, ANIMCURSOR_BUILDSIGNALS, HT_RECT, PlaceRail_AutoSignals);
	}
}

/**
 * The "build bridge"-button click proc of the build-rail toolbar.
 * @param w Build-rail toolbar window
 * @see BuildRailToolbWndProc()
 */
static void BuildRailClick_Bridge(Window *w)
{
	HandlePlacePushButton(w, RTW_BUILD_BRIDGE, SPR_CURSOR_BRIDGE, HT_RECT, PlaceRail_Bridge);
}

/**
 * The "build tunnel"-button click proc of the build-rail toolbar.
 * @param w Build-rail toolbar window
 * @see BuildRailToolbWndProc()
 */
static void BuildRailClick_Tunnel(Window *w)
{
	HandlePlacePushButton(w, RTW_BUILD_TUNNEL, GetRailTypeInfo(_cur_railtype)->cursor.tunnel, HT_SPECIAL, PlaceRail_Tunnel);
}

/**
 * The "remove"-button click proc of the build-rail toolbar.
 * @param w Build-rail toolbar window
 * @see BuildRailToolbWndProc()
 */
static void BuildRailClick_Remove(Window *w)
{
	if (w->IsWidgetDisabled(RTW_REMOVE)) return;
	ToggleRailButton_Remove(w);
	SndPlayFx(SND_15_BEEP);

	/* handle station builder */
	if (w->IsWidgetLowered(RTW_BUILD_STATION)) {
		if (_remove_button_clicked) {
			/* starting drag & drop remove */
			if (!_settings_client.gui.station_dragdrop) {
				SetTileSelectSize(1, 1);
			} else {
				VpSetPlaceSizingLimit(-1);
			}
		} else {
			/* starting station build mode */
			if (!_settings_client.gui.station_dragdrop) {
				int x = _settings_client.gui.station_numtracks;
				int y = _settings_client.gui.station_platlength;
				if (_railstation.orientation == 0) Swap(x, y);
				SetTileSelectSize(x, y);
			} else {
				VpSetPlaceSizingLimit(_settings_game.station.station_spread);
			}
		}
	}
}

/**
 * The "convert-rail"-button click proc of the build-rail toolbar.
 * Switches to 'convert-rail' mode
 * @param w Build-rail toolbar window
 * @see BuildRailToolbWndProc()
 */
static void BuildRailClick_Convert(Window *w)
{
	HandlePlacePushButton(w, RTW_CONVERT_RAIL, GetRailTypeInfo(_cur_railtype)->cursor.convert, HT_RECT, PlaceRail_ConvertRail);
}


static void DoRailroadTrack(int mode)
{
	DoCommandP(TileVirtXY(_thd.selstart.x, _thd.selstart.y), TileVirtXY(_thd.selend.x, _thd.selend.y), _cur_railtype | (mode << 4),
		_remove_button_clicked ?
		CMD_REMOVE_RAILROAD_TRACK | CMD_MSG(STR_ERROR_CAN_T_REMOVE_RAILROAD_TRACK) :
		CMD_BUILD_RAILROAD_TRACK  | CMD_MSG(STR_ERROR_CAN_T_BUILD_RAILROAD_TRACK)
	);
}

static void HandleAutodirPlacement()
{
	TileHighlightData *thd = &_thd;
	int trackstat = thd->drawstyle & 0xF; // 0..5

	if (thd->drawstyle & HT_RAIL) { // one tile case
		GenericPlaceRail(TileVirtXY(thd->selend.x, thd->selend.y), trackstat);
		return;
	}

	DoRailroadTrack(trackstat);
}

/**
 * Build new signals or remove signals or (if only one tile marked) edit a signal.
 *
 * If one tile marked abort and use GenericPlaceSignals()
 * else use CmdBuildSingleSignal() or CmdRemoveSingleSignal() in rail_cmd.cpp to build many signals
 */
static void HandleAutoSignalPlacement()
{
	TileHighlightData *thd = &_thd;
	uint32 p2 = GB(thd->drawstyle, 0, 3); // 0..5

	if (thd->drawstyle == HT_RECT) { // one tile case
		GenericPlaceSignals(TileVirtXY(thd->selend.x, thd->selend.y));
		return;
	}

	const Window *w = FindWindowById(WC_BUILD_SIGNAL, 0);

	if (w != NULL) {
		/* signal GUI is used */
		SB(p2,  3, 1, 0);
		SB(p2,  4, 1, _cur_signal_variant);
		SB(p2,  6, 1, _ctrl_pressed);
		SB(p2,  7, 3, _cur_signal_type);
		SB(p2, 24, 8, _settings_client.gui.drag_signals_density);
	} else {
		SB(p2,  3, 1, 0);
		SB(p2,  4, 1, (_cur_year < _settings_client.gui.semaphore_build_before ? SIG_SEMAPHORE : SIG_ELECTRIC));
		SB(p2,  6, 1, _ctrl_pressed);
		SB(p2,  7, 3, _default_signal_type[_settings_client.gui.default_signal_type]);
		SB(p2, 24, 8, _settings_client.gui.drag_signals_density);
	}

	/* _settings_client.gui.drag_signals_density is given as a parameter such that each user
	 * in a network game can specify his/her own signal density */
	DoCommandP(
		TileVirtXY(thd->selstart.x, thd->selstart.y),
		TileVirtXY(thd->selend.x, thd->selend.y),
		p2,
		_remove_button_clicked ?
			CMD_REMOVE_SIGNAL_TRACK | CMD_MSG(STR_ERROR_CAN_T_REMOVE_SIGNALS_FROM) :
			CMD_BUILD_SIGNAL_TRACK  | CMD_MSG(STR_ERROR_CAN_T_BUILD_SIGNALS_HERE),
		CcPlaySound1E);
}


typedef void OnButtonClick(Window *w);

/** Data associated with a push button in the build rail toolbar window */
struct RailBuildingGUIButtonData {
	uint16 keycode;            ///< Keycode associated with the button
	OnButtonClick *click_proc; ///< Procedure to call when button is clicked
};

/**
 * GUI rail-building button data constants.
 * Offsets match widget order, starting at RTW_BUILD_NS
 */
static const RailBuildingGUIButtonData _rail_build_button_data[] = {
	{'1', BuildRailClick_N          },
	{'2', BuildRailClick_NE         },
	{'3', BuildRailClick_E          },
	{'4', BuildRailClick_NW         },
	{'5', BuildRailClick_AutoRail   },
	{'6', BuildRailClick_Demolish   },
	{'7', BuildRailClick_Depot      },
	{'8', BuildRailClick_Waypoint   },
	{'9', BuildRailClick_Station    },
	{'S', BuildRailClick_AutoSignals},
	{'B', BuildRailClick_Bridge     },
	{'T', BuildRailClick_Tunnel     },
	{'R', BuildRailClick_Remove     },
	{'C', BuildRailClick_Convert    }
};

/**
 * Based on the widget clicked, update the status of the 'remove' button.
 * @param w              Rail toolbar window
 * @param clicked_widget Widget clicked in the toolbar
 */
struct BuildRailToolbarWindow : Window {
	BuildRailToolbarWindow(const WindowDesc *desc, WindowNumber window_number, RailType railtype) : Window()
	{
		this->InitNested(desc);
		this->SetupRailToolbar(railtype);
		this->DisableWidget(RTW_REMOVE);

		if (_settings_client.gui.link_terraform_toolbar) ShowTerraformToolbar(this);
	}

	~BuildRailToolbarWindow()
	{
		if (_settings_client.gui.link_terraform_toolbar) DeleteWindowById(WC_SCEN_LAND_GEN, 0, false);
	}

	/** Configures the rail toolbar for railtype given
	 * @param railtype the railtype to display
	 */
	void SetupRailToolbar(RailType railtype)
	{
		const RailtypeInfo *rti = GetRailTypeInfo(railtype);

		assert(railtype < RAILTYPE_END);
		this->GetWidget<NWidgetCore>(RTW_CAPTION)->widget_data      = rti->strings.toolbar_caption;
		this->GetWidget<NWidgetCore>(RTW_BUILD_NS)->widget_data     = rti->gui_sprites.build_ns_rail;
		this->GetWidget<NWidgetCore>(RTW_BUILD_X)->widget_data      = rti->gui_sprites.build_x_rail;
		this->GetWidget<NWidgetCore>(RTW_BUILD_EW)->widget_data     = rti->gui_sprites.build_ew_rail;
		this->GetWidget<NWidgetCore>(RTW_BUILD_Y)->widget_data      = rti->gui_sprites.build_y_rail;
		this->GetWidget<NWidgetCore>(RTW_AUTORAIL)->widget_data     = rti->gui_sprites.auto_rail;
		this->GetWidget<NWidgetCore>(RTW_BUILD_DEPOT)->widget_data  = rti->gui_sprites.build_depot;
		this->GetWidget<NWidgetCore>(RTW_CONVERT_RAIL)->widget_data = rti->gui_sprites.convert_rail;
		this->GetWidget<NWidgetCore>(RTW_BUILD_TUNNEL)->widget_data = rti->gui_sprites.build_tunnel;
	}

	/** Switch to another rail type.
	 * @param railtype New rail type.
	 */
	void ModifyRailType(RailType railtype)
	{
		this->SetupRailToolbar(railtype);
		this->ReInit();
	}

	void UpdateRemoveWidgetStatus(int clicked_widget)
	{
		switch (clicked_widget) {
			case RTW_REMOVE:
				/* If it is the removal button that has been clicked, do nothing,
				 * as it is up to the other buttons to drive removal status */
				return;
				break;
			case RTW_BUILD_NS:
			case RTW_BUILD_X:
			case RTW_BUILD_EW:
			case RTW_BUILD_Y:
			case RTW_AUTORAIL:
			case RTW_BUILD_WAYPOINT:
			case RTW_BUILD_STATION:
			case RTW_BUILD_SIGNALS:
				/* Removal button is enabled only if the rail/signal/waypoint/station
				 * button is still lowered.  Once raised, it has to be disabled */
				this->SetWidgetDisabledState(RTW_REMOVE, !this->IsWidgetLowered(clicked_widget));
				break;

			default:
				/* When any other buttons than rail/signal/waypoint/station, raise and
				 * disable the removal button */
				this->DisableWidget(RTW_REMOVE);
				this->RaiseWidget(RTW_REMOVE);
				break;
		}
	}

	virtual void OnPaint()
	{
		this->DrawWidgets();
	}

	virtual void OnClick(Point pt, int widget, int click_count)
	{
		if (widget >= RTW_BUILD_NS) {
			_remove_button_clicked = false;
			_rail_build_button_data[widget - RTW_BUILD_NS].click_proc(this);
		}
		this->UpdateRemoveWidgetStatus(widget);
		if (_ctrl_pressed) RailToolbar_CtrlChanged(this);
	}

	virtual EventState OnKeyPress(uint16 key, uint16 keycode)
	{
		EventState state = ES_NOT_HANDLED;
		for (uint8 i = 0; i != lengthof(_rail_build_button_data); i++) {
			if (keycode == _rail_build_button_data[i].keycode) {
				_remove_button_clicked = false;
				_rail_build_button_data[i].click_proc(this);
				this->UpdateRemoveWidgetStatus(i + RTW_BUILD_NS);
				if (_ctrl_pressed) RailToolbar_CtrlChanged(this);
				state = ES_HANDLED;
				break;
			}
		}
		MarkTileDirtyByTile(TileVirtXY(_thd.pos.x, _thd.pos.y)); // redraw tile selection
		return state;
	}

	virtual void OnPlaceObject(Point pt, TileIndex tile)
	{
		_place_proc(tile);
	}

	virtual void OnPlaceDrag(ViewportPlaceMethod select_method, ViewportDragDropSelectionProcess select_proc, Point pt)
	{
		/* no dragging if you have pressed the convert button */
		if (FindWindowById(WC_BUILD_SIGNAL, 0) != NULL && _convert_signal_button && this->IsWidgetLowered(RTW_BUILD_SIGNALS)) return;

		VpSelectTilesWithMethod(pt.x, pt.y, select_method);
	}

	virtual void OnPlaceMouseUp(ViewportPlaceMethod select_method, ViewportDragDropSelectionProcess select_proc, Point pt, TileIndex start_tile, TileIndex end_tile)
	{
		if (pt.x != -1) {
			switch (select_proc) {
				default: NOT_REACHED();
				case DDSP_BUILD_BRIDGE:
					if (!_settings_client.gui.persistent_buildingtools) ResetObjectToPlace();
					ShowBuildBridgeWindow(start_tile, end_tile, TRANSPORT_RAIL, _cur_railtype);
					break;

				case DDSP_PLACE_RAIL:
					HandleAutodirPlacement();
					break;

				case DDSP_BUILD_SIGNALS:
					HandleAutoSignalPlacement();
					break;

				case DDSP_DEMOLISH_AREA:
					GUIPlaceProcDragXY(select_proc, start_tile, end_tile);
					break;

				case DDSP_CONVERT_RAIL:
					DoCommandP(end_tile, start_tile, _cur_railtype, CMD_CONVERT_RAIL | CMD_MSG(STR_ERROR_CAN_T_CONVERT_RAIL), CcPlaySound10);
					break;

				case DDSP_REMOVE_STATION:
				case DDSP_BUILD_STATION:
					if (this->IsWidgetLowered(RTW_BUILD_STATION)) {
						/* Station */
						if (_remove_button_clicked) {
							DoCommandP(end_tile, start_tile, _ctrl_pressed ? 0 : 1, CMD_REMOVE_FROM_RAIL_STATION | CMD_MSG(STR_ERROR_CAN_T_REMOVE_PART_OF_STATION), CcPlaySound1E);
						} else {
							HandleStationPlacement(start_tile, end_tile);
						}
					} else {
						/* Waypoint */
						if (_remove_button_clicked) {
							DoCommandP(end_tile, start_tile, _ctrl_pressed ? 0 : 1, CMD_REMOVE_FROM_RAIL_WAYPOINT | CMD_MSG(STR_ERROR_CAN_T_REMOVE_TRAIN_WAYPOINT), CcPlaySound1E);
						} else {
							TileArea ta(start_tile, end_tile);
							uint32 p1 = _cur_railtype | (select_method == VPM_FIX_X ? AXIS_X : AXIS_Y) << 4 | ta.w << 8 | ta.h << 16 | _ctrl_pressed << 24;
							uint32 p2 = STAT_CLASS_WAYP | _cur_waypoint_type << 8 | INVALID_STATION << 16;

							CommandContainer cmdcont = { ta.tile, p1, p2, CMD_BUILD_RAIL_WAYPOINT | CMD_MSG(STR_ERROR_CAN_T_BUILD_TRAIN_WAYPOINT), CcPlaySound1E, "" };
							ShowSelectWaypointIfNeeded(cmdcont, ta);
						}
					}
					break;
			}
		}
	}

	virtual void OnPlaceObjectAbort()
	{
		this->RaiseButtons();
		this->DisableWidget(RTW_REMOVE);
		this->SetWidgetDirty(RTW_REMOVE);

		DeleteWindowById(WC_BUILD_SIGNAL, TRANSPORT_RAIL);
		DeleteWindowById(WC_BUILD_STATION, TRANSPORT_RAIL);
		DeleteWindowById(WC_BUILD_DEPOT, TRANSPORT_RAIL);
		DeleteWindowById(WC_SELECT_STATION, 0);
		DeleteWindowByClass(WC_BUILD_BRIDGE);
	}

	virtual void OnPlacePresize(Point pt, TileIndex tile)
	{
		DoCommand(tile, _cur_railtype, 0, DC_AUTO, CMD_BUILD_TUNNEL);
		VpSetPresizeRange(tile, _build_tunnel_endtile == 0 ? tile : _build_tunnel_endtile);
	}

	virtual EventState OnCTRLStateChange()
	{
		/* do not toggle Remove button by Ctrl when placing station */
		if (!this->IsWidgetLowered(RTW_BUILD_STATION) && !this->IsWidgetLowered(RTW_BUILD_WAYPOINT) && RailToolbar_CtrlChanged(this)) return ES_HANDLED;
		return ES_NOT_HANDLED;
	}
};

static const NWidgetPart _nested_build_rail_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN, RTW_CAPTION), SetDataTip(STR_RAIL_TOOLBAR_RAILROAD_CONSTRUCTION_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_STICKYBOX, COLOUR_DARK_GREEN),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, RTW_BUILD_NS),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_RAIL_NS, STR_RAIL_TOOLBAR_TOOLTIP_BUILD_RAILROAD_TRACK),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, RTW_BUILD_X),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_RAIL_NE, STR_RAIL_TOOLBAR_TOOLTIP_BUILD_RAILROAD_TRACK),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, RTW_BUILD_EW),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_RAIL_EW, STR_RAIL_TOOLBAR_TOOLTIP_BUILD_RAILROAD_TRACK),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, RTW_BUILD_Y),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_RAIL_NW, STR_RAIL_TOOLBAR_TOOLTIP_BUILD_RAILROAD_TRACK),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, RTW_AUTORAIL),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_AUTORAIL, STR_RAIL_TOOLBAR_TOOLTIP_BUILD_AUTORAIL),

		NWidget(WWT_PANEL, COLOUR_DARK_GREEN), SetMinimalSize(4, 22), SetDataTip(0x0, STR_NULL), EndContainer(),

		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, RTW_DEMOLISH),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_DYNAMITE, STR_TOOLTIP_DEMOLISH_BUILDINGS_ETC),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, RTW_BUILD_DEPOT),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_DEPOT_RAIL, STR_RAIL_TOOLBAR_TOOLTIP_BUILD_TRAIN_DEPOT_FOR_BUILDING),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, RTW_BUILD_WAYPOINT),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_WAYPOINT, STR_RAIL_TOOLBAR_TOOLTIP_CONVERT_RAIL_TO_WAYPOINT),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, RTW_BUILD_STATION),
						SetFill(0, 1), SetMinimalSize(42, 22), SetDataTip(SPR_IMG_RAIL_STATION, STR_RAIL_TOOLBAR_TOOLTIP_BUILD_RAILROAD_STATION),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, RTW_BUILD_SIGNALS),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_RAIL_SIGNALS, STR_RAIL_TOOLBAR_TOOLTIP_BUILD_RAILROAD_SIGNALS),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, RTW_BUILD_BRIDGE),
						SetFill(0, 1), SetMinimalSize(42, 22), SetDataTip(SPR_IMG_BRIDGE, STR_RAIL_TOOLBAR_TOOLTIP_BUILD_RAILROAD_BRIDGE),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, RTW_BUILD_TUNNEL),
						SetFill(0, 1), SetMinimalSize(20, 22), SetDataTip(SPR_IMG_TUNNEL_RAIL, STR_RAIL_TOOLBAR_TOOLTIP_BUILD_RAILROAD_TUNNEL),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, RTW_REMOVE),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_REMOVE, STR_RAIL_TOOLBAR_TOOLTIP_TOGGLE_BUILD_REMOVE_FOR),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, RTW_CONVERT_RAIL),
						SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_CONVERT_RAIL, STR_RAIL_TOOLBAR_TOOLTIP_CONVERT_RAIL),
	EndContainer(),
};

static const WindowDesc _build_rail_desc(
	WDP_ALIGN_TOOLBAR, 0, 0,
	WC_BUILD_TOOLBAR, WC_NONE,
	WDF_CONSTRUCTION,
	_nested_build_rail_widgets, lengthof(_nested_build_rail_widgets)
);


/**
 * Open the build rail toolbar window for a specific rail type.
 * The window may be opened in the 'normal' way by clicking at the rail icon in
 * the main toolbar, or by means of selecting one of the functions of the
 * toolbar. In the latter case, the corresponding widget is also selected.
 *
 * If the terraform toolbar is linked to the toolbar, that window is also opened.
 *
 * @param railtype Rail type to open the window for
 * @param button   Widget clicked (\c -1 means no button clicked)
 */
void ShowBuildRailToolbar(RailType railtype, int button)
{
	if (!Company::IsValidID(_local_company)) return;
	if (!ValParamRailtype(railtype)) return;

	BuildRailToolbarWindow *w = (BuildRailToolbarWindow *)FindWindowById(WC_BUILD_TOOLBAR, TRANSPORT_RAIL);

	/* don't recreate the window if we're clicking on a button and the window exists. */
	if (button < 0 || w == NULL) {
		DeleteWindowByClass(WC_BUILD_TOOLBAR);
		_cur_railtype = railtype;
		w = new BuildRailToolbarWindow(&_build_rail_desc, TRANSPORT_RAIL, railtype);
	}

	_remove_button_clicked = false;
	if (w != NULL && button >= 0) {
		_rail_build_button_data[button].click_proc(w);
		w->UpdateRemoveWidgetStatus(button + RTW_BUILD_NS);
	}
}

/* TODO: For custom stations, respect their allowed platforms/lengths bitmasks!
 * --pasky */

static void HandleStationPlacement(TileIndex start, TileIndex end)
{
	TileArea ta(start, end);
	uint numtracks = ta.w;
	uint platlength = ta.h;

	if (_railstation.orientation == AXIS_X) Swap(numtracks, platlength);

	uint32 p1 = _cur_railtype | _railstation.orientation << 4 | numtracks << 8 | platlength << 16 | _ctrl_pressed << 24;
	uint32 p2 = _railstation.station_class | _railstation.station_type << 8 | INVALID_STATION << 16;

	CommandContainer cmdcont = { ta.tile, p1, p2, CMD_BUILD_RAIL_STATION | CMD_MSG(STR_ERROR_CAN_T_BUILD_RAILROAD_STATION), CcStation, "" };
	ShowSelectStationIfNeeded(cmdcont, ta);
}

/** Enum referring to the widgets of the rail stations window */
enum BuildRailStationWidgets {
	BRSW_BACKGROUND,

	BRSW_PLATFORM_DIR_X,
	BRSW_PLATFORM_DIR_Y,

	BRSW_PLATFORM_NUM_1,
	BRSW_PLATFORM_NUM_2,
	BRSW_PLATFORM_NUM_3,
	BRSW_PLATFORM_NUM_4,
	BRSW_PLATFORM_NUM_5,
	BRSW_PLATFORM_NUM_6,
	BRSW_PLATFORM_NUM_7,

	BRSW_PLATFORM_LEN_1,
	BRSW_PLATFORM_LEN_2,
	BRSW_PLATFORM_LEN_3,
	BRSW_PLATFORM_LEN_4,
	BRSW_PLATFORM_LEN_5,
	BRSW_PLATFORM_LEN_6,
	BRSW_PLATFORM_LEN_7,

	BRSW_PLATFORM_DRAG_N_DROP,

	BRSW_HIGHLIGHT_OFF,
	BRSW_HIGHLIGHT_ON,

	BRSW_NEWST_DROPDOWN,
	BRSW_NEWST_LIST,
	BRSW_NEWST_SCROLL,

	BRSW_PLATFORM_NUM_BEGIN = BRSW_PLATFORM_NUM_1 - 1,
	BRSW_PLATFORM_LEN_BEGIN = BRSW_PLATFORM_LEN_1 - 1,
};

struct BuildRailStationWindow : public PickerWindowBase {
private:
	uint line_height; ///< Height of a single line in the newstation selection matrix (#BRSW_NEWST_LIST widget).

	/**
	 * Verify whether the currently selected station size is allowed after selecting a new station class/type.
	 * If not, change the station size variables ( _settings_client.gui.station_numtracks and _settings_client.gui.station_platlength ).
	 * @param statspec Specification of the new station class/type
	 */
	void CheckSelectedSize(const StationSpec *statspec)
	{
		if (statspec == NULL || _settings_client.gui.station_dragdrop) return;

		/* If current number of tracks is not allowed, make it as big as possible (which is always less than currently selected) */
		if (HasBit(statspec->disallowed_platforms, _settings_client.gui.station_numtracks - 1)) {
			this->RaiseWidget(_settings_client.gui.station_numtracks + BRSW_PLATFORM_NUM_BEGIN);
			_settings_client.gui.station_numtracks = 1;
			while (HasBit(statspec->disallowed_platforms, _settings_client.gui.station_numtracks - 1)) {
				_settings_client.gui.station_numtracks++;
			}
			this->LowerWidget(_settings_client.gui.station_numtracks + BRSW_PLATFORM_NUM_BEGIN);
		}

		if (HasBit(statspec->disallowed_lengths, _settings_client.gui.station_platlength - 1)) {
			this->RaiseWidget(_settings_client.gui.station_platlength + BRSW_PLATFORM_LEN_BEGIN);
			_settings_client.gui.station_platlength = 1;
			while (HasBit(statspec->disallowed_lengths, _settings_client.gui.station_platlength - 1)) {
				_settings_client.gui.station_platlength++;
			}
			this->LowerWidget(_settings_client.gui.station_platlength + BRSW_PLATFORM_LEN_BEGIN);
		}
	}

	/** Build a dropdown list of available station classes */
	static DropDownList *BuildStationClassDropDown()
	{
		DropDownList *list = new DropDownList();

		for (uint i = 0; i < GetNumStationClasses(); i++) {
			if (i == STAT_CLASS_WAYP) continue;
			list->push_back(new DropDownListStringItem(GetStationClassName((StationClassID)i), i, false));
		}

		return list;
	}

public:
	BuildRailStationWindow(const WindowDesc *desc, Window *parent, bool newstation) : PickerWindowBase(parent)
	{
		this->InitNested(desc, TRANSPORT_RAIL);

		this->LowerWidget(_railstation.orientation + BRSW_PLATFORM_DIR_X);
		if (_settings_client.gui.station_dragdrop) {
			this->LowerWidget(BRSW_PLATFORM_DRAG_N_DROP);
		} else {
			this->LowerWidget(_settings_client.gui.station_numtracks + BRSW_PLATFORM_NUM_BEGIN);
			this->LowerWidget(_settings_client.gui.station_platlength + BRSW_PLATFORM_LEN_BEGIN);
		}
		this->SetWidgetLoweredState(BRSW_HIGHLIGHT_OFF, !_settings_client.gui.station_show_coverage);
		this->SetWidgetLoweredState(BRSW_HIGHLIGHT_ON, _settings_client.gui.station_show_coverage);

		_railstation.newstations = newstation;

		if (newstation) {
			_railstation.station_count = GetNumCustomStations(_railstation.station_class);

			this->vscroll.SetCount(_railstation.station_count);
			this->vscroll.SetCapacity(GB(this->GetWidget<NWidgetCore>(BRSW_NEWST_LIST)->widget_data, MAT_ROW_START, MAT_ROW_BITS));
			this->vscroll.SetPosition(Clamp(_railstation.station_type - 2, 0, max(this->vscroll.GetCount() - this->vscroll.GetCapacity(), 0)));
		} else {
			/* New stations are not available, so ensure the default station
			 * type is 'selected'. */
			_railstation.station_class = STAT_CLASS_DFLT;
			_railstation.station_type = 0;
		}
	}

	virtual ~BuildRailStationWindow()
	{
		DeleteWindowById(WC_SELECT_STATION, 0);
	}

	virtual void OnPaint()
	{
		bool newstations = _railstation.newstations;
		const StationSpec *statspec = newstations ? GetCustomStationSpec(_railstation.station_class, _railstation.station_type) : NULL;

		if (_settings_client.gui.station_dragdrop) {
			SetTileSelectSize(1, 1);
		} else {
			int x = _settings_client.gui.station_numtracks;
			int y = _settings_client.gui.station_platlength;
			if (_railstation.orientation == AXIS_X) Swap(x, y);
			if (!_remove_button_clicked)
				SetTileSelectSize(x, y);
		}

		int rad = (_settings_game.station.modified_catchment) ? CA_TRAIN : CA_UNMODIFIED;

		if (_settings_client.gui.station_show_coverage)
			SetTileSelectBigSize(-rad, -rad, 2 * rad, 2 * rad);

		for (uint bits = 0; bits < 7; bits++) {
			bool disable = bits >= _settings_game.station.station_spread;
			if (statspec == NULL) {
				this->SetWidgetDisabledState(bits + BRSW_PLATFORM_NUM_1, disable);
				this->SetWidgetDisabledState(bits + BRSW_PLATFORM_LEN_1, disable);
			} else {
				this->SetWidgetDisabledState(bits + BRSW_PLATFORM_NUM_1, HasBit(statspec->disallowed_platforms, bits) || disable);
				this->SetWidgetDisabledState(bits + BRSW_PLATFORM_LEN_1, HasBit(statspec->disallowed_lengths,   bits) || disable);
			}
		}

		this->DrawWidgets();

		/* 'Accepts' and 'Supplies' texts. */
		int top = this->GetWidget<NWidgetBase>(BRSW_HIGHLIGHT_ON)->pos_y + this->GetWidget<NWidgetBase>(BRSW_HIGHLIGHT_ON)->current_y + WD_PAR_VSEP_NORMAL;
		NWidgetBase *back_nwi = this->GetWidget<NWidgetBase>(BRSW_BACKGROUND);
		int right = back_nwi->pos_x +  back_nwi->current_x;
		int bottom = back_nwi->pos_y +  back_nwi->current_y;
		top = DrawStationCoverageAreaText(back_nwi->pos_x + WD_FRAMERECT_LEFT, right - WD_FRAMERECT_RIGHT, top, SCT_ALL, rad, false) + WD_PAR_VSEP_NORMAL;
		top = DrawStationCoverageAreaText(back_nwi->pos_x + WD_FRAMERECT_LEFT, right - WD_FRAMERECT_RIGHT, top, SCT_ALL, rad, true) + WD_PAR_VSEP_NORMAL;
		/* Resize background if the text is not equally long as the window. */
		if (top > bottom || (top < bottom && back_nwi->current_y > back_nwi->smallest_y)) {
			ResizeWindow(this, 0, top - bottom);
		}
	}

	virtual void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize)
	{
		switch (widget) {
			case BRSW_NEWST_DROPDOWN: {
				Dimension d = {0, 0};
				for (uint i = 0; i < GetNumStationClasses(); i++) {
					if (i == STAT_CLASS_WAYP) continue;
					SetDParam(0, GetStationClassName((StationClassID)i));
					d = maxdim(d, GetStringBoundingBox(STR_BLACK_STRING));
				}
				d.width += padding.width;
				d.height += padding.height;
				*size = maxdim(*size, d);
				break;
			}
			case BRSW_NEWST_LIST: {
				Dimension d = GetStringBoundingBox(STR_STATION_CLASS_DFLT);
				for (StationClassID statclass = STAT_CLASS_BEGIN; statclass < (StationClassID)GetNumStationClasses(); statclass++) {
					if (statclass == STAT_CLASS_WAYP) continue;
					for (uint16 j = 0; j < GetNumCustomStations(statclass); j++) {
						const StationSpec *statspec = GetCustomStationSpec(statclass, j);
						if (statspec != NULL && statspec->name != 0) d = maxdim(d, GetStringBoundingBox(statspec->name));
					}
				}
				size->width = max(size->width, d.width + padding.width);

				this->line_height = FONT_HEIGHT_NORMAL + WD_MATRIX_TOP + WD_MATRIX_BOTTOM;
				size->height = GB(this->GetWidget<NWidgetCore>(widget)->widget_data, MAT_ROW_START, MAT_ROW_BITS) * this->line_height;
				break;
			}
		}
	}

	virtual void DrawWidget(const Rect &r, int widget) const
	{
		DrawPixelInfo tmp_dpi;

		switch (widget) {
			case BRSW_PLATFORM_DIR_X:
				/* Set up a clipping area for the '/' station preview */
				if (FillDrawPixelInfo(&tmp_dpi, r.left, r.top, r.right - r.left + 1, r.bottom - r.top + 1)) {
					DrawPixelInfo *old_dpi = _cur_dpi;
					_cur_dpi = &tmp_dpi;
					if (!DrawStationTile(32, 16, _cur_railtype, AXIS_X, _railstation.station_class, _railstation.station_type)) {
						StationPickerDrawSprite(32, 16, STATION_RAIL, _cur_railtype, INVALID_ROADTYPE, 2);
					}
					_cur_dpi = old_dpi;
				}
				break;

			case BRSW_PLATFORM_DIR_Y:
				/* Set up a clipping area for the '\' station preview */
				if (FillDrawPixelInfo(&tmp_dpi, r.left, r.top, r.right - r.left + 1, r.bottom - r.top + 1)) {
					DrawPixelInfo *old_dpi = _cur_dpi;
					_cur_dpi = &tmp_dpi;
					if (!DrawStationTile(32, 16, _cur_railtype, AXIS_Y, _railstation.station_class, _railstation.station_type)) {
						StationPickerDrawSprite(32, 16, STATION_RAIL, _cur_railtype, INVALID_ROADTYPE, 3);
					}
					_cur_dpi = old_dpi;
				}
				break;

			case BRSW_NEWST_LIST: {
				uint y = r.top;
				for (uint16 i = this->vscroll.GetPosition(); i < _railstation.station_count && this->vscroll.IsVisible(i); i++) {
					const StationSpec *statspec = GetCustomStationSpec(_railstation.station_class, i);

					StringID str = STR_STATION_CLASS_DFLT;
					if (statspec != NULL && statspec->name != 0) {
						if (HasBit(statspec->callback_mask, CBM_STATION_AVAIL) && GB(GetStationCallback(CBID_STATION_AVAILABILITY, 0, 0, statspec, NULL, INVALID_TILE), 0, 8) == 0) {
							GfxFillRect(r.left + 1, y + 1, r.right - 1, y + this->line_height - 2, 0, FILLRECT_CHECKER);
						}
						str = statspec->name;
					}
					DrawString(r.left + WD_MATRIX_LEFT, r.right - WD_MATRIX_RIGHT, y + WD_MATRIX_TOP, str, i == _railstation.station_type ? TC_WHITE : TC_BLACK);

					y += this->line_height;
				}
				break;
			}
		}
	}

	virtual void SetStringParameters(int widget) const
	{
		if (widget == BRSW_NEWST_DROPDOWN) SetDParam(0, GetStationClassName(_railstation.station_class));
	}

	virtual void OnClick(Point pt, int widget, int click_count)
	{
		switch (widget) {
			case BRSW_PLATFORM_DIR_X:
			case BRSW_PLATFORM_DIR_Y:
				this->RaiseWidget(_railstation.orientation + BRSW_PLATFORM_DIR_X);
				_railstation.orientation = (Axis)(widget - BRSW_PLATFORM_DIR_X);
				this->LowerWidget(_railstation.orientation + BRSW_PLATFORM_DIR_X);
				SndPlayFx(SND_15_BEEP);
				this->SetDirty();
				DeleteWindowById(WC_SELECT_STATION, 0);
				break;

			case BRSW_PLATFORM_NUM_1:
			case BRSW_PLATFORM_NUM_2:
			case BRSW_PLATFORM_NUM_3:
			case BRSW_PLATFORM_NUM_4:
			case BRSW_PLATFORM_NUM_5:
			case BRSW_PLATFORM_NUM_6:
			case BRSW_PLATFORM_NUM_7: {
				this->RaiseWidget(_settings_client.gui.station_numtracks + BRSW_PLATFORM_NUM_BEGIN);
				this->RaiseWidget(BRSW_PLATFORM_DRAG_N_DROP);

				_settings_client.gui.station_numtracks = widget - BRSW_PLATFORM_NUM_BEGIN;
				_settings_client.gui.station_dragdrop = false;

				_settings_client.gui.station_dragdrop = false;

				const StationSpec *statspec = _railstation.newstations ? GetCustomStationSpec(_railstation.station_class, _railstation.station_type) : NULL;
				if (statspec != NULL && HasBit(statspec->disallowed_lengths, _settings_client.gui.station_platlength - 1)) {
					/* The previously selected number of platforms in invalid */
					for (uint i = 0; i < 7; i++) {
						if (!HasBit(statspec->disallowed_lengths, i)) {
							this->RaiseWidget(_settings_client.gui.station_platlength + BRSW_PLATFORM_LEN_BEGIN);
							_settings_client.gui.station_platlength = i + 1;
							break;
						}
					}
				}

				this->LowerWidget(_settings_client.gui.station_numtracks + BRSW_PLATFORM_NUM_BEGIN);
				this->LowerWidget(_settings_client.gui.station_platlength + BRSW_PLATFORM_LEN_BEGIN);
				SndPlayFx(SND_15_BEEP);
				this->SetDirty();
				DeleteWindowById(WC_SELECT_STATION, 0);
				break;
			}

			case BRSW_PLATFORM_LEN_1:
			case BRSW_PLATFORM_LEN_2:
			case BRSW_PLATFORM_LEN_3:
			case BRSW_PLATFORM_LEN_4:
			case BRSW_PLATFORM_LEN_5:
			case BRSW_PLATFORM_LEN_6:
			case BRSW_PLATFORM_LEN_7: {
				this->RaiseWidget(_settings_client.gui.station_platlength + BRSW_PLATFORM_LEN_BEGIN);
				this->RaiseWidget(BRSW_PLATFORM_DRAG_N_DROP);

				_settings_client.gui.station_platlength = widget - BRSW_PLATFORM_LEN_BEGIN;
				_settings_client.gui.station_dragdrop = false;

				_settings_client.gui.station_dragdrop = false;

				const StationSpec *statspec = _railstation.newstations ? GetCustomStationSpec(_railstation.station_class, _railstation.station_type) : NULL;
				if (statspec != NULL && HasBit(statspec->disallowed_platforms, _settings_client.gui.station_numtracks - 1)) {
					/* The previously selected number of tracks in invalid */
					for (uint i = 0; i < 7; i++) {
						if (!HasBit(statspec->disallowed_platforms, i)) {
							this->RaiseWidget(_settings_client.gui.station_numtracks + BRSW_PLATFORM_NUM_BEGIN);
							_settings_client.gui.station_numtracks = i + 1;
							break;
						}
					}
				}

				this->LowerWidget(_settings_client.gui.station_numtracks + BRSW_PLATFORM_NUM_BEGIN);
				this->LowerWidget(_settings_client.gui.station_platlength + BRSW_PLATFORM_LEN_BEGIN);
				SndPlayFx(SND_15_BEEP);
				this->SetDirty();
				DeleteWindowById(WC_SELECT_STATION, 0);
				break;
			}

			case BRSW_PLATFORM_DRAG_N_DROP: {
				_settings_client.gui.station_dragdrop ^= true;

				this->ToggleWidgetLoweredState(BRSW_PLATFORM_DRAG_N_DROP);

				/* get the first allowed length/number of platforms */
				const StationSpec *statspec = _railstation.newstations ? GetCustomStationSpec(_railstation.station_class, _railstation.station_type) : NULL;
				if (statspec != NULL && HasBit(statspec->disallowed_lengths, _settings_client.gui.station_platlength - 1)) {
					for (uint i = 0; i < 7; i++) {
						if (!HasBit(statspec->disallowed_lengths, i)) {
							this->RaiseWidget(_settings_client.gui.station_platlength + BRSW_PLATFORM_LEN_BEGIN);
							_settings_client.gui.station_platlength = i + 1;
							break;
						}
					}
				}
				if (statspec != NULL && HasBit(statspec->disallowed_platforms, _settings_client.gui.station_numtracks - 1)) {
					for (uint i = 0; i < 7; i++) {
						if (!HasBit(statspec->disallowed_platforms, i)) {
							this->RaiseWidget(_settings_client.gui.station_numtracks + BRSW_PLATFORM_NUM_BEGIN);
							_settings_client.gui.station_numtracks = i + 1;
							break;
						}
					}
				}

				this->SetWidgetLoweredState(_settings_client.gui.station_numtracks + BRSW_PLATFORM_NUM_BEGIN, !_settings_client.gui.station_dragdrop);
				this->SetWidgetLoweredState(_settings_client.gui.station_platlength + BRSW_PLATFORM_LEN_BEGIN, !_settings_client.gui.station_dragdrop);
				SndPlayFx(SND_15_BEEP);
				this->SetDirty();
				DeleteWindowById(WC_SELECT_STATION, 0);
			} break;

			case BRSW_HIGHLIGHT_OFF:
			case BRSW_HIGHLIGHT_ON:
				_settings_client.gui.station_show_coverage = (widget != BRSW_HIGHLIGHT_OFF);

				this->SetWidgetLoweredState(BRSW_HIGHLIGHT_OFF, !_settings_client.gui.station_show_coverage);
				this->SetWidgetLoweredState(BRSW_HIGHLIGHT_ON, _settings_client.gui.station_show_coverage);
				SndPlayFx(SND_15_BEEP);
				this->SetDirty();
				break;

			case BRSW_NEWST_DROPDOWN:
				ShowDropDownList(this, BuildStationClassDropDown(), _railstation.station_class, BRSW_NEWST_DROPDOWN);
				break;

			case BRSW_NEWST_LIST: {
				const StationSpec *statspec;
				int y = (pt.y - this->GetWidget<NWidgetBase>(BRSW_NEWST_LIST)->pos_y) / this->line_height;

				if (y >= this->vscroll.GetCapacity()) return;
				y += this->vscroll.GetPosition();
				if (y >= _railstation.station_count) return;

				/* Check station availability callback */
				statspec = GetCustomStationSpec(_railstation.station_class, y);
				if (statspec != NULL &&
					HasBit(statspec->callback_mask, CBM_STATION_AVAIL) &&
					GB(GetStationCallback(CBID_STATION_AVAILABILITY, 0, 0, statspec, NULL, INVALID_TILE), 0, 8) == 0) return;

				_railstation.station_type = y;

				this->CheckSelectedSize(statspec);

				SndPlayFx(SND_15_BEEP);
				this->SetDirty();
				DeleteWindowById(WC_SELECT_STATION, 0);
				break;
			}
		}
	}

	virtual void OnDropdownSelect(int widget, int index)
	{
		if (_railstation.station_class != index) {
			_railstation.station_class = (StationClassID)index;
			_railstation.station_type  = 0;
			_railstation.station_count = GetNumCustomStations(_railstation.station_class);

			this->CheckSelectedSize(GetCustomStationSpec(_railstation.station_class, _railstation.station_type));

			this->vscroll.SetCount(_railstation.station_count);
			this->vscroll.SetPosition(_railstation.station_type);
		}

		SndPlayFx(SND_15_BEEP);
		this->SetDirty();
		DeleteWindowById(WC_SELECT_STATION, 0);
	}

	virtual void OnTick()
	{
		CheckRedrawStationCoverage(this);
	}
};

static const NWidgetPart _nested_station_builder_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN), SetDataTip(STR_STATION_BUILD_RAIL_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_DARK_GREEN, BRSW_BACKGROUND),
		NWidget(WWT_LABEL, COLOUR_DARK_GREEN), SetMinimalSize(144, 11), SetDataTip(STR_STATION_BUILD_ORIENTATION, STR_NULL), SetPadding(1, 2, 0, 2),
		NWidget(NWID_HORIZONTAL),
			NWidget(NWID_SPACER), SetMinimalSize(7, 0), SetFill(1, 0),
			NWidget(WWT_PANEL, COLOUR_GREY, BRSW_PLATFORM_DIR_X), SetMinimalSize(66, 48), SetFill(0, 0), SetDataTip(0x0, STR_STATION_BUILD_RAILROAD_ORIENTATION_TOOLTIP), EndContainer(),
			NWidget(NWID_SPACER), SetMinimalSize(2, 0), SetFill(1, 0),
			NWidget(WWT_PANEL, COLOUR_GREY, BRSW_PLATFORM_DIR_Y), SetMinimalSize(66, 48), SetFill(0, 0), SetDataTip(0x0, STR_STATION_BUILD_RAILROAD_ORIENTATION_TOOLTIP), EndContainer(),
			NWidget(NWID_SPACER), SetMinimalSize(7, 0), SetFill(1, 0),
		EndContainer(),
		NWidget(WWT_LABEL, COLOUR_DARK_GREEN), SetMinimalSize(144, 11), SetDataTip(STR_STATION_BUILD_NUMBER_OF_TRACKS, STR_NULL), SetPadding(2, 2, 0, 2),
		NWidget(NWID_HORIZONTAL),
			NWidget(NWID_SPACER), SetFill(1, 0),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, BRSW_PLATFORM_NUM_1), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_1, STR_STATION_BUILD_NUMBER_OF_TRACKS_TOOLTIP),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, BRSW_PLATFORM_NUM_2), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_2, STR_STATION_BUILD_NUMBER_OF_TRACKS_TOOLTIP),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, BRSW_PLATFORM_NUM_3), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_3, STR_STATION_BUILD_NUMBER_OF_TRACKS_TOOLTIP),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, BRSW_PLATFORM_NUM_4), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_4, STR_STATION_BUILD_NUMBER_OF_TRACKS_TOOLTIP),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, BRSW_PLATFORM_NUM_5), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_5, STR_STATION_BUILD_NUMBER_OF_TRACKS_TOOLTIP),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, BRSW_PLATFORM_NUM_6), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_6, STR_STATION_BUILD_NUMBER_OF_TRACKS_TOOLTIP),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, BRSW_PLATFORM_NUM_7), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_7, STR_STATION_BUILD_NUMBER_OF_TRACKS_TOOLTIP),
			NWidget(NWID_SPACER), SetFill(1, 0),
		EndContainer(),
		NWidget(WWT_LABEL, COLOUR_DARK_GREEN), SetMinimalSize(144, 11), SetDataTip(STR_STATION_BUILD_PLATFORM_LENGTH, STR_NULL), SetPadding(2, 2, 0, 2),
		NWidget(NWID_HORIZONTAL),
			NWidget(NWID_SPACER), SetFill(1, 0),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, BRSW_PLATFORM_LEN_1), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_1, STR_STATION_BUILD_PLATFORM_LENGTH_TOOLTIP),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, BRSW_PLATFORM_LEN_2), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_2, STR_STATION_BUILD_PLATFORM_LENGTH_TOOLTIP),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, BRSW_PLATFORM_LEN_3), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_3, STR_STATION_BUILD_PLATFORM_LENGTH_TOOLTIP),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, BRSW_PLATFORM_LEN_4), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_4, STR_STATION_BUILD_PLATFORM_LENGTH_TOOLTIP),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, BRSW_PLATFORM_LEN_5), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_5, STR_STATION_BUILD_PLATFORM_LENGTH_TOOLTIP),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, BRSW_PLATFORM_LEN_6), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_6, STR_STATION_BUILD_PLATFORM_LENGTH_TOOLTIP),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, BRSW_PLATFORM_LEN_7), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_7, STR_STATION_BUILD_PLATFORM_LENGTH_TOOLTIP),
			NWidget(NWID_SPACER), SetFill(1, 0),
		EndContainer(),
		NWidget(NWID_SPACER), SetMinimalSize(0, 2),
		NWidget(NWID_HORIZONTAL),
			NWidget(NWID_SPACER), SetMinimalSize(2, 0), SetFill(1, 0),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, BRSW_PLATFORM_DRAG_N_DROP), SetMinimalSize(75, 12), SetDataTip(STR_STATION_BUILD_DRAG_DROP, STR_STATION_BUILD_DRAG_DROP_TOOLTIP),
			NWidget(NWID_SPACER), SetMinimalSize(2, 0), SetFill(1, 0),
		EndContainer(),
		NWidget(WWT_LABEL, COLOUR_DARK_GREEN), SetMinimalSize(144, 11), SetDataTip(STR_STATION_BUILD_COVERAGE_AREA_TITLE, STR_NULL), SetPadding(3, 2, 0, 2),
		NWidget(NWID_HORIZONTAL),
			NWidget(NWID_SPACER), SetMinimalSize(2, 0), SetFill(1, 0),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, BRSW_HIGHLIGHT_OFF), SetMinimalSize(60, 12),
										SetDataTip(STR_STATION_BUILD_COVERAGE_OFF, STR_STATION_BUILD_COVERAGE_AREA_OFF_TOOLTIP),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, BRSW_HIGHLIGHT_ON), SetMinimalSize(60, 12),
										SetDataTip(STR_STATION_BUILD_COVERAGE_ON, STR_STATION_BUILD_COVERAGE_AREA_ON_TOOLTIP),
			NWidget(NWID_SPACER), SetMinimalSize(2, 0), SetFill(1, 0),
		EndContainer(),
		NWidget(NWID_SPACER), SetMinimalSize(0, 20), SetResize(0, 1),
	EndContainer(),
};

static const NWidgetPart _nested_newstation_builder_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN), SetDataTip(STR_STATION_BUILD_RAIL_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_DARK_GREEN, BRSW_BACKGROUND),
		/* begin newstations gui additions. */
		NWidget(WWT_DROPDOWN, COLOUR_GREY, BRSW_NEWST_DROPDOWN), SetMinimalSize(134, 12), SetFill(1, 0), SetPadding(3, 7, 3, 7), SetDataTip(STR_BLACK_STRING, STR_STATION_BUILD_STATION_CLASS_TOOLTIP),
		NWidget(NWID_HORIZONTAL), SetPIP(7, 0, 7),
			NWidget(WWT_MATRIX, COLOUR_GREY, BRSW_NEWST_LIST), SetMinimalSize(122, 71), SetFill(1, 0), SetDataTip(0x501, STR_STATION_BUILD_STATION_TYPE_TOOLTIP),
			NWidget(WWT_SCROLLBAR, COLOUR_GREY, BRSW_NEWST_SCROLL),
		EndContainer(),
		NWidget(NWID_SPACER), SetMinimalSize(0, 1),
		/* end newstations gui additions. */
		NWidget(WWT_LABEL, COLOUR_DARK_GREEN), SetMinimalSize(144, 11), SetDataTip(STR_STATION_BUILD_ORIENTATION, STR_NULL), SetPadding(1, 2, 0, 2),
		NWidget(NWID_HORIZONTAL),
			NWidget(NWID_SPACER), SetMinimalSize(7, 0), SetFill(1, 0),
			NWidget(WWT_PANEL, COLOUR_GREY, BRSW_PLATFORM_DIR_X), SetMinimalSize(66, 48), SetFill(0, 0), SetDataTip(0x0, STR_STATION_BUILD_RAILROAD_ORIENTATION_TOOLTIP), EndContainer(),
			NWidget(NWID_SPACER), SetMinimalSize(2, 0), SetFill(1, 0),
			NWidget(WWT_PANEL, COLOUR_GREY, BRSW_PLATFORM_DIR_Y), SetMinimalSize(66, 48), SetFill(0, 0), SetDataTip(0x0, STR_STATION_BUILD_RAILROAD_ORIENTATION_TOOLTIP), EndContainer(),
			NWidget(NWID_SPACER), SetMinimalSize(7, 0), SetFill(1, 0),
		EndContainer(),
		NWidget(WWT_LABEL, COLOUR_DARK_GREEN), SetMinimalSize(144, 11), SetDataTip(STR_STATION_BUILD_NUMBER_OF_TRACKS, STR_NULL), SetPadding(2, 2, 0, 2),
		NWidget(NWID_HORIZONTAL),
			NWidget(NWID_SPACER), SetFill(1, 0),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, BRSW_PLATFORM_NUM_1), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_1, STR_STATION_BUILD_NUMBER_OF_TRACKS_TOOLTIP),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, BRSW_PLATFORM_NUM_2), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_2, STR_STATION_BUILD_NUMBER_OF_TRACKS_TOOLTIP),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, BRSW_PLATFORM_NUM_3), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_3, STR_STATION_BUILD_NUMBER_OF_TRACKS_TOOLTIP),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, BRSW_PLATFORM_NUM_4), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_4, STR_STATION_BUILD_NUMBER_OF_TRACKS_TOOLTIP),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, BRSW_PLATFORM_NUM_5), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_5, STR_STATION_BUILD_NUMBER_OF_TRACKS_TOOLTIP),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, BRSW_PLATFORM_NUM_6), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_6, STR_STATION_BUILD_NUMBER_OF_TRACKS_TOOLTIP),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, BRSW_PLATFORM_NUM_7), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_7, STR_STATION_BUILD_NUMBER_OF_TRACKS_TOOLTIP),
			NWidget(NWID_SPACER), SetFill(1, 0),
		EndContainer(),
		NWidget(WWT_LABEL, COLOUR_DARK_GREEN), SetMinimalSize(144, 11), SetDataTip(STR_STATION_BUILD_PLATFORM_LENGTH, STR_NULL), SetPadding(2, 2, 0, 2),
		NWidget(NWID_HORIZONTAL),
			NWidget(NWID_SPACER), SetFill(1, 0),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, BRSW_PLATFORM_LEN_1), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_1, STR_STATION_BUILD_PLATFORM_LENGTH_TOOLTIP),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, BRSW_PLATFORM_LEN_2), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_2, STR_STATION_BUILD_PLATFORM_LENGTH_TOOLTIP),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, BRSW_PLATFORM_LEN_3), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_3, STR_STATION_BUILD_PLATFORM_LENGTH_TOOLTIP),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, BRSW_PLATFORM_LEN_4), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_4, STR_STATION_BUILD_PLATFORM_LENGTH_TOOLTIP),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, BRSW_PLATFORM_LEN_5), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_5, STR_STATION_BUILD_PLATFORM_LENGTH_TOOLTIP),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, BRSW_PLATFORM_LEN_6), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_6, STR_STATION_BUILD_PLATFORM_LENGTH_TOOLTIP),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, BRSW_PLATFORM_LEN_7), SetMinimalSize(15, 12), SetDataTip(STR_BLACK_7, STR_STATION_BUILD_PLATFORM_LENGTH_TOOLTIP),
			NWidget(NWID_SPACER), SetFill(1, 0),
		EndContainer(),
		NWidget(NWID_SPACER), SetMinimalSize(0, 2),
		NWidget(NWID_HORIZONTAL),
			NWidget(NWID_SPACER), SetMinimalSize(2, 0), SetFill(1, 0),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, BRSW_PLATFORM_DRAG_N_DROP), SetMinimalSize(75, 12), SetDataTip(STR_STATION_BUILD_DRAG_DROP, STR_STATION_BUILD_DRAG_DROP_TOOLTIP),
			NWidget(NWID_SPACER), SetMinimalSize(2, 0), SetFill(1, 0),
		EndContainer(),
		NWidget(WWT_LABEL, COLOUR_DARK_GREEN), SetMinimalSize(144, 11), SetDataTip(STR_STATION_BUILD_COVERAGE_AREA_TITLE, STR_NULL), SetPadding(3, 2, 0, 2),
		NWidget(NWID_HORIZONTAL),
			NWidget(NWID_SPACER), SetMinimalSize(2, 0), SetFill(1, 0),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, BRSW_HIGHLIGHT_OFF), SetMinimalSize(60, 12),
										SetDataTip(STR_STATION_BUILD_COVERAGE_OFF, STR_STATION_BUILD_COVERAGE_AREA_OFF_TOOLTIP),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, BRSW_HIGHLIGHT_ON), SetMinimalSize(60, 12),
										SetDataTip(STR_STATION_BUILD_COVERAGE_ON, STR_STATION_BUILD_COVERAGE_AREA_ON_TOOLTIP),
			NWidget(NWID_SPACER), SetMinimalSize(2, 0), SetFill(1, 0),
		EndContainer(),
		NWidget(NWID_SPACER), SetMinimalSize(0, 20), SetResize(0, 1),
	EndContainer(),
};

/** High level window description of the default station-build window */
static const WindowDesc _station_builder_desc(
	WDP_AUTO, 0, 0,
	WC_BUILD_STATION, WC_BUILD_TOOLBAR,
	WDF_CONSTRUCTION,
	_nested_station_builder_widgets, lengthof(_nested_station_builder_widgets)
);

/** High level window description of the newGRF station-build window */
static const WindowDesc _newstation_builder_desc(
	WDP_AUTO, 0, 0,
	WC_BUILD_STATION, WC_BUILD_TOOLBAR,
	WDF_CONSTRUCTION,
	_nested_newstation_builder_widgets, lengthof(_nested_newstation_builder_widgets)
);

/** Open station build window */
static void ShowStationBuilder(Window *parent)
{
	if (GetNumStationClasses() <= 2 && GetNumCustomStations(STAT_CLASS_DFLT) == 1) {
		new BuildRailStationWindow(&_station_builder_desc, parent, false);
	} else {
		new BuildRailStationWindow(&_newstation_builder_desc, parent, true);
	}
}

/** Enum referring to the widgets of the signal window */
enum BuildSignalWidgets {
	BSW_SEMAPHORE_NORM,
	BSW_SEMAPHORE_ENTRY,
	BSW_SEMAPHORE_EXIT,
	BSW_SEMAPHORE_COMBO,
	BSW_SEMAPHORE_PBS,
	BSW_SEMAPHORE_PBS_OWAY,
	BSW_ELECTRIC_NORM,
	BSW_ELECTRIC_ENTRY,
	BSW_ELECTRIC_EXIT,
	BSW_ELECTRIC_COMBO,
	BSW_ELECTRIC_PBS,
	BSW_ELECTRIC_PBS_OWAY,
	BSW_CONVERT,
	BSW_DRAG_SIGNALS_DENSITY,
	BSW_DRAG_SIGNALS_DENSITY_LABEL,
	BSW_DRAG_SIGNALS_DENSITY_DECREASE,
	BSW_DRAG_SIGNALS_DENSITY_INCREASE,
};

struct BuildSignalWindow : public PickerWindowBase {
private:
	/**
	 * Draw dynamic a signal-sprite in a button in the signal GUI
	 * Draw the sprite +1px to the right and down if the button is lowered and change the sprite to sprite + 1 (red to green light)
	 *
	 * @param widget_index index of this widget in the window
	 * @param image        the sprite to draw
	 */
	void DrawSignalSprite(byte widget_index, SpriteID image) const
	{
		/* First get the right image, which is one later for 'green' signals. */
		image += this->IsWidgetLowered(widget_index);

		/* Next get the actual sprite so we can calculate the right offsets. */
		const Sprite *sprite = GetSprite(image, ST_NORMAL);

		/* For the x offset we want the sprite to be centered, so undo the offset
		 * for sprite drawing and add half of the sprite's width. For the y offset
		 * we want the sprite to be aligned on the bottom, so again we undo the
		 * offset for sprite drawing and assume it is the bottom of the sprite. */
		int sprite_center_x_offset = sprite->x_offs + sprite->width / 2;
		int sprite_bottom_y_offset = sprite->height + sprite->y_offs;

		/* Next we want to know where on the window to draw. Calculate the center
		 * and the bottom of the area to draw. */
		const NWidgetBase *widget = this->GetWidget<NWidgetBase>(widget_index);
		int widget_center_x = widget->pos_x + widget->current_x / 2;
		int widget_bottom_y = widget->pos_y + widget->current_y - 2;

		/* Finally we draw the signal. */
		DrawSprite(image, PAL_NONE,
				widget_center_x - sprite_center_x_offset + this->IsWidgetLowered(widget_index),
				widget_bottom_y - sprite_bottom_y_offset + this->IsWidgetLowered(widget_index));
	}

public:
	BuildSignalWindow(const WindowDesc *desc, Window *parent) : PickerWindowBase(parent)
	{
		this->InitNested(desc, TRANSPORT_RAIL);
		this->OnInvalidateData();
	};

	virtual void SetStringParameters(int widget) const
	{
		switch (widget) {
			case BSW_DRAG_SIGNALS_DENSITY_LABEL:
				SetDParam(0, _settings_client.gui.drag_signals_density);
				break;
		}
	}

	virtual void OnPaint()
	{
		this->DrawWidgets();
	}

	virtual void DrawWidget(const Rect &r, int widget) const
	{
		if (IsInsideMM(widget, BSW_SEMAPHORE_NORM, BSW_ELECTRIC_PBS_OWAY + 1)) {
			/* We need to do some custom sprite widget drawing for the signals. */
			const SpriteID _signal_lookup[] = {
				SPR_IMG_SIGNAL_SEMAPHORE_NORM,  SPR_IMG_SIGNAL_SEMAPHORE_ENTRY, SPR_IMG_SIGNAL_SEMAPHORE_EXIT,
				SPR_IMG_SIGNAL_SEMAPHORE_COMBO, SPR_IMG_SIGNAL_SEMAPHORE_PBS,   SPR_IMG_SIGNAL_SEMAPHORE_PBS_OWAY,

				SPR_IMG_SIGNAL_ELECTRIC_NORM,  SPR_IMG_SIGNAL_ELECTRIC_ENTRY, SPR_IMG_SIGNAL_ELECTRIC_EXIT,
				SPR_IMG_SIGNAL_ELECTRIC_COMBO, SPR_IMG_SIGNAL_ELECTRIC_PBS,   SPR_IMG_SIGNAL_ELECTRIC_PBS_OWAY
			};

			this->DrawSignalSprite(widget, _signal_lookup[widget - BSW_SEMAPHORE_NORM]);
		}
	}

	virtual void OnClick(Point pt, int widget, int click_count)
	{
		switch (widget) {
			case BSW_SEMAPHORE_NORM:
			case BSW_SEMAPHORE_ENTRY:
			case BSW_SEMAPHORE_EXIT:
			case BSW_SEMAPHORE_COMBO:
			case BSW_SEMAPHORE_PBS:
			case BSW_SEMAPHORE_PBS_OWAY:
			case BSW_ELECTRIC_NORM:
			case BSW_ELECTRIC_ENTRY:
			case BSW_ELECTRIC_EXIT:
			case BSW_ELECTRIC_COMBO:
			case BSW_ELECTRIC_PBS:
			case BSW_ELECTRIC_PBS_OWAY:
				this->RaiseWidget((_cur_signal_variant == SIG_ELECTRIC ? BSW_ELECTRIC_NORM : BSW_SEMAPHORE_NORM) + _cur_signal_type);

				_cur_signal_type = (SignalType)((uint)((widget - BSW_SEMAPHORE_NORM) % (SIGTYPE_LAST + 1)));
				_cur_signal_variant = widget >= BSW_ELECTRIC_NORM ? SIG_ELECTRIC : SIG_SEMAPHORE;
				break;

			case BSW_CONVERT:
				_convert_signal_button = !_convert_signal_button;
				break;

			case BSW_DRAG_SIGNALS_DENSITY_DECREASE:
				if (_settings_client.gui.drag_signals_density > 1) {
					_settings_client.gui.drag_signals_density--;
					SetWindowDirty(WC_GAME_OPTIONS, 0);
				}
				break;

			case BSW_DRAG_SIGNALS_DENSITY_INCREASE:
				if (_settings_client.gui.drag_signals_density < 20) {
					_settings_client.gui.drag_signals_density++;
					SetWindowDirty(WC_GAME_OPTIONS, 0);
				}
				break;

			default: break;
		}

		this->InvalidateData();
	}

	virtual void OnInvalidateData(int data = 0)
	{
		this->LowerWidget((_cur_signal_variant == SIG_ELECTRIC ? BSW_ELECTRIC_NORM : BSW_SEMAPHORE_NORM) + _cur_signal_type);

		this->SetWidgetLoweredState(BSW_CONVERT, _convert_signal_button);

		this->SetWidgetDisabledState(BSW_DRAG_SIGNALS_DENSITY_DECREASE, _settings_client.gui.drag_signals_density == 1);
		this->SetWidgetDisabledState(BSW_DRAG_SIGNALS_DENSITY_INCREASE, _settings_client.gui.drag_signals_density == 20);
	}
};

/** Nested widget definition of the build signal window */
static const NWidgetPart _nested_signal_builder_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN), SetDataTip(STR_BUILD_SIGNAL_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
	EndContainer(),
	NWidget(NWID_VERTICAL, NC_EQUALSIZE),
		NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
			NWidget(WWT_PANEL, COLOUR_DARK_GREEN, BSW_SEMAPHORE_NORM), SetDataTip(STR_NULL, STR_BUILD_SIGNAL_SEMAPHORE_NORM_TOOLTIP), EndContainer(), SetFill(1, 1),
			NWidget(WWT_PANEL, COLOUR_DARK_GREEN, BSW_SEMAPHORE_ENTRY), SetDataTip(STR_NULL, STR_BUILD_SIGNAL_SEMAPHORE_ENTRY_TOOLTIP), EndContainer(), SetFill(1, 1),
			NWidget(WWT_PANEL, COLOUR_DARK_GREEN, BSW_SEMAPHORE_EXIT), SetDataTip(STR_NULL, STR_BUILD_SIGNAL_SEMAPHORE_EXIT_TOOLTIP), EndContainer(), SetFill(1, 1),
			NWidget(WWT_PANEL, COLOUR_DARK_GREEN, BSW_SEMAPHORE_COMBO), SetDataTip(STR_NULL, STR_BUILD_SIGNAL_SEMAPHORE_COMBO_TOOLTIP), EndContainer(), SetFill(1, 1),
			NWidget(WWT_PANEL, COLOUR_DARK_GREEN, BSW_SEMAPHORE_PBS), SetDataTip(STR_NULL, STR_BUILD_SIGNAL_SEMAPHORE_PBS_TOOLTIP), EndContainer(), SetFill(1, 1),
			NWidget(WWT_PANEL, COLOUR_DARK_GREEN, BSW_SEMAPHORE_PBS_OWAY), SetDataTip(STR_NULL, STR_BUILD_SIGNAL_SEMAPHORE_PBS_OWAY_TOOLTIP), EndContainer(), SetFill(1, 1),
			NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, BSW_CONVERT), SetDataTip(SPR_IMG_SIGNAL_CONVERT, STR_BUILD_SIGNAL_CONVERT_TOOLTIP), SetFill(1, 1),
		EndContainer(),
		NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
			NWidget(WWT_PANEL, COLOUR_DARK_GREEN, BSW_ELECTRIC_NORM), SetDataTip(STR_NULL, STR_BUILD_SIGNAL_ELECTRIC_NORM_TOOLTIP), EndContainer(), SetFill(1, 1),
			NWidget(WWT_PANEL, COLOUR_DARK_GREEN, BSW_ELECTRIC_ENTRY), SetDataTip(STR_NULL, STR_BUILD_SIGNAL_ELECTRIC_ENTRY_TOOLTIP), EndContainer(), SetFill(1, 1),
			NWidget(WWT_PANEL, COLOUR_DARK_GREEN, BSW_ELECTRIC_EXIT), SetDataTip(STR_NULL, STR_BUILD_SIGNAL_ELECTRIC_EXIT_TOOLTIP), EndContainer(), SetFill(1, 1),
			NWidget(WWT_PANEL, COLOUR_DARK_GREEN, BSW_ELECTRIC_COMBO), SetDataTip(STR_NULL, STR_BUILD_SIGNAL_ELECTRIC_COMBO_TOOLTIP), EndContainer(), SetFill(1, 1),
			NWidget(WWT_PANEL, COLOUR_DARK_GREEN, BSW_ELECTRIC_PBS), SetDataTip(STR_NULL, STR_BUILD_SIGNAL_ELECTRIC_PBS_TOOLTIP), EndContainer(), SetFill(1, 1),
			NWidget(WWT_PANEL, COLOUR_DARK_GREEN, BSW_ELECTRIC_PBS_OWAY), SetDataTip(STR_NULL, STR_BUILD_SIGNAL_ELECTRIC_PBS_OWAY_TOOLTIP), EndContainer(), SetFill(1, 1),
			NWidget(WWT_PANEL, COLOUR_DARK_GREEN, BSW_DRAG_SIGNALS_DENSITY), SetDataTip(STR_NULL, STR_BUILD_SIGNAL_DRAG_SIGNALS_DENSITY_TOOLTIP), SetFill(1, 1),
				NWidget(WWT_LABEL, COLOUR_DARK_GREEN, BSW_DRAG_SIGNALS_DENSITY_LABEL), SetDataTip(STR_ORANGE_INT, STR_BUILD_SIGNAL_DRAG_SIGNALS_DENSITY_TOOLTIP), SetFill(1, 1),
				NWidget(NWID_HORIZONTAL), SetPIP(2, 0, 2),
					NWidget(NWID_SPACER), SetFill(1, 0),
					NWidget(NWID_BUTTON_ARROW, COLOUR_GREY, BSW_DRAG_SIGNALS_DENSITY_DECREASE), SetMinimalSize(9, 12), SetDataTip(AWV_DECREASE, STR_BUILD_SIGNAL_DRAG_SIGNALS_DENSITY_DECREASE_TOOLTIP),
					NWidget(NWID_BUTTON_ARROW, COLOUR_GREY, BSW_DRAG_SIGNALS_DENSITY_INCREASE), SetMinimalSize(9, 12), SetDataTip(AWV_INCREASE, STR_BUILD_SIGNAL_DRAG_SIGNALS_DENSITY_INCREASE_TOOLTIP),
					NWidget(NWID_SPACER), SetFill(1, 0),
				EndContainer(),
				NWidget(NWID_SPACER), SetMinimalSize(0, 2), SetFill(1, 0),
			EndContainer(),
		EndContainer(),
	EndContainer(),
};

/** Signal selection window description */
static const WindowDesc _signal_builder_desc(
	WDP_AUTO, 0, 0,
	WC_BUILD_SIGNAL, WC_BUILD_TOOLBAR,
	WDF_UNCLICK_BUTTONS | WDF_CONSTRUCTION,
	_nested_signal_builder_widgets, lengthof(_nested_signal_builder_widgets)
);

/**
 * Open the signal selection window
 */
static void ShowSignalBuilder(Window *parent)
{
	new BuildSignalWindow(&_signal_builder_desc, parent);
}

/** Enum referring to the widgets of the build rail depot window */
enum BuildRailDepotWidgets {
	BRDW_DEPOT_NE,
	BRDW_DEPOT_SE,
	BRDW_DEPOT_SW,
	BRDW_DEPOT_NW,
};

struct BuildRailDepotWindow : public PickerWindowBase {
	BuildRailDepotWindow(const WindowDesc *desc, Window *parent) : PickerWindowBase(parent)
	{
		this->InitNested(desc, TRANSPORT_RAIL);
		this->LowerWidget(_build_depot_direction + BRDW_DEPOT_NE);
	}

	virtual void OnPaint()
	{
		this->DrawWidgets();
	}

	virtual void DrawWidget(const Rect &r, int widget) const
	{
		if (!IsInsideMM(widget, BRDW_DEPOT_NE, BRDW_DEPOT_NW + 1)) return;

		DrawTrainDepotSprite(r.left - 1, r.top, widget - BRDW_DEPOT_NE + DIAGDIR_NE, _cur_railtype);
	}

	virtual void OnClick(Point pt, int widget, int click_count)
	{
		switch (widget) {
			case BRDW_DEPOT_NE:
			case BRDW_DEPOT_SE:
			case BRDW_DEPOT_SW:
			case BRDW_DEPOT_NW:
				this->RaiseWidget(_build_depot_direction + BRDW_DEPOT_NE);
				_build_depot_direction = (DiagDirection)(widget - BRDW_DEPOT_NE);
				this->LowerWidget(_build_depot_direction + BRDW_DEPOT_NE);
				SndPlayFx(SND_15_BEEP);
				this->SetDirty();
				break;
		}
	}
};

/** Nested widget definition of the build rail depot window */
static const NWidgetPart _nested_build_depot_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN), SetDataTip(STR_BUILD_DEPOT_TRAIN_ORIENTATION_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_DARK_GREEN),
		NWidget(NWID_SPACER), SetMinimalSize(0, 3),
		NWidget(NWID_HORIZONTAL_LTR),
			NWidget(NWID_SPACER), SetMinimalSize(3, 0), SetFill(1, 0),
			NWidget(NWID_VERTICAL),
				NWidget(WWT_PANEL, COLOUR_GREY, BRDW_DEPOT_NW), SetMinimalSize(66, 50), SetDataTip(0x0, STR_BUILD_DEPOT_TRAIN_ORIENTATION_TOOLTIP),
				EndContainer(),
				NWidget(NWID_SPACER), SetMinimalSize(0, 2),
				NWidget(WWT_PANEL, COLOUR_GREY, BRDW_DEPOT_SW), SetMinimalSize(66, 50), SetDataTip(0x0, STR_BUILD_DEPOT_TRAIN_ORIENTATION_TOOLTIP),
				EndContainer(),
			EndContainer(),
			NWidget(NWID_SPACER), SetMinimalSize(2, 0),
			NWidget(NWID_VERTICAL),
				NWidget(WWT_PANEL, COLOUR_GREY, BRDW_DEPOT_NE), SetMinimalSize(66, 50), SetDataTip(0x0, STR_BUILD_DEPOT_TRAIN_ORIENTATION_TOOLTIP),
				EndContainer(),
				NWidget(NWID_SPACER), SetMinimalSize(0, 2),
				NWidget(WWT_PANEL, COLOUR_GREY, BRDW_DEPOT_SE), SetMinimalSize(66, 50), SetDataTip(0x0, STR_BUILD_DEPOT_TRAIN_ORIENTATION_TOOLTIP),
				EndContainer(),
			EndContainer(),
			NWidget(NWID_SPACER), SetMinimalSize(3, 0), SetFill(1, 0),
		EndContainer(),
		NWidget(NWID_SPACER), SetMinimalSize(0, 3),
	EndContainer(),
};

static const WindowDesc _build_depot_desc(
	WDP_AUTO, 0, 0,
	WC_BUILD_DEPOT, WC_BUILD_TOOLBAR,
	WDF_CONSTRUCTION,
	_nested_build_depot_widgets, lengthof(_nested_build_depot_widgets)
);

static void ShowBuildTrainDepotPicker(Window *parent)
{
	new BuildRailDepotWindow(&_build_depot_desc, parent);
}

/** Enum referring to the widgets of the build NewGRF rail waypoint window */
enum BuildRailWaypointWidgets {
	BRWW_WAYPOINT_1,
	BRWW_WAYPOINT_2,
	BRWW_WAYPOINT_3,
	BRWW_WAYPOINT_4,
	BRWW_WAYPOINT_5,
	BRWW_SCROLL,
};

struct BuildRailWaypointWindow : PickerWindowBase {
	BuildRailWaypointWindow(const WindowDesc *desc, Window *parent) : PickerWindowBase(parent)
	{
		this->InitNested(desc, TRANSPORT_RAIL);
		this->hscroll.SetCapacity(5);
		this->hscroll.SetCount(_waypoint_count);
	};

	virtual void OnPaint()
	{
		for (uint i = 0; i < this->hscroll.GetCapacity(); i++) {
			this->SetWidgetLoweredState(i + BRWW_WAYPOINT_1, (this->hscroll.GetPosition() + i) == _cur_waypoint_type);
		}

		this->DrawWidgets();

		for (uint i = 0; i < this->hscroll.GetCapacity(); i++) {
			if (this->hscroll.GetPosition() + i < this->hscroll.GetCount()) {
				const StationSpec *statspec = GetCustomStationSpec(STAT_CLASS_WAYP, this->hscroll.GetPosition() + i);
				NWidgetBase *nw = this->GetWidget<NWidgetBase>(BRWW_WAYPOINT_1 + i);

				int bottom = nw->pos_y + nw->current_y;
				DrawWaypointSprite(nw->pos_x + TILE_PIXELS, bottom - TILE_PIXELS, this->hscroll.GetPosition() + i, _cur_railtype);

				if (statspec != NULL &&
						HasBit(statspec->callback_mask, CBM_STATION_AVAIL) &&
						GB(GetStationCallback(CBID_STATION_AVAILABILITY, 0, 0, statspec, NULL, INVALID_TILE), 0, 8) == 0) {
					GfxFillRect(nw->pos_x + 1, nw->pos_y + 1, nw->pos_x + nw->current_x - 2, bottom - 2, 0, FILLRECT_CHECKER);
				}
			}
		}
	}

	virtual void OnClick(Point pt, int widget, int click_count)
	{
		switch (widget) {
			case BRWW_WAYPOINT_1:
			case BRWW_WAYPOINT_2:
			case BRWW_WAYPOINT_3:
			case BRWW_WAYPOINT_4:
			case BRWW_WAYPOINT_5: {
				byte type = widget - BRWW_WAYPOINT_1 + this->hscroll.GetPosition();

				/* Check station availability callback */
				const StationSpec *statspec = GetCustomStationSpec(STAT_CLASS_WAYP, type);
				if (statspec != NULL &&
						HasBit(statspec->callback_mask, CBM_STATION_AVAIL) &&
						GB(GetStationCallback(CBID_STATION_AVAILABILITY, 0, 0, statspec, NULL, INVALID_TILE), 0, 8) == 0) return;

				_cur_waypoint_type = type;
				SndPlayFx(SND_15_BEEP);
				this->SetDirty();
				break;
			}
		}
	}
};

/** Nested widget definition for the build NewGRF rail waypoint window */
static const NWidgetPart _nested_build_waypoint_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN), SetDataTip(STR_WAYPOINT_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_DARK_GREEN),
		NWidget(NWID_SPACER), SetMinimalSize(0, 3),
		NWidget(NWID_HORIZONTAL), SetPIP(3, 2, 3),
			NWidget(WWT_PANEL, COLOUR_DARK_GREEN, BRWW_WAYPOINT_1), SetMinimalSize(66, 60), SetDataTip(0x0, STR_WAYPOINT_GRAPHICS_TOOLTIP), EndContainer(),
			NWidget(WWT_PANEL, COLOUR_DARK_GREEN, BRWW_WAYPOINT_2), SetMinimalSize(66, 60), SetDataTip(0x0, STR_WAYPOINT_GRAPHICS_TOOLTIP), EndContainer(),
			NWidget(WWT_PANEL, COLOUR_DARK_GREEN, BRWW_WAYPOINT_3), SetMinimalSize(66, 60), SetDataTip(0x0, STR_WAYPOINT_GRAPHICS_TOOLTIP), EndContainer(),
			NWidget(WWT_PANEL, COLOUR_DARK_GREEN, BRWW_WAYPOINT_4), SetMinimalSize(66, 60), SetDataTip(0x0, STR_WAYPOINT_GRAPHICS_TOOLTIP), EndContainer(),
			NWidget(WWT_PANEL, COLOUR_DARK_GREEN, BRWW_WAYPOINT_5), SetMinimalSize(66, 60), SetDataTip(0x0, STR_WAYPOINT_GRAPHICS_TOOLTIP), EndContainer(),
		EndContainer(),
		NWidget(NWID_SPACER), SetMinimalSize(0, 3),
		NWidget(WWT_HSCROLLBAR, COLOUR_DARK_GREEN, BRWW_SCROLL),
	EndContainer(),
};

static const WindowDesc _build_waypoint_desc(
	WDP_AUTO, 0, 0,
	WC_BUILD_DEPOT, WC_BUILD_TOOLBAR,
	WDF_CONSTRUCTION,
	_nested_build_waypoint_widgets, lengthof(_nested_build_waypoint_widgets)
);

static void ShowBuildWaypointPicker(Window *parent)
{
	new BuildRailWaypointWindow(&_build_waypoint_desc, parent);
}

/**
 * Initialize rail building GUI settings
 */
void InitializeRailGui()
{
	_build_depot_direction = DIAGDIR_NW;
}

/**
 * Re-initialize rail-build toolbar after toggling support for electric trains
 * @param disable Boolean whether electric trains are disabled (removed from the game)
 */
void ReinitGuiAfterToggleElrail(bool disable)
{
	extern RailType _last_built_railtype;
	if (disable && _last_built_railtype == RAILTYPE_ELECTRIC) {
		_last_built_railtype = _cur_railtype = RAILTYPE_RAIL;
		BuildRailToolbarWindow *w = dynamic_cast<BuildRailToolbarWindow *>(FindWindowById(WC_BUILD_TOOLBAR, TRANSPORT_RAIL));
		if (w != NULL) w->ModifyRailType(_cur_railtype);
	}
	MarkWholeScreenDirty();
}

/** Set the initial (default) railtype to use */
static void SetDefaultRailGui()
{
	if (_local_company == COMPANY_SPECTATOR || !Company::IsValidID(_local_company)) return;

	extern RailType _last_built_railtype;
	RailType rt = (RailType)(_settings_client.gui.default_rail_type + RAILTYPE_END);
	if (rt == DEF_RAILTYPE_MOST_USED) {
		/* Find the most used rail type */
		RailType count[RAILTYPE_END];
		memset(count, 0, sizeof(count));
		for (TileIndex t = 0; t < MapSize(); t++) {
			if (IsTileType(t, MP_RAILWAY) || IsLevelCrossingTile(t) || HasStationTileRail(t) ||
					(IsTileType(t, MP_TUNNELBRIDGE) && GetTunnelBridgeTransportType(t) == TRANSPORT_RAIL)) {
				count[GetRailType(t)]++;
			}
		}

		rt = RAILTYPE_RAIL;
		for (RailType r = RAILTYPE_ELECTRIC; r < RAILTYPE_END; r++) {
			if (count[r] >= count[rt]) rt = r;
		}

		/* No rail, just get the first available one */
		if (count[rt] == 0) rt = DEF_RAILTYPE_FIRST;
	}
	switch (rt) {
		case DEF_RAILTYPE_FIRST:
			rt = RAILTYPE_RAIL;
			while (rt < RAILTYPE_END && !HasRailtypeAvail(_local_company, rt)) rt++;
			break;

		case DEF_RAILTYPE_LAST:
			rt = GetBestRailtype(_local_company);
			break;

		default:
			break;
	}

	_last_built_railtype = _cur_railtype = rt;
	BuildRailToolbarWindow *w = dynamic_cast<BuildRailToolbarWindow *>(FindWindowById(WC_BUILD_TOOLBAR, TRANSPORT_RAIL));
	if (w != NULL) w->ModifyRailType(_cur_railtype);
}

/**
 * Updates the current signal variant used in the signal GUI
 * to the one adequate to current year.
 * @param p needed to be called when a setting changes
 * @return success, needed for settings
 */
bool ResetSignalVariant(int32 p = 0)
{
	SignalVariant new_variant = (_cur_year < _settings_client.gui.semaphore_build_before ? SIG_SEMAPHORE : SIG_ELECTRIC);

	if (new_variant != _cur_signal_variant) {
		Window *w = FindWindowById(WC_BUILD_SIGNAL, 0);
		if (w != NULL) {
			w->SetDirty();
			w->RaiseWidget((_cur_signal_variant == SIG_ELECTRIC ? BSW_ELECTRIC_NORM : BSW_SEMAPHORE_NORM) + _cur_signal_type);
		}
		_cur_signal_variant = new_variant;
	}

	return true;
}

/** Resets the rail GUI - sets default railtype to build
 * and resets the signal GUI
 */
void InitializeRailGUI()
{
	SetDefaultRailGui();

	_convert_signal_button = false;
	_cur_signal_type = _default_signal_type[_settings_client.gui.default_signal_type];
	ResetSignalVariant();
}
