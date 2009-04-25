/** @file flowmapper.cpp Definition of flowmapper */

#include "flowmapper.h"

void FlowMapper::Run(LinkGraphComponent * c) {
	component = c;
	for (NodeID node_id = 0; node_id < component->GetSize(); ++node_id) {
		Node & node = component->GetNode(node_id);
		StationID via = node.station;
		PathSet & paths = node.paths;
		for(PathSet::iterator i = paths.begin(); i != paths.end(); ++i) {
			Path * path = *i;
			if (path->GetParent() == NULL) {
				continue;
			}
			StationID origin = component->GetNode(path->GetOrigin()).station;
			uint flow = path->GetFlow();
			Path * parent = path->GetParent();
			assert(parent->GetFlow() >= flow);
			Node & prev_node = component->GetNode(parent->GetNode());
			/* mark all of the flow for local consumation at first */
			node.flows[origin][via] += flow;
			/* pass some of the flow marked for local consumation at prev on to this node */
			prev_node.flows[origin][via] += flow;
			StationID prev = prev_node.station;
			if (origin != prev) {
				/* find simple circular flows ... */
				assert(node.flows[origin][prev] == 0);
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
