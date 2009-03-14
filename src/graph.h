/*
 * graph.h
 *
 *  Created on: 28.02.2009
 *      Author: alve
 */

#ifndef GRAPH_H_
#define GRAPH_H_

#include "stdafx.h"
#include "station_base.h"
#include "cargo_type.h"
#include "thread.h"
#include "cargodist.h"
#include "demands.h"
#include <queue>

typedef ushort colour;

void runCargoDist(void * cargoDist);

struct SaveLoad;
extern const SaveLoad * getCargoDistDesc(uint);

class CargoDist {
public:
	CargoDist(CargoID cargo);
	CargoDist(const InitNodeList & nodes, const InitEdgeList & edges, uint numNodes, CargoID cargo, ushort colour);
	void join() {thread->Join();}
	void start() {ThreadObject::New(&(runCargoDist), this, &thread);}
	CargoDistGraph & getGraph() {return graph;}
	uint16 getJoinTime() const {return joinTime;}
	colour getColour() const {return componentColour;}
	static ComponentHandler handlers[NUM_CARGO];
private:
	friend const SaveLoad * getCargoDistDesc(uint);
	friend void runCargoDist(void *);
	ThreadObject * thread;
	void calculateDemands() {demands.calcDemands(graph);}
	void calculateUsage();
	CargoDistGraph graph;
	DemandCalculator demands;
	uint16 joinTime;
	colour componentColour;
};

typedef std::list<CargoDist *> CargoDistList;


class ComponentHandler {
public:
	ComponentHandler();
	void runAnalysis();
	colour getColor(StationID station) const {return stationColours[station];}
	CargoID getCargo() const {return cargo;}
	bool join();
	bool nextComponent();
	uint getNumComponents();
	CargoDistList & getComponents() {return components;}
	void addComponent(CargoDist * component);
	const static uint CARGO_DIST_TICK = 21;

private:
	friend const SaveLoad * getCargoDistDesc(uint);
	colour c;
	StationID currentStation;
	CargoID cargo;
	CargoDistList components;
	colour stationColours[Station_POOL_MAX_BLOCKS];
};








#endif /* GRAPH_H_ */
