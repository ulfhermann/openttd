/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file cargodest_base.h Classes and types for entities having cargo destinations. */

#ifndef CARGODEST_BASE_H
#define CARGODEST_BASE_H

#include "cargo_type.h"
#include "town_type.h"
#include "core/smallvec_type.hpp"

struct CargoSourceSink;

/** Information about a demand link for cargo. */
struct CargoLink {
	CargoSourceSink      *dest;      ///< Destination of the link.
	TransportedCargoStat amount;     ///< Transported cargo statistics.
	uint                 weight;     ///< Weight of this link.
	byte                 weight_mod; ///< Weight modifier.

	CargoLink(CargoSourceSink *d, byte mod = 1) : dest(d), weight(1), weight_mod(mod) {}

	/* Compare two cargo links for inequality. */
	bool operator !=(const CargoLink &other) const
	{
		return other.dest != dest;
	}
};

/** An entity producing or accepting cargo with a destination. */
struct CargoSourceSink {
	/** List of destinations for each cargo type. */
	SmallVector<CargoLink, 8> cargo_links[NUM_CARGO];
	/** Sum of the destination weights for each cargo type. */
	uint cargo_links_weight[NUM_CARGO];

	virtual ~CargoSourceSink();

	/** Get the type of this entity. */
	virtual SourceType GetType() const = 0;
	/** Get the source ID corresponding with this entity. */
	virtual SourceID GetID() const = 0;

	/**
	 * Test if a demand link to a destination exists.
	 * @param cid Cargo type for which a link should be searched.
	 * @param dest Destination to search for.
	 * @return True if a link to the destination is present.
	 */
	bool HasLinkTo(CargoID cid, const CargoSourceSink *dest) const
	{
		return this->cargo_links[cid].Contains(CargoLink(const_cast<CargoSourceSink *>(dest)));
	}

	void SaveCargoSourceSink();
	void LoadCargoSourceSink();
	void PtrsCargoSourceSink();
};

#endif /* CARGODEST_BASE_H */
