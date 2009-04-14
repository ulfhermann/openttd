/*
 * flowmapper.cpp
 *
 *  Created on: 31.03.2009
 *      Author: alve
 */

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
			Node & prev_node = component->GetNode(path->GetParent()->GetNode());
			/* mark all of the flow for local consumation at first */
			node.flows[origin][via] += flow;
			/* pass some of the flow marked for local consumation at prev on to this node */
			prev_node.flows[origin][via] += flow;
			StationID prev = prev_node.station;
			if (origin != prev) {
				prev_node.flows[origin][prev] -= flow;
			}
		}
	}
}
