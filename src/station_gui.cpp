/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file station_gui.cpp The GUI for stations. */

#include "stdafx.h"
#include "debug.h"
#include "gui.h"
#include "textbuf_gui.h"
#include "company_func.h"
#include "command_func.h"
#include "vehicle_gui.h"
#include "cargotype.h"
#include "station_gui.h"
#include "strings_func.h"
#include "window_func.h"
#include "viewport_func.h"
#include "widgets/dropdown_func.h"
#include "station_base.h"
#include "waypoint_base.h"
#include "tilehighlight_func.h"
#include "company_base.h"
#include "sortlist_type.h"
#include "core/geometry_func.hpp"
#include "vehiclelist.h"

#include "table/strings.h"

#include <vector>

/**
 * Draw a (multi)line of cargos seperated by commas, and prefixed with a string.
 * @param cargo_mask Mask of cargos to include in the list.
 * @param r          Rectangle to draw the cargos in.
 * @param prefix     String to use as prefix for the list of cargos.
 * @return Bottom position of the last line used for drawing the cargos.
 */
static int DrawCargoListText(uint32 cargo_mask, const Rect &r, StringID prefix)
{
	bool first = true;
	char string[512];
	char *b = string;

	CargoID i;
	FOR_EACH_SET_CARGO_ID(i, cargo_mask) {
		if (b >= lastof(string) - (1 + 2 * 4)) break; // ',' or ' ' and two calls to Utf8Encode()

		if (first) {
			first = false;
		} else {
			/* Add a comma if this is not the first item */
			*b++ = ',';
			*b++ = ' ';
		}
		b = InlineString(b, CargoSpec::Get(i)->name);
	}

	/* If first is still true then no cargo is accepted */
	if (first) b = InlineString(b, STR_JUST_NOTHING);

	*b = '\0';

	/* Make sure we detect any buffer overflow */
	assert(b < endof(string));

	SetDParamStr(0, string);
	return DrawStringMultiLine(r.left, r.right, r.top, r.bottom, prefix);
}

/**
 * Calculates and draws the accepted or supplied cargo around the selected tile(s)
 * @param left x position where the string is to be drawn
 * @param right the right most position to draw on
 * @param top y position where the string is to be drawn
 * @param sct which type of cargo is to be displayed (passengers/non-passengers)
 * @param rad radius around selected tile(s) to be searched
 * @param supplies if supplied cargos should be drawn, else accepted cargos
 * @return Returns the y value below the string that was drawn
 */
int DrawStationCoverageAreaText(int left, int right, int top, StationCoverageType sct, int rad, bool supplies)
{
	TileIndex tile = TileVirtXY(_thd.pos.x, _thd.pos.y);
	if (tile < MapSize()) {
		CargoArray cargos;
		if (supplies) {
			cargos = GetProductionAroundTiles(tile, _thd.size.x / TILE_SIZE, _thd.size.y / TILE_SIZE, rad);
		} else {
			cargos = GetAcceptanceAroundTiles(tile, _thd.size.x / TILE_SIZE, _thd.size.y / TILE_SIZE, rad);
		}

		/* Convert cargo counts to a set of cargo bits, and draw the result. */
		uint32 cargo_mask = 0;
		for (CargoID i = 0; i < NUM_CARGO; i++) {
			switch (sct) {
				case SCT_PASSENGERS_ONLY: if (!IsCargoInClass(i, CC_PASSENGERS)) continue; break;
				case SCT_NON_PASSENGERS_ONLY: if (IsCargoInClass(i, CC_PASSENGERS)) continue; break;
				case SCT_ALL: break;
				default: NOT_REACHED();
			}
			if (cargos[i] >= (supplies ? 1U : 8U)) SetBit(cargo_mask, i);
		}
		Rect r = {left, top, right, INT32_MAX};
		return DrawCargoListText(cargo_mask, r, supplies ? STR_STATION_BUILD_SUPPLIES_CARGO : STR_STATION_BUILD_ACCEPTS_CARGO);
	}

	return top;
}

/**
 * Check whether we need to redraw the station coverage text.
 * If it is needed actually make the window for redrawing.
 * @param w the window to check.
 */
void CheckRedrawStationCoverage(const Window *w)
{
	if (_thd.dirty & 1) {
		_thd.dirty &= ~1;
		w->SetDirty();
	}
}

/**
 * Draw small boxes of cargo amount and ratings data at the given
 * coordinates. If amount exceeds 576 units, it is shown 'full', same
 * goes for the rating: at above 90% orso (224) it is also 'full'
 *
 * @param left   left most coordinate to draw the box at
 * @param right  right most coordinate to draw the box at
 * @param y      coordinate to draw the box at
 * @param type   Cargo type
 * @param amount Cargo amount
 * @param rating ratings data for that particular cargo
 *
 * @note Each cargo-bar is 16 pixels wide and 6 pixels high
 * @note Each rating 14 pixels wide and 1 pixel high and is 1 pixel below the cargo-bar
 */
static void StationsWndShowStationRating(int left, int right, int y, CargoID type, uint amount, byte rating)
{
	static const uint units_full  = 576; ///< number of units to show station as 'full'
	static const uint rating_full = 224; ///< rating needed so it is shown as 'full'

	const CargoSpec *cs = CargoSpec::Get(type);
	if (!cs->IsValid()) return;

	int colour = cs->rating_colour;
	uint w = (minu(amount, units_full) + 5) / 36;

	int height = GetCharacterHeight(FS_SMALL);

	/* Draw total cargo (limited) on station (fits into 16 pixels) */
	if (w != 0) GfxFillRect(left, y, left + w - 1, y + height, colour);

	/* Draw a one pixel-wide bar of additional cargo meter, useful
	 * for stations with only a small amount (<=30) */
	if (w == 0) {
		uint rest = amount / 5;
		if (rest != 0) {
			w += left;
			GfxFillRect(w, y + height - rest, w, y + height, colour);
		}
	}

	DrawString(left + 1, right, y, cs->abbrev, TC_BLACK);

	/* Draw green/red ratings bar (fits into 14 pixels) */
	y += height + 2;
	GfxFillRect(left + 1, y, left + 14, y, 0xB8);
	rating = minu(rating, rating_full) / 16;
	if (rating != 0) GfxFillRect(left + 1, y, left + rating, y, 0xD0);
}

typedef GUIList<const Station*> GUIStationList;

/** Enum for CompanyStations, referring to _company_stations_widgets */
enum StationListWidgets {
	SLW_CAPTION,        ///< Window caption
	SLW_LIST,           ///< The main panel, list of stations
	SLW_SCROLLBAR,      ///< Scrollbar next to the main panel

	/* Vehicletypes need to be in order of StationFacility due to bit magic */
	SLW_TRAIN,          ///< 'TRAIN' button - list only facilities where is a railroad station
	SLW_TRUCK,          ///< 'TRUCK' button - list only facilities where is a truck stop
	SLW_BUS,            ///< 'BUS' button - list only facilities where is a bus stop
	SLW_AIRPLANE,       ///< 'AIRPLANE' button - list only facilities where is an airport
	SLW_SHIP,           ///< 'SHIP' button - list only facilities where is a dock
	SLW_FACILALL,       ///< 'ALL' button - list all facilities

	SLW_NOCARGOWAITING, ///< 'NO' button - list stations where no cargo is waiting
	SLW_CARGOALL,       ///< 'ALL' button - list all stations

	SLW_SORTBY,         ///< 'Sort by' button - reverse sort direction
	SLW_SORTDROPBTN,    ///< Dropdown button

	SLW_CARGOSTART,     ///< Widget numbers used for list of cargo types (not present in _company_stations_widgets)
};

/**
 * The list of stations per company.
 */
class CompanyStationsWindow : public Window
{
protected:
	/* Runtime saved values */
	static Listing last_sorting;
	static byte facilities;               // types of stations of interest
	static bool include_empty;            // whether we should include stations without waiting cargo
	static const uint32 cargo_filter_max;
	static uint32 cargo_filter;           // bitmap of cargo types to include
	static const Station *last_station;

	/* Constants for sorting stations */
	static const StringID sorter_names[];
	static GUIStationList::SortFunction * const sorter_funcs[];

	GUIStationList stations;
	Scrollbar *vscroll;

	/**
	 * (Re)Build station list
	 *
	 * @param owner company whose stations are to be in list
	 */
	void BuildStationsList(const Owner owner)
	{
		if (!this->stations.NeedRebuild()) return;

		DEBUG(misc, 3, "Building station list for company %d", owner);

		this->stations.Clear();

		const Station *st;
		FOR_ALL_STATIONS(st) {
			if (st->owner == owner || (st->owner == OWNER_NONE && HasStationInUse(st->index, true, owner))) {
				if (this->facilities & st->facilities) { // only stations with selected facilities
					int num_waiting_cargo = 0;
					for (CargoID j = 0; j < NUM_CARGO; j++) {
						if (HasBit(st->goods[j].acceptance_pickup, GoodsEntry::PICKUP)) {
							num_waiting_cargo++; // count number of waiting cargo
							if (HasBit(this->cargo_filter, j)) {
								*this->stations.Append() = st;
								break;
							}
						}
					}
					/* stations without waiting cargo */
					if (num_waiting_cargo == 0 && this->include_empty) {
						*this->stations.Append() = st;
					}
				}
			}
		}

		this->stations.Compact();
		this->stations.RebuildDone();

		this->vscroll->SetCount(this->stations.Length()); // Update the scrollbar
	}

	/** Sort stations by their name */
	static int CDECL StationNameSorter(const Station * const *a, const Station * const *b)
	{
		static char buf_cache[64];
		char buf[64];

		SetDParam(0, (*a)->index);
		GetString(buf, STR_STATION_NAME, lastof(buf));

		if (*b != last_station) {
			last_station = *b;
			SetDParam(0, (*b)->index);
			GetString(buf_cache, STR_STATION_NAME, lastof(buf_cache));
		}

		return strcmp(buf, buf_cache);
	}

	/** Sort stations by their type */
	static int CDECL StationTypeSorter(const Station * const *a, const Station * const *b)
	{
		return (*a)->facilities - (*b)->facilities;
	}

	/** Sort stations by their waiting cargo */
	static int CDECL StationWaitingSorter(const Station * const *a, const Station * const *b)
	{
		Money diff = 0;

		CargoID j;
		FOR_EACH_SET_CARGO_ID(j, cargo_filter) {
			if (!(*a)->goods[j].cargo.Empty()) diff += GetTransportedGoodsIncome((*a)->goods[j].cargo.Count(), 20, 50, j);
			if (!(*b)->goods[j].cargo.Empty()) diff -= GetTransportedGoodsIncome((*b)->goods[j].cargo.Count(), 20, 50, j);
		}

		return ClampToI32(diff);
	}

	/** Sort stations by their rating */
	static int CDECL StationRatingMaxSorter(const Station * const *a, const Station * const *b)
	{
		byte maxr1 = 0;
		byte maxr2 = 0;

		CargoID j;
		FOR_EACH_SET_CARGO_ID(j, cargo_filter) {
			if (HasBit((*a)->goods[j].acceptance_pickup, GoodsEntry::PICKUP)) maxr1 = max(maxr1, (*a)->goods[j].rating);
			if (HasBit((*b)->goods[j].acceptance_pickup, GoodsEntry::PICKUP)) maxr2 = max(maxr2, (*b)->goods[j].rating);
		}

		return maxr1 - maxr2;
	}

	/** Sort stations by their rating */
	static int CDECL StationRatingMinSorter(const Station * const *a, const Station * const *b)
	{
		byte minr1 = 255;
		byte minr2 = 255;

		for (CargoID j = 0; j < NUM_CARGO; j++) {
			if (!HasBit(cargo_filter, j)) continue;
			if (HasBit((*a)->goods[j].acceptance_pickup, GoodsEntry::PICKUP)) minr1 = min(minr1, (*a)->goods[j].rating);
			if (HasBit((*b)->goods[j].acceptance_pickup, GoodsEntry::PICKUP)) minr2 = min(minr2, (*b)->goods[j].rating);
		}

		return -(minr1 - minr2);
	}

	/** Sort the stations list */
	void SortStationsList()
	{
		if (!this->stations.Sort()) return;

		/* Reset name sorter sort cache */
		this->last_station = NULL;

		/* Set the modified widget dirty */
		this->SetWidgetDirty(SLW_LIST);
	}

public:
	CompanyStationsWindow(const WindowDesc *desc, WindowNumber window_number) : Window()
	{
		this->stations.SetListing(this->last_sorting);
		this->stations.SetSortFuncs(this->sorter_funcs);
		this->stations.ForceRebuild();
		this->stations.NeedResort();
		this->SortStationsList();

		this->CreateNestedTree(desc);
		this->vscroll = this->GetScrollbar(SLW_SCROLLBAR);
		this->FinishInitNested(desc, window_number);
		this->owner = (Owner)this->window_number;

		CargoID cid;
		FOR_EACH_SET_CARGO_ID(cid, this->cargo_filter) {
			if (CargoSpec::Get(cid)->IsValid()) this->LowerWidget(SLW_CARGOSTART + cid);
		}

		if (this->cargo_filter == this->cargo_filter_max) this->cargo_filter = _cargo_mask;

		for (uint i = 0; i < 5; i++) {
			if (HasBit(this->facilities, i)) this->LowerWidget(i + SLW_TRAIN);
		}
		this->SetWidgetLoweredState(SLW_NOCARGOWAITING, this->include_empty);

		this->GetWidget<NWidgetCore>(SLW_SORTDROPBTN)->widget_data = this->sorter_names[this->stations.SortType()];
	}

	~CompanyStationsWindow()
	{
		this->last_sorting = this->stations.GetListing();
	}

	virtual void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize)
	{
		switch (widget) {
			case SLW_SORTBY: {
				Dimension d = GetStringBoundingBox(this->GetWidget<NWidgetCore>(widget)->widget_data);
				d.width += padding.width + WD_SORTBUTTON_ARROW_WIDTH * 2; // Doubled since the string is centred and it also looks better.
				d.height += padding.height;
				*size = maxdim(*size, d);
				break;
			}

			case SLW_SORTDROPBTN: {
				Dimension d = {0, 0};
				for (int i = 0; this->sorter_names[i] != INVALID_STRING_ID; i++) {
					d = maxdim(d, GetStringBoundingBox(this->sorter_names[i]));
				}
				d.width += padding.width;
				d.height += padding.height;
				*size = maxdim(*size, d);
				break;
			}

			case SLW_LIST:
				resize->height = FONT_HEIGHT_NORMAL;
				size->height = WD_FRAMERECT_TOP + 5 * resize->height + WD_FRAMERECT_BOTTOM;
				break;

			case SLW_TRAIN:
			case SLW_TRUCK:
			case SLW_BUS:
			case SLW_AIRPLANE:
			case SLW_SHIP:
				size->height = max<uint>(FONT_HEIGHT_SMALL, 10) + padding.height;
				break;

			case SLW_CARGOALL:
			case SLW_FACILALL:
			case SLW_NOCARGOWAITING: {
				Dimension d = GetStringBoundingBox(widget == SLW_NOCARGOWAITING ? STR_ABBREV_NONE : STR_ABBREV_ALL);
				d.width  += padding.width + 2;
				d.height += padding.height;
				*size = maxdim(*size, d);
				break;
			}

			default:
				if (widget >= SLW_CARGOSTART) {
					const CargoSpec *cs = CargoSpec::Get(widget - SLW_CARGOSTART);
					if (cs->IsValid()) {
						Dimension d = GetStringBoundingBox(cs->abbrev);
						d.width  += padding.width + 2;
						d.height += padding.height;
						*size = maxdim(*size, d);
					}
				}
				break;
		}
	}

	virtual void OnPaint()
	{
		this->BuildStationsList((Owner)this->window_number);
		this->SortStationsList();

		this->DrawWidgets();
	}

	virtual void DrawWidget(const Rect &r, int widget) const
	{
		switch (widget) {
			case SLW_SORTBY:
				/* draw arrow pointing up/down for ascending/descending sorting */
				this->DrawSortButtonState(SLW_SORTBY, this->stations.IsDescSortOrder() ? SBS_DOWN : SBS_UP);
				break;

			case SLW_LIST: {
				bool rtl = _current_text_dir == TD_RTL;
				int max = min(this->vscroll->GetPosition() + this->vscroll->GetCapacity(), this->stations.Length());
				int y = r.top + WD_FRAMERECT_TOP;
				for (int i = this->vscroll->GetPosition(); i < max; ++i) { // do until max number of stations of owner
					const Station *st = this->stations[i];
					assert(st->xy != INVALID_TILE);

					/* Do not do the complex check HasStationInUse here, it may be even false
					 * when the order had been removed and the station list hasn't been removed yet */
					assert(st->owner == owner || st->owner == OWNER_NONE);

					SetDParam(0, st->index);
					SetDParam(1, st->facilities);
					int x = DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, STR_STATION_LIST_STATION);
					x += rtl ? -5 : 5;

					/* show cargo waiting and station ratings */
					for (CargoID j = 0; j < NUM_CARGO; j++) {
						if (!st->goods[j].cargo.Empty()) {
							/* For RTL we work in exactly the opposite direction. So
							 * decrement the space needed first, then draw to the left
							 * instead of drawing to the left and then incrementing
							 * the space. */
							if (rtl) {
								x -= 20;
								if (x < r.left + WD_FRAMERECT_LEFT) break;
							}
							StationsWndShowStationRating(x, x + 16, y, j, st->goods[j].cargo.Count(), st->goods[j].rating);
							if (!rtl) {
								x += 20;
								if (x > r.right - WD_FRAMERECT_RIGHT) break;
							}
						}
					}
					y += FONT_HEIGHT_NORMAL;
				}

				if (this->vscroll->GetCount() == 0) { // company has no stations
					DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, STR_STATION_LIST_NONE);
					return;
				}
				break;
			}

			case SLW_NOCARGOWAITING: {
				int cg_ofst = this->IsWidgetLowered(widget) ? 2 : 1;
				DrawString(r.left + cg_ofst, r.right + cg_ofst, r.top + cg_ofst, STR_ABBREV_NONE, TC_BLACK, SA_HOR_CENTER);
				break;
			}

			case SLW_CARGOALL: {
				int cg_ofst = this->IsWidgetLowered(widget) ? 2 : 1;
				DrawString(r.left + cg_ofst, r.right + cg_ofst, r.top + cg_ofst, STR_ABBREV_ALL, TC_BLACK, SA_HOR_CENTER);
				break;
			}

			case SLW_FACILALL: {
				int cg_ofst = this->IsWidgetLowered(widget) ? 2 : 1;
				DrawString(r.left + cg_ofst, r.right + cg_ofst, r.top + cg_ofst, STR_ABBREV_ALL, TC_BLACK);
				break;
			}

			default:
				if (widget >= SLW_CARGOSTART) {
					const CargoSpec *cs = CargoSpec::Get(widget - SLW_CARGOSTART);
					if (cs->IsValid()) {
						int cg_ofst = HasBit(this->cargo_filter, cs->Index()) ? 2 : 1;
						GfxFillRect(r.left + cg_ofst, r.top + cg_ofst, r.right - 2 + cg_ofst, r.bottom - 2 + cg_ofst, cs->rating_colour);
						DrawString(r.left + cg_ofst, r.right + cg_ofst, r.top + cg_ofst, cs->abbrev, TC_BLACK, SA_HOR_CENTER);
					}
				}
				break;
		}
	}

	virtual void SetStringParameters(int widget) const
	{
		if (widget == SLW_CAPTION) {
			SetDParam(0, this->window_number);
			SetDParam(1, this->vscroll->GetCount());
		}
	}

	virtual void OnClick(Point pt, int widget, int click_count)
	{
		switch (widget) {
			case SLW_LIST: {
				uint id_v = this->vscroll->GetScrolledRowFromWidget(pt.y, this, SLW_LIST, 0, FONT_HEIGHT_NORMAL);
				if (id_v >= this->stations.Length()) return; // click out of list bound

				const Station *st = this->stations[id_v];
				/* do not check HasStationInUse - it is slow and may be invalid */
				assert(st->owner == (Owner)this->window_number || st->owner == OWNER_NONE);

				if (_ctrl_pressed) {
					ShowExtraViewPortWindow(st->xy);
				} else {
					ScrollMainWindowToTile(st->xy);
				}
				break;
			}

			case SLW_TRAIN:
			case SLW_TRUCK:
			case SLW_BUS:
			case SLW_AIRPLANE:
			case SLW_SHIP:
				if (_ctrl_pressed) {
					ToggleBit(this->facilities, widget - SLW_TRAIN);
					this->ToggleWidgetLoweredState(widget);
				} else {
					uint i;
					FOR_EACH_SET_BIT(i, this->facilities) {
						this->RaiseWidget(i + SLW_TRAIN);
					}
					this->facilities = 1 << (widget - SLW_TRAIN);
					this->LowerWidget(widget);
				}
				this->stations.ForceRebuild();
				this->SetDirty();
				break;

			case SLW_FACILALL:
				for (uint i = SLW_TRAIN; i <= SLW_SHIP; i++) {
					this->LowerWidget(i);
				}

				this->facilities = FACIL_TRAIN | FACIL_TRUCK_STOP | FACIL_BUS_STOP | FACIL_AIRPORT | FACIL_DOCK;
				this->stations.ForceRebuild();
				this->SetDirty();
				break;

			case SLW_CARGOALL: {
				for (uint i = 0; i < NUM_CARGO; i++) {
					const CargoSpec *cs = CargoSpec::Get(i);
					if (cs->IsValid()) this->LowerWidget(SLW_CARGOSTART + i);
				}
				this->LowerWidget(SLW_NOCARGOWAITING);

				this->cargo_filter = _cargo_mask;
				this->include_empty = true;
				this->stations.ForceRebuild();
				this->SetDirty();
				break;
			}

			case SLW_SORTBY: // flip sorting method asc/desc
				this->stations.ToggleSortOrder();
				this->flags4 |= WF_TIMEOUT_BEGIN;
				this->LowerWidget(SLW_SORTBY);
				this->SetDirty();
				break;

			case SLW_SORTDROPBTN: // select sorting criteria dropdown menu
				ShowDropDownMenu(this, this->sorter_names, this->stations.SortType(), SLW_SORTDROPBTN, 0, 0);
				break;

			case SLW_NOCARGOWAITING:
				if (_ctrl_pressed) {
					this->include_empty = !this->include_empty;
					this->ToggleWidgetLoweredState(SLW_NOCARGOWAITING);
				} else {
					for (uint i = 0; i < NUM_CARGO; i++) {
						const CargoSpec *cs = CargoSpec::Get(i);
						if (cs->IsValid()) this->RaiseWidget(SLW_CARGOSTART + i);
					}

					this->cargo_filter = 0;
					this->include_empty = true;

					this->LowerWidget(SLW_NOCARGOWAITING);
				}
				this->stations.ForceRebuild();
				this->SetDirty();
				break;

			default:
				if (widget >= SLW_CARGOSTART) { // change cargo_filter
					/* Determine the selected cargo type */
					const CargoSpec *cs = CargoSpec::Get(widget - SLW_CARGOSTART);
					if (!cs->IsValid()) break;

					if (_ctrl_pressed) {
						ToggleBit(this->cargo_filter, cs->Index());
						this->ToggleWidgetLoweredState(widget);
					} else {
						for (uint i = 0; i < NUM_CARGO; i++) {
							const CargoSpec *cs = CargoSpec::Get(i);
							if (cs->IsValid()) this->RaiseWidget(SLW_CARGOSTART + i);
						}
						this->RaiseWidget(SLW_NOCARGOWAITING);

						this->cargo_filter = 0;
						this->include_empty = false;

						SetBit(this->cargo_filter, cs->Index());
						this->LowerWidget(widget);
					}
					this->stations.ForceRebuild();
					this->SetDirty();
				}
				break;
		}
	}

	virtual void OnDropdownSelect(int widget, int index)
	{
		if (this->stations.SortType() != index) {
			this->stations.SetSortType(index);

			/* Display the current sort variant */
			this->GetWidget<NWidgetCore>(SLW_SORTDROPBTN)->widget_data = this->sorter_names[this->stations.SortType()];

			this->SetDirty();
		}
	}

	virtual void OnTick()
	{
		if (_pause_mode != PM_UNPAUSED) return;
		if (this->stations.NeedResort()) {
			DEBUG(misc, 3, "Periodic rebuild station list company %d", this->window_number);
			this->SetDirty();
		}
	}

	virtual void OnTimeout()
	{
		this->RaiseWidget(SLW_SORTBY);
		this->SetDirty();
	}

	virtual void OnResize()
	{
		this->vscroll->SetCapacityFromWidget(this, SLW_LIST, WD_FRAMERECT_TOP + WD_FRAMERECT_BOTTOM);
	}

	virtual void OnInvalidateData(int data)
	{
		if (data == 0) {
			this->stations.ForceRebuild();
		} else {
			this->stations.ForceResort();
		}
	}
};

Listing CompanyStationsWindow::last_sorting = {false, 0};
byte CompanyStationsWindow::facilities = FACIL_TRAIN | FACIL_TRUCK_STOP | FACIL_BUS_STOP | FACIL_AIRPORT | FACIL_DOCK;
bool CompanyStationsWindow::include_empty = true;
const uint32 CompanyStationsWindow::cargo_filter_max = UINT32_MAX;
uint32 CompanyStationsWindow::cargo_filter = UINT32_MAX;
const Station *CompanyStationsWindow::last_station = NULL;

/* Availible station sorting functions */
GUIStationList::SortFunction * const CompanyStationsWindow::sorter_funcs[] = {
	&StationNameSorter,
	&StationTypeSorter,
	&StationWaitingSorter,
	&StationRatingMaxSorter,
	&StationRatingMinSorter
};

/* Names of the sorting functions */
const StringID CompanyStationsWindow::sorter_names[] = {
	STR_SORT_BY_NAME,
	STR_SORT_BY_FACILITY,
	STR_SORT_BY_WAITING,
	STR_SORT_BY_RATING_MAX,
	STR_SORT_BY_RATING_MIN,
	INVALID_STRING_ID
};

/**
 * Make a horizontal row of cargo buttons, starting at widget #SLW_CARGOSTART.
 * @param biggest_index Pointer to store biggest used widget number of the buttons.
 * @return Horizontal row.
 */
static NWidgetBase *CargoWidgets(int *biggest_index)
{
	NWidgetHorizontal *container = new NWidgetHorizontal();

	for (uint i = 0; i < NUM_CARGO; i++) {
		const CargoSpec *cs = CargoSpec::Get(i);
		if (cs->IsValid()) {
			NWidgetBackground *panel = new NWidgetBackground(WWT_PANEL, COLOUR_GREY, SLW_CARGOSTART + i);
			panel->SetMinimalSize(14, 11);
			panel->SetResize(0, 0);
			panel->SetFill(0, 1);
			panel->SetDataTip(0, STR_STATION_LIST_USE_CTRL_TO_SELECT_MORE);
			container->Add(panel);
		} else {
			NWidgetLeaf *nwi = new NWidgetLeaf(WWT_EMPTY, COLOUR_GREY, SLW_CARGOSTART + i, 0x0, STR_NULL);
			nwi->SetMinimalSize(0, 11);
			nwi->SetResize(0, 0);
			nwi->SetFill(0, 1);
			container->Add(nwi);
		}
	}
	*biggest_index = SLW_CARGOSTART + NUM_CARGO;
	return container;
}

static const NWidgetPart _nested_company_stations_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, SLW_CAPTION), SetDataTip(STR_STATION_LIST_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_TEXTBTN, COLOUR_GREY, SLW_TRAIN), SetMinimalSize(14, 11), SetDataTip(STR_TRAIN, STR_STATION_LIST_USE_CTRL_TO_SELECT_MORE), SetFill(0, 1),
		NWidget(WWT_TEXTBTN, COLOUR_GREY, SLW_TRUCK), SetMinimalSize(14, 11), SetDataTip(STR_LORRY, STR_STATION_LIST_USE_CTRL_TO_SELECT_MORE), SetFill(0, 1),
		NWidget(WWT_TEXTBTN, COLOUR_GREY, SLW_BUS), SetMinimalSize(14, 11), SetDataTip(STR_BUS, STR_STATION_LIST_USE_CTRL_TO_SELECT_MORE), SetFill(0, 1),
		NWidget(WWT_TEXTBTN, COLOUR_GREY, SLW_SHIP), SetMinimalSize(14, 11), SetDataTip(STR_SHIP, STR_STATION_LIST_USE_CTRL_TO_SELECT_MORE), SetFill(0, 1),
		NWidget(WWT_TEXTBTN, COLOUR_GREY, SLW_AIRPLANE), SetMinimalSize(14, 11), SetDataTip(STR_PLANE, STR_STATION_LIST_USE_CTRL_TO_SELECT_MORE), SetFill(0, 1),
		NWidget(WWT_PUSHBTN, COLOUR_GREY, SLW_FACILALL), SetMinimalSize(14, 11), SetDataTip(0x0, STR_STATION_LIST_SELECT_ALL_FACILITIES), SetFill(0, 1),
		NWidget(WWT_PANEL, COLOUR_GREY), SetMinimalSize(5, 11), SetFill(0, 1), EndContainer(),
		NWidgetFunction(CargoWidgets),
		NWidget(WWT_PANEL, COLOUR_GREY, SLW_NOCARGOWAITING), SetMinimalSize(14, 11), SetDataTip(0x0, STR_STATION_LIST_NO_WAITING_CARGO), SetFill(0, 1), EndContainer(),
		NWidget(WWT_PUSHBTN, COLOUR_GREY, SLW_CARGOALL), SetMinimalSize(14, 11), SetDataTip(0x0, STR_STATION_LIST_SELECT_ALL_TYPES), SetFill(0, 1),
		NWidget(WWT_PANEL, COLOUR_GREY), SetDataTip(0x0, STR_NULL), SetResize(1, 0), SetFill(1, 1), EndContainer(),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_TEXTBTN, COLOUR_GREY, SLW_SORTBY), SetMinimalSize(81, 12), SetDataTip(STR_BUTTON_SORT_BY, STR_TOOLTIP_SORT_ORDER),
		NWidget(WWT_DROPDOWN, COLOUR_GREY, SLW_SORTDROPBTN), SetMinimalSize(163, 12), SetDataTip(STR_SORT_BY_NAME, STR_TOOLTIP_SORT_CRITERIA), // widget_data gets overwritten.
		NWidget(WWT_PANEL, COLOUR_GREY), SetDataTip(0x0, STR_NULL), SetResize(1, 0), SetFill(1, 1), EndContainer(),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, COLOUR_GREY, SLW_LIST), SetMinimalSize(346, 125), SetResize(1, 10), SetDataTip(0x0, STR_STATION_LIST_TOOLTIP), SetScrollbar(SLW_SCROLLBAR), EndContainer(),
		NWidget(NWID_VERTICAL),
			NWidget(NWID_VSCROLLBAR, COLOUR_GREY, SLW_SCROLLBAR),
			NWidget(WWT_RESIZEBOX, COLOUR_GREY),
		EndContainer(),
	EndContainer(),
};

static const WindowDesc _company_stations_desc(
	WDP_AUTO, 358, 162,
	WC_STATION_LIST, WC_NONE,
	WDF_UNCLICK_BUTTONS,
	_nested_company_stations_widgets, lengthof(_nested_company_stations_widgets)
);

/**
 * Opens window with list of company's stations
 *
 * @param company whose stations' list show
 */
void ShowCompanyStations(CompanyID company)
{
	if (!Company::IsValidID(company)) return;

	AllocateWindowDescFront<CompanyStationsWindow>(&_company_stations_desc, company);
}

static const NWidgetPart _nested_station_view_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, SVW_CAPTION), SetDataTip(STR_STATION_VIEW_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, SVW_SORT_ORDER), SetMinimalSize(81, 12), SetFill(1, 1), SetDataTip(STR_BUTTON_SORT_BY, STR_TOOLTIP_SORT_ORDER),
		NWidget(WWT_DROPDOWN, COLOUR_GREY, SVW_SORT_BY), SetMinimalSize(168, 12), SetResize(1, 0), SetFill(0, 1), SetDataTip(0x0, STR_TOOLTIP_SORT_CRITERIA),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_TEXTBTN, COLOUR_GREY, SVW_GROUP), SetMinimalSize(81, 12), SetFill(1, 1), SetDataTip(STR_STATION_VIEW_GROUP, 0x0),
		NWidget(WWT_DROPDOWN, COLOUR_GREY, SVW_GROUP_BY), SetMinimalSize(168, 12), SetResize(1, 0), SetFill(0, 1), SetDataTip(0x0, STR_TOOLTIP_GROUP_ORDER),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, COLOUR_GREY, SVW_WAITING), SetMinimalSize(237, 44), SetResize(1, 10), SetScrollbar(SVW_SCROLLBAR), EndContainer(),
		NWidget(NWID_VSCROLLBAR, COLOUR_GREY, SVW_SCROLLBAR),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_GREY, SVW_ACCEPTLIST), SetMinimalSize(249, 23), SetResize(1, 0), EndContainer(),
	NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, SVW_LOCATION), SetMinimalSize(60, 12), SetResize(1, 0), SetFill(1, 1),
				SetDataTip(STR_BUTTON_LOCATION, STR_STATION_VIEW_CENTER_TOOLTIP),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, SVW_ACCEPTS), SetMinimalSize(61, 12), SetResize(1, 0), SetFill(1, 1),
				SetDataTip(STR_STATION_VIEW_RATINGS_BUTTON, STR_STATION_VIEW_RATINGS_TOOLTIP),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, SVW_RENAME), SetMinimalSize(60, 12), SetResize(1, 0), SetFill(1, 1),
				SetDataTip(STR_BUTTON_RENAME, STR_STATION_VIEW_RENAME_TOOLTIP),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, SVW_TRAINS), SetMinimalSize(14, 12), SetFill(0, 1), SetDataTip(STR_TRAIN, STR_STATION_VIEW_SCHEDULED_TRAINS_TOOLTIP),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, SVW_ROADVEHS), SetMinimalSize(14, 12), SetFill(0, 1), SetDataTip(STR_LORRY, STR_STATION_VIEW_SCHEDULED_ROAD_VEHICLES_TOOLTIP),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, SVW_SHIPS), SetMinimalSize(14, 12), SetFill(0, 1), SetDataTip(STR_SHIP, STR_STATION_VIEW_SCHEDULED_SHIPS_TOOLTIP),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, SVW_PLANES),  SetMinimalSize(14, 12), SetFill(0, 1), SetDataTip(STR_PLANE, STR_STATION_VIEW_SCHEDULED_AIRCRAFT_TOOLTIP),
		NWidget(WWT_RESIZEBOX, COLOUR_GREY),
	EndContainer(),
};

/**
 * Draws icons of waiting cargo in the StationView window
 *
 * @param i type of cargo
 * @param waiting number of waiting units
 * @param left  left most coordinate to draw on
 * @param right right most coordinate to draw on
 * @param y y coordinate
 * @param width the width of the view
 */
static void DrawCargoIcons(CargoID i, uint waiting, int left, int right, int y)
{
	uint num = min((waiting + 5) / 10, (right - left) / 10); // maximum is width / 10 icons so it won't overflow
	if (num == 0) return;

	SpriteID sprite = CargoSpec::Get(i)->GetCargoIcon();

	int x = _current_text_dir == TD_RTL ? left : right - num * 10;
	do {
		DrawSprite(sprite, PAL_NONE, x, y);
		x += 10;
	} while (--num);
}

CargoDataEntry::CargoDataEntry() :
	parent(NULL),
	station(INVALID_STATION),
	num_children(0),
	count(0),
	children(new CargoDataSet(CargoSorter(ST_CARGO_ID)))
{}

CargoDataEntry::CargoDataEntry(CargoID cargo, uint count, CargoDataEntry *parent) :
	parent(parent),
	cargo(cargo),
	num_children(0),
	count(count),
	children(new CargoDataSet)
{}

CargoDataEntry::CargoDataEntry(StationID station, uint count, CargoDataEntry *parent) :
	parent(parent),
	station(station),
	num_children(0),
	count(count),
	children(new CargoDataSet)
{}

CargoDataEntry::CargoDataEntry(StationID station) :
	parent(NULL),
	station(station),
	num_children(0),
	count(0),
	children(NULL)
{}

CargoDataEntry::CargoDataEntry(CargoID cargo) :
	parent(NULL),
	cargo(cargo),
	num_children(0),
	count(0),
	children(NULL)
{}

CargoDataEntry::~CargoDataEntry()
{
	this->Clear();
	delete this->children;
}

/**
 * Delete all subentries, reset count and num_children and adapt parent's count.
 */
void CargoDataEntry::Clear()
{
	if (this->children != NULL) {
		for (CargoDataSet::iterator i = this->children->begin(); i != this->children->end(); ++i) {
			assert(*i != this);
			delete *i;
		}
		this->children->clear();
	}
	if (this->parent != NULL) this->parent->count -= this->count;
	this->count = 0;
	this->num_children = 0;
}

/**
 * Remove a subentry from this one and delete it.
 * @param child the entry to be removed. This may also be a synthetic entry
 * which only contains the ID of the entry to be removed. In this case child is
 * not deleted.
 */
void CargoDataEntry::Remove(CargoDataEntry *child)
{
	CargoDataSet::iterator i = this->children->find(child);
	if (i != this->children->end()) {
		delete(*i);
		this->children->erase(i);
	}
}

/**
 * Retrieve a subentry or insert it if it doesn't exist, yet.
 * @tparam ID type of ID: either StationID or CargoID
 * @param child_id ID of the child to be inserted or retrieved.
 * @return the new or retrieved subentry
 */
template<class ID>
CargoDataEntry *CargoDataEntry::InsertOrRetrieve(ID child_id)
{
	CargoDataEntry tmp(child_id);
	CargoDataSet::iterator i = this->children->find(&tmp);
	if (i == this->children->end()) {
		IncrementSize();
		return *(this->children->insert(new CargoDataEntry(child_id, 0, this)).first);
	} else {
		CargoDataEntry *ret = *i;
		assert(this->children->value_comp().GetSortType() != ST_COUNT);
		return ret;
	}
}

/**
 * Update the count for this entry and propagate the change to the parent entry
 * if there is one.
 * @param count the amount to be added to this entry
 */
void CargoDataEntry::Update(uint count)
{
	this->count += count;
	if (this->parent != NULL) this->parent->Update(count);
}

/**
 * Increment
 */
void CargoDataEntry::IncrementSize()
{
	 ++this->num_children;
	 if (this->parent != NULL) this->parent->IncrementSize();
}

void CargoDataEntry::Resort(CargoSortType type, SortOrder order)
{
	CargoDataSet *new_subs = new CargoDataSet(this->children->begin(), this->children->end(), CargoSorter(type, order));
	delete this->children;
	this->children = new_subs;
}

CargoDataEntry *CargoDataEntry::Retrieve(CargoDataSet::iterator i) const
{
	if (i == this->children->end()) {
		return NULL;
	} else {
		assert(this->children->value_comp().GetSortType() != ST_COUNT);
		return *i;
	}
}

bool CargoSorter::operator()(const CargoDataEntry *cd1, const CargoDataEntry *cd2) const
{
	switch (this->type) {
	case ST_STATION_ID:
		return this->SortId<StationID>(cd1->GetStation(), cd2->GetStation());
		break;
	case ST_CARGO_ID:
		return this->SortId<CargoID>(cd1->GetCargo(), cd2->GetCargo());
		break;
	case ST_COUNT:
		return this->SortCount(cd1, cd2);
		break;
	case ST_STATION_STRING:
		return this->SortStation(cd1->GetStation(), cd2->GetStation());
		break;
	default:
		NOT_REACHED();
	}
}

template<class ID>
bool CargoSorter::SortId(ID st1, ID st2) const
{
	return (this->order == SO_ASCENDING) ? st1 < st2 : st2 < st1;
}

bool CargoSorter::SortCount(const CargoDataEntry *cd1, const CargoDataEntry *cd2) const
{
	uint c1 = cd1->GetCount();
	uint c2 = cd2->GetCount();
	if (c1 == c2) {
		return this->SortStation(cd1->GetStation(), cd2->GetStation());
	} else if (this->order == SO_ASCENDING) {
		return c1 < c2;
	} else {
		return c2 < c1;
	}
}

bool CargoSorter::SortStation(StationID st1, StationID st2) const
{
	static char buf1[MAX_LENGTH_STATION_NAME_CHARS];
	static char buf2[MAX_LENGTH_STATION_NAME_CHARS];

	if (!Station::IsValidID(st1)) {
		return Station::IsValidID(st2) ? this->order == SO_ASCENDING : this->SortId(st1, st2);
	} else if (!Station::IsValidID(st2)) {
		return order == SO_DESCENDING;
	}

	SetDParam(0, st1);
	GetString(buf1, STR_STATION_NAME, lastof(buf1));
	SetDParam(0, st2);
	GetString(buf2, STR_STATION_NAME, lastof(buf2));

	int res = strcmp(buf1, buf2);
	if (res == 0) {
		return this->SortId(st1, st2);
	} else {
		return (this->order == SO_ASCENDING) ? res < 0 : res > 0;
	}
}

/**
 * The StationView window
 */
struct StationViewWindow : public Window {
	struct RowDisplay {
		RowDisplay(CargoDataEntry *f, StationID n) : filter(f), next_station(n) {}
		RowDisplay(CargoDataEntry *f, CargoID n) : filter(f), next_cargo(n) {}
		CargoDataEntry *filter;
		union {
			StationID next_station;
			CargoID next_cargo;
		};
	};

	typedef std::vector<RowDisplay> CargoDataVector;

	static const int NUM_COLUMNS = 4;

	enum Invalidation {
		INV_FLOWS = 0x100,
		INV_CARGO = 0x200
	};

	enum Grouping {
		GR_SOURCE,
		GR_NEXT,
		GR_DESTINATION,
		GR_CARGO,
	};

	enum Mode {
		MODE_WAITING,
		MODE_PLANNED
	};

	uint expand_shrink_width;     ///< The width allocated to the expand/shrink 'button'
	int rating_lines;             ///< Number of lines in the cargo ratings view.
	int accepts_lines;            ///< Number of lines in the accepted cargo view.
	Scrollbar *vscroll;

	/** Height of the #SVW_ACCEPTLIST widget for different views. */
	enum AcceptListHeight {
		ALH_RATING  = 13, ///< Height of the cargo ratings view.
		ALH_ACCEPTS = 3,  ///< Height of the accepted cargo view.
	};

	static const StringID _sort_names[];
	static const StringID _group_names[];

	CargoSortType sortings[NUM_COLUMNS];
	SortOrder sort_orders[NUM_COLUMNS];

	int scroll_to_row;
	int grouping_index;
	Mode current_mode;
	Grouping groupings[NUM_COLUMNS];

	CargoDataEntry expanded_rows;
	CargoDataEntry cached_destinations;
	CargoDataVector displayed_rows;

	StationViewWindow(const WindowDesc *desc, WindowNumber window_number) : Window(),
		scroll_to_row(INT_MAX), grouping_index(0)
	{
		this->rating_lines  = ALH_RATING;
		this->accepts_lines = ALH_ACCEPTS;

		this->CreateNestedTree(desc);
		this->vscroll = this->GetScrollbar(SVW_SCROLLBAR);
		/* Nested widget tree creation is done in two steps to ensure that this->GetWidget<NWidgetCore>(SVW_ACCEPTS) exists in UpdateWidgetSize(). */
		this->FinishInitNested(desc, window_number);

		this->groupings[0] = GR_CARGO;
		this->sortings[0] = ST_AS_GROUPING;
		this->SelectGroupBy(_settings_client.gui.station_gui_group_order);
		this->SelectSortBy(_settings_client.gui.station_gui_sort_by);
		this->sort_orders[0] = SO_ASCENDING;
		this->SelectSortOrder((SortOrder)_settings_client.gui.station_gui_sort_order);
		Owner owner = Station::Get(window_number)->owner;
		if (owner != OWNER_NONE) this->owner = owner;
	}

	~StationViewWindow()
	{
		Owner owner = Station::Get(this->window_number)->owner;
		if (!Company::IsValidID(owner)) owner = _local_company;
		if (!Company::IsValidID(owner)) return; // Spectators
		DeleteWindowById(WC_TRAINS_LIST,   VehicleListIdentifier(VL_STATION_LIST, VEH_TRAIN,    owner, this->window_number).Pack(), false);
		DeleteWindowById(WC_ROADVEH_LIST,  VehicleListIdentifier(VL_STATION_LIST, VEH_ROAD,     owner, this->window_number).Pack(), false);
		DeleteWindowById(WC_SHIPS_LIST,    VehicleListIdentifier(VL_STATION_LIST, VEH_SHIP,     owner, this->window_number).Pack(), false);
		DeleteWindowById(WC_AIRCRAFT_LIST, VehicleListIdentifier(VL_STATION_LIST, VEH_AIRCRAFT, owner, this->window_number).Pack(), false);
	}

	void ShowCargo(CargoDataEntry *data, CargoID cargo, StationID source, StationID next, StationID dest, uint count)
	{
		if (count == 0) return;
		const CargoDataEntry *expand = &this->expanded_rows;
		for (int i = 0; i < NUM_COLUMNS && expand != NULL; ++i) {
			switch (groupings[i]) {
			case GR_CARGO:
				assert(i == 0);
				data = data->InsertOrRetrieve(cargo);
				expand = expand->Retrieve(cargo);
				break;
			case GR_SOURCE:
				data = data->InsertOrRetrieve(source);
				expand = expand->Retrieve(source);
				break;
			case GR_NEXT:
				data = data->InsertOrRetrieve(next);
				expand = expand->Retrieve(next);
				break;
			case GR_DESTINATION:
				data = data->InsertOrRetrieve(dest);
				expand = expand->Retrieve(dest);
				break;
			}
		}
		data->Update(count);
	}

	virtual void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize)
	{
		switch (widget) {
			case SVW_WAITING:
				resize->height = FONT_HEIGHT_NORMAL;
				size->height = WD_FRAMERECT_TOP + 4 * resize->height + WD_FRAMERECT_BOTTOM;
				this->expand_shrink_width = max(GetStringBoundingBox("-").width, GetStringBoundingBox("+").width) + WD_FRAMERECT_LEFT + WD_FRAMERECT_RIGHT;
				break;

			case SVW_ACCEPTLIST:
				size->height = WD_FRAMERECT_TOP + ((this->GetWidget<NWidgetCore>(SVW_ACCEPTS)->widget_data == STR_STATION_VIEW_RATINGS_BUTTON) ? this->accepts_lines : this->rating_lines) * FONT_HEIGHT_NORMAL + WD_FRAMERECT_BOTTOM;
				break;
		}
	}

	virtual void OnPaint()
	{
		const Station *st = Station::Get(this->window_number);
		CargoDataEntry cargo;
		BuildCargoList(&cargo, st);

		this->vscroll->SetCount(cargo.GetNumChildren()); // update scrollbar

		/* disable some buttons */
		this->SetWidgetDisabledState(SVW_RENAME,   st->owner != _local_company);
		this->SetWidgetDisabledState(SVW_TRAINS,   !(st->facilities & FACIL_TRAIN));
		this->SetWidgetDisabledState(SVW_ROADVEHS, !(st->facilities & FACIL_TRUCK_STOP) && !(st->facilities & FACIL_BUS_STOP));
		this->SetWidgetDisabledState(SVW_SHIPS,    !(st->facilities & FACIL_DOCK));
		this->SetWidgetDisabledState(SVW_PLANES,   !(st->facilities & FACIL_AIRPORT));

		SetDParam(0, st->index);
		SetDParam(1, st->facilities);
		this->DrawWidgets();

		if (!this->IsShaded()) {
			/* Draw 'accepted cargo' or 'cargo ratings'. */
			const NWidgetBase *wid = this->GetWidget<NWidgetBase>(SVW_ACCEPTLIST);
			const Rect r = {wid->pos_x, wid->pos_y, wid->pos_x + wid->current_x - 1, wid->pos_y + wid->current_y - 1};
			if (this->GetWidget<NWidgetCore>(SVW_ACCEPTS)->widget_data == STR_STATION_VIEW_RATINGS_BUTTON) {
				int lines = this->DrawAcceptedCargo(r);
				if (lines > this->accepts_lines) { // Resize the widget, and perform re-initialization of the window.
					this->accepts_lines = lines;
					this->ReInit();
					return;
				}
			} else {
				int lines = this->DrawCargoRatings(r);
				if (lines > this->rating_lines) { // Resize the widget, and perform re-initialization of the window.
					this->rating_lines = lines;
					this->ReInit();
					return;
				}
			}

			/* draw arrow pointing up/down for ascending/descending sorting */
			this->DrawSortButtonState(SVW_SORT_ORDER, sort_orders[1] == SO_ASCENDING ? SBS_UP : SBS_DOWN);

			int pos = this->vscroll->GetPosition();

			int maxrows = this->vscroll->GetCapacity();

			displayed_rows.clear();

			/* Draw waiting cargo. */
			NWidgetBase *nwi = this->GetWidget<NWidgetBase>(SVW_WAITING);
			Rect waiting_rect = {nwi->pos_x, nwi->pos_y, nwi->pos_x + nwi->current_x - 1, nwi->pos_y + nwi->current_y - 1};
			this->DrawEntries(&cargo, waiting_rect, pos, maxrows, 0);
			scroll_to_row = INT_MAX;
		}
	}

	virtual void SetStringParameters(int widget) const
	{
		if (widget == SVW_CAPTION) {
			const Station *st = Station::Get(this->window_number);
			SetDParam(0, st->index);
			SetDParam(1, st->facilities);
		}
	}

	void RecalcDestinations(CargoID i)
	{
		const Station *st = Station::Get(this->window_number);
		CargoDataEntry *cargo_entry = cached_destinations.InsertOrRetrieve(i);
		cargo_entry->Clear();

		const FlowStatMap &flows = st->goods[i].flows;
		for (FlowStatMap::const_iterator it = flows.begin(); it != flows.end(); ++it) {
			StationID from = it->first;
			CargoDataEntry *source_entry = cargo_entry->InsertOrRetrieve(from);
			const FlowStatSet &flow_set = it->second;
			for (FlowStatSet::const_iterator flow_it = flow_set.begin(); flow_it != flow_set.end(); ++flow_it) {
				const FlowStat &stat = *flow_it;
				StationID via = stat.Via();
				CargoDataEntry *via_entry = source_entry->InsertOrRetrieve(via);
				if (via == this->window_number) {
					via_entry->InsertOrRetrieve(via)->Update(stat.Planned());
				} else {
					EstimateDestinations(i, from, via, stat.Planned(), via_entry);
				}
			}
		}
	}

	void EstimateDestinations(CargoID cargo, StationID source, StationID next, uint count, CargoDataEntry *dest)
	{
		if (Station::IsValidID(next) && Station::IsValidID(source)) {
			CargoDataEntry tmp;
			const FlowStatMap &flowmap = Station::Get(next)->goods[cargo].flows;
			FlowStatMap::const_iterator map_it = flowmap.find(source);
			if (map_it != flowmap.end()) {
				const FlowStatSet &flows = map_it->second;
				for (FlowStatSet::const_iterator i = flows.begin(); i != flows.end(); ++i) {
					tmp.InsertOrRetrieve(i->Via())->Update(i->Planned());
				}
			}

			if (tmp.GetCount() == 0) {
				dest->InsertOrRetrieve(INVALID_STATION)->Update(count);
			} else {
				uint sum_estimated = 0;
				while(sum_estimated < count) {
					for(CargoDataSet::iterator i = tmp.Begin(); i != tmp.End() && sum_estimated < count; ++i) {
						CargoDataEntry *child = *i;
						uint estimate = DivideApprox(child->GetCount() * count, tmp.GetCount());
						if (estimate == 0) estimate = 1;

						sum_estimated += estimate;
						if (sum_estimated > count) {
							estimate -= sum_estimated - count;
							sum_estimated = count;
						}

						if (estimate > 0) {
							if (child->GetStation() == next) {
								dest->InsertOrRetrieve(next)->Update(estimate);
							} else {
								EstimateDestinations(cargo, source, child->GetStation(), estimate, dest);
							}
						}
					}

				}
			}
		} else {
			dest->InsertOrRetrieve(INVALID_STATION)->Update(count);
		}
	}

	void BuildFlowList(CargoID i, const FlowStatMap &flows, CargoDataEntry *cargo)
	{
		const CargoDataEntry *source_dest = this->cached_destinations.Retrieve(i);
		for (FlowStatMap::const_iterator it = flows.begin(); it != flows.end(); ++it) {
			StationID from = it->first;
			const CargoDataEntry *source_entry = source_dest->Retrieve(from);
			const FlowStatSet &flow_set = it->second;
			for (FlowStatSet::const_iterator flow_it = flow_set.begin(); flow_it != flow_set.end(); ++flow_it) {
				const FlowStat &stat = *flow_it;
				const CargoDataEntry *via_entry = source_entry->Retrieve(stat.Via());
				for (CargoDataSet::iterator dest_it = via_entry->Begin(); dest_it != via_entry->End(); ++dest_it) {
					CargoDataEntry *dest_entry = *dest_it;
					uint val = dest_entry->GetCount();
					ShowCargo(cargo, i, from, stat.Via(), dest_entry->GetStation(), val);
				}
			}
		}
	}

	void BuildCargoList(CargoID i, const StationCargoList &packets, CargoDataEntry *cargo)
	{
		const CargoDataEntry *source_dest = this->cached_destinations.Retrieve(i);
		for (StationCargoList::ConstIterator it = packets.Packets()->begin(); it != packets.Packets()->end(); it++) {
			const CargoPacket *cp = *it;
			StationID next = it.GetKey();

			const CargoDataEntry *source_entry = source_dest->Retrieve(cp->SourceStation());
			if (source_entry == NULL) {
				ShowCargo(cargo, i, cp->SourceStation(), next, INVALID_STATION, cp->Count());
				continue;
			}

			const CargoDataEntry *via_entry = source_entry->Retrieve(next);
			if (via_entry == NULL) {
				ShowCargo(cargo, i, cp->SourceStation(), next, INVALID_STATION, cp->Count());
				continue;
			}

			for (CargoDataSet::iterator dest_it = via_entry->Begin(); dest_it != via_entry->End(); ++dest_it) {
				CargoDataEntry *dest_entry = *dest_it;
				uint val = DivideApprox(cp->Count() * dest_entry->GetCount(), via_entry->GetCount());
				ShowCargo(cargo, i, cp->SourceStation(), next, dest_entry->GetStation(), val);
			}
		}
	}

	void BuildCargoList(CargoDataEntry *cargo, const Station *st)
	{
		for (CargoID i = 0; i < NUM_CARGO; i++) {

			if (this->cached_destinations.Retrieve(i) == NULL) {
				this->RecalcDestinations(i);
			}

			if (this->current_mode == MODE_WAITING) {
				BuildCargoList(i, st->goods[i].cargo, cargo);
			} else {
				BuildFlowList(i, st->goods[i].flows, cargo);
			}
		}
	}

	void SetDisplayedRow(const CargoDataEntry *data)
	{
		std::list<StationID> stations;
		const CargoDataEntry *parent = data->GetParent();
		if (parent->GetParent() == NULL) {
			this->displayed_rows.push_back(RowDisplay(&this->expanded_rows, data->GetCargo()));
			return;
		}

		StationID next = data->GetStation();
		while(parent->GetParent()->GetParent() != NULL) {
			stations.push_back(parent->GetStation());
			parent = parent->GetParent();
		}

		CargoID cargo = parent->GetCargo();
		CargoDataEntry *filter = this->expanded_rows.Retrieve(cargo);
		while(!stations.empty()) {
			filter = filter->Retrieve(stations.back());
			stations.pop_back();
		}

		this->displayed_rows.push_back(RowDisplay(filter, next));
	}

	StringID GetEntryString(StationID station, StringID here, StringID other_station, StringID any)
	{
		if (station == this->window_number) {
			return here;
		} else if (station != INVALID_STATION) {
			SetDParam(2, station);
			return other_station;
		} else {
			return any;
		}
	}

	StringID SearchNonStop(CargoDataEntry *cd, StationID station, int column)
	{
		CargoDataEntry *parent = cd->GetParent();
		for (int i = column - 1; i > 0; --i) {
			if (this->groupings[i] == GR_DESTINATION) {
				if (parent->GetStation() == station) {
					return STR_STATION_VIEW_NONSTOP;
				} else {
					return STR_STATION_VIEW_VIA;
				}
			}
			parent = parent->GetParent();
		}

		if (this->groupings[column + 1] == GR_DESTINATION) {
			CargoDataSet::iterator begin = cd->Begin();
			CargoDataSet::iterator end = cd->End();
			if (begin != end && ++(cd->Begin()) == end && (*(begin))->GetStation() == station) {
				return STR_STATION_VIEW_NONSTOP;
			} else {
				return STR_STATION_VIEW_VIA;
			}
		}

		return STR_STATION_VIEW_VIA;
	}

	int DrawEntries(CargoDataEntry *entry, Rect &r, int pos, int maxrows, int column, CargoID cargo = CT_INVALID)
	{
		if (this->sortings[column] == ST_AS_GROUPING) {
			if (this->groupings[column] != GR_CARGO) {
				entry->Resort(ST_STATION_STRING, this->sort_orders[column]);
			}
		} else {
			entry->Resort(ST_COUNT, this->sort_orders[column]);
		}
		for (CargoDataSet::iterator i = entry->Begin(); i != entry->End(); ++i) {
			CargoDataEntry *cd = *i;

			if (this->groupings[column] == GR_CARGO) cargo = cd->GetCargo();

			if (pos > -maxrows && pos <= 0) {
				StringID str = STR_EMPTY;
				int y = r.top + WD_FRAMERECT_TOP - pos * FONT_HEIGHT_NORMAL;
				SetDParam(0, cargo);
				SetDParam(1, cd->GetCount());

				if (this->groupings[column] == GR_CARGO) {
					str = STR_STATION_VIEW_WAITING_CARGO;
					DrawCargoIcons(cd->GetCargo(), cd->GetCount(), r.left + WD_FRAMERECT_LEFT + this->expand_shrink_width, r.right - WD_FRAMERECT_RIGHT - this->expand_shrink_width, y);
				} else {
					StationID station = cd->GetStation();

					switch(this->groupings[column]) {
					case GR_SOURCE:
						str = GetEntryString(station, STR_STATION_VIEW_FROM_HERE, STR_STATION_VIEW_FROM, STR_STATION_VIEW_FROM_ANY);
						break;
					case GR_NEXT:
						str = GetEntryString(station, STR_STATION_VIEW_VIA_HERE, STR_STATION_VIEW_VIA, STR_STATION_VIEW_VIA_ANY);
						if (str == STR_STATION_VIEW_VIA) str = SearchNonStop(cd, station, column);
						break;
					case GR_DESTINATION:
						str = GetEntryString(station, STR_STATION_VIEW_TO_HERE, STR_STATION_VIEW_TO, STR_STATION_VIEW_TO_ANY);
						break;
					default:
						NOT_REACHED();
					}
					if (pos == -this->scroll_to_row && Station::IsValidID(station)) {
						ScrollMainWindowToTile(Station::Get(station)->xy);
					}
				}

				bool rtl = _current_text_dir == TD_RTL;
				int text_left    = rtl ? r.left + this->expand_shrink_width : r.left + WD_FRAMERECT_LEFT + column * this->expand_shrink_width;
				int text_right   = rtl ? r.right - WD_FRAMERECT_LEFT - column * this->expand_shrink_width : r.right - this->expand_shrink_width;
				int shrink_left  = rtl ? r.left + WD_FRAMERECT_LEFT : r.right - this->expand_shrink_width + WD_FRAMERECT_LEFT;
				int shrink_right = rtl ? r.left + this->expand_shrink_width - WD_FRAMERECT_RIGHT : r.right - WD_FRAMERECT_RIGHT;

				DrawString(text_left, text_right, y, str, TC_FROMSTRING);

				if (column < NUM_COLUMNS - 1) {
					const char *sym = cd->GetNumChildren() > 0 ? "-" : "+";
					DrawString(shrink_left, shrink_right, y, sym, TC_YELLOW);
				}
				SetDisplayedRow(cd);
			}
			pos = DrawEntries(cd, r, --pos, maxrows, column + 1, cargo);
		}
		return pos;
	}

	virtual void OnInvalidateData(int cargo)
	{
		this->cached_destinations.Remove((CargoID)cargo);
		this->SetDirty();
	}

	/**
	 * Draw accepted cargo in the #SVW_ACCEPTLIST widget.
	 * @param r Rectangle of the widget.
	 * @return Number of lines needed for drawing the accepted cargo.
	 */
	int DrawAcceptedCargo(const Rect &r) const
	{
		const Station *st = Station::Get(this->window_number);

		uint32 cargo_mask = 0;
		for (CargoID i = 0; i < NUM_CARGO; i++) {
			if (HasBit(st->goods[i].acceptance_pickup, GoodsEntry::ACCEPTANCE)) SetBit(cargo_mask, i);
		}
		Rect s = {r.left + WD_FRAMERECT_LEFT, r.top + WD_FRAMERECT_TOP, r.right - WD_FRAMERECT_RIGHT, INT32_MAX};
		int bottom = DrawCargoListText(cargo_mask, s, STR_STATION_VIEW_ACCEPTS_CARGO);
		return CeilDiv(bottom - r.top - WD_FRAMERECT_TOP, FONT_HEIGHT_NORMAL);
	}

	/**
	 * Draw cargo ratings in the #SVW_ACCEPTLIST widget.
	 * @param r Rectangle of the widget.
	 * @return Number of lines needed for drawing the cargo ratings.
	 */
	int DrawCargoRatings(const Rect &r) const
	{
		const Station *st = Station::Get(this->window_number);
		int y = r.top + WD_FRAMERECT_TOP;

		DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, STR_STATION_VIEW_CARGO_RATINGS_TITLE);
		y += FONT_HEIGHT_NORMAL;

		const CargoSpec *cs;
		FOR_ALL_SORTED_STANDARD_CARGOSPECS(cs) {
			const GoodsEntry *ge = &st->goods[cs->Index()];
			if (!HasBit(ge->acceptance_pickup, GoodsEntry::PICKUP)) continue;

			SetDParam(0, cs->name);
			SetDParam(1, ge->supply);
			SetDParam(3, ToPercent8(ge->rating));
			SetDParam(2, STR_CARGO_RATING_APPALLING + (ge->rating >> 5));
			DrawString(r.left + WD_FRAMERECT_LEFT + 6, r.right - WD_FRAMERECT_RIGHT - 6, y, STR_STATION_VIEW_CARGO_SUPPLY_RATING);
			y += FONT_HEIGHT_NORMAL;
		}
		return CeilDiv(y - r.top - WD_FRAMERECT_TOP, FONT_HEIGHT_NORMAL);
	}

	template<class ID>
	void HandleCargoWaitingClick(CargoDataEntry *filter, ID next)
	{
		if (filter->Retrieve(next) != NULL) {
			filter->Remove(next);
		} else {
			filter->InsertOrRetrieve(next);
		}
	}

	void HandleCargoWaitingClick(int row)
	{
		if (row < 0 || (uint)row >= this->displayed_rows.size()) return;
		if (_ctrl_pressed) {
			this->scroll_to_row = row;
		} else {
			RowDisplay &display = this->displayed_rows[row];
			if (display.filter == &this->expanded_rows) {
				HandleCargoWaitingClick<CargoID>(display.filter, display.next_cargo);
			} else {
				HandleCargoWaitingClick<StationID>(display.filter, display.next_station);
			}
		}
		this->SetWidgetDirty(SVW_WAITING);
	}

	virtual void OnClick(Point pt, int widget, int click_count)
	{
		switch (widget) {
			case SVW_WAITING:
				this->HandleCargoWaitingClick(this->vscroll->GetScrolledRowFromWidget(pt.y, this, SVW_WAITING, WD_FRAMERECT_TOP, FONT_HEIGHT_NORMAL) - this->vscroll->GetPosition());
				break;

			case SVW_LOCATION:
				if (_ctrl_pressed) {
					ShowExtraViewPortWindow(Station::Get(this->window_number)->xy);
				} else {
					ScrollMainWindowToTile(Station::Get(this->window_number)->xy);
				}
				break;

			case SVW_RATINGS: {
				/* Swap between 'accepts' and 'ratings' view. */
				int height_change;
				NWidgetCore *nwi = this->GetWidget<NWidgetCore>(SVW_RATINGS);
				if (this->GetWidget<NWidgetCore>(SVW_RATINGS)->widget_data == STR_STATION_VIEW_RATINGS_BUTTON) {
					nwi->SetDataTip(STR_STATION_VIEW_ACCEPTS_BUTTON, STR_STATION_VIEW_ACCEPTS_TOOLTIP); // Switch to accepts view.
					height_change = this->rating_lines - this->accepts_lines;
				} else {
					nwi->SetDataTip(STR_STATION_VIEW_RATINGS_BUTTON, STR_STATION_VIEW_RATINGS_TOOLTIP); // Switch to ratings view.
					height_change = this->accepts_lines - this->rating_lines;
				}
				this->ReInit(0, height_change * FONT_HEIGHT_NORMAL);
				break;
			}

			case SVW_RENAME:
				SetDParam(0, this->window_number);
				ShowQueryString(STR_STATION_NAME, STR_STATION_VIEW_RENAME_STATION_CAPTION, MAX_LENGTH_STATION_NAME_CHARS, MAX_LENGTH_STATION_NAME_PIXELS,
						this, CS_ALPHANUMERAL, QSF_ENABLE_DEFAULT | QSF_LEN_IN_CHARS);
				break;

			case SVW_TRAINS:   // Show list of scheduled trains to this station
			case SVW_ROADVEHS: // Show list of scheduled road-vehicles to this station
			case SVW_SHIPS:    // Show list of scheduled ships to this station
			case SVW_PLANES:   // Show list of scheduled aircraft to this station
				ShowVehicleListWindow(this->owner, (VehicleType)(widget - SVW_TRAINS), (StationID)this->window_number);
				break;

			case SVW_SORT_BY: {
				ShowDropDownMenu(this, _sort_names, this->current_mode, SVW_SORT_BY, 0, 0);
				break;
			}

			case SVW_GROUP_BY: {
				ShowDropDownMenu(this, _group_names, this->grouping_index, SVW_GROUP_BY, 0, 0);
				break;
			}

			case SVW_SORT_ORDER: { // flip sorting method asc/desc
				this->SelectSortOrder(this->sort_orders[1] == SO_ASCENDING ? SO_DESCENDING : SO_ASCENDING);
				this->flags4 |= WF_TIMEOUT_BEGIN;
				this->LowerWidget(SVW_SORT_ORDER);
				break;
			}
		}
	}

	void SelectSortOrder(SortOrder order)
	{
		this->sort_orders[1] = this->sort_orders[2] = this->sort_orders[3] = order;
		_settings_client.gui.station_gui_sort_order = this->sort_orders[1];
		this->SetDirty();
	}

	void SelectSortBy(int index)
	{
		_settings_client.gui.station_gui_sort_by = index;
		switch(_sort_names[index]) {
		case STR_STATION_VIEW_WAITING_STATION:
			this->current_mode = MODE_WAITING;
			this->sortings[1] = this->sortings[2] = this->sortings[3] = ST_AS_GROUPING;
			break;
		case STR_STATION_VIEW_WAITING_AMOUNT:
			this->current_mode = MODE_WAITING;
			this->sortings[1] = this->sortings[2] = this->sortings[3] = ST_COUNT;
			break;
		case STR_STATION_VIEW_PLANNED_STATION:
			this->current_mode = MODE_PLANNED;
			this->sortings[1] = this->sortings[2] = this->sortings[3] = ST_AS_GROUPING;
			break;
		case STR_STATION_VIEW_PLANNED_AMOUNT:
			this->current_mode = MODE_PLANNED;
			this->sortings[1] = this->sortings[2] = this->sortings[3] = ST_COUNT;
			break;
		default:
			NOT_REACHED();
		}
		/* Display the current sort variant */
		this->GetWidget<NWidgetCore>(SVW_SORT_BY)->widget_data = _sort_names[index];
		this->SetDirty();
	}

	void SelectGroupBy(int index)
	{
		this->grouping_index = index;
		_settings_client.gui.station_gui_group_order = index;
		this->GetWidget<NWidgetCore>(SVW_GROUP_BY)->widget_data = _group_names[index];
		switch(_group_names[index]) {
		case STR_STATION_VIEW_GROUP_S_V_D:
			this->groupings[1] = GR_SOURCE;
			this->groupings[2] = GR_NEXT;
			this->groupings[3] = GR_DESTINATION;
			break;
		case STR_STATION_VIEW_GROUP_S_D_V:
			this->groupings[1] = GR_SOURCE;
			this->groupings[2] = GR_DESTINATION;
			this->groupings[3] = GR_NEXT;
			break;
		case STR_STATION_VIEW_GROUP_V_S_D:
			this->groupings[1] = GR_NEXT;
			this->groupings[2] = GR_SOURCE;
			this->groupings[3] = GR_DESTINATION;
			break;
		case STR_STATION_VIEW_GROUP_V_D_S:
			this->groupings[1] = GR_NEXT;
			this->groupings[2] = GR_DESTINATION;
			this->groupings[3] = GR_SOURCE;
			break;
		case STR_STATION_VIEW_GROUP_D_S_V:
			this->groupings[1] = GR_DESTINATION;
			this->groupings[2] = GR_SOURCE;
			this->groupings[3] = GR_NEXT;
			break;
		case STR_STATION_VIEW_GROUP_D_V_S:
			this->groupings[1] = GR_DESTINATION;
			this->groupings[2] = GR_NEXT;
			this->groupings[3] = GR_SOURCE;
			break;
		}
		this->SetDirty();
	}

	virtual void OnDropdownSelect(int widget, int index)
	{
		if (widget == SVW_SORT_BY) {
			this->SelectSortBy(index);
		} else {
			this->SelectGroupBy(index);
		}
	}

	virtual void OnQueryTextFinished(char *str)
	{
		if (str == NULL) return;

		DoCommandP(0, this->window_number, 0, CMD_RENAME_STATION | CMD_MSG(STR_ERROR_CAN_T_RENAME_STATION), NULL, str);
	}

	virtual void OnResize()
	{
		this->vscroll->SetCapacityFromWidget(this, SVW_WAITING, WD_FRAMERECT_TOP + WD_FRAMERECT_BOTTOM);
	}
};

const StringID StationViewWindow::_sort_names[] = {
	STR_STATION_VIEW_WAITING_STATION,
	STR_STATION_VIEW_WAITING_AMOUNT,
	STR_STATION_VIEW_PLANNED_STATION,
	STR_STATION_VIEW_PLANNED_AMOUNT,
	INVALID_STRING_ID
};

const StringID StationViewWindow::_group_names[] = {
	STR_STATION_VIEW_GROUP_S_V_D,
	STR_STATION_VIEW_GROUP_S_D_V,
	STR_STATION_VIEW_GROUP_V_S_D,
	STR_STATION_VIEW_GROUP_V_D_S,
	STR_STATION_VIEW_GROUP_D_S_V,
	STR_STATION_VIEW_GROUP_D_V_S,
	INVALID_STRING_ID
};

static const WindowDesc _station_view_desc(
	WDP_AUTO, 249, 117,
	WC_STATION_VIEW, WC_NONE,
	WDF_UNCLICK_BUTTONS,
	_nested_station_view_widgets, lengthof(_nested_station_view_widgets)
);

/**
 * Opens StationViewWindow for given station
 *
 * @param station station which window should be opened
 */
void ShowStationViewWindow(StationID station)
{
	AllocateWindowDescFront<StationViewWindow>(&_station_view_desc, station);
}

/** Struct containing TileIndex and StationID */
struct TileAndStation {
	TileIndex tile;    ///< TileIndex
	StationID station; ///< StationID
};

static SmallVector<TileAndStation, 8> _deleted_stations_nearby;
static SmallVector<StationID, 8> _stations_nearby_list;

/**
 * Add station on this tile to _stations_nearby_list if it's fully within the
 * station spread.
 * @param tile Tile just being checked
 * @param user_data Pointer to TileArea context
 * @tparam T the type of station to look for
 */
template <class T>
static bool AddNearbyStation(TileIndex tile, void *user_data)
{
	TileArea *ctx = (TileArea *)user_data;

	/* First check if there were deleted stations here */
	for (uint i = 0; i < _deleted_stations_nearby.Length(); i++) {
		TileAndStation *ts = _deleted_stations_nearby.Get(i);
		if (ts->tile == tile) {
			*_stations_nearby_list.Append() = _deleted_stations_nearby[i].station;
			_deleted_stations_nearby.Erase(ts);
			i--;
		}
	}

	/* Check if own station and if we stay within station spread */
	if (!IsTileType(tile, MP_STATION)) return false;

	StationID sid = GetStationIndex(tile);

	/* This station is (likely) a waypoint */
	if (!T::IsValidID(sid)) return false;

	T *st = T::Get(sid);
	if (st->owner != _local_company || _stations_nearby_list.Contains(sid)) return false;

	if (st->rect.BeforeAddRect(ctx->tile, ctx->w, ctx->h, StationRect::ADD_TEST).Succeeded()) {
		*_stations_nearby_list.Append() = sid;
	}

	return false; // We want to include *all* nearby stations
}

/**
 * Circulate around the to-be-built station to find stations we could join.
 * Make sure that only stations are returned where joining wouldn't exceed
 * station spread and are our own station.
 * @param ta Base tile area of the to-be-built station
 * @param distant_join Search for adjacent stations (false) or stations fully
 *                     within station spread
 * @tparam T the type of station to look for
 */
template <class T>
static const T *FindStationsNearby(TileArea ta, bool distant_join)
{
	TileArea ctx = ta;

	_stations_nearby_list.Clear();
	_deleted_stations_nearby.Clear();

	/* Check the inside, to return, if we sit on another station */
	TILE_AREA_LOOP(t, ta) {
		if (t < MapSize() && IsTileType(t, MP_STATION) && T::IsValidID(GetStationIndex(t))) return T::GetByTile(t);
	}

	/* Look for deleted stations */
	const BaseStation *st;
	FOR_ALL_BASE_STATIONS(st) {
		if (T::IsExpected(st) && !st->IsInUse() && st->owner == _local_company) {
			/* Include only within station spread (yes, it is strictly less than) */
			if (max(DistanceMax(ta.tile, st->xy), DistanceMax(TILE_ADDXY(ta.tile, ta.w - 1, ta.h - 1), st->xy)) < _settings_game.station.station_spread) {
				TileAndStation *ts = _deleted_stations_nearby.Append();
				ts->tile = st->xy;
				ts->station = st->index;

				/* Add the station when it's within where we're going to build */
				if (IsInsideBS(TileX(st->xy), TileX(ctx.tile), ctx.w) &&
						IsInsideBS(TileY(st->xy), TileY(ctx.tile), ctx.h)) {
					AddNearbyStation<T>(st->xy, &ctx);
				}
			}
		}
	}

	/* Only search tiles where we have a chance to stay within the station spread.
	 * The complete check needs to be done in the callback as we don't know the
	 * extent of the found station, yet. */
	if (distant_join && min(ta.w, ta.h) >= _settings_game.station.station_spread) return NULL;
	uint max_dist = distant_join ? _settings_game.station.station_spread - min(ta.w, ta.h) : 1;

	TileIndex tile = TILE_ADD(ctx.tile, TileOffsByDir(DIR_N));
	CircularTileSearch(&tile, max_dist, ta.w, ta.h, AddNearbyStation<T>, &ctx);

	return NULL;
}

enum JoinStationWidgets {
	JSW_WIDGET_CAPTION,
	JSW_PANEL,
	JSW_SCROLLBAR,
};

static const NWidgetPart _nested_select_station_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN, JSW_WIDGET_CAPTION), SetDataTip(STR_JOIN_STATION_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, COLOUR_DARK_GREEN, JSW_PANEL), SetResize(1, 0), SetScrollbar(JSW_SCROLLBAR), EndContainer(),
		NWidget(NWID_VERTICAL),
			NWidget(NWID_VSCROLLBAR, COLOUR_DARK_GREEN, JSW_SCROLLBAR),
			NWidget(WWT_RESIZEBOX, COLOUR_DARK_GREEN),
		EndContainer(),
	EndContainer(),
};

/**
 * Window for selecting stations/waypoints to (distant) join to.
 * @tparam T The type of station to join with
 */
template <class T>
struct SelectStationWindow : Window {
	CommandContainer select_station_cmd; ///< Command to build new station
	TileArea area; ///< Location of new station
	Scrollbar *vscroll;

	SelectStationWindow(const WindowDesc *desc, CommandContainer cmd, TileArea ta) :
		Window(),
		select_station_cmd(cmd),
		area(ta)
	{
		this->CreateNestedTree(desc);
		this->vscroll = this->GetScrollbar(JSW_SCROLLBAR);
		this->GetWidget<NWidgetCore>(JSW_WIDGET_CAPTION)->widget_data = T::EXPECTED_FACIL == FACIL_WAYPOINT ? STR_JOIN_WAYPOINT_CAPTION : STR_JOIN_STATION_CAPTION;
		this->FinishInitNested(desc, 0);
		this->OnInvalidateData(0);
	}

	virtual void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize)
	{
		if (widget != JSW_PANEL) return;

		/* Determine the widest string */
		Dimension d = GetStringBoundingBox(T::EXPECTED_FACIL == FACIL_WAYPOINT ? STR_JOIN_WAYPOINT_CREATE_SPLITTED_WAYPOINT : STR_JOIN_STATION_CREATE_SPLITTED_STATION);
		for (uint i = 0; i < _stations_nearby_list.Length(); i++) {
			const T *st = T::Get(_stations_nearby_list[i]);
			SetDParam(0, st->index);
			SetDParam(1, st->facilities);
			d = maxdim(d, GetStringBoundingBox(T::EXPECTED_FACIL == FACIL_WAYPOINT ? STR_STATION_LIST_WAYPOINT : STR_STATION_LIST_STATION));
		}

		resize->height = d.height;
		d.height *= 5;
		d.width += WD_FRAMERECT_RIGHT + WD_FRAMERECT_LEFT;
		d.height += WD_FRAMERECT_TOP + WD_FRAMERECT_BOTTOM;
		*size = d;
	}

	virtual void DrawWidget(const Rect &r, int widget) const
	{
		if (widget != JSW_PANEL) return;

		uint y = r.top + WD_FRAMERECT_TOP;
		if (this->vscroll->GetPosition() == 0) {
			DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, T::EXPECTED_FACIL == FACIL_WAYPOINT ? STR_JOIN_WAYPOINT_CREATE_SPLITTED_WAYPOINT : STR_JOIN_STATION_CREATE_SPLITTED_STATION);
			y += this->resize.step_height;
		}

		for (uint i = max<uint>(1, this->vscroll->GetPosition()); i <= _stations_nearby_list.Length(); ++i, y += this->resize.step_height) {
			/* Don't draw anything if it extends past the end of the window. */
			if (i - this->vscroll->GetPosition() >= this->vscroll->GetCapacity()) break;

			const T *st = T::Get(_stations_nearby_list[i - 1]);
			SetDParam(0, st->index);
			SetDParam(1, st->facilities);
			DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, T::EXPECTED_FACIL == FACIL_WAYPOINT ? STR_STATION_LIST_WAYPOINT : STR_STATION_LIST_STATION);
		}
	}

	virtual void OnClick(Point pt, int widget, int click_count)
	{
		if (widget != JSW_PANEL) return;

		uint st_index = this->vscroll->GetScrolledRowFromWidget(pt.y, this, JSW_PANEL, WD_FRAMERECT_TOP);
		bool distant_join = (st_index > 0);
		if (distant_join) st_index--;

		if (distant_join && st_index >= _stations_nearby_list.Length()) return;

		/* Insert station to be joined into stored command */
		SB(this->select_station_cmd.p2, 16, 16,
		   (distant_join ? _stations_nearby_list[st_index] : NEW_STATION));

		/* Execute stored Command */
		DoCommandP(&this->select_station_cmd);

		/* Close Window; this might cause double frees! */
		DeleteWindowById(WC_SELECT_STATION, 0);
	}

	virtual void OnTick()
	{
		if (_thd.dirty & 2) {
			_thd.dirty &= ~2;
			this->SetDirty();
		}
	}

	virtual void OnResize()
	{
		this->vscroll->SetCapacityFromWidget(this, JSW_PANEL, WD_FRAMERECT_TOP + WD_FRAMERECT_BOTTOM);
	}

	virtual void OnInvalidateData(int data)
	{
		FindStationsNearby<T>(this->area, true);
		this->vscroll->SetCount(_stations_nearby_list.Length() + 1);
		this->SetDirty();
	}
};

static const WindowDesc _select_station_desc(
	WDP_AUTO, 200, 180,
	WC_SELECT_STATION, WC_NONE,
	WDF_CONSTRUCTION,
	_nested_select_station_widgets, lengthof(_nested_select_station_widgets)
);


/**
 * Check whether we need to show the station selection window.
 * @param cmd Command to build the station.
 * @param ta Tile area of the to-be-built station
 * @tparam T the type of station
 * @return whether we need to show the station selection window.
 */
template <class T>
static bool StationJoinerNeeded(CommandContainer cmd, TileArea ta)
{
	/* Only show selection if distant join is enabled in the settings */
	if (!_settings_game.station.distant_join_stations) return false;

	/* If a window is already opened and we didn't ctrl-click,
	 * return true (i.e. just flash the old window) */
	Window *selection_window = FindWindowById(WC_SELECT_STATION, 0);
	if (selection_window != NULL) {
		/* Abort current distant-join and start new one */
		delete selection_window;
		UpdateTileSelection();
	}

	/* only show the popup, if we press ctrl */
	if (!_ctrl_pressed) return false;

	/* Now check if we could build there */
	if (DoCommand(&cmd, CommandFlagsToDCFlags(GetCommandFlags(cmd.cmd))).Failed()) return false;

	/* Test for adjacent station or station below selection.
	 * If adjacent-stations is disabled and we are building next to a station, do not show the selection window.
	 * but join the other station immediately. */
	const T *st = FindStationsNearby<T>(ta, false);
	return st == NULL && (_settings_game.station.adjacent_stations || _stations_nearby_list.Length() == 0);
}

/**
 * Show the station selection window when needed. If not, build the station.
 * @param cmd Command to build the station.
 * @param ta Area to build the station in
 * @tparam the class to find stations for
 */
template <class T>
void ShowSelectBaseStationIfNeeded(CommandContainer cmd, TileArea ta)
{
	if (StationJoinerNeeded<T>(cmd, ta)) {
		if (!_settings_client.gui.persistent_buildingtools) ResetObjectToPlace();
		new SelectStationWindow<T>(&_select_station_desc, cmd, ta);
	} else {
		DoCommandP(&cmd);
	}
}

/**
 * Show the station selection window when needed. If not, build the station.
 * @param cmd Command to build the station.
 * @param ta Area to build the station in
 */
void ShowSelectStationIfNeeded(CommandContainer cmd, TileArea ta)
{
	ShowSelectBaseStationIfNeeded<Station>(cmd, ta);
}

/**
 * Show the waypoint selection window when needed. If not, build the waypoint.
 * @param cmd Command to build the waypoint.
 * @param ta Area to build the waypoint in
 */
void ShowSelectWaypointIfNeeded(CommandContainer cmd, TileArea ta)
{
	ShowSelectBaseStationIfNeeded<Waypoint>(cmd, ta);
}
