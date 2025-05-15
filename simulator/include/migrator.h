#ifndef __MIGRATOR_H__
#define __MIGRATOR_H__

#include "cxl_tm.h"
#include "tier.h"

struct migrator {
	int pol;
	int rate;
	int traffic_rate;
	struct tier *tiers;
	struct pa_inf *pinf;

};


void init_migrator (int policy, int rate, int traffic_rate);
bool need_urgent_migration();
bool need_migration (uint64_t cur_time);

int do_promo (int id);
int do_demo (int id);
void do_migration (uint64_t cur_time);

bool add_promo_list(int id, struct frame *frame);
bool add_demo_warm_list(int id, struct frame *frame);
bool add_demo_cold_list(int id, struct frame *frame);
bool is_list_available(struct tier *t, int list_num);

#endif
