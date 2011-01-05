/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file linkgraph_sl.cpp Code handling saving and loading of link graphs */

#include "../linkgraph/linkgraph.h"
#include "../settings_internal.h"
#include "saveload.h"
#include <vector>

const SettingDesc *GetSettingDescription(uint index);

/**
 * Get a SaveLoad array for a link graph. The settings struct is derived from
 * the global settings saveload array. The exact entries are calculated when the function
 * is called the first time.
 * It's necessary to keep a copy of the settings for each link graph so that you can
 * change the settings while in-game and still not mess with current link graph runs.
 * Of course the settings have to be saved and loaded, too, to avoid desyncs.
 * @return Array of SaveLoad structs.
 */
const SaveLoad *GetLinkGraphDesc()
{
	static std::vector<SaveLoad> saveloads;
	static const char *prefix = "linkgraph.";

	/* Build the SaveLoad array on first call and don't touch it later on */
	if (saveloads.empty()) {
		size_t offset_gamesettings = cpp_offsetof(GameSettings, linkgraph);
		size_t offset_component = cpp_offsetof(LinkGraph, settings);

		size_t prefixlen = strlen(prefix);

		int setting = 0;
		const SettingDesc *desc = GetSettingDescription(setting);
		while (desc->save.cmd != SL_END) {
			if (desc->desc.name != NULL && strncmp(desc->desc.name, prefix, prefixlen) == 0) {
				SaveLoad sl = desc->save;
				char *&address = reinterpret_cast<char *&>(sl.address);
				address -= offset_gamesettings;
				address += offset_component;
				saveloads.push_back(sl);
			}
			desc = GetSettingDescription(++setting);
		}

		const SaveLoad component_desc[] = {
			SLE_CONDVAR(LinkGraph, num_nodes,          SLE_UINT32, SL_COMPONENTS, SL_MAX_VERSION),
			SLE_CONDVAR(LinkGraph, index,              SLE_UINT16, SL_COMPONENTS, SL_MAX_VERSION),
			SLE_CONDVAR(LinkGraph, current_station_id, SLE_UINT16, SL_COMPONENTS, SL_MAX_VERSION),
			SLE_CONDVAR(LinkGraph, cargo,              SLE_UINT8,  SL_COMPONENTS, SL_MAX_VERSION),
			SLE_END()
		};

		int i = 0;
		do {
			saveloads.push_back(component_desc[i++]);
		} while (saveloads.back().cmd != SL_END);
	}

	return &saveloads[0];
}

/* Edges and nodes are saved in the correct order, so we don't need to save their ids. */

/**
 * SaveLoad desc for a link graph node.
 */
static const SaveLoad _node_desc[] = {
	 SLE_CONDVAR(Node, supply,    SLE_UINT32, SL_COMPONENTS, SL_MAX_VERSION),
	 SLE_CONDVAR(Node, demand,    SLE_UINT32, SL_COMPONENTS, SL_MAX_VERSION),
	 SLE_CONDVAR(Node, station,   SLE_UINT16, SL_COMPONENTS, SL_MAX_VERSION),
	 SLE_END()
};

/**
 * SaveLoad desc for a link graph edge.
 */
static const SaveLoad _edge_desc[] = {
	 SLE_CONDVAR(Edge, distance,  SLE_UINT32, SL_COMPONENTS, SL_MAX_VERSION),
	 SLE_CONDVAR(Edge, capacity,  SLE_UINT32, SL_COMPONENTS, SL_MAX_VERSION),
	 SLE_END()
};

/**
 * Save/load a component of a link graph
 * @param comp the component to be saved or loaded
 */
static void SaveLoad_LinkGraphComponent(LinkGraphComponent &comp) {
	uint size = comp.GetSize();
	for (NodeID from = 0; from < size; ++from) {
		Node *node = &comp.GetNode(from);
		SlObject(node, _node_desc);
		for (NodeID to = 0; to < size; ++to) {
			SlObject(&comp.GetEdge(from, to), _edge_desc);
		}
	}
}

/**
 * Save all link graphs.
 */
static void DoSave_LGRP(void *)
{
	for(CargoID cargo = 0; cargo < NUM_CARGO; ++cargo) {
		LinkGraph &graph = _link_graphs[cargo];
		SlObject(&graph, GetLinkGraphDesc());
		SaveLoad_LinkGraphComponent(graph);
	}
}

/**
 * Load all link graphs.
 */
static void Load_LGRP()
{
	for(CargoID cargo = 0; cargo < NUM_CARGO; ++cargo) {
		LinkGraph &graph = _link_graphs[cargo];
		assert(graph.GetSize() == 0);
		SlObject(&graph, GetLinkGraphDesc());
		graph.SetSize();
		SaveLoad_LinkGraphComponent(graph);
	}
}

/**
 * Spawn the threads for running link graph calculations.
 * Has to be done after loading as the cargo classes might have changed.
 */
void AfterLoadLinkGraphs()
{
	for(CargoID cargo = 0; cargo < NUM_CARGO; ++cargo) {
		LinkGraph &graph = _link_graphs[cargo];
		if (graph.GetSize() > 0) graph.SpawnThread();
	}
}

static void Save_LGRP() {
	SlAutolength((AutolengthProc*)DoSave_LGRP, NULL);
}

extern const ChunkHandler _linkgraph_chunk_handlers[] = {
	{ 'LGRP', Save_LGRP, Load_LGRP, NULL, NULL, CH_LAST},
};
