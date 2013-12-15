/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file destinations.cpp Definition of cargo destinations. */

#include "../stdafx.h"
#include "destinations.h"
#include "../date_func.h"

void CargoDestinations::AddSource(SourceType type, SourceID id)
{
	this->destinations[CargoSourceSink(type, id)] = DestinationList();
	this->last_update = _date;
}

void CargoDestinations::RemoveSource(SourceType type, SourceID id)
{
	this->destinations.erase(CargoSourceSink(type, id));
	this->last_update = this->last_removal = _date;
}

void CargoDestinations::AddSink(SourceType type, SourceID id)
{
	this->origins[CargoSourceSink(type, id)] = OriginList();
	this->last_update = _date;
}

void CargoDestinations::RemoveSink(SourceType type, SourceID id)
{
	this->origins.erase(CargoSourceSink(type, id));
	this->last_update = this->last_removal = _date;
}

void CargoDestinations::UpdateNumLinksExpected(CargoID cargo, Town *t)
{
	bool is_pax = IsCargoInClass(cargo, CC_PASSENGERS);
	bool is_symmetric = _settings_game.linkgraph.GetDistributionType(cargo) == DT_DEST_SYMMETRIC;
	uint max_amt = t->supplied[cargo].old_max;
	uint big_amt = is_pax ? BIG_TOWN_POP_PAX : BIG_TOWN_POP_OTHER;

	uint num_links = is_symmetric ? BASE_TOWN_LINKS_SYMM : BASE_TOWN_LINKS;
	/* Add links based on the available cargo amount. */
	num_links += min(max_amt, big_amt) / (is_pax ? SCALE_TOWN_PAX : SCALE_TOWN);
	if (max_amt > big_amt) num_links += (max_amt - big_amt) / (is_pax ? SCALE_TOWN_BIG_PAX : SCALE_TOWN_BIG);
	/* Ensure a city has at least city_town_links more than the base value.
	 * This improves the link distribution at the beginning of a game when
	 * the towns are still small. */
	if (t->larger_town) num_links = max<uint>(num_links, CITY_TOWN_LINKS + is_symmetric ? BASE_TOWN_LINKS_SYMM : BASE_TOWN_LINKS);

	/* Account for the two special links. */
	num_links += 2;

	this->destinations[CargoSourceSink(ST_TOWN, t->index)].num_links_expected = ClampToU16(num_links);
	this->last_update = _date;
}

void CargoDestinations::UpdateNumLinksExpected(CargoID cargo, Industry *ind)
{
	int i = ind->produced_cargo[0] == cargo ? 0 : 1;
	bool is_town_cargo = CargoSpec::Get(cargo)->town_effect != TE_NONE;

	uint num_links;
	/* Use different base values for symmetric cargos, cargos
	* with a town effect and all other cargos. */
	num_links = _settings_game.linkgraph.GetDistributionType(cargo) == DT_DEST_SYMMETRIC ? BASE_IND_LINKS_SYMM :
			(is_town_cargo ? BASE_IND_LINKS_TOWN : BASE_IND_LINKS);
	/* Add links based on last industry production. */
	num_links += ind->last_month_production[i] / is_town_cargo ? CARGO_SCALE_IND_TOWN : CARGO_SCALE_IND;

	/* Account for the one special link. */
	num_links++;

	this->destinations[CargoSourceSink(ST_INDUSTRY, ind->index)].num_links_expected = ClampToU16(num_links);
	this->last_update = _date;
}

const DestinationList &CargoDestinations::GetDestinations(SourceType type, SourceID id) const
{
	return this->destinations.find(CargoSourceSink(type, id))->second;
}

const OriginList &CargoDestinations::GetOrigins(SourceType type, SourceID id) const
{
	return this->origins.find(CargoSourceSink(type, id))->second;
}

void CargoDestinations::UpdateDestinations()
{
}

void CargoDestinations::Merge(const CargoDestinations &other)
{
}
