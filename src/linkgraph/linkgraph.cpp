/** @file linkgraph.cpp Definition of link graph classes used for cargo distribution. */

#include "linkgraph.h"
#include "../date_func.h"
#include "../variables.h"
#include "../map_func.h"
#include "../core/bitmath_func.hpp"
#include "../debug.h"
#include "../moving_average.h"
#include <queue>

LinkGraph _link_graphs[NUM_CARGO];

typedef std::map<StationID, NodeID> ReverseNodeIndex;

void LinkGraph::CreateComponent(Station * first) {
	ReverseNodeIndex index;
	NodeID node = 0;
	std::queue<Station *> search_queue;
	LinkGraphComponent * component = NULL;

	search_queue.push(first);

	first->goods[this->cargo].last_component = this->current_component_id;
	component = new LinkGraphComponent(this->cargo, this->current_component_id);
	GoodsEntry & good = first->goods[this->cargo];
	node = component->AddNode(this->current_station_id, good.supply.Value(), good.acceptance);
	this->component_acceptance[this->current_component_id] = good.acceptance;
	index[this->current_station_id++] = node;
	// find all stations belonging to the current component
	while(!search_queue.empty()) {
		Station * source = search_queue.front();
		StationID source_id = source->index;
		search_queue.pop();
		GoodsEntry & good = source->goods[cargo];
		LinkStatMap & links = good.link_stats;
		for(LinkStatMap::iterator i = links.begin(); i != links.end(); ++i) {
			StationID target_id = i->first;
			Station *target = Station::GetIfValid(target_id);
			if (target == NULL) {
				continue;
			}
			assert(target_id != source_id);
			LinkStat & link_stat = i->second;
			ReverseNodeIndex::iterator index_it = index.find(target_id);
			if (index_it == index.end()) {
				GoodsEntry & good = target->goods[cargo];
				good.last_component = this->current_component_id;
				search_queue.push(target);
				node = component->AddNode(target_id, good.supply.Value(), good.acceptance);
				this->component_acceptance[this->current_component_id] += good.acceptance;
				index[target_id] = node;
			} else {
				node = index_it->second;
			}
			
			assert(!link_stat.IsNull());
			component->AddEdge(index[source_id], node, link_stat.Capacity());
		}
	}
	// here the list of nodes and edges for this component is complete.
	component->CalculateDistances();
	LinkGraphJob * job = new LinkGraphJob(component);
	assert(job != NULL);
	job->SpawnThread(this->cargo);
	this->jobs.push_back(job);
}

void LinkGraph::NextComponent()
{
	while (!Station::IsValidID(this->current_station_id) && this->current_station_id > 0) {
		--this->current_station_id;
	}
	StationID last_station_id = this->current_station_id;

	do {
		// find first station of next component
		if (Station::IsValidID(this->current_station_id)) {
			Station * station = Station::Get(this->current_station_id);
			GoodsEntry & ge = station->goods[this->cargo];
			if ((ge.last_component + this->current_component_id) % 2 != 0) {
				// has not been seen in this run through the graph

				LinkStatMap & links = station->goods[cargo].link_stats;
				if (!links.empty()) {
					this->current_component_id += 2;
					if (this->current_component_id >= LinkGraph::MAX_COMPONENTS) {
						if (this->current_component_id % 2 == 0) {
							this->current_component_id = 0;
						} else {
							this->current_component_id = 1;
						}
					}
					CreateComponent(station);
					return;
				}
			}
		}

		if (++this->current_station_id == Station::GetPoolSize()) {
			this->current_station_id = 0;
			if (this->current_component_id % 2 == 0) {
				this->current_component_id = 1;
			} else {
				this->current_component_id = 0;
			}
		}
	} while (this->current_station_id != last_station_id);
}

void OnTick_LinkGraph()
{
	bool spawn = (_tick_counter + LinkGraph::COMPONENTS_SPAWN_TICK) % DAY_TICKS == 0;
	bool join =  (_tick_counter + LinkGraph::COMPONENTS_JOIN_TICK)  % DAY_TICKS == 0;
	if (spawn || join) {
		for(CargoID cargo = CT_BEGIN; cargo != CT_END; ++cargo) {
			if ((_date + cargo) % _settings_game.linkgraph.recalc_interval == 0) {
				LinkGraph & graph = _link_graphs[cargo];
				if (spawn) {
					graph.NextComponent();
				} else {
					graph.Join();
				}
			}
		}
	}
}

LinkGraph::LinkGraph()  : current_component_id(1), current_station_id(0), cargo(CT_INVALID)
{
	for (CargoID i = CT_BEGIN; i != CT_END; ++i) {
		if (this == &(_link_graphs[i])) {
			cargo = i;
		}
	}
}

NodeID LinkGraphComponent::AddNode(StationID st, uint supply, uint demand) {
	nodes.push_back(Node(st, supply, demand));
	for(NodeID i = 0; i < num_nodes; ++i) {
		edges[i].push_back(Edge());
	}
	edges.push_back(std::vector<Edge>(++num_nodes));
	return num_nodes - 1;
}

void LinkGraphComponent::AddEdge(NodeID from, NodeID to, uint capacity) {
	assert(from != to);
	edges[from][to].capacity = capacity;
}

void LinkGraphComponent::CalculateDistances() {
	for(NodeID i = 0; i < num_nodes; ++i) {
		for(NodeID j = 0; j < i; ++j) {
			Station * st1 = Station::Get(nodes[i].station);
			Station * st2 = Station::Get(nodes[j].station);
			uint distance = DistanceManhattan(st1->xy, st2->xy);
			edges[i][j].distance = distance;
			edges[j][i].distance = distance;
		}
	}
}

void LinkGraphComponent::SetSize(uint size) {
	num_nodes = size;
	nodes.resize(num_nodes);
	edges.resize(num_nodes, std::vector<Edge>(num_nodes));
}

LinkGraphComponent::LinkGraphComponent(CargoID car, LinkGraphComponentID col) :
	settings(_settings_game.linkgraph),
	cargo(car),
	num_nodes(0),
	index(col)
{
}

void LinkGraph::Join() {
	while (!this->jobs.empty()) {
		LinkGraphJob * job = this->jobs.front();
		assert(job != NULL);

		/* also join if join date is far in the future. This prevents excessive memory use when resetting time */
		if (job->GetJoinDate() > _date && job->GetJoinDate() <= _date + _settings_game.linkgraph.recalc_interval) {
			return;
		}
		job->Join();

		delete job;
		this->jobs.pop_front();
	}
}

void LinkGraph::AddComponent(LinkGraphComponent * component, uint join) {
	LinkGraphComponentID index = component->GetIndex();
	for(NodeID i = 0; i < component->GetSize(); ++i) {
		Station *station = Station::GetIfValid(component->GetNode(i).station);
		if (station != NULL) {
			station->goods[cargo].last_component = index;
		}
	}
	LinkGraphJob * job = new LinkGraphJob(component, join);
	assert(job != NULL);
	job->SpawnThread(this->cargo);
	this->jobs.push_back(job);
}

void LinkGraphJob::Run() {
	for (HandlerList::iterator i = this->handlers.begin(); i != this->handlers.end(); ++i) {
		ComponentHandler * handler = *i;
		handler->Run(this->component);
	}
}

LinkGraphJob::~LinkGraphJob() {
	for (HandlerList::iterator i = this->handlers.begin(); i != this->handlers.end(); ++i) {
		ComponentHandler * handler = *i;
		delete handler;
	}
	this->handlers.clear();
	DEBUG(misc, 2, "removing job for cargo %d with index %d and join date %d at %d", this->component->GetCargo(),
			this->component->GetIndex(), this->join_date, _date);
	delete this->component;
	delete this->thread;
}

void RunLinkGraphJob(void * j) {
	LinkGraphJob * job = (LinkGraphJob *)j;
	job->Run();
}

void LinkGraphJob::SpawnThread(CargoID cargo) {
	if (!ThreadObject::New(&(RunLinkGraphJob), this, &thread)) {
		thread = NULL;
		// Of course this will hang a bit.
		// On the other hand, if you want to play games which make this hang noticably
		// on a platform without threads then you'll probably get other problems first.
		// OK:
		// If someone comes and tells me that this hangs for him/her, I'll implement a
		// smaller grained "Step" method for all handlers and add some more ticks where
		// "Step" is called. No problem in principle.
		RunLinkGraphJob(this);
	}
}

uint LinkGraph::GetComponentAcceptance(LinkGraphComponentID id)
{
	return this->component_acceptance[id];
}

LinkGraphJob::LinkGraphJob(LinkGraphComponent * c) :
	thread(NULL),
	join_date(_date + c->GetSettings().recalc_interval),
	component(c)
{
	DEBUG(misc, 2, "new job for cargo %d with index %d and join date %d at %d", c->GetCargo(), c->GetIndex(), join_date, _date);
}

LinkGraphJob::LinkGraphJob(LinkGraphComponent * c, Date join) :
	thread(NULL),
	join_date(join),
	component(c)
{
	DEBUG(misc, 2, "new job for cargo %d with index %d and join date %d at %d", c->GetCargo(), c->GetIndex(), join_date, _date);
}

void LinkGraph::Clear() {
	for (JobList::iterator i = jobs.begin(); i != jobs.end(); ++i) {
		LinkGraphJob * job = *i;
		assert(job != NULL);
		job->Join();
		delete job;
	}
	jobs.clear();
	current_component_id = 1;
	current_station_id = 0;
}

void InitializeLinkGraphs() {
	for (CargoID c = CT_BEGIN; c != CT_END; ++c) _link_graphs[c].Clear();
}
