/*
 * mcf.cpp
 *
 *  Created on: 20.03.2009
 *      Author: alve
 */

#include "mcf.h"

ThreadMutex * MultiCommodityFlow::mutex() {
	static ThreadMutex * m = ThreadMutex::New();
	return m;
}

void MultiCommodityFlow::Run(Component * g) {
	graph = g;
	mutex()->BeginCritical();
	BuildPathForest();
	SetPathConstraints();
	SetSourceConstraints();
	SetEdgeConstraints();
	Solve();
	mutex()->EndCritical();
}

void MultiCommodityFlow::BuildPathForest() {
	uint path_id = 1;
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
					entry = predecessor;

					path_mapping.insert(std::make_pair(&edge, path));
				}
			}
			if (entry != NULL) {
				delete entry;
			}
			SetDemandContraints(source, dest, paths);
		}
	}
}


void MultiCommodityFlow::BuildPathTree(NodeID source, NodeID dest, PathTree & tree) {
	PathEntry * origin = new PathEntry(source);
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
				tree.push_back(path->fork(next));
			} else if (!path->HasPredecessor(next)) {
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
	uint paths_size = paths.size();
	if (paths_size == 0) {
		return; //nothing to do
	}
	glp_add_cols(lp, paths_size);
	int row = glp_add_rows(lp, 1);
	glp_set_row_bnds(lp, row, GLP_DB, 0.0, graph->GetEdge(source, dest).demand);

	int * cols = new int[1+paths_size];
	double * vals = new double[1+paths_size];

	int col = 1;
	for (PathList::iterator i = paths.begin(); i != paths.end(); ++i) {
		cols[col] = (*i)->id;
		vals[col] = 1.0;
		col++;
	}
	glp_set_mat_row(lp, row, paths_size, cols, vals);
	delete[] cols;
	delete[] vals;
}

void MultiCommodityFlow::SetPathConstraints() {
	for (SourceMapping::iterator i = source_mapping.begin(); i != source_mapping.end(); ++i) {
		Path * p = i->second;
		glp_set_col_bnds(lp, p->id, GLP_DB, 0.0, p->capacity);
		glp_set_obj_coef(lp, p->id, 1.0);
	}
}

void MultiCommodityFlow::SetSourceConstraints() {
	NodeID source = UINT_MAX;
	uint num_paths = 0;
	uint index = 1;
	int * cols = 0;
	double * vals = 0;
	int row = 0;
	for (SourceMapping::iterator i = source_mapping.begin(); i != source_mapping.end(); ++i) {
		if (i->first != source) {
			if (num_paths != 0) {
				glp_set_mat_row(lp, row, num_paths, cols, vals);
				index = 1;
			}
			source = i->first;
			row = glp_add_rows(lp, 1);
			uint new_num = source_mapping.count(source);
			if (new_num > num_paths) {
				delete[] cols;
				delete[] vals;
				cols = new int[1+new_num];
				vals = new double[1+new_num];
			}
			num_paths = new_num;
		}
		Path * p = i->second;
		cols[index] = p->id;
		vals[index] = 1.0;
		index++;
	}
	delete[] cols;
	delete[] vals;
}

void MultiCommodityFlow::SetEdgeConstraints() {
	Edge * edge = 0;
	uint num_paths = 0;
	uint index = 1;
	// TODO: save maximum size
	int * cols = 0;
	double * vals = 0;
	int row = 0;
	for (PathMapping::iterator i = path_mapping.begin(); i != path_mapping.end(); ++i) {
		if (i->first != edge) {
			if (num_paths != 0) {
				glp_set_mat_row(lp, row, num_paths, cols, vals);
				index = 1;
			}
			edge = i->first;
			row = glp_add_rows(lp, 1);
			uint new_num = path_mapping.count(edge);
			if (new_num > num_paths) {
				delete[] cols;
				delete[] vals;
				cols = new int[1+new_num];
				vals = new double[1+new_num];
			}
			num_paths = new_num;
		}
		Path * p = i->second;
		cols[index] = p->id;
		vals[index] = 1.0;
		index++;
	}
	delete[] cols;
	delete[] vals;
}

void MultiCommodityFlow::Solve() {
	glp_set_obj_dir(lp, GLP_MAX);
	glp_write_lp(lp, NULL, "/dev/stdout");
	glp_simplex(lp, NULL);
/*	assert(set_add_rowmode(lp, FALSE));
	uint cols = get_Ncolumns(lp);
	REAL * row = (REAL *)malloc(cols + 1 * sizeof(*row));
	for (uint i = 0; i <= cols; ++i) {
		row[i] = 1;
	}
	assert(set_obj_fn(lp, row));
	set_maxim(lp);

	write_LP(lp, stdout);
	if (solve(lp) != OPTIMAL) {
		printf("couldn't find an optimal solution!\n");
	}



    printf("Objective value: %f\n", get_objective(lp));


    get_variables(lp, row);
    for(uint j = 0; j < cols; j++) {
      printf("%i: %f\n", j + 1, row[j]);
    }
    delete row;
    */
}
