#ifndef __AN_H__
#define __AN_H__

#include <cstdint>
#include <map>
#include <list>
#include "sim.h"
using namespace std;

#define INIT -1
#define ACTIVE 0
#define INACTIVE 1

#define M_BALANCE 1
#define M_TIER 2
#define M_NO_MIG 3

struct an_trace {
	uint64_t va;
	bool is_load;
	bool is_alloc;
	int tier;
};

struct an_page {
	uint64_t addr;
	int freq;
	int tier;
	int in_active;
	std::list<struct an_page *>::iterator g_iter;
	std::list<struct an_page *>::iterator t_iter;
};

struct an_tier {
	int cap;
	int size;
	std::list<struct an_page *> *lru_list;
};

struct an_perf {
	int64_t lat_acc;
	int64_t lat_mig;
	int64_t lat_alc;
};

struct an {
	struct an_tier tiers[MAX_NR_TIERS];
	int nr_alloc[MAX_NR_TIERS];
	int nr_loads[MAX_NR_TIERS];
	int nr_stores[MAX_NR_TIERS];
	int nr_accesses[MAX_NR_TIERS];
	int nr_mig[MAX_NR_TIERS][MAX_NR_TIERS];
	int alloc_order[MAX_NR_TIERS];
	struct an_perf perf;
	int nr_tiers;
	int mig_traffic;
	int mig_period;
	int nr_period;
	int mode;
	int tier_lat_loads[MAX_NR_TIERS];
	int tier_lat_stores[MAX_NR_TIERS];
	int tier_lat_4KB_reads[MAX_NR_TIERS];
	int tier_lat_4KB_writes[MAX_NR_TIERS];
	char *sched_file;


	map<uint64_t, struct an_page *> pt;
	std::list<struct an_page *> *lru_list;
};

void an_add_trace(struct trace_req &t);
//void init_an(int cap, int _nr_tiers, int *aorder, int *cap_ratio, int *_lat_loads, int *_lat_stores, int *_lat_4KB_reads, int *_lat_4KB_writes, int _mig_period, int _mig_traffic, int mode, char *alloc_file);
void init_an(struct sim_cfg &scfg);
void do_an();
void destroy_an();

#endif