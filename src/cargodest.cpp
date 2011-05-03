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
#include "core/pool_func.hpp"
#include "cargotype.h"
#include "settings_type.h"
#include "town.h"
#include "industry.h"
#include "window_func.h"
#include "vehicle_base.h"
#include "station_base.h"


/* Possible link weight modifiers. */
static const byte LWM_ANYWHERE      = 1; ///< Weight modifier for undetermined destinations.
static const byte LWM_TONY_ANY      = 2; ///< Default weight modifier for towns.
static const byte LWM_TONY_BIG      = 3; ///< Weight modifier for big towns.
static const byte LWM_TONY_CITY     = 4; ///< Weight modifier for cities.
static const byte LWM_TONY_NEARBY   = 5; ///< Weight modifier for nearby towns.
static const byte LWM_INTOWN        = 8; ///< Weight modifier for in-town links.
static const byte LWM_IND_ANY       = 2; ///< Default weight modifier for industries.
static const byte LWM_IND_NEARBY    = 3; ///< Weight modifier for nearby industries.
static const byte LWM_IND_PRODUCING = 4; ///< Weight modifier for producing industries.

static const uint MAX_EXTRA_LINKS       = 2; ///< Number of extra links allowed.
static const uint MAX_IND_STOCKPILE     = 1000; ///< Maximum stockpile to consider for industry link weight.

static const uint BASE_TOWN_LINKS       = 0; ///< Index into _settings_game.economy.cargodest.base_town_links for normal cargo
static const uint BASE_TOWN_LINKS_SYMM  = 1; ///< Index into _settings_game.economy.cargodest.base_town_links for symmteric cargos
static const uint BASE_IND_LINKS        = 0; ///< Index into _settings_game.economy.cargodest.base_ind_links for normal cargo
static const uint BASE_IND_LINKS_TOWN   = 1; ///< Index into _settings_game.economy.cargodest.base_ind_links for town cargos
static const uint BASE_IND_LINKS_SYMM   = 2; ///< Index into _settings_game.economy.cargodest.base_ind_links for symmetric cargos
static const uint BIG_TOWN_POP_MAIL     = 0; ///< Index into _settings_game.economy.cargodest.big_town_pop for mail
static const uint BIG_TOWN_POP_PAX      = 1; ///< Index into _settings_game.economy.cargodest.big_town_pop for passengers
static const uint SCALE_TOWN            = 0; ///< Index into _settings_game.economy.cargodest.pop_scale_town/weight_scale_town for normal cargo
static const uint SCALE_TOWN_BIG        = 1; ///< Index into _settings_game.economy.cargodest.pop_scale_town/weight_scale_town for normal cargo of big towns
static const uint SCALE_TOWN_PAX        = 2; ///< Index into _settings_game.economy.cargodest.pop_scale_town/weight_scale_town for passengers
static const uint SCALE_TOWN_BIG_PAX    = 3; ///< Index into _settings_game.economy.cargodest.pop_scale_town/weight_scale_town for passengers of big towns
static const uint CARGO_SCALE_IND       = 0; ///< Index into _settings_game.economy.cargodest.cargo_scale_ind for normal cargo
static const uint CARGO_SCALE_IND_TOWN  = 1; ///< Index into _settings_game.economy.cargodest.cargo_scale_ind for town cargos
static const uint MIN_WEIGHT_TOWN       = 0; ///< Index into _settings_game.economy.cargodest.min_weight_town for normal cargo
static const uint MIN_WEIGHT_TOWN_PAX   = 1; ///< Index into _settings_game.economy.cargodest.min_weight_town for passengers
static const uint WEIGHT_SCALE_IND_PROD = 0; ///< Index into _settings_game.economy.cargodest.weight_scale_ind for produced cargo
static const uint WEIGHT_SCALE_IND_PILE = 1; ///< Index into _settings_game.economy.cargodest.weight_scale_ind for stockpiled cargo

/** Are cargo destinations for this cargo type enabled? */
bool CargoHasDestinations(CargoID cid)
{
	const CargoSpec *spec = CargoSpec::Get(cid);
	switch (spec->town_effect) {
		case TE_PASSENGERS:
		case TE_MAIL:
			return _settings_game.economy.cargodest.mode_pax_mail != CRM_OFF;

		case TE_GOODS:
		case TE_WATER:
		case TE_FOOD:
			return _settings_game.economy.cargodest.mode_town_cargo != CRM_OFF;

		default:
			return _settings_game.economy.cargodest.mode_others != CRM_OFF;
	}
}

/** Are cargo destinations for all cargo types disabled? */
bool CargoDestinationsDisabled()
{
	return _settings_game.economy.cargodest.mode_pax_mail == CRM_OFF && _settings_game.economy.cargodest.mode_town_cargo == CRM_OFF && _settings_game.economy.cargodest.mode_others == CRM_OFF;
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

/**
 * Test whether two tiles are nearby with map-size scaling.
 * @param t1 First tile.
 * @param t2 Second tile.
 * @param dist_square Allowed squared distance between the tiles.
 * @return True if the tiles are nearby.
 */
static bool IsNearby(TileIndex t1, TileIndex t2, uint32 dist_square)
{
	/* Scale distance by 1D map size to make sure that there are still
	 * candidates left on larger maps with few towns, but don't scale
	 * by 2D map size so the map still feels bigger. */
	return DistanceSquare(t1, t2) < ScaleByMapSize1D(dist_square);
}

/**
 * Test whether a tiles is near a town.
 * @param t The town.
 * @param ti The tile to test.
 * @return True if the tiles is near the town.
 */
static bool IsTownNearby(const Town *t, TileIndex ti)
{
	return IsNearby(t->xy, ti, _settings_game.economy.cargodest.town_nearby_dist);
}

/**
 * Test whether a tiles is near an industry.
 * @param ind The industry.
 * @param ti The tile to test.
 * @return True if the tiles is near the town.
 */
static bool IsIndustryNearby(const Industry *ind, TileIndex ti)
{
	return IsNearby(ind->location.tile, ti, _settings_game.economy.cargodest.ind_nearby_dist);
}

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
	return EnumAnyTown(t, data) && IsTownNearby(t, erd->source_xy);
}

/** Enumerate any industry not already a destination and accepting a specific cargo. */
static bool EnumAnyIndustry(const Industry *ind, void *data)
{
	EnumRandomData *erd = (EnumRandomData *)data;
	return EnumAnyDest(ind, erd) && ind->AcceptsCargo(erd->cid);
}

/** Enumerate nearby industries. */
static bool EnumNearbyIndustry(const Industry *ind, void *data)
{
	EnumRandomData *erd = (EnumRandomData *)data;
	return EnumAnyIndustry(ind, data) && IsIndustryNearby(ind, erd->source_xy);
}

/** Enumerate industries that are producing cargo. */
static bool EnumProducingIndustry(const Industry *ind, void *data)
{
	return EnumAnyIndustry(ind, data) && (ind->produced_cargo[0] != CT_INVALID || ind->produced_cargo[1] != CT_INVALID);
}

/** Enumerate cargo sources supplying a specific cargo. */
template <typename T>
static bool EnumAnySupplier(const T *css, void *data)
{
	return css->SuppliesCargo(((EnumRandomData *)data)->cid);
}

/** Enumerate nearby cargo sources supplying a specific cargo. */
static bool EnumNearbySupplier(const Industry *ind, void *data)
{
	EnumRandomData *erd = (EnumRandomData *)data;
	return EnumAnySupplier(ind, data) && IsIndustryNearby(ind, erd->source_xy);
}

/** Enumerate nearby cargo sources supplying a specific cargo. */
static bool EnumNearbySupplier(const Town *t, void *data)
{
	EnumRandomData *erd = (EnumRandomData *)data;
	return EnumAnySupplier(t, data) && IsTownNearby(t, erd->source_xy);
}


/** Find a town as a destination. */
static CargoSourceSink *FindTownDestination(byte &weight_mod, CargoSourceSink *source, TileIndex source_xy, CargoID cid, const uint8 destclass_chance[4], TownID skip = INVALID_TOWN)
{
	/* Enum functions for: nearby town, city, big town, and any town. */
	static const Town::EnumTownProc destclass_enum[] = {
		&EnumNearbyTown, &EnumCity, &EnumBigTown, &EnumAnyTown
	};
	static const byte weight_mods[] = {LWM_TONY_NEARBY, LWM_TONY_CITY, LWM_TONY_BIG, LWM_TONY_ANY};
	assert_compile(lengthof(destclass_enum) == lengthof(weight_mods));

	EnumRandomData erd = {source, source_xy, cid, IsSymmetricCargo(cid)};

	/* Determine destination class. If no town is found in this class,
	 * the search falls through to the following classes. */
	byte destclass = RandomRange(destclass_chance[3]);

	weight_mod = LWM_ANYWHERE;
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
static CargoSourceSink *FindIndustryDestination(byte &weight_mod, CargoSourceSink *source, TileIndex source_xy, CargoID cid, IndustryID skip = INVALID_INDUSTRY)
{
	/* Enum functions for: nearby industry, producing industry, and any industry. */
	static const Industry::EnumIndustryProc destclass_enum[] = {
		&EnumNearbyIndustry, &EnumProducingIndustry, &EnumAnyIndustry
	};
	static const byte weight_mods[] = {LWM_IND_NEARBY, LWM_IND_PRODUCING, LWM_IND_ANY};
	assert_compile(lengthof(destclass_enum) == lengthof(_settings_game.economy.cargodest.ind_chances));

	EnumRandomData erd = {source, source_xy, cid, IsSymmetricCargo(cid)};

	/* Determine destination class. If no industry is found in this class,
	 * the search falls through to the following classes. */
	byte destclass = RandomRange(*lastof(_settings_game.economy.cargodest.ind_chances));

	weight_mod = LWM_ANYWHERE;
	Industry *dest = NULL;
	for (uint i = 0; i < lengthof(destclass_enum) && dest == NULL; i++) {
		/* Skip if destination class not reached. */
		if (destclass > _settings_game.economy.cargodest.ind_chances[i]) continue;

		dest = Industry::GetRandom(destclass_enum[i], skip, &erd);
		weight_mod = weight_mods[i];
	}

	return dest;
}

/** Find a supply for a cargo type. */
static CargoSourceSink *FindSupplySource(Industry *dest, CargoID cid)
{
	EnumRandomData erd = {dest, dest->location.tile, cid, false};

	CargoSourceSink *source = NULL;

	/* Even chance for industry source first, town second and vice versa.
	 * Try a nearby supplier first, then check all suppliers. */
	if (Chance16(1, 2)) {
		source = Industry::GetRandom(&EnumNearbySupplier, dest->index, &erd);
		if (source == NULL) source = Town::GetRandom(&EnumNearbySupplier, INVALID_TOWN, &erd);
		if (source == NULL) source = Industry::GetRandom(&EnumAnySupplier, dest->index, &erd);
		if (source == NULL) source = Town::GetRandom(&EnumAnySupplier, INVALID_TOWN, &erd);
	} else {
		source = Town::GetRandom(&EnumNearbySupplier, INVALID_TOWN, &erd);
		if (source == NULL) source = Industry::GetRandom(&EnumNearbySupplier, dest->index, &erd);
		if (source == NULL) source = Town::GetRandom(&EnumAnySupplier, INVALID_TOWN, &erd);
		if (source == NULL) source = Industry::GetRandom(&EnumAnySupplier, dest->index, &erd);
	}

	return source;
}

/* virtual */ void CargoSourceSink::CreateSpecialLinks(CargoID cid)
{
	/* First link is for undetermined destinations. */
	if (this->cargo_links[cid].Length() == 0) {
		*this->cargo_links[cid].Append() = CargoLink(NULL, LWM_ANYWHERE);
	}
	if (this->cargo_links[cid].Get(0)->dest != NULL) {
		/* Insert link at first place. */
		*this->cargo_links[cid].Append() = *this->cargo_links[cid].Get(0);
		*this->cargo_links[cid].Get(0) = CargoLink(NULL, LWM_ANYWHERE);
	}
}

/* virtual */ void Town::CreateSpecialLinks(CargoID cid)
{
	CargoSourceSink::CreateSpecialLinks(cid);

	if (this->AcceptsCargo(cid)) {
		/* Add special link for town-local demand if not already present. */
		if (this->cargo_links[cid].Length() < 2) *this->cargo_links[cid].Append() = CargoLink(this, LWM_INTOWN);
		if (this->cargo_links[cid].Get(1)->dest != this) {
			/* Insert link at second place. */
			*this->cargo_links[cid].Append() = *this->cargo_links[cid].Get(1);
			*this->cargo_links[cid].Get(1) = CargoLink(this, LWM_INTOWN);
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
			lowest_link->dest->cargo_links[cid].Erase(lowest_link->dest->cargo_links[cid].Find(CargoLink(source, LWM_ANYWHERE)));
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
		byte weight_mod = LWM_ANYWHERE;

		/* Chance for town/industry is chance_a/chance_b, otherwise try industry/town. */
		if (Chance16(chance_a, chance_b)) {
			dest = FindTownDestination(weight_mod, source, source_xy, cid, town_chance, skip_town);
			/* No town found? Try an industry. */
			if (dest == NULL) dest = FindIndustryDestination(weight_mod, source, source_xy, cid, skip_ind);
		} else {
			dest = FindIndustryDestination(weight_mod, source, source_xy, cid, skip_ind);
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

			uint num_links;
			/* Use different base values for symmetric cargos, cargos
			 * with a town effect and all other cargos. */
			num_links = _settings_game.economy.cargodest.base_ind_links[IsSymmetricCargo(cid) ? BASE_IND_LINKS_SYMM : (IsTownCargo(cid) ? BASE_IND_LINKS_TOWN : BASE_IND_LINKS)];
			/* Add links based on the average industry production. */
			num_links += ind->average_production[i] / _settings_game.economy.cargodest.cargo_scale_ind[IsTownCargo(cid) ? CARGO_SCALE_IND_TOWN : CARGO_SCALE_IND];

			/* Account for the one special link. */
			num_links++;

			ind->num_links_expected[cid] = ClampToU16(num_links);
		}
	}
}

/** Make sure an industry has at least one incoming link for each accepted cargo. */
void AddMissingIndustryLinks(Industry *ind)
{
	for (uint i = 0; i < lengthof(ind->accepts_cargo); i++) {
		CargoID cid = ind->accepts_cargo[i];
		if (cid == INVALID_CARGO) continue;

		/* Do we already have at least one cargo source? */
		if (ind->num_incoming_links[cid] > 0) continue;

		CargoSourceSink *source = FindSupplySource(ind, cid);
		if (source == NULL) continue; // Too bad...

		if (source->cargo_links[cid].Length() >= source->num_links_expected[cid] + MAX_EXTRA_LINKS) {
			/* Increase the expected link count if adding another link would
			 * exceed the count, as otherwise this (or another) link would
			 * get removed right again. */
			source->num_links_expected[cid]++;
		}

		*source->cargo_links[cid].Append() = CargoLink(ind, LWM_IND_ANY);
		ind->num_incoming_links[cid]++;

		/* If this is a symmetric cargo and we produce it as well, create a back link. */
		if (IsSymmetricCargo(cid) && ind->SuppliesCargo(cid) && source->AcceptsCargo(cid)) {
			*ind->cargo_links[cid].Append() = CargoLink(source, LWM_IND_ANY);
			source->num_incoming_links[cid]++;
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
	uint weight = _settings_game.economy.cargodest.min_weight_ind;

	for (uint i = 0; i < lengthof(this->accepts_cargo); i++) {
		if (this->accepts_cargo[i] != cid) continue;
		/* Empty stockpile means more weight for the link. Stockpiles
		 * above a fixed maximum have no further effect. */
		uint stockpile = ClampU(this->incoming_cargo_waiting[i], 0, MAX_IND_STOCKPILE);
		weight += (MAX_IND_STOCKPILE - stockpile) * weight_mod / _settings_game.economy.cargodest.weight_scale_ind[WEIGHT_SCALE_IND_PILE];
	}

	/* Add a weight for the produced cargo. Use the average production
	 * here so the weight isn't fluctuating that much when the input
	 * cargo isn't delivered regularly. */
	weight += (this->average_production[0] + this->average_production[1]) * weight_mod / _settings_game.economy.cargodest.weight_scale_ind[WEIGHT_SCALE_IND_PROD];

	return weight;
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
		uint weight_sum = 0;

		if (css->cargo_links[cid].Length() == 0) continue;

		css->cargo_links[cid].Begin()->amount.NewMonth();

		for (CargoLink *l = css->cargo_links[cid].Begin() + 1; l != css->cargo_links[cid].End(); l++) {
			l->weight = l->dest->GetDestinationWeight(cid, l->weight_mod);
			weight_sum += l->weight;

			l->amount.NewMonth();
		}

		/* Set weight for the undetermined destination link to random_dest_chance%. */
		css->cargo_links[cid].Begin()->weight = weight_sum == 0 ? 1 : (weight_sum * _settings_game.economy.cargodest.random_dest_chance) / (100 - _settings_game.economy.cargodest.random_dest_chance);

		css->cargo_links_weight[cid] = weight_sum + css->cargo_links[cid].Begin()->weight;
	}
}

/* virtual */ CargoSourceSink::~CargoSourceSink()
{
	if (Town::CleaningPool() || Industry::CleaningPool()) return;

	/* Remove all demand links having us as a destination. */
	Town *t;
	FOR_ALL_TOWNS(t) {
		for (CargoID cid = 0; cid < NUM_CARGO; cid++) {
			if (t->HasLinkTo(cid, this)) {
				t->cargo_links[cid].Erase(t->cargo_links[cid].Find(CargoLink(this, LWM_ANYWHERE)));
				InvalidateWindowData(WC_TOWN_VIEW, t->index, 1);
			}
		}
	}

	Industry *ind;
	FOR_ALL_INDUSTRIES(ind) {
		for (CargoID cid = 0; cid < NUM_CARGO; cid++) {
			if (ind->HasLinkTo(cid, this)) {
				ind->cargo_links[cid].Erase(ind->cargo_links[cid].Find(CargoLink(this, LWM_ANYWHERE)));
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
	if (CargoDestinationsDisabled()) return;

	Town *t;
	Industry *ind;

	/* Remove links that have become invalid. */
	FOR_ALL_TOWNS(t) RemoveInvalidLinks(t);
	FOR_ALL_INDUSTRIES(ind) RemoveInvalidLinks(ind);

	/* Recalculate the number of expected links. */
	FOR_ALL_TOWNS(t) UpdateExpectedLinks(t);
	FOR_ALL_INDUSTRIES(ind) UpdateExpectedLinks(ind);

	/* Make sure each industry gets at at least some input cargo. */
	FOR_ALL_INDUSTRIES(ind) AddMissingIndustryLinks(ind);

	/* Update the demand link list. */
	FOR_ALL_TOWNS(t) UpdateCargoLinks(t);
	FOR_ALL_INDUSTRIES(ind) UpdateCargoLinks(ind);

	/* Recalculate links weights. */
	FOR_ALL_TOWNS(t) UpdateLinkWeights(t);
	FOR_ALL_INDUSTRIES(ind) UpdateLinkWeights(ind);

	InvalidateWindowClassesData(WC_TOWN_VIEW, 1);
	InvalidateWindowClassesData(WC_INDUSTRY_VIEW, 1);
}

/**
 * Get a random demand link.
 * @param cid Cargo type
 * @param allow_self Indicates if the local link is acceptable as a result.
 * @return Pointer to a demand link or this->cargo_links[cid].End() if no link found.
 */
CargoLink *CargoSourceSink::GetRandomLink(CargoID cid, bool allow_self)
{
	/* Randomly choose a cargo link. */
	uint weight = RandomRange(this->cargo_links_weight[cid] - 1);
	uint cur_sum = 0;

	CargoLink *l;
	for (l = this->cargo_links[cid].Begin(); l != this->cargo_links[cid].End(); ++l) {
		cur_sum += l->weight;
		if (weight < cur_sum) {
			/* Link is valid if it is random destination or only the
			 * local link if allowed and accepts the cargo. */
			if (l->dest == NULL || ((allow_self || l->dest != this) && l->dest->AcceptsCargo(cid))) break;
		}
	}

	return l;
}


/* Initialize the RouteLink-pool */
RouteLinkPool _routelink_pool("RouteLink");
INSTANTIATE_POOL_METHODS(RouteLink)

/**
 * Update or create a single route link for a specific vehicle and cargo.
 * @param v The vehicle.
 * @param cargos Create links for the cargo types whose bit is set.
 * @param clear_others Should route links for cargo types nor carried be cleared?
 * @param from Originating station.
 * @param from_oid Originating order.
 * @param to_id Destination station ID.
 * @param to_oid Destination order.
 * @param travel_time Travel time for the route.
 */
void UpdateVehicleRouteLinks(const Vehicle *v, uint32 cargos, bool clear_others, Station *from, OrderID from_oid, StationID to_id, OrderID to_oid, uint32 travel_time)
{
	for (CargoID cid = 0; cid < NUM_CARGO; cid++) {
		bool has_cargo = HasBit(cargos, cid);
		/* Skip if cargo not carried and we aren't supposed to clear other links. */
		if (!clear_others && !has_cargo) continue;
		/* Skip cargo types that don't have destinations enabled. */
		if (!CargoHasDestinations(cid)) continue;

		RouteLinkList::iterator link;
		for (link = from->goods[cid].routes.begin(); link != from->goods[cid].routes.end(); ++link) {
			if ((*link)->GetOriginOrderId() == from_oid) {
				if (has_cargo) {
					/* Update destination if necessary. */
					(*link)->SetDestination(to_id, to_oid);
					(*link)->UpdateTravelTime(travel_time);
				} else {
					/* Remove link. */
					delete *link;
					from->goods[cid].routes.erase(link);
				}
				break;
			}
		}

		/* No link found? Append a new one. */
		if (has_cargo && link == from->goods[cid].routes.end() && RouteLink::CanAllocateItem()) {
			from->goods[cid].routes.push_back(new RouteLink(to_id, from_oid, to_oid, v->owner, travel_time));
		}
	}
}

/**
 * Update route links after a vehicle has arrived at a station.
 * @param v The vehicle.
 * @param arrived_at The station the vehicle arrived at.
 */
void UpdateVehicleRouteLinks(const Vehicle *v, StationID arrived_at)
{
	/* Only update links if we have valid previous station and orders. */
	if (v->last_station_loaded == INVALID_STATION || v->last_order_id == INVALID_ORDER || v->current_order.index == INVALID_ORDER) return;
	/* Loop? Not good. */
	if (v->last_station_loaded == arrived_at) return;

	Station *from = Station::Get(v->last_station_loaded);
	Station *to = Station::Get(arrived_at);

	/* Update incoming route link. */
	UpdateVehicleRouteLinks(v, v->vcache.cached_cargo_mask, false, from, v->last_order_id, arrived_at, v->current_order.index, v->travel_time);

	/* Update outgoing links. */
	CargoID cid;
	FOR_EACH_SET_CARGO_ID(cid, v->vcache.cached_cargo_mask) {
		/* Skip cargo types that don't have destinations enabled. */
		if (!CargoHasDestinations(cid)) continue;

		for (RouteLinkList::iterator link = to->goods[cid].routes.begin(); link != to->goods[cid].routes.end(); ++link) {
			if ((*link)->GetOriginOrderId() == v->current_order.index) {
				(*link)->VehicleArrived();
				break;
			}
		}
	}
}

/**
 * Pre-fill the route links from the orders of a vehicle.
 * @param v The vehicle to get the orders from.
 */
void PrefillRouteLinks(const Vehicle *v)
{
	if (CargoDestinationsDisabled()) return;
	if (v->orders.list == NULL || v->orders.list->GetNumOrders() < 2) return;

	/* Can't pre-fill if the vehicle has refit or conditional orders. */
	uint count = 0;
	Order *order;
	FOR_VEHICLE_ORDERS(v, order) {
		if (order->IsType(OT_GOTO_DEPOT) && order->IsRefit()) return;
		if (order->IsType(OT_CONDITIONAL)) return;
		if ((order->IsType(OT_IMPLICIT) || order->IsType(OT_GOTO_STATION)) && (order->GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION) == 0) count++;
	}

	/* Increment count by one to account for the circular nature of the order list. */
	if (count > 0) count++;

	/* Collect cargo types carried by all vehicles in the shared order list. */
	uint32 transported_cargos = 0;
	for (Vehicle *u = v->FirstShared(); u != NULL; u = u->NextShared()) {
		transported_cargos |= u->vcache.cached_cargo_mask;
	}

	/* Loop over all orders to update/pre-fill the route links. */
	order = v->orders.list->GetFirstOrder();
	Order *prev_order = NULL;
	do {
		/* Goto station or implicit order and not a go via-order, consider as destination. */
		if ((order->IsType(OT_IMPLICIT) || order->IsType(OT_GOTO_STATION)) && (order->GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION) == 0) {
			/* Previous destination is set and the new destination is different, create/update route links. */
			if (prev_order != NULL && prev_order != order && prev_order->GetDestination() != order->GetDestination()) {
				Station *from = Station::Get(prev_order->GetDestination());
				Station *to = Station::Get(order->GetDestination());
				/* A vehicle with the speed of 128 km/h-ish would take one tick for each of the
				 * #TILE_SIZE steps per tile. For aircraft, the time needs to be scaled with the
				 * plane speed factor. */
				uint time = DistanceManhattan(from->xy, to->xy) * TILE_SIZE * 128 / v->GetDisplayMaxSpeed();
				if (v->type == VEH_AIRCRAFT) time *= _settings_game.vehicle.plane_speed;
				UpdateVehicleRouteLinks(v, transported_cargos, true, from, prev_order->index, order->GetDestination(), order->index, time);
			}

			prev_order = order;
			count--;
		}

		/* Get next order, wrap around if necessary. */
		order = order->next;
		if (order == NULL) order = v->orders.list->GetFirstOrder();
	} while (count > 0);
}

/**
 * Remove all route links to and from a station.
 * @param station Station being removed.
 */
void InvalidateStationRouteLinks(Station *station)
{
	/* Delete all outgoing links. */
	for (CargoID cid = 0; cid < NUM_CARGO; cid++) {
		for (RouteLinkList::iterator link = station->goods[cid].routes.begin(); link != station->goods[cid].routes.end(); ++link) {
			delete *link;
		}
	}

	/* Delete all incoming link. */
	Station *st_from;
	FOR_ALL_STATIONS(st_from) {
		if (st_from == station) continue;

		for (CargoID cid = 0; cid < NUM_CARGO; cid++) {
			/* Don't increment the iterator directly in the for loop as we don't want to increment when deleting a link. */
			for (RouteLinkList::iterator link = st_from->goods[cid].routes.begin(); link != st_from->goods[cid].routes.end(); ) {
				if ((*link)->GetDestination() == station->index) {
					delete *link;
					link = st_from->goods[cid].routes.erase(link);
				} else {
					++link;
				}
			}
		}
	}
}

/**
 * Remove all route links referencing an order.
 * @param order The order being removed.
 */
void InvalidateOrderRouteLinks(OrderID order)
{
	Station *st;
	FOR_ALL_STATIONS(st) {
		for (CargoID cid = 0; cid < NUM_CARGO; cid++) {
			/* Don't increment the iterator directly in the for loop as we don't want to increment when deleting a link. */
			for (RouteLinkList::iterator link = st->goods[cid].routes.begin(); link != st->goods[cid].routes.end(); ) {
				if ((*link)->GetOriginOrderId() == order || (*link)->GetDestOrderId() == order) {
					delete *link;
					link = st->goods[cid].routes.erase(link);
				} else {
					++link;
				}
			}
		}
	}
}

/** Age and expire route links of a station. */
void AgeRouteLinks(Station *st)
{
	/* Reset waiting time for all vehicles currently loading. */
	for (std::list<Vehicle *>::const_iterator v_itr = st->loading_vehicles.begin(); v_itr != st->loading_vehicles.end(); ++v_itr) {
		CargoID cid;
		FOR_EACH_SET_CARGO_ID(cid, (*v_itr)->vcache.cached_cargo_mask) {
			for (RouteLinkList::iterator link = st->goods[cid].routes.begin(); link != st->goods[cid].routes.end(); ++link) {
				if ((*link)->GetOriginOrderId() == (*v_itr)->last_order_id) (*link)->wait_time = 0;
			}
		}
	}

	for (CargoID cid = 0; cid < NUM_CARGO; cid++) {
		/* Don't increment the iterator directly in the for loop as we don't want to increment when deleting a link. */
		for (RouteLinkList::iterator link = st->goods[cid].routes.begin(); link != st->goods[cid].routes.end(); ) {
			if ((*link)->wait_time++ > _settings_game.economy.cargodest.max_route_age) {
				delete *link;
				link = st->goods[cid].routes.erase(link);
			} else {
				++link;
			}
		}
	}
}
