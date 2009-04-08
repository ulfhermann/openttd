/*
 * flowmapper.cpp
 *
 *  Created on: 31.03.2009
 *      Author: alve
 */

#include "flowmapper.h"

void FlowMapper::CalcScaleFactors() {
	scale_factors.resize(component->GetSize(), 0);
	for (NodeID node_id = 0; node_id < component->GetSize(); ++node_id) {
		Number flow_sum = 0;
		Node & node = component->GetNode(node_id);
		PathSet & paths = node.paths;
		for(PathSet::iterator i = paths.begin(); i != paths.end(); ++i) {
			Path * path = *i;
			if (path->GetParent() == NULL) {
				flow_sum += path->GetFlow();
			}
		}
		scale_factors[node_id] = node.supply / flow_sum;
	}
}

void FlowMapper::Run(Component * c) {
	component = c;
	CalcScaleFactors();
	for (NodeID node_id = 0; node_id < component->GetSize(); ++node_id) {
		Node & node = component->GetNode(node_id);
		StationID via = node.station;
		PathSet & paths = node.paths;
		for(PathSet::iterator i = paths.begin(); i != paths.end(); ++i) {
			Path * path = *i;
			if (path->GetParent() == NULL) {
				continue;
			}
			NodeID origin_node_id = path->GetOrigin();
			StationID origin = component->GetNode(origin_node_id).station;
			Number flow = scale_factors[origin_node_id] * path->GetFlow();
			Node & prev_node = component->GetNode(path->GetParent()->GetNode());
			/* mark all of the flow for local consumation at first */
			node.flows[origin][via] += flow;
			/* pass some of the flow marked for local consumation at prev on to this node */
			prev_node.flows[origin][via] += flow;
			prev_node.flows[origin][prev_node.station] -= flow;
		}
	}
}
