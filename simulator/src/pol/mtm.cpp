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
#include <list>
#include <unordered_set>
#include <fstream>
#include <cmath>
#include "mtm.h"
using namespace std;

#define NR_REV_DEMO 5

static struct mtm *my_mtm;
static vector<struct trace_req> traces;

static vector<int> calc_nr_free () {
	vector<int> nr_free = vector<int>(my_mtm->nr_tiers, 0);

	for (int i = 0; i < my_mtm->nr_tiers; i++) {
		nr_free[i] = my_mtm->tiers[i].cap - my_mtm->tiers[i].size;
		if(nr_free[i] < 0)
			abort();
	}

	return nr_free;
}

void mtm_add_trace(struct mtm_trace &t) {
	traces.push_back(t);
}

void init_mtm(int cap, int _my_mtm->nr_tiers, int *aorder, int *cap_ratio, int *_lat_loads, int *_lat_stores, int *_lat_4KB_reads, int *_lat_4KB_writes, int _mig_period, int _mig_traffic, int mtm_mode, char *_alloc_file) {
}

void init_mtm(struct sim_cfg &scfg) {
	my_mtm = new struct mtm;
	memset(my_mtm, 0, sizeof(struct mtm));
	my_mtm->nr_tiers = scfg.nr_tiers;
	my_mtm->mig_period = scfg.mig_period;
	my_mtm->mig_traffic = scfg.mig_traffic == -1 ? 1000 : scfg.mig_traffic;
	my_mtm->mode = scfg.do_at;
	my_mtm->sched_file = scfg.sched_file;

	for(int i = 0; i < scfg.nr_tiers; i++) {
		my_mtm->tier_lat_loads[i] = scfg.tier_lat_loads[i];
		my_mtm->tier_lat_stores[i] = scfg.tier_lat_stores[i];
		my_mtm->tier_lat_4KB_reads[i] = scfg.tier_lat_4KB_reads[i];
		my_mtm->tier_lat_4KB_writes[i] = scfg.tier_lat_4KB_writes[i];
		my_mtm->tiers[i].cap = scfg.tier_cap[i];
		my_mtm->tiers[i].size = 0;
	}

	my_mtm->pt = map<uint64_t, struct mtm_page *>();
	my_mtm->hist = map<int, map<uint64_t, struct mtm_page *>>();
}

void destroy_mtm() {
	for (auto &page : my_mtm->pt) {
		delete(page.second);
	}
	my_mtm->pt.clear();
	my_mtm->hist.clear();

	delete my_mtm;
	my_mtm = NULL;

	traces.clear();
}

// Update the histogram for the page
// If the old frequency is not zero, remove the page from the old histogram bin
// If the new frequency bin does not exist, create it and insert the page
// If the new frequency bin exists, insert the page into the bin
static void update_hist(uint64_t addr, int old_freq, int new_freq, struct mtm_page *page) {
	if (old_freq != 0) {
		auto old_hist_bin = my_mtm->hist.find(old_freq);
		old_hist_bin->second.erase(addr);
	}

	auto new_hist_bin = my_mtm->hist.find(new_freq);
	if (new_hist_bin == my_mtm->hist.end()) {
		map<uint64_t, struct mtm_page *> new_bin;
		new_bin.insert({addr, page});
		my_mtm->hist.insert({new_freq, new_bin});
	} else {
		new_hist_bin->second.insert({addr, page});
	}
}

// Cool down the histogram by reducing the frequency of each page by 50%
static void cool_hist() {
	map<int, map<uint64_t, struct mtm_page *>> hist;

	int freq;

	for (auto it = my_mtm->hist.rbegin(); it != my_mtm->hist.rend(); ++it) {
		if (it->second.size() == 0) continue;

		freq = it->first;
		freq = freq * 50 / 100;

		auto new_hist_bin = hist.find(freq);
		if (new_hist_bin == hist.end()) {
			map<uint64_t, struct mtm_page *> new_bin;
			hist.insert({freq, new_bin});
		}

		for (auto &bin_item : it->second) {
			bin_item.second->freq = freq;
			hist[freq].insert(bin_item);
		}
	}

	my_mtm->hist.clear();
	my_mtm->hist = hist;
}

static struct mtm_page *alloc_page(uint64_t addr) {
	int cur_tier;
	for (int i = 0; i < my_mtm->nr_tiers; i++) {
		cur_tier = my_mtm->alloc_order[i];
		if (my_mtm->tiers[cur_tier].size < my_mtm->tiers[cur_tier].cap)
			break;
	}

	if (my_mtm->tiers[cur_tier].size >= my_mtm->tiers[cur_tier].cap) {
		printf("cannot alloc in %d\n", cur_tier);
		abort();
	}

	struct mtm_page *page = new struct mtm_page;
	page->addr = addr;
	page->freq = 0;
	page->tier = cur_tier;
	page->target = -1;
	my_mtm->tiers[cur_tier].size++;
	my_mtm->nr_alloc[cur_tier]++;

	return page;
}

static struct mtm_page *get_page(uint64_t addr, bool &is_new) {
	auto it = my_mtm->pt.find(addr);
	struct mtm_page *page;
	if (it == my_mtm->pt.end()) {
		page = alloc_page(addr);
		my_mtm->pt.insert({addr, page});
		is_new = true;
	} else {
		page = it->second;
		is_new = false;
	}

	return page;
}

static void mtm_proc_req(struct trace_req *t) {
	bool is_new;
	struct mtm_page *page = get_page(t->addr, is_new);

	int old_freq = page->freq;
	page->freq++;

	update_hist(my_mtm, t->va, old_freq, page->freq, page);

	if (t->type == LOAD) my_mtm->nr_loads[page->tier]++;
	else my_mtm->nr_stores[page->tier]++;
	my_mtm->nr_accesses[page->tier]++;

	t->tier = page->tier;
}

static int do_promo(list<struct mtm_page *> promo_list) {
	int src, dst;
	int nr_promo = 0;

	for (auto &page : promo_list) {
		if (nr_promo >= mig_traffic)
			return nr_promo;

		src = page->tier;
		dst = page->target;

		if (src == dst)
			abort();
		
		page->tier = dst; // mig
		page->target = -1;
		my_mtm->nr_mig[src][dst]++;
		my_mtm->tiers[src].size--; my_mtm->tiers[dst].size++;
		nr_promo++;
	}

	return nr_promo;
}

// Demo pages according to the order of demo_path
static int do_demo(vector<stack<struct mtm_page *>> &demo_cand) {
	vector<pair<int,int>> demo_path = {{0,1}, {1,2}, {2,3}};
	int src, dst;
	int nr_demo = 0;
	int margin = (int)ceil((float)my_mtm->tiers[0].cap * NR_REV_DEMO/100);
	struct mtm_page *page;

	for (int i = 0; i < demo_path.size(); i++) {
		src = demo_path[i].first;
		dst = demo_path[i].second;

		while (my_mtm->tiers[src].size > (my_mtm->tiers[src].cap - margin) 
				&& !demo_cand[src].empty()) {
			page = demo_cand[src].top();

			if (dst == 3 && my_mtm->tiers[3].cap <= my_mtm->tiers[3].size)
				abort();

			page->tier = dst; // mig
			my_mtm->nr_mig[src][dst]++;
			my_mtm->tiers[src].size--; my_mtm->tiers[dst].size++;
			demo_cand[src].pop();
			nr_demo++;
		}
	}

	for (int i = 0; i < my_mtm->nr_tiers; i++) {
		if (my_mtm->tiers[i].size > my_mtm->tiers[i].cap)
			abort();
	}

	return 0;
}


static list<struct mtm_page *> scan_hist_for_promo() {
	int promo_target = 0;
	int promo_nr_scan = 0;
	list<struct mtm_page *> promo_list;

	int nr_need_move_item = 0;

	for (auto it = my_mtm->hist.rbegin(); it != my_mtm->hist.rend(); ++it) {
		if (it->second.size() == 0) continue;


		for (auto &bin_item : it->second) {
			if (promo_nr_scan >= my_mtm->tiers[promo_target].cap) {
				promo_target++;
				if (promo_target >= my_mtm->nr_tiers)
					abort();
	
				promo_nr_scan = 0;
			}
			
			if (bin_item.second->tier > promo_target) {
				bin_item.second->target = promo_target;
				promo_list.push_back({bin_item.second});
			}

			promo_nr_scan++;
		}
	}

	return promo_list;
}


static vector<stack<struct mtm_page*>> scan_hist_for_demo() {
	vector<stack<struct mtm_page *>> demo_cand(my_mtm->nr_tiers, stack<struct mtm_page *>());

	for (auto it = my_mtm->hist.rbegin(); it != my_mtm->hist.rend(); ++it) {
		if (it->second.size() == 0) continue;
	
		for (auto &bin_item : it->second) {
			demo_cand[bin_item.second->tier].push(bin_item.second);
		}
	}

	return demo_cand;
}

static int do_mig(struct mtm *my_mtm, int *promo_prior, int *demo_prior) {
	auto promo_list = scan_hist_for_promo_default(my_mtm);
	if (do_promo_default(my_mtm, promo_prior, promo_list) == 0)
		return 0;

	auto demo_cand = scan_hist_for_demo(my_mtm);
	if (do_demo(my_mtm, demo_prior, demo_cand) < 0)
		return -1;

	return 0;
}


struct mtm_perf calc_perf(struct mtm *my_mtm) {
	int64_t lat_acc = 0, lat_mig = 0, lat_alc = 0;
	int nr_acc[4] = {0,};
	int sum;
	for (int i = 0; i < my_mtm->nr_tiers; i++) {
		lat_acc += my_mtm->nr_loads[i] * lat_loads[i] + my_mtm->nr_stores[i] * lat_stores[i];
		lat_alc += my_mtm->nr_alloc[i] * lat_4KB_writes[i];
		nr_acc[i] = my_mtm->nr_loads[i] + my_mtm->nr_stores[i];
		sum += nr_acc[i];
	}

	for (int i = 0; i < my_mtm->nr_tiers; i++) {
		for (int j = 0; j < my_mtm->nr_tiers; j++) {
			lat_mig += my_mtm->nr_mig[i][j] * (lat_4KB_reads[i] + lat_4KB_writes[j]);
		}
	}

	printf("trace size: %ld, sum: %d\n", trace.size(), sum);
	for (int i = 0; i < 4; i++) {
		printf("%d ", nr_acc[i]);
	}
	printf("\n");

	struct mtm_perf ret = {lat_acc, lat_mig, lat_alc};

	return ret;
}

void *__do_mtm (void *arg) {
	struct mtm *my_mtm = (struct mtm *)arg;
	int cnt = 0;

	loads = stores = nr_sum = 0;

	for (int i = 0; i < trace.size(); i++) {
		//mtm_proc_req(my_mtm, trace[i].first, trace[i].second);
		cnt = nr_sum;
		mtm_proc_req(my_mtm, &trace[i]);
		calc_nr_free(my_mtm);
		if (i != 0 && (i % mig_period) == 0) {
			if (do_mig(my_mtm, my_mtm->promo_prior, my_mtm->demo_prior) < 0) {
				my_mtm->perf = {0,0,0};
				return my_mtm;
			}
		}

		if (i != 0 && (i % mig_period * 2) == 0) {
			cool_hist(my_mtm);
		}

		if (nr_sum != cnt + 1)
			abort();
	}


	printf("loads: %d, stores: %d, sum: %d\n", loads, stores, nr_sum);

	my_mtm->perf = calc_perf(my_mtm);
	return my_mtm;

	//print_hist();
}

static void clear_mtm(struct mtm *my_mtm) {
	for (auto &page : my_mtm->pt) {
		free(page.second);
	}

	my_mtm->pt.clear();
	my_mtm->hist.clear();

	free(my_mtm);
}

void print_mtm_sched (struct mtm *my_mtm) {
	vector<unordered_set<uint64_t>> addr_by_tier = vector(my_mtm->nr_tiers, unordered_set<uint64_t>());

	int period = 0;

	string aorder = "";
	for (int i = 0; i < my_mtm->nr_tiers; i++)
		aorder += to_string(my_mtm->alloc_order[i]);

	string output_file = alloc_file;

	output_file = output_file + "_mtm_mode" + to_string(mode) + "_aorder" + aorder + ".txt";
	ofstream writeFile(output_file.c_str());
	
	for (int i = 0; i < trace.size(); i++) {
		period = i / mig_period;

		if ((i % mig_period) == 0) {
			for (int j = 0; j < my_mtm->nr_tiers; j++) {
				for (auto item: addr_by_tier[j]) {
					writeFile << "A " << (period - 1) * mig_period << " " << item << " " << j << " " << 0 << "\n";
				}
				addr_by_tier[j].clear();
			}
			period++;
		}

		if (addr_by_tier[trace[i].tier].count(trace[i].va) == 0)
			addr_by_tier[trace[i].tier].insert(trace[i].va);
	}

	for (int j = 0; j < my_mtm->nr_tiers; j++) {
		for (auto item: addr_by_tier[j]) {
			writeFile << "A " << period * mig_period << " " << item << " " << j << " " << 0 << "\n";
		}
		addr_by_tier[j].clear();
	}

	fflush(stdout);
	writeFile.close();
}


void print_mtm(struct mtm *my_mtm) {
	for (int i = 0; i < 6; i++) {
		cout << my_mtm->promo_prior[i];
	}
	cout << " ";
	for (int i = 0; i < 6; i++) {
		cout << my_mtm->demo_prior[i];
	}
	cout << " ";

	cout << my_mtm->perf.lat_acc << " " << my_mtm->perf.lat_mig <<  " " << my_mtm->perf.lat_alc << endl; 

	print_mtm_sched(my_mtm);

	return;
}

void print_mtm_default(struct mtm *my_mtm) {
	cout << "lat_acc lat_mig lat_alc" << endl;
	cout << my_mtm->perf.lat_acc << " " << my_mtm->perf.lat_mig <<  " " << my_mtm->perf.lat_alc << endl; 

	cout << "alloc stat\n";
	for (int i = 0; i < my_mtm->nr_tiers; i++) {
		cout << my_mtm->nr_alloc[i] << " " ;
	}
	cout << endl;

	cout << "access stat\n";
	for (int i = 0; i < my_mtm->nr_tiers; i++) {
		cout << my_mtm->nr_accesses[i] << " ";
	}
	cout << endl;

	cout << "mig traffic\n" << endl;
	for (int i = 0; i < my_mtm->nr_tiers; i++) {
		for (int j = 0; j < my_mtm->nr_tiers; j++) {
			cout << my_mtm->nr_mig[i][j] << " ";
		}
		cout << endl;
	}

	print_mtm_sched(my_mtm);

	return;
}

void do_mtm() {

    array<int, 6> promo = {0, 1, 2, 3, 4, 5};
    array<int, 6> demo = {0, 1, 2, 3, 4, 5};
	vector<vector<int>> aorder = {{0,2,1,3}, {1,0,2,3}, {2,0,1,3}, {0,1,2,3}};

	int promo_prior[6] = {0,};
	int demo_prior[6] = {0,};
	int i = 0;
	int nr_fail = 0, nr_succ = 0, nr_proc = 0;

    using namespace std::chrono;
    auto start_time = steady_clock::now();  // 시작 시간 기록
	
	int cur_id = 0;
	pthread_t threads[NR_MAX_TH];
	void *res;


	for (int alloc_id = 0; alloc_id < aorder.size(); alloc_id++) {
		cout << "alloc id: " << alloc_id << endl;

		struct mtm *my_mtm = (struct mtm *)malloc(sizeof(struct mtm));
		memset(my_mtm, 0, sizeof(struct mtm));

		for (int i = 0; i < my_mtm->nr_tiers; i++) {
			my_mtm->tiers[i].cap = tiers[i].cap;
			my_mtm->alloc_order[i] = aorder[alloc_id][i];
		}

		my_mtm->hist = map<int, map<uint64_t, struct mtm_page *>>();
		my_mtm->pt = map<uint64_t, struct mtm_page *>();

		 __do_mtm(my_mtm);

		print_mtm_default(my_mtm);
		clear_mtm(my_mtm);
	}

}
