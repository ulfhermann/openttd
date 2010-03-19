/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file airport.cpp Functions related to airports. */

#include "stdafx.h"
#include "debug.h"
#include "airport.h"
#include "map_type.h"
#include "table/airport_movement.h"
#include "core/alloc_func.hpp"
#include "date_func.h"
#include "settings_type.h"
#include "newgrf_airport.h"
#include "station_base.h"
#include "table/strings.h"
#include "table/airporttile_ids.h"

/* Uncomment this to print out a full report of the airport-structure
 * You should either use
 * - true: full-report, print out every state and choice with string-names
 * OR
 * - false: give a summarized report which only shows current and next position */
//#define DEBUG_AIRPORT false

static AirportFTAClass _airportfta_dummy(
		_airport_moving_data_dummy,
		NULL,
		NULL,
		_airport_entries_dummy,
		AirportFTAClass::ALL,
		_airport_fta_dummy,
		0
	);

static AirportFTAClass _airportfta_country(
		_airport_moving_data_country,
		_airport_terminal_country,
		NULL,
		_airport_entries_country,
		AirportFTAClass::ALL | AirportFTAClass::SHORT_STRIP,
		_airport_fta_country,
		0
	);

static AirportFTAClass _airportfta_city(
		_airport_moving_data_town,
		_airport_terminal_city,
		NULL,
		_airport_entries_city,
		AirportFTAClass::ALL,
		_airport_fta_city,
		0
	);

static AirportFTAClass _airportfta_oilrig(
		_airport_moving_data_oilrig,
		NULL,
		_airport_helipad_heliport_oilrig,
		_airport_entries_heliport_oilrig,
		AirportFTAClass::HELICOPTERS,
		_airport_fta_heliport_oilrig,
		54
	);

static AirportFTAClass _airportfta_heliport(
		_airport_moving_data_heliport,
		NULL,
		_airport_helipad_heliport_oilrig,
		_airport_entries_heliport_oilrig,
		AirportFTAClass::HELICOPTERS,
		_airport_fta_heliport_oilrig,
		60
	);

static AirportFTAClass _airportfta_metropolitan(
		_airport_moving_data_metropolitan,
		_airport_terminal_metropolitan,
		NULL,
		_airport_entries_metropolitan,
		AirportFTAClass::ALL,
		_airport_fta_metropolitan,
		0
	);

static AirportFTAClass _airportfta_international(
		_airport_moving_data_international,
		_airport_terminal_international,
		_airport_helipad_international,
		_airport_entries_international,
		AirportFTAClass::ALL,
		_airport_fta_international,
		0
	);

static AirportFTAClass _airportfta_commuter(
		_airport_moving_data_commuter,
		_airport_terminal_commuter,
		_airport_helipad_commuter,
		_airport_entries_commuter,
		AirportFTAClass::ALL | AirportFTAClass::SHORT_STRIP,
		_airport_fta_commuter,
		0
	);

static AirportFTAClass _airportfta_helidepot(
		_airport_moving_data_helidepot,
		NULL,
		_airport_helipad_helidepot,
		_airport_entries_helidepot,
		AirportFTAClass::HELICOPTERS,
		_airport_fta_helidepot,
		0
	);

static AirportFTAClass _airportfta_intercontinental(
		_airport_moving_data_intercontinental,
		_airport_terminal_intercontinental,
		_airport_helipad_intercontinental,
		_airport_entries_intercontinental,
		AirportFTAClass::ALL,
		_airport_fta_intercontinental,
		0
	);

static AirportFTAClass _airportfta_helistation(
		_airport_moving_data_helistation,
		NULL,
		_airport_helipad_helistation,
		_airport_entries_helistation,
		AirportFTAClass::HELICOPTERS,
		_airport_fta_helistation,
		0
	);

#include "table/airport_defaults.h"


static uint16 AirportGetNofElements(const AirportFTAbuildup *apFA);
static AirportFTA *AirportBuildAutomata(uint nofelements, const AirportFTAbuildup *apFA);
static byte AirportGetTerminalCount(const byte *terminals, byte *groups);
static byte AirportTestFTA(uint nofelements, const AirportFTA *layout, const byte *terminals);

#ifdef DEBUG_AIRPORT
static void AirportPrintOut(uint nofelements, const AirportFTA *layout, bool full_report);
#endif


AirportFTAClass::AirportFTAClass(
	const AirportMovingData *moving_data_,
	const byte *terminals_,
	const byte *helipads_,
	const byte *entry_points_,
	Flags flags_,
	const AirportFTAbuildup *apFA,
	byte delta_z_
) :
	moving_data(moving_data_),
	terminals(terminals_),
	helipads(helipads_),
	flags(flags_),
	nofelements(AirportGetNofElements(apFA)),
	entry_points(entry_points_),
	delta_z(delta_z_)
{
	byte nofterminalgroups, nofhelipadgroups;

	/* Set up the terminal and helipad count for an airport.
	 * TODO: If there are more than 10 terminals or 4 helipads, internal variables
	 * need to be changed, so don't allow that for now */
	uint nofterminals = AirportGetTerminalCount(terminals, &nofterminalgroups);
	if (nofterminals > MAX_TERMINALS) {
		DEBUG(misc, 0, "[Ap] only a maximum of %d terminals are supported (requested %d)", MAX_TERMINALS, nofterminals);
		assert(nofterminals <= MAX_TERMINALS);
	}

	uint nofhelipads = AirportGetTerminalCount(helipads, &nofhelipadgroups);
	if (nofhelipads > MAX_HELIPADS) {
		DEBUG(misc, 0, "[Ap] only a maximum of %d helipads are supported (requested %d)", MAX_HELIPADS, nofhelipads);
		assert(nofhelipads <= MAX_HELIPADS);
	}

	/* Get the number of elements from the source table. We also double check this
	 * with the entry point which must be within bounds and use this information
	 * later on to build and validate the state machine */
	for (DiagDirection i = DIAGDIR_BEGIN; i < DIAGDIR_END; i++) {
		if (entry_points[i] >= nofelements) {
			DEBUG(misc, 0, "[Ap] entry (%d) must be within the airport (maximum %d)", entry_points[i], nofelements);
			assert(entry_points[i] < nofelements);
		}
	}

	/* Build the state machine itself */
	layout = AirportBuildAutomata(nofelements, apFA);
	DEBUG(misc, 6, "[Ap] #count %3d; #term %2d (%dgrp); #helipad %2d (%dgrp); entries %3d, %3d, %3d, %3d",
		nofelements, nofterminals, nofterminalgroups, nofhelipads, nofhelipadgroups,
		entry_points[DIAGDIR_NE], entry_points[DIAGDIR_SE], entry_points[DIAGDIR_SW], entry_points[DIAGDIR_NW]);

	/* Test if everything went allright. This is only a rude static test checking
	 * the symantic correctness. By no means does passing the test mean that the
	 * airport is working correctly or will not deadlock for example */
	uint ret = AirportTestFTA(nofelements, layout, terminals);
	if (ret != MAX_ELEMENTS) DEBUG(misc, 0, "[Ap] problem with element: %d", ret - 1);
	assert(ret == MAX_ELEMENTS);

#ifdef DEBUG_AIRPORT
	AirportPrintOut(nofelements, layout, DEBUG_AIRPORT);
#endif
}

AirportFTAClass::~AirportFTAClass()
{
	for (uint i = 0; i < nofelements; i++) {
		AirportFTA *current = layout[i].next;
		while (current != NULL) {
			AirportFTA *next = current->next;
			free(current);
			current = next;
		};
	}
	free(layout);
}

/** Get the number of elements of a source Airport state automata
 * Since it is actually just a big array of AirportFTA types, we only
 * know one element from the other by differing 'position' identifiers */
static uint16 AirportGetNofElements(const AirportFTAbuildup *apFA)
{
	uint16 nofelements = 0;
	int temp = apFA[0].position;

	for (uint i = 0; i < MAX_ELEMENTS; i++) {
		if (temp != apFA[i].position) {
			nofelements++;
			temp = apFA[i].position;
		}
		if (apFA[i].position == MAX_ELEMENTS) break;
	}
	return nofelements;
}

/** We calculate the terminal/helipod count based on the data passed to us
 * This data (terminals) contains an index as a first element as to how many
 * groups there are, and then the number of terminals for each group */
static byte AirportGetTerminalCount(const byte *terminals, byte *groups)
{
	byte nof_terminals = 0;
	*groups = 0;

	if (terminals != NULL) {
		uint i = terminals[0];
		*groups = i;
		while (i-- > 0) {
			terminals++;
			assert(*terminals != 0); // no empty groups please
			nof_terminals += *terminals;
		}
	}
	return nof_terminals;
}


static AirportFTA *AirportBuildAutomata(uint nofelements, const AirportFTAbuildup *apFA)
{
	AirportFTA *FAutomata = MallocT<AirportFTA>(nofelements);
	uint16 internalcounter = 0;

	for (uint i = 0; i < nofelements; i++) {
		AirportFTA *current = &FAutomata[i];
		current->position      = apFA[internalcounter].position;
		current->heading       = apFA[internalcounter].heading;
		current->block         = apFA[internalcounter].block;
		current->next_position = apFA[internalcounter].next;

		/* outgoing nodes from the same position, create linked list */
		while (current->position == apFA[internalcounter + 1].position) {
			AirportFTA *newNode = MallocT<AirportFTA>(1);

			newNode->position      = apFA[internalcounter + 1].position;
			newNode->heading       = apFA[internalcounter + 1].heading;
			newNode->block         = apFA[internalcounter + 1].block;
			newNode->next_position = apFA[internalcounter + 1].next;
			/* create link */
			current->next = newNode;
			current = current->next;
			internalcounter++;
		}
		current->next = NULL;
		internalcounter++;
	}
	return FAutomata;
}


static byte AirportTestFTA(uint nofelements, const AirportFTA *layout, const byte *terminals)
{
	uint next_position = 0;

	for (uint i = 0; i < nofelements; i++) {
		uint position = layout[i].position;
		if (position != next_position) return i;
		const AirportFTA *first = &layout[i];

		for (const AirportFTA *current = first; current != NULL; current = current->next) {
			/* A heading must always be valid. The only exceptions are
			 * - multiple choices as start, identified by a special value of 255
			 * - terminal group which is identified by a special value of 255 */
			if (current->heading > MAX_HEADINGS) {
				if (current->heading != 255) return i;
				if (current == first && current->next == NULL) return i;
				if (current != first && current->next_position > terminals[0]) return i;
			}

			/* If there is only one choice, it must be at the end */
			if (current->heading == 0 && current->next != NULL) return i;
			/* Obviously the elements of the linked list must have the same identifier */
			if (position != current->position) return i;
			/* A next position must be within bounds */
			if (current->next_position >= nofelements) return i;
		}
		next_position++;
	}
	return MAX_ELEMENTS;
}

#ifdef DEBUG_AIRPORT
static const char * const _airport_heading_strings[] = {
	"TO_ALL",
	"HANGAR",
	"TERM1",
	"TERM2",
	"TERM3",
	"TERM4",
	"TERM5",
	"TERM6",
	"HELIPAD1",
	"HELIPAD2",
	"TAKEOFF",
	"STARTTAKEOFF",
	"ENDTAKEOFF",
	"HELITAKEOFF",
	"FLYING",
	"LANDING",
	"ENDLANDING",
	"HELILANDING",
	"HELIENDLANDING",
	"TERM7",
	"TERM8",
	"HELIPAD3",
	"HELIPAD4",
	"DUMMY" // extra heading for 255
};

static void AirportPrintOut(uint nofelements, const AirportFTA *layout, bool full_report)
{
	if (!full_report) printf("(P = Current Position; NP = Next Position)\n");

	for (uint i = 0; i < nofelements; i++) {
		for (const AirportFTA *current = &layout[i]; current != NULL; current = current->next) {
			if (full_report) {
				byte heading = (current->heading == 255) ? MAX_HEADINGS + 1 : current->heading;
				printf("\tPos:%2d NPos:%2d Heading:%15s Block:%2d\n", current->position,
					    current->next_position, _airport_heading_strings[heading],
							FindLastBit(current->block));
			} else {
				printf("P:%2d NP:%2d", current->position, current->next_position);
			}
		}
		printf("\n");
	}
}
#endif

const AirportFTAClass *GetAirport(const byte airport_type)
{
	if (airport_type == AT_DUMMY) return &_airportfta_dummy;
	return AirportSpec::Get(airport_type)->fsm;
}

/**
 * Get the vehicle position when an aircraft is build at the given tile
 * @param hangar_tile The tile on which the vehicle is build
 * @return The position (index in airport node array) where the aircraft ends up
 */
byte GetVehiclePosOnBuild(TileIndex hangar_tile)
{
	const Station *st = Station::GetByTile(hangar_tile);
	const AirportFTAClass *apc = st->airport.GetFTA();
	/* When we click on hangar we know the tile it is on. By that we know
	 * its position in the array of depots the airport has.....we can search
	 * layout for #th position of depot. Since layout must start with a listing
	 * of all depots, it is simple */
	for (uint i = 0;; i++) {
		if (st->GetHangarTile(i) == hangar_tile) {
			assert(apc->layout[i].heading == HANGAR);
			return apc->layout[i].position;
		}
	}
	NOT_REACHED();
}
