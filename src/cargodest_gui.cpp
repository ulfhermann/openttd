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
#include "cargodest_base.h"
#include "cargodest_gui.h"
#include "town.h"
#include "industry.h"
#include "string_func.h"

#include "table/strings.h"

static const CargoSourceSink *_cur_cargo_source;

int CDECL CargoLinkSorter(const GUICargoLink *a, const GUICargoLink *b)
{
	/* Sort by cargo type. */
	if (a->cid != b->cid) return a->cid < b->cid ? -1 : +1;

	/* Sort unspecified destination links always last. */
	if (a->link->dest == NULL) return +1;
	if (b->link->dest == NULL) return -1;

	/* Sort link with the current source as destination first. */
	if (a->link->dest == _cur_cargo_source) return -1;
	if (b->link->dest == _cur_cargo_source) return +1;

	/* Sort towns before industries. */
	if (a->link->dest->GetType() != b->link->dest->GetType()) {
		return a->link->dest->GetType() < b->link->dest->GetType() ? +1 : -1;
	}

	/* Sort by name. */
	static const CargoLink *last_b = NULL;
	static char last_name[128];

	char name[128];
	SetDParam(0, a->link->dest->GetID());
	GetString(name, a->link->dest->GetType() == ST_TOWN ? STR_TOWN_NAME : STR_INDUSTRY_NAME, lastof(name));

	/* Cache name lookup of 'b', as the sorter is often called
	 * multiple times with the same 'b'. */
	if (b->link != last_b) {
		last_b = b->link;

		SetDParam(0, b->link->dest->GetID());
		GetString(last_name, b->link->dest->GetType() == ST_TOWN ? STR_TOWN_NAME : STR_INDUSTRY_NAME, lastof(last_name));
	}

	return strcmp(name, last_name);
}

CargoDestinationList::CargoDestinationList(const CargoSourceSink *o) : obj(o)
{
	this->InvalidateData();
}

/** Rebuild the link list from the source object. */
void CargoDestinationList::RebuildList()
{
	if (!this->link_list.NeedRebuild()) return;

	this->link_list.Clear();
	for (CargoID i = 0; i < lengthof(this->obj->cargo_links); i++) {
		for (const CargoLink *l = this->obj->cargo_links[i].Begin(); l != this->obj->cargo_links[i].End(); l++) {
			*this->link_list.Append() = GUICargoLink(i, l);
		}
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
		SetDParam(0, l->cid);
		SetDParam(1, l->link->amount.old_act);
		SetDParam(2, l->cid);
		SetDParam(3, l->link->amount.old_max);

		/* Select string according to the destination type. */
		if (l->link->dest == NULL) {
			DrawString(left + 2 * WD_FRAMERECT_LEFT, right - WD_FRAMERECT_RIGHT, y += FONT_HEIGHT_NORMAL, STR_VIEW_CARGO_LAST_MONTH_OTHER);
		} else if (l->link->dest == obj) {
			DrawString(left + 2 * WD_FRAMERECT_LEFT, right - WD_FRAMERECT_RIGHT, y += FONT_HEIGHT_NORMAL, STR_VIEW_CARGO_LAST_MONTH_LOCAL);
		} else {
			SetDParam(4, l->link->dest->GetID());
			DrawString(left + 2 * WD_FRAMERECT_LEFT, right - WD_FRAMERECT_RIGHT, y += FONT_HEIGHT_NORMAL, l->link->dest->GetType() == ST_TOWN ? STR_VIEW_CARGO_LAST_MONTH_TOWN : STR_VIEW_CARGO_LAST_MONTH_INDUSTRY);
		}
	}

	return y + FONT_HEIGHT_NORMAL;
}
