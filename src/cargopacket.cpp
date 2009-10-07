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

CargoPacket::CargoPacket(StationID source, StationID next, uint16 count, SourceType source_type, SourceID source_id) :
	count(count),
	source_id(source_id),
	source(source),
	next(next)
{
	this->source_type = source_type;

	if (source != INVALID_STATION) {
		assert(count != 0);
		this->source_xy    = Station::Get(source)->xy;
		this->loaded_at_xy = this->source_xy;
	}
}

CargoPacket::CargoPacket(uint16 count, byte days_in_transit, Money feeder_share, SourceType source_type, SourceID source_id) :
		feeder_share(feeder_share),
		count(count),
		days_in_transit(days_in_transit),
		source_id(source_id)
{
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

/*
 *
 * Cargo list implementation
 *
 */

CargoList::~CargoList()
{
	while (!this->packets.empty()) {
		delete this->packets.front();
		this->packets.pop_front();
	}
}

void CargoList::RemoveFromCache(const CargoPacket *cp)
{
	this->count                 -= cp->count;
	this->feeder_share          -= cp->feeder_share;
	this->cargo_days_in_transit -= cp->days_in_transit * cp->count;
}

void CargoList::AddToCache(const CargoPacket *cp)
{
	this->count                 += cp->count;
	this->feeder_share          += cp->feeder_share;
	this->cargo_days_in_transit += cp->days_in_transit * cp->count;
}

void CargoList::AgeCargo()
{
	for (List::const_iterator it = this->packets.begin(); it != this->packets.end(); it++) {
		/* If we're at the maximum, then we can't increase no more. */
		if ((*it)->days_in_transit == 0xFF) continue;

		(*it)->days_in_transit++;
		this->cargo_days_in_transit += (*it)->count;
	}
}

void CargoList::Append(CargoPacket *cp)
{
	assert(cp != NULL);

	for (List::iterator it = this->packets.begin(); it != this->packets.end(); it++) {
		CargoPacket *icp = *it;
		if (icp->SameSource(cp) && icp->count + cp->count <= CargoPacket::MAX_COUNT) {
			icp->count        += cp->count;
			icp->feeder_share += cp->feeder_share;

			this->AddToCache(cp);
			delete cp;
			return;
		}
	}

	/* The packet could not be merged with another one */
	this->packets.push_back(cp);
	this->AddToCache(cp);
}

void CargoList::Import(List & list)
{
	packets.splice(packets.end(), list);
	InvalidateCache();
}

void CargoList::Truncate(uint max_remaining)
{
	for (List::iterator it = packets.begin(); it != packets.end(); /* done during loop*/) {
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

CargoPacket * CargoPacket::Split(uint new_size) {
	CargoPacket *cp_new = new CargoPacket(this->source, this->next, new_size, this->source_type, this->source_id);
	Money fs = feeder_share * new_size / static_cast<uint>(count);
	feeder_share -= fs;
	cp_new->source_xy       = this->source_xy;
	cp_new->loaded_at_xy    = this->loaded_at_xy;
	cp_new->days_in_transit = this->days_in_transit;
	cp_new->feeder_share    = fs;
	this->count -= new_size;
	return cp_new;
}

void CargoList::DeliverPacket(List::iterator & c, uint & remaining_unload, CargoPayment *payment) {
	CargoPacket * p = *c;
	if (p->count <= remaining_unload) {
		remaining_unload -= p->count;
		payment->PayFinalDelivery(p, p->count);
		delete p;
		packets.erase(c++);
	} else {
		payment->PayFinalDelivery(p, remaining_unload);
		p->count -= remaining_unload;
		remaining_unload = 0;
		++c;
	}
}

CargoPacket * CargoList::TransferPacket(List::iterator & c, uint & remaining_unload, GoodsEntry *dest, CargoPayment *payment) {
	CargoPacket * p = *c;
	if (p->count <= remaining_unload) {
		packets.erase(c++);
	} else {
		p = p->Split(remaining_unload);
		++c;
	}
	payment->PayTransfer(p, p->count);
	dest->cargo.packets.push_back(p);
	SetBit(dest->acceptance_pickup, GoodsEntry::PICKUP);
	remaining_unload -= p->count;
	return p;
}

UnloadType CargoList::WillUnload(const UnloadDescription & ul, const CargoPacket * p) const {
	if (ul.dest->flows[p->source].empty() || ul.next_station == INVALID_STATION) {
		/* there is no plan: use normal unloading */
		return WillUnloadOld(ul, p);
	} else {
		/* use cargodist unloading*/
		return WillUnloadCargoDist(ul, p);
	}
}

UnloadType CargoList::WillUnloadOld(const UnloadDescription & ul, const CargoPacket * p) const {
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

UnloadType CargoList::WillUnloadCargoDist(const UnloadDescription & ul, const CargoPacket * p) const {
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

uint CargoList::MoveToStation(GoodsEntry * dest, uint max_unload, OrderUnloadFlags flags, StationID curr_station, StationID next_station, CargoPayment *payment) {
	uint remaining_unload = max_unload;
	UnloadDescription ul(dest, curr_station, next_station, flags);

	for(List::iterator c = packets.begin(); c != packets.end() && remaining_unload > 0;) {

		CargoPacket * p = *c;
		StationID source = p->source;
		uint last_remaining = remaining_unload;
		UnloadType unload_flags = WillUnload(ul, p);

		if (unload_flags & UL_DELIVER) {
			DeliverPacket(c, remaining_unload, payment);
			dest->UpdateFlowStats(source, last_remaining - remaining_unload, curr_station);
		} else if (unload_flags & UL_TRANSFER) {
			/* TransferPacket may split the packet and return the transferred part */
			p = TransferPacket(c, remaining_unload, dest, payment);
			p->next = dest->UpdateFlowStatsTransfer(source, last_remaining - remaining_unload, curr_station);
		} else /* UL_KEEP */ {
			++c;
		}
	}

	dest->cargo.InvalidateCache();
	InvalidateCache();
	return max_unload - remaining_unload;
}

uint CargoList::LoadPackets(List * dest, uint cap, StationID next_station, List * rejected, TileIndex load_place) {
	while(!packets.empty() && cap > 0) {
		CargoPacket * p = packets.front();
		if (rejected == NULL || p->next == next_station || p->next == INVALID_STATION || next_station == INVALID_STATION) {
			/* load the packet if possible */
			if (p->count <= cap) {
				/* load all of the packet */
				packets.pop_front();
			} else {
				/* packet needs to be split */
				p = p->Split(cap);
				assert(p->count == cap);
			}
			cap -= p->count;
			dest->push_back(p);
			if (load_place != INVALID_TILE) {
				p->loaded_at_xy = load_place;
			}
		} else {
			packets.pop_front();
			rejected->push_back(p);
		}
	}
	InvalidateCache();
	return cap;
}

uint CargoList::MoveToVehicle(CargoList *dest, uint max_load, StationID next_station, List * rejected, TileIndex load_place) {
	uint space_remaining = LoadPackets(&dest->packets, max_load, next_station, rejected, load_place);
	dest->InvalidateCache();
	return max_load - space_remaining;
}

void CargoList::InvalidateCache()
{
	this->count = 0;
	this->feeder_share = 0;
	this->cargo_days_in_transit = 0;

	for (List::const_iterator it = this->packets.begin(); it != this->packets.end(); it++) {
		this->AddToCache(*it);
	}
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

void CargoList::RerouteStalePackets(StationID curr, StationID to, GoodsEntry * ge) {
	for(List::iterator it = packets.begin(); it != packets.end(); ++it) {
		CargoPacket * packet = *it;
		if (packet->next == to) {
			packet->next = ge->UpdateFlowStatsTransfer(packet->source, packet->count, curr);
		}
	}
	InvalidateCache();
}

void CargoList::UpdateFlows(StationID next, GoodsEntry * ge) {
	for(List::iterator i = packets.begin(); i != packets.end(); ++i) {
		CargoPacket * p = *i;
		ge->UpdateFlowStats(p->source, p->count, next);
		p->next = next;
	}
}
