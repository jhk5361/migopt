#ifndef __BELADY_H__
#define __BELADY_H__

#include <stdint.h>
#include <unordered_map>
#include <map>
#include <queue>
#include "cxl_tm.h"

struct belady_stat {
	uint64_t nr_accesses;
};

struct belady_tier {
	//std::priority_queue<std::pair<uint64_t,uint64_t> fartest; // timestamp, addr pair
	std::multimap<uint64_t, uint64_t, std::greater<uint64_t>> fartest; // timestamp, addr pair
	struct belady_stat bstat;
	uint64_t nr_pages;
};

struct belady {
	std::unordered_map<uint64_t, std::queue<uint64_t>> ts_map;
	struct belady_tier tiers[MAX_NR_TIERS];
	int nr_tiers;
	struct belady_stat bstat;
	std::multimap<uint64_t, uint64_t, std::greater<uint64_t>> hottest; // access count, Page # addr map
	std::unordered_map<uint64_t, uint64_t> hot_ranking;
};


void init_belady (uint64_t nr_pages, int *ratio, int nr_tiers);
void insert_belady_addr (uint64_t addr, uint64_t time);
void insert_belady_addr_tier (int id, uint64_t time, uint64_t addr);
std::pair<uint64_t, uint64_t> proc_belady_addr (int src, int dest, uint64_t va, uint64_t time);
std::pair<uint64_t, uint64_t> pick_fartest_one (int id); // return timestamp, addr
void put_time_to_tiers (uint64_t time, uint64_t addr, int src, int dest);
int get_tier_size(int id);
void sort_offline_pages();
int get_belady_tier(uint64_t va);
void print_belady_cdf();
uint64_t print_belady();
uint64_t destroy_belady();

#endif
