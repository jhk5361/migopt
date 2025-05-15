#ifndef __CHOPT_H__
#define __CHOPT_H__

#include <vector>
#include <unordered_map>
#include <queue>
#include <stdint.h>
#include <limits.h>
#include "cxl_tm.h"
using namespace std;

#define MCMF_DEF 0
#define MCMF_FAULT 1

#define E_EVICTION 0
#define E_CACHING 1
#define E_LOAD 2
#define E_STORE 3
#define E_SINK 4
#define E_REV 5

#define FAULT_OVERHEAD 2000

struct mcmf_stat {
	uint64_t nr_accesses;
	uint64_t nr_loads;
	uint64_t nr_stores;
	uint64_t nr_promo;
	uint64_t nr_demo;
}

struct edge {
	int next; // target
	int cap;
	int cost;
	int flow;
	edge *rev;
	int lat_load;
	int lat_store;
	int type;
	int cur_level;
	int next_level;
	
	void update_flow(int f) { flow += f; rev->flow -= f; }
	int remain_flow() { return (cap - flow); }
};

struct node {
	uint64_t addr;
	enum req_type type;
	vector<struct edge> *adj;
	//std::unordered_map<int, struct edge> *adj;
};

struct opt_mcmf {
	vector<int> prev; vector<struct edge*> path;  // NetWork Flow에 필요한 정보 저장
	vector<int64_t> cost; vector<bool> in_q; vector<int> cnt; // spfa에 필요한 정보 저장
	vector<int> potential;

	bool bellman_ford(vector<vector<struct edge *>> &graph) {
		int size = graph.size();
		potential[0] = 0;
		bool updated = true;
		for (int i = 0; i < size && updated; i++) {
			updated = false;
			for (int u = 0; u < size; u++) {
				if (potential[u] == LLONG_MAX) continue;
				for (auto& edge : graph[u]) {
					if (edge.capacity > 0 && potential[edge->next] > potential[u] + edge->cost) {
						potential[edge->next] = potential[u] + edge->cost;
						updated = true;
					}
				}
			}
		}

		return !updated;
	}

	bool spfa(vector<vector<struct edge*>>& graph, int s, int t)  // S -> T 증가 경로 중 최소 비용인 것을 찾음
	{
		queue<int> q;
		cost[s] = 0; q.push(s); in_q[s] = true;
		int curr;
		int next;

		while (!q.empty())
		{
			curr = q.front(); q.pop(); in_q[curr] = false;

			for (auto& edge : graph[curr])
			{
				next = edge->next;
				// 유량이 추가로 흐를 수 있고, 기존보다 비용이 작아야 한다. (여분 유량)
				if (edge->remain_flow() > 0 && cost[next] > cost[curr] + edge->cost)
				{
					cost[next] = cost[curr] + edge->cost;
					prev[next] = curr;
					path[next] = edge;

					if (!in_q[next]) { q.push(next); in_q[next] = true; }
				}
			}
		}
		return (prev[t] != -1);
	}

	pair<int64_t, int> mincost_maxflow(vector<vector<struct edge*>>& graph, int s, int t, int threshold)
	{
		int size = graph.size(), total_flow = 0;
		int64_t cost_sum = 0;
		int old_total_flow = 0;
		int one_cnt = 0;
		int64_t cur_cost = 0;

		prev.assign(size, -1), path.assign(size, nullptr);
		cost.assign(size, LLONG_MAX), in_q.assign(size, false), cnt.assign(size, 0);
		potential.assign(size, LLONG_MAX);

		while (1)
		{
			fill(prev.begin(), prev.end(), -1);
			fill(cost.begin(), cost.end(), LLONG_MAX);
			fill(in_q.begin(), in_q.end(), false);
			fill(cnt.begin(), cnt.end(), 0);

			if (!spfa(graph, s, t)) break;  // 더이상 S -> T 증가경로를 찾을 수 없다면 알고리즘 종료

			int flow = INT_MAX;  // 찾은 경로에서 최소 여분 유량을 구하고, 그만큼 흘려줌
					
			cur_cost = 0;
			for (int i = t; i != s; i = prev[i])
			{
				cur_cost += flow * path[i]->cost;
				cost_sum += (flow * path[i]->cost);
				path[i]->update_flow(flow);
			}
			total_flow += flow;
			if (flow == 1)
				one_cnt++;
				fprintf(stderr, "\rtotal flow: %d, total_cost: %lu, cur flow: %d, cur_cost: %ld, one flow cnt: %d ...", total_flow, cost_sum, flow, cur_cost, one_cnt);
			if (old_total_flow - total_flow > 1000) {
				old_total_flow = total_flow;
				fprintf(stderr, "\r %d flow...", total_flow);
			}
		}
		return { cost_sum, total_flow };  // 최소 비용, 최대 유량 반환
	}
};


	pair<int64_t, int> edmonds_karp(vector<vector<struct edge*>>& graph, int s, int t, int threshold)
	{
		int size = graph.size(), total_flow = 0;
		int64_t cost_sum = 0;
		int old_total_flow = 0;
		int one_cnt = 0;
		int64_t cur_cost = 0;

		prev.assign(size, -1), path.assign(size, nullptr);
		cost.assign(size, LLONG_MAX), in_q.assign(size, false), cnt.assign(size, 0);

		while (1)
		{
			fill(prev.begin(), prev.end(), -1);
			fill(cost.begin(), cost.end(), LLONG_MAX);
			fill(in_q.begin(), in_q.end(), false);
			fill(cnt.begin(), cnt.end(), 0);

			if (!spfa(graph, s, t)) break;  // 더이상 S -> T 증가경로를 찾을 수 없다면 알고리즘 종료

			int flow = INT_MAX;  // 찾은 경로에서 최소 여분 유량을 구하고, 그만큼 흘려줌
					
			cur_cost = 0;
			for (int i = t; i != s; i = prev[i])
			{
				cur_cost += flow * path[i]->cost;
				cost_sum += (flow * path[i]->cost);
				path[i]->update_flow(flow);
			}
			total_flow += flow;
			if (flow == 1)
				one_cnt++;
				fprintf(stderr, "\rtotal flow: %d, total_cost: %lu, cur flow: %d, cur_cost: %ld, one flow cnt: %d ...", total_flow, cost_sum, flow, cur_cost, one_cnt);
			if (old_total_flow - total_flow > 1000) {
				old_total_flow = total_flow;
				fprintf(stderr, "\r %d flow...", total_flow);
			}
		}
		return { cost_sum, total_flow };  // 최소 비용, 최대 유량 반환
	}
};

struct chopt {
	//struct edge **edges;
	struct node *nodes;
	vector<vector<struct edge *>> rgraph; // residual graph
	int64_t base_cost;
	int *prev;
	int source;
	int sink;
	int base_layer;
	//std::queue q;
	//std::unordered_map<int,visit> visit;
	unordered_map<uint64_t, int> prev_accesses;
	int nr_traces;
	int nr_tiers;
	int nr_nodes;
	int cur;
	int nr_cache_pages[MAX_NR_TIERS];
	int lat_loads[MAX_NR_TIERS];
	int lat_stores[MAX_NR_TIERS];
	struct mcmf_stat stats[MAX_NR_TIERS];
	struct mcmf_stat g_stat;
};

void init_chopt (int nr_traces, int nr_tiers, int *nr_cache_sizes, int *lat_loads, int *lat_stores);
void destroy_opt(void);
void generate_graph(uint64_t addr, enum req_type type);
void generate_rgraph(uint64_t addr, enum req_type type);
uint64_t do_optimal(void);
void print_chopt();

#endif
