/* $Id$ */

/** @file cargopacket.cpp Implementation of the cargo packets */

#include "stdafx.h"
#include "station_base.h"
#include "core/bitmath_func.hpp"
#include "oldpool_func.h"
#include "order_type.h"

/* Initialize the cargopacket-pool */
DEFINE_OLD_POOL_GENERIC(CargoPacket, CargoPacket)

void InitializeCargoPackets()
{
	/* Clean the cargo packet pool and create 1 block in it */
	_CargoPacket_pool.CleanPool();
	_CargoPacket_pool.AddBlockToPool();
}

CargoPacket::CargoPacket(StationID source, StationID next, uint16 count)
{
	if (source != INVALID_STATION) assert(count != 0);

	this->source          = source;
	this->next            = next;
	this->source_xy       = (source != INVALID_STATION) ? GetStation(source)->xy : 0;
	this->loaded_at_xy    = this->source_xy;

	this->count           = count;
	this->days_in_transit = 0;
	this->feeder_share    = 0;
	this->paid_for        = false;
}

CargoPacket::~CargoPacket()
{
	this->count = 0;
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
	assert(cp->IsValid());

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

void CargoList::TransferPacket(List::iterator & c, uint & remaining_unload, GoodsEntry * dest) {
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
}

void CargoList::UnloadPacketOld(UnloadDescription & ul, List::iterator & c, uint & remaining_unload) {
	CargoPacket * p = *c;
	p->next = INVALID_STATION;

	/* try to unload cargo */
	bool move = ul.flags & (OUFB_UNLOAD | OUF_UNLOAD_IF_POSSIBLE);
	/* try to deliver cargo if unloading */
	bool deliver = (ul.flags & OUF_UNLOAD_IF_POSSIBLE) && !(ul.flags & OUFB_TRANSFER) && (p->source != ul.curr_station);
	/* transfer cargo if delivery was unsuccessful */
	bool transfer = ul.flags & (OUFB_TRANSFER | OUFB_UNLOAD);

	if (move) {
		if(deliver) {
			DeliverPacket(c, remaining_unload);
		} else if (transfer) {
			TransferPacket(c, remaining_unload, ul.dest);
		} else {
			/* this case is for (non-)delivery to the source station without special flags.
			 * like the code in MoveTo did, we keep the packet in this case
			 */
			++c;
		}
	} else {
		++c;
	}
}

void CargoList::UnloadPacketCargoDist(UnloadDescription & ul, List::iterator & c, uint & remaining_unload) {
	CargoPacket * p = *c;
	uint last_remaining = remaining_unload;

	FlowStatSet & flow_stats = ul.dest->flows[p->source];
	FlowStatSet::iterator flow_it = flow_stats.begin();
	StationID via = flow_it->via;
	bool plan_fulfilled = true;

	if (via == ul.curr_station) {
		/* this is the final destination, deliver ... */
		p->next = INVALID_STATION;
		if (ul.flags & OUFB_TRANSFER) {
			/* .. except if explicitly told not to do so ... */
			TransferPacket(c, remaining_unload, ul.dest);
			plan_fulfilled = false;
		} else if (ul.flags & OUF_UNLOAD_IF_POSSIBLE) {
			DeliverPacket(c, remaining_unload);
		} else {
			/* .. or if the station suddenly doesn't accept our cargo. */
			++c;
			plan_fulfilled = false;
		}
	} else {
		p->next = via;
		/* packet has to travel on, find out if it can stay on board */
		if (ul.flags & OUFB_UNLOAD) {
			/* order overrides cargodist:
			 * play by the old loading rules here as player is interfering with cargodist
			 * try to deliver, as move has been forced upon us */
			if ((ul.flags & OUF_UNLOAD_IF_POSSIBLE) && !(ul.flags & OUFB_TRANSFER) &&	p->source != ul.curr_station) {
				DeliverPacket(c, remaining_unload);
				plan_fulfilled = false;
			} else {
				/* transfer cargo, as unloading didn't work
				 * transfer condition doesn't need to be checked as MTF_FORCE_MOVE is known to be set
				 * the plan is still fulfilled as the packet can be picked up by another vehicle travelling to via
				 */
				TransferPacket(c, remaining_unload, ul.dest);
			}
		} else if (ul.next_station == via) {
			/* vehicle goes to the packet's next hop, keep the packet*/
			++c;
		} else {
			/* vehicle goes somewhere else, transfer the packet*/
			TransferPacket(c, remaining_unload, ul.dest);
		}
	}

	if(plan_fulfilled) {
		uint planned = flow_it->planned;
		uint sent = flow_it->sent + last_remaining - remaining_unload;
		flow_stats.erase(flow_it);
		flow_stats.insert(FlowStat(via, planned, sent));
	}
}

uint CargoList::MoveToStation(GoodsEntry * dest, uint max_unload, uint flags, StationID curr_station, StationID next_station) {
	uint remaining_unload = max_unload;
	UnloadDescription ul(dest, curr_station, next_station, flags);

	for(List::iterator c = packets.begin(); c != packets.end() && remaining_unload > 0;) {
		CargoPacket * p = *c;
		if (p->next != curr_station || dest->flows[p->source].empty()) {
			/* there is no plan: use normal unloading */
			UnloadPacketOld(ul, c, remaining_unload);
		} else {
			/* use cargodist unloading*/
			UnloadPacketCargoDist(ul, c, remaining_unload);
		}
	}

	dest->cargo.InvalidateCache();
	InvalidateCache();
	return max_unload - remaining_unload;
}

uint CargoList::MoveToVehicle(CargoList *dest, uint max_load, bool force_load, StationID next_station, TileIndex load_place) {
	uint space_remaining = max_load;
	for(List::iterator c = packets.begin(); c != packets.end() && space_remaining > 0;) {
		CargoPacket * p = *c;
		if (force_load || p->next == next_station || p->next == INVALID_STATION) {
			/* load the packet if possible */
			if (p->count <= space_remaining) {
				/* load all of the packet */
				space_remaining -= count;
				packets.erase(c++);
			} else {
				/* packet needs to be split */
				p = p->Split(space_remaining);
				space_remaining = 0;
				++c;
			}
			dest->packets.push_back(p);
			if (load_place != INVALID_TILE) {
				p->loaded_at_xy = load_place;
				p->paid_for = false;
			}
		}
	}
	dest->InvalidateCache();
	InvalidateCache();

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

