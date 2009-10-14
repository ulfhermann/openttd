/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file cargopacket.cpp Implementation of the cargo packets */

#include "stdafx.h"
#include "station_base.h"
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

CargoPacket::CargoPacket(uint16 count, byte days_in_transit, Money feeder_share, TileIndex source_xy, SourceType source_type, SourceID source_id) :
		feeder_share(feeder_share),
		count(count),
		days_in_transit(days_in_transit),
		source_id(source_id),
		source_xy(source_xy)
{
	this->source_type = source_type;
}

/**
 * Merge another packet into this one and delete the other one.
 * @param other the other packet
 */
void CargoPacket::Merge(CargoPacket *other)
{
	this->count += other->count;
	this->feeder_share += other->feeder_share;
	delete other;
}

CargoPacket * CargoPacket::Split(uint new_size) {
	CargoPacket *cp_new = new CargoPacket(this->source, new_size, this->source_type, this->source_id);
	Money fs = feeder_share * new_size / static_cast<uint>(count);
	feeder_share -= fs;
	cp_new->source_xy       = this->source_xy;
	cp_new->loaded_at_xy    = this->loaded_at_xy;
	cp_new->days_in_transit = this->days_in_transit;
	cp_new->feeder_share    = fs;
	this->count -= new_size;
	return cp_new;
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

template<class Tlist>
CargoList<Tlist>::~CargoList()
{
	for (Iterator it(this->packets.begin()); it != this->packets.end(); ++it) {
		delete *it;
	}
}

template<class Tlist>
void CargoList<Tlist>::RemoveFromCache(const CargoPacket *cp)
{
	this->count                 -= cp->count;
	this->feeder_share          -= cp->feeder_share;
	this->cargo_days_in_transit -= cp->days_in_transit * cp->count;
}

template<class Tlist>
void CargoList<Tlist>::AddToCache(const CargoPacket *cp)
{
	this->count                 += cp->count;
	this->feeder_share          += cp->feeder_share;
	this->cargo_days_in_transit += cp->days_in_transit * cp->count;
}

template<class Tlist>
void CargoList<Tlist>::Truncate(uint max_remaining)
{
	for (Iterator it = this->packets.begin(); it != this->packets.end(); /* done during loop */) {
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

template<class Tlist>
uint CargoList<Tlist>::MovePacket(VehicleCargoList *dest, Iterator &it, uint cap, TileIndex load_place)
{
	CargoPacket *packet = MovePacket(it, cap, load_place);
	uint ret = packet->count;
	dest->Append(packet);
	return ret;
}

template<class Tlist>
uint CargoList<Tlist>::MovePacket(StationCargoList *dest, StationID next, Iterator &it, uint cap, TileIndex load_place)
{
	CargoPacket *packet = MovePacket(it, cap, load_place);
	uint ret = packet->count;
	dest->Append(next, packet);
	return ret;
}

template<class Tlist>
CargoPacket *CargoList<Tlist>::MovePacket(Iterator &it, uint cap, TileIndex load_place)
{
	CargoPacket *packet = *it;
	/* load the packet if possible */
	if (packet->count > cap) {
		/* packet needs to be split */
		packet = packet->Split(cap);
		assert(packet->count == cap);
		++it;
	} else {
		this->packets.erase(it++);
	}
	RemoveFromCache(packet);
	if (load_place != INVALID_TILE) {
		packet->loaded_at_xy = load_place;
	}
	return packet;
}

template<class Tlist>
void CargoList<Tlist>::UpdateFlows(StationID next, GoodsEntry * ge) {
	for(Iterator i = packets.begin(); i != packets.end(); ++i) {
		CargoPacket * p = *i;
		ge->UpdateFlowStats(p->source, p->count, next);
	}
}

template<class Tlist>
void CargoList<Tlist>::InvalidateCache()
{
	this->count = 0;
	this->feeder_share = 0;
	this->cargo_days_in_transit = 0;

	for (ConstIterator it = this->packets.begin(); it != this->packets.end(); it++) {
		this->AddToCache(*it);
	}
}

/*
 *
 * Vehicle cargo list implementation
 *
 */

uint VehicleCargoList::DeliverPacket(Iterator &c, uint remaining_unload, GoodsEntry *dest, CargoPayment *payment, StationID curr_station) {
	CargoPacket * p = *c;
	uint loaded = 0;
	StationID source = p->source;
	if (p->count <= remaining_unload) {
		payment->PayFinalDelivery(p, p->count);
		packets.erase(c++);
		RemoveFromCache(p);
		loaded = p->count;
		delete p;
	} else {
		payment->PayFinalDelivery(p, remaining_unload);
		this->count -= remaining_unload;
		this->cargo_days_in_transit -= remaining_unload * p->days_in_transit;
		p->count -= remaining_unload;
		loaded = remaining_unload;
		++c;
	}
	dest->UpdateFlowStats(source, loaded, curr_station);
	return loaded;
}

uint VehicleCargoList::TransferPacket(Iterator &c, uint remaining_unload, GoodsEntry *dest, CargoPayment *payment, StationID curr_station) {
	CargoPacket *p = *c;
	Money fs = payment->PayTransfer(p, p->count);
	p->feeder_share += fs;
	this->feeder_share += fs;
	StationID next = dest->UpdateFlowStatsTransfer(p->source, p->count, curr_station);
	SetBit(dest->acceptance_pickup, GoodsEntry::PICKUP);
	return MovePacket(&dest->cargo, next, c, remaining_unload);
}

UnloadType VehicleCargoList::WillUnload(const UnloadDescription & ul, const CargoPacket * p) const {
	if (ul.dest->flows[p->source].empty() || ul.next_station == INVALID_STATION) {
		/* there is no plan: use normal unloading */
		return WillUnloadOld(ul, p);
	} else {
		/* use cargodist unloading*/
		return WillUnloadCargoDist(ul, p);
	}
}

UnloadType VehicleCargoList::WillUnloadOld(const UnloadDescription & ul, const CargoPacket * p) const {
	/* try to unload cargo */
	bool move = (ul.flags & (UL_DELIVER | UL_ACCEPTED | UL_TRANSFER)) != 0;
	/* try to deliver cargo if unloading */
	bool deliver = (ul.flags & UL_ACCEPTED) && !(ul.flags & UL_TRANSFER) && (p->source != ul.curr_station);
	/* transfer cargo if delivery was unsuccessful */
	bool transfer = (ul.flags & (UL_TRANSFER | UL_DELIVER)) != 0;
	if (move) {
		if(deliver) {
			return UL_DELIVER;
		} else if (transfer) {
			return UL_TRANSFER;
		} else {
			/* this case is for (non-)delivery to the source station without special flags.
			 * like the code in MoveTo did, we keep the packet in this case
			 */
			return UL_KEEP;
		}
	} else {
		return UL_KEEP;
	}
}

UnloadType VehicleCargoList::WillUnloadCargoDist(const UnloadDescription & ul, const CargoPacket * p) const {
	StationID via = ul.dest->flows[p->source].begin()->via;
	if (via == ul.curr_station) {
		/* this is the final destination, deliver ... */
		if (ul.flags & UL_TRANSFER) {
			/* .. except if explicitly told not to do so ... */
			return UL_TRANSFER;
		} else if (ul.flags & UL_ACCEPTED) {
			return UL_DELIVER;
		} else if (ul.flags & UL_DELIVER) {
			/* .. or if the station suddenly doesn't accept our cargo, but we have an explicit deliver order... */
			return UL_TRANSFER;
		} else {
			/* .. or else if it doesn't accept. */
			return UL_KEEP;
		}
	} else {
		/* packet has to travel on, find out if it can stay on board */
		if (ul.flags & UL_DELIVER) {
			/* order overrides cargodist:
			 * play by the old loading rules here as player is interfering with cargodist
			 * try to deliver, as move has been forced upon us */
			if ((ul.flags & UL_ACCEPTED) && !(ul.flags & UL_TRANSFER) && p->source != ul.curr_station) {
				return UL_DELIVER;
			} else {
				/* transfer cargo, as delivering didn't work */
				/* plan might still be fulfilled as the packet can be picked up by another vehicle travelling to "via" */
				return UL_TRANSFER;
			}
		} else if (ul.flags & UL_TRANSFER) {
			/* transfer forced, plan still fulfilled as above */
			return UL_TRANSFER;
		} else if (ul.next_station == via) {
			/* vehicle goes to the packet's next hop or has nondeterministic order: keep the packet*/
			return UL_KEEP;
		} else {
			/* vehicle goes somewhere else, transfer the packet*/
			return UL_TRANSFER;
		}
	}
}

uint VehicleCargoList::MoveToStation(GoodsEntry * dest, uint max_unload, OrderUnloadFlags flags, StationID curr_station, StationID next_station, CargoPayment *payment) {
	uint remaining_unload = max_unload;
	UnloadDescription ul(dest, curr_station, next_station, flags);

	for(Iterator c = packets.begin(); c != packets.end() && remaining_unload > 0;) {
		UnloadType action = this->WillUnload(ul, *c);

		switch(action) {
			case UL_DELIVER:
				remaining_unload -= this->DeliverPacket(c, remaining_unload, dest, payment, curr_station);
				break;
			case UL_TRANSFER:
				/* TransferPacket may split the packet and return the transferred part */
				remaining_unload -= this->TransferPacket(c, remaining_unload, dest, payment, curr_station);
				break;
			case UL_KEEP:
				/* don't update the flow stats here as those packets can be kept multiple times
				 * the flow stats are updated from LoadUnloadVehicle when all loading is done
				 */
				++c;
				break;
			default:
				NOT_REACHED();
		}
	}
	return max_unload - remaining_unload;
}

void VehicleCargoList::AgeCargo()
{
	CargoPacketSet new_packets;
	CargoPacket *last = NULL;
	for (Iterator it = this->packets.begin(); it != this->packets.end();) {
		CargoPacket *cp = *it;
		this->packets.erase(it++);
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
	this->packets.swap(new_packets);
}

void VehicleCargoList::Append(CargoPacket *cp)
{
	AddToCache(cp);
	Iterator it(this->packets.lower_bound(cp));
	CargoPacket *icp;
	if (it != this->packets.end() && cp->SameSource(icp = *it)) {
		/* there aren't any vehicles able to carry that amount of cargo */
		assert(cp->count + icp->count <= CargoPacket::MAX_COUNT);
		icp->Merge(cp);
	} else {
		/* The packet could not be merged with another one */
		this->packets.insert(it, cp);
	}
}

uint VehicleCargoList::MoveToVehicle(VehicleCargoList *dest, uint cap, TileIndex load_place)
{
	uint orig_cap = cap;
	Iterator it = packets.begin();
	while(it != packets.end() && cap > 0) {
		cap -= MovePacket(dest, it, cap, load_place);
	}
	return orig_cap - cap;
}

/**
 * Invalidates all cargo packets from the given source in all vehicles.
 * @see CargoPacket::InvalidateAllFrom(SourceType src_type, SourceID src)
 * @param src_type type of source
 * @param src index of source
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

/**
 * Convert the packets from a std::set<void *> (which has been created by the saveload system)
 * to a std::set<void *, PacketCompare>, then build the cache.
 * This involves moving all packets to a temporary set, calling the destructor of the std::set<void *> manually,
 * placement-creating a new set and swapping the temporary set in.
 * It is assumed that sizeof(std::set<void *>) == sizeof(std::set<CargoPacket *, PacketCompare>), but if this doesn't
 * hold we probably get problems with loading lists, too.
 */
void VehicleCargoList::SortAndCache() {
	typedef std::set<CargoPacket *> UnsortedSet;
	UnsortedSet &unsorted = (UnsortedSet &)this->packets;

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
	CargoPacketSet *my_packets = new (&this->packets) CargoPacketSet; // allocate a new set of the correct type there
	my_packets->swap(new_packets);
	assert(*my_packets == this->packets);

	InvalidateCache();
}

UnloadDescription::UnloadDescription(GoodsEntry * d, StationID curr, StationID next, OrderUnloadFlags order_flags) :
	dest(d), curr_station(curr), next_station(next), flags(UL_KEEP)
{
	if (HasBit(dest->acceptance_pickup, GoodsEntry::ACCEPTANCE)) {
		flags |= UL_ACCEPTED;
	}
	if (order_flags & OUFB_UNLOAD) {
		flags |= UL_DELIVER;
	}
	if (order_flags & OUFB_TRANSFER) {
		flags |= UL_TRANSFER;
	}
}

/**
 * compares the given packets by the same principles as SameSource, but creates a strict weak ordering
 * useful for std::set.
 * @param a the first cargo packet to compare
 * @param b the second cargo packet to compare
 * @return if a < b according to source_xy, source_type, source_id and days_in_transit in this order.
 */
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

/*
 *
 * Station cargo list implementation
 *
 */

void StationCargoList::Append(StationID next, CargoPacket *cp)
{
	assert(cp != NULL);
	this->AddToCache(cp);
	StationCargoPacketMap::List &list = this->packets[next];
	if (list.empty()) {
		list.push_back(cp);
	} else {
		CargoPacket *icp = list.back();
		if (icp->SameSource(cp) && icp->count + cp->count <= CargoPacket::MAX_COUNT) {
			icp->Merge(cp);
		} else {
			list.push_back(cp);
		}
	}
}

uint StationCargoList::MovePackets(VehicleCargoList *dest, uint cap, Iterator begin, Iterator end, TileIndex load_place) {
	uint orig_cap = cap;
	while(begin != end && cap > 0) {
		cap -= MovePacket(dest, begin, cap, load_place);
	}
	return orig_cap - cap;
}

uint StationCargoList::MoveToVehicle(VehicleCargoList *dest, uint cap, StationID selected_station, TileIndex load_place) {
	uint orig_cap = cap;
	if (selected_station != INVALID_STATION) {
		std::pair<Iterator, Iterator> bounds(packets.equal_range(selected_station));
		cap -= MovePackets(dest, cap, bounds.first, bounds.second, load_place);
		if (cap > 0) {
			bounds = packets.equal_range(INVALID_STATION);
			cap -= MovePackets(dest, cap, bounds.first, bounds.second, load_place);
		}
	} else {
		cap -= MovePackets(dest, cap, packets.begin(), packets.end(), load_place);
	}
	return orig_cap - cap;
}

void StationCargoList::RerouteStalePackets(StationID curr, StationID to, GoodsEntry * ge) {
	std::pair<Iterator, Iterator> range(packets.equal_range(to));
	for(Iterator it(range.first); it != range.second && it.GetKey() == to;) {
		CargoPacket * packet = *it;
		packets.erase(it++);
		StationID next = ge->UpdateFlowStatsTransfer(packet->source, packet->count, curr);
		assert(next != to);

		/* legal, as insert doesn't invalidate iterators in the MultiMap, however
		 * this might insert the packet between range.first and range.second (which might be end())
		 * This is why we check for GetKey above to avoid infinite loops
		 */
		packets.Insert(next, packet);
	}
}

/**
 * Invalidates all cargo packets from the given source in all stations.
 * @see CargoPacket::InvalidateAllFrom(SourceType src_type, SourceID src)
 * @param src_type type of source
 * @param src index of source
 */
/* static */ void StationCargoList::InvalidateAllFrom(SourceType src_type, SourceID src)
{
	Station *st;
	FOR_ALL_STATIONS(st) {
		for (CargoID c = 0; c != NUM_CARGO; ++c) {
			StationCargoPacketMap &packets = st->goods[c].cargo.packets;
			for (Iterator it = packets.begin(); it != packets.end(); ++it) {
				CargoPacket *cp = *it;
				if (cp->source_type == src_type && cp->source_id == src) cp->source_id = INVALID_SOURCE;
			}
		}
	}
}

/*
 * stupid workaround to make the compiler recognize the template instances
 */
template class CargoList<CargoPacketSet>;
template class CargoList<StationCargoPacketMap>;
