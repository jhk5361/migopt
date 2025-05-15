#ifndef __AT_H__
#define __AT_H__

#include <cstdint>
#include <map>
#include <list>
using namespace std;

#define AT_MAX_TIER 4
#define NR_MAX_TH 36

#define INIT -1
#define ACTIVE 0
#define INACTIVE 1

struct at_trace {
	uint64_t va;
	bool is_load;
	bool is_alloc;
	int tier;
};

struct at_page {
	uint64_t addr;
	int freq;
	int tier;
	int in_active;
	std::list<struct at_page *>::iterator g_iter;
	std::list<struct at_page *>::iterator t_iter;
};

struct at_tier {
	int cap;
	int size;
	//std::list<struct at_page *> active;
	//std::list<struct at_page *> inactive;
	std::list<struct at_page *> *lru_list;
};

struct at_perf {
	int64_t lat_acc;
	int64_t lat_mig;
	int64_t lat_alc;
};

struct at {
	struct at_tier tiers[AT_MAX_TIER];
	int nr_alloc[AT_MAX_TIER];
	int nr_loads[AT_MAX_TIER];
	int nr_stores[AT_MAX_TIER];
	int nr_accesses[AT_MAX_TIER];
	int nr_mig[AT_MAX_TIER][AT_MAX_TIER];
	int promo_prior[6];
	int demo_prior[6];
	int alloc_order[AT_MAX_TIER];
	struct at_perf perf;
	int mig_traffic;
	int mig_period;
	int mode;


	map<uint64_t, struct at_page *> pt;
	std::list<struct at_page *> *lru_list;
};

void at_add_trace(uint64_t va, bool is_load); 
void init_at(int cap, int _nr_tiers, int *aorder, int *cap_ratio, int *_lat_loads, int *_lat_stores, int *_lat_4KB_reads, int *_lat_4KB_writes, int _mig_period, int _mig_traffic, int mode, char *_alloc_file);
void do_at();

#endif
