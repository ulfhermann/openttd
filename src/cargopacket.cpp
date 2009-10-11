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
#include "station_base.h"
#include "vehicle_base.h"

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

CargoPacket::CargoPacket(StationID source, uint16 count, SourceType source_type, SourceID source_id) :
	count(count),
	source_id(source_id),
	source(source)
{
	this->source_type = source_type;

	if (source != INVALID_STATION) {
		assert(count != 0);
		this->source_xy    = Station::Get(source)->xy;
		this->loaded_at_xy = this->source_xy;
	}
}

CargoPacket::CargoPacket(SourceType source_type, SourceID source_id, TileIndex source_xy, uint16 count, byte days_in_transit, Money feeder_share) :
		feeder_share(feeder_share),
		count(count),
		days_in_transit(days_in_transit),
		source_id(source_id),
		source_xy(source_xy)
{
	this->source_type = source_type;
}

void CargoPacket::Merge(CargoPacket *other)
{
	this->count += other->count;
	this->feeder_share += other->feeder_share;
	delete other;
}

/**
 * Invalidates (sets source_id to INVALID_SOURCE) all cargo packets from given source
 * @param src_type type of source
 * @param src index of source
 */
/* static */ void CargoPacket::InvalidateAllFrom(SourceType src_type, SourceID src)
{
	VehicleCargoList::InvalidateAllFrom(src_type, src);
	StationCargoList::InvalidateAllFrom(src_type, src);
}

/*
 *
 * Cargo list implementation
 *
 */

/* static */ void VehicleCargoList::InvalidateAllFrom(SourceType src_type, SourceID src)
{
	Vehicle *v;
	FOR_ALL_VEHICLES(v) {
		CargoPacketSet &packets = v->cargo.packets;
		for (Iterator it = packets.begin(); it != packets.end();) {
			CargoPacket *p = *it;
			if (p->source_type == src_type && p->source_id == src) {
				packets.erase(it++);
				p->source_id = INVALID_SOURCE;
				packets.insert(p);
			} else {
				++it;
			}
		}
	}
}

/* static */ void StationCargoList::InvalidateAllFrom(SourceType src_type, SourceID src)
{
	Station *st;
	FOR_ALL_STATIONS(st) {
		for (CargoID c = 0; c != NUM_CARGO; ++c) {
			CargoPacketList &packets = st->goods[c].cargo.packets;
			for (Iterator it = packets.begin(); it != packets.end(); ++it) {
				CargoPacket *cp = *it;
				if (cp->source_type == src_type && cp->source_id == src) cp->source_id = INVALID_SOURCE;
			}
		}
	}
}

template<class LIST>
CargoList<LIST>::~CargoList()
{
	for (Iterator it(this->packets.begin()); it != this->packets.end(); ++it) {
		delete *it;
	}
}

template<class LIST>
void CargoList<LIST>::RemoveFromCache(const CargoPacket *cp)
{
	this->count                 -= cp->count;
	this->feeder_share          -= cp->feeder_share;
	this->cargo_days_in_transit -= cp->days_in_transit * cp->count;
}

template<class LIST>
void CargoList<LIST>::AddToCache(const CargoPacket *cp)
{
	this->count                 += cp->count;
	this->feeder_share          += cp->feeder_share;
	this->cargo_days_in_transit += cp->days_in_transit * cp->count;
}

void VehicleCargoList::AgeCargo()
{
	CargoPacketSet new_packets;
	CargoPacket *last = NULL;
	for (Iterator it = packets.begin(); it != packets.end();) {
		CargoPacket *cp = *it;
		packets.erase(it++);
		if (cp->days_in_transit != 0xFF) {
			cp->days_in_transit++;
			this->cargo_days_in_transit += cp->count;
		} else if (last != NULL && last->SameSource(cp)) {
			/* there are no vehicles with > MAX_COUNT capacity,
			 * so we don't have to check for overflow here */
			assert(last->count + cp->count <= CargoPacket::MAX_COUNT);
			last->Merge(cp);
			continue;
		}

		/* hinting makes this a constant time operation */
		new_packets.insert(new_packets.end(), cp);
		last = cp;
	}
	/* this is constant time, too */
	packets.swap(new_packets);
}

void StationCargoList::Append(CargoPacket *cp)
{
	assert(cp != NULL);

	this->AddToCache(cp);
	if (count > 0) {
		CargoPacket *icp = this->packets.back();
		if (icp->SameSource(cp) && icp->count + cp->count <= CargoPacket::MAX_COUNT) {
			icp->Merge(cp);
			return;
		}
	}

	/* The packet could not be merged with another one */
	this->packets.push_back(cp);
}

void VehicleCargoList::Append(CargoPacket *cp)
{
	AddToCache(cp);
	Iterator it(packets.lower_bound(cp));
	CargoPacket *icp;
	if (it != packets.end() && cp->SameSource(icp = *it)) {
		/* there aren't any vehicles able to carry that amount of cargo */
		assert(cp->count + icp->count <= CargoPacket::MAX_COUNT);
		icp->Merge(cp);
	} else {
		/* The packet could not be merged with another one */
		packets.insert(it, cp);
	}
}

template<class LIST>
void CargoList<LIST>::Truncate(uint max_remaining)
{
	for (Iterator it = packets.begin(); it != packets.end(); /* done during loop*/) {
		CargoPacket *cp = *it;
		if (max_remaining == 0) {
			/* Nothing should remain, just remove the packets. */
			packets.erase(it++);
			this->RemoveFromCache(cp);
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

template<class LIST>
template<class OTHERLIST>
bool CargoList<LIST>::MoveTo(OTHERLIST *dest, uint max_move, MoveToAction mta, CargoPayment *payment, uint data)
{
	assert(mta == MTA_FINAL_DELIVERY || dest != NULL);
	assert(mta == MTA_UNLOAD || mta == MTA_CARGO_LOAD || payment != NULL);

	Iterator it = packets.begin();
	while (it != packets.end() && max_move > 0) {
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
			this->RemoveFromCache(cp);
			switch(mta) {
				case MTA_FINAL_DELIVERY:
					payment->PayFinalDelivery(cp, cp->count);
					delete cp;
					continue; // of the loop

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
			this->count -= max_move;
			this->cargo_days_in_transit -= max_move * cp->days_in_transit;

			/* Final delivery payment pays the feeder share, so we have to
			 * reset that so it is not 'shown' twice for partial unloads. */
			this->feeder_share -= cp->feeder_share;
			cp->feeder_share = 0;
		} else {
			/* But... the rest needs package splitting. */
			Money fs = cp->feeder_share * max_move / static_cast<uint>(cp->count);
			cp->feeder_share -= fs;

			CargoPacket *cp_new = new CargoPacket(cp->source_type, cp->source_id, cp->source_xy, max_move, cp->days_in_transit, fs);

			cp_new->source          = cp->source;
			cp_new->loaded_at_xy    = (mta == MTA_CARGO_LOAD) ? data : cp->loaded_at_xy;

			this->RemoveFromCache(cp_new); // this reflects the changes in cp.

			if (mta == MTA_TRANSFER) {
				/* Add the feeder share before inserting in dest. */
				cp_new->feeder_share += payment->PayTransfer(cp_new, max_move);
			}

			dest->Append(cp_new);
		}
		cp->count -= max_move;

		max_move = 0;
	}

	return it != packets.end();
}

void VehicleCargoList::SortAndCache() {
	typedef std::set<CargoPacket *> UnsortedSet;
	UnsortedSet &unsorted = (UnsortedSet &)packets;

	CargoPacketSet new_packets;
	for(UnsortedSet::iterator it = unsorted.begin(); it != unsorted.end(); ++it) {
		CargoPacket *cp = *it;
		CargoPacketSet::iterator new_it = new_packets.find(cp);
		if (new_it != new_packets.end()) {
			(*new_it)->Merge(cp);
		} else {
			new_packets.insert(cp);
		}
	}

	unsorted.~UnsortedSet(); // destroy whatever may be left
	CargoPacketSet *my_packets = new (&packets) CargoPacketSet; // allocate a new set of the correct type there
	my_packets->swap(new_packets);
	assert(*my_packets == packets);

	InvalidateCache();
}

template<class LIST>
void CargoList<LIST>::InvalidateCache()
{
	this->count = 0;
	this->feeder_share = 0;
	this->cargo_days_in_transit = 0;

	for (ConstIterator it = this->packets.begin(); it != this->packets.end(); it++) {
		this->AddToCache(*it);
	}
}

template<class LIST>
void CargoList<LIST>::ValidateCache() {
	uint p_count = this->count;
	Money p_feeder = this->feeder_share;
	uint p_days = this->cargo_days_in_transit;

	InvalidateCache();
	assert(p_count == this->count);
	assert(p_feeder == this->feeder_share);
	assert(p_days == this->cargo_days_in_transit);
}

bool PacketCompare::operator()(const CargoPacket *a, const CargoPacket *b) const {
	if (a->GetSourceXY() == b->GetSourceXY()) {
		if(a->GetSourceType() == b->GetSourceType()) {
			if (a->GetSourceID() == b->GetSourceID()) {
				/* it's important to check this last to make the merging in AgeCargo work. */
				return a->DaysInTransit() < b->DaysInTransit();
			} else {
				return a->GetSourceID() < b->GetSourceID();
			}
		} else {
			return a->GetSourceType() < b->GetSourceType();
		}
	} else {
		return a->GetSourceXY() < b->GetSourceXY();
	}
}

/* stupid workaround to make the compiler recognize the template instances */
template class CargoList<CargoPacketSet>;
template class CargoList<CargoPacketList>;

template bool CargoList<CargoPacketSet>::MoveTo(VehicleCargoList *, uint max_move, MoveToAction mta, CargoPayment *payment, uint data);
template bool CargoList<CargoPacketSet>::MoveTo(StationCargoList *, uint max_move, MoveToAction mta, CargoPayment *payment, uint data);
template bool CargoList<CargoPacketList>::MoveTo(VehicleCargoList *, uint max_move, MoveToAction mta, CargoPayment *payment, uint data);
template bool CargoList<CargoPacketList>::MoveTo(StationCargoList *, uint max_move, MoveToAction mta, CargoPayment *payment, uint data);

