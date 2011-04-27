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
static const uint BIG_TOWN_POP_MAIL     = 0; ///< Index into _settings_game.economy.cargodest.big_town_pop for mail
static const uint BIG_TOWN_POP_PAX      = 1; ///< Index into _settings_game.economy.cargodest.big_town_pop for passengers
static const uint SCALE_TOWN            = 0; ///< Index into _settings_game.economy.cargodest.pop_scale_town/weight_scale_town for normal cargo
static const uint SCALE_TOWN_BIG        = 1; ///< Index into _settings_game.economy.cargodest.pop_scale_town/weight_scale_town for normal cargo of big towns
static const uint SCALE_TOWN_PAX        = 2; ///< Index into _settings_game.economy.cargodest.pop_scale_town/weight_scale_town for passengers
static const uint SCALE_TOWN_BIG_PAX    = 3; ///< Index into _settings_game.economy.cargodest.pop_scale_town/weight_scale_town for passengers of big towns
static const uint MIN_WEIGHT_TOWN       = 0; ///< Index into _settings_game.economy.cargodest.min_weight_town for normal cargo
static const uint MIN_WEIGHT_TOWN_PAX   = 1; ///< Index into _settings_game.economy.cargodest.min_weight_town for passengers

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

/** Is this a passenger cargo. */
static bool IsPassengerCargo(CargoID cid)
{
	const CargoSpec *spec = CargoSpec::Get(cid);
	return spec->town_effect == TE_PASSENGERS;
}


/** Information for the town/industry enumerators. */
struct EnumRandomData {
	CargoSourceSink *source;
	TileIndex       source_xy;
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

/** Enumerate cities. */
static bool EnumCity(const Town *t, void *data)
{
	return EnumAnyTown(t, data) && t->larger_town;
}

/** Enumerate towns with a big population. */
static bool EnumBigTown(const Town *t, void *data)
{
	EnumRandomData *erd = (EnumRandomData *)data;
	return EnumAnyTown(t, erd) && (IsPassengerCargo(erd->cid) ? t->pass.old_max > _settings_game.economy.cargodest.big_town_pop[BIG_TOWN_POP_PAX] : t->mail.old_max > _settings_game.economy.cargodest.big_town_pop[BIG_TOWN_POP_MAIL]);
}

/** Enumerate nearby towns. */
static bool EnumNearbyTown(const Town *t, void *data)
{
	EnumRandomData *erd = (EnumRandomData *)data;
	/* Scale distance by 1D map size to make sure that there are still
	 * candidates left on larger maps with few towns, but don't scale
	 * by 2D map size so the map still feels bigger. */
	return EnumAnyTown(t, data) && DistanceSquare(t->xy, erd->source_xy) < ScaleByMapSize1D(_settings_game.economy.cargodest.town_nearby_dist);
}

/** Enumerate any industry not already a destination and accepting a specific cargo. */
static bool EnumAnyIndustry(const Industry *ind, void *data)
{
	EnumRandomData *erd = (EnumRandomData *)data;
	return EnumAnyDest(ind, erd) && ind->AcceptsCargo(erd->cid);
}


/** Find a town as a destination. */
static CargoSourceSink *FindTownDestination(byte &weight_mod, CargoSourceSink *source, TileIndex source_xy, CargoID cid, const uint8 destclass_chance[4], TownID skip = INVALID_TOWN)
{
	/* Enum functions for: nearby town, city, big town, and any town. */
	static const Town::EnumTownProc destclass_enum[] = {
		&EnumNearbyTown, &EnumCity, &EnumBigTown, &EnumAnyTown
	};
	static const byte weight_mods[] = {5, 4, 3, 2};
	assert_compile(lengthof(destclass_enum) == lengthof(weight_mods));

	EnumRandomData erd = {source, source_xy, cid, IsSymmetricCargo(cid)};

	/* Determine destination class. If no town is found in this class,
	 * the search falls through to the following classes. */
	byte destclass = RandomRange(destclass_chance[3]);

	weight_mod = 1;
	Town *dest = NULL;
	for (uint i = 0; i < lengthof(destclass_enum) && dest == NULL; i++) {
		/* Skip if destination class not reached. */
		if (destclass > destclass_chance[i]) continue;

		dest = Town::GetRandom(destclass_enum[i], skip, &erd);
		weight_mod = weight_mods[i];
	}

	return dest;
}

/** Find an industry as a destination. */
static CargoSourceSink *FindIndustryDestination(CargoSourceSink *source, CargoID cid, IndustryID skip = INVALID_INDUSTRY)
{
	EnumRandomData erd = {source, INVALID_TILE, cid, IsSymmetricCargo(cid)};

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
static void CreateNewLinks(CargoSourceSink *source, TileIndex source_xy, CargoID cid, uint chance_a, uint chance_b, const uint8 town_chance[], TownID skip_town, IndustryID skip_ind)
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
		byte weight_mod = 1;

		/* Chance for town/industry is chance_a/chance_b, otherwise try industry/town. */
		if (Chance16(chance_a, chance_b)) {
			dest = FindTownDestination(weight_mod, source, source_xy, cid, town_chance, skip_town);
			/* No town found? Try an industry. */
			if (dest == NULL) dest = FindIndustryDestination(source, cid, skip_ind);
		} else {
			dest = FindIndustryDestination(source, cid, skip_ind);
			/* No industry found? Try a town. */
			if (dest == NULL) dest = FindTownDestination(weight_mod, source, source_xy, cid, town_chance, skip_town);
		}

		/* If we didn't find a destination, break out of the loop because no
		 * more destinations are left on the map. */
		if (dest == NULL) break;

		/* If this is a symmetric cargo and we accept it as well, create a back link. */
		if (IsSymmetricCargo(cid) && dest->SuppliesCargo(cid) && source->AcceptsCargo(cid)) {
			*dest->cargo_links[cid].Append() = CargoLink(source, weight_mod);
			source->num_incoming_links[cid]++;
		}

		*source->cargo_links[cid].Append() = CargoLink(dest, weight_mod);
		dest->num_incoming_links[cid]++;
	}
}

/** Remove invalid links from a cargo source/sink. */
static void RemoveInvalidLinks(CargoSourceSink *css)
{
	for (CargoID cid = 0; cid < NUM_CARGO; cid++) {
		/* Remove outgoing links if cargo isn't supplied anymore. */
		if (!css->SuppliesCargo(cid)) {
			for (CargoLink *l = css->cargo_links[cid].Begin(); l != css->cargo_links[cid].End(); l++) {
				if (l->dest != NULL && l->dest != css) l->dest->num_incoming_links[cid]--;
			}
			css->cargo_links[cid].Clear();
			css->cargo_links_weight[cid] = 0;
		}

		/* Remove outgoing links if the dest doesn't accept the cargo anymore. */
		for (CargoLink *l = css->cargo_links[cid].Begin(); l != css->cargo_links[cid].End(); ) {
			if (l->dest != NULL && !l->dest->AcceptsCargo(cid)) {
				if (l->dest != css) l->dest->num_incoming_links[cid]--;
				css->cargo_links[cid].Erase(l);
			} else {
				l++;
			}
		}
	}
}

/** Updated the desired link count for each cargo. */
void UpdateExpectedLinks(Town *t)
{
	CargoID cid;

	FOR_EACH_SET_CARGO_ID(cid, t->cargo_produced) {
		if (CargoHasDestinations(cid)) {
			t->CreateSpecialLinks(cid);

			uint max_amt = IsPassengerCargo(cid) ? t->pass.old_max : t->mail.old_max;
			uint big_amt = _settings_game.economy.cargodest.big_town_pop[IsPassengerCargo(cid) ? BIG_TOWN_POP_PAX : BIG_TOWN_POP_MAIL];

			uint num_links = _settings_game.economy.cargodest.base_town_links[IsSymmetricCargo(cid) ? BASE_TOWN_LINKS_SYMM : BASE_TOWN_LINKS];
			/* Add links based on the available cargo amount. */
			num_links += min(max_amt, big_amt) / _settings_game.economy.cargodest.pop_scale_town[IsPassengerCargo(cid) ? SCALE_TOWN_PAX : SCALE_TOWN];
			if (max_amt > big_amt) num_links += (max_amt - big_amt) / _settings_game.economy.cargodest.pop_scale_town[IsPassengerCargo(cid) ? SCALE_TOWN_BIG_PAX : SCALE_TOWN_BIG];
			/* Ensure a city has at least city_town_links more than the base value.
			 * This improves the link distribution at the beginning of a game when
			 * the towns are still small. */
			if (t->larger_town) num_links = max<uint>(num_links, _settings_game.economy.cargodest.city_town_links + _settings_game.economy.cargodest.base_town_links[IsSymmetricCargo(cid) ? BASE_TOWN_LINKS_SYMM : BASE_TOWN_LINKS]);

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
			CreateNewLinks(t, t->xy, cid, IsTownCargo(cid) ? 19 : 1, 20, t->larger_town ? _settings_game.economy.cargodest.town_chances_city : _settings_game.economy.cargodest.town_chances_town, t->index, INVALID_INDUSTRY);
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
			CreateNewLinks(ind, ind->location.tile, cid, IsTownCargo(cid) ? 3 : 1, 4, _settings_game.economy.cargodest.town_chances_town, INVALID_TOWN, ind->index);
		}
	}
}

/* virtual */ uint Town::GetDestinationWeight(CargoID cid, byte weight_mod) const
{
	uint max_amt = IsPassengerCargo(cid) ? this->pass.old_max : this->mail.old_max;
	uint big_amt = _settings_game.economy.cargodest.big_town_pop[IsPassengerCargo(cid) ? BIG_TOWN_POP_PAX : BIG_TOWN_POP_MAIL];

	/* The weight is calculated by a piecewise function. We start with a predefined
	 * minimum weight and then add the weight for the cargo amount up to the big
	 * town amount. If the amount is more than the big town amount, this is also
	 * added to the weight with a different scale factor to make sure that big towns
	 * don't siphon the cargo away too much from the smaller destinations. */
	uint weight = _settings_game.economy.cargodest.min_weight_town[IsPassengerCargo(cid) ? MIN_WEIGHT_TOWN_PAX : MIN_WEIGHT_TOWN];
	weight += min(max_amt, big_amt) * weight_mod / _settings_game.economy.cargodest.weight_scale_town[IsPassengerCargo(cid) ? SCALE_TOWN_PAX : SCALE_TOWN];
	if (max_amt > big_amt) weight += (max_amt - big_amt) * weight_mod / _settings_game.economy.cargodest.weight_scale_town[IsPassengerCargo(cid) ? SCALE_TOWN_BIG_PAX : SCALE_TOWN_BIG];

	return weight;
}

/* virtual */ uint Industry::GetDestinationWeight(CargoID cid, byte weight_mod) const
{
	return weight_mod;
}

/** Recalculate the link weights. */
void UpdateLinkWeights(Town *t)
{
	for (CargoID cid = 0; cid < NUM_CARGO; cid++) {
		uint weight_sum = 0;

		if (t->cargo_links[cid].Length() == 0) continue;

		t->cargo_links[cid].Begin()->amount.NewMonth();

		/* Skip the special link for undetermined destinations. */
		for (CargoLink *l = t->cargo_links[cid].Begin() + 1; l != t->cargo_links[cid].End(); l++) {
			l->weight = l->dest->GetDestinationWeight(cid, l->weight_mod);
			weight_sum += l->weight;

			l->amount.NewMonth();
		}

		/* Limit the weight of the in-town link to at most 1/3 of the total weight. */
		if (t->cargo_links[cid].Length() > 1 && t->cargo_links[cid].Get(1)->dest == t) {
			uint new_weight = min(t->cargo_links[cid].Get(1)->weight, weight_sum / 3);
			weight_sum -= t->cargo_links[cid].Get(1)->weight - new_weight;
			t->cargo_links[cid].Get(1)->weight = new_weight;
		}

		/* Set weight for the undetermined destination link to random_dest_chance%. */
		t->cargo_links[cid].Begin()->weight = weight_sum == 0 ? 1 : (weight_sum * _settings_game.economy.cargodest.random_dest_chance) / (100 - _settings_game.economy.cargodest.random_dest_chance);

		t->cargo_links_weight[cid] = weight_sum + t->cargo_links[cid].Begin()->weight;
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

	/* Remove links that have become invalid. */
	FOR_ALL_TOWNS(t) RemoveInvalidLinks(t);
	FOR_ALL_INDUSTRIES(ind) RemoveInvalidLinks(ind);

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
