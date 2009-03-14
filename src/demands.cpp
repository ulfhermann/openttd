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

uint DemandCalculator::maxDistance = 0;

void DemandCalculator::printDemandMatrix(CargoDistGraph & graph) {
	for (uint from = 0; from < graph.size(); ++from) {
		cout << graph.node(from).station << "\t";
		for(uint to = 0; to < graph.size(); ++to) {
			if (from == to) {
				cout << graph.node(from).supply << "\t";
			} else {
				cout << graph.edge(from, to).demand << "\t";
			}
		}
		cout << "\n";
	}
}

void DemandCalculator::calcSymmetric(CargoDistGraph & graph) {
	NodeList nodes;
	uint supplySum = 0;
	for(uint node = 0; node < graph.size(); node++) {
		nodes.push_back(node);
		supplySum += graph.node(node).supply;
	}

	if (supplySum == 0) {
		return;
	}

	while(!nodes.empty()) {
		uint node1 = nodes.front();
		nodes.pop_front();

		Node & from = graph.node(node1);

		for(NodeList::iterator i = nodes.begin(); i != nodes.end(); ++i) {
			uint node2 = *i;
			Edge & forward = graph.edge(node1, node2);
			Edge & backward = graph.edge(node2, node1);

			Node & to = graph.node(node2);

			uint demand = from.supply * to.supply * (maxDistance - forward.distance) / maxDistance / supplySum + 1;
			demand = min(demand, from.undeliveredSupply);
			demand = min(demand, to.undeliveredSupply);

			forward.demand += demand;
			backward.demand += demand;

			from.undeliveredSupply -= demand;
			to.undeliveredSupply -= demand;

			if (to.undeliveredSupply == 0) {
				i = nodes.erase(i);
				if (nodes.empty()) {
					// only one node left
					return;
				} else {
					--i;
				}
			}
			if (from.undeliveredSupply == 0) {
				break;
			}
		}
		if (from.undeliveredSupply != 0) {
			if (nodes.empty()) {
				// only one node left
				return;
			} else {
				nodes.push_back(node1);
			}
		}
	}
}
