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
#include "an.h"
using namespace std;

#define NR_REV_DEMO 5

static struct an *my_an;
static vector<struct trace_req> traces;

void an_add_trace(struct trace_req &t) {
	traces.push_back(t);
}

void init_an(struct sim_cfg &scfg) {
	my_an = new struct an;
	my_an->nr_tiers = scfg.nr_tiers;
	my_an->mig_period = scfg.mig_period;
	my_an->mig_traffic = scfg.mig_traffic == -1 ? 1000 : scfg.mig_traffic;
	my_an->mode = scfg.do_an;
	my_an->sched_file = scfg.sched_file;

	for(int i = 0; i < scfg.nr_tiers; i++) {
		my_an->lat_loads[i] = scfg.lat_loads[i];
		my_an->lat_stores[i] = scfg.lat_stores[i];
		my_an->lat_4KB_reads[i] = scfg.lat_4KB_reads[i];
		my_an->lat_4KB_writes[i] = scfg.lat_4KB_writes[i];
		my_an->tiers[i].cap = scfg.tier_cap[i];
		my_an->tiers[i].size = 0;
	}

	for (int i = 0; i < my_an->nr_tiers; i++) {
		my_an->tiers[i].lru_list = new std::list<struct an_page *>();
	}

	my_an->pt = map<uint64_t, struct an_page *>();
	my_an->lru_list = new std::list<struct an_page *>();
	my_an->nr_period = 1;
}

void destroy_an() {
	for (int i = 0; i < my_an->nr_tiers; i++) {
		my_an->tiers[i].lru_list->clear();
		delete my_an->tiers[i].lru_list;
	}
	my_an->pt.clear();
	my_an->lru_list->clear();
	delete my_an->lru_list;
	traces.clear();
	delete my_an;
}

static void update_tier_meta(struct an_page *page, bool is_new) {
	if (!is_new) {
		my_an->tiers[page->tier].lru_list->erase(page->t_iter);
	}

	my_an->tiers[page->tier].lru_list->push_front({page});
	
	page->t_iter = my_an->tiers[page->tier].lru_list->begin();
}

static void update_global_meta(struct an_page *page, bool is_new) {
	if (!is_new) {
		my_an->lru_list->erase(page->g_iter);
	}

	my_an->lru_list->push_front({page});

	page->g_iter = my_an->lru_list->begin();
}

static struct an_page *alloc_page(uint64_t addr) {
	int cur_tier;
	for (int i = 0; i < my_an->nr_tiers; i++) {
		cur_tier = my_an->alloc_order[i];
		if (my_an->tiers[cur_tier].size < my_an->tiers[cur_tier].cap)
			break;
	}

	if (my_an->tiers[cur_tier].size >= my_an->tiers[cur_tier].cap) {
		printf("cannot alloc in %d\n", cur_tier);
		abort();
	}

	struct an_page *page = new struct an_page;
	page->addr = addr;
	page->freq = 0;
	page->tier = cur_tier;
	page->in_active = INIT;
	my_an->tiers[cur_tier].size++;
	my_an->nr_alloc[cur_tier]++;

	return page;
}

static struct an_page *get_page(uint64_t addr, bool &is_new) {
	auto it = my_an->pt.find(addr);
	struct an_page *page;
	if (it == my_an->pt.end()) {
		page = alloc_page(addr);
		my_an->pt.insert({addr, page});
		is_new = true;
	} else {
		page = it->second;
		is_new = false;
	}

	return page;
}

static void an_proc_req(struct trace_req *t) {
	bool is_new;
	struct an_page *page = get_page(t->addr, is_new);

	page->freq++;

	update_global_meta(page, is_new);
	update_tier_meta(page, is_new);

	if (t->type == LOAD) my_an->nr_loads[page->tier]++;
	else my_an->nr_stores[page->tier]++;
	my_an->nr_accesses[page->tier]++;

	t->tier = page->tier;
}

static int do_promo(list<struct an_page *> &promo_list) {
	int src, dst;
	int nr_promo = 0;
	struct an_page *page;

	for (auto cand = promo_list.begin(); cand != promo_list.end(); cand++) {
		page = *cand;
		src = page->tier;
		dst = 0;

		page->tier = dst;
		my_an->nr_mig[src][dst]++;
		my_an->tiers[src].lru_list->erase(page->t_iter);
		my_an->tiers[dst].lru_list->push_front({page});
		page->t_iter = my_an->tiers[dst].lru_list->begin();
		my_an->tiers[src].size--; my_an->tiers[dst].size++;
		nr_promo++;
	}

	return nr_promo;
}

static int do_demo(list<struct an_page *> &demo_list) {
	int src, dst;
	int nr_demo = 0;
	struct an_page *page;

	for (auto cand = demo_list.begin(); cand != demo_list.end(); cand++) {
		page = *cand;
		src = page->tier;
		if (src == 0) dst = 2;
		else if (src == 1) dst = 3;
		else
			abort();

		page->tier = dst;
		my_an->nr_mig[src][dst]++;
		my_an->tiers[src].lru_list->erase(page->t_iter);
		my_an->tiers[dst].lru_list->push_back({page});
		page->t_iter = prev(my_an->tiers[dst].lru_list->end());
		my_an->tiers[src].size--; my_an->tiers[dst].size++;
		if (my_an->tiers[src].size < 0 || my_an->tiers[dst].size > my_an->tiers[dst].cap)
			abort();
		nr_demo++;
	}

	return nr_demo;
}

static list<struct an_page*> scan_meta_for_promo(vector<int> &nr_free, vector<int> &nr_cand) {
	vector<vector<vector<struct an_page *>>> promo_cand(my_an->nr_tiers, vector<vector<struct an_page*>>(my_an->nr_tiers, vector<struct an_page *>()));
	list<struct an_page *> promo_list;

	int nr_need_to_move = 0;

	struct an_page *page;

	for (auto cand = my_an->lru_list->begin(); cand != my_an->lru_list->end(); ++cand) {
		page = *cand;

		if (nr_need_to_move == my_an->mig_traffic || -nr_free[0] >= (int)ceil((float)my_an->tiers[0].cap * (NR_REV_DEMO)/100))
			break;

		if (page->tier == 0)
			continue;

		promo_list.push_front(page);
		//promo_cand[page->tier][0].push_back(page);
		nr_cand[page->tier]++;
		nr_free[0]--;
		nr_free[page->tier]++;

		nr_need_to_move++;

		if (nr_need_to_move >= my_an->tiers[0].cap)
			break;
	}
	
	//return promo_cand;
	return promo_list;
}

static list<struct an_page *> scan_meta_for_demo(int nr_demo) {
	list<struct an_page *> demo_list;

	struct an_page *page;

	if (!nr_demo)
		return demo_list;

	for (auto cand = my_an->tiers[0].lru_list->rbegin(); cand != my_an->tiers[0].lru_list->rend(); ++cand) {
		page = *cand;

		demo_list.push_front(page);

		if (--nr_demo == 0)
			break;
	}

	return demo_list;
}

static list<struct an_page *> scan_meta_for_rev_demo(int nr_rev_rate, vector<int> &nr_free) {
	list<struct an_page *> demo_list;

	int nr_demo[2];

	for (int i = 0; i < 2; i++) {
		nr_demo[i] = (int)ceil((float)my_an->tiers[i].cap * nr_rev_rate/100);
		if (nr_free[i] < nr_demo[i])
			nr_demo[i] = nr_demo[i] - nr_free[i];
		else
			nr_demo[i] = 0;
	}


	struct an_page *page;

	for (auto cand = my_an->tiers[0].lru_list->rbegin(); cand != my_an->tiers[0].lru_list->rend() && nr_free[2] > 0; ++cand) {
		page = *cand;

		demo_list.push_front(page);
		nr_free[2]--;

		if (--nr_demo[0] == 0)
			break;
	}

	for (auto cand = my_an->tiers[1].lru_list->rbegin(); cand != my_an->tiers[1].lru_list->rend() && nr_free[3] > 0; ++cand) {
		page = *cand;

		demo_list.push_front(page);
		nr_free[3]--;

		if (--nr_demo[1] == 0)
			break;
	}


	return demo_list;
}

static vector<int> calc_nr_free () {
	vector<int> nr_free = vector<int>(my_an->nr_tiers, 0);

	for (int i = 0; i < my_an->nr_tiers; i++) {
		nr_free[i] = my_an->tiers[i].cap - my_an->tiers[i].size;
		if(nr_free[i] < 0)
			abort();
	}

	return nr_free;
}

static int adjust_promo_cand_for_demo(list<struct an_page*> &promo_list, vector<int> &nr_free) {
	int nr_should_demo = -nr_free[0];

	if (nr_should_demo <= 0)
		// no need to demote
		return 0;

	struct an_page *page;
	auto list_it = promo_list.begin();

	// if nr_free has not enough space for a demotion, cancel the promoted page
	while(nr_should_demo > nr_free[2] && list_it != promo_list.end()) {
		page = *list_it;

		if (page->tier != 2) { // cancel pages
			list_it = promo_list.erase(list_it);
			nr_should_demo--;
			nr_free[0]++;
			nr_free[page->tier]--;
		} else {
			list_it++;
		}
	}

	if (nr_should_demo > nr_free[2]) {
		printf("Tier-2 full!\n");
		abort();
	}

	return nr_should_demo;
}

static void refill_promo_demo_list(list<struct an_page *> &promo_list, list<struct an_page *> &demo_list) {
	if (promo_list.size() == my_an->mig_traffic)
		return;

	auto promo_begin = promo_list.begin();
	auto demo_begin = demo_list.begin();

	struct an_page *page;
	volatile bool start_select = false;
	

	for (auto cand = my_an->lru_list->begin(); cand != my_an->lru_list->end(); ++cand) {
		page = *cand;

		if (promo_list.size() >= my_an->mig_traffic || promo_list.size() >= my_an->tiers[0].cap ||
				promo_list.size() + my_an->tiers[0].size >= my_an->tiers[0].cap + (int)ceil((float)my_an->tiers[0].cap * (NR_REV_DEMO)/100))
			break;

		if (start_select && page->tier == 2) {
			promo_list.push_front(page);
		}

		if (page == *promo_begin)
			start_select = true;
	}

	start_select = false;
	int nr_demo = my_an->tiers[0].size + promo_list.size() - my_an->tiers[0].cap;

	if (nr_demo <= 0)
		return;

	if (nr_demo < demo_list.size())
		abort();


	for (auto cand = my_an->tiers[0].lru_list->rbegin(); cand != my_an->tiers[0].lru_list->rend(); ++cand) {
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

static int do_mig() {
	vector<int> nr_free = calc_nr_free();
	vector<int> nr_promo_cand = vector<int>(my_an->nr_tiers, 0);

	if (my_an->mode == M_NO_MIG)
		return 0;

	if (my_an->mode == M_BALANCE && nr_free[0] <= 0)
		return 0;

	// nr_free stores the results under the assumption where the promotion occurs successfully
	auto promo_list = scan_meta_for_promo(nr_free, nr_promo_cand);

	if (promo_list.empty())
		return 0;

	int nr_demo = adjust_promo_cand_for_demo(promo_list, nr_free);

	auto demo_list = scan_meta_for_demo(nr_demo);

	refill_promo_demo_list(promo_list, demo_list);

	if (do_promo(promo_list) == 0)
		return 0;


	//calc_nr_free();

	if (do_demo(demo_list) < 0)
		return -1;

	nr_free = calc_nr_free();

	demo_list = scan_meta_for_rev_demo(NR_REV_DEMO, nr_free);

	if (do_demo(demo_list) < 0)
		return -1;

	return 0;
}


struct an_perf calc_perf() {
	int64_t lat_acc = 0, lat_mig = 0, lat_alc = 0;
	for (int i = 0; i < my_an->nr_tiers; i++) {
		lat_acc += my_an->nr_loads[i] * my_an->lat_loads[i] + my_an->nr_stores[i] * my_an->lat_stores[i];
		lat_alc += my_an->nr_alloc[i] * my_an->lat_4KB_writes[i];
	}

	for (int i = 0; i < my_an->nr_tiers; i++) {
		for (int j = 0; j < my_an->nr_tiers; j++) {
			lat_mig += my_an->nr_mig[i][j] * (my_an->lat_4KB_reads[i] + my_an->lat_4KB_writes[j]);
		}
	}

	struct an_perf ret = {lat_acc, lat_mig, lat_alc};

	return ret;
}

void *__do_an (vector<int> &alloc_order) {
	for (int i = 0; i < my_an->nr_tiers; i++) {
		my_an->alloc_order[i] = alloc_order[i];
	}

	for (int i = 0; i < traces.size(); i++) {
		an_proc_req(&traces[i]);
		if (i != 0 && (i % my_an->mig_period) == 0) {
			my_an->nr_period++;
			if (do_mig() < 0) {
				my_an->perf = {0,0,0};
				return my_an;
			}
		}
	}

	my_an->perf = calc_perf();

	return my_an;
}

static void clear_an() {
	for (auto &page : my_an->pt) {
		delete(page.second);
	}

	for (int i = 0; i < my_an->nr_tiers; i++) {
		my_an->tiers[i].lru_list->clear();
	}

	my_an->pt.clear();
	my_an->lru_list->clear();
}

void print_an_sched () {
	vector<unordered_set<uint64_t>> addr_by_tier = vector(my_an->nr_tiers, unordered_set<uint64_t>());

	int period = 0;

	string aorder = "";
	for (int i = 0; i < my_an->nr_tiers; i++)
		aorder += to_string(my_an->alloc_order[i]);

	string output_file = my_an->sched_file;
	output_file = output_file + "_an_mode" + to_string(my_an->mode) + "_aorder" + aorder + ".txt";

	ofstream writeFile(output_file.c_str());

	
	for (int i = 0; i < traces.size(); i++) {
		period = i / my_an->mig_period;

		if ((i % my_an->mig_period) == 0) {
			for (int j = 0; j < my_an->nr_tiers; j++) {
				for (auto item: addr_by_tier[j]) {
					writeFile << "A " << (period - 1) * my_an->mig_period << " " << item << " " << j << " " << 0 << "\n";
				}
				addr_by_tier[j].clear();
			}
			period++;
		}

		if (addr_by_tier[traces[i].tier].count(traces[i].addr) == 0)
			addr_by_tier[traces[i].tier].insert(traces[i].addr);
	}

	for (int j = 0; j < my_an->nr_tiers; j++) {
		for (auto item: addr_by_tier[j]) {
			writeFile << "A " << period * my_an->mig_period << " " << item << " " << j << " " << 0 << "\n";
		}
		addr_by_tier[j].clear();
	}

	fflush(stdout);
	writeFile.close();
}

void print_an() {
	cout << "lat_acc lat_mig lat_alc" << endl;
	cout << my_an->perf.lat_acc << " " << my_an->perf.lat_mig <<  " " << my_an->perf.lat_alc << endl; 

	cout << "alloc stat\n";
	for (int i = 0; i < my_an->nr_tiers; i++) {
		cout << " " << my_an->nr_alloc[i];
	}

	cout << "access stat\n";
	for (int i = 0; i < my_an->nr_tiers; i++) {
		cout << " " << my_an->nr_accesses[i];
	}
	cout << "mig traffic\n" << endl;
	for (int i = 0; i < my_an->nr_tiers; i++) {
		for (int j = 0; j < my_an->nr_tiers; j++) {
			cout << my_an->nr_mig[i][j] << " ";
		}
		cout << endl;
	}

	print_an_sched();

	return;
}

void do_an() {
	vector<vector<int>> alloc_orders = {{0,2,1,3}, {1,0,2,3}, {2,0,1,3}, {0,1,2,3}};

	for (int alloc_id = 0; alloc_id < alloc_orders.size(); alloc_id++) {
		cout << "alloc id: " << alloc_id << endl;

		 __do_an(alloc_orders[alloc_id]);

		print_an();
		clear_an();
	}
}