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

#define M_DEFAULT 1
#define M_ALL 2
#define NR_REV_DEMO 5

static mtm_tier *tiers;
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
static int mode;
char *alloc_file;


static int loads = 0, stores = 0, nr_sum = 0;

static vector<struct mtm_trace> trace;

static vector<int> calc_nr_free (struct mtm *my_mtm) {
	vector<int> nr_free = vector<int>(nr_tiers, 0);

	for (int i = 0; i < nr_tiers; i++) {
		nr_free[i] = my_mtm->tiers[i].cap - my_mtm->tiers[i].size;
		if(nr_free[i] < 0)
			abort();
	}

	return nr_free;
}


void mtm_add_trace(uint64_t va, bool is_load) {
	//trace.push_back({va/4096, is_load});
	trace.push_back({va/4096, is_load, false, -1});
}

void init_mtm(int cap, int _nr_tiers, int *aorder, int *cap_ratio, int *_lat_loads, int *_lat_stores, int *_lat_4KB_reads, int *_lat_4KB_writes, int _mig_period, int _mig_traffic, int mtm_mode, char *_alloc_file) {
	nr_tiers = _nr_tiers;

	lat_loads = (int *)malloc(sizeof(int) * nr_tiers);
	lat_stores = (int *)malloc(sizeof(int) * nr_tiers);
	lat_4KB_reads = (int *)malloc(sizeof(int) * nr_tiers);
	lat_4KB_writes = (int *)malloc(sizeof(int) * nr_tiers);
	tiers = (struct mtm_tier *)malloc(sizeof(struct mtm_tier) * nr_tiers);

	mig_period = _mig_period;
	mig_traffic = _mig_traffic;
	mode = mtm_mode;
	alloc_file = _alloc_file;

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
}

static void update_hist(struct mtm *my_mtm, uint64_t addr, int old_freq, int new_freq, struct mtm_page *page) {
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

static void cool_hist(struct mtm *my_mtm) {
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

	my_mtm->hist = hist;
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

static struct mtm_page *alloc_page(struct mtm *my_mtm, uint64_t addr) {
	int cur_tier;
	for (int i = 0; i < nr_tiers; i++) {
		cur_tier = my_mtm->alloc_order[i];
		//cout << cur_tier << " " << my_mtm->tiers[i].size << " " << my_mtm->tiers[i].cap << endl;
		if (my_mtm->tiers[cur_tier].size < my_mtm->tiers[cur_tier].cap)
			break;
	}

	if (my_mtm->tiers[cur_tier].size >= my_mtm->tiers[cur_tier].cap) {
		printf("cannot alloc in %d\n", cur_tier);
		abort();
	}

	struct mtm_page *page = (struct mtm_page *)malloc(sizeof(struct mtm_page));
	page->addr = addr;
	page->freq = 0;
	page->tier = cur_tier;
	page->target = -1;
	my_mtm->tiers[cur_tier].size++;

	my_mtm->nr_alloc[cur_tier]++;

	return page;
}

static struct mtm_page *get_page(struct mtm *my_mtm, uint64_t addr, bool &is_new) {
	auto it = my_mtm->pt.find(addr);
	struct mtm_page *page;
	if (it == my_mtm->pt.end()) {
		page = alloc_page(my_mtm,addr);
		my_mtm->pt.insert({addr, page});
		is_new = true;
	} else {
		page = it->second;
		is_new = false;
	}

	return page;
}

static void mtm_proc_req(struct mtm *my_mtm, struct mtm_trace *t) {
	bool is_new;
	struct mtm_page *page = get_page(my_mtm, t->va, is_new);


	int old_freq = page->freq;
	page->freq++;

	nr_sum++;

	update_hist(my_mtm, t->va, old_freq, page->freq, page);

	if (t->is_load) {
		my_mtm->nr_loads[page->tier]++;
		loads++;
	}
	else {
		my_mtm->nr_stores[page->tier]++;
		stores++;
	}
	my_mtm->nr_accesses[page->tier]++;

	if (is_new)
		t->is_alloc = true;
	t->tier = page->tier;
}

static int do_promo(struct mtm *my_mtm, int *promo_prior, vector<vector<vector<struct mtm_page *>>> &promo_cand) {
	vector<pair<int,int>> promo_path = {{1,0}, {2,0}, {2,1}, {3,0}, {3,1}, {3,2}};
	int src, dst;
	int nr_promo = 0;

	for (int i = 0; i < promo_path.size(); i++) {
		src = promo_path[promo_prior[i]].first;
		dst = promo_path[promo_prior[i]].second;

		for (auto &page : promo_cand[src][dst]) {
			if (nr_promo >= mig_traffic)
				return nr_promo;

			page->tier = dst; // mig
			my_mtm->nr_mig[src][dst]++;
			my_mtm->tiers[src].size--; my_mtm->tiers[dst].size++;
			nr_promo++;
		}
	}

	return nr_promo;
}

static int do_promo_default(struct mtm *my_mtm, int *promo_prior, list<struct mtm_page *> promo_list) {
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

static int do_demo(struct mtm *my_mtm, int *demo_prior, vector<stack<struct mtm_page *>> &demo_cand) {
	vector<pair<int,int>> demo_path = {{0,1}, {0,2}, {0,3}, {1,2}, {1,3}, {2,3}};
	int src, dst;
	int nr_demo = 0;
	struct mtm_page *page;
	for (int i = 0; i < demo_path.size(); i++) {
		src = demo_path[demo_prior[i]].first;
		dst = demo_path[demo_prior[i]].second;

		while (my_mtm->tiers[src].size > my_mtm->tiers[src].cap && !demo_cand[src].empty()) {
			page = demo_cand[src].top();

			page->tier = dst; // mig
			my_mtm->nr_mig[src][dst]++;
			my_mtm->tiers[src].size--; my_mtm->tiers[dst].size++;
			nr_demo++;
		}
	}

	for (int i = 0; i < nr_tiers; i++) {
		if (my_mtm->tiers[i].size > my_mtm->tiers[i].cap)
			return -1;
	}

	return 0;
}

static int do_demo_default(struct mtm *my_mtm, int *demo_prior, vector<stack<struct mtm_page *>> &demo_cand) {
	vector<pair<int,int>> demo_path = {{0,1}, {1,2}, {2,3}};
	int src, dst;
	int nr_demo = 0;
	struct mtm_page *page;
	for (int i = 0; i < demo_path.size(); i++) {
		src = demo_path[i].first;
		dst = demo_path[i].second;
		//int margin = (int)ceil((float)my_mtm->tiers[src].cap * NR_REV_DEMO/100);
		int margin = (int)ceil((float)my_mtm->tiers[0].cap * NR_REV_DEMO/100);

		while (my_mtm->tiers[src].size > (my_mtm->tiers[src].cap - margin) && !demo_cand[src].empty()) {
			page = demo_cand[src].top();

			if (dst == 3 && my_mtm->tiers[3].cap <= my_mtm->tiers[3].size)
				break;

			page->tier = dst; // mig
			my_mtm->nr_mig[src][dst]++;
			my_mtm->tiers[src].size--; my_mtm->tiers[dst].size++;
			nr_demo++;
		}
	}

	for (int i = 0; i < nr_tiers; i++) {
		if (my_mtm->tiers[i].size > my_mtm->tiers[i].cap)
			return -1;
	}

	return 0;
}

static vector<vector<vector<struct mtm_page*>>> scan_hist_for_promo(struct mtm *my_mtm) {
	int promo_target = 0;
	int promo_nr_scan = 0;
	vector<vector<vector<struct mtm_page *>>> promo_cand(nr_tiers, vector<vector<struct mtm_page*>>(nr_tiers, vector<struct mtm_page *>()));

	int nr_need_move_item = 0;

	for (auto it = my_mtm->hist.rbegin(); it != my_mtm->hist.rend(); ++it) {
		if (it->second.size() == 0) continue;

		if (promo_nr_scan >= my_mtm->tiers[promo_target].cap) {
			promo_target++;
			if (promo_target >= MTM_MAX_TIER)
				abort();

			promo_nr_scan = 0;
		}

		for (auto &bin_item : it->second) {
			promo_nr_scan++;
			if (bin_item.second->tier == promo_target)
				continue;
			promo_cand[bin_item.second->tier][promo_target].push_back(bin_item.second);
			nr_need_move_item++;
		}
	}

	return promo_cand;
}

static list<struct mtm_page *> scan_hist_for_promo_default(struct mtm *my_mtm) {
	int promo_target = 0;
	int promo_nr_scan = 0;
	list<struct mtm_page *> promo_list;

	int nr_need_move_item = 0;

	for (auto it = my_mtm->hist.rbegin(); it != my_mtm->hist.rend(); ++it) {
		if (it->second.size() == 0) continue;


		for (auto &bin_item : it->second) {
			if (promo_nr_scan >= my_mtm->tiers[promo_target].cap) {
				promo_target++;
				if (promo_target >= MTM_MAX_TIER)
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


static vector<stack<struct mtm_page*>> scan_hist_for_demo(struct mtm *my_mtm) {
	vector<stack<struct mtm_page *>> demo_cand(nr_tiers, stack<struct mtm_page *>());


	for (auto it = my_mtm->hist.rbegin(); it != my_mtm->hist.rend(); ++it) {
		if (it->second.size() == 0) continue;
	
		for (auto &bin_item : it->second) {
			demo_cand[bin_item.second->tier].push(bin_item.second);
		}
	}

	return demo_cand;
}

static int do_mig(struct mtm *my_mtm, int *promo_prior, int *demo_prior) {



	if (mode == M_DEFAULT) {
		auto promo_list = scan_hist_for_promo_default(my_mtm);
		if (do_promo_default(my_mtm, promo_prior, promo_list) == 0)
			return 0;
	} else {
		auto promo_cand = scan_hist_for_promo(my_mtm);
		if (do_promo(my_mtm, promo_prior, promo_cand) == 0)
			return 0;
	}




	auto demo_cand = scan_hist_for_demo(my_mtm);
	if (mode == M_DEFAULT) {
		if (do_demo_default(my_mtm, demo_prior, demo_cand) < 0)
			abort();
	} else {
		if (do_demo(my_mtm, demo_prior, demo_cand) < 0)
			return -1;
	}



	return 0;
}


struct mtm_perf calc_perf(struct mtm *my_mtm) {
	int64_t lat_acc = 0, lat_mig = 0, lat_alc = 0;
	int nr_acc[4] = {0,};
	int sum;
	for (int i = 0; i < nr_tiers; i++) {
		lat_acc += my_mtm->nr_loads[i] * lat_loads[i] + my_mtm->nr_stores[i] * lat_stores[i];
		lat_alc += my_mtm->nr_alloc[i] * lat_4KB_writes[i];
		nr_acc[i] = my_mtm->nr_loads[i] + my_mtm->nr_stores[i];
		sum += nr_acc[i];
	}

	for (int i = 0; i < nr_tiers; i++) {
		for (int j = 0; j < nr_tiers; j++) {
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
	vector<unordered_set<uint64_t>> addr_by_tier = vector(nr_tiers, unordered_set<uint64_t>());

	int period = 0;

	string aorder = "";
	for (int i = 0; i < nr_tiers; i++)
		aorder += to_string(my_mtm->alloc_order[i]);

	string output_file = alloc_file;

	if (mode == M_DEFAULT) {
		output_file = output_file + "_mtm_mode" + to_string(mode) + "_aorder" + aorder + ".txt";
	} else {
		string porder = "";
		string dorder = "";

		for (int i = 0; i < 6; i++)
			porder += to_string(my_mtm->promo_prior[i]);

		for (int i = 0; i < 6; i++)
			dorder += to_string(my_mtm->demo_prior[i]);

		output_file = output_file + "_mtm_mode" + to_string(mode) + "_aorder" + aorder + "_porder" + porder + "_dorder" + dorder + ".txt";
	}

	ofstream writeFile(output_file.c_str());
	
	for (int i = 0; i < trace.size(); i++) {
		period = i / mig_period;

		if ((i % mig_period) == 0) {
			for (int j = 0; j < nr_tiers; j++) {
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

	for (int j = 0; j < nr_tiers; j++) {
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
	for (int i = 0; i < nr_tiers; i++) {
		cout << my_mtm->nr_alloc[i] << " " ;
	}
	cout << endl;

	cout << "access stat\n";
	for (int i = 0; i < nr_tiers; i++) {
		cout << my_mtm->nr_accesses[i] << " ";
	}
	cout << endl;

	cout << "mig traffic\n" << endl;
	for (int i = 0; i < nr_tiers; i++) {
		for (int j = 0; j < nr_tiers; j++) {
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


	if (mode == M_DEFAULT) {
		for (int alloc_id = 0; alloc_id < aorder.size(); alloc_id++) {
			cout << "alloc id: " << alloc_id << endl;

			struct mtm *my_mtm = (struct mtm *)malloc(sizeof(struct mtm));
			memset(my_mtm, 0, sizeof(struct mtm));

			for (int i = 0; i < nr_tiers; i++) {
				my_mtm->tiers[i].cap = tiers[i].cap;
				my_mtm->alloc_order[i] = aorder[alloc_id][i];
			}

			my_mtm->hist = map<int, map<uint64_t, struct mtm_page *>>();
			my_mtm->pt = map<uint64_t, struct mtm_page *>();

			 __do_mtm(my_mtm);

			print_mtm_default(my_mtm);
			clear_mtm(my_mtm);
		}
	} else {
		for (int alloc_id = 0; alloc_id < aorder.size(); alloc_id++) {
			cout << "alloc id: " << alloc_id << endl;
			do {
				do {
					struct mtm *my_mtm = (struct mtm *)malloc(sizeof(struct mtm));
					memset(my_mtm, 0, sizeof(struct mtm));

					for (int i = 0; i < nr_tiers; i++) {
						my_mtm->tiers[i].cap = tiers[i].cap;
						my_mtm->alloc_order[i] = aorder[alloc_id][i];
					}

					my_mtm->hist = map<int, map<uint64_t, struct mtm_page *>>();
					my_mtm->pt = map<uint64_t, struct mtm_page *>();

					for (int i = 0; i < 6; i++) {
						//cout << promo[i];
						my_mtm->promo_prior[i] = promo[i];
					}
					//cout << " ";

					for (int i = 0; i < 6; i++) {
						//cout << demo[i];
						my_mtm->demo_prior[i] = demo[i];
					}

					pthread_create(&threads[cur_id++], NULL, __do_mtm, (void *)my_mtm);

					//auto ret = __do_mtm(my_mtm);
					
					nr_proc++;


					if ((cur_id % NR_MAX_TH) == 0) {
						for (int j = 0; j < NR_MAX_TH; j++) {
							pthread_join(threads[j], &res);
							my_mtm = (struct mtm *)res;
							print_mtm(my_mtm);
							clear_mtm(my_mtm);
						}
						cur_id = 0;
					}


					/*
					if (ret.lat_acc == 0)
						nr_fail++;
					else
						nr_succ++;
						*/
					//clear_mtm(my_mtm);
				} while (std::next_permutation(demo.begin(), demo.end()));

				auto current_time = steady_clock::now();
				auto duration = duration_cast<seconds>(current_time - start_time).count();
				std::cerr << "Count: " << nr_proc << " - Elapsed time: " << duration << " s" << std::endl;

				std::sort(demo.begin(), demo.end());
			} while (std::next_permutation(promo.begin(), promo.end()));
			std::sort(promo.begin(), promo.end());
		}

		cout << "Successed: " << nr_succ << "Failed: " << nr_fail;

	}
}
