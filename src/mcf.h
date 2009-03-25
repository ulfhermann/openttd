/*
 * mcf.h
 *
 *  Created on: 20.03.2009
 *      Author: alve
 */

#ifndef MCF_H_
#define MCF_H_

#include "linkgraph.h"
#include <glpk.h>
#include <map>

class Path {
public:
	Path(uint i) : id(i), capacity(UINT_MAX) {}
	uint id;
	uint capacity;
};

typedef std::list<Path *> PathList;

class PathEntry {
public:
	PathEntry(NodeID n, PathEntry * p = NULL) : prev(p), dest(n), num_forks(0) {}
	PathEntry * fork (NodeID next);
	PathEntry * prev;
	NodeID dest;
	uint num_forks;
	bool HasPredecessor(NodeID node);
};

typedef std::multimap<Edge *, Path *> PathMapping;
typedef std::multimap<NodeID, Path *> SourceMapping;
typedef std::list<PathEntry *> PathTree;

class MultiCommodityFlow : public ComponentHandler {
public:
	virtual void Run(Component * graph);
	MultiCommodityFlow() : graph(NULL), lp(NULL) {}
	virtual ~MultiCommodityFlow() {}
private:
	void BuildPathForest();
	void BuildPathTree(NodeID source, NodeID dest, PathTree & tree);
	void SetPathConstraints();    // capacity(path) <= min capacity of edges
	void SetSourceConstraints();  // sum of all paths with origin n <= supply(n)
	void SetEdgeConstraints();    // sum of all paths passing edge e <= capacity(e)
	void SetDemandContraints(NodeID source, NodeID dest, PathList paths); // sum of all paths <= demand(source, dest)
	void Solve();
	static ThreadMutex * mutex();
	SourceMapping source_mapping;
	PathMapping path_mapping;
	Component * graph;
	glp_prob * lp;
};

#endif /* MCF_H_ */
