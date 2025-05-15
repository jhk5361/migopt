#include <iostream>
#include <thread>
#include <random>
#include <limits>
#include <time.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <functional>
#include <list>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <iomanip>
#include <algorithm>
#include <unordered_map>
#include <libconfig.h>
#include "sim.h"
#include "mem.h"
#include "monitor.h"
#include "migrator.h"
#include "tier.h"
#include "belady.h"
#include "mcmf.h" // for testing
#include "analysis.h"
#include "mtm.h"
#include "an.h"
#include "at.h"
using namespace std;


enum req_type get_type(string &str) {
	string sub_str = str.substr(0,10);
	if (sub_str[0] == 'R') {
		return LOAD;
	} else if (sub_str[0] == 'W') {
		return STORE;
	}
	abort();
	return OTHERS;
}

static inline unsigned long long get_mem_addr(string str) {
	char *end;
	unsigned long long addr;
	string addr_str = str.substr(2, str.size());
	addr = strtoull(addr_str.c_str(), &end, 16);
	return addr;
}

static inline struct trace_req get_trace_req(string &str) {
	struct trace_req treq = {0};
	treq.addr = get_mem_addr(str)/PAGE_SIZE;
	treq.type = get_type(str);
	treq.tier = -1;
	return treq;
}

void print_mem_req (struct mem_req &mreq) {
	cout << "MEM TYPE: " << mreq.type << ", VADDR: " << mreq.vaddr << ", PADDR: " << mreq.paddr << endl;
}

void print_stat () {
	// Use setw to format the output into aligned columns for "TOTAL", "LOAD", and "STORE".
	cout << setw(20) << "TOTAL" << setw(10)<< "LOAD" << setw(10) << "STORE" << endl;
	auto total_traces = tstat.nr_trace[LOAD] + tstat.nr_trace[STORE];
	cout << setw(20) << total_traces << setw(10) << tstat.nr_trace[LOAD] << setw(10) << tstat.nr_trace[STORE] << endl;

	double lat_total = calc_total_latency();
	double lat_load = calc_load_latency();
	double lat_store = calc_store_latency();
	double lat_alloc = calc_alloc_latency();
	double lat_promo = calc_promo_latency();
	double lat_demo = calc_demo_latency();
	auto mig = get_migration_pages();

	for (int i = 0; i < mig.size(); i++) {
		for (int j = 0; j < mig[i].size(); j++) {
			printf("%ld ", mig[i][j]);
		}
		printf("\n");
	}

	printf("Total latency (ns): %.2f\n", lat_total);
	printf("Total load latency (ns): %.2f\n", lat_load);
	printf("Total store latency (ns): %.2f\n", lat_store);
	printf("Total alloc latency (ns): %.2f\n", lat_alloc);
	printf("Total promo latency (ns): %.2f\n", lat_promo);
	printf("Total demo latency (ns): %.2f\n", lat_demo);

	lat_total = lat_total / 1000 / 1000 / 1000;
	double throughput = (tstat.nr_trace[LOAD] + tstat.nr_trace[STORE]) / lat_total;
	printf("Throughput (instructions per second): %.2f\n", throughput);
	lat_total = (lat_load + lat_store + lat_alloc) / 1000 / 1000 / 1000;
	throughput = (tstat.nr_trace[LOAD] + tstat.nr_trace[STORE]) / lat_total;
	printf("Throughput w/o migration (instructions per second): %.2f\n", throughput);


}


void get_sim_conf(cost char *cfg_file, struct sim_cfg &scfg) {
	config_t cfg;
	config_setting_t *setting;
	int intval;
	const char *str;
	long long int int64val;

	config_init(&cfg);

	/* Read the file. If there is an error, report it and exit. */
	if(!config_read_file(&cfg, cfg_file))
	{
		fprintf(stderr, "%s:%d - %s\n", config_error_file(&cfg),
				config_error_line(&cfg), config_error_text(&cfg));
		config_destroy(&cfg);
	}

	if(config_lookup_string(&cfg, "trace_file", &str))
		memcpy(scfg.trace_file, str, strlen(str));

	if(config_lookup_string(&cfg, "sched_file", &str))
		memcpy(scfg.sched_file, str, strlen(str));

	if(config_lookup_int64(&cfg, "nr_pages", &int64val))
		scfg.nr_pages = int64val;

	if(config_lookup_int(&cfg, "nr_traces", &intval))
		scfg.nr_traces = intval;

	if(config_lookup_int(&cfg, "nr_tiers", &intval))
		scfg.nr_tiers = intval;

	if(config_lookup_int(&cfg, "bar_ratio", &intval))
		scfg.bar_ratio = intval;

	if(config_lookup_int(&cfg, "nr_access_bits", &intval))
		scfg.nr_access_bits = intval;

	if(config_lookup_int(&cfg, "mpol", &intval))
		scfg.mpol = intval;

	if(config_lookup_int(&cfg, "alloc_id", &intval))
		scfg.alloc_id = intval;

	if(config_lookup_int(&cfg, "mcmf_type", &intval))
		scfg.mcmf_type = intval;

	if(config_lookup_int(&cfg, "mcmf", &intval))
		scfg.mcmf = intval;

	if(config_lookup_int(&cfg, "trace_sample", &intval))
		scfg.trace_sample = intval;

	if(config_lookup_int(&cfg, "do_analysis", &intval))
		scfg.do_analysis = intval;

	if(config_lookup_int(&cfg, "do_mtm", &intval))
		scfg.do_mtm = intval;

	if(config_lookup_int(&cfg, "do_an", &intval))
		scfg.do_an = intval;

	if(config_lookup_int(&cfg, "do_at", &intval))
		scfg.do_at = intval;

	if(config_lookup_int(&cfg, "mcmf_period", &intval))
		scfg.mcmf_period = intval;

	if(config_lookup_int(&cfg, "mcmf_mig_traffic", &intval))
		scfg.mcmf_mig_traffic = intval;

	if(config_lookup_int(&cfg, "mon_pol", &intval))
		scfg.mon_pol = intval;

	if(config_lookup_int(&cfg, "mon_hot_threshold", &intval))
		scfg.mon_hot_threshold = intval;

	if(config_lookup_int(&cfg, "mon_rate", &intval))
		scfg.mon_rate = intval;

	if(config_lookup_int(&cfg, "mig_pol", &intval))
		scfg.mig_pol = intval;

	if(config_lookup_int(&cfg, "mig_rate", &intval))
		scfg.mig_rate = intval;

	if(config_lookup_int(&cfg, "mig_traffic_rate", &intval))
		scfg.mig_traffic_rate = intval;


	if(config_lookup_int(&cfg, "mig_overhead", &intval))
		scfg.mig_overhead = intval;

	setting = config_lookup(&cfg, "ratio");
	if (setting != NULL) {
		int count = config_setting_length(setting);
		if (scfg.nr_tiers != count) {
			printf("ratio's count is not equall to nr_tiers!\n");
			abort();
		}
		for (int i = 0; i < count; i++) {
			config_setting_t *ratio = config_setting_get_elem(setting, i); 
			scfg.ratio[i] =  config_setting_get_int(ratio);
		}
	}

	setting = config_lookup(&cfg, "lat_loads");
	if (setting != NULL) {
		int count = config_setting_length(setting);
		if (scfg.nr_tiers != count) {
			printf("lat_loads's count is not equall to nr_tiers!\n");
			abort();
		}
		for (int i = 0; i < count; i++) {
			config_setting_t *lat = config_setting_get_elem(setting, i); 
			scfg.lat_loads[i] = config_setting_get_int(lat);
		}
	}

	setting = config_lookup(&cfg, "lat_stores");
	if (setting != NULL) {
		int count = config_setting_length(setting);
		if (scfg.nr_tiers != count) {
			printf("lat_stores's count is not equall to nr_tiers!\n");
			abort();
		}
		for (int i = 0; i < count; i++) {
			config_setting_t *lat = config_setting_get_elem(setting, i); 
			scfg.lat_stores[i] = config_setting_get_int(lat);
		}
	}

	setting = config_lookup(&cfg, "lat_4KB_reads");
	if (setting != NULL) {
		int count = config_setting_length(setting);
		if (scfg.nr_tiers != count) {
			printf("lat_4KB_reads's count is not equall to nr_tiers!\n");
			abort();
		}
		for (int i = 0; i < count; i++) {
			config_setting_t *lat = config_setting_get_elem(setting, i); 
			scfg.lat_4KB_reads[i] = config_setting_get_int(lat) * scfg.mig_overhead / 10000;
		}
	}

	setting = config_lookup(&cfg, "lat_4KB_writes");
	if (setting != NULL) {
		int count = config_setting_length(setting);
		if (scfg.nr_tiers != count) {
			printf("lat_4KB_writes's count is not equall to nr_tiers!\n");
			abort();
		}
		for (int i = 0; i < count; i++) {
			config_setting_t *lat = config_setting_get_elem(setting, i); 
			scfg.lat_4KB_writes[i] = config_setting_get_int(lat) * scfg.mig_overhead / 10000;
		}
	}

	printf("-----------------------------\nconfiguration\n");
	printf("\rtrace_file: %s\n \
			\rnr_pages: %lu\n \
			\rnr_traces: %d\n \
			\rnr_tiers: %d\n \
			\rmig_period: %d\n \
			\rmig_traffic: %d\n \
			\rmig_overhead: %d\n \
			\rsample_rate: %d\n \
			\rdo_an: %d\n \
			\rdo_at: %d\n \
			\rdo_mtm: %d\n \
			\rmpol: %d\n",
			scfg.trace_file,
			scfg.nr_pages,
			scfg.nr_traces,
			scfg.nr_tiers,
			scfg.mcmf_period,
			scfg.mcmf_mig_traffic,
			scfg.mig_overhead,
			scfg.trace_sample,
			scfg.do_an,
			scfg.do_at,
			scfg.do_mtm,
			scfg.mpol);
	printf("ratio: ");
	for (int i = 0; i < scfg.nr_tiers; i++) {
			printf("%d ", scfg.ratio[i]);
	}
	printf("\n");

	printf("lat_4KB_reads, lat_4KB_writes: ");
	for (int i = 0; i < scfg.nr_tiers; i++) {
			printf("%d,%d ", scfg.lat_4KB_reads[i], scfg.lat_4KB_writes[i]);
	}
	printf("\n");



	printf("-----------------------------\n");
}

static bool get_hash(uint64_t addr, uint64_t sample) {
	hash<uint64_t> hash_fn;
	if (sample == TRACE_SAMPLE)
		return true;
	size_t hash_value = hash_fn(addr);

	if ((hash_value % TRACE_SAMPLE) < sample) {
		return true;
	}

	return false;
}

static int do_sample_trace(vector<struct trace_req> &traces, struct sim_cfg &scfg, struct sim_stat &sstat) {
	unordered_set<uint64_t> org_pages, sampled_pages;
	struct trace_req treq = {0};
	ifstream input_file(scfg.trace_file);
	uint64_t lines = 0;


	// Do sampling with trace sampling ratio
	// and get the number of original/sampled pages and the number of original/sampled traces
	while (getline(input_file, cur_str)) {
		if ((++lines % 100000) == 0){
			fprintf(stderr, "\r%lu processed...", lines);
		}

		treq = get_trace_req(cur_str);
		switch (treq.type) {
			case LOAD:
			case STORE:
				if (!get_hash(treq.addr, scfg.trace_sampling_ratio)) {
					org_pages.insert(treq.addr);
					sstat.nr_org_traces[treq.type]++;
					sstat.nr_org_traces[TOTAL]++;
				} else {
					sampled_pages.insert(treq.addr);
					sstat.nr_sampled_traces[treq.rtype]++;
					sstat.nr_sampled_traces[TOTAL]++;
					sampled_traces.push_back(treq);
				}
				break;
			default:
				break;
		}

		scfg.nr_org_pages = org_pages.size();
		scfg.nr_org_traces = sstat.nr_sampled_traces[TOTAL];
		scfg.nr_sampled_pages = sampled_pages.size();
		scfg.nr_sampled_traces = sstat.nr_sampled_traces[TOTAL];

		lines = 0;
		input_file.clear();
		input_file.seekg(0);
	}
}

// Initialize the simulators
void init_simulators(struct sim_cfg &scfg) {
	// Set the capacity of each tier
	// total_cap is scfg.nr_sampled_pages * scfg.tier_cap_scale / 100;
	int total_cap = scfg.nr_sampled_pages * scfg.tier_cap_scale / 100;
	for (int i = 0; i < scfg.nr_tiers; i++) {
		scfg.tier_cap[i] = total_cap * scfg.tier_cap_ratio[i] / 100;
	}

	// Initialize the simulators
	if (scfg.do_an)
		init_an(scfg);
	if (scfg.do_at)
		init_at(scfg);
	if (scfg.do_mtm)
		init_mtm(scfg);
}

void process_trace(struct trace_req &trace, struct sim_cfg &scfg) {
	switch (trace.type) {
		case LOAD:
		case STORE:
			if (scfg.do_an)
				an_add_trace(trace.addr, trace.type == LOAD);
			if (scfg.do_at)
				at_add_trace(trace.addr, trace.type == LOAD);
			if (scfg.do_mtm)
				mtm_add_trace(trace.addr, trace.type == LOAD);
			break;
		default:
			break;
	}
}

// Do simulation
void do_sim(vector<struct trace_req> &traces, struct sim_cfg &scfg, struct sim_stat &sstat) {
	// Process each trace request
	for (auto &trace : traces) {
		process_trace(trace, scfg);
	}

	if (scfg.do_an)
		do_an(scfg);
	if (scfg.do_at)
		do_at(scfg);
	if (scfg.do_mtm)
		do_mtm(scfg);
}

int main(int argc, char **argv) {
	string cur_str;
	struct sim_cfg scfg = {0};
	struct sim_stat sstat = {0};
	const char *cfg_file = "/home/koo/src/trace_generator/run_script/simulator/default.cfg";

	memset(&sstat, 0, sizeof(sstat)); // Ensure tstat.nr_trace is properly initialized

	if (argc > 1)
		cfg_file = argv[1];

	get_sim_conf(cfg_file, scfg);

	struct trace_req treq = {0};
	uint64_t cur_time = 1;

	vector<struct trace_req> sampled_traces;
	do_sample_trace(sampled_traces, scfg, sstat);

	init_sim(scfg);

	do_sim(sampled_traces, scfg, sstat);

	destroy_sim(scfg);

	for (int i = 0; i < scfg.nr_sampled_traces; i++) {

	}

	if (scfg.nr_pages > 0) {
		scfg.mcmf = scfg.nr_traces;
		if (scfg.mcmf_period == -1) scfg.mcmf_period = scfg.mig_rate * scfg.trace_sample / TRACE_SAMPLE;

		printf("\nnr_traces: %d, # of org pages: %ld\n", scfg.nr_traces, scfg.nr_pages);
	} else {
		if (scfg.trace_sample != 10000) {
			int nr_sampled = 0;
			uint64_t addr;
			unordered_set<uint64_t> sampled_addr;
			while (getline(input_file, cur_str)) {
				if ((++lines % 100000) == 0){
					fprintf(stderr, "\r%lu processed...", lines);
				}
				rtype = get_type(cur_str);
				switch (rtype) {
					case LOAD:
					case STORE:
						addr = get_mem_addr(cur_str);
						if (get_hash(addr/PAGE_SIZE, scfg.trace_sample)) {
							nr_sampled++;
							sampled_addr.insert(addr/PAGE_SIZE);
							printf("%s\n", cur_str.c_str());
						}
						cur_time++;
						break;
					case OTHERS:
						break;
					default:
						break;
				}
			}
			input_file.clear();
			input_file.seekg(0);
			cout << endl;

			//scfg.mcmf = min(scfg.mcmf, nr_sampled);
			scfg.mcmf = nr_sampled;
			if (scfg.mcmf_period == -1) scfg.mcmf_period = scfg.mig_rate * scfg.trace_sample / TRACE_SAMPLE;

			scfg.nr_pages = sampled_addr.size();
			scfg.mig_rate = scfg.mcmf_period;
			scfg.mon_rate = scfg.mig_rate / 10;
			printf("\nnr_traces: %lu, nr_sampled: %d, # of org pages: %ld, # of sampled addr: %ld, period: %d\n", cur_time - 1, nr_sampled, org_pages.size(), sampled_addr.size(), scfg.mcmf_period);
			cur_time = 1;
			lines = 0;
			fflush(stdout);
		}
	}

	//init_pa_inf(scfg.nr_pages * (100 + scfg.mig_traffic_rate) / 100, scfg.ratio, scfg.nr_tiers, scfg.mpol, scfg.promo_target, scfg.demo_target, scfg.nr_access_bits, scfg.nr_cache_pages, scfg.lat_loads, scfg.lat_stores, scfg.lat_4KB_reads, scfg.lat_4KB_writes, scfg.alloc_id);
	init_pa_inf(scfg.nr_pages, scfg.ratio, scfg.nr_tiers, scfg.mpol, scfg.promo_target, scfg.demo_target, scfg.nr_access_bits, scfg.nr_cache_pages, scfg.lat_loads, scfg.lat_stores, scfg.lat_4KB_reads, scfg.lat_4KB_writes, scfg.alloc_id);
	init_monitor(scfg.mon_pol, scfg.mon_rate, scfg.mon_hot_threshold);
	init_migrator(scfg.mig_pol, scfg.mig_rate, scfg.mig_traffic_rate);


	if (scfg.mcmf_type != -1 && scfg.mcmf)
		if (scfg.mcmf_type == MCMF_MIGOPT) {
			init_migopt (scfg.mcmf_type, scfg.mcmf, scfg.mcmf_period, scfg.mcmf_mig_traffic, scfg.nr_tiers, scfg.nr_cache_pages, scfg.lat_loads, scfg.lat_stores, scfg.lat_4KB_reads, scfg.lat_4KB_writes, scfg.sched_file); // for testing
		} else {
			init_chopt (scfg.mcmf_type, scfg.mcmf, scfg.nr_tiers, scfg.nr_cache_pages, scfg.lat_loads, scfg.lat_stores, scfg.lat_4KB_reads, scfg.lat_4KB_writes); // for testing
		}

	vector<vector<uint64_t>> mcmf_mig(scfg.nr_tiers, vector<uint64_t>(scfg.nr_tiers, 0));

	if (scfg.do_analysis > 0) {
		printf("nr_trace: %d\n", scfg.nr_traces);
		init_analysis(scfg.sched_file, scfg.nr_traces, scfg.mig_rate, scfg.mcmf_mig_traffic, scfg.nr_tiers, scfg.bar_ratio, scfg.nr_cache_pages, scfg.lat_loads, scfg.lat_stores, scfg.lat_4KB_reads, scfg.lat_4KB_writes);
	}

	if (scfg.do_mtm > 0) {
		init_mtm(scfg.nr_pages, scfg.nr_tiers, NULL, scfg.ratio, scfg.lat_loads, scfg.lat_stores, scfg.lat_4KB_reads, scfg.lat_4KB_writes, scfg.mcmf_period, scfg.mcmf_mig_traffic, scfg.do_mtm, scfg.sched_file);
	}

	if (scfg.do_an > 0) {
		init_an(scfg.nr_pages, scfg.nr_tiers, NULL, scfg.ratio, scfg.lat_loads, scfg.lat_stores, scfg.lat_4KB_reads, scfg.lat_4KB_writes, scfg.mcmf_period, scfg.mcmf_mig_traffic, scfg.do_an, scfg.sched_file);
	}

	if (scfg.do_at > 0) {
		init_at(scfg.nr_pages, scfg.nr_tiers, NULL, scfg.ratio, scfg.lat_loads, scfg.lat_stores, scfg.lat_4KB_reads, scfg.lat_4KB_writes, scfg.mcmf_period, scfg.mcmf_mig_traffic, scfg.do_at, scfg.sched_file);
	}
	
	while (getline(input_file, cur_str)) {
		if ((++lines % 100000) == 0){
			fprintf(stderr, "\r%lu processed...", lines);
		}

		//if ((lines % 10000000) == 0)
		//	print_stat();
		rtype = get_type(cur_str);
		switch (rtype) {
			case LOAD:
			case STORE:

				if (!get_hash(get_mem_addr(cur_str)/PAGE_SIZE, scfg.trace_sample)) {
					//printf("%s\n", cur_str.c_str());
					continue;
				}

				///*
				// for testing
				if (scfg.mcmf_type != -1 && scfg.mcmf) {
					if (scfg.mcmf_type == MCMF_MIGOPT) {
						migopt_generate_graph(mreq.vaddr, mreq.type);
					} else {
						chopt_generate_graph(mreq.vaddr, mreq.type);
					}
				}

				if (scfg.do_analysis > 0) {
					analysis_generate_graph(mreq.vaddr, mreq.type);
				}


				cur_time++;
				break;
			case OTHERS:
				break;
			default:
				break;
		}

		tstat.nr_trace[rtype]++;

		if (scfg.mcmf_type != -1 && cur_time > scfg.mcmf && scfg.trace_sample == 10000)
			break;
	}

	if (scfg.mcmf_type != -1 && scfg.mcmf) {
		if (scfg.mcmf_type == MCMF_MIGOPT) {
			migopt_do_optimal();
			//migopt_analysis_graph();
		} else {
			chopt_do_optimal();
		}
		//print_chopt();
		//print_chopt();
	}

	if (scfg.do_mtm > 0) {
		do_mtm();
	}

	if (scfg.do_an > 0) {
		do_an();
	}

	if (scfg.do_at > 0) {
		do_at();
	}

	if (scfg.do_analysis > 0) {
		analysis_do();
	}

	cout << endl;
	print_stat();
	
	printf("mcmf mig\n");
	for (int i = 0; i < mcmf_mig.size(); i++) {
		for (int j = 0; j < mcmf_mig[i].size(); j++) {
			printf("%ld ", mcmf_mig[i][j]);
		}
		printf("\n");
	}

	destroy_belady();
	destroy_pa_inf();
	return 0;
}