/*
 * mcf.cpp
 *
 *  Created on: 20.03.2009
 *      Author: alve
 */

#include "mcf.h"

void MultiCommodityFlow::Run(Component * g) {
	graph = g;
	set_add_rowmode(lp, TRUE);
	BuildPathForest();
	set_add_rowmode(lp, FALSE);
	uint cols = get_Ncolumns(lp) + 1;
	REAL * row = (REAL *)malloc(cols * sizeof(*row));
	for (uint i = 0; i < cols; ++i) {
		row[i] = 1;
	}
	set_obj_fn(lp, row);
	free(row);
	set_maxim(lp);
	set_add_rowmode(lp, TRUE);
	SetPathConstraints();
	SetSourceConstraints();
	SetEdgeConstraints();

	Solve();
}

void MultiCommodityFlow::BuildPathForest() {
	uint path_id = 0;
	for(NodeID source = 0; source < graph->GetSize(); ++source) {
		for (NodeID dest = 0; dest < graph->GetSize(); ++dest) {
			if(dest == source || graph->GetEdge(source, dest).demand == 0) {
				continue;
			}
			PathList paths;
			PathTree tree;
			BuildPathTree(source, dest, tree);
			PathEntry * entry = NULL;
			while(!tree.empty()) {
				PathEntry * entry = tree.front();
				assert(entry->HasPredecessor(source));
				tree.pop_front();
				Path * path = new Path(path_id++);
				paths.push_back(path);
				source_mapping.insert(std::make_pair(source, path));
				while(entry->prev != NULL) {
					PathEntry * predecessor = entry->prev;

					Edge & edge = graph->GetEdge(predecessor->dest, entry->dest);
					if (edge.capacity < path->capacity) {
						path->capacity = edge.capacity;
					}
					if (entry->num_forks == 0) {
						delete entry;
						predecessor->num_forks--;
					}
					assert(entry->dest != source);
					entry = predecessor;

					path_mapping.insert(std::make_pair(&edge, path));
				}
				assert(entry->dest == source);
			}
			if (entry != NULL) {
				assert(entry->num_forks == 0);
				delete entry;
			}
			SetDemandContraints(source, dest, paths);
		}
	}
}


void MultiCommodityFlow::BuildPathTree(NodeID source, NodeID dest, PathTree & tree) {
	PathEntry * origin = new PathEntry(source);
	assert(origin->prev == NULL);
	assert(origin->dest == source);
	PathTree growing_paths;
	growing_paths.push_back(origin);
	while(!growing_paths.empty()) {
		PathEntry * path = growing_paths.front();
		NodeID prev = path->dest;
		growing_paths.pop_front();
		for (NodeID next = 0; next < graph->GetSize(); ++next) {
			assert(path == origin || path->HasPredecessor(source));
			if (prev == next) continue;
			if (graph->GetEdge(prev, next).capacity == 0) continue;
			if (next == dest) {
				assert(!path->HasPredecessor(next));
				assert(path == origin || path->HasPredecessor(source));
				tree.push_back(path->fork(next));
			} else if (!path->HasPredecessor(next)) {
				assert(path == origin || path->HasPredecessor(source));
				growing_paths.push_back(path->fork(next));
			}
		}
	}
}

PathEntry * PathEntry::fork(NodeID next) {
	num_forks++;
	return new PathEntry(next, this);
}

bool PathEntry::HasPredecessor(NodeID node) {
	assert(node != dest);
	PathEntry * entry = prev;
	while(entry != NULL) {
		if (entry->dest == node) {
			return true;
		}
		entry = entry->prev;
	}
	return false;
}

void MultiCommodityFlow::SetDemandContraints(NodeID source, NodeID dest, PathList paths) {
	int ncol = get_Ncolumns(lp);
	int nrow = get_Nrows(lp);
	int paths_size = paths.size();
	int new_size = ncol + paths_size;
	resize_lp(lp, nrow, new_size);
	REAL * row = 0;
	int * colno = 0;
	row = (REAL *) malloc(paths_size * sizeof(*row));
	colno = (int *) malloc(paths_size * sizeof(*colno));

	int col = 0;
	for (PathList::iterator i = paths.begin(); i != paths.end(); ++i) {
		row[col] = 1;
		colno[col++] = (*i)->id;
	}

	add_constraintex(lp, paths_size, row, colno, LE, graph->GetEdge(source, dest).demand);
	free(row);
	free(colno);
}

void MultiCommodityFlow::SetPathConstraints() {
	for (SourceMapping::iterator i = source_mapping.begin(); i != source_mapping.end(); ++i) {
		Path * p = i->second;
		REAL row[1] = {1};
		int colno[1] = {p->id};
		add_constraintex(lp, 1, row, colno, LE, p->capacity);
	}
}

void MultiCommodityFlow::SetSourceConstraints() {
	NodeID source = UINT_MAX;
	uint num_paths = 0;
	uint index = 0;
	REAL * row = 0;
	int * colno = 0;
	for (SourceMapping::iterator i = source_mapping.begin(); i != source_mapping.end(); ++i) {
		if (i->first != source) {
			if (num_paths != 0) {
				add_constraintex(lp, num_paths, row, colno, LE, graph->GetNode(source).supply);
				index = 0;
			}
			source = i->first;
			uint new_num = source_mapping.count(source);
			if (new_num > num_paths) {
				free(row);
				free(colno);
				row = (REAL *)malloc(new_num * sizeof(*row));
				colno = (int *)malloc(new_num * sizeof(*colno));
			}

			num_paths = new_num;
		}
		Path * p = i->second;
		row[index] = 1;
		colno[index] = p->id;
		index++;
	}
	free(row);
	free(colno);
}

void MultiCommodityFlow::SetEdgeConstraints() {
	Edge * edge = 0;
	uint num_paths = 0;
	uint index = 0;
	// TODO: save maximum size
	REAL * row = 0;
	int * colno = 0;
	for (PathMapping::iterator i = path_mapping.begin(); i != path_mapping.end(); ++i) {
		if (i->first != edge) {
			if (num_paths != 0) {
				add_constraintex(lp, num_paths, row, colno, LE, edge->capacity);
				index = 0;
			}
			edge = i->first;
			uint new_num = path_mapping.count(edge);
			if (new_num > num_paths) {
				free(row);
				free(colno);
				row = (REAL *)malloc(new_num * sizeof(*row));
				colno = (int *)malloc(new_num * sizeof(*colno));
			}
			num_paths = new_num;
		}
		Path * p = i->second;
		row[index] = 1;
		colno[index] = p->id;
		index++;
	}
	free(row);
	free(colno);
}

void MultiCommodityFlow::Solve() {
	set_add_rowmode(lp, FALSE);
	write_LP(lp, stdout);
	if (solve(lp) != OPTIMAL) {
		printf("couldn't find an optimal solution!\n");
	}
	/* a solution is calculated, now lets get some results */

    /* objective value */
    printf("Objective value: %f\n", get_objective(lp));
    /* variable values */
    uint cols = get_Ncolumns(lp);
    REAL * row = (REAL *)malloc(cols * sizeof(*row));
    get_variables(lp, row);
    for(uint j = 0; j < cols; j++) {
      printf("%s: %f\n", get_col_name(lp, j + 1), row[j]);
    }
    delete row;
    /* we are done now */
}
