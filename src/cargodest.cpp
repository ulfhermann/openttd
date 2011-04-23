/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file cargodest.cpp Implementation of cargo destinations. */

#include "stdafx.h"
#include "cargodest_base.h"
#include "town.h"
#include "industry.h"

/* virtual */ CargoSourceSink::~CargoSourceSink()
{
	if (Town::CleaningPool() || Industry::CleaningPool()) return;

	/* Remove all demand links having us as a destination. */
	Town *t;
	FOR_ALL_TOWNS(t) {
		for (CargoID cid = 0; cid < NUM_CARGO; cid++) {
			if (t->HasLinkTo(cid, this)) {
				t->cargo_links[cid].Erase(t->cargo_links[cid].Find(CargoLink(this)));
				InvalidateWindowData(WC_TOWN_VIEW, t->index, 1);
			}
		}
	}

	Industry *ind;
	FOR_ALL_INDUSTRIES(ind) {
		for (CargoID cid = 0; cid < NUM_CARGO; cid++) {
			if (ind->HasLinkTo(cid, this)) {
				ind->cargo_links[cid].Erase(ind->cargo_links[cid].Find(CargoLink(this)));
				InvalidateWindowData(WC_INDUSTRY_VIEW, ind->index, 1);
			}
		}
	}
}
