/** @file linkgraph_sl.cpp Code handling saving and loading of link graphs */

#include "../linkgraph/linkgraph.h"
#include "../settings_internal.h"
#include "saveload.h"
#include <vector>

const SettingDesc *GetSettingDescription(uint index);

static uint32 _num_components;
static Date _join_date;

enum {
	LGRP_GRAPH = 0,
	LGRP_COMPONENT = 1,
	LGRP_NODE = 2,
	LGRP_EDGE = 3,
};

const SaveLoad * GetLinkGraphComponentDesc() {

	static const SaveLoad _component_desc[] = {
		 SLE_CONDVAR(LinkGraphComponent, num_nodes,        SLE_UINT32, LINKGRAPH_SV, SL_MAX_VERSION),
		 SLE_CONDVAR(LinkGraphComponent, index,            SLE_UINT16, LINKGRAPH_SV, SL_MAX_VERSION),
		SLEG_CONDVAR(                    _join_date,       SLE_INT32,  LINKGRAPH_SV, SL_MAX_VERSION),
		 SLE_END()
	};

	size_t offset_gamesettings = cpp_offsetof(GameSettings, linkgraph);
	size_t offset_component = cpp_offsetof(LinkGraphComponent, settings);

	typedef std::vector<SaveLoad> SaveLoadVector;
	static SaveLoadVector saveloads;
	static const char * prefix = "linkgraph.";
	size_t prefixlen = strlen(prefix);

	int setting = 0;
	const SettingDesc * desc = GetSettingDescription(setting);
	while (desc->save.cmd != SL_END) {
		if (desc->desc.name != NULL && strncmp(desc->desc.name, prefix, prefixlen) == 0) {
			SaveLoad sl = desc->save;
			char *& address = reinterpret_cast<char *&>(sl.address);
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

	return &saveloads[0];
}

const SaveLoad *GetGlobalCargoAcceptanceDesc()
{
	static const SaveLoad acceptance_desc[] = {
		SLE_CONDARR(GlobalCargoAcceptance, acceptance,        SLE_UINT, NUM_CARGO, SUPPLY_SV, SL_MAX_VERSION),
		SLE_CONDARR(GlobalCargoAcceptance, current_tile_loop, SLE_UINT, NUM_CARGO, SUPPLY_SV, SL_MAX_VERSION),
		SLE_END()
	};

	return acceptance_desc;
}

const SaveLoad * GetLinkGraphDesc(uint type) {

	static const SaveLoad _linkgraph_desc[] = {
		SLEG_CONDVAR(           _num_components,      SLE_UINT32,                            LINKGRAPH_SV, SL_MAX_VERSION),
		 SLE_CONDVAR(LinkGraph, current_component_id, SLE_UINT16,                            LINKGRAPH_SV, SL_MAX_VERSION),
		 SLE_CONDVAR(LinkGraph, current_station_id,   SLE_UINT16,                            LINKGRAPH_SV, SL_MAX_VERSION),
		 SLE_CONDVAR(LinkGraph, cargo,                SLE_UINT8,                             LINKGRAPH_SV, SL_MAX_VERSION),
		 SLE_CONDARR(LinkGraph, component_acceptance, SLE_UINT,   LinkGraph::MAX_COMPONENTS, SUPPLY_SV,    SL_MAX_VERSION),
		 SLE_END()
	};

	static const SaveLoad * _component_desc = GetLinkGraphComponentDesc();

	// edges and nodes are saved in the correct order, so we don't need to save their ids.

	static const SaveLoad _node_desc[] = {
		 SLE_CONDVAR(Node, supply,    SLE_UINT32, LINKGRAPH_SV, SL_MAX_VERSION),
		 SLE_CONDVAR(Node, demand,    SLE_UINT32, LINKGRAPH_SV, SL_MAX_VERSION),
		 SLE_CONDVAR(Node, station,   SLE_UINT16, LINKGRAPH_SV, SL_MAX_VERSION),
		 SLE_END()
	};

	static const SaveLoad _edge_desc[] = {
		 SLE_CONDVAR(Edge, distance,  SLE_UINT32, LINKGRAPH_SV, SL_MAX_VERSION),
		 SLE_CONDVAR(Edge, capacity,  SLE_UINT32, LINKGRAPH_SV, SL_MAX_VERSION),
		 SLE_END()
	};

	static const SaveLoad *_lgrp_descs[] = {
		_linkgraph_desc,
		_component_desc,
		_node_desc,
		_edge_desc,
	};

	return _lgrp_descs[type];

}

static void SaveLoad_LinkGraphComponent(LinkGraphComponent * comp) {
	for (NodeID from = 0; from < comp->GetSize(); ++from) {
		Node * node = &comp->GetNode(from);
		SlObject(node, GetLinkGraphDesc(LGRP_NODE));
		for (NodeID to = 0; to < comp->GetSize(); ++to) {
			SlObject(&comp->GetEdge(from, to), GetLinkGraphDesc(LGRP_EDGE));
		}
	}
}

static void DoSave_LGRP(void *)
{
	for(CargoID cargo = CT_BEGIN; cargo != CT_END; ++cargo) {
		LinkGraph & graph = _link_graphs[cargo];
		_num_components = (uint32)graph.GetNumJobs();
		SlObject(&graph, GetLinkGraphDesc(LGRP_GRAPH));
		JobList & jobs = graph.GetJobs();
		for (JobList::iterator i = jobs.begin(); i != jobs.end(); ++i) {
			LinkGraphJob * job = *i;
			LinkGraphComponent * comp = job->GetComponent();
			_join_date = job->GetJoinDate();
			SlObject(comp, GetLinkGraphDesc(LGRP_COMPONENT));
			SaveLoad_LinkGraphComponent(comp);
		}
	}

	SlObject(&GlobalCargoAcceptance::inst, GetGlobalCargoAcceptanceDesc());
}

static void Load_LGRP()
{
	for(CargoID cargo = CT_BEGIN; cargo != CT_END; ++cargo) {
		LinkGraph & graph = _link_graphs[cargo];
		SlObject(&graph, GetLinkGraphDesc(LGRP_GRAPH));
		for (uint32 i = 0; i < _num_components; ++i) {
			LinkGraphComponent * comp = new LinkGraphComponent(cargo);
			SlObject(comp, GetLinkGraphDesc(LGRP_COMPONENT));
			comp->SetSize(comp->GetSize());
			SaveLoad_LinkGraphComponent(comp);
			graph.AddComponent(comp, _join_date);
		}
	}

	SlObject(&GlobalCargoAcceptance::inst, GetGlobalCargoAcceptanceDesc());
}

static void Save_LGRP() {
	SlAutolength((AutolengthProc*)DoSave_LGRP, NULL);
}

extern const ChunkHandler _linkgraph_chunk_handlers[] = {
	{ 'LGRP', Save_LGRP,      Load_LGRP,	NULL,      CH_LAST},
};
