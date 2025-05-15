#ifndef __MONITOR_H__
#define __MONITOR_H__

#include "cxl_tm.h"
#include "tier.h"

struct monitor {
	struct tier *tiers;
	int pol;
	int rate;
	int hot_threshold;
};

void init_monitor(int pol, int rate, int hot_threshold);
bool need_monitoring(uint64_t cur_time);
void do_monitoring(uint64_t cur_time, struct mem_req &mreq);

#endif
