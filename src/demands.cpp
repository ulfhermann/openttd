/*
 * demands.cpp
 *
 *  Created on: 08.03.2009
 *      Author: alve
 */

#include "demands.h"
#include "station_base.h"
#include <list>
#include <iostream>
using std::cout;

typedef std::list<uint> NodeList;

uint DemandCalculator::max_distance = 0;

void DemandCalculator::PrintDemandMatrix(Component & graph) {
	for (uint from = 0; from < graph.GetSize(); ++from) {
		cout << graph.Node(from).station << "\t";
		for(uint to = 0; to < graph.GetSize(); ++to) {
			if (from == to) {
				cout << graph.Node(from).supply << "\t";
			} else {
				cout << graph.Edge(from, to).demand << "\t";
			}
		}
		cout << "\n";
	}
}

void DemandCalculator::CalcSymmetric(Component & graph) {
	NodeList nodes;
	uint supply_sum = 0;
	for(uint node = 0; node < graph.GetSize(); node++) {
		nodes.push_back(node);
		supply_sum += graph.Node(node).supply;
	}

	if (supply_sum == 0) {
		return;
	}

	while(!nodes.empty()) {
		uint node1 = nodes.front();
		nodes.pop_front();

		Node & from = graph.Node(node1);

		for(NodeList::iterator i = nodes.begin(); i != nodes.end(); ++i) {
			uint node2 = *i;
			Edge & forward = graph.Edge(node1, node2);
			Edge & backward = graph.Edge(node2, node1);

			Node & to = graph.Node(node2);

			uint demand = from.supply * to.supply * (max_distance - forward.distance) / max_distance / supply_sum + 1;
			demand = min(demand, from.supply);
			demand = min(demand, to.supply);

			forward.demand += demand;
			backward.demand += demand;

			from.supply -= demand;
			to.supply -= demand;

			if (to.supply == 0) {
				i = nodes.erase(i);
				if (nodes.empty()) {
					// only one node left
					return;
				} else {
					--i;
				}
			}
			if (from.supply == 0) {
				break;
			}
		}
		if (from.supply != 0) {
			if (nodes.empty()) {
				// only one node left
				return;
			} else {
				nodes.push_back(node1);
			}
		}
	}
}
