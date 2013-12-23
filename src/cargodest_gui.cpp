/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file cargodest_gui.cpp GUI for cargo destinations. */

#include "stdafx.h"
#include "window_gui.h"
#include "gfx_func.h"
#include "strings_func.h"
#include "cargodest_gui.h"
#include "town.h"
#include "industry.h"
#include "string_func.h"
#include "gui.h"
#include "viewport_func.h"
#include "company_base.h"
#include "linkgraph/destinations.h"

#include "table/strings.h"

static CargoSourceSink _cur_cargo_source(ST_ANY, INVALID_SOURCE);

int CDECL CargoLinkSorter(const GUICargoLink *a, const GUICargoLink *b)
{
	/* Sort by cargo type. */
	if (a->cid != b->cid) return a->cid < b->cid ? -1 : +1;

	/* Sort unspecified destination links always last. */
	if (a->to.id == INVALID_SOURCE) return +1;
	if (b->to.id == INVALID_SOURCE) return -1;

	/* Sort link with the current source as destination first. */
	if (a->to == _cur_cargo_source) return -1;
	if (b->to == _cur_cargo_source) return +1;

	/* Sort towns before industries. */
	if (a->to.type != b->to.type) {
		return a->to.type < b->to.type ? +1 : -1;
	}

	/* Sort by name. */
	static CargoSourceSink last_b(ST_ANY, INVALID_SOURCE);
	static char last_name[128];

	char name[128];
	SetDParam(0, a->to.id);
	GetString(name, a->to.type == ST_TOWN ? STR_TOWN_NAME : STR_INDUSTRY_NAME, lastof(name));

	/* Cache name lookup of 'b', as the sorter is often called
	 * multiple times with the same 'b'. */
	if (b->to != last_b) {
		last_b = b->to;

		SetDParam(0, b->to.id);
		GetString(last_name, b->to.type == ST_TOWN ? STR_TOWN_NAME : STR_INDUSTRY_NAME, lastof(last_name));
	}

	return strcmp(name, last_name);
}

CargoDestinationList::CargoDestinationList(const CargoSourceSink &o) : obj(o)
{
	this->InvalidateData();
}

void CargoDestinationList::AppendLinks(CargoID c)
{
	const DestinationList &dests = _cargo_destinations[c].GetDestinations(obj.type, obj.id);
	for (const CargoSourceSink *i = dests.Begin(); i != dests.End(); ++i) {
		*this->link_list.Append() = GUICargoLink(c, obj, *i);
	}
}

/** Rebuild the link list from the source object. */
void CargoDestinationList::RebuildList()
{
	if (!this->link_list.NeedRebuild()) return;

	this->link_list.Clear();
	switch(obj.type) {
	case ST_TOWN: {
		CargoID c;
		FOR_EACH_SET_BIT(c, Town::Get(obj.id)->cargo_produced) {
			this->AppendLinks(c);
		}
		break;
	}
	case ST_INDUSTRY: {
		const Industry *ind = Industry::Get(obj.id);
		for (uint i = 0; i < lengthof(ind->produced_cargo); ++i) {
			this->AppendLinks(ind->produced_cargo[i]);
		}
		break;
	}
	case ST_HEADQUARTERS: {
		this->AppendLinks(CT_PASSENGERS);
		this->AppendLinks(CT_MAIL);
		break;
	}
	default:
		NOT_REACHED();
		break;
	}

	this->link_list.Compact();
	this->link_list.RebuildDone();
}

/** Sort the link list. */
void CargoDestinationList::SortList()
{
	_cur_cargo_source = this->obj;
	this->link_list.Sort(&CargoLinkSorter);
}

/** Rebuild the list, e.g. when a new cargo link was added. */
void CargoDestinationList::InvalidateData()
{
	this->link_list.ForceRebuild();
	this->RebuildList();
	this->SortList();
}

/** Resort the list, e.g. when a town is renamed. */
void CargoDestinationList::Resort()
{
	this->link_list.ForceResort();
	this->SortList();
}

/**
 * Get the height needed to display the destination list.
 * @param obj Object to display the destinations of.
 * @return Height needed for display.
 */
uint CargoDestinationList::GetListHeight() const
{
	uint lines = 1 + this->link_list.Length();
	return lines > 1 ? WD_PAR_VSEP_WIDE + lines * FONT_HEIGHT_NORMAL : 0;
}

/**
 * Draw the destination list.
 * @param left The left most position to draw on.
 * @param right The right most position to draw on.
 * @param y The top position to start drawing.
 * @return New \c y value below the drawn text.
 */
uint CargoDestinationList::DrawList(uint left, uint right, uint y) const
{
	if (this->link_list.Length() == 0) return y;

	DrawString(left + WD_FRAMERECT_LEFT, right - WD_FRAMERECT_RIGHT, y += WD_PAR_VSEP_WIDE + FONT_HEIGHT_NORMAL, STR_VIEW_CARGO_LAST_MONTH_OUT);

	for (const GUICargoLink *l = this->link_list.Begin(); l != this->link_list.End(); l++) {
		SetDParam(0, CargoSpec::Get(l->cid)->name);

		/* Select string according to the destination type. */
		if (l->to.id == INVALID_SOURCE) {
			DrawString(left + 2 * WD_FRAMERECT_LEFT, right - WD_FRAMERECT_RIGHT, y += FONT_HEIGHT_NORMAL, STR_VIEW_CARGO_LAST_MONTH_OTHER);
		} else if (l->to == obj) {
			DrawString(left + 2 * WD_FRAMERECT_LEFT, right - WD_FRAMERECT_RIGHT, y += FONT_HEIGHT_NORMAL, STR_VIEW_CARGO_LAST_MONTH_LOCAL);
		} else {
			SetDParam(1, l->to.id);
			StringID string = INVALID_STRING_ID;
			switch (l->to.type) {
			case ST_TOWN: string = STR_VIEW_CARGO_LAST_MONTH_TOWN; break;
			case ST_INDUSTRY: string = STR_VIEW_CARGO_LAST_MONTH_INDUSTRY; break;
			case ST_HEADQUARTERS: string = STR_VIEW_CARGO_LAST_MONTH_HQ; break;
			default: NOT_REACHED(); break;
			}

			DrawString(left + 2 * WD_FRAMERECT_LEFT, right - WD_FRAMERECT_RIGHT, y += FONT_HEIGHT_NORMAL, string);
		}
	}

	return y + FONT_HEIGHT_NORMAL;
}

/**
 * Handle click event onto the destination list.
 * @param y Position of the click in relative to the top of the destination list.
 */
void CargoDestinationList::OnClick(uint y) const
{
	/* Subtract caption height. */
	y -= WD_PAR_VSEP_WIDE + 2 * FONT_HEIGHT_NORMAL;

	/* Calculate line from click pos. */
	y /= FONT_HEIGHT_NORMAL;
	if (y >= this->link_list.Length()) return;

	/* Move viewpoint to the position of the destination. */
	const CargoSourceSink &to = this->link_list[y].to;
	if (to.id == INVALID_SOURCE) return;

	TileIndex xy = INVALID_TILE;
	switch(to.type) {
	case ST_TOWN: xy = Town::Get(to.id)->xy; break;
	case ST_INDUSTRY: xy = Industry::Get(to.id)->location.tile; break;
	case ST_HEADQUARTERS: xy = Company::Get(to.id)->location_of_HQ; break;
	default: NOT_REACHED(); break;
	}

	if (_ctrl_pressed) {
		ShowExtraViewPortWindow(xy);
	} else {
		ScrollMainWindowToTile(xy);
	}
}
