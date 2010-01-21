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
#include "table/airport_movement.h"
#include "core/alloc_func.hpp"
#include "date_func.h"
#include "settings_type.h"


/* 8-66 are mapped to 0-58, 83+ are mapped to 59+ */
enum AirportTiles {
	APT_APRON                  = 0,
	APT_APRON_FENCE_NW         = 1,
	APT_APRON_FENCE_SW         = 2,
	APT_STAND                  = 3,
	APT_APRON_W                = 4,
	APT_APRON_S                = 5,
	APT_APRON_VER_CROSSING_S   = 6,
	APT_APRON_HOR_CROSSING_W   = 7,
	APT_APRON_VER_CROSSING_N   = 8,
	APT_APRON_HOR_CROSSING_E   = 9,
	APT_APRON_E                = 10,
	APT_ARPON_N                = 11,
	APT_APRON_HOR              = 12,
	APT_APRON_N_FENCE_SW       = 13,
	APT_RUNWAY_1               = 14,
	APT_RUNWAY_2               = 15,
	APT_RUNWAY_3               = 16,
	APT_RUNWAY_4               = 17,
	APT_RUNWAY_END_FENCE_SE    = 18,
	APT_BUILDING_2             = 19,
	APT_TOWER_FENCE_SW         = 20,
	APT_ROUND_TERMINAL         = 21,
	APT_BUILDING_3             = 22,
	APT_BUILDING_1             = 23,
	APT_DEPOT_SE               = 24,
	APT_STAND_1                = 25,
	APT_STAND_PIER_NE          = 26,
	APT_PIER_NW_NE             = 27,
	APT_PIER                   = 28,
	APT_EMPTY                  = 29,
	APT_EMPTY_FENCE_NE         = 30,
	APT_RADAR_GRASS_FENCE_SW   = 31,
	/* 32-42 are for turning the radar */
	APT_RADIO_TOWER_FENCE_NE   = 43,
	APT_SMALL_BUILDING_3       = 44,
	APT_SMALL_BUILDING_2       = 45,
	APT_SMALL_BUILDING_1       = 46,
	APT_GRASS_FENCE_SW         = 47,
	APT_GRASS_2                = 48,
	APT_GRASS_1                = 49,
	APT_GRASS_FENCE_NE_FLAG    = 50,
	/* 51-53 are for flag animation */
	APT_RUNWAY_SMALL_NEAR_END  = 54,
	APT_RUNWAY_SMALL_MIDDLE    = 55,
	APT_RUNWAY_SMALL_FAR_END   = 56,
	APT_SMALL_DEPOT_SE         = 57,
	APT_HELIPORT               = 58,
	APT_RUNWAY_END             = 59,
	APT_RUNWAY_5               = 60,
	APT_TOWER                  = 61,
	APT_SMALL_DEPOT_SE_2       = 62, // unused (copy of APT_SMALL_DEPOT_SE)
	APT_APRON_FENCE_NE         = 63,
	APT_RUNWAY_END_FENCE_NW    = 64,
	APT_RUNWAY_FENCE_NW        = 65,
	APT_RADAR_FENCE_SW         = 66,
	/* 67-77 are for turning the radar */
	APT_RADAR_FENCE_NE         = 78,
	/* 79-89 are for turning the radar */
	APT_HELIPAD_1              = 90,
	APT_HELIPAD_2_FENCE_NW     = 91,
	APT_HELIPAD_2              = 92,
	APT_APRON_FENCE_NE_SW      = 93,
	APT_RUNWAY_END_FENCE_NW_SW = 94,
	APT_RUNWAY_END_FENCE_SE_SW = 95,
	APT_RUNWAY_END_FENCE_NE_NW = 96,
	APT_RUNWAY_END_FENCE_NE_SE = 97,
	APT_HELIPAD_2_FENCE_NE_SE  = 98,
	APT_APRON_FENCE_SE_SW      = 99,
	APT_LOW_BUILDING_FENCE_N   = 100,
	APT_ROT_RUNWAY_FENCE_NE    = 101, // unused
	APT_ROT_RUNWAY_END_FENCE_NE= 102, // unused
	APT_ROT_RUNWAY_FENCE_SW    = 103, // unused
	APT_ROT_RUNWAY_END_FENCE_SW= 104, // unused
	APT_DEPOT_SW               = 105, // unused
	APT_DEPOT_NW               = 106, // unused
	APT_DEPOT_NE               = 107, // unused
	APT_HELIPAD_2_FENCE_SE_SW  = 108, // unused
	APT_HELIPAD_2_FENCE_SE     = 109, // unused
	APT_LOW_BUILDING_FENCE_NW  = 110,
	APT_LOW_BUILDING_FENCE_NE  = 111, // unused
	APT_LOW_BUILDING_FENCE_SW  = 112, // unused
	APT_LOW_BUILDING_FENCE_SE  = 113, // unused
	APT_STAND_FENCE_NE         = 114, // unused
	APT_STAND_FENCE_SE         = 115, // unused
	APT_STAND_FENCE_SW         = 116, // unused
	APT_APRON_FENCE_NE_2       = 117, // unused (copy of APT_APRON_FENCE_NE)
	APT_APRON_FENCE_SE         = 118,
	APT_HELIPAD_2_FENCE_NW_SW  = 119, // unused
	APT_HELIPAD_2_FENCE_SW     = 120, // unused
	APT_RADAR_FENCE_SE         = 121, // unused
	/* 122-132 used for radar rotation */
	APT_HELIPAD_3_FENCE_SE_SW  = 133,
	APT_HELIPAD_3_FENCE_NW_SW  = 134,
	APT_HELIPAD_3_FENCE_NW     = 135,
	APT_LOW_BUILDING           = 136,
	APT_APRON_FENCE_NE_SE      = 137,
	APT_APRON_HALF_EAST        = 138,
	APT_APRON_HALF_WEST        = 139,
	APT_GRASS_FENCE_NE_FLAG_2  = 140,
	/* 141-143 used for flag animation */
};

#include "table/airport_defaults.h"
#include "table/airporttiles.h"

AirportSpec AirportSpec::dummy = {NULL, NULL, 0, 0, 0, 0, 0, MIN_YEAR, MIN_YEAR};
AirportSpec AirportSpec::oilrig = {NULL, NULL, 0, 1, 1, 0, 4, MIN_YEAR, MIN_YEAR};


/**
 * Retrieve airport spec for the given airport
 * @param type index of airport
 * @return A pointer to the corresponding AirportSpec
 */
/* static */ const AirportSpec *AirportSpec::Get(byte type)
{
	if (type == AT_OILRIG) return &oilrig;
	assert(type < NUM_AIRPORTS);
	extern const AirportSpec _origin_airport_specs[];
	return &_origin_airport_specs[type];
}

/**
 * Retrieve airport tile spec for the given airport tile
 * @param gfx index of airport tile
 * @return A pointer to the corresponding AirportTileSpec
 */
/* static */ const AirportTileSpec *AirportTileSpec::Get(StationGfx gfx)
{
	assert(gfx < NUM_AIRPORTTILES);
	extern const AirportTileSpec _origin_airporttile_specs[];
	return &_origin_airporttile_specs[gfx];
}

/* Uncomment this to print out a full report of the airport-structure
 * You should either use
 * - true: full-report, print out every state and choice with string-names
 * OR
 * - false: give a summarized report which only shows current and next position */
//#define DEBUG_AIRPORT false

static AirportFTAClass *_dummy_airport;
static AirportFTAClass *_country_airport;
static AirportFTAClass *_city_airport;
static AirportFTAClass *_oilrig;
static AirportFTAClass *_heliport;
static AirportFTAClass *_metropolitan_airport;
static AirportFTAClass *_international_airport;
static AirportFTAClass *_commuter_airport;
static AirportFTAClass *_heli_depot;
static AirportFTAClass *_intercontinental_airport;
static AirportFTAClass *_heli_station;


void InitializeAirports()
{
	_dummy_airport = new AirportFTAClass(
		_airport_moving_data_dummy,
		NULL,
		NULL,
		_airport_entries_dummy,
		AirportFTAClass::ALL,
		_airport_fta_dummy,
		0
	);

	_country_airport = new AirportFTAClass(
		_airport_moving_data_country,
		_airport_terminal_country,
		NULL,
		_airport_entries_country,
		AirportFTAClass::ALL | AirportFTAClass::SHORT_STRIP,
		_airport_fta_country,
		0
	);

	_city_airport = new AirportFTAClass(
		_airport_moving_data_town,
		_airport_terminal_city,
		NULL,
		_airport_entries_city,
		AirportFTAClass::ALL,
		_airport_fta_city,
		0
	);

	_metropolitan_airport = new AirportFTAClass(
		_airport_moving_data_metropolitan,
		_airport_terminal_metropolitan,
		NULL,
		_airport_entries_metropolitan,
		AirportFTAClass::ALL,
		_airport_fta_metropolitan,
		0
	);

	_international_airport = new AirportFTAClass(
		_airport_moving_data_international,
		_airport_terminal_international,
		_airport_helipad_international,
		_airport_entries_international,
		AirportFTAClass::ALL,
		_airport_fta_international,
		0
	);

	_intercontinental_airport = new AirportFTAClass(
		_airport_moving_data_intercontinental,
		_airport_terminal_intercontinental,
		_airport_helipad_intercontinental,
		_airport_entries_intercontinental,
		AirportFTAClass::ALL,
		_airport_fta_intercontinental,
		0
	);

	_heliport = new AirportFTAClass(
		_airport_moving_data_heliport,
		NULL,
		_airport_helipad_heliport_oilrig,
		_airport_entries_heliport_oilrig,
		AirportFTAClass::HELICOPTERS,
		_airport_fta_heliport_oilrig,
		60
	);

	_oilrig = new AirportFTAClass(
		_airport_moving_data_oilrig,
		NULL,
		_airport_helipad_heliport_oilrig,
		_airport_entries_heliport_oilrig,
		AirportFTAClass::HELICOPTERS,
		_airport_fta_heliport_oilrig,
		54
	);

	_commuter_airport = new AirportFTAClass(
		_airport_moving_data_commuter,
		_airport_terminal_commuter,
		_airport_helipad_commuter,
		_airport_entries_commuter,
		AirportFTAClass::ALL | AirportFTAClass::SHORT_STRIP,
		_airport_fta_commuter,
		0
	);

	_heli_depot = new AirportFTAClass(
		_airport_moving_data_helidepot,
		NULL,
		_airport_helipad_helidepot,
		_airport_entries_helidepot,
		AirportFTAClass::HELICOPTERS,
		_airport_fta_helidepot,
		0
	);

	_heli_station = new AirportFTAClass(
		_airport_moving_data_helistation,
		NULL,
		_airport_helipad_helistation,
		_airport_entries_helistation,
		AirportFTAClass::HELICOPTERS,
		_airport_fta_helistation,
		0
	);
}

void UnInitializeAirports()
{
	delete _dummy_airport;
	delete _country_airport;
	delete _city_airport;
	delete _heliport;
	delete _metropolitan_airport;
	delete _international_airport;
	delete _commuter_airport;
	delete _heli_depot;
	delete _intercontinental_airport;
	delete _heli_station;
}


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

bool AirportSpec::IsAvailable() const
{
	if (_cur_year < this->min_year) return false;
	if (_settings_game.station.never_expire_airports) return true;
	return _cur_year <= this->max_year;
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
	/* FIXME -- AircraftNextAirportPos_and_Order -> Needs something nicer, don't like this code
	 * needs constant change if more airports are added */
	switch (airport_type) {
		default:               NOT_REACHED();
		case AT_SMALL:         return _country_airport;
		case AT_LARGE:         return _city_airport;
		case AT_METROPOLITAN:  return _metropolitan_airport;
		case AT_HELIPORT:      return _heliport;
		case AT_OILRIG:        return _oilrig;
		case AT_INTERNATIONAL: return _international_airport;
		case AT_COMMUTER:      return _commuter_airport;
		case AT_HELIDEPOT:     return _heli_depot;
		case AT_INTERCON:      return _intercontinental_airport;
		case AT_HELISTATION:   return _heli_station;
		case AT_DUMMY:         return _dummy_airport;
	}
}
