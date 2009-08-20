/* $Id$ */

/** @file cargopacket.cpp Implementation of the cargo packets */

#include "stdafx.h"
#include "core/pool_func.hpp"
#include "economy_base.h"
#include "station_base.h"
#include "vehicle_base.h"

/* Initialize the cargopacket-pool */
CargoPacketPool _cargopacket_pool("CargoPacket");
INSTANTIATE_POOL_METHODS(CargoPacket)

void InitializeCargoPackets()
{
	_cargopacket_pool.CleanPool();
}

CargoPacket::CargoPacket(StationID in_source, uint16 in_count, SourceType in_source_type, SourceID in_source_id,
		byte in_days_in_transit, Money in_feeder_share, TileIndex in_source_xy, TileIndex in_loaded_at_xy) :
	source_id(in_source_id),
	source_xy(in_source_xy),
	feeder_share(in_feeder_share),
	count(in_count),
	days_in_transit(in_days_in_transit),
	source(in_source),
	loaded_at_xy(in_loaded_at_xy)
{
	this->source_type = in_source_type;
	if (Station::IsValidID(source)) {
		assert(count != 0);
		if(in_source_xy == 0) {
			this->source_xy = Station::Get(source)->xy;
		}
	}

	if (in_loaded_at_xy == 0) {
		this->loaded_at_xy    = this->source_xy;
	}
}

/**
 * Invalidates (sets source_id to INVALID_SOURCE) all cargo packets from given source
 * unfortunately we have to drop all append caches in this case. But as this only happens
 * very rarely it should be acceptable.
 *
 * @param src_type type of source
 * @param src index of source
 */
/* static */ void CargoPacket::InvalidateAllFrom(SourceType src_type, SourceID src)
{
	Station *st;
	FOR_ALL_STATIONS(st) {
		for(CargoID c = 0; c < NUM_CARGO; ++c) {
			st->goods[c].cargo.InvalidateAppend();
		}
	}

	Vehicle *v;
	FOR_ALL_VEHICLES(v) {
		v->cargo.InvalidateAppend();
	}

	CargoPacket *cp;
	FOR_ALL_CARGOPACKETS(cp) {
		if (cp->source_type == src_type && cp->source_id == src) cp->source_id = INVALID_SOURCE;
	}
}

bool CargoSorter::operator()(const CargoPacket *cp1, const CargoPacket *cp2) const
{
	if (cp1->GetSourceXY() < cp2->GetSourceXY()) return true;
	if (cp1->GetSourceXY() > cp2->GetSourceXY()) return false;
	if (cp1->GetDaysInTransit() < cp2->GetDaysInTransit()) return true;
	if (cp1->GetDaysInTransit() > cp2->GetDaysInTransit()) return false;
	if (cp1->GetSourceType() < cp2->GetSourceType()) return true;
	if (cp1->GetSourceType() > cp2->GetSourceType()) return false;
	if (cp1->GetSourceID() < cp2->GetSourceID()) return true;
	if (cp1->GetSourceID() > cp2->GetSourceID()) return false;

	return false; // SameSource holds
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

void CargoList::AgeCargo()
{
	if (packets.empty()) return;

	/*
	 * the append_positions cache isn't invalidated as long as all < and > relations
	 * between days_in_transit stay the same. This is the case as long as no packet
	 * reaches 0xFF
	 */
	bool invalid_append = false;

	days_in_transit = 0;
	for (List::const_iterator it = packets.begin(); it != packets.end(); it++) {
		CargoPacket *cp = *it;
		if (cp->days_in_transit != 0xFF) {
			if (++(cp->days_in_transit) == 0xFF) invalid_append = true;
		} else {
			invalid_append = true;
		}
		days_in_transit += cp->days_in_transit * cp->count;
	}
	if (invalid_append) append_positions.clear();
}

void CargoList::Merge(CargoPacket *in_list, CargoPacket *insert) {
	if (in_list->count + insert->count <= CargoPacket::MAX_COUNT) {
		in_list->count += insert->count;
		this->count += insert->count;
		this->days_in_transit += insert->count * insert->days_in_transit;
	} else {
		int diff = CargoPacket::MAX_COUNT - in_list->count;
		this->count += diff;
		this->days_in_transit += diff * insert->days_in_transit;
		in_list->count = CargoPacket::MAX_COUNT;
	}
	this->feeder_share += insert->feeder_share;
	in_list->feeder_share += insert->feeder_share;
	delete insert;
}

void CargoList::RemoveFromCache(CargoPacket *cp, bool remove_append) {
	assert(this->count >= cp->count);
	this->count -= cp->count;
	this->feeder_share -= cp->feeder_share;
	this->days_in_transit -= cp->days_in_transit * cp->count;
	if (remove_append) append_positions.erase(cp);
}

void CargoList::Append(CargoPacket *cp)
{
	assert(cp != NULL);

	AppendMap::iterator append_it = append_positions.find(cp);
	if (append_it != append_positions.end()) {
		Merge(*append_it, cp);
		return;
	}

	for (List::iterator it = packets.begin(); it != packets.end(); it++) {
		CargoPacket *in_list = *it;
		if (in_list->SameSource(cp)) {
			Merge(in_list, cp);
			append_positions.insert(in_list);
			return;
		}
	}

	/* The packet could not be merged with another one */
	packets.push_back(cp);
	append_positions.insert(cp);
	this->count += cp->count;
	this->feeder_share += cp->feeder_share;
	this->days_in_transit += cp->count * cp->days_in_transit;
}


void CargoList::Truncate(uint max_remain)
{
	for (List::iterator it = packets.begin(); it != packets.end();) {
		CargoPacket * cp = *it;
		uint local_count = cp->count;
		if (max_remain == 0) {
			RemoveFromCache(cp);
			delete cp;
			packets.erase(it++);
		} else {
			if (local_count > max_remain) {
				assert(this->count >= local_count - max_remain);
				this->count -= local_count - max_remain;
				this->days_in_transit -= cp->days_in_transit * (local_count - max_remain);
				cp->count = max_remain;
				max_remain = 0;
			} else {
				max_remain -= local_count;
			}
			++it;
		}
	}
}

bool CargoList::MoveTo(CargoList *dest, uint max_move, CargoList::MoveToAction mta, CargoPayment *payment, uint data)
{
	assert(mta == MTA_FINAL_DELIVERY || dest != NULL);
	assert(mta == MTA_UNLOAD || mta == MTA_CARGO_LOAD || payment != NULL);

	List::iterator it = packets.begin();
	while(it != packets.end() && max_move > 0) {
		CargoPacket *cp = *it;
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
					cp->feeder_share += payment->PayTransfer(cp, cp->count);
					break;

				case MTA_UNLOAD:
					break;
				}
				dest->Append(cp);
			}
		} else {
			/* Can move only part of the packet, so split it into two pieces */
			if (mta != MTA_FINAL_DELIVERY) {
				CargoPacket *cp_new = new CargoPacket();

				Money fs = cp->feeder_share * max_move / static_cast<uint>(cp->count);
				cp->feeder_share -= fs;

				cp_new->source          = cp->source;
				cp_new->source_xy       = cp->source_xy;
				cp_new->loaded_at_xy    = (mta == MTA_CARGO_LOAD) ? data : cp->loaded_at_xy;

				cp_new->days_in_transit = cp->days_in_transit;
				cp_new->feeder_share    = fs;

				cp_new->source_type     = cp->source_type;
				cp_new->source_id       = cp->source_id;

				cp_new->count = max_move;

				RemoveFromCache(cp_new, false);

				if (mta == MTA_TRANSFER) payment->PayTransfer(cp_new, max_move);
				dest->Append(cp_new);
			} else if (cp->source == data) {
				++it;
				continue;
			} else {
				payment->PayFinalDelivery(cp, max_move);
				assert(this->count >= max_move);
				this->count -= max_move;
				this->days_in_transit -= max_move * cp->days_in_transit;
			}
			cp->count -= max_move;
			max_move = 0;
		}
	}

	return it != packets.end();
}

void CargoList::InvalidateCache()
{
	count = 0;
	feeder_share = 0;
	days_in_transit = 0;

	if (packets.empty()) return;

	for (List::const_iterator it = packets.begin(); it != packets.end(); it++) {
		count           += (*it)->count;
		days_in_transit += (*it)->days_in_transit * (*it)->count;
		feeder_share    += (*it)->feeder_share;
	}
}

