/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file cargopacket.cpp Implementation of the cargo packets */

#include "stdafx.h"
#include "core/pool_func.hpp"
#include "economy_base.h"

/* Initialize the cargopacket-pool */
CargoPacketPool _cargopacket_pool("CargoPacket");
INSTANTIATE_POOL_METHODS(CargoPacket)

/**
 * Initialize, i.e. clean, the pool with cargo packets.
 */
void InitializeCargoPackets()
{
	_cargopacket_pool.CleanPool();
}

CargoPacket::CargoPacket()
{
	this->source_type = ST_INDUSTRY;
	this->source_id   = INVALID_SOURCE;
}

/* NOTE: We have to zero memory ourselves here because we are using a 'new'
 * that, in contrary to all other pools, does not memset to 0. */
CargoPacket::CargoPacket(StationID source, TileIndex source_xy, uint16 count, SourceType source_type, SourceID source_id) :
	feeder_share(0),
	count(count),
	days_in_transit(0),
	source_id(source_id),
	source(source),
	source_xy(source_xy),
	loaded_at_xy(0)
{
	assert(count != 0);
	this->source_type  = source_type;
}

CargoPacket::CargoPacket(uint16 count, byte days_in_transit, StationID source, TileIndex source_xy, TileIndex loaded_at_xy, Money feeder_share, SourceType source_type, SourceID source_id) :
		feeder_share(feeder_share),
		count(count),
		days_in_transit(days_in_transit),
		source_id(source_id),
		source(source),
		source_xy(source_xy),
		loaded_at_xy(loaded_at_xy)
{
	assert(count != 0);
	this->source_type = source_type;
}

/**
 * Invalidates (sets source_id to INVALID_SOURCE) all cargo packets from given source
 * @param src_type type of source
 * @param src index of source
 */
/* static */ void CargoPacket::InvalidateAllFrom(SourceType src_type, SourceID src)
{
	CargoPacket *cp;
	FOR_ALL_CARGOPACKETS(cp) {
		if (cp->source_type == src_type && cp->source_id == src) cp->source_id = INVALID_SOURCE;
	}
}

/**
 * Invalidates (sets source to INVALID_STATION) all cargo packets from given station
 * @param sid the station that gets removed
 */
/* static */ void CargoPacket::InvalidateAllFrom(StationID sid)
{
	CargoPacket *cp;
	FOR_ALL_CARGOPACKETS(cp) {
		if (cp->source == sid) cp->source = INVALID_STATION;
	}
}

/*
 *
 * Cargo list implementation
 *
 */

template <class Tinst>
CargoList<Tinst>::~CargoList()
{
	for (Iterator it(this->packets.begin()); it != this->packets.end(); ++it) {
		delete *it;
	}
}

template <class Tinst>
void CargoList<Tinst>::RemoveFromCache(const CargoPacket *cp)
{
	this->count                 -= cp->count;
	this->cargo_days_in_transit -= cp->days_in_transit * cp->count;
}

template <class Tinst>
void CargoList<Tinst>::AddToCache(const CargoPacket *cp)
{
	this->count                 += cp->count;
	this->cargo_days_in_transit += cp->days_in_transit * cp->count;
}

/**
 * Tries to merge the packet with another one in the packets list.
 * if no fitting packet is found, appends it.
 * @param cp the packet to be inserted
 */
template <class Tinst>
void CargoList<Tinst>::MergeOrPush(CargoPacket *cp)
{
	for (List::reverse_iterator it(this->packets.rbegin()); it != this->packets.rend(); it++) {
		CargoPacket *icp = *it;
		if (Tinst::AreMergable(icp, cp) && icp->count + cp->count <= CargoPacket::MAX_COUNT) {
			icp->count        += cp->count;
			icp->feeder_share += cp->feeder_share;

			delete cp;
			return;
		}
	}

	/* The packet could not be merged with another one */
	this->packets.push_back(cp);
}

template <class Tinst>
void CargoList<Tinst>::Append(CargoPacket *cp)
{
	assert(cp != NULL);
	static_cast<Tinst *>(this)->AddToCache(cp);
	this->MergeOrPush(cp);
}


template <class Tinst>
void CargoList<Tinst>::Truncate(uint max_remaining)
{
	for (Iterator it(packets.begin()); it != packets.end(); /* done during loop*/) {
		CargoPacket *cp = *it;
		if (max_remaining == 0) {
			/* Nothing should remain, just remove the packets. */
			this->packets.erase(it++);
			static_cast<Tinst *>(this)->RemoveFromCache(cp);
			delete cp;
			continue;
		}

		uint local_count = cp->count;
		if (local_count > max_remaining) {
			uint diff = local_count - max_remaining;
			this->count -= diff;
			this->cargo_days_in_transit -= cp->days_in_transit * diff;
			cp->count = max_remaining;
			max_remaining = 0;
		} else {
			max_remaining -= local_count;
		}
		++it;
	}
}

/**
 * Reserves a packet for later loading and adds it to the cache.
 * @param cp the packet to be reserved
 */
void VehicleCargoList::Reserve(CargoPacket *cp)
{
	assert(cp != NULL);
	this->AddToCache(cp);
	this->reserved_count += cp->count;
	this->reserved.push_back(cp);
}

/**
 * Returns all reserved cargo to the station and removes it from the cache.
 * @param dest the station the cargo is returned to
 */
void VehicleCargoList::Unreserve(StationCargoList *dest)
{
	Iterator it(this->reserved.begin());
	while (it != this->reserved.end()) {
		CargoPacket *cp = *it;
		this->RemoveFromCache(cp);
		this->reserved_count -= cp->count;
		dest->Append(cp);
		this->reserved.erase(it++);
	}
}

/**
 * Load packets from the reservation list.
 * @params count the number of cargo to load
 * @return true if there are still packets that might be loaded from the reservation list
 */
bool VehicleCargoList::LoadReserved(uint max_move)
{
	Iterator it(this->reserved.begin());
	while (it != this->reserved.end() && max_move > 0) {
		CargoPacket *cp = *it;
		if (cp->count <= max_move) {
			/* Can move the complete packet */
			max_move -= cp->count;
			this->reserved.erase(it++);
			this->reserved_count -= cp->count;
			this->MergeOrPush(cp);
		} else {
			cp->count -= max_move;
			CargoPacket *cp_new = new CargoPacket(max_move, cp->days_in_transit, cp->source, cp->source_xy, cp->loaded_at_xy, 0, cp->source_type, cp->source_id);
			this->MergeOrPush(cp_new);
			this->reserved_count -= max_move;
			max_move = 0;
		}
	}
	return it != packets.end();
}

template <class Tinst>
template <class Tother_inst>
bool CargoList<Tinst>::MoveTo(Tother_inst *dest, uint max_move, MoveToAction mta, CargoPayment *payment, uint data)
{
	assert(mta == MTA_FINAL_DELIVERY || dest != NULL);
	assert(mta == MTA_UNLOAD || mta == MTA_CARGO_LOAD || mta == MTA_RESERVE || payment != NULL);

	Iterator it(this->packets.begin());
	while (it != this->packets.end() && max_move > 0) {
		CargoPacket *cp = *it;
		if (cp->source == data && mta == MTA_FINAL_DELIVERY) {
			/* Skip cargo that originated from this station. */
			++it;
			continue;
		}

		if (cp->count <= max_move) {
			/* Can move the complete packet */
			max_move -= cp->count;
			this->packets.erase(it++);
			static_cast<Tinst *>(this)->RemoveFromCache(cp);
			switch(mta) {
				case MTA_FINAL_DELIVERY:
					payment->PayFinalDelivery(cp, cp->count);
					delete cp;
					continue; // of the loop

				case MTA_RESERVE:
					cp->loaded_at_xy = data;
					/* this reinterpret cast is nasty. The method should be
					 * refactored to get rid of it. However, as this is only
					 * a step on the way to cargodist and the whole method is
					 * rearranged in a later step we can tolerate it to make the
					 * patches smaller.
					 * MTA_RESERVE can only happen if dest is a vehicle, so we
					 * cannot crash here. I don't know a way to assert that,
					 * though.
					 */
					reinterpret_cast<VehicleCargoList *>(dest)->Reserve(cp);
					continue;

				case MTA_CARGO_LOAD:
					cp->loaded_at_xy = data;
					break;

				case MTA_TRANSFER:
					cp->feeder_share += payment->PayTransfer(cp, cp->count);
					break;

				case MTA_UNLOAD:
					break;
			}
			dest->Append(cp);
			continue;
		}

		/* Can move only part of the packet */
		if (mta == MTA_FINAL_DELIVERY) {
			/* Final delivery doesn't need package splitting. */
			payment->PayFinalDelivery(cp, max_move);

			/* Remove the delivered data from the cache */
			uint left = cp->count - max_move;
			cp->count = max_move;
			static_cast<Tinst *>(this)->RemoveFromCache(cp);

			/* Final delivery payment pays the feeder share, so we have to
			 * reset that so it is not 'shown' twice for partial unloads. */
			cp->feeder_share = 0;
			cp->count = left;
		} else {
			/* But... the rest needs package splitting. */
			Money fs = cp->feeder_share * max_move / static_cast<uint>(cp->count);
			cp->feeder_share -= fs;
			cp->count -= max_move;

			CargoPacket *cp_new = new CargoPacket(max_move, cp->days_in_transit, cp->source, cp->source_xy, (mta == MTA_CARGO_LOAD) ? data : cp->loaded_at_xy, fs, cp->source_type, cp->source_id);
			static_cast<Tinst *>(this)->RemoveFromCache(cp_new); // this reflects the changes in cp.

			if (mta == MTA_TRANSFER) {
				/* Add the feeder share before inserting in dest. */
				cp_new->feeder_share += payment->PayTransfer(cp_new, max_move);
			}

			if (mta == MTA_RESERVE) {
				/* nasty reinterpret cast, see above */
				reinterpret_cast<VehicleCargoList *>(dest)->Reserve(cp_new);
			} else {
				dest->Append(cp_new);
			}
		}

		max_move = 0;
	}

	return it != packets.end();
}

template <class Tinst>
void CargoList<Tinst>::InvalidateCache()
{
	this->count = 0;
	this->cargo_days_in_transit = 0;

	for (ConstIterator it(this->packets.begin()); it != this->packets.end(); it++) {
		static_cast<Tinst *>(this)->AddToCache(*it);
	}
}


void VehicleCargoList::RemoveFromCache(const CargoPacket *cp)
{
	this->feeder_share -= cp->feeder_share;
	this->Parent::RemoveFromCache(cp);
}

void VehicleCargoList::AddToCache(const CargoPacket *cp)
{
	this->feeder_share += cp->feeder_share;
	this->Parent::AddToCache(cp);
}

void VehicleCargoList::AgeCargo()
{
	for (ConstIterator it(this->packets.begin()); it != this->packets.end(); it++) {
		CargoPacket *cp = *it;
		/* If we're at the maximum, then we can't increase no more. */
		if (cp->days_in_transit == 0xFF) continue;

		cp->days_in_transit++;
		this->cargo_days_in_transit += cp->count;
	}
}

void VehicleCargoList::InvalidateCache()
{
	this->feeder_share = 0;
	this->reserved_count = 0;
	this->Parent::InvalidateCache();
	for (ConstIterator it(this->reserved.begin()); it != this->reserved.end(); it++) {
		this->AddToCache(*it);
		this->reserved_count += (*it)->count;
	}
}

/**
 * Assign the cargo list to a goods entry.
 * @param station the station the cargo list is assigned to
 * @param cargo the cargo the list is assigned to
 */
void StationCargoList::AssignTo(Station *station, CargoID cargo)
{
	assert(this->station == NULL);
	assert(station != NULL && cargo != INVALID_CARGO);
	this->station = station;
	this->cargo = cargo;
}


/*
 * We have to instantiate everything we want to be usable.
 */
template class CargoList<VehicleCargoList>;
template class CargoList<StationCargoList>;

/** Autoreplace Vehicle -> Vehicle 'transfer' */
template bool CargoList<VehicleCargoList>::MoveTo(VehicleCargoList *, uint max_move, MoveToAction mta, CargoPayment *payment, uint data);
/** Cargo unloading at a station */
template bool CargoList<VehicleCargoList>::MoveTo(StationCargoList *, uint max_move, MoveToAction mta, CargoPayment *payment, uint data);
/** Cargo loading at a station */
template bool CargoList<StationCargoList>::MoveTo(VehicleCargoList *, uint max_move, MoveToAction mta, CargoPayment *payment, uint data);
