/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file cargodest.cpp Implementation of cargo destinations. */

#include "stdafx.h"
#include "cargodest_type.h"
#include "cargodest_base.h"
#include "cargodest_func.h"
#include "core/bitmath_func.hpp"
#include "core/random_func.hpp"
#include "cargotype.h"
#include "settings_type.h"
#include "town.h"
#include "industry.h"
#include "window_func.h"


static const uint MAX_EXTRA_LINKS       = 2; ///< Number of extra links allowed.
static const byte INTOWN_LINK_WEIGHTMOD = 8; ///< Weight modifier for in-town links.

static const uint BASE_TOWN_LINKS       = 0; ///< Index into _settings_game.economy.cargodest.base_town_links for normal cargo
static const uint BASE_TOWN_LINKS_SYMM  = 1; ///< Index into _settings_game.economy.cargodest.base_town_links for symmteric cargos
static const uint BASE_IND_LINKS        = 0; ///< Index into _settings_game.economy.cargodest.base_ind_links for normal cargo
static const uint BASE_IND_LINKS_TOWN   = 1; ///< Index into _settings_game.economy.cargodest.base_ind_links for town cargos

/** Are cargo destinations for this cargo type enabled? */
bool CargoHasDestinations(CargoID cid)
{
	const CargoSpec *spec = CargoSpec::Get(cid);
	if (spec->town_effect == TE_PASSENGERS || spec->town_effect == TE_MAIL) {
		return HasBit(_settings_game.economy.cargodest.mode, CRM_TOWN_CARGOS);
	} else {
		return HasBit(_settings_game.economy.cargodest.mode, CRM_INDUSTRY_CARGOS);
	}
}

/** Should this cargo type primarily have towns as a destination? */
static bool IsTownCargo(CargoID cid)
{
	const CargoSpec *spec = CargoSpec::Get(cid);
	return spec->town_effect != TE_NONE;
}

/** Does this cargo have a symmetric demand?  */
static bool IsSymmetricCargo(CargoID cid)
{
	const CargoSpec *spec = CargoSpec::Get(cid);
	return spec->town_effect == TE_PASSENGERS;
}


/** Information for the town/industry enumerators. */
struct EnumRandomData {
	CargoSourceSink *source;
	CargoID         cid;
	bool            limit_links;
};

/** Common helper for town/industry enumeration. */
static bool EnumAnyDest(const CargoSourceSink *dest, EnumRandomData *erd)
{
	/* Already a destination? */
	if (erd->source->HasLinkTo(erd->cid, dest)) return false;

	/* Destination already has too many links? */
	if (erd->limit_links && dest->cargo_links[erd->cid].Length() > dest->num_links_expected[erd->cid] + MAX_EXTRA_LINKS) return false;

	return true;
}

/** Enumerate any town not already a destination and accepting a specific cargo.*/
static bool EnumAnyTown(const Town *t, void *data)
{
	EnumRandomData *erd = (EnumRandomData *)data;
	return EnumAnyDest(t, erd) && t->AcceptsCargo(erd->cid);
}

/** Enumerate any industry not already a destination and accepting a specific cargo. */
static bool EnumAnyIndustry(const Industry *ind, void *data)
{
	EnumRandomData *erd = (EnumRandomData *)data;
	return EnumAnyDest(ind, erd) && ind->AcceptsCargo(erd->cid);
}


/** Find a town as a destination. */
static CargoSourceSink *FindTownDestination(CargoSourceSink *source, CargoID cid, TownID skip = INVALID_TOWN)
{
	EnumRandomData erd = {source, cid, IsSymmetricCargo(cid)};

	return Town::GetRandom(&EnumAnyTown, skip, &erd);
}

/** Find an industry as a destination. */
static CargoSourceSink *FindIndustryDestination(CargoSourceSink *source, CargoID cid, IndustryID skip = INVALID_INDUSTRY)
{
	EnumRandomData erd = {source, cid, IsSymmetricCargo(cid)};

	return Industry::GetRandom(&EnumAnyIndustry, skip, &erd);
}

/* virtual */ void CargoSourceSink::CreateSpecialLinks(CargoID cid)
{
	if (this->cargo_links[cid].Length() == 0) {
		/* First link is for undetermined destinations. */
		*this->cargo_links[cid].Append() = CargoLink(NULL);
	}
}

/* virtual */ void Town::CreateSpecialLinks(CargoID cid)
{
	CargoSourceSink::CreateSpecialLinks(cid);

	if (this->AcceptsCargo(cid)) {
		/* Add special link for town-local demand if not already present. */
		if (this->cargo_links[cid].Length() < 2) *this->cargo_links[cid].Append() = CargoLink(this, INTOWN_LINK_WEIGHTMOD);
		if (this->cargo_links[cid].Get(1)->dest != this) {
			/* Insert link at second place. */
			*this->cargo_links[cid].Append() = *this->cargo_links[cid].Get(1);
			*this->cargo_links[cid].Get(1) = CargoLink(this, INTOWN_LINK_WEIGHTMOD);
		}
	} else {
		/* Remove link for town-local demand if present. */
		if (this->cargo_links[cid].Length() > 1 && this->cargo_links[cid].Get(1)->dest == this) {
			this->cargo_links[cid].Erase(this->cargo_links[cid].Get(1));
		}
	}
}

/**
 * Remove the link with the lowest weight from a cargo source. The
 * reverse link is removed as well if the cargo has symmetric demand.
 * @param source Remove the link from this cargo source.
 * @param cid Cargo type of the link to remove.
 */
static void RemoveLowestLink(CargoSourceSink *source, CargoID cid)
{
	uint lowest_weight = UINT_MAX;
	CargoLink *lowest_link = NULL;

	for (CargoLink *l = source->cargo_links[cid].Begin(); l != source->cargo_links[cid].End(); l++) {
		/* Don't remove special links. */
		if (l->dest == NULL || l->dest == source) continue;

		if (l->weight < lowest_weight) {
			lowest_weight = l->weight;
			lowest_link = l;
		}
	}

	if (lowest_link != NULL) {
		/* If this is a symmetric cargo, also remove the reverse link. */
		if (IsSymmetricCargo(cid) && lowest_link->dest->HasLinkTo(cid, source)) {
			source->num_incoming_links[cid]--;
			lowest_link->dest->cargo_links[cid].Erase(lowest_link->dest->cargo_links[cid].Find(CargoLink(source)));
		}
		lowest_link->dest->num_incoming_links[cid]--;
		source->cargo_links[cid].Erase(lowest_link);
	}
}

/** Create missing cargo links for a source. */
static void CreateNewLinks(CargoSourceSink *source, CargoID cid, uint chance_a, uint chance_b, TownID skip_town, IndustryID skip_ind)
{
	uint num_links = source->num_links_expected[cid];

	/* Remove the link with the lowest weight if the
	 * town has more than links more than expected. */
	if (source->cargo_links[cid].Length() > num_links + MAX_EXTRA_LINKS) {
		RemoveLowestLink(source, cid);
	}

	/* Add new links until the expected link count is reached. */
	while (source->cargo_links[cid].Length() < num_links) {
		CargoSourceSink *dest = NULL;

		/* Chance for town/industry is chance_a/chance_b, otherwise try industry/town. */
		if (Chance16(chance_a, chance_b)) {
			dest = FindTownDestination(source, cid, skip_town);
			/* No town found? Try an industry. */
			if (dest == NULL) dest = FindIndustryDestination(source, cid, skip_ind);
		} else {
			dest = FindIndustryDestination(source, cid, skip_ind);
			/* No industry found? Try a town. */
			if (dest == NULL) dest = FindTownDestination(source, cid, skip_town);
		}

		/* If we didn't find a destination, break out of the loop because no
		 * more destinations are left on the map. */
		if (dest == NULL) break;

		/* If this is a symmetric cargo and we accept it as well, create a back link. */
		if (IsSymmetricCargo(cid) && dest->SuppliesCargo(cid) && source->AcceptsCargo(cid)) {
			*dest->cargo_links[cid].Append() = CargoLink(source);
			source->num_incoming_links[cid]++;
		}

		*source->cargo_links[cid].Append() = CargoLink(dest);
		dest->num_incoming_links[cid]++;
	}
}

/** Updated the desired link count for each cargo. */
void UpdateExpectedLinks(Town *t)
{
	CargoID cid;

	FOR_EACH_SET_CARGO_ID(cid, t->cargo_produced) {
		if (CargoHasDestinations(cid)) {
			t->CreateSpecialLinks(cid);

			uint num_links = _settings_game.economy.cargodest.base_town_links[IsSymmetricCargo(cid) ? BASE_TOWN_LINKS_SYMM : BASE_TOWN_LINKS];

			/* Account for the two special links. */
			num_links++;
			if (t->cargo_links[cid].Length() > 1 && t->cargo_links[cid].Get(1)->dest == t) num_links++;

			t->num_links_expected[cid] = ClampToU16(num_links);
		}
	}
}

/** Updated the desired link count for each cargo. */
void UpdateExpectedLinks(Industry *ind)
{
	for (uint i = 0; i < lengthof(ind->produced_cargo); i++) {
		CargoID cid = ind->produced_cargo[i];
		if (cid == INVALID_CARGO) continue;

		if (CargoHasDestinations(cid)) {
			ind->CreateSpecialLinks(cid);

			uint num_links = _settings_game.economy.cargodest.base_ind_links[IsTownCargo(cid) ? BASE_IND_LINKS_TOWN : BASE_IND_LINKS];

			/* Account for the one special link. */
			num_links++;

			ind->num_links_expected[cid] = ClampToU16(num_links);
		}
	}
}

/** Update the demand links. */
void UpdateCargoLinks(Town *t)
{
	CargoID cid;

	FOR_EACH_SET_CARGO_ID(cid, t->cargo_produced) {
		if (CargoHasDestinations(cid)) {
			/* If this is a town cargo, 95% chance for town/industry destination and
			 * 5% for industry/town. The reverse chance otherwise. */
			CreateNewLinks(t, cid, IsTownCargo(cid) ? 19 : 1, 20, t->index, INVALID_INDUSTRY);
		}
	}
}

/** Update the demand links. */
void UpdateCargoLinks(Industry *ind)
{
	for (uint i = 0; i < lengthof(ind->produced_cargo); i++) {
		CargoID cid = ind->produced_cargo[i];
		if (cid == INVALID_CARGO) continue;

		if (CargoHasDestinations(cid)) {
			/* If this is a town cargo, 75% chance for town/industry destination and
			 * 25% for industry/town. The reverse chance otherwise. */
			CreateNewLinks(ind, cid, IsTownCargo(cid) ? 3 : 1, 4, INVALID_TOWN, ind->index);
		}
	}
}

/** Recalculate the link weights. */
void UpdateLinkWeights(CargoSourceSink *css)
{
	for (uint cid = 0; cid < NUM_CARGO; cid++) {
		css->cargo_links_weight[cid] = 0;

		for (CargoLink *l = css->cargo_links[cid].Begin(); l != css->cargo_links[cid].End(); l++) {
			css->cargo_links_weight[cid] += l->weight;
			l->amount.NewMonth();
		}
	}
}

/* virtual */ CargoSourceSink::~CargoSourceSink()
{
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

	/* Decrement incoming link count for all link destinations. */
	for (CargoID cid = 0; cid < NUM_CARGO; cid++) {
		for (CargoLink *l = this->cargo_links[cid].Begin(); l != this->cargo_links[cid].End(); l++) {
			if (l->dest != NULL) l->dest->num_incoming_links[cid]--;
		}
	}
}

/** Rebuild the cached count of incoming cargo links. */
void RebuildCargoLinkCounts()
{
	/* Clear incoming link count of all towns and industries. */
	CargoSourceSink *source;
	FOR_ALL_TOWNS(source) MemSetT(source->num_incoming_links, 0, lengthof(source->num_incoming_links));
	FOR_ALL_INDUSTRIES(source) MemSetT(source->num_incoming_links, 0, lengthof(source->num_incoming_links));

	/* Count all incoming links. */
	FOR_ALL_TOWNS(source) {
		for (CargoID cid = 0; cid < NUM_CARGO; cid++) {
			for (CargoLink *l = source->cargo_links[cid].Begin(); l != source->cargo_links[cid].End(); l++) {
				if (l->dest != NULL && l->dest != source) l->dest->num_incoming_links[cid]++;
			}
		}
	}
	FOR_ALL_INDUSTRIES(source) {
		for (CargoID cid = 0; cid < NUM_CARGO; cid++) {
			for (CargoLink *l = source->cargo_links[cid].Begin(); l != source->cargo_links[cid].End(); l++) {
				if (l->dest != NULL && l->dest != source) l->dest->num_incoming_links[cid]++;
			}
		}
	}
}

/** Update the demand links of all towns and industries. */
void UpdateCargoLinks()
{
	if (_settings_game.economy.cargodest.mode == 0) return;

	Town *t;
	Industry *ind;

	/* Recalculate the number of expected links. */
	FOR_ALL_TOWNS(t) UpdateExpectedLinks(t);
	FOR_ALL_INDUSTRIES(ind) UpdateExpectedLinks(ind);

	/* Update the demand link list. */
	FOR_ALL_TOWNS(t) UpdateCargoLinks(t);
	FOR_ALL_INDUSTRIES(ind) UpdateCargoLinks(ind);

	/* Recalculate links weights. */
	FOR_ALL_TOWNS(t) UpdateLinkWeights(t);
	FOR_ALL_INDUSTRIES(ind) UpdateLinkWeights(ind);

	InvalidateWindowClassesData(WC_TOWN_VIEW, 1);
	InvalidateWindowClassesData(WC_INDUSTRY_VIEW, 1);
}
