/** @file flowmapper.cpp Definition of flowmapper */

#include "flowmapper.h"

void FlowMapper::Run(LinkGraphComponent * c) {
	component = c;
	for (NodeID node_id = 0; node_id < component->GetSize(); ++node_id) {
		Node & prev_node = component->GetNode(node_id);
		StationID prev = prev_node.station;
		PathSet & paths = prev_node.paths;
		for(PathSet::iterator i = paths.begin(); i != paths.end(); ++i) {
			Path * path = *i;
			uint flow = path->GetFlow();
			if (flow == 0) continue;
			Node & node = component->GetNode(path->GetNode());
			StationID via = node.station;
			assert(prev != via);
			StationID origin = component->GetNode(path->GetOrigin()).station;
			assert(via != origin);
			/* mark all of the flow for local consumation at first */
			node.flows[origin][via] += flow;
			/* pass some of the flow marked for local consumation at prev on to this node */
			prev_node.flows[origin][via] += flow;
			/* find simple circular flows ... */
			assert(node.flows[origin][prev] == 0);
			if (prev != origin) {
				prev_node.flows[origin][prev] -= flow;
			}
		}
	}
	for (NodeID node_id = 0; node_id < component->GetSize(); ++node_id) {
		PathSet & paths = component->GetNode(node_id).paths;
		for (PathSet::iterator i = paths.begin(); i != paths.end(); ++i) {
			delete (*i);
		}
		paths.clear();
	}
}
