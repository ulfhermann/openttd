/* $Id$ */

/** @file cargopacket.cpp Implementation of the cargo packets */

#include "stdafx.h"
#include "station_base.h"
#include "core/pool_func.hpp"
#include "economy_base.h"
#include "station_base.h"

/* Initialize the cargopacket-pool */
CargoPacketPool _cargopacket_pool("CargoPacket");
INSTANTIATE_POOL_METHODS(CargoPacket)

void InitializeCargoPackets()
{
	_cargopacket_pool.CleanPool();
}

CargoPacket::CargoPacket(StationID in_source, StationID in_next, uint16 in_count, SourceType source_type, SourceID in_source_id) :
	count(in_count),
	source(in_source),
	source_id(in_source_id),
	next(in_next)
{
	if (Station::IsValidID(source)) {
		assert(count != 0);
		this->source_xy = Station::Get(source)->xy;
	}

	this->loaded_at_xy    = this->source_xy;

	this->source_type     = source_type;
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
template<class LIST>
CargoList<LIST>::~CargoList()
{
	for (Iterator i = packets.begin(); i != packets.end(); ++i) {
		delete Deref(i);
	}
}

template<class LIST>
void CargoList<LIST>::AgeCargo()
{
	if (packets.empty()) return;

	days_in_transit = 0;
	for (Iterator it = packets.begin(); it != packets.end(); it++) {
		CargoPacket *cp = Deref(it);
		if (cp->days_in_transit != 0xFF) ++(cp->days_in_transit);
		days_in_transit += cp->days_in_transit * cp->count;
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

template<class LIST>
void CargoList<LIST>::RemoveFromCache(CargoPacket *cp) {
	this->count -= cp->count;
	this->feeder_share -= cp->feeder_share;
	this->days_in_transit -= cp->days_in_transit * cp->count;
}

template<class LIST>
void CargoList<LIST>::AddToCache(CargoPacket *cp) {
	this->count += cp->count;
	this->feeder_share += cp->feeder_share;
	this->days_in_transit += cp->count * cp->days_in_transit;
}

template<class LIST>
void CargoList<LIST>::Append(CargoPacket *cp, bool merge)
{
	assert(cp != NULL);

	if (merge) {
		for (Iterator it = packets.begin(); it != packets.end(); it++) {
			CargoPacket *in_list = Deref(it);
			if (in_list->SameSource(cp) && in_list->count + cp->count <= CargoPacket::MAX_COUNT) {
				in_list->count += cp->count;
				in_list->feeder_share += cp->feeder_share;
				AddToCache(cp);
				delete cp;
				return;
			}
		}
	}

	/* The packet could or should not be merged with another one */
	Insert(cp);
	AddToCache(cp);
}

template<class LIST>
void CargoList<LIST>::Truncate(uint max_remain)
{
	for (Iterator it = packets.begin(); it != packets.end();) {
		CargoPacket * cp = Deref(it);
		uint local_count = cp->count;
		if (max_remain == 0) {
			RemoveFromCache(cp);
			delete cp;
			packets.erase(it++);
		} else {
			if (local_count > max_remain) {
				uint diff = local_count - max_remain;
				this->count -= diff;
				this->days_in_transit -= cp->days_in_transit * diff;
				cp->count = max_remain;
				max_remain = 0;
			} else {
				max_remain -= local_count;
			}
			++it;
		}
	}
}

template<class LIST>
void CargoList<LIST>::Transfer(CargoPacket *cp, CargoPayment *payment)
{
	RemoveFromCache(cp);
	cp->feeder_share += payment->PayTransfer(cp, cp->count);
}

template<class LIST>
void CargoList<LIST>::DeliverPart(CargoPacket *cp, CargoPayment *payment, uint deliver)
{
	payment->PayFinalDelivery(cp, deliver);
	this->count -= deliver;
	this->days_in_transit -= deliver * cp->days_in_transit;
	cp->count -= deliver;
}

template<class LIST>
bool CargoList<LIST>::MoveTo(CargoList *dest, uint max_move, CargoList::MoveToAction mta, CargoPayment *payment, uint data)
{
	assert(mta == MTA_FINAL_DELIVERY || dest != NULL);
	assert(mta == MTA_UNLOAD || mta == MTA_CARGO_LOAD || payment != NULL);

	Iterator it = packets.begin();
	while(it != packets.end() && max_move > 0) {
		CargoPacket *cp = Deref(it);
		if (cp->count <= max_move) {
			/* Can move the complete packet */
			if (cp->source == data && mta == MTA_FINAL_DELIVERY) {
				++it;
			} else {
				max_move -= cp->count;
				packets.erase(it++);
				RemoveFromCache(cp);
				switch(mta) {
					case MTA_FINAL_DELIVERY:
						payment->PayFinalDelivery(cp, cp->count);
						delete cp;
						continue; // of the loop

					case MTA_CARGO_LOAD:
						cp->loaded_at_xy = data;
						break;

					case MTA_TRANSFER:
						/* here the packet isn't in either list, so we can change it */
						cp->feeder_share += payment->PayTransfer(cp, cp->count);
						break;

					case MTA_UNLOAD:
						break;
				}
				dest->Append(cp, false);
			}
		} else {
			/* Can move only part of the packet, so split it into two pieces */
			if (mta != MTA_FINAL_DELIVERY) {
				Money fs = cp->feeder_share * max_move / static_cast<uint>(cp->count);
				cp->feeder_share -= fs;
				CargoPacket *cp_new = new CargoPacket(max_move, cp->days_in_transit, fs);
				cp_new->source_type     = cp->source_type;
				cp_new->source_id       = cp->source_id;

				RemoveFromCache(cp_new); // this reflects the changes in cp

				if (mta == MTA_TRANSFER) {
					/* add the feeder share before inserting in dest */
					cp_new->feeder_share += payment->PayTransfer(cp_new, max_move);
				}

				dest->Append(cp_new, false);
				cp->count -= max_move;
			} else if (cp->source == data) {
				++it;
				continue;
			} else {
				DeliverPart(cp, payment, max_move);
			}

			max_move = 0;
		}
	}

	return it != packets.end();
}


void VehicleCargoList::DeliverPacket(Iterator & c, uint & remaining_unload, CargoPayment *payment) {
	CargoPacket * p = *c;
	if (p->Count() <= remaining_unload) {
		remaining_unload -= p->Count();
		payment->PayFinalDelivery(p, p->Count());
		delete p;
		RemoveFromCache(p);
		packets.erase(c++);
	} else {
		DeliverPart(p, payment, remaining_unload);
		remaining_unload = 0;
		++c;
	}
}

CargoPacket * VehicleCargoList::TransferPacket(Iterator & c, uint & remaining_unload, GoodsEntry *dest, CargoPayment *payment) {
	CargoPacket *p = *c;
	if (p->Count() <= remaining_unload) {
		packets.erase(c++);
	} else {
		p = p->Split(remaining_unload);
		++c;
	}
	Transfer(p, payment);
	dest->cargo.Append(p);
	SetBit(dest->acceptance_pickup, GoodsEntry::PICKUP);
	remaining_unload -= p->Count();
	return p;
}

UnloadType VehicleCargoList::WillUnload(const UnloadDescription & ul, const CargoPacket * p) const {
	if (ul.dest->flows[p->source].empty()) {
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
		} else if (ul.next_station == via || ul.next_station == INVALID_STATION) {
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
		CargoPacket * p = *c;
		StationID source = p->source;
		uint last_remaining = remaining_unload;
		UnloadType unload_flags = this->WillUnload(ul, p);

		if (unload_flags & UL_DELIVER) {
			this->DeliverPacket(c, remaining_unload, payment);
			dest->UpdateFlowStats(source, last_remaining - remaining_unload, curr_station);
		} else if (unload_flags & UL_TRANSFER) {
			/* TransferPacket may split the packet and return the transferred part */
			p = this->TransferPacket(c, remaining_unload, dest, payment);
			p->next = dest->UpdateFlowStatsTransfer(source, last_remaining - remaining_unload, curr_station);
		} else /* UL_KEEP */ {
			++c;
		}
	}

	dest->cargo.InvalidateCache();
	InvalidateCache();
	return max_unload - remaining_unload;
}

template<class LIST>
bool CargoList<LIST>::LoadPacket(CargoPacket * packet, uint &cap, TileIndex load_place)
{
	bool ret = true;
	/* load the packet if possible */
	if (packet->count > cap) {
		/* packet needs to be split */
		packet = packet->Split(cap);
		assert(packet->count == cap);
		ret = false;
	}
	cap -= packet->count;
	this->Insert(packet);
	if (load_place != INVALID_TILE) {
		packet->loaded_at_xy = load_place;
	}
	return ret;
}

uint VehicleCargoList::MoveToOtherVehicle(VehicleCargoList *dest, uint cap)
{
	uint orig_cap = cap;
	Iterator begin = this->packets.begin();
	Iterator end = this->packets.begin();
	while(begin != end && dest->LoadPacket(Deref(begin), cap)) {
		this->packets.erase(begin++);
	}
	if (cap != orig_cap) {
		dest->InvalidateCache();
		this->InvalidateCache();
	}
	return orig_cap - cap;
}

uint StationCargoList::LoadReserved(VehicleCargoList *dest, VehicleID v, uint cap, TileIndex load_place)
{
	uint orig_cap = cap;
	CargoReservation::iterator begin = this->reserved.lower_bound(v);
	CargoReservation::iterator end = this->reserved.upper_bound(v);
	while(begin != end && dest->LoadPacket(Deref(begin), cap, load_place)) {
		this->reserved.erase(begin++);
	}
	uint loaded = orig_cap - cap;
	if (loaded > 0) {
		reserved_amounts[v] -= loaded;
		dest->InvalidateCache();
	}
	return loaded;
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

void StationCargoList::RerouteStalePackets(StationID curr, StationID to, GoodsEntry * ge) {
	for(Iterator it = packets.begin(); it != packets.end(); ++it) {
		CargoPacket * packet = it->second;
		if (packet->next == to) {
			packet->next = ge->UpdateFlowStatsTransfer(packet->source, packet->Count(), curr);
		}
	}
	InvalidateCache();
}

template<class LIST>
void CargoList<LIST>::UpdateFlows(StationID next, GoodsEntry * ge) {
	for(Iterator i = packets.begin(); i != packets.end(); ++i) {
		CargoPacket * p = Deref(i);
		ge->UpdateFlowStats(p->source, p->count, next);
		p->next = next;
	}
}

uint StationCargoList::ReservePackets(VehicleID v, uint cap, Iterator begin, Iterator end) {
	uint &amount = reserved_amounts[v];
	while(begin != end && cap > 0) {
		CargoPacket * cp = begin->second;
		if (cp->Count() <= cap) {
			packets.erase(begin++);
		} else {
			cp = cp->Split(cap);
		}
		cap -= cp->Count();
		reserved.insert(std::make_pair(v, cp));
		amount += cp->Count();
	}
	return cap;
}

void StationCargoList::ReservePacketsForLoading(VehicleID v, uint cap, StationID selected_station) {
	Iterator begin;
	Iterator end;
	uint orig_cap = cap;
	if (selected_station != INVALID_STATION) {
		begin = this->packets.lower_bound(selected_station);
		end = this->packets.upper_bound(selected_station);
		cap = ReservePackets(v, cap, begin, end);
		if (cap > 0) {
			begin = this->packets.lower_bound(INVALID_STATION);
			end = this->packets.upper_bound(INVALID_STATION);
			cap = ReservePackets(v, cap, begin, end);
		}
	} else {
		begin = this->packets.begin();
		end = this->packets.end();
		cap = ReservePackets(v, cap, begin, end);
	}
	if (cap != orig_cap) {
		InvalidateCache();
	}
}

StationCargoList::~StationCargoList() {
	for (CargoReservation::iterator i = reserved.begin(); i != reserved.end(); ++i) {
		delete i->second;
	}
}

void StationCargoList::Unreserve(VehicleID v) {
	CargoReservation::iterator begin = reserved.lower_bound(v);
	CargoReservation::iterator end = reserved.upper_bound(v);
	while(begin != end) {
		Insert(begin->second);
		reserved.erase(begin++);
	}
}

template<class LIST>
void CargoList<LIST>::InvalidateCache()
{
	count = 0;
	feeder_share = 0;
	days_in_transit = 0;

	if (packets.empty()) return;

	for (ConstIterator it = packets.begin(); it != packets.end(); it++) {
		CargoPacket *cp = Deref(it);
		count           += cp->count;
		days_in_transit += cp->days_in_transit * cp->count;
		feeder_share    += cp->feeder_share;
	}
}

/* stupid workaround to make the compile recognize the template instances */
template class CargoList<CargoPacketList>;
template class CargoList<StationCargoPacketMap>;
