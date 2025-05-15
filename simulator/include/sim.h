#ifndef __SIM_H__
#define __SIM_H__

#include <stdint.h>

#define PAGE_SIZE 4096
#define FRAME_SIZE 4096

#define TRACE_SAMPLE 10000LL

#define HOT 0 
#define WARM 1
#define COLD 2

#define MPOL_BIND 0
#define MPOL_PREFERRED 1
#define MPOL_INTERLEAVE 2
#define MPOL_BELADY 3
#define MPOL_STATIC_OFFLINE 4
#define MPOL_REV_PREFERRED 5
#define MPOL_BIND_LRU 6
#define MPOL_BIND_FIFO 7
#define MPOL_MCMF 8

#define MON_POL_NOP -1
#define MON_POL_SCAN 0
#define MON_POL_SAMPLE 1

#define MIG_POL_NOP -1
#define MIG_POL_WF 0
#define MIG_POL_RWF 1

#define MCMF_CHOPT 0
#define MCMF_MIGOPT 1

#define MAX_NR_TIERS 4

struct sim_cfg {
	char trace_file[200];
	char alloc_file[200];

	uint64_t nr_org_pages;
	uint64_t nr_org_traces;
	uint64_t nr_sampled_pages;
	uint64_t nr_sampled_traces;

	int nr_tiers;
	int tier_cap_scale;
	int tier_cap_ratio[MAX_NR_TIERS];
	int tier_cap[MAX_NR_TIERS];

	int trace_sampling_ratio;
	int do_analysis;
	int do_mtm;
	int do_an;
	int do_at;
	int mig_overhead;

	int bar_ratio;

	int mcmf_type;
	int mcmf;
	int mcmf_period;
	int mcmf_mig_traffic;

	int lat_loads[MAX_NR_TIERS];
	int lat_stores[MAX_NR_TIERS];
	int lat_4KB_reads[MAX_NR_TIERS];
	int lat_4KB_writes[MAX_NR_TIERS];
};


#define NR_REQ_TYPE 6
enum trace_type {
	TOTAL = 0,
	LOAD,
	STORE,
	ALLOC,
	FREE,
	OTHERS,
};

struct trace_req {
	uint64_t addr;
	enum trace_type type;
	int tier;
};

struct sim_stat {
	uint64_t nr_org_traces[NR_REQ_TYPE];
	uint64_t nr_sampled_traces[NR_REQ_TYPE];
	//uint64_t nr_simulated_traces[MAX_NR_TIERS][NR_REQ_TYPE];
	//uint64_t nr_mig_traffic[MAX_NR_TIERS][MAX_NR_TIERS];
};

#endif