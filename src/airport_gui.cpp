/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file airport_gui.cpp The GUI for airports. */

#include "stdafx.h"
#include "window_gui.h"
#include "station_gui.h"
#include "terraform_gui.h"
#include "airport.h"
#include "sound_func.h"
#include "window_func.h"
#include "strings_func.h"
#include "viewport_func.h"
#include "gfx_func.h"
#include "company_func.h"
#include "tilehighlight_func.h"
#include "company_base.h"

#include "table/sprites.h"
#include "table/strings.h"

static byte _selected_airport_type;

static void ShowBuildAirportPicker(Window *parent);


void CcBuildAirport(const CommandCost &result, TileIndex tile, uint32 p1, uint32 p2)
{
	if (result.Failed()) return;

	SndPlayTileFx(SND_1F_SPLAT, tile);
	if (!_settings_client.gui.persistent_buildingtools) ResetObjectToPlace();
}

static void PlaceAirport(TileIndex tile)
{
	uint32 p2 = _ctrl_pressed;
	SB(p2, 16, 16, INVALID_STATION); // no station to join

	CommandContainer cmdcont = { tile, _selected_airport_type, p2, CMD_BUILD_AIRPORT | CMD_MSG(STR_ERROR_CAN_T_BUILD_AIRPORT_HERE), CcBuildAirport, "" };
	ShowSelectStationIfNeeded(cmdcont, TileArea(tile, _thd.size.x / TILE_SIZE, _thd.size.y / TILE_SIZE));
}

/** Widget number of the airport build window. */
enum {
	ATW_AIRPORT,
	ATW_DEMOLISH,
};


static void BuildAirClick_Airport(Window *w)
{
	if (HandlePlacePushButton(w, ATW_AIRPORT, SPR_CURSOR_AIRPORT, HT_RECT, PlaceAirport)) ShowBuildAirportPicker(w);
}

static void BuildAirClick_Demolish(Window *w)
{
	HandlePlacePushButton(w, ATW_DEMOLISH, ANIMCURSOR_DEMOLISH, HT_RECT, PlaceProc_DemolishArea);
}


typedef void OnButtonClick(Window *w);
static OnButtonClick * const _build_air_button_proc[] = {
	BuildAirClick_Airport,
	BuildAirClick_Demolish,
};

struct BuildAirToolbarWindow : Window {
	BuildAirToolbarWindow(const WindowDesc *desc, WindowNumber window_number) : Window()
	{
		this->InitNested(desc, window_number);
		if (_settings_client.gui.link_terraform_toolbar) ShowTerraformToolbar(this);
	}

	~BuildAirToolbarWindow()
	{
		if (_settings_client.gui.link_terraform_toolbar) DeleteWindowById(WC_SCEN_LAND_GEN, 0, false);
	}

	virtual void OnPaint()
	{
		this->DrawWidgets();
	}

	virtual void OnClick(Point pt, int widget)
	{
		if (!IsInsideBS(widget, ATW_AIRPORT, lengthof(_build_air_button_proc))) return;

		_build_air_button_proc[widget - ATW_AIRPORT](this);
	}


	virtual EventState OnKeyPress(uint16 key, uint16 keycode)
	{
		switch (keycode) {
			case '1': BuildAirClick_Airport(this); break;
			case '2': BuildAirClick_Demolish(this); break;
			default: return ES_NOT_HANDLED;
		}
		return ES_HANDLED;
	}

	virtual void OnPlaceObject(Point pt, TileIndex tile)
	{
		_place_proc(tile);
	}

	virtual void OnPlaceDrag(ViewportPlaceMethod select_method, ViewportDragDropSelectionProcess select_proc, Point pt)
	{
		VpSelectTilesWithMethod(pt.x, pt.y, select_method);
	}

	virtual void OnPlaceMouseUp(ViewportPlaceMethod select_method, ViewportDragDropSelectionProcess select_proc, Point pt, TileIndex start_tile, TileIndex end_tile)
	{
		if (pt.x != -1 && select_proc == DDSP_DEMOLISH_AREA) {
			GUIPlaceProcDragXY(select_proc, start_tile, end_tile);
		}
	}

	virtual void OnPlaceObjectAbort()
	{
		this->RaiseButtons();

		DeleteWindowById(WC_BUILD_STATION, TRANSPORT_AIR);
		DeleteWindowById(WC_SELECT_STATION, 0);
	}
};

static const NWidgetPart _nested_air_toolbar_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN), SetDataTip(STR_TOOLBAR_AIRCRAFT_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_STICKYBOX, COLOUR_DARK_GREEN),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, ATW_AIRPORT), SetFill(0, 1), SetMinimalSize(42, 22), SetDataTip(SPR_IMG_AIRPORT, STR_TOOLBAR_AIRCRAFT_BUILD_AIRPORT_TOOLTIP),
		NWidget(WWT_PANEL, COLOUR_DARK_GREEN), SetMinimalSize(4, 22), SetFill(1, 1), EndContainer(),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, ATW_DEMOLISH), SetFill(0, 1), SetMinimalSize(22, 22), SetDataTip(SPR_IMG_DYNAMITE, STR_TOOLTIP_DEMOLISH_BUILDINGS_ETC),
	EndContainer(),
};

static const WindowDesc _air_toolbar_desc(
	WDP_ALIGN_TOOLBAR, 0, 0,
	WC_BUILD_TOOLBAR, WC_NONE,
	WDF_CONSTRUCTION,
	_nested_air_toolbar_widgets, lengthof(_nested_air_toolbar_widgets)
);

void ShowBuildAirToolbar()
{
	if (!Company::IsValidID(_local_company)) return;

	DeleteWindowByClass(WC_BUILD_TOOLBAR);
	AllocateWindowDescFront<BuildAirToolbarWindow>(&_air_toolbar_desc, TRANSPORT_AIR);
}

/** Airport widgets in the airport picker window. */
enum AirportPickerWidgets {
	/* Airport selection buttons. */
	BAW_SMALL_AIRPORT,
	BAW_CITY_AIRPORT,
	BAW_HELIPORT,
	BAW_METRO_AIRPORT,
	BAW_INTERNATIONAL_AIRPORT,
	BAW_COMMUTER_AIRPORT,
	BAW_HELIDEPOT,
	BAW_INTERCONTINENTAL_AIRPORT,
	BAW_HELISTATION,

	BAW_BOTTOMPANEL,
	BAW_BTN_DONTHILIGHT,
	BAW_BTN_DOHILIGHT,

	BAW_LAST_AIRPORT = BAW_HELISTATION,
	BAW_AIRPORT_COUNT = BAW_LAST_AIRPORT - BAW_SMALL_AIRPORT + 1,
};

class AirportPickerWindow : public PickerWindowBase {
public:
	AirportPickerWindow(const WindowDesc *desc, Window *parent) : PickerWindowBase(parent)
	{
		this->InitNested(desc, TRANSPORT_AIR);
		this->SetWidgetLoweredState(BAW_BTN_DONTHILIGHT, !_settings_client.gui.station_show_coverage);
		this->SetWidgetLoweredState(BAW_BTN_DOHILIGHT, _settings_client.gui.station_show_coverage);
		this->OnInvalidateData();
		this->SelectOtherAirport(_selected_airport_type);
	}

	virtual ~AirportPickerWindow()
	{
		DeleteWindowById(WC_SELECT_STATION, 0);
	}

	virtual void OnPaint()
	{
		this->DrawWidgets();

		const AirportSpec *as = AirportSpec::Get(_selected_airport_type);
		int rad = _settings_game.station.modified_catchment ? as->catchment : (uint)CA_UNMODIFIED;

		uint16 top = this->GetWidget<NWidgetBase>(BAW_BTN_DOHILIGHT)->pos_y + this->GetWidget<NWidgetBase>(BAW_BTN_DOHILIGHT)->current_y + WD_PAR_VSEP_NORMAL;
		NWidgetBase *panel_nwi = this->GetWidget<NWidgetBase>(BAW_BOTTOMPANEL);
		int right = panel_nwi->pos_x +  panel_nwi->current_x;
		int bottom = panel_nwi->pos_y +  panel_nwi->current_y;
		/* only show the station (airport) noise, if the noise option is activated */
		if (_settings_game.economy.station_noise_level) {
			/* show the noise of the selected airport */
			SetDParam(0, as->noise_level);
			DrawString(panel_nwi->pos_x + WD_FRAMERECT_LEFT, right - WD_FRAMERECT_RIGHT, top, STR_STATION_BUILD_NOISE);
			top += FONT_HEIGHT_NORMAL + WD_PAR_VSEP_NORMAL;
		}

		/* strings such as 'Size' and 'Coverage Area' */
		top = DrawStationCoverageAreaText(panel_nwi->pos_x + WD_FRAMERECT_LEFT, right - WD_FRAMERECT_RIGHT, top, SCT_ALL, rad, false) + WD_PAR_VSEP_NORMAL;
		top = DrawStationCoverageAreaText(panel_nwi->pos_x + WD_FRAMERECT_LEFT, right - WD_FRAMERECT_RIGHT, top, SCT_ALL, rad, true) + WD_PAR_VSEP_NORMAL;
		/* Resize background if the text is not equally long as the window. */
		if (top > bottom || (top < bottom && panel_nwi->current_y > panel_nwi->smallest_y)) {
			ResizeWindow(this, 0, top - bottom);
		}
	}

	void SelectOtherAirport(byte airport_id)
	{
		this->RaiseWidget(_selected_airport_type + BAW_SMALL_AIRPORT);
		_selected_airport_type = airport_id;
		this->LowerWidget(airport_id + BAW_SMALL_AIRPORT);

		const AirportSpec *as = AirportSpec::Get(airport_id);
		SetTileSelectSize(as->size_x, as->size_y);

		int rad = _settings_game.station.modified_catchment ? as->catchment : (uint)CA_UNMODIFIED;
		if (_settings_client.gui.station_show_coverage) SetTileSelectBigSize(-rad, -rad, 2 * rad, 2 * rad);

		this->SetDirty();
	}

	virtual void OnInvalidateData(int data = 0)
	{
		if (!AirportSpec::Get(_selected_airport_type)->IsAvailable()) {
			for (int i = 0; i < BAW_AIRPORT_COUNT; i++) {
				if (AirportSpec::Get(i)->IsAvailable()) {
					this->SelectOtherAirport(i);
					break;
				}
			}
		}
		for (int i = 0; i < BAW_AIRPORT_COUNT; i++) {
			this->SetWidgetDisabledState(i + BAW_SMALL_AIRPORT, !AirportSpec::Get(i)->IsAvailable());
		}
	}

	virtual void OnClick(Point pt, int widget)
	{
		switch (widget) {
			case BAW_SMALL_AIRPORT: case BAW_CITY_AIRPORT: case BAW_HELIPORT: case BAW_METRO_AIRPORT:
			case BAW_INTERNATIONAL_AIRPORT: case BAW_COMMUTER_AIRPORT: case BAW_HELIDEPOT:
			case BAW_INTERCONTINENTAL_AIRPORT: case BAW_HELISTATION:
				this->SelectOtherAirport(widget - BAW_SMALL_AIRPORT);
				SndPlayFx(SND_15_BEEP);
				DeleteWindowById(WC_SELECT_STATION, 0);
				break;

			case BAW_BTN_DONTHILIGHT: case BAW_BTN_DOHILIGHT:
				_settings_client.gui.station_show_coverage = (widget != BAW_BTN_DONTHILIGHT);
				this->SetWidgetLoweredState(BAW_BTN_DONTHILIGHT, !_settings_client.gui.station_show_coverage);
				this->SetWidgetLoweredState(BAW_BTN_DOHILIGHT, _settings_client.gui.station_show_coverage);
				SndPlayFx(SND_15_BEEP);
				this->SelectOtherAirport(_selected_airport_type);
				break;
		}
	}

	virtual void OnTick()
	{
		CheckRedrawStationCoverage(this);
	}
};

static const NWidgetPart _nested_build_airport_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN), SetDataTip(STR_STATION_BUILD_AIRPORT_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
	EndContainer(),
	/* Small airports. */
	NWidget(WWT_PANEL, COLOUR_DARK_GREEN),
		NWidget(WWT_LABEL, COLOUR_DARK_GREEN), SetMinimalSize(148, 14), SetFill(1, 0), SetDataTip(STR_STATION_BUILD_AIRPORT_SMALL_AIRPORTS, STR_NULL),
		NWidget(NWID_HORIZONTAL),
			NWidget(NWID_SPACER), SetMinimalSize(2, 0),
			NWidget(NWID_VERTICAL),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, BAW_SMALL_AIRPORT), SetMinimalSize(144, 12), SetFill(1, 0),
									SetDataTip(STR_STATION_BUILD_AIRPORT_SMALL_AIRPORT, STR_STATION_BUILD_AIRPORT_TOOLTIP),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, BAW_COMMUTER_AIRPORT), SetMinimalSize(144, 12), SetFill(1, 0),
									SetDataTip(STR_STATION_BUILD_AIRPORT_COMMUTER_AIRPORT, STR_STATION_BUILD_AIRPORT_TOOLTIP),
				NWidget(NWID_SPACER), SetMinimalSize(0, 1), SetFill(1, 0),
			EndContainer(),
			NWidget(NWID_SPACER), SetMinimalSize(2, 0),
		EndContainer(),
	EndContainer(),
	/* Large airports. */
	NWidget(WWT_PANEL, COLOUR_DARK_GREEN),
		NWidget(WWT_LABEL, COLOUR_DARK_GREEN), SetMinimalSize(148, 14), SetFill(1, 0), SetDataTip(STR_STATION_BUILD_AIRPORT_LARGE_AIRPORTS, STR_NULL),
		NWidget(NWID_HORIZONTAL),
			NWidget(NWID_SPACER), SetMinimalSize(2, 0),
			NWidget(NWID_VERTICAL),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, BAW_CITY_AIRPORT), SetMinimalSize(144, 12), SetFill(1, 0),
									SetDataTip(STR_STATION_BUILD_AIRPORT_CITY_AIRPORT, STR_STATION_BUILD_AIRPORT_TOOLTIP),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, BAW_METRO_AIRPORT), SetMinimalSize(144, 12), SetFill(1, 0),
									SetDataTip(STR_STATION_BUILD_AIRPORT_METRO_AIRPORT, STR_STATION_BUILD_AIRPORT_TOOLTIP),
				NWidget(NWID_SPACER), SetMinimalSize(0, 1), SetFill(1, 0),
			EndContainer(),
			NWidget(NWID_SPACER), SetMinimalSize(2, 0),
		EndContainer(),
	EndContainer(),
	/* Hub airports. */
	NWidget(WWT_PANEL, COLOUR_DARK_GREEN),
		NWidget(WWT_LABEL, COLOUR_DARK_GREEN), SetMinimalSize(148, 14), SetFill(1, 0), SetDataTip(STR_STATION_BUILD_AIRPORT_HUB_AIRPORTS, STR_NULL),
		NWidget(NWID_HORIZONTAL),
			NWidget(NWID_SPACER), SetMinimalSize(2, 0),
			NWidget(NWID_VERTICAL),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, BAW_INTERNATIONAL_AIRPORT), SetMinimalSize(144, 12), SetFill(1, 0),
									SetDataTip(STR_STATION_BUILD_AIRPORT_INTERNATIONAL_AIRPORT, STR_STATION_BUILD_AIRPORT_TOOLTIP),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, BAW_INTERCONTINENTAL_AIRPORT), SetMinimalSize(144, 12), SetFill(1, 0),
									SetDataTip(STR_STATION_BUILD_AIRPORT_INTERCONTINENTAL_AIRPORT, STR_STATION_BUILD_AIRPORT_TOOLTIP),
				NWidget(NWID_SPACER), SetMinimalSize(0, 1), SetFill(1, 0),
			EndContainer(),
			NWidget(NWID_SPACER), SetMinimalSize(2, 0),
		EndContainer(),
	EndContainer(),
	/* Heliports. */
	NWidget(WWT_PANEL, COLOUR_DARK_GREEN),
		NWidget(WWT_LABEL, COLOUR_DARK_GREEN), SetMinimalSize(148, 14), SetFill(1, 0), SetDataTip(STR_STATION_BUILD_AIRPORT_HELIPORTS, STR_NULL),
		NWidget(NWID_HORIZONTAL),
			NWidget(NWID_SPACER), SetMinimalSize(2, 0),
			NWidget(NWID_VERTICAL),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, BAW_HELIPORT), SetMinimalSize(144, 12), SetFill(1, 0),
									SetDataTip(STR_STATION_BUILD_AIRPORT_HELIPORT, STR_STATION_BUILD_AIRPORT_TOOLTIP),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, BAW_HELISTATION), SetMinimalSize(144, 12), SetFill(1, 0),
									SetDataTip(STR_STATION_BUILD_AIRPORT_HELISTATION, STR_STATION_BUILD_AIRPORT_TOOLTIP),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, BAW_HELIDEPOT), SetMinimalSize(144, 12), SetFill(1, 0),
									SetDataTip(STR_STATION_BUILD_AIRPORT_HELIDEPOT, STR_STATION_BUILD_AIRPORT_TOOLTIP),
				NWidget(NWID_SPACER), SetMinimalSize(0, 1), SetFill(1, 0),
			EndContainer(),
			NWidget(NWID_SPACER), SetMinimalSize(2, 0),
		EndContainer(),
	EndContainer(),
	/* Bottom panel. */
	NWidget(WWT_PANEL, COLOUR_DARK_GREEN, BAW_BOTTOMPANEL),
		NWidget(WWT_LABEL, COLOUR_DARK_GREEN), SetMinimalSize(148, 14), SetFill(1, 0), SetDataTip(STR_STATION_BUILD_COVERAGE_AREA_TITLE, STR_NULL),
		NWidget(NWID_HORIZONTAL),
			NWidget(NWID_SPACER), SetMinimalSize(14, 0),
			NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, BAW_BTN_DONTHILIGHT), SetMinimalSize(60, 12), SetFill(1, 0),
											SetDataTip(STR_STATION_BUILD_COVERAGE_OFF, STR_STATION_BUILD_COVERAGE_AREA_OFF_TOOLTIP),
				NWidget(WWT_TEXTBTN, COLOUR_GREY, BAW_BTN_DOHILIGHT), SetMinimalSize(60, 12), SetFill(1, 0),
											SetDataTip(STR_STATION_BUILD_COVERAGE_ON, STR_STATION_BUILD_COVERAGE_AREA_ON_TOOLTIP),
			EndContainer(),
			NWidget(NWID_SPACER), SetMinimalSize(14, 0),
		EndContainer(),
		NWidget(NWID_SPACER), SetMinimalSize(0, 10), SetResize(0, 1), SetFill(1, 0),
	EndContainer(),
};

static const WindowDesc _build_airport_desc(
	WDP_AUTO, 0, 0,
	WC_BUILD_STATION, WC_BUILD_TOOLBAR,
	WDF_CONSTRUCTION,
	_nested_build_airport_widgets, lengthof(_nested_build_airport_widgets)
);

static void ShowBuildAirportPicker(Window *parent)
{
	new AirportPickerWindow(&_build_airport_desc, parent);
}

void InitializeAirportGui()
{
	_selected_airport_type = AT_SMALL;
}
