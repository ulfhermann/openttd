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
#include "../core/random_func.hpp"
#include "../company_base.h"
#include "../window_func.h"

CargoDestinations _cargo_destinations[NUM_CARGO];
const CargoSourceSink CargoDestinations::_invalid_source_sink(ST_ANY, INVALID_SOURCE);

void CargoDestinations::Initialize()
{
    for (CargoID c = 0; c != NUM_CARGO; ++c) {
        _cargo_destinations[c].cargo = c;
		_cargo_destinations[c].origins.clear();
		_cargo_destinations[c].destinations.clear();
		MemSetT(_cargo_destinations[c].origin_stations, 0, FILTER_LENGTH);
		MemSetT(_cargo_destinations[c].destination_stations, 0, FILTER_LENGTH);
    }
}

void CargoDestinations::RemoveSource(SourceType type, SourceID id)
{
    CargoSourceSink erase(type, id);
    DestinationList dests = this->destinations[erase];
    this->destinations.erase(erase);
    for (CargoSourceSink *i(dests.Begin()); i != dests.End(); ++i) {
        OriginList &origs = this->origins[*i];
        origs.Erase(origs.Find(erase));
        if (origs.Length() <= (i->type == ST_TOWN ? 1 : 0)) {
            this->AddMissingOrigin(origs, *i);
        }
    }
}

void CargoDestinations::RemoveSink(SourceType type, SourceID id)
{
    CargoSourceSink erase(type, id);
    OriginList origs = this->origins[erase];
    this->origins.erase(erase);
    for (CargoSourceSink *i(origs.Begin()); i != origs.End(); ++i) {
        DestinationList &dests = this->destinations[*i];
        dests.Erase(dests.Find(erase));
        if (dests.Length() < dests.num_links_expected) {
            this->AddMissingDestinations(dests, *i);
        }
    }
}

void CargoDestinations::UpdateDestinations(const Town *t)
{
    bool is_pax = IsCargoInClass(this->cargo, CC_PASSENGERS);
    uint max_amt = t->supplied[this->cargo].old_max;
	uint big_amt = is_pax ? BIG_TOWN_POP_PAX : BIG_TOWN_POP_OTHER;

	uint num_links = BASE_TOWN_LINKS;
	/* Add links based on the available cargo amount. */
	num_links += min(max_amt, big_amt) / (is_pax ? SCALE_TOWN_PAX : SCALE_TOWN);
	if (max_amt > big_amt) num_links += (max_amt - big_amt) / (is_pax ? SCALE_TOWN_BIG_PAX : SCALE_TOWN_BIG);
	/* Ensure a city has at least CITY_TOWN_LINKS more than the base value.
	 * This improves the link distribution at the beginning of a game when
	 * the towns are still small. */
	if (t->larger_town) num_links = max<uint>(num_links, CITY_TOWN_LINKS + BASE_TOWN_LINKS);

    CargoSourceSink self(ST_TOWN, t->index);
    DestinationList &own_destinations = this->destinations[self];
    OriginList &own_origins = this->origins[self];

    if (HasBit(t->cargo_accepted_total, this->cargo)) {
		num_links++;
		if (own_destinations.Length() == 0) {
            *own_destinations.Append() = self;
            own_origins.Include(self);
		} else if (own_destinations[0] != self) {
			*own_destinations.Append() = own_destinations[0];
			own_destinations[0] = self;
            own_origins.Include(self);
        }
	}

    own_destinations.num_links_expected = ClampToU16(num_links);
    this->AddMissingDestinations(own_destinations, self);
}

void CargoDestinations::UpdateOrigins(const Town *t)
{
    CargoSourceSink self(ST_TOWN, t->index);
    OriginList &own_origins = this->origins[self];
	if (own_origins.Length() <= 1) this->AddMissingOrigin(own_origins, self);
}

void CargoDestinations::UpdateDestinations(const Industry *ind)
{
    int i = ind->produced_cargo[0] == this->cargo ? 0 : 1;
    bool is_town_cargo = CargoSpec::Get(this->cargo)->town_effect != TE_NONE;

    uint num_links;
    /* Use different base values for symmetric cargos, cargos
    * with a town effect and all other cargos. */
    num_links = this->IsSymmetric() ? BASE_IND_LINKS_SYMM : (is_town_cargo ? BASE_IND_LINKS_TOWN : BASE_IND_LINKS);
    /* Add links based on last industry production. */
	num_links += ind->last_month_production[i] / (is_town_cargo ? CARGO_SCALE_IND_TOWN : CARGO_SCALE_IND);

	CargoSourceSink self(ST_INDUSTRY, ind->index);
    DestinationList &own_destinations = this->destinations[self];
    this->destinations[self].num_links_expected = ClampToU16(num_links);
    this->AddMissingDestinations(own_destinations, self);
}

void CargoDestinations::UpdateOrigins(const Industry *ind)
{
    CargoSourceSink self(ST_INDUSTRY, ind->index);
    OriginList &own_origins = this->origins[self];
    if (own_origins.Length() == 0) this->AddMissingOrigin(own_origins, self);
}

void CargoDestinations::UpdateDestinations(const Company *company)
{
    CargoSourceSink self(ST_HEADQUARTERS, company->index);
    DestinationList &own_destinations = this->destinations[self];

	own_destinations.num_links_expected = ClampToU16(HQ_LINKS);
    this->AddMissingDestinations(own_destinations, self);
}

void CargoDestinations::UpdateOrigins(const Company *company)
{
    CargoSourceSink self(ST_HEADQUARTERS, company->index);
    OriginList &own_origins = this->origins[self];
    if (own_origins.Length() == 0) this->AddMissingOrigin(own_origins, self);
}

void CargoDestinations::AddMissingDestinations(DestinationList &own_destinations, const CargoSourceSink &self)
{
    if (this->origins.empty()) return;
    const CargoSourceSink &last = (--this->origins.end())->first;
    while (own_destinations.Length() < own_destinations.num_links_expected) {
		CargoSourceSink chosen = this->AddLink(own_destinations, this->origins, self, last);
		if (chosen == CargoDestinations::_invalid_source_sink) break;
		this->AddSymmetric(chosen, self);
    }
	this->UpdateWindow(self);
}

void CargoDestinations::AddMissingOrigin(OriginList &own_origins, const CargoSourceSink &self)
{
    if (this->destinations.empty()) return;
	CargoSourceSink chosen = this->AddLink(own_origins, this->destinations,
			self, (--this->destinations.end())->first);
	if (chosen != CargoDestinations::_invalid_source_sink) {
		this->AddSymmetric(self, chosen);
		this->UpdateWindow(chosen);
	}
}

template<class Tmap, class Tlist>
CargoSourceSink CargoDestinations::AddLink(Tlist &own, Tmap &other, const CargoSourceSink &self, const CargoSourceSink &last)
{
	typename Tmap::iterator chosen = other.upper_bound(
				CargoSourceSink(static_cast<SourceType>(RandomRange(ST_ANY)), RandomRange(last.id)));
	uint num_candidates = other.size();
	while ((chosen->first == self || chosen->second.Contains(self)) && --num_candidates > 0) {
		if (++chosen == other.end()) chosen = other.begin();
	}
	if (num_candidates == 0) return CargoDestinations::_invalid_source_sink;
	*chosen->second.Append() = self;
	*own.Append() = chosen->first;
	return chosen->first;
}

void CargoDestinations::AddSymmetric(const CargoSourceSink &orig, const CargoSourceSink &dest)
{
	if (!this->IsSymmetric()) return;
	std::map<CargoSourceSink, DestinationList>::iterator reverse_dest(this->destinations.find(orig));
	if (reverse_dest == this->destinations.end()) return;
	std::map<CargoSourceSink, OriginList>::iterator reverse_orig(this->origins.find(dest));
	if (reverse_orig == this->origins.end()) return;
	reverse_dest->second.Include(dest);
	reverse_orig->second.Include(orig);
	this->UpdateWindow(orig);
}

void CargoDestinations::UpdateWindow(const CargoSourceSink &source_sink)
{
	switch(source_sink.type) {
	case ST_TOWN: InvalidateWindowData(WC_TOWN_VIEW, source_sink.id, 1); break;
	case ST_INDUSTRY: InvalidateWindowData(WC_INDUSTRY_VIEW, source_sink.id, 1); break;
	default: break;
	}
}

const DestinationList &CargoDestinations::GetDestinations(SourceType type, SourceID id) const
{
	return this->destinations.find(CargoSourceSink(type, id))->second;
}

const OriginList &CargoDestinations::GetOrigins(SourceType type, SourceID id) const
{
	return this->origins.find(CargoSourceSink(type, id))->second;
}

