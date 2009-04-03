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
		float flow_sum = 0;
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
	scale_factors.resize(component->GetSize(), 0);
	for (NodeID node_id = 0; node_id < component->GetSize(); ++node_id) {
		Node & node = component->GetNode(node_id);
		StationID via = node.station;
		PathSet & paths = node.paths;
		for(PathSet::iterator i = paths.begin(); i != paths.end(); ++i) {
			Path * path = *i;
			if (path->GetParent() == NULL) {
				continue;
			}
			NodeID origin_node = path->GetOrigin();
			StationID origin = component->GetNode(origin_node).station;
			float flow = scale_factors[origin_node] * path->GetFlow();
			NodeID prev = path->GetParent()->GetNode();
			component->GetNode(prev).flows[origin][via] += flow;

			if (path->GetNumChildren() == 0) {
				node.flows[origin][via] += flow;
			}
		}
	}
}
