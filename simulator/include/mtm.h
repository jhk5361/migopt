#ifndef __MTM_H__
#define __MTM_H__

#include <cstdint>
#include <map>
using namespace std;

#define MTM_MAX_TIER 4
#define NR_MAX_TH 36

struct mtm_trace {
	uint64_t va;
	bool is_load;
	bool is_alloc;
	int tier;
};

struct mtm_page {
	uint64_t addr;
	int freq;
	int tier;
	int target;
};

struct mtm_tier {
	int cap;
	int size;
};

struct mtm_perf {
	int64_t lat_acc;
	int64_t lat_mig;
	int64_t lat_alc;
};

struct mtm {
	struct mtm_tier tiers[MTM_MAX_TIER];
	int nr_alloc[MTM_MAX_TIER];
	int nr_loads[MTM_MAX_TIER];
	int nr_stores[MTM_MAX_TIER];
	int nr_accesses[MTM_MAX_TIER];
	int nr_mig[MTM_MAX_TIER][MTM_MAX_TIER];
	int promo_prior[6];
	int demo_prior[6];
	int alloc_order[MTM_MAX_TIER];
	struct mtm_perf perf;
	int mode;


	map<int, map<uint64_t, struct mtm_page *>> hist;
	map<uint64_t, struct mtm_page *> pt;

};

void mtm_add_trace(uint64_t va, bool is_load); 
void init_mtm(int cap, int _nr_tiers, int *aorder, int *cap_ratio, int *_lat_loads, int *_lat_stores, int *_lat_4KB_reads, int *_lat_4KB_writes, int _mig_period, int _mig_traffic, int mtm_mode, char *_alloc_file);
void do_mtm();

#endif
