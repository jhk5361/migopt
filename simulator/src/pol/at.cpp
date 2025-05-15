#include <map>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <stack>
#include <iostream>
#include <algorithm>
#include <array>
#include <chrono>
#include <pthread.h>
#include <unordered_set>
#include <fstream>
#include <cmath>
#include "at.h"
using namespace std;

#define NR_REV_DEMO 5

static at_tier *tiers;
static int at_mode;
static char *alloc_file;
//static int alloc_order[4] = {0,2,1,3};
//static int alloc_order[4] = {1,0,2,3};
//static int alloc_order[4] = {2,0,1,3};
//static int alloc_order[4] = {3,0,2,1};
static int nr_tiers;
static int *lat_loads;
static int *lat_stores;
static int *lat_4KB_reads;
static int *lat_4KB_writes;

static int mig_traffic;
static int mig_period;

static int *nr_alloc;
static int *nr_loads;
static int *nr_stores;
static int *nr_accesses;

static int nr_mig[AT_MAX_TIER][AT_MAX_TIER];

//static map<int, map<uint64_t, struct at_page *>> hist;
//static map<uint64_t, struct at_page *> pt;
//static vector<pair<uint64_t, bool>> trace;
static vector<struct at_trace> trace;

void at_add_trace(uint64_t va, bool is_load) {
	//trace.push_back({va/4096, is_load});
	trace.push_back({va/4096, is_load, false, -1});
}

void init_at(int cap, int _nr_tiers, int *aorder, int *cap_ratio, int *_lat_loads, int *_lat_stores, int *_lat_4KB_reads, int *_lat_4KB_writes, int _mig_period, int _mig_traffic, int mode, char *_alloc_file) {
	nr_tiers = _nr_tiers;
	mig_period = _mig_period;
	mig_traffic = _mig_traffic == -1 ? 1000 : _mig_traffic;
	at_mode = mode;
	alloc_file = _alloc_file;

	lat_loads = (int *)malloc(sizeof(int) * nr_tiers);
	lat_stores = (int *)malloc(sizeof(int) * nr_tiers);
	lat_4KB_reads = (int *)malloc(sizeof(int) * nr_tiers);
	lat_4KB_writes = (int *)malloc(sizeof(int) * nr_tiers);
	tiers = (struct at_tier *)malloc(sizeof(struct at_tier) * nr_tiers);



	int total_ratio = 0;
	int tsize;
	for (int i = 0;  i < nr_tiers; i++) {
		total_ratio += cap_ratio[i];
	}

	for (int i = 0; i < nr_tiers; i++) {
		tsize = cap * cap_ratio[i] / total_ratio;
		tiers[i].size = 0;
		tiers[i].cap = tsize * 11 / 10;

		lat_loads[i] = _lat_loads[i];
		lat_stores[i] = _lat_stores[i];
		lat_4KB_reads[i] = _lat_4KB_reads[i];
		lat_4KB_writes[i] = _lat_4KB_writes[i];
	}

	/*
	nr_alloc = (int *)malloc(nr_tiers * sizeof(int));
	nr_loads = (int *)malloc(nr_tiers * sizeof(int));
	nr_stores = (int *)malloc(nr_tiers * sizeof(int));
	nr_accesses = (int *)malloc(nr_tiers * sizeof(int));
	for (int i = 0; i < nr_tiers; i++) {
		nr_alloc[i] = nr_loads[i] = nr_stores[i] = nr_accesses[i] = 0;
	}
	*/
}

static void update_tier_meta(struct at *my_at, struct at_page *page, bool is_new) {
	if (!is_new) {
		my_at->tiers[page->tier].lru_list->erase(page->t_iter);
	}

	/*
	if (page->in_active == ACTIVE) {
		my_at->tiers[page->tier].active.erase(page->t_iter);
	} else (page->in_active == INACTIVE) {
		my_at->tiers[page->tier].inactive.erase(page->t_iter);
	}
	*/

	my_at->tiers[page->tier].lru_list->push_front({page});
	//my_at->tiers[page->tier].active.push_front({page});
	
	page->t_iter = my_at->tiers[page->tier].lru_list->begin();
	//page->t_iter = my_at->tiers[page->tier].active.begin();
}

static void update_global_meta(struct at *my_at, struct at_page *page, bool is_new) {
	if (!is_new) {
		my_at->lru_list->erase(page->g_iter);
		//my_at->tiers[page->tier].lru_list->erase(page->t_iter);
	}

	my_at->lru_list->push_front({page});

	page->g_iter = my_at->lru_list->begin();
}

/*
static void print_hist() {
	int total_pages = 0;
	printf("Number of hist bins: %lu\n", hist.size());
	for (auto &bin : hist) {
		printf("Hist freq: %d, size: %lu\n", bin.first, bin.second.size());
		total_pages += bin.second.size();
	}
	printf("Total pages: %d\n", total_pages);
}

*/

static struct at_page *alloc_page(struct at *my_at, uint64_t addr) {
	int cur_tier;
	for (int i = 0; i < nr_tiers; i++) {
		cur_tier = my_at->alloc_order[i];
		//cout << cur_tier << " " << my_at->tiers[i].size << " " << my_at->tiers[i].cap << endl;
		if (my_at->tiers[cur_tier].size < my_at->tiers[cur_tier].cap)
			break;
	}

	if (my_at->tiers[cur_tier].size >= my_at->tiers[cur_tier].cap) {
		printf("cannot alloc in %d\n", cur_tier);
		abort();
	}

	struct at_page *page = (struct at_page *)malloc(sizeof(struct at_page));
	page->addr = addr;
	page->freq = 0;
	page->tier = cur_tier;
	page->in_active = INIT;
	my_at->tiers[cur_tier].size++;

	my_at->nr_alloc[cur_tier]++;

	return page;
}

static struct at_page *get_page(struct at *my_at, uint64_t addr, bool &is_new) {
	auto it = my_at->pt.find(addr);
	struct at_page *page;
	if (it == my_at->pt.end()) {
		page = alloc_page(my_at,addr);
		my_at->pt.insert({addr, page});
		is_new = true;
	} else {
		page = it->second;
		is_new = false;
	}

	return page;
}

static void at_proc_req(struct at *my_at, struct at_trace *t) {
	bool is_new;
	struct at_page *page = get_page(my_at, t->va, is_new);

	page->freq++;

	update_global_meta(my_at, page, is_new);
	update_tier_meta(my_at, page, is_new);

	if (t->is_load) my_at->nr_loads[page->tier]++;
	else my_at->nr_stores[page->tier]++;
	my_at->nr_accesses[page->tier]++;

	if (is_new)
		t->is_alloc = true;
	t->tier = page->tier;
}

static int do_promo(struct at *my_at, list<struct at_page *> &promo_list) {
	int src, dst;
	int nr_promo = 0;
	struct at_page *page;

	for (auto cand = promo_list.begin(); cand != promo_list.end(); cand++) {
		page = *cand;
		src = page->tier;
		dst = 0;

		page->tier = dst;
		my_at->nr_mig[src][dst]++;
		my_at->tiers[src].lru_list->erase(page->t_iter);
		my_at->tiers[dst].lru_list->push_front({page});
		page->t_iter = my_at->tiers[dst].lru_list->begin();
		my_at->tiers[src].size--; my_at->tiers[dst].size++;
		nr_promo++;
	}

	return nr_promo;
}

static int do_demo(struct at *my_at, list<struct at_page *> &demo_list, bool rev=false) {
	int src, dst;
	int nr_demo = 0;
	struct at_page *page;

	for (auto cand = demo_list.begin(); cand != demo_list.end(); cand++) {
		page = *cand;
		src = page->tier;

		if (rev == true) {
			if (src == 0) dst = 2;
			else if (src == 1) dst = 3;
			else
				abort();
		} else {
			if (src != 0)
				abort();

			if (my_at->tiers[2].size < my_at->tiers[2].cap)
				dst = 2;
			else
				dst = 3;
		}

		page->tier = dst;
		my_at->nr_mig[src][dst]++;
		my_at->tiers[src].lru_list->erase(page->t_iter);
		my_at->tiers[dst].lru_list->push_back({page});
		page->t_iter = prev(my_at->tiers[dst].lru_list->end());
		my_at->tiers[src].size--; my_at->tiers[dst].size++;
		if (my_at->tiers[src].size < 0 || my_at->tiers[dst].size > my_at->tiers[dst].cap)
			abort();
		nr_demo++;
	}

	return nr_demo;
}

//static vector<vector<vector<struct at_page*>>> scan_meta_for_promo(struct an *my_at, vector<int> &nr_free, vector<int> &nr_cand) {
static list<struct at_page*> scan_meta_for_promo(struct at *my_at, vector<int> &nr_free, vector<int> &nr_cand) {
	vector<vector<vector<struct at_page *>>> promo_cand(nr_tiers, vector<vector<struct at_page*>>(nr_tiers, vector<struct at_page *>()));
	list<struct at_page *> promo_list;

	int nr_need_to_move = 0;

	struct at_page *page;

	for (auto cand = my_at->lru_list->begin(); cand != my_at->lru_list->end(); ++cand) {
		page = *cand;

		if (nr_need_to_move == mig_traffic || -nr_free[0] >= (int)ceil((float)tiers[0].cap * (NR_REV_DEMO)/100))
			break;

		if (page->tier == 0)
			continue;

		promo_list.push_front(page);
		//promo_cand[page->tier][0].push_back(page);
		nr_cand[page->tier]++;
		nr_free[0]--;
		nr_free[page->tier]++;

		nr_need_to_move++;

		if (nr_need_to_move >= my_at->tiers[0].cap)
			break;
	}
	
	//return promo_cand;
	return promo_list;
}

static list<struct at_page *> scan_meta_for_demo(struct at *my_at, int nr_demo) {
	list<struct at_page *> demo_list;

	struct at_page *page;

	if (!nr_demo)
		return demo_list;

	for (auto cand = my_at->tiers[0].lru_list->rbegin(); cand != my_at->tiers[0].lru_list->rend(); ++cand) {
		page = *cand;

		demo_list.push_front(page);

		if (--nr_demo == 0)
			break;
	}

	return demo_list;
}

static list<struct at_page *> scan_meta_for_rev_demo(struct at *my_at, int nr_rev_rate, vector<int> &nr_free) {
	list<struct at_page *> demo_list;

	int nr_demo[2];

	for (int i = 0; i < 2; i++) {
		nr_demo[i] = (int)ceil((float)my_at->tiers[i].cap * nr_rev_rate/100);
		if (nr_free[i] < nr_demo[i])
			nr_demo[i] = nr_demo[i] - nr_free[i];
		else
			nr_demo[i] = 0;
	}

	struct at_page *page;

	for (auto cand = my_at->tiers[0].lru_list->rbegin(); cand != my_at->tiers[0].lru_list->rend() && nr_free[2] > 0; ++cand) {
		page = *cand;

		demo_list.push_front(page);
		nr_free[2]--;

		if (--nr_demo[0] == 0)
			break;
	}

	for (auto cand = my_at->tiers[1].lru_list->rbegin(); cand != my_at->tiers[1].lru_list->rend() && nr_free[3] > 0; ++cand) {
		page = *cand;

		demo_list.push_front(page);
		nr_free[3]--;

		if (--nr_demo[1] == 0)
			break;
	}

	return demo_list;
}

static vector<int> calc_nr_free (struct at *my_at) {
	vector<int> nr_free = vector<int>(nr_tiers, 0);

	for (int i = 0; i < nr_tiers; i++) {
		nr_free[i] = my_at->tiers[i].cap - my_at->tiers[i].size;
		if(nr_free[i] < 0)
			abort();
	}

	return nr_free;
}

//static bool adjust_promo_cand_for_demo(struct an *my_at, vector<vector<vector<struct at_page*>>> &promo_cand, vector<int> &nr_promo_cand, vector<int> &nr_free, vector<int> &nr_demo) {
static int adjust_promo_cand_for_demo(struct at *my_at, list<struct at_page*> &promo_list, vector<int> &nr_free) {
	int nr_should_demo = -nr_free[0];

	if (nr_should_demo <= 0)
		// no need to demote
		return 0;

	struct at_page *page;
	auto list_it = promo_list.begin();

	// if nr_free has not enough space for a demotion, cancel the promoted page
	while(nr_should_demo > (nr_free[2] + nr_free[3]) && list_it != promo_list.end()) {
		page = *list_it;

		if (page->tier == 1) { // cancel pages
			list_it = promo_list.erase(list_it);
			nr_should_demo--;
			nr_free[0]++;
			nr_free[page->tier]--;
		} else {
			list_it++;
		}
	}

	if (nr_should_demo > (nr_free[2] + nr_free[3])) {
		printf("Tier-2,3 full!\n");
		abort();
	}

	return nr_should_demo;
}

static void refill_promo_demo_list(struct at *my_at, list<struct at_page *> &promo_list, list<struct at_page *> &demo_list) {
	if (promo_list.size() == mig_traffic)
		return;

	auto promo_begin = promo_list.begin();
	auto demo_begin = demo_list.begin();

	struct at_page *page;
	volatile bool start_select = false;
	

	for (auto cand = my_at->lru_list->begin(); cand != my_at->lru_list->end(); ++cand) {
		page = *cand;

		if (promo_list.size() >= mig_traffic || promo_list.size() >= my_at->tiers[0].cap ||
				promo_list.size() + my_at->tiers[0].size >= my_at->tiers[0].cap + (int)ceil((float)tiers[0].cap * (NR_REV_DEMO)/100))
			break;

		if (start_select && (page->tier == 2 || page->tier == 3)) {
			promo_list.push_front(page);
		}

		if (page == *promo_begin)
			start_select = true;
	}

	start_select = false;
	int nr_demo = my_at->tiers[0].size + promo_list.size() - my_at->tiers[0].cap;

	if (nr_demo <= 0)
		return;

	if (nr_demo < demo_list.size())
		abort();


	for (auto cand = my_at->tiers[0].lru_list->rbegin(); cand != my_at->tiers[0].lru_list->rend(); ++cand) {
		page = *cand;

		if (demo_list.size() == nr_demo)
			break;

		if (start_select) {
			demo_list.push_front(page);
		}

		if (page == *demo_begin)
			start_select = true;
	}

	return;
}

static int do_mig(struct at *my_at) {
	vector<int> nr_free = calc_nr_free(my_at);
	vector<int> nr_promo_cand = vector<int>(nr_tiers, 0);

	auto promo_list = scan_meta_for_promo(my_at, nr_free, nr_promo_cand);

	if (promo_list.empty())
		return 0;

	int nr_demo = adjust_promo_cand_for_demo(my_at, promo_list, nr_free);

	auto demo_list = scan_meta_for_demo(my_at, nr_demo);

	calc_nr_free(my_at);

	refill_promo_demo_list(my_at, promo_list, demo_list);

	if (do_promo(my_at, promo_list) == 0)
		return 0;

	//calc_nr_free(my_at);

	if (do_demo(my_at, demo_list) < 0)
		return -1;

	nr_free = calc_nr_free(my_at);

	demo_list = scan_meta_for_rev_demo(my_at, NR_REV_DEMO, nr_free);

	if (do_demo(my_at, demo_list, true) < 0)
		return -1;

	calc_nr_free(my_at);

	return 0;
}


struct at_perf calc_perf(struct at *my_at) {
	int64_t lat_acc = 0, lat_mig = 0, lat_alc = 0;
	int nr_acc[4] = {0,};
	int sum;
	for (int i = 0; i < nr_tiers; i++) {
		lat_acc += my_at->nr_loads[i] * lat_loads[i] + my_at->nr_stores[i] * lat_stores[i];
		lat_alc += my_at->nr_alloc[i] * lat_4KB_writes[i];
		nr_acc[i] = my_at->nr_loads[i] + my_at->nr_stores[i];
		sum += nr_acc[i];
	}

	for (int i = 0; i < nr_tiers; i++) {
		for (int j = 0; j < nr_tiers; j++) {
			lat_mig += my_at->nr_mig[i][j] * (lat_4KB_reads[i] + lat_4KB_writes[j]);
		}
	}

	printf("trace size: %ld, sum: %d\n", trace.size(), sum);
	for (int i = 0; i < 4; i++) {
		printf("%d ", nr_acc[i]);
	}
	printf("\n");

	struct at_perf ret = {lat_acc, lat_mig, lat_alc};

	return ret;
}

void *__do_at (void *arg) {
	struct at *my_at = (struct at *)arg;

	for (int i = 0; i < nr_tiers; i++) {
		my_at->tiers[i].cap = tiers[i].cap;
	}

	for (int i = 0; i < trace.size(); i++) {
		at_proc_req(my_at, &trace[i]);
		if (i != 0 && (i % mig_period) == 0) {
			if (do_mig(my_at) < 0) {
				my_at->perf = {0,0,0};
				return my_at;
			}
		}
	}
	my_at->perf = calc_perf(my_at);
	return my_at;

	//print_hist();
	//printf("trace size: %d, pt size: %d\n", trace.size(), pt.size());
}

static void clear_at(struct at *my_at) {
	for (auto &page : my_at->pt) {
		free(page.second);
	}

	for (int i = 0; i < nr_tiers; i++) {
		//my_at->tiers[i].active.clear();
		//my_at->tiers[i].inactive.clear();
		my_at->tiers[i].lru_list->clear();
		delete my_at->tiers[i].lru_list;
	}

	my_at->pt.clear();
	my_at->lru_list->clear();

	free(my_at);
}

void print_at_sched (struct at *my_at) {
	int nr_period = (trace.size() % mig_period == 0) ? trace.size() / mig_period : trace.size() / mig_period + 1;

	vector<map<uint64_t,int>> mig_sched = vector(nr_period, map<uint64_t,int>());

	int period = 0;

	string aorder = "";
	for (int i = 0; i < nr_tiers; i++)
		aorder += to_string(my_at->alloc_order[i]);

	string output_file = alloc_file;
	output_file = output_file + "_at_mode" + to_string(my_at->mode) + "_aorder" + aorder + ".txt";

	ofstream writeFile(output_file.c_str());
	
	for (int i = 0; i < trace.size(); i++) {
		period = i / mig_period;

		if (i > 0 && (i-1)/mig_period != period) {
			for (auto &item : mig_sched[period-1]) {
				mig_sched[period].insert(item);
			}
		}

		//if (mig_sched[period].count(trace[i].va) == 0)
		//	mig_sched[period].insert({trace[i].va, trace[i].tier});
		
		mig_sched[period][trace[i].va] = trace[i].tier;
	}

	for (int i = 0; i < nr_period; i++) {
		for (auto item: mig_sched[i]) {
				writeFile << "A " << i * mig_period << " " << item.first << " " << item.second << " " << 0 << "\n";
		}
	}

	fflush(stdout);
	writeFile.close();
}


void print_at(struct at *my_at) {
	cout << "lat_acc lat_mig lat_alc\n" << endl;
	cout << my_at->perf.lat_acc << " " << my_at->perf.lat_mig <<  " " << my_at->perf.lat_alc << endl; 

	cout << "alloc stat\n";
	for (int i = 0; i < nr_tiers; i++) {
		cout << my_at->nr_alloc[i] << " ";
	}
	cout << endl;

	cout << "access stat\n";
	for (int i = 0; i < nr_tiers; i++) {
		cout << my_at->nr_accesses[i] << " ";
	}
	cout << endl;

	int promo = 0, demo = 0;
	cout << "mig traffic\n" << endl;
	for (int i = 0; i < nr_tiers; i++) {
		for (int j = 0; j < nr_tiers; j++) {
			cout << my_at->nr_mig[i][j] << " ";
			if (i > j) promo += my_at->nr_mig[i][j];
			if (i < j) demo += my_at->nr_mig[i][j];
		}
		cout << endl;
	}

	cout << "promo demo " << promo << " " << demo << endl;

	print_at_sched(my_at);

	return;
}

void do_at() {
	vector<vector<int>> aorder = {{0,2,1,3}, {1,0,2,3}, {2,0,1,3}, {0,1,2,3}};

	for (int alloc_id = 0; alloc_id < aorder.size(); alloc_id++) {
		cout << "alloc id: " << alloc_id << endl;

		struct at *my_at = (struct at *)malloc(sizeof(struct at));
		memset(my_at, 0, sizeof(struct at));

		for (int i = 0; i < nr_tiers; i++) {
			my_at->tiers[i].cap = tiers[i].cap;
			my_at->tiers[i].lru_list = new std::list<struct at_page *>();
			my_at->alloc_order[i] = aorder[alloc_id][i];
		}

		my_at->mode = at_mode;
		my_at->pt = map<uint64_t, struct at_page *>();
		my_at->lru_list = new std::list<struct at_page *>();

		 __do_at(my_at);

		print_at(my_at);
		clear_at(my_at);
	}
}
