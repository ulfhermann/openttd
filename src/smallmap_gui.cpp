/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file smallmap_gui.cpp GUI that shows a small map of the world with metadata like owner or height. */

#include "stdafx.h"
#include "clear_map.h"
#include "industry.h"
#include "station_map.h"
#include "landscape.h"
#include "window_gui.h"
#include "tree_map.h"
#include "viewport_func.h"
#include "gfx_func.h"
#include "town.h"
#include "blitter/factory.hpp"
#include "tunnelbridge_map.h"
#include "strings_func.h"
#include "core/endian_func.hpp"
#include "vehicle_base.h"
#include "sound_func.h"
#include "window_func.h"
#include "cargotype.h"
#include "openttd.h"
#include "company_func.h"
#include "station_base.h"
#include "zoom_func.h"

#include "table/strings.h"
#include "table/sprites.h"

#include <cmath>
#include <vector>

/** Widget numbers of the small map window. */
enum SmallMapWindowWidgets {
	SM_WIDGET_CLOSEBOX,
	SM_WIDGET_CAPTION,
	SM_WIDGET_STICKYBOX,
	SM_WIDGET_MAP_BORDER,
	SM_WIDGET_MAP,
	SM_WIDGET_LEGEND,
	SM_WIDGET_BUTTONSPANEL,
	SM_WIDGET_BLANK,
	SM_WIDGET_ZOOM_IN,
	SM_WIDGET_ZOOM_OUT,
	SM_WIDGET_CONTOUR,
	SM_WIDGET_VEHICLES,
	SM_WIDGET_INDUSTRIES,
	SM_WIDGET_LINKSTATS,
	SM_WIDGET_ROUTES,
	SM_WIDGET_VEGETATION,
	SM_WIDGET_OWNERS,
	SM_WIDGET_CENTERMAP,
	SM_WIDGET_TOGGLETOWNNAME,
	SM_WIDGET_BOTTOMPANEL,
	SM_WIDGET_SELECTINDUSTRIES,
	SM_WIDGET_ENABLE_ALL,
	SM_WIDGET_DISABLE_ALL,
	SM_WIDGET_RESIZEBOX,
};

static const NWidgetPart _nested_smallmap_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_BROWN, SM_WIDGET_CLOSEBOX),
		NWidget(WWT_CAPTION, COLOUR_BROWN, SM_WIDGET_CAPTION), SetDataTip(STR_SMALLMAP_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_STICKYBOX, COLOUR_BROWN, SM_WIDGET_STICKYBOX),
	EndContainer(),
	/* Small map display. */
	NWidget(WWT_PANEL, COLOUR_BROWN, SM_WIDGET_MAP_BORDER),
		NWidget(WWT_INSET, COLOUR_BROWN, SM_WIDGET_MAP), SetMinimalSize(346, 140), SetResize(1, 1), SetPadding(2, 2, 2, 2), EndContainer(),
	EndContainer(),
	/* Panel. */
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, COLOUR_BROWN, SM_WIDGET_LEGEND), SetMinimalSize(218, 44), SetResize(1, 0), EndContainer(),
		NWidget(NWID_VERTICAL),
			/* Top button row. */
			NWidget(NWID_HORIZONTAL),
				NWidget(WWT_PUSHIMGBTN, COLOUR_BROWN, SM_WIDGET_ZOOM_IN), SetMinimalSize(22, 22),
											SetDataTip(SPR_IMG_ZOOMIN, STR_TOOLBAR_TOOLTIP_ZOOM_THE_VIEW_IN),
				NWidget(WWT_PUSHIMGBTN, COLOUR_BROWN, SM_WIDGET_CENTERMAP), SetMinimalSize(22, 22),
											SetDataTip(SPR_IMG_SMALLMAP, STR_SMALLMAP_CENTER),
				NWidget(WWT_IMGBTN, COLOUR_BROWN, SM_WIDGET_BLANK), SetMinimalSize(22, 22),
											SetDataTip(SPR_DOT_SMALL, STR_NULL),
				NWidget(WWT_IMGBTN, COLOUR_BROWN, SM_WIDGET_CONTOUR), SetMinimalSize(22, 22),
											SetDataTip(SPR_IMG_SHOW_COUNTOURS, STR_SMALLMAP_TOOLTIP_SHOW_LAND_CONTOURS_ON_MAP),
				NWidget(WWT_IMGBTN, COLOUR_BROWN, SM_WIDGET_VEHICLES), SetMinimalSize(22, 22),
											SetDataTip(SPR_IMG_SHOW_VEHICLES, STR_SMALLMAP_TOOLTIP_SHOW_VEHICLES_ON_MAP),
				NWidget(WWT_IMGBTN, COLOUR_BROWN, SM_WIDGET_INDUSTRIES), SetMinimalSize(22, 22),
											SetDataTip(SPR_IMG_INDUSTRY, STR_SMALLMAP_TOOLTIP_SHOW_INDUSTRIES_ON_MAP),
			EndContainer(),
			/* Bottom button row. */
			NWidget(NWID_HORIZONTAL),
				NWidget(WWT_PUSHIMGBTN, COLOUR_BROWN, SM_WIDGET_ZOOM_OUT), SetMinimalSize(22, 22),
											SetDataTip(SPR_IMG_ZOOMOUT, STR_TOOLBAR_TOOLTIP_ZOOM_THE_VIEW_OUT),
				NWidget(WWT_IMGBTN, COLOUR_BROWN, SM_WIDGET_TOGGLETOWNNAME), SetMinimalSize(22, 22),
											SetDataTip(SPR_IMG_TOWN, STR_SMALLMAP_TOOLTIP_TOGGLE_TOWN_NAMES_ON_OFF),
				NWidget(WWT_IMGBTN, COLOUR_BROWN, SM_WIDGET_LINKSTATS), SetMinimalSize(22, 22),
											SetDataTip(SPR_IMG_GRAPHS, STR_SMALLMAP_TOOLTIP_SHOW_LINK_STATS_ON_MAP),
				NWidget(WWT_IMGBTN, COLOUR_BROWN, SM_WIDGET_ROUTES), SetMinimalSize(22, 22),
											SetDataTip(SPR_IMG_SHOW_ROUTES, STR_SMALLMAP_TOOLTIP_SHOW_TRANSPORT_ROUTES_ON),
				NWidget(WWT_IMGBTN, COLOUR_BROWN, SM_WIDGET_VEGETATION), SetMinimalSize(22, 22),
											SetDataTip(SPR_IMG_PLANTTREES, STR_SMALLMAP_TOOLTIP_SHOW_VEGETATION_ON_MAP),
				NWidget(WWT_IMGBTN, COLOUR_BROWN, SM_WIDGET_OWNERS), SetMinimalSize(22, 22),
											SetDataTip(SPR_IMG_COMPANY_GENERAL, STR_SMALLMAP_TOOLTIP_SHOW_LAND_OWNERS_ON_MAP),
			EndContainer(),
			NWidget(WWT_PANEL, COLOUR_BROWN, SM_WIDGET_BUTTONSPANEL), SetFill(true, true), EndContainer(),
		EndContainer(),
	EndContainer(),
	/* Bottom button row and resize box. */
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, COLOUR_BROWN, SM_WIDGET_BOTTOMPANEL),
			NWidget(NWID_HORIZONTAL),
				NWidget(NWID_SELECTION, INVALID_COLOUR, SM_WIDGET_SELECTINDUSTRIES),
					NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
						NWidget(WWT_TEXTBTN, COLOUR_BROWN, SM_WIDGET_ENABLE_ALL), SetMinimalSize(100, 12), SetDataTip(STR_SMALLMAP_ENABLE_ALL, STR_NULL),
						NWidget(WWT_TEXTBTN, COLOUR_BROWN, SM_WIDGET_DISABLE_ALL), SetMinimalSize(100, 12), SetDataTip(STR_SMALLMAP_DISABLE_ALL, STR_NULL),
					EndContainer(),
					NWidget(NWID_SPACER), SetFill(true, true),
				EndContainer(),
				NWidget(NWID_SPACER), SetFill(true, false), SetResize(1, 0),
			EndContainer(),
		EndContainer(),
		NWidget(WWT_RESIZEBOX, COLOUR_BROWN, SM_WIDGET_RESIZEBOX), SetFill(false, true),
	EndContainer(),
};


static int _smallmap_industry_count; ///< Number of used industries

/* number of cargos in the link stats legend */
static int _smallmap_cargo_count;

enum SmallMapStats {
	STAT_CAPACITY,
	STAT_BEGIN = STAT_CAPACITY,
	STAT_USAGE,
	STAT_PLANNED,
	STAT_SENT,
	STAT_TEXT,
	STAT_GRAPH,
	STAT_END,
	NUM_STATS = STAT_END,
};

/** Macro for ordinary entry of LegendAndColour */
#define MK(a, b) {a, b, INVALID_INDUSTRYTYPE, true, false, false}
/** Macro for end of list marker in arrays of LegendAndColour */
#define MKEND() {0, STR_NULL, INVALID_INDUSTRYTYPE, true, true, false}
/** Macro for break marker in arrays of LegendAndColour.
 * It will have valid data, though */
#define MS(a, b) {a, b, INVALID_INDUSTRYTYPE, true, false, true}

/** Structure for holding relevant data for legends in small map */
struct LegendAndColour {
	uint8 colour;      ///< colour of the item on the map
	StringID legend;   ///< string corresponding to the coloured item
	IndustryType type; ///< type of industry
	bool show_on_map;  ///< for filtering industries, if true is shown on map in colour
	bool end;          ///< this is the end of the list
	bool col_break;    ///< perform a break and go one column further
};

/** Legend text giving the colours to look for on the minimap */
static const LegendAndColour _legend_land_contours[] = {
	MK(0x5A, STR_SMALLMAP_LEGENDA_100M),
	MK(0x5C, STR_SMALLMAP_LEGENDA_200M),
	MK(0x5E, STR_SMALLMAP_LEGENDA_300M),
	MK(0x1F, STR_SMALLMAP_LEGENDA_400M),
	MK(0x27, STR_SMALLMAP_LEGENDA_500M),

	MS(0xD7, STR_SMALLMAP_LEGENDA_ROADS),
	MK(0x0A, STR_SMALLMAP_LEGENDA_RAILROADS),
	MK(0x98, STR_SMALLMAP_LEGENDA_STATIONS_AIRPORTS_DOCKS),
	MK(0xB5, STR_SMALLMAP_LEGENDA_BUILDINGS_INDUSTRIES),
	MK(0x0F, STR_SMALLMAP_LEGENDA_VEHICLES),
	MKEND()
};

static const LegendAndColour _legend_vehicles[] = {
	MK(0xB8, STR_SMALLMAP_LEGENDA_TRAINS),
	MK(0xBF, STR_SMALLMAP_LEGENDA_ROAD_VEHICLES),
	MK(0x98, STR_SMALLMAP_LEGENDA_SHIPS),
	MK(0x0F, STR_SMALLMAP_LEGENDA_AIRCRAFT),

	MS(0xD7, STR_SMALLMAP_LEGENDA_TRANSPORT_ROUTES),
	MK(0xB5, STR_SMALLMAP_LEGENDA_BUILDINGS_INDUSTRIES),
	MKEND()
};

static const LegendAndColour _legend_routes[] = {
	MK(0xD7, STR_SMALLMAP_LEGENDA_ROADS),
	MK(0x0A, STR_SMALLMAP_LEGENDA_RAILROADS),
	MK(0xB5, STR_SMALLMAP_LEGENDA_BUILDINGS_INDUSTRIES),

	MS(0x56, STR_SMALLMAP_LEGENDA_RAILROAD_STATION),
	MK(0xC2, STR_SMALLMAP_LEGENDA_TRUCK_LOADING_BAY),
	MK(0xBF, STR_SMALLMAP_LEGENDA_BUS_STATION),
	MK(0xB8, STR_SMALLMAP_LEGENDA_AIRPORT_HELIPORT),
	MK(0x98, STR_SMALLMAP_LEGENDA_DOCK),
	MKEND()
};

static const LegendAndColour _legend_vegetation[] = {
	MK(0x52, STR_SMALLMAP_LEGENDA_ROUGH_LAND),
	MK(0x54, STR_SMALLMAP_LEGENDA_GRASS_LAND),
	MK(0x37, STR_SMALLMAP_LEGENDA_BARE_LAND),
	MK(0x25, STR_SMALLMAP_LEGENDA_FIELDS),
	MK(0x57, STR_SMALLMAP_LEGENDA_TREES),
	MK(0xD0, STR_SMALLMAP_LEGENDA_FOREST),

	MS(0x0A, STR_SMALLMAP_LEGENDA_ROCKS),
	MK(0xC2, STR_SMALLMAP_LEGENDA_DESERT),
	MK(0x98, STR_SMALLMAP_LEGENDA_SNOW),
	MK(0xD7, STR_SMALLMAP_LEGENDA_TRANSPORT_ROUTES),
	MK(0xB5, STR_SMALLMAP_LEGENDA_BUILDINGS_INDUSTRIES),
	MKEND()
};

static const LegendAndColour _legend_land_owners[] = {
	MK(0xCA, STR_SMALLMAP_LEGENDA_WATER),
	MK(0x54, STR_SMALLMAP_LEGENDA_NO_OWNER),
	MK(0xB4, STR_SMALLMAP_LEGENDA_TOWNS),
	MK(0x20, STR_SMALLMAP_LEGENDA_INDUSTRIES),
	MKEND()
};
#undef MK
#undef MS
#undef MKEND

/** Allow room for all industries, plus a terminator entry
 * This is required in order to have the indutry slots all filled up */
static LegendAndColour _legend_from_industries[NUM_INDUSTRYTYPES + 1];
/* For connecting industry type to position in industries list(small map legend) */
static uint _industry_to_list_pos[NUM_INDUSTRYTYPES];

/**
 * Fills an array for the industries legends.
 */
void BuildIndustriesLegend()
{
	uint j = 0;

	/* Add each name */
	for (IndustryType i = 0; i < NUM_INDUSTRYTYPES; i++) {
		const IndustrySpec *indsp = GetIndustrySpec(i);
		if (indsp->enabled) {
			_legend_from_industries[j].legend = indsp->name;
			_legend_from_industries[j].colour = indsp->map_colour;
			_legend_from_industries[j].type = i;
			_legend_from_industries[j].show_on_map = true;
			_legend_from_industries[j].col_break = false;
			_legend_from_industries[j].end = false;

			/* Store widget number for this industry type */
			_industry_to_list_pos[i] = j;
			j++;
		}
	}
	/* Terminate the list */
	_legend_from_industries[j].end = true;

	/* Store number of enabled industries */
	_smallmap_industry_count = j;
}

static LegendAndColour _legend_linkstats[NUM_CARGO + NUM_STATS + 1];

/**
 * Populate legend table for the route map view.
 */
void BuildLinkStatsLegend()
{
	/* Clear the legend */
	memset(_legend_linkstats, 0, sizeof(_legend_linkstats));

	uint i = 0;

	for (CargoID c = CT_BEGIN; c != CT_END; ++c) {
		const CargoSpec *cs = CargoSpec::Get(c);
		if (!cs->IsValid()) continue;

		_legend_linkstats[i].legend = cs->name;
		_legend_linkstats[i].colour = cs->legend_colour;
		_legend_linkstats[i].type = c;
		_legend_linkstats[i].show_on_map = true;

		i++;
	}

	_legend_linkstats[i].col_break = true;

	_smallmap_cargo_count = i;

	/* the colours cannot be resolved before the gfx system is initialized.
	 * So we have to build the legend when creating the window.
	 */
	for (uint st = 0; st < NUM_STATS; ++st) {
		LegendAndColour & legend_entry = _legend_linkstats[i + st];
		switch(st) {
		case STAT_CAPACITY:
			legend_entry.colour = _colour_gradient[COLOUR_WHITE][7];
			legend_entry.legend = STR_SMALLMAP_LEGENDA_CAPACITY;
			legend_entry.show_on_map = true;
			break;
		case STAT_USAGE:
			legend_entry.colour = _colour_gradient[COLOUR_GREY][1];
			legend_entry.legend = STR_SMALLMAP_LEGENDA_USAGE;
			legend_entry.show_on_map = false;
			break;
		case STAT_PLANNED:
			legend_entry.colour = _colour_gradient[COLOUR_RED][5];
			legend_entry.legend = STR_SMALLMAP_LEGENDA_PLANNED;
			legend_entry.show_on_map = true;
			break;
		case STAT_SENT:
			legend_entry.colour = _colour_gradient[COLOUR_YELLOW][5];
			legend_entry.legend = STR_SMALLMAP_LEGENDA_SENT;
			legend_entry.show_on_map = false;
			break;
		case STAT_TEXT:
			legend_entry.colour = _colour_gradient[COLOUR_GREY][7];
			legend_entry.legend = STR_SMALLMAP_LEGENDA_SHOW_TEXT;
			legend_entry.show_on_map = false;
			break;
		case STAT_GRAPH:
			legend_entry.colour = _colour_gradient[COLOUR_GREY][7];
			legend_entry.legend = STR_SMALLMAP_LEGENDA_SHOW_GRAPH;
			legend_entry.show_on_map = true;
			break;
		}
	}

	_legend_linkstats[i + NUM_STATS].end = true;
}

static const LegendAndColour * const _legend_table[] = {
	_legend_land_contours,
	_legend_vehicles,
	_legend_from_industries,
	_legend_linkstats,
	_legend_routes,
	_legend_vegetation,
	_legend_land_owners,
};

#define MKCOLOUR(x) TO_LE32X(x)

/**
 * Height encodings; MAX_TILE_HEIGHT + 1 levels, from 0 to MAX_TILE_HEIGHT
 */
static const uint32 _map_height_bits[] = {
	MKCOLOUR(0x5A5A5A5A),
	MKCOLOUR(0x5A5B5A5B),
	MKCOLOUR(0x5B5B5B5B),
	MKCOLOUR(0x5B5C5B5C),
	MKCOLOUR(0x5C5C5C5C),
	MKCOLOUR(0x5C5D5C5D),
	MKCOLOUR(0x5D5D5D5D),
	MKCOLOUR(0x5D5E5D5E),
	MKCOLOUR(0x5E5E5E5E),
	MKCOLOUR(0x5E5F5E5F),
	MKCOLOUR(0x5F5F5F5F),
	MKCOLOUR(0x5F1F5F1F),
	MKCOLOUR(0x1F1F1F1F),
	MKCOLOUR(0x1F271F27),
	MKCOLOUR(0x27272727),
	MKCOLOUR(0x27272727),
};
assert_compile(lengthof(_map_height_bits) == MAX_TILE_HEIGHT + 1);

struct AndOr {
	uint32 mor;
	uint32 mand;
};

static inline uint32 ApplyMask(uint32 colour, const AndOr *mask)
{
	return (colour & mask->mand) | mask->mor;
}


static const AndOr _smallmap_contours_andor[] = {
	{MKCOLOUR(0x00000000), MKCOLOUR(0xFFFFFFFF)},
	{MKCOLOUR(0x000A0A00), MKCOLOUR(0xFF0000FF)},
	{MKCOLOUR(0x00D7D700), MKCOLOUR(0xFF0000FF)},
	{MKCOLOUR(0x00B5B500), MKCOLOUR(0xFF0000FF)},
	{MKCOLOUR(0x00000000), MKCOLOUR(0xFFFFFFFF)},
	{MKCOLOUR(0x98989898), MKCOLOUR(0x00000000)},
	{MKCOLOUR(0xCACACACA), MKCOLOUR(0x00000000)},
	{MKCOLOUR(0x00000000), MKCOLOUR(0xFFFFFFFF)},
	{MKCOLOUR(0xB5B5B5B5), MKCOLOUR(0x00000000)},
	{MKCOLOUR(0x00000000), MKCOLOUR(0xFFFFFFFF)},
	{MKCOLOUR(0x00B5B500), MKCOLOUR(0xFF0000FF)},
	{MKCOLOUR(0x000A0A00), MKCOLOUR(0xFF0000FF)},
};

static const AndOr _smallmap_vehicles_andor[] = {
	{MKCOLOUR(0x00000000), MKCOLOUR(0xFFFFFFFF)},
	{MKCOLOUR(0x00D7D700), MKCOLOUR(0xFF0000FF)},
	{MKCOLOUR(0x00D7D700), MKCOLOUR(0xFF0000FF)},
	{MKCOLOUR(0x00B5B500), MKCOLOUR(0xFF0000FF)},
	{MKCOLOUR(0x00000000), MKCOLOUR(0xFFFFFFFF)},
	{MKCOLOUR(0x00D7D700), MKCOLOUR(0xFF0000FF)},
	{MKCOLOUR(0xCACACACA), MKCOLOUR(0x00000000)},
	{MKCOLOUR(0x00000000), MKCOLOUR(0xFFFFFFFF)},
	{MKCOLOUR(0xB5B5B5B5), MKCOLOUR(0x00000000)},
	{MKCOLOUR(0x00000000), MKCOLOUR(0xFFFFFFFF)},
	{MKCOLOUR(0x00B5B500), MKCOLOUR(0xFF0000FF)},
	{MKCOLOUR(0x00D7D700), MKCOLOUR(0xFF0000FF)},
};

static const AndOr _smallmap_vegetation_andor[] = {
	{MKCOLOUR(0x00000000), MKCOLOUR(0xFFFFFFFF)},
	{MKCOLOUR(0x00D7D700), MKCOLOUR(0xFF0000FF)},
	{MKCOLOUR(0x00D7D700), MKCOLOUR(0xFF0000FF)},
	{MKCOLOUR(0x00B5B500), MKCOLOUR(0xFF0000FF)},
	{MKCOLOUR(0x00575700), MKCOLOUR(0xFF0000FF)},
	{MKCOLOUR(0x00D7D700), MKCOLOUR(0xFF0000FF)},
	{MKCOLOUR(0xCACACACA), MKCOLOUR(0x00000000)},
	{MKCOLOUR(0x00000000), MKCOLOUR(0xFFFFFFFF)},
	{MKCOLOUR(0xB5B5B5B5), MKCOLOUR(0x00000000)},
	{MKCOLOUR(0x00000000), MKCOLOUR(0xFFFFFFFF)},
	{MKCOLOUR(0x00B5B500), MKCOLOUR(0xFF0000FF)},
	{MKCOLOUR(0x00D7D700), MKCOLOUR(0xFF0000FF)},
};

typedef uint32 GetSmallMapPixels(TileIndex tile); ///< Typedef callthrough function


static inline TileType GetEffectiveTileType(TileIndex tile)
{
	TileType t = GetTileType(tile);

	if (t == MP_TUNNELBRIDGE) {
		TransportType tt = GetTunnelBridgeTransportType(tile);

		switch (tt) {
			case TRANSPORT_RAIL: t = MP_RAILWAY; break;
			case TRANSPORT_ROAD: t = MP_ROAD;    break;
			default:             t = MP_WATER;   break;
		}
	}
	return t;
}

/**
 * Return the colour a tile would be displayed with in the small map in mode "Contour".
 * @param tile The tile of which we would like to get the colour.
 * @return The colour of tile in the small map in mode "Contour"
 */
static inline uint32 GetSmallMapContoursPixels(TileIndex tile)
{
	TileType t = GetEffectiveTileType(tile);

	return ApplyMask(_map_height_bits[TileHeight(tile)], &_smallmap_contours_andor[t]);
}

/**
 * Return the colour a tile would be displayed with in the small map in mode "Vehicles".
 *
 * @param tile The tile of which we would like to get the colour.
 * @return The colour of tile in the small map in mode "Vehicles"
 */
static inline uint32 GetSmallMapVehiclesPixels(TileIndex tile)
{
	TileType t = GetEffectiveTileType(tile);

	return ApplyMask(MKCOLOUR(0x54545454), &_smallmap_vehicles_andor[t]);
}

/**
 * Return the colour a tile would be displayed with in the small map in mode "Industries".
 *
 * @param tile The tile of which we would like to get the colour.
 * @return The colour of tile in the small map in mode "Industries"
 */
static inline uint32 GetSmallMapIndustriesPixels(TileIndex tile)
{
	TileType t = GetEffectiveTileType(tile);

	if (t == MP_INDUSTRY) {
		/* If industry is allowed to be seen, use its colour on the map */
		if (_legend_from_industries[_industry_to_list_pos[Industry::GetByTile(tile)->type]].show_on_map) {
			return GetIndustrySpec(Industry::GetByTile(tile)->type)->map_colour * 0x01010101;
		} else {
			/* Otherwise, return the colour of the clear tiles, which will make it disappear */
			return ApplyMask(MKCOLOUR(0x54545454), &_smallmap_vehicles_andor[MP_CLEAR]);
		}
	}

	return ApplyMask(MKCOLOUR(0x54545454), &_smallmap_vehicles_andor[t]);
}

/**
 * Return the colour a tile would be displayed with in the small map in mode "Routes".
 *
 * @param tile The tile of which we would like to get the colour.
 * @return The colour of tile  in the small map in mode "Routes"
 */
static inline uint32 GetSmallMapRoutesPixels(TileIndex tile)
{
	TileType t = GetEffectiveTileType(tile);

	if (t == MP_STATION) {
		switch (GetStationType(tile)) {
			case STATION_RAIL:    return MKCOLOUR(0x56565656);
			case STATION_AIRPORT: return MKCOLOUR(0xB8B8B8B8);
			case STATION_TRUCK:   return MKCOLOUR(0xC2C2C2C2);
			case STATION_BUS:     return MKCOLOUR(0xBFBFBFBF);
			case STATION_DOCK:    return MKCOLOUR(0x98989898);
			default:              return MKCOLOUR(0xFFFFFFFF);
		}
	}

	/* Ground colour */
	return ApplyMask(MKCOLOUR(0x54545454), &_smallmap_contours_andor[t]);
}


static const uint32 _vegetation_clear_bits[] = {
	MKCOLOUR(0x54545454), ///< full grass
	MKCOLOUR(0x52525252), ///< rough land
	MKCOLOUR(0x0A0A0A0A), ///< rocks
	MKCOLOUR(0x25252525), ///< fields
	MKCOLOUR(0x98989898), ///< snow
	MKCOLOUR(0xC2C2C2C2), ///< desert
	MKCOLOUR(0x54545454), ///< unused
	MKCOLOUR(0x54545454), ///< unused
};

static inline uint32 GetSmallMapVegetationPixels(TileIndex tile)
{
	TileType t = GetEffectiveTileType(tile);

	switch (t) {
		case MP_CLEAR:
			return (IsClearGround(tile, CLEAR_GRASS) && GetClearDensity(tile) < 3) ? MKCOLOUR(0x37373737) : _vegetation_clear_bits[GetClearGround(tile)];

		case MP_INDUSTRY:
			return GetIndustrySpec(Industry::GetByTile(tile)->type)->check_proc == CHECK_FOREST ? MKCOLOUR(0xD0D0D0D0) : MKCOLOUR(0xB5B5B5B5);

		case MP_TREES:
			if (GetTreeGround(tile) == TREE_GROUND_SNOW_DESERT) {
				return (_settings_game.game_creation.landscape == LT_ARCTIC) ? MKCOLOUR(0x98575798) : MKCOLOUR(0xC25757C2);
			}
			return MKCOLOUR(0x54575754);

		default:
			return ApplyMask(MKCOLOUR(0x54545454), &_smallmap_vehicles_andor[t]);
	}
}


static uint32 _owner_colours[OWNER_END + 1];

/**
 * Return the colour a tile would be displayed with in the small map in mode "Owner".
 *
 * @param tile The tile of which we would like to get the colour.
 * @return The colour of tile in the small map in mode "Owner"
 */
static inline uint32 GetSmallMapOwnerPixels(TileIndex tile)
{
	Owner o;

	switch (GetTileType(tile)) {
		case MP_INDUSTRY: o = OWNER_END;          break;
		case MP_HOUSE:    o = OWNER_TOWN;         break;
		default:          o = GetTileOwner(tile); break;
		/* FIXME: For MP_ROAD there are multiple owners.
		 * GetTileOwner returns the rail owner (level crossing) resp. the owner of ROADTYPE_ROAD (normal road),
		 * even if there are no ROADTYPE_ROAD bits on the tile.
		 */
	}

	return _owner_colours[o];
}

/* Each tile has 4 x pixels and 1 y pixel */

static GetSmallMapPixels * const _smallmap_draw_procs[] = {
	GetSmallMapContoursPixels,
	GetSmallMapVehiclesPixels,
	GetSmallMapIndustriesPixels,
	GetSmallMapContoursPixels,
	GetSmallMapRoutesPixels,
	GetSmallMapVegetationPixels,
	GetSmallMapOwnerPixels,
};

static const byte _vehicle_type_colours[6] = {
	184, 191, 152, 15, 215, 184
};


void DrawVertex(int x, int y, int size, int colour, int boder_colour)
{
	size--;
	int w1 = size / 2;
	int w2 = size / 2 + size % 2;

	GfxFillRect(x - w1, y - w1, x + w2, y + w2, colour);

	w1++;
	w2++;
	GfxDrawLine(x - w1, y - w1, x + w2, y - w1, boder_colour);
	GfxDrawLine(x - w1, y + w2, x + w2, y + w2, boder_colour);
	GfxDrawLine(x - w1, y - w1, x - w1, y + w2, boder_colour);
	GfxDrawLine(x + w2, y - w1, x + w2, y + w2, boder_colour);
}

class SmallMapWindow : public Window {
	enum SmallMapType {
		SMT_CONTOUR,
		SMT_VEHICLES,
		SMT_INDUSTRY,
		SMT_LINKSTATS,
		SMT_ROUTES,
		SMT_VEGETATION,
		SMT_OWNER,
	};

	enum SmallmapWindowDistances {
		SD_MAP_COLUMN_WIDTH = 4,
		SD_MAP_ROW_OFFSET = 2,
		SD_MAP_MIN_INDUSTRY_WIDTH = 3,
	};

	/**
	 * save the Vehicle's old position here, so that we don't get glitches when redrawing
	 */
	struct VehicleAndPosition {
		VehicleAndPosition(const Vehicle *v) : tile(v->tile), vehicle(v->index) {}
		TileIndex tile;
		VehicleID vehicle;
	};

	typedef std::list<VehicleAndPosition> VehicleList;
	VehicleList vehicles_on_map;
	
	static SmallMapType map_type;
	static bool show_towns;

	static const uint LEGEND_BLOB_WIDTH = 8;
	uint column_width;
	uint number_of_rows;

	int32 scroll_x;
	int32 scroll_y;

	/**
	 * zoom level of the smallmap.
	 * May be something between ZOOM_LVL_MIN and ZOOM_LVL_MAX.
	 */
	ZoomLevel zoom;

	bool HasButtons()
	{
		return this->map_type == SMT_INDUSTRY || this->map_type == SMT_LINKSTATS;
	}

	Point cursor;

	struct BaseCargoDetail {
		BaseCargoDetail() :
			scale(_settings_game.economy.moving_average_length * _settings_game.economy.moving_average_unit)
		{
			this->Clear();
		}

		void AddLink(const LinkStat & orig_link, const FlowStat & orig_flow)
		{
			this->capacity += orig_link.capacity;
			this->usage += orig_link.usage;
			this->planned += orig_flow.planned;
			this->sent += orig_flow.sent;
		}

		void Scale()
		{
			this->capacity = this->capacity * 30 / this->scale;
			this->usage = this->usage * 30 / this->scale;
			this->planned = this->planned * 30 / this->scale;
			this->sent = this->sent * 30 / this->scale;
		}

		void Clear()
		{
			capacity = usage = planned = sent = 0;
		}

		uint capacity;
		uint usage;
		uint planned;
		uint sent;
		uint scale;
	};

	struct CargoDetail : public BaseCargoDetail {
		CargoDetail(const LegendAndColour * c, const LinkStat &ls, const FlowStat &fs) : legend(c)
		{
			this->AddLink(ls, fs);
			this->Scale();
		}

		const LegendAndColour *legend;
	};

	typedef std::vector<CargoDetail> StatVector;

	struct LinkDetails {
		LinkDetails() {Clear();}

		StationID sta;
		StationID stb;
		StatVector a_to_b;
		StatVector b_to_a;

		void Clear()
		{
			this->sta = INVALID_STATION;
			this->stb = INVALID_STATION;
			this->a_to_b.clear();
			this->b_to_a.clear();
		}

		bool Empty() const
		{
			return this->sta == INVALID_STATION;
		}
	};

	/**
	 * those are detected while drawing the links and used when drawing
	 * the legend. They don't represent game state.
	 */
	mutable LinkDetails link_details;
	mutable StationID supply_details;

	static const uint8 FORCE_REFRESH_PERIOD = 0x1F; ///< map is redrawn after that many ticks
	static const uint8 REFRESH_NEXT_TICK = 1;
	uint8 refresh; ///< refresh counter, zeroed every FORCE_REFRESH_PERIOD ticks

	/* The order of calculations when remapping is _very_ important as it introduces rounding errors.
	 * Everything has to be done just like when drawing the background otherwise the rounding errors are
	 * different on the background and on the overlay which creates "jumping" behaviour. This means:
	 * 1. UnScaleByZoom
	 * 2. divide by TILE_SIZE
	 * 3. subtract or add things or RemapCoords
	 * Note:
	 * We can't divide scroll_{x|y} by TILE_SIZE before scaling as that would mean we can only scroll full tiles.
	 */

	/**
	 * remap coordinates on the main map into coordinates on the smallmap
	 * @param pos_x X position on the main map
	 * @param pos_y Y position on the main map
	 * @return Point in the smallmap
	 */
	inline Point RemapPlainCoords(int pos_x, int pos_y) const
	{
		return RemapCoords(
				RemapX(pos_x),
				RemapY(pos_y),
				0
				);
	}

	/**
	 * remap a tile coordinate into coordinates on the smallmap
	 * @param tile the tile to be remapped
	 * @return Point with coordinates of the tile's upper left corner in the smallmap
	 */
	inline Point RemapTileCoords(TileIndex tile) const
	{
		return RemapPlainCoords(TileX(tile) * TILE_SIZE, TileY(tile) * TILE_SIZE);
	}

	/**
	 * scale a coordinate from the main map into the smallmap dimension
	 * @param pos coordinate to be scaled
	 * @return scaled coordinate
	 */
	inline int UnScalePlainCoord(int pos) const
	{
		return UnScaleByZoomLower(pos, this->zoom) / TILE_SIZE;
	}

	/**
	 * Remap a map X coordinate to a location on this smallmap.
	 * @param pos_x the tile's X coordinate.
	 * @return the X coordinate to draw on.
	 */
	inline int RemapX(int pos_x) const
	{
		return UnScalePlainCoord(pos_x) - UnScalePlainCoord(this->scroll_x);
	}

	/**
	 * Remap a map Y coordinate to a location on this smallmap.
	 * @param pos_y the tile's Y coordinate.
	 * @return the Y coordinate to draw on.
	 */
	inline int RemapY(int pos_y) const
	{
		return UnScalePlainCoord(pos_y) - UnScalePlainCoord(this->scroll_y);
	}

	/**
	 * choose a different tile from the tiles to be drawn in one pixel
	 * each time. This decreases the chance that certain structures
	 * (railway lines, roads) disappear completely when zooming out.
	 * @param x the X coordinate of the upper right corner of the drawn area
	 * @param y the Y coordinate of the upper right corner of the drawn area
	 * @param xc the unscaled X coordinate x was calcluated from
	 * @param yc the unscaled Y coordinate y was calcluated from
	 */
	void AntiAlias(uint &x, uint &y, uint xc, uint yc) const
	{
		int bits_needed = this->zoom - ZOOM_LVL_NORMAL;
		if (bits_needed <= 0) return;
		for(int i = 0; i < bits_needed; ++i) {
			x += ((xc ^ yc) & 0x1) << i;
			yc >>= 1;
			y += ((xc ^ yc) & 0x1) << i;
			xc >>= 1;
		}
		x = min(x, MapMaxX() - 1);
		y = min(y, MapMaxY() - 1);
	}

	/**
	 * Draws at most MAP_COLUMN_WIDTH columns (of one pixel each) of the small map in a certain
	 * mode onto the screen buffer. This function looks exactly the same for all types. Due to
	 * the constraints that no less than MAP_COLUMN_WIDTH pixels can be resolved at once via a
	 * GetSmallMapPixels function and that a single tile may be mapped onto more than one pixel
	 * in the smallmap dst, xc and yc may point to a place outside the area to be drawn.
	 *
	 * col_start, col_end, row_start and row_end give a more precise description of that area which
	 * is respected when drawing.
	 *
	 * @param dst Pointer to a part of the screen buffer to write to.
	 * @param xc First unscaled X coordinate of the first tile in the column.
	 * @param yc First unscaled Y coordinate of the first tile in the column
	 * @param col_start the first column in the buffer to be actually drawn
	 * @param col_end the last column to be actually drawn
	 * @param row_start the first row to be actually drawn
	 * @param row_end the last row to be actually drawn
	 * @see GetSmallMapPixels(TileIndex)
	 */
	void DrawSmallMapStuff(void *dst, uint xc, uint yc, int col_start, int col_end, int row_start, int row_end, Blitter *blitter, GetSmallMapPixels *proc) const
	{
		for (int row = 0; row < row_end; row += SD_MAP_ROW_OFFSET) {
			if (row >= row_start) {
				/* check if the tile (xc,yc) is within the map range */
				uint min_xy = _settings_game.construction.freeform_edges ? 1 : 0;
				uint x = ScaleByZoomLower(xc, this->zoom);
				uint y = ScaleByZoomLower(yc, this->zoom);
				uint32 val = 0;
				if (IsInsideMM(x, min_xy, MapMaxX()) && IsInsideMM(y, min_xy, MapMaxY())) {
					AntiAlias(x, y, xc, yc);
					val = proc(TileXY(x, y));
				}
				uint8 *val8 = (uint8 *)&val;
				for (int i = col_start; i < col_end; ++i ) {
					blitter->SetPixel(dst, i, 0, val8[i]);
				}
			}

			/* switch to next row in the column */
			xc++;
			yc++;
			dst = blitter->MoveTo(dst, 0, SD_MAP_ROW_OFFSET);
		}
	}

	/**
	 * Adds vehicles to the smallmap.
	 * @param dpi the part of the smallmap to be drawn into
	 * @param blitter current blitter
	 */
	void DrawVehicles(const DrawPixelInfo *dpi, Blitter *blitter) const
	{
		for(VehicleList::const_iterator i = this->vehicles_on_map.begin(); i != this->vehicles_on_map.end(); ++i) {
			const Vehicle *v = Vehicle::GetIfValid((*i).vehicle);
			if (v == NULL) continue;


			/* Remap into flat coordinates. */
			Point pos = RemapTileCoords((*i).tile);

			pos.x -= dpi->left;
			pos.y -= dpi->top;

			int scale = GetVehicleScale();
			/* Check if rhombus is inside bounds */
			if (!IsInsideMM(pos.x, -2 * scale, dpi->width + 2 * scale) ||
				!IsInsideMM(pos.y, -2 * scale, dpi->height + 2 * scale)) {
				continue;
			}

			byte colour = (this->map_type == SMT_VEHICLES) ? _vehicle_type_colours[v->type]	: 0xF;

			/* Draw rhombus */
			for (int dy = 0; dy < scale; dy++) {
				for (int dx = 0; dx < scale; dx++) {
					Point pt = RemapCoords(-dx, -dy, 0);
					if (IsInsideMM(pos.y + pt.y, 0, dpi->height)) {
						if (IsInsideMM(pos.x + pt.x, 0, dpi->width)) {
							blitter->SetPixel(dpi->dst_ptr, pos.x + pt.x, pos.y + pt.y, colour);
						}
						if (IsInsideMM(pos.x + pt.x + 1, 0, dpi->width)) {
							blitter->SetPixel(dpi->dst_ptr, pos.x + pt.x + 1, pos.y + pt.y, colour);
						}
					}
				}
			}
		}
	}


	FORCEINLINE int GetVehicleScale() const
	{
		int scale = 1;
		if (this->zoom < ZOOM_LVL_NORMAL) {
			scale = 1 << (ZOOM_LVL_NORMAL - this->zoom);
		}
		return scale;
	}

	inline Point GetStationMiddle(const Station * st) const {
		int x = (st->rect.right + st->rect.left - 1) * TILE_SIZE / 2;
		int y = (st->rect.bottom + st->rect.top - 1) * TILE_SIZE / 2;
		return RemapPlainCoords(x, y);
	}

	StationID DrawStationDots() const {
		const Station *supply_details = NULL;

		const Station *st;
		FOR_ALL_STATIONS(st) {
			if (st->owner != _local_company && Company::IsValidID(st->owner)) continue;

			Point pt = GetStationMiddle(st);

			if (supply_details == NULL && CheckStationSelected(&pt)) {
				supply_details = st;
			}

			/* Add up cargo supplied for each selected cargo type */
			uint q = 0;
			int colour = 0;
			int numCargos = 0;
			for (int i = 0; i < _smallmap_cargo_count; ++i) {
				const LegendAndColour &tbl = _legend_table[this->map_type][i];
				if (!tbl.show_on_map && supply_details != st) continue;
				CargoID c = tbl.type;
				int add = st->goods[c].supply;
				if (add > 0) {
					q += add * 30 / _settings_game.economy.moving_average_length / _settings_game.economy.moving_average_unit;
					colour += tbl.colour;
					numCargos++;
				}
			}
			if (numCargos > 1)
				colour /= numCargos;

			uint r = 2;
			if (q >= 10) r++;
			if (q >= 20) r++;
			if (q >= 40) r++;
			if (q >= 80) r++;
			if (q >= 160) r++;

			DrawVertex(pt.x, pt.y, r, colour, _colour_gradient[COLOUR_GREY][supply_details == st ? 3 : 1]);
		}
		return (supply_details == NULL) ? INVALID_STATION : supply_details->index;
	}

	class LinkDrawer {

	protected:
		virtual void DrawContent() = 0;
		virtual void Highlight() {}
		virtual void AddLink(const LinkStat & orig_link, const FlowStat & orig_flow, const LegendAndColour &cargo_entry) = 0;

		Point pta, ptb;
		bool search_link_details;
		LinkDetails link_details;
		const SmallMapWindow * window;

		void DrawLink(StationID sta, StationID stb) {

			this->pta = window->GetStationMiddle(Station::Get(sta));
			this->ptb = window->GetStationMiddle(Station::Get(stb));

			bool highlight_empty = this->search_link_details && this->link_details.Empty();
			bool highlight =
					(sta == this->link_details.sta && stb == this->link_details.stb) ||
					(highlight_empty && window->CheckLinkSelected(&pta, &ptb));
			bool reverse_empty = this->link_details.b_to_a.empty();
			bool reverse_highlight = (sta == this->link_details.stb && stb == this->link_details.sta);
			if (highlight_empty && highlight) {
				this->link_details.sta = sta;
				this->link_details.stb = stb;
			}

			if (highlight || reverse_highlight) {
				this->Highlight();
			}

			for (int i = 0; i < _smallmap_cargo_count; ++i) {
				const LegendAndColour &cargo_entry = _legend_table[window->map_type][i];
				CargoID cargo = cargo_entry.type;
				if (cargo_entry.show_on_map || highlight || reverse_highlight) {
					GoodsEntry &ge = Station::Get(sta)->goods[cargo];
					FlowStat sum_flows = ge.GetSumFlowVia(stb);
					const LinkStatMap &ls_map = ge.link_stats;
					LinkStatMap::const_iterator i = ls_map.find(stb);
					if (i != ls_map.end()) {
						const LinkStat &link_stat = i->second;
						AddLink(link_stat, sum_flows, cargo_entry);
						if (highlight_empty && highlight) {
							this->link_details.a_to_b.push_back(CargoDetail(&cargo_entry, link_stat, sum_flows));
						} else if (reverse_empty && reverse_highlight) {
							this->link_details.b_to_a.push_back(CargoDetail(&cargo_entry, link_stat, sum_flows));
						}
					}
				}
			}
		}

		virtual void DrawForwBackLinks(StationID sta, StationID stb) {
			DrawLink(sta, stb);
			DrawContent();
			DrawLink(stb, sta);
			DrawContent();
		}

	public:
		virtual ~LinkDrawer() {}

		LinkDetails DrawLinks(const SmallMapWindow * w, bool search)
		{
			this->link_details.Clear();
			this->window = w;
			this->search_link_details = search;
			std::set<StationID> seen_stations;
			std::set<std::pair<StationID, StationID> > seen_links;

			const Station * sta;
			FOR_ALL_STATIONS(sta) {
				if (sta->owner != _local_company && Company::IsValidID(sta->owner)) continue;
				for (int i = 0; i < _smallmap_cargo_count; ++i) {
					const LegendAndColour &tbl = _legend_table[window->map_type][i];
					if (!tbl.show_on_map) continue;

					CargoID c = tbl.type;
					const LinkStatMap & links = sta->goods[c].link_stats;
					for (LinkStatMap::const_iterator i = links.begin(); i != links.end(); ++i) {
						StationID from = sta->index;
						StationID to = i->first;
						if (Station::IsValidID(to) && seen_stations.find(to) == seen_stations.end()) {
							const Station *stb = Station::Get(to);

							if (stb->owner != _local_company && Company::IsValidID(stb->owner)) continue;
							if (seen_links.find(std::make_pair(to, from)) != seen_links.end()) continue;

							DrawForwBackLinks(sta->index, stb->index);
							seen_stations.insert(to);
						}
						seen_links.insert(std::make_pair(from, to));
					}
				}
				seen_stations.clear();
			}
			return this->link_details;
		}

	};

	class LinkLineDrawer : public LinkDrawer {
	public:
		LinkLineDrawer() : highlight(false) {}

	protected:
		typedef std::set<uint16> ColourSet;
		ColourSet colours;
		bool highlight;

		virtual void DrawForwBackLinks(StationID sta, StationID stb) {
			DrawLink(sta, stb);
			DrawLink(stb, sta);
			DrawContent();
		}

		virtual void AddLink(const LinkStat & orig_link, const FlowStat & orig_flow, const LegendAndColour &cargo_entry) {
			this->colours.insert(cargo_entry.colour);
		}

		virtual void Highlight() {
			this->highlight = true;
		}

		virtual void DrawContent() {
			uint colour = 0;
			uint num_colours = 0;
			for (ColourSet::iterator i = colours.begin(); i != colours.end(); ++i) {
				colour += *i;
				num_colours++;
			}
			colour /= num_colours;
			byte border_colour = _colour_gradient[COLOUR_GREY][highlight ? 3 : 1];
			GfxDrawLine(this->pta.x - 1, this->pta.y, this->ptb.x - 1, this->ptb.y, border_colour);
			GfxDrawLine(this->pta.x + 1, this->pta.y, this->ptb.x + 1, this->ptb.y, border_colour);
			GfxDrawLine(this->pta.x, this->pta.y - 1, this->ptb.x, this->ptb.y - 1, border_colour);
			GfxDrawLine(this->pta.x, this->pta.y + 1, this->ptb.x, this->ptb.y + 1, border_colour);
			GfxDrawLine(this->pta.x, this->pta.y, this->ptb.x, this->ptb.y, colour);
			this->colours.clear();
			this->highlight = false;
		}
	};

	class LinkValueDrawer : public LinkDrawer, public BaseCargoDetail {
	protected:

		virtual void AddLink(const LinkStat & orig_link, const FlowStat & orig_flow, const LegendAndColour &cargo_entry)
		{
			this->BaseCargoDetail::AddLink(orig_link, orig_flow);
		}
	};

	class LinkTextDrawer : public LinkValueDrawer {
	protected:
		virtual void DrawContent() {
			Scale();
			Point ptm;
			ptm.x = (this->pta.x + 2*this->ptb.x) / 3;
			ptm.y = (this->pta.y + 2*this->ptb.y) / 3;
			int nums = 0;
			if (_legend_linkstats[_smallmap_cargo_count + STAT_CAPACITY].show_on_map) {
				SetDParam(nums++, this->capacity);
			}
			if (_legend_linkstats[_smallmap_cargo_count + STAT_USAGE].show_on_map) {
				SetDParam(nums++, this->usage);
			}
			if (_legend_linkstats[_smallmap_cargo_count + STAT_PLANNED].show_on_map) {
				SetDParam(nums++, this->planned);
			}
			if (_legend_linkstats[_smallmap_cargo_count + STAT_SENT].show_on_map) {
				SetDParam(nums++, this->sent);
			}
			StringID str;
			switch (nums) {
			case 0:
				str = STR_EMPTY; break;
			case 1:
				str = STR_NUM; break;
			case 2:
				str = STR_NUM_RELATION_2; break;
			case 3:
				str = STR_NUM_RELATION_3; break;
			case 4:
				str = STR_NUM_RELATION_4; break;
			default:
				NOT_REACHED();
			}
			DrawString(ptm.x, ptm.x + this->window->ColumnWidth(), ptm.y, str , TC_BLACK);
			this->Clear();
		}
	};

	class LinkGraphDrawer : public LinkValueDrawer {
		typedef std::multimap<uint, byte, std::greater<uint> > SizeMap;
	protected:
		virtual void DrawContent() {
			Scale();
			Point ptm;
			SizeMap sizes;
			/* these floats only serve to calculate the size of the coloured boxes for capacity, usage, planned, sent
			 * they are not reused anywhere, so it's network safe.
			 */
			const LegendAndColour *legend_entry = _legend_linkstats + _smallmap_cargo_count + STAT_USAGE;
			if (legend_entry->show_on_map && this->usage > 0) {
				sizes.insert(std::make_pair((uint)sqrt((float)this->usage), legend_entry->colour));
			}
			legend_entry = _legend_linkstats + _smallmap_cargo_count + STAT_CAPACITY;
			if (legend_entry->show_on_map && this->capacity > 0) {
				sizes.insert(std::make_pair((uint)sqrt((float)this->capacity), legend_entry->colour));
			}
			legend_entry = _legend_linkstats + _smallmap_cargo_count + STAT_PLANNED;
			if (legend_entry->show_on_map && this->planned > 0) {
				sizes.insert(std::make_pair((uint)sqrt((float)this->planned),  legend_entry->colour));
			}
			legend_entry = _legend_linkstats + _smallmap_cargo_count + STAT_SENT;
			if (legend_entry->show_on_map && this->sent > 0) {
				sizes.insert(std::make_pair((uint)sqrt((float)this->sent), legend_entry->colour));
			}

			ptm.x = (this->pta.x + this->ptb.x) / 2;
			ptm.y = (this->pta.y + this->ptb.y) / 2;

			for (SizeMap::iterator i = sizes.begin(); i != sizes.end(); ++i) {
				if (this->pta.x > this->ptb.x) {
					ptm.x -= 1;
					GfxFillRect(ptm.x - i->first / 2, ptm.y - i->first * 2, ptm.x, ptm.y, i->second);
				} else {
					ptm.x += 1;
					GfxFillRect(ptm.x, ptm.y - i->first * 2, ptm.x + i->first / 2, ptm.y, i->second);
				}
			}
			this->Clear();
		}
	};

	void DrawIndustries(DrawPixelInfo *dpi) const {
		/* Emphasize all industries if current view is zoomed out "Industreis" */
		Blitter *blitter = BlitterFactoryBase::GetCurrentBlitter();
		if ((this->map_type == SMT_INDUSTRY) && (this->zoom > ZOOM_LVL_NORMAL)) {
			const Industry *i;
			FOR_ALL_INDUSTRIES(i) {
				if (_legend_from_industries[_industry_to_list_pos[i->type]].show_on_map) {
					Point pt = RemapTileCoords(i->xy);

					int y = pt.y - dpi->top;
					if (!IsInsideMM(y, 0, dpi->height)) continue;

					int x = pt.x - dpi->left;
					byte colour = GetIndustrySpec(i->type)->map_colour;

					for (int offset = 0; offset < SD_MAP_MIN_INDUSTRY_WIDTH; ++offset) {
						if (IsInsideMM(x + offset, 0, dpi->width)) {
							blitter->SetPixel(dpi->dst_ptr, x + offset, y, colour);
						}
					}
				}
			}
		}
	}

	static const uint MORE_SPACE_NEEDED = 0x1000;

	uint DrawLinkDetails(StatVector &details, uint x, uint y, uint right, uint bottom) const {
		uint x_orig = x;
		SetDParam(0, 9999);
		static uint entry_width = LEGEND_BLOB_WIDTH +
				GetStringBoundingBox(STR_ABBREV_PASSENGERS).width +
				GetStringBoundingBox(STR_SMALLMAP_LINK_CAPACITY).width +
				GetStringBoundingBox(STR_SMALLMAP_LINK_USAGE).width +
				GetStringBoundingBox(STR_SMALLMAP_LINK_PLANNED).width +
				GetStringBoundingBox(STR_SMALLMAP_LINK_SENT).width;
		uint entries_per_row = (right - x_orig) / entry_width;
		if (details.empty()) {
			DrawString(x, x + entry_width, y, STR_TINY_NOTHING, TC_BLACK);
			return y + FONT_HEIGHT_SMALL;
		}
		for (uint i = 0; i < details.size(); ++i) {
			CargoDetail &detail = details[i];
			if (x + entry_width >= right) {
				x = x_orig;
				y += FONT_HEIGHT_SMALL;
				if (y + 2 * FONT_HEIGHT_SMALL > bottom && details.size() - i > entries_per_row) {
					return y | MORE_SPACE_NEEDED;
				}
			}
			uint x_next = x + entry_width;
			if (detail.legend->show_on_map) {
				GfxFillRect(x, y + 1, x + LEGEND_BLOB_WIDTH, y + FONT_HEIGHT_SMALL - 1, 0); // outer border of the legend colour
			}
			GfxFillRect(x + 1, y + 2, x + LEGEND_BLOB_WIDTH - 1, y + FONT_HEIGHT_SMALL - 2, detail.legend->colour); // legend colour
			x += LEGEND_BLOB_WIDTH + WD_FRAMERECT_LEFT;
			TextColour textcol[4];
			for (int stat = STAT_CAPACITY; stat <= STAT_SENT; ++stat) {
				textcol[stat] = (detail.legend->show_on_map && _legend_linkstats[_smallmap_cargo_count + stat].show_on_map) ?
						TC_BLACK : TC_GREY;
			}

			SetDParam(0, STR_ABBREV_PASSENGERS + detail.legend->type);
			x = DrawString(x, x_next - 1, y, STR_SMALLMAP_LINK, detail.legend->show_on_map ? TC_BLACK : TC_GREY);
			SetDParam(0, detail.capacity);
			x = DrawString(x, x_next - 1, y, STR_SMALLMAP_LINK_CAPACITY, textcol[STAT_CAPACITY]);
			SetDParam(0, detail.usage);
			x = DrawString(x, x_next - 1, y, STR_SMALLMAP_LINK_USAGE, textcol[STAT_USAGE]);
			SetDParam(0, detail.planned);
			x = DrawString(x, x_next - 1, y, STR_SMALLMAP_LINK_PLANNED, textcol[STAT_PLANNED]);
			SetDParam(0, detail.sent);
			x = DrawString(x, x_next - 1, y, STR_SMALLMAP_LINK_SENT, textcol[STAT_SENT]);
			x = x_next;
		}
		return y + FONT_HEIGHT_SMALL;
	}

	uint DrawLinkDetailCaption(uint x, uint y, uint right, StationID sta, StationID stb) const {
		SetDParam(0, sta);
		SetDParam(1, stb);
		static uint height = GetStringBoundingBox(STR_SMALLMAP_LINK_CAPTION).height;
		DrawString(x, right - 1, y, STR_SMALLMAP_LINK_CAPTION, TC_BLACK);
		y += height;
		return y;
	}

	void DrawLinkDetails(uint x, uint y, uint right, uint bottom) const {
		y = DrawLinkDetailCaption(x, y, right, this->link_details.sta, this->link_details.stb);
		if (y + 2 * FONT_HEIGHT_SMALL > bottom) {
			DrawString(x, right, y, "...", TC_BLACK);
			return;
		}
		y = DrawLinkDetails(this->link_details.a_to_b, x, y, right, bottom);
		if (y + 3 * FONT_HEIGHT_SMALL > bottom) {
			/* caption takes more space -> 3 * row height */
			DrawString(x, right, y, "...", TC_BLACK);
			return;
		}
		y = DrawLinkDetailCaption(x, y + 2, right, this->link_details.stb, this->link_details.sta);
		if (y + 2 * FONT_HEIGHT_SMALL > bottom) {
			DrawString(x, right, y, "...", TC_BLACK);
			return;
		}
		y = DrawLinkDetails(this->link_details.b_to_a, x, y, right, bottom);
		if (y & MORE_SPACE_NEEDED) {
			/* only draw "..." if more entries would have been drawn */
			DrawString(x, right, y ^ MORE_SPACE_NEEDED, "...", TC_BLACK);
			return;
		}
	}

	void DrawSupplyDetails(uint x, uint y_org, uint bottom) const {
		const Station *st = Station::GetIfValid(this->supply_details);
		if (st == NULL) return;
		SetDParam(0, this->supply_details);
		static uint height = GetStringBoundingBox(STR_SMALLMAP_SUPPLY_CAPTION).height;
		DrawString(x, x + 2 * this->column_width - 1, y_org, STR_SMALLMAP_SUPPLY_CAPTION, TC_BLACK);
		y_org += height;
		uint y = y_org;
		for (int i = 0; i < _smallmap_cargo_count; ++i) {
			if (y + FONT_HEIGHT_SMALL - 1 >= bottom) {
				/* Column break needed, continue at top, SD_LEGEND_COLUMN_WIDTH pixels
				 * (one "row") to the right. */
				x += this->column_width;
				y = y_org;
			}

			const LegendAndColour &tbl = _legend_table[this->map_type][i];

			CargoID c = tbl.type;
			int supply = st->goods[c].supply * 30 / _settings_game.economy.moving_average_length / _settings_game.economy.moving_average_unit;;
			if (supply > 0) {
				TextColour textcol = TC_BLACK;
				if (tbl.show_on_map) {
					GfxFillRect(x, y + 1, x + LEGEND_BLOB_WIDTH, y + FONT_HEIGHT_SMALL - 1, 0); // outer border of the legend colour
				} else {
					textcol = TC_GREY;
				}
				SetDParam(0, c);
				SetDParam(1, supply);
				DrawString(x + LEGEND_BLOB_WIDTH + WD_FRAMERECT_LEFT, x + this->column_width - 1, y, STR_SMALLMAP_SUPPLY, textcol);
				GfxFillRect(x + 1, y + 2, x + LEGEND_BLOB_WIDTH - 1, y + FONT_HEIGHT_SMALL - 2, tbl.colour); // legend colour
				y += FONT_HEIGHT_SMALL;
			}
		}
	}

	/**
	 * Adds town names to the smallmap.
	 * @param dpi the part of the smallmap to be drawn into
	 */
	void DrawTowns(const DrawPixelInfo *dpi) const
	{
		const Town *t;
		FOR_ALL_TOWNS(t) {
			/* Remap the town coordinate */
			Point pt = RemapTileCoords(t->xy);
			int x = pt.x - (t->sign.width_small >> 1);
			int y = pt.y;

			/* Check if the town sign is within bounds */
			if (x + t->sign.width_small > dpi->left &&
					x < dpi->left + dpi->width &&
					y + FONT_HEIGHT_SMALL > dpi->top &&
					y < dpi->top + dpi->height) {
				/* And draw it. */
				SetDParam(0, t->index);
				DrawString(x, x + t->sign.width_small, y, STR_SMALLMAP_TOWN);
			}
		}
	}

	/**
	 * Draws vertical part of map indicator
	 * @param x X coord of left/right border of main viewport
	 * @param y Y coord of top border of main viewport
	 * @param y2 Y coord of bottom border of main viewport
	 */
	static inline void DrawVertMapIndicator(int x, int y, int y2)
	{
		GfxFillRect(x, y,      x, y + 3, 69);
		GfxFillRect(x, y2 - 3, x, y2,    69);
	}

	/**
	 * Draws horizontal part of map indicator
	 * @param x X coord of left border of main viewport
	 * @param x2 X coord of right border of main viewport
	 * @param y Y coord of top/bottom border of main viewport
	 */
	static inline void DrawHorizMapIndicator(int x, int x2, int y)
	{
		GfxFillRect(x,      y, x + 3, y, 69);
		GfxFillRect(x2 - 3, y, x2,    y, 69);
	}

	/**
	 * Adds map indicators to the smallmap.
	 */
	void DrawMapIndicators() const
	{
		/* Find main viewport. */
		const ViewPort *vp = FindWindowById(WC_MAIN_WINDOW, 0)->viewport;

		Point pt = RemapCoords(this->scroll_x, this->scroll_y, 0);

		/* UnScale everything separately to produce the same rounding errors as when drawing the background */
		int x = UnScalePlainCoord(vp->virtual_left) - UnScalePlainCoord(pt.x);
		int y = UnScalePlainCoord(vp->virtual_top) - UnScalePlainCoord(pt.y);
		int x2 = x + UnScalePlainCoord(vp->virtual_width);
		int y2 = y + UnScalePlainCoord(vp->virtual_height);

		SmallMapWindow::DrawVertMapIndicator(x, y, y2);
		SmallMapWindow::DrawVertMapIndicator(x2, y, y2);

		SmallMapWindow::DrawHorizMapIndicator(x, x2, y);
		SmallMapWindow::DrawHorizMapIndicator(x, x2, y2);
	}

	/**
	 * Draws the small map.
	 *
	 * Basically, the small map is draw column of pixels by column of pixels. The pixels
	 * are drawn directly into the screen buffer. The final map is drawn in multiple passes.
	 * The passes are:
	 * <ol><li>The colours of tiles in the different modes.</li>
	 * <li>Town names (optional)</li></ol>
	 *
	 * @param dpi pointer to pixel to write onto
	 */
	void DrawSmallMap(DrawPixelInfo *dpi) const
	{
		Blitter *blitter = BlitterFactoryBase::GetCurrentBlitter();
		DrawPixelInfo *old_dpi;

		old_dpi = _cur_dpi;
		_cur_dpi = dpi;

		/* Setup owner table */
		if (this->map_type == SMT_OWNER) {
			const Company *c;

			/* Fill with some special colours */
			_owner_colours[OWNER_TOWN]  = MKCOLOUR(0xB4B4B4B4);
			_owner_colours[OWNER_NONE]  = MKCOLOUR(0x54545454);
			_owner_colours[OWNER_WATER] = MKCOLOUR(0xCACACACA);
			_owner_colours[OWNER_END]   = MKCOLOUR(0x20202020); // Industry

			/* Now fill with the company colours */
			FOR_ALL_COMPANIES(c) {
				_owner_colours[c->index] = _colour_gradient[c->colour][5] * 0x01010101;
			}
		}

		int tile_x = UnScalePlainCoord(this->scroll_x);
		int tile_y = UnScalePlainCoord(this->scroll_y);

		int dx = dpi->left;
		tile_x -= dx / 4;
		tile_y += dx / 4;
		dx &= 3;

		int dy = dpi->top;
		tile_x += dy / 2;
		tile_y += dy / 2;

		/* prevent some artifacts when partially redrawing.
		 * I have no idea how this works.
		 */
		dx += 1;
		if (dy & 1) {
			tile_x++;
			dx += 2;
		}

		/**
		 * As we can resolve no less than 4 pixels of the smallmap at once we have to start drawing at an X position <= -4
		 * otherwise we get artifacts when partially redrawing.
		 * Make sure dx provides for that and update tile_x and tile_y accordingly.
		 */
		while(dx < SD_MAP_COLUMN_WIDTH) {
			dx += SD_MAP_COLUMN_WIDTH;
			tile_x++;
			tile_y--;
		}

		/* The map background is off by a little less than one tile in y direction compared to vehicles and signs.
		 * I have no idea why this is the case.
		 * on zoom levels >= ZOOM_LVL_NORMAL this isn't visible as only full tiles can be shown. However, beginning
		 * at ZOOM_LVL_OUT_2X it's again off by 1 pixel
		 */
		dy = 0;
		if (this->zoom < ZOOM_LVL_NORMAL) {
			dy = UnScaleByZoomLower(2, this->zoom) - 2;
		} else if (this->zoom > ZOOM_LVL_NORMAL) {
			dy = 1;
		}

		/* correct the various problems mentioned above by moving the initial drawing pointer a little */
		void *ptr = blitter->MoveTo(dpi->dst_ptr, -dx, -dy);
		int x = -dx;
		int y = 0;

		for (;;) {
			/* Distance from left edge */
			if (x > -SD_MAP_COLUMN_WIDTH) {

				/* Distance from right edge */
				if (dpi->width - x <= 0) break;

				int col_start = x < 0 ? -x : 0;
				int col_end = x + SD_MAP_COLUMN_WIDTH > dpi->width ? dpi->width - x : SD_MAP_COLUMN_WIDTH;
				int row_start = dy - y;
				int row_end = dy + dpi->height - y;
				this->DrawSmallMapStuff(ptr, tile_x, tile_y, col_start, col_end, row_start, row_end, blitter, _smallmap_draw_procs[this->map_type]);
			}

			if (y == 0) {
				tile_y++;
				y++;
				ptr = blitter->MoveTo(ptr, 0, SD_MAP_ROW_OFFSET / 2);
			} else {
				tile_x--;
				y--;
				ptr = blitter->MoveTo(ptr, 0, -SD_MAP_ROW_OFFSET / 2);
			}
			ptr = blitter->MoveTo(ptr, SD_MAP_COLUMN_WIDTH / 2, 0);
			x += SD_MAP_COLUMN_WIDTH / 2;
		}

		/* Draw vehicles */
		if (this->map_type == SMT_CONTOUR || this->map_type == SMT_VEHICLES) this->DrawVehicles(dpi, blitter);

		if (this->map_type == SMT_LINKSTATS && _game_mode == GM_NORMAL) {
			LinkLineDrawer lines;
			this->link_details = lines.DrawLinks(this, true);

			this->supply_details = DrawStationDots();

			if (_legend_linkstats[_smallmap_cargo_count + STAT_TEXT].show_on_map) {
				LinkTextDrawer text;
				text.DrawLinks(this, false);
			}
			if (_legend_linkstats[_smallmap_cargo_count + STAT_GRAPH].show_on_map) {
				LinkGraphDrawer graph;
				graph.DrawLinks(this, false);
			}
		}

		this->DrawIndustries(dpi);

		/* Draw town names */
		if (this->show_towns) this->DrawTowns(dpi);

		/* Draw map indicators */
		this->DrawMapIndicators();

		_cur_dpi = old_dpi;
	}

	bool CheckStationSelected(Point *pt) const {
		return abs(this->cursor.x - pt->x) < 7 && abs(this->cursor.y - pt->y) < 7;
	}

	bool CheckLinkSelected(Point * pta, Point * ptb) const {
		if (this->cursor.x == -1 && this->cursor.y == -1) return false;
		if (CheckStationSelected(pta) || CheckStationSelected(ptb)) return false;
		if (pta->x > ptb->x) Swap(pta, ptb);
		int minx = min(pta->x, ptb->x);
		int maxx = max(pta->x, ptb->x);
		int miny = min(pta->y, ptb->y);
		int maxy = max(pta->y, ptb->y);
		if (!IsInsideMM(cursor.x, minx - 3, maxx + 3) || !IsInsideMM(cursor.y, miny - 3, maxy + 3)) {
			return false;
		}

		if (pta->x == ptb->x || ptb->y == pta->y) {
			return true;
		} else {
			int incliney = (ptb->y - pta->y);
			int inclinex = (ptb->x - pta->x);
			int diff = (cursor.x - minx) * incliney / inclinex - (cursor.y - miny);
			if (incliney < 0) {
				diff += maxy - miny;
			}
			return abs(diff) < 4;
		}
	}

	/**
	 * Zoom in the map by one level.
	 * @param cx horizontal coordinate of center point, relative to SM_WIDGET_MAP widget
	 * @param cy vertical coordinate of center point, relative to SM_WIDGET_MAP widget
	 */
	void ZoomIn(int cx, int cy)
	{
		if (this->zoom > ZOOM_LVL_MIN) {
			this->zoom--;
			this->DoScroll(cx, cy);
			this->SetWidgetDisabledState(SM_WIDGET_ZOOM_IN, this->zoom == ZOOM_LVL_MIN);
			this->EnableWidget(SM_WIDGET_ZOOM_OUT);
			this->SetDirty();
		}
	}

	/**
	 * Zoom out the map by one level.
	 * @param cx horizontal coordinate of center point, relative to SM_WIDGET_MAP widget
	 * @param cy vertical coordinate of center point, relative to SM_WIDGET_MAP widget
	 */
	void ZoomOut(int cx, int cy)
	{
		if (this->zoom < ZOOM_LVL_MAX) {
			this->zoom++;
			this->DoScroll(cx / -2, cy / -2);
			this->EnableWidget(SM_WIDGET_ZOOM_IN);
			this->SetWidgetDisabledState(SM_WIDGET_ZOOM_OUT, this->zoom == ZOOM_LVL_MAX);
			this->SetDirty();
		}
	}

	void RecalcVehiclePositions() {
		this->vehicles_on_map.clear();
		const Vehicle *v;
		const NWidgetCore *wi = this->GetWidget<NWidgetCore>(SM_WIDGET_MAP);
		int scale = GetVehicleScale();

		FOR_ALL_VEHICLES(v) {
			if (v->type == VEH_EFFECT) continue;
			if (v->vehstatus & (VS_HIDDEN | VS_UNCLICKABLE)) continue;

			/* Remap into flat coordinates. */
			Point pos = RemapTileCoords(v->tile);

			pos.x -= wi->pos_x;
			pos.y -= wi->pos_y;
			/* Check if rhombus is inside bounds */
			if (IsInsideMM(pos.x, -2 * scale, wi->current_x + 2 * scale) &&
				IsInsideMM(pos.y, -2 * scale, wi->current_y + 2 * scale)) {

				this->vehicles_on_map.push_back(VehicleAndPosition(v));
			}
		}
	}

public:
	SmallMapWindow(const WindowDesc *desc, int window_number) : Window(), zoom(ZOOM_LVL_NORMAL), supply_details(INVALID_STATION), refresh(FORCE_REFRESH_PERIOD)
	{
		this->cursor.x = -1;
		this->cursor.y = -1;
		this->InitNested(desc, window_number);
		if (_smallmap_cargo_count == 0) {
			this->DisableWidget(SM_WIDGET_LINKSTATS);
			if (this->map_type == SMT_LINKSTATS) {
				this->map_type = SMT_CONTOUR;
			}
		}

		this->LowerWidget(this->map_type + SM_WIDGET_CONTOUR);

		this->SetWidgetLoweredState(SM_WIDGET_TOGGLETOWNNAME, this->show_towns);
		this->GetWidget<NWidgetStacked>(SM_WIDGET_SELECTINDUSTRIES)->SetDisplayedPlane(this->map_type != SMT_INDUSTRY && this->map_type != SMT_LINKSTATS);

		this->SmallMapCenterOnCurrentPos();
	}

	virtual void SetStringParameters(int widget) const
	{
		switch (widget) {
			case SM_WIDGET_CAPTION:
				SetDParam(0, STR_SMALLMAP_TYPE_CONTOURS + this->map_type);
				break;
		}
	}

	virtual void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *resize)
	{
		if (widget != SM_WIDGET_LEGEND) return;

		uint min_height = 0;
		uint min_width = 0;
		for (uint i = 0; i < lengthof(_legend_table); i++) {
			/* Only check the width, which are a bit more special! */
			if (i == SMT_INDUSTRY || i == SMT_LINKSTATS) {
				StringID string = (i == SMT_INDUSTRY) ? STR_SMALLMAP_INDUSTRY : STR_SMALLMAP_LINKSTATS_LEGEND;
				for (const LegendAndColour *tbl = _legend_table[i]; !tbl->end; ++tbl) {
					SetDParam(0, tbl->legend);
					SetDParam(1, IndustryPool::MAX_SIZE);
					min_width = max(GetStringBoundingBox(string).width, min_width);
				}
			} else {
				uint height = 0;
				for (const LegendAndColour *tbl = _legend_table[i]; !tbl->end; ++tbl) {
					min_width = max(GetStringBoundingBox(tbl->legend).width, min_width);
					if (tbl->col_break) {
						min_height = max(min_height, height);
						height = 0;
					}
					height++;
				}
				min_height = max(min_height, height);
			}
		}

		/* The width of a column is the minimum width of all texts + the size of the blob + some spacing */
		this->column_width = min_width + LEGEND_BLOB_WIDTH + WD_FRAMERECT_LEFT + WD_FRAMERECT_RIGHT;
		/* The number of columns is always two, but if the it's wide enough there may be more columns */
		uint columns = max(2U, (size->width - WD_FRAMERECT_LEFT) / this->column_width);
		/* The number of rows is always the minimum, otherwise it depends on the number of industries */
		this->number_of_rows = max(min_height, (_smallmap_industry_count + columns - 1) / columns);

		size->width  = max(columns * column_width + WD_FRAMERECT_LEFT, size->width);
		size->height = max(this->number_of_rows * FONT_HEIGHT_SMALL + WD_FRAMERECT_TOP + 1 + WD_FRAMERECT_BOTTOM, size->height);
	}

	virtual void DrawWidget(const Rect &r, int widget) const
	{
		switch (widget) {
			case SM_WIDGET_MAP: {
				DrawPixelInfo new_dpi;
				if (!FillDrawPixelInfo(&new_dpi, r.left + 1, r.top + 1, r.right - r.left - 1, r.bottom - r.top - 1)) return;
				this->DrawSmallMap(&new_dpi);
			} break;

			case SM_WIDGET_LEGEND: {
				DrawLegend(r);
			} break;
		}
	}

	void DrawLegend(const Rect &r) const {
		uint y_org = r.top + WD_FRAMERECT_TOP;
		uint x = r.left + WD_FRAMERECT_LEFT;
		if (this->supply_details != INVALID_STATION) {
			this->DrawSupplyDetails(x, y_org, r.bottom - WD_FRAMERECT_BOTTOM);
		} else if (!link_details.Empty()) {
			this->DrawLinkDetails(x, y_org, r.right - WD_FRAMERECT_RIGHT, r.bottom - WD_FRAMERECT_BOTTOM);
		} else {
			bool rtl = _dynlang.text_dir == TD_RTL;
			uint y_org = r.top + WD_FRAMERECT_TOP;
			uint x = rtl ? r.right - this->column_width - WD_FRAMERECT_RIGHT : r.left + WD_FRAMERECT_LEFT;
			uint y = y_org;
			uint i = 0;
			uint row_height = FONT_HEIGHT_SMALL;

			uint text_left  = rtl ? 0 : LEGEND_BLOB_WIDTH + WD_FRAMERECT_LEFT;
			uint text_right = this->column_width - 1 - (rtl ? LEGEND_BLOB_WIDTH + WD_FRAMERECT_RIGHT : 0);
			uint blob_left  = rtl ? this->column_width - 1 - LEGEND_BLOB_WIDTH : 0;
			uint blob_right = rtl ? this->column_width - 1 : LEGEND_BLOB_WIDTH;

			StringID string = (this->map_type == SMT_INDUSTRY) ? STR_SMALLMAP_INDUSTRY : STR_SMALLMAP_LINKSTATS_LEGEND;

			for (const LegendAndColour *tbl = _legend_table[this->map_type]; !tbl->end; ++tbl) {
				if (tbl->col_break || i++ >= this->number_of_rows) {
					/* Column break needed, continue at top, COLUMN_WIDTH pixels
					 * (one "row") to the right. */
					x += rtl ? -this->column_width : this->column_width;
					y = y_org;
					i = 0;
				}

				switch(this->map_type) {
					case SMT_INDUSTRY:
						/* Industry name must be formated, since it's not in tiny font in the specs.
						 * So, draw with a parameter and use the STR_SMALLMAP_INDUSTRY string, which is tiny font */
						assert(tbl->type < NUM_INDUSTRYTYPES);
						SetDParam(1, _industry_counts[tbl->type]);
					case SMT_LINKSTATS:
						SetDParam(0, tbl->legend);
						if (!tbl->show_on_map) {
							/* Simply draw the string, not the black border of the legend colour.
							 * This will enforce the idea of the disabled item */
							DrawString(x + text_left, x + text_right, y, string, TC_GREY);
						} else {
							DrawString(x + text_left, x + text_right, y, string, TC_BLACK);
							GfxFillRect(x + blob_left, y + 1, x + blob_right, y + row_height - 1, 0); // outer border of the legend colour
						}
						break;
					default:
						/* Anything that is not an industry is using normal process */
						GfxFillRect(x + blob_left, y + 1, x + blob_right, y + row_height - 1, 0);
						DrawString(x + text_left, x + text_right, y, tbl->legend);
				}
				GfxFillRect(x + blob_left + 1, y + 2, x + blob_right - 1, y + row_height - 2, tbl->colour); // Legend colour

				y += row_height;
			}
		}
	}



	virtual void OnPaint()
	{
		this->DrawWidgets();
	}

	virtual void OnClick(Point pt, int widget)
	{
		switch (widget) {
			case SM_WIDGET_MAP: { // Map window
				/*
				 * XXX: scrolling with the left mouse button is done by subsequently
				 * clicking with the left mouse button; clicking once centers the
				 * large map at the selected point. So by unclicking the left mouse
				 * button here, it gets reclicked during the next inputloop, which
				 * would make it look like the mouse is being dragged, while it is
				 * actually being (virtually) clicked every inputloop.
				 */
				_left_button_clicked = false;

				Point pt = RemapCoords(this->scroll_x, this->scroll_y, 0);
				Window *w = FindWindowById(WC_MAIN_WINDOW, 0);
				w->viewport->follow_vehicle = INVALID_VEHICLE;
				int scaled_x_off = ScaleByZoom((_cursor.pos.x - this->left - WD_FRAMERECT_LEFT) * TILE_SIZE, this->zoom);
				int scaled_y_off = ScaleByZoom((_cursor.pos.y - this->top - WD_FRAMERECT_TOP - WD_CAPTION_HEIGHT) * TILE_SIZE, this->zoom);
				w->viewport->dest_scrollpos_x = pt.x + scaled_x_off - w->viewport->virtual_width / 2;
				w->viewport->dest_scrollpos_y = pt.y + scaled_y_off - w->viewport->virtual_height / 2;

				this->SetDirty();
			} break;

			case SM_WIDGET_ZOOM_OUT: {
				const NWidgetCore *wi = this->GetWidget<NWidgetCore>(SM_WIDGET_MAP);
				this->ZoomOut(wi->current_x / 2, wi->current_y / 2);
				this->HandleButtonClick(SM_WIDGET_ZOOM_OUT);
				SndPlayFx(SND_15_BEEP);
			} break;

			case SM_WIDGET_ZOOM_IN: {
				const NWidgetCore *wi = this->GetWidget<NWidgetCore>(SM_WIDGET_MAP);
				this->ZoomIn(wi->current_x / 2, wi->current_y / 2);
				this->HandleButtonClick(SM_WIDGET_ZOOM_IN);
				SndPlayFx(SND_15_BEEP);
			} break;

			case SM_WIDGET_CONTOUR:    // Show land contours
			case SM_WIDGET_VEHICLES:   // Show vehicles
			case SM_WIDGET_INDUSTRIES: // Show industries
			case SM_WIDGET_LINKSTATS:   // Show route map
			case SM_WIDGET_ROUTES:     // Show transport routes
			case SM_WIDGET_VEGETATION: // Show vegetation
			case SM_WIDGET_OWNERS:     // Show land owners
				this->RaiseWidget(this->map_type + SM_WIDGET_CONTOUR);
				this->map_type = (SmallMapType)(widget - SM_WIDGET_CONTOUR);
				this->LowerWidget(this->map_type + SM_WIDGET_CONTOUR);

				/* Hide Enable all/Disable all buttons if is not industry type small map */
				this->GetWidget<NWidgetStacked>(SM_WIDGET_SELECTINDUSTRIES)->SetDisplayedPlane(this->map_type != SMT_INDUSTRY && this->map_type != SMT_LINKSTATS);

				this->SetDirty();
				SndPlayFx(SND_15_BEEP);
				break;

			case SM_WIDGET_CENTERMAP: // Center the smallmap again
				this->SmallMapCenterOnCurrentPos();
				this->HandleButtonClick(SM_WIDGET_CENTERMAP);
				SndPlayFx(SND_15_BEEP);
				break;

			case SM_WIDGET_TOGGLETOWNNAME: // Toggle town names
				this->show_towns = !this->show_towns;
				this->SetWidgetLoweredState(SM_WIDGET_TOGGLETOWNNAME, this->show_towns);

				this->SetDirty();
				SndPlayFx(SND_15_BEEP);
				break;

			case SM_WIDGET_LEGEND: // Legend
				/* If industry type small map*/
				if (this->map_type == SMT_INDUSTRY || this->map_type == SMT_LINKSTATS) {
					/* If click on industries label, find right industry type and enable/disable it */
					const NWidgetCore *wi = this->GetWidget<NWidgetCore>(SM_WIDGET_LEGEND); // Label panel
					uint line = (pt.y - wi->pos_y - WD_FRAMERECT_TOP) / FONT_HEIGHT_SMALL;
					if (line >= this->number_of_rows) break;

					bool rtl = _dynlang.text_dir == TD_RTL;
					int x = pt.x - wi->pos_x;
					if (rtl) x = wi->current_x - x;
					uint column = (x - WD_FRAMERECT_LEFT) / this->column_width;

					/* Check if click is on industry label*/
					int click_pos = (column * this->number_of_rows) + line;
					if (this->map_type == SMT_INDUSTRY) {
						if (click_pos < _smallmap_industry_count) {
							_legend_from_industries[click_pos].show_on_map = !_legend_from_industries[click_pos].show_on_map;
						}
					} else if (this->map_type == SMT_LINKSTATS) {
						if (click_pos < _smallmap_cargo_count) {
							_legend_linkstats[click_pos].show_on_map = !_legend_linkstats[click_pos].show_on_map;
						} else {
							uint stats_column = _smallmap_cargo_count / this->number_of_rows;
							if (_smallmap_cargo_count % this->number_of_rows != 0) stats_column++;

							if (column == stats_column && line < NUM_STATS) {
								click_pos = _smallmap_cargo_count + line;
								_legend_linkstats[click_pos].show_on_map = !_legend_linkstats[click_pos].show_on_map;
							}
						}
					}

					/* Raise the two buttons "all", as we have done a specific choice */
					this->RaiseWidget(SM_WIDGET_ENABLE_ALL);
					this->RaiseWidget(SM_WIDGET_DISABLE_ALL);
					this->SetDirty();
				}
				break;

			case SM_WIDGET_ENABLE_ALL: { // Enable all items
				LegendAndColour *tbl = (this->map_type == SMT_INDUSTRY) ? _legend_from_industries : _legend_linkstats;
				for (; !tbl->end; ++tbl) {
					tbl->show_on_map = true;
				}
				/* Toggle appeareance indicating the choice */
				this->LowerWidget(SM_WIDGET_ENABLE_ALL);
				this->RaiseWidget(SM_WIDGET_DISABLE_ALL);
				this->SetDirty();
				break;
			}

			case SM_WIDGET_DISABLE_ALL: { // Disable all items
				LegendAndColour *tbl = (this->map_type == SMT_INDUSTRY) ? _legend_from_industries : _legend_linkstats;
				for (; !tbl->end; ++tbl) {
					tbl->show_on_map = false;
				}
				/* Toggle appeareance indicating the choice */
				this->RaiseWidget(SM_WIDGET_ENABLE_ALL);
				this->LowerWidget(SM_WIDGET_DISABLE_ALL);
				this->SetDirty();
				break;
			}
		}
	}

	virtual void OnMouseWheel(int wheel)
	{
		/* Cursor position relative to window */
		int cx = _cursor.pos.x - this->left;
		int cy = _cursor.pos.y - this->top;

		const NWidgetCore *wi = this->GetWidget<NWidgetCore>(SM_WIDGET_MAP);
		/* Is cursor over the map ? */
		if (IsInsideMM(cx, wi->pos_x, wi->pos_x + wi->current_x + 1) &&
				IsInsideMM(cy, wi->pos_y, wi->pos_y + wi->current_y + 1)) {
			/* Cursor position relative to map */
			cx -= wi->pos_x;
			cy -= wi->pos_y;

			if (wheel < 0) {
				this->ZoomIn(cx, cy);
			} else {
				this->ZoomOut(cx, cy);
			}
		}
	}

	virtual void OnMouseOver(Point pt, int widget) {
		static Point invalid = {-1, -1};
		if (pt.x != cursor.x || pt.y != cursor.y) {
			this->refresh = 1;
			if (widget == SM_WIDGET_MAP) {
				cursor = pt;
				cursor.x -= WD_FRAMERECT_LEFT;
				cursor.y -= WD_FRAMERECT_TOP + WD_CAPTION_HEIGHT;
			} else {
				cursor = invalid;
			}
		}
	}


	virtual void OnRightClick(Point pt, int widget)
	{
		if (widget == SM_WIDGET_MAP) {
			if (_scrolling_viewport) return;
			_scrolling_viewport = true;
		}
	}

	virtual void OnTick()
	{
		/* Update the window every now and then */
		if (--this->refresh != 0) return;

		this->RecalcVehiclePositions();

		this->refresh = FORCE_REFRESH_PERIOD;
		this->SetDirty();
	}

	virtual void OnScroll(Point delta)
	{
		_cursor.fix_at = true;
		DoScroll(delta.x, delta.y);
		this->SetDirty();
	}

	/**
	 * Do the actual scrolling, but don't fix the cursor or set the window dirty.
	 * @param dx x offset to scroll in screen dimension
	 * @param dy y offset to scroll in screen dimension
	 */
	void DoScroll(int dx, int dy)
	{
		/* divide as late as possible to avoid premature reduction to 0, which causes "jumpy" behaviour
		 * at the same time make sure this is the exact reverse function of the drawing methods in order to
		 * avoid map indicators shifting around:
		 * 1. add/subtract
		 * 2. * TILE_SIZE
		 * 3. scale
		 */
		int x = dy * 2 - dx;
		int y = dx + dy * 2;

		/* round to next divisible by 4 to allow for smoother scrolling */
		int rem_x = abs(x % 4);
		int rem_y = abs(y % 4);
		if (rem_x != 0) {
			x += x > 0 ? 4 - rem_x : rem_x - 4;
		}
		if (rem_y != 0) {
			y += y > 0 ? 4 - rem_y : rem_y - 4;
		}

		this->scroll_x += ScaleByZoomLower(x / 4 * TILE_SIZE, this->zoom);
		this->scroll_y += ScaleByZoomLower(y / 4 * TILE_SIZE, this->zoom);

		/* enforce the screen limits */
		const NWidgetCore *wi = this->GetWidget<NWidgetCore>(SM_WIDGET_MAP);
		int hx = wi->current_x;
		int hy = wi->current_y;
		int hvx = ScaleByZoomLower(hy * 4 - hx * 2, this->zoom);
		int hvy = ScaleByZoomLower(hx * 2 + hy * 4, this->zoom);
		this->scroll_x = Clamp(this->scroll_x, -hvx, MapMaxX() * TILE_SIZE);
		this->scroll_y = Clamp(this->scroll_y, -hvy, MapMaxY() * TILE_SIZE - hvy);
		this->refresh = REFRESH_NEXT_TICK;
	}

	void SmallMapCenterOnCurrentPos()
	{
		const ViewPort *vp = FindWindowById(WC_MAIN_WINDOW, 0)->viewport;
		const NWidgetCore *wi = this->GetWidget<NWidgetCore>(SM_WIDGET_MAP);

		int zoomed_width = ScaleByZoom(wi->current_x * TILE_SIZE, this->zoom);
		int zoomed_height = ScaleByZoom(wi->current_y * TILE_SIZE, this->zoom);
		int x  = ((vp->virtual_width - zoomed_width) / 2 + vp->virtual_left);
		int y  = ((vp->virtual_height - zoomed_height) / 2 + vp->virtual_top);
		this->scroll_x = (y * 2 - x) / 4;
		this->scroll_y = (x + y * 2) / 4;
		this->SetDirty();
	}

	uint ColumnWidth() const {return column_width;}
};

SmallMapWindow::SmallMapType SmallMapWindow::map_type = SMT_CONTOUR;
bool SmallMapWindow::show_towns = true;

static const WindowDesc _smallmap_desc(
	WDP_AUTO, WDP_AUTO, 460, 314,
	WC_SMALLMAP, WC_NONE,
	WDF_STD_TOOLTIPS | WDF_STD_BTN | WDF_DEF_WIDGET | WDF_STICKY_BUTTON | WDF_RESIZABLE | WDF_UNCLICK_BUTTONS,
	_nested_smallmap_widgets, lengthof(_nested_smallmap_widgets)
);

void ShowSmallMap()
{
	AllocateWindowDescFront<SmallMapWindow>(&_smallmap_desc, 0);
}

/**
 * Scrolls the main window to given coordinates.
 * @param x x coordinate
 * @param y y coordinate
 * @param z z coordinate; -1 to scroll to terrain height
 * @param instant scroll instantly (meaningful only when smooth_scrolling is active)
 * @return did the viewport position change?
 */
bool ScrollMainWindowTo(int x, int y, int z, bool instant)
{
	bool res = ScrollWindowTo(x, y, z, FindWindowById(WC_MAIN_WINDOW, 0), instant);

	/* If a user scrolls to a tile (via what way what so ever) and already is on
	 * that tile (e.g.: pressed twice), move the smallmap to that location,
	 * so you directly see where you are on the smallmap. */

	if (res) return res;

	SmallMapWindow *w = dynamic_cast<SmallMapWindow*>(FindWindowById(WC_SMALLMAP, 0));
	if (w != NULL) w->SmallMapCenterOnCurrentPos();

	return res;
}
