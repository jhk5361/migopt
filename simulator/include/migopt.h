#ifndef __CHOPT_H__
#define __CHOPT_H__

#include <vector>
#include <unordered_map>
#include <stdint.h>
#include <limits.h>
#include <limits>
#include <deque>
#include <queue>
#include <tuple>
#include <algorithm>
#include <iostream>
#include <chrono>
#include <ctime>
#include <unordered_set>
#include <string>
#include <list>
#include "sim.h"
using namespace std;

#define INVALID -1
#define INVALID_REQ {(uint64_t)INVALID, OTHERS, INVALID}

#define MCMF_SAMPLE 10000LL

enum layer_type {
	LAYER_ACCESS = -1, // Access layer, used for the access trace
	LAYER_DEF_META = 0,
	LAYER_DEMO = 1,
	LAYER_DEMO_CHOKE = 2,
	LAYER_PROMO = 3,
	LAYER_PROMO_CHOKE = 4,
	LAYER_ALLOC = 5,
	LAYER_FIRST = 6,
	LAYER_TIER_CHOKE = 7,
	LAYER_END = 8,
	NUM_META_LAYERS
};

enum node_type {
	NODE_NULL = -1,
	NODE_SOURCE = 0, // LAYER_DEF_META
	NODE_CAP_CHOKE = 1, // LAYER_DEF_META
	NODE_SINK = 2, // LAYER_DEF_META
	NODE_ACCESS = 3, // LAYER_ACCESS
	NODE_FIRST = 4, // LAYER_FIRST
	NODE_TIER_CHOKE = 5, // LAYER_TIER_CHOKE
	NODE_END = 6, // LAYER_END
	NODE_INTERVAL_RETENTION = 7, // LAYER_END
	NODE_ALLOC = 8, // LAYER_ALLOC
	NODE_DEMO = 9, // LAYER_DEMO
	NODE_DEMO_CHOKE = 10, // LAYER_DEMO_CHOKE
	NODE_PROMO = 11, // LAYER_PROMO
	NODE_PROMO_CHOKE = 12, // LAYER_PROMO_CHOKE
	NUM_NODE_TYPES
};

enum edge_type {
	EDGE_CAP_CHOKE = 0,
	EDGE_SINK = 1,
	EDGE_RETENTION = 2,
	EDGE_CAPACITY = 3,
	EDGE_DEMO_CHOKE = 4,
	EDGE_RECLAIMED = 5,
	EDGE_PROMO_CHOKE = 6,
	EDGE_TIER_CHOKE = 7,
	EDGE_ALLOC = 8,
	EDGE_ALLOC_REWARD = 9,
	EDGE_PROMO = 10,
	EDGE_DEMO = 11,
	EDGE_END = 12,
	EDGE_INTERVAL_RETENTION = 13,
	EDGE_RETENTION_REWARD = 14,
	NUM_EDGE_TYPES
};

struct edge {
	int start;
	int next; // target
	int cap;
	int cost;
	int flow;
	enum edge_type type;
	int associated_tier; // tier this edge is associated with, used for removing and debugging
	
    int get_dest(int from) {  // Return the destination node if we were to traverse the arc from node `from`
        if(from == start) {
            return next;
        } else {
            return start;
        }
    }

    void add_flow(int from, int to_add) {  // Adds flow from originating vertex `from`
        if(from == start) {
            flow += to_add;
        } else {
            flow -= to_add;
        }
    }

    int get_capacity(int from) {  // Gets the capacity of the edge if the originating vertex is `from`
        if(from == start) {
            return cap - flow;
        } else {
            return flow;
        }
    }

    int get_cost_from(int from) {
        if(from == start) {
            return cost;
        } else {
            return -cost;
        }
    }
};

struct node {
	int _index; // used in "rgraph"
	struct trace_req req;
	enum node_type ntype;
	vector<struct edge> *adj; // used in "nodes"
	vector<edge*> connected_arcs_; // used in "rgraph"
};

struct SuccessiveShortestPathFlowNetwork {

    vector<struct node> nodes_;
    deque<struct edge> arcs_;

	string str;

	void print_rnode(int idx) {
		struct node cur = nodes_[idx];
		printf("Node[%d]: addr: %lu, type: %d, tier: %d, ntype: %d, %ld edges\n", idx, cur.req.addr, cur.req.type, cur.req.tier, cur.ntype, cur.connected_arcs_.size());
	}

    void add_node(struct trace_req t, enum node_type ntype) {
        nodes_.push_back({(int)nodes_.size(), t, ntype, NULL, {}});
    }

    int add_arc(int start, int end, int cap, int cost, int flow, enum edge_type type, int associated_tier) {
        arcs_.push_back({start, end, cap, cost, flow, type, associated_tier});
        nodes_[start].connected_arcs_.push_back(&arcs_.back());
        nodes_[end].connected_arcs_.push_back(&arcs_.back());
        return (int)arcs_.size() - 1;
    }

    // Successive shortest paths min-cost max-flow algorithm
    // If there is an negative cost cycle initially, then it goes into infinite loop
    vector<int> min_cost_max_flow(int source_i, int sink_i) {

		auto now = std::chrono::system_clock::now();
		time_t now_time = std::chrono::system_clock::to_time_t(now);

		cout << "\nPotential start time: " << std::ctime(&now_time);


        // First calculate the potentials with Bellman–Ford derivative
        // It starts from a single vertex and is optimized to only operate on active vertices on each layer
        // Thus, it works more like a BFS derivative
        vector<int> potentials(nodes_.size(), numeric_limits<int>::max());
        {

            deque<pair<int, int> > front;
            front.push_back({0, source_i});

            while(front.size() > 0) {
                int potential;
                int cur_i;
                std::tie(potential, cur_i) = front.front();
                front.pop_front();

                if(potential >= potentials[cur_i]) {
                    continue;
                }
                potentials[cur_i] = potential;
				//printf("cur_i: %d, poten: %d\n" ,cur_i, potential);

                for(edge* arc : nodes_[cur_i].connected_arcs_) if(arc->get_capacity(cur_i) > 0) {
                    // Traverse the arc if there is some remaining capacity
                    front.push_back({potential + arc->get_cost_from(cur_i), arc->get_dest(cur_i)});
                }
            }
        }

		auto cur = chrono::system_clock::now();
		time_t cur_time = chrono::system_clock::to_time_t(cur);

		cout << "Potential end time: " << std::ctime(&cur_time);

        // Next loop Dijkstra to saturate flow. Once we subtract the difference in potential, every arc will have a
        // non-negative cost in both directions, so using Dijkstra is safe

		priority_queue<tuple<int, int, edge*> > frontier;
        vector<bool> explr(nodes_.size(), false);
        vector<int> cost_to_node(nodes_.size(), -1);
        vector<edge*> arc_used(nodes_.size(), NULL);
		int path_cost;
		int cur_i;
		int next_i;

		int total_flow = 0;
		int cur_flow = 0;
		int cur_cost = 0;
        int result = 0;

        while(1) {
			fill(explr.begin(), explr.end(), false);
			fill(cost_to_node.begin(), cost_to_node.end(), -1);
			fill(arc_used.begin(), arc_used.end(), nullptr);

            frontier.push({0, source_i, NULL});

            while(frontier.size() > 0) {
                edge* cur_arc_used;
                tie(path_cost, cur_i, cur_arc_used) = frontier.top();
                path_cost = -path_cost;
                frontier.pop();

                if(!explr[cur_i]) {
                    explr[cur_i] = true;
                    arc_used[cur_i] = cur_arc_used;
                    cost_to_node[cur_i] = path_cost;

                    for(edge* arc : nodes_[cur_i].connected_arcs_) if(arc->get_capacity(cur_i) > 0) {
                        next_i = arc->get_dest(cur_i);
						//printf("cur i: %d, next i: %d\n", cur_i, next_i);
                        // As priority_queue is a max-heap, we use the negative of the path cost for convenience
                        // We subtract the difference of potentials from the arc cost to ensure all arcs have positive
                        // cost
                        frontier.push({
                            -path_cost - (arc->get_cost_from(cur_i) - potentials[next_i] + potentials[cur_i]),
                            next_i,
                            arc
                        });
                    }
                }
            }

            if(arc_used[sink_i] == NULL) {
				cur = chrono::system_clock::now();
				cur_time = chrono::system_clock::to_time_t(cur);
				cout << "MCMF end time: " << std::ctime(&cur_time);
                return {result, total_flow};  // We didn't find a path, so return
            }
            vector<edge*> arcs;
            int flow_pushed = numeric_limits<int>::max();
			int cur_cost = 0;
            {
                // Here we counstruct the path of arcs from source to sink
                int cur_i = sink_i;
                while(cur_i != source_i) {
                    edge* arc = arc_used[cur_i];
                    cur_i = arc->get_dest(cur_i);
                    flow_pushed = min(flow_pushed, arc->get_capacity(cur_i));
					//printf("flow_pushed: %d\n", flow_pushed);
                    arcs.push_back(arc);
                }

                // Next we push flow back across all the arcs
                for(auto arc_it = arcs.rbegin(); arc_it != arcs.rend(); arc_it++) {
                    edge* arc = *arc_it;
                    arc->add_flow(cur_i, flow_pushed);
                    result += arc->get_cost_from(cur_i) * flow_pushed;
                    cur_i = arc->get_dest(cur_i);
					cur_cost += arc->get_cost_from(cur_i) * flow_pushed;
					/*
					if (arc->cur_layer != 1 && arc->cur_layer - arc->next_layer == 1)
						printf("funcking1\n");
					if (arc->cur_layer != 2 && arc->cur_layer - arc->next_layer == 2)
						printf("funcking2\n");

					if (arc->cur_layer > arc->next_layer && arc->next_layer != 0) 
						printf("funcking3\n");
						*/



					//printf("flow_pushed: %d, acc cost: %d, cur i: %d, cur cost: %d result: %d\n", flow_pushed, cur_cost, cur_i, arc->get_cost_from(cur_i), result);
					//print_rnode(cur_i);
                }
				total_flow += flow_pushed;
				//printf("\rcur flow: %d, cur cost: %d, total flow: %d, total cost: %d", flow_pushed, cur_cost, total_flow, result);
            }



            // Finally, update the potentials so all edge-traversal costs remain non-negative
            for(int i=0; i<(int)nodes_.size(); i++) if(cost_to_node[i] != -1) {
                potentials[i] += cost_to_node[i];
            }
        }
    }
};

struct migopt_trace_req {
	struct trace_req t;
	bool is_g_first;
	bool is_g_last;
	bool is_p_first;
	bool is_p_last;
	int prev_idx; // previous access index, used for checking if the access is a reuse
	int next_idx; // next trace index
};

struct migopt {
	int nr_traces;
	int nr_tiers;
	int nr_nodes;
	int mode;
	int mig_period;
	int mig_traffic;
	int tier_cap[MAX_NR_TIERS];
	int tier_lat_loads[MAX_NR_TIERS];
	int tier_lat_stores[MAX_NR_TIERS];
	int tier_lat_4KB_reads[MAX_NR_TIERS];
	int tier_lat_4KB_writes[MAX_NR_TIERS];
	char *sched_file;

	int source_nidx;
	int cap_choke_nidx;
	int sink_nidx;
	int bottom_tier;

	int nr_loads[MAX_NR_TIERS];
	int nr_stores[MAX_NR_TIERS];
	int nr_accesses[MAX_NR_TIERS];

	// these two are used for the graph generation
	struct node *nodes;
	struct SuccessiveShortestPathFlowNetwork rgraph;

	// layer, type (-1: unknown, 0: ALLOC, 1: PROMO, 2: HIT, 3: DEMO), local accesses, initial alloc time, local alloc time, reuse distance, inter ref
	unordered_map<uint64_t,vector<int>> item_sched;
};

void init_migopt(struct sim_cfg &scfg);
void destroy_migopt(void);
void migopt_add_trace(struct trace_req &t);
string do_migopt(void);

void build_migopt_graph();
void fill_item_sched(int iter);
void print_migopt();


#endif
