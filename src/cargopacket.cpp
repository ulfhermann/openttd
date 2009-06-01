/* $Id$ */

/** @file cargopacket.cpp Implementation of the cargo packets */

#include "stdafx.h"
#include "station_base.h"
#include "core/bitmath_func.hpp"
#include "order_type.h"
#include "core/pool_func.hpp"

/* Initialize the cargopacket-pool */
CargoPacketPool _cargopacket_pool("CargoPacket");
INSTANTIATE_POOL_METHODS(CargoPacket)

void InitializeCargoPackets()
{
	_cargopacket_pool.CleanPool();
}

CargoPacket::CargoPacket(StationID source, StationID next, uint16 count)
{
	if (source != INVALID_STATION) assert(count != 0);

	this->source          = source;
	this->next            = next;
	this->source_xy       = (source != INVALID_STATION) ? Station::Get(source)->xy : 0;
	this->loaded_at_xy    = this->source_xy;

	this->count           = count;
	this->days_in_transit = 0;
	this->feeder_share    = 0;
	this->paid_for        = false;
}

bool CargoPacket::SameSource(const CargoPacket *cp) const
{
	return this->source_xy == cp->source_xy && this->days_in_transit == cp->days_in_transit
		&& this->paid_for == cp->paid_for && this->next == cp->next;
}

/*
 *
 * Cargo list implementation
 *
 */

CargoList::~CargoList()
{
	while (!packets.empty()) {
		delete packets.front();
		packets.pop_front();
	}
}

const CargoList::List *CargoList::Packets() const
{
	return &packets;
}

void CargoList::AgeCargo()
{
	if (empty) return;

	uint dit = 0;
	for (List::const_iterator it = packets.begin(); it != packets.end(); it++) {
		if ((*it)->days_in_transit != 0xFF) (*it)->days_in_transit++;
		dit += (*it)->days_in_transit * (*it)->count;
	}
	days_in_transit = dit / count;
}

bool CargoList::Empty() const
{
	return empty;
}

uint CargoList::Count() const
{
	return count;
}

bool CargoList::UnpaidCargo() const
{
	return unpaid_cargo;
}

Money CargoList::FeederShare() const
{
	return feeder_share;
}

StationID CargoList::Source() const
{
	return source;
}

uint CargoList::DaysInTransit() const
{
	return days_in_transit;
}

void CargoList::Append(CargoPacket *cp)
{
	assert(cp != NULL);

	for (List::iterator it = packets.begin(); it != packets.end(); it++) {
		if ((*it)->SameSource(cp) && (*it)->count + cp->count <= 65535) {
			(*it)->count        += cp->count;
			(*it)->feeder_share += cp->feeder_share;
			delete cp;

			InvalidateCache();
			return;
		}
	}

	/* The packet could not be merged with another one */
	packets.push_back(cp);
	InvalidateCache();
}

void CargoList::Import(List & list)
{
	packets.splice(packets.end(), list);
	InvalidateCache();
}

void CargoList::Truncate(uint count)
{
	for (List::iterator it = packets.begin(); it != packets.end(); it++) {
		uint local_count = (*it)->count;
		if (local_count <= count) {
			count -= local_count;
			continue;
		}

		(*it)->count = count;
		count = 0;
	}

	while (!packets.empty()) {
		CargoPacket *cp = packets.back();
		if (cp->count != 0) break;
		delete cp;
		packets.pop_back();
	}

	InvalidateCache();
}

CargoPacket * CargoPacket::Split(uint new_size) {
	CargoPacket *cp_new = new CargoPacket(source, next, new_size);
	Money fs = feeder_share * new_size / static_cast<uint>(count);
	feeder_share -= fs;
	cp_new->source_xy       = source_xy;
	cp_new->loaded_at_xy    = loaded_at_xy;
	cp_new->days_in_transit = days_in_transit;
	cp_new->paid_for        = paid_for;
	cp_new->feeder_share    = fs;
	count -= new_size;
	return cp_new;
}

void CargoList::DeliverPacket(List::iterator & c, uint & remaining_unload) {
	CargoPacket * p = *c;
	if (p->count <= remaining_unload) {
		remaining_unload -= p->count;
		delete p;
		packets.erase(c++);
	} else {
		p->count -= remaining_unload;
		remaining_unload = 0;
		++c;
	}
}

CargoPacket * CargoList::TransferPacket(List::iterator & c, uint & remaining_unload, GoodsEntry * dest) {
	CargoPacket * p = *c;
	if (p->count <= remaining_unload) {
		packets.erase(c++);
	} else {
		p = p->Split(remaining_unload);
		++c;
	}
	dest->cargo.packets.push_back(p);
	SetBit(dest->acceptance_pickup, GoodsEntry::PICKUP);
	remaining_unload -= p->count;
	return p;
}

UnloadType CargoList::WillUnload(const UnloadDescription & ul, const CargoPacket * p) const {
	if (p->next != ul.curr_station || ul.dest->flows[p->source].empty()) {
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
			/* vehicle goes to the packet's next hop, keep the packet*/
			return UL_KEEP;
		} else {
			/* vehicle goes somewhere else, transfer the packet*/
			return UL_TRANSFER;
		}
	}
}

uint CargoList::MoveToStation(GoodsEntry * dest, uint max_unload, OrderUnloadFlags flags, StationID curr_station, StationID next_station) {
	uint remaining_unload = max_unload;
	UnloadDescription ul(dest, curr_station, next_station, flags);

	for(List::iterator c = packets.begin(); c != packets.end() && remaining_unload > 0;) {

		CargoPacket * p = *c;
		StationID source = p->source;
		uint last_remaining = remaining_unload;
		UnloadType unload_flags = WillUnload(ul, p);

		if (unload_flags & UL_DELIVER) {
			DeliverPacket(c, remaining_unload);
			dest->UpdateFlowStats(source, last_remaining - remaining_unload, curr_station);
		} else if (unload_flags & UL_TRANSFER) {
			/* TransferPacket may split the packet and return the transferred part */
			p = TransferPacket(c, remaining_unload, dest);
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
				p->paid_for = false;
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
	empty = packets.empty();
	count = 0;
	unpaid_cargo = false;
	feeder_share = 0;
	source = INVALID_STATION;
	days_in_transit = 0;

	if (empty) return;

	uint dit = 0;
	for (List::const_iterator it = packets.begin(); it != packets.end(); it++) {
		count        += (*it)->count;
		unpaid_cargo |= !(*it)->paid_for;
		dit          += (*it)->days_in_transit * (*it)->count;
		feeder_share += (*it)->feeder_share;
	}
	days_in_transit = dit / count;
	source = (*packets.begin())->source;
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
