/** @file linkgraph_sl.cpp Code handling saving and loading of link graphs */

#include "../linkgraph/linkgraph.h"
#include "../settings_internal.h"
#include "saveload.h"
#include <vector>

const SettingDesc *GetSettingDescription(uint index);

static Date _join_date;

/**
 * Get a SaveLoad array for a linkgraph component. The settings struct is derived from
 * the global settings saveload array. The exact entries are calculated when the function
 * is called the first time.
 * @return an array of SaveLoad structs
 */
const SaveLoad *GetLinkGraphComponentDesc() {

	static const SaveLoad _component_desc[] = {
		 SLE_CONDVAR(LinkGraphComponent, num_nodes,        SLE_UINT32, SL_COMPONENTS, SL_MAX_VERSION),
		 SLE_CONDVAR(LinkGraphComponent, index,            SLE_UINT16, SL_COMPONENTS, SL_MAX_VERSION),
		SLEG_CONDVAR(                    _join_date,       SLE_INT32,  SL_COMPONENTS, SL_MAX_VERSION),
		 SLE_END()
	};

	size_t offset_gamesettings = cpp_offsetof(GameSettings, linkgraph);
	size_t offset_component = cpp_offsetof(LinkGraphComponent, settings);

	typedef std::vector<SaveLoad> SaveLoadVector;
	static SaveLoadVector saveloads;
	static const char *prefix = "linkgraph.";

	/* Build the component SaveLoad array on first call and don't touch it later on */
	if (saveloads.empty()) {
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

		int i = 0;
		do {
			saveloads.push_back(_component_desc[i++]);
		} while (saveloads.back().cmd != SL_END);
	}

	return &saveloads[0];
}

/**
 * Get a SaveLoad description for a link graph.
 * @return the SaveLoad array to save/load a link graph
 */
const SaveLoad *GetLinkGraphDesc() {

	static const SaveLoad _linkgraph_desc[] = {
		 SLE_CONDVAR(LinkGraph, current_component_id, SLE_UINT16, SL_COMPONENTS, SL_MAX_VERSION),
		 SLE_CONDVAR(LinkGraph, current_station_id,   SLE_UINT16, SL_COMPONENTS, SL_MAX_VERSION),
		 SLE_CONDVAR(LinkGraph, cargo,                SLE_UINT8,  SL_COMPONENTS, SL_MAX_VERSION),
		 SLE_END()
	};

	return _linkgraph_desc;
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
static void SaveLoad_LinkGraphComponent(LinkGraphComponent *comp) {
	for (NodeID from = 0; from < comp->GetSize(); ++from) {
		Node *node = &comp->GetNode(from);
		SlObject(node, _node_desc);
		for (NodeID to = 0; to < comp->GetSize(); ++to) {
			SlObject(&comp->GetEdge(from, to), _edge_desc);
		}
	}
}

/**
 * Save all link graphs.
 */
static void DoSave_LGRP(void *)
{
	for(CargoID cargo = CT_BEGIN; cargo != CT_END; ++cargo) {
		LinkGraph &graph = _link_graphs[cargo];
		SlObject(&graph, GetLinkGraphDesc());

		LinkGraphJob *job = graph.GetCurrentJob();
		LinkGraphComponent *comp = job->GetComponent();
		_join_date = job->GetJoinDate();
		SlObject(comp, GetLinkGraphComponentDesc());
		SaveLoad_LinkGraphComponent(comp);
	}
}

/**
 * Load all link graphs.
 */
static void Load_LGRP()
{
	for(CargoID cargo = CT_BEGIN; cargo != CT_END; ++cargo) {
		LinkGraph &graph = _link_graphs[cargo];
		SlObject(&graph, GetLinkGraphDesc());
		LinkGraphComponent *comp = new LinkGraphComponent(cargo);
		SlObject(comp, GetLinkGraphComponentDesc());
		comp->SetSize(comp->GetSize());
		SaveLoad_LinkGraphComponent(comp);
		graph.AddComponent(comp, _join_date);
	}
}

static void Save_LGRP() {
	SlAutolength((AutolengthProc*)DoSave_LGRP, NULL);
}

extern const ChunkHandler _linkgraph_chunk_handlers[] = {
	{ 'LGRP', Save_LGRP,      Load_LGRP,	NULL,      CH_LAST},
};
