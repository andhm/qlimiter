#include "qlimiter.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>

static unsigned long get_round_time(int round, long custom_secs);
static int check_qps(limiter_qps_t *p_qps, unsigned short maxqps, unsigned int timestamp, unsigned int msec, unsigned short *ret_currqps);

int limiter_incr(void *pshm, int step, long initval, long maxval, long *retval, int time_type, unsigned int custom_secs, pthread_mutex_t *smutex) {
	LT_DEBUG("incr pshm[%p], time_type[%d]", pshm, time_type);
	limiter_t *limiter = (limiter_t*)pshm;
	LT_DEBUG("shm info: curr_val=%ld, init_val=%ld, in_use=%d, time=%ld, time_type=%d", limiter->curr_val, limiter->init_val, limiter->in_use, limiter->time, limiter->time_type);

	int retry_times = 0;
retry:

	if (!limiter->in_use) {
		LT_DEBUG("create shm info");
		if (initval + step > maxval) {
			LT_DEBUG("current val[%ld] reach max[%ld]", initval+step, maxval);
			*retval = limiter->curr_val;
			return LT_FAIL;				
		}
		if (pthread_mutex_trylock(smutex) == 0) { 
			limiter->init_val = initval;
			limiter->curr_val = initval + step;
			limiter->in_use = 1;
			limiter->time = get_round_time(time_type, custom_secs);
			limiter->time_type = time_type;
			limiter->custom_secs = time_type == LT_TIME_TYPE_CUSTOM ? custom_secs : 0;
			limiter->version = LT_VERSION_1;

			LT_LOCK_INIT(limiter);

			*retval = limiter->curr_val;
			pthread_mutex_unlock(smutex);
		} else if (retry_times < 5) {
			retry_times++;
			goto retry;
		} else {
			LT_DEBUG("init failed, reach retry max times[%d]", retry_times);
			*retval = limiter->curr_val;
			return LT_ERR;
		}

	} else {
		
		LT_LOCK(limiter);
		LT_DEBUG("update shm info");	

		unsigned long time = 0UL;

		if (limiter->time_type != time_type || (time_type == LT_TIME_TYPE_CUSTOM && custom_secs != limiter->custom_secs)) {
			// want to change
			time = get_round_time(time_type, custom_secs);
			LT_DEBUG("reset it, want to change, old-time_type[%d], new-time_type[%d], old-time[%ld], new-time[%ld], old-custom_secs[%d], new-custom_secs[%d]", limiter->time_type, time_type, limiter->time, time, limiter->custom_secs, custom_secs);
			if (initval + step > maxval) {
				LT_DEBUG("current val[%ld] reach max[%ld]", initval+step, maxval);
				LT_UNLOCK(limiter);
				*retval = limiter->curr_val;
				return LT_FAIL;
			}
			limiter->init_val = initval;
			limiter->curr_val = initval + step;
			limiter->time = time;
			limiter->time_type = time_type;
			limiter->custom_secs = time_type == LT_TIME_TYPE_CUSTOM ? custom_secs : 0;
		} else if (time_type != LT_TIME_TYPE_NONE &&
			(time = get_round_time(limiter->time_type, limiter->custom_secs)) != limiter->time) {
			// time_type not change
			LT_DEBUG("reset it, time not same, time_type[%d], old-time[%ld], new-time[%ld], custom_secs[%d]", limiter->time_type, limiter->time, time, limiter->custom_secs);
			if (initval + step > maxval) {
				LT_DEBUG("current val[%ld] reach max[%ld]", initval+step, maxval);
				LT_UNLOCK(limiter);
				*retval = limiter->curr_val;
				return LT_FAIL;
			}
			limiter->init_val = initval;
			limiter->curr_val = initval + step;
			limiter->time = time;
			limiter->time_type = time_type;
			limiter->custom_secs = time_type == LT_TIME_TYPE_CUSTOM ? custom_secs : 0;
		} else {
			// same, no change
			if (limiter->curr_val + step > maxval) {
				LT_DEBUG("current val[%ld] reach max[%ld]", limiter->curr_val+step, maxval);
				LT_UNLOCK(limiter);
				*retval = limiter->curr_val;
				return LT_FAIL;
			}
			limiter->curr_val += step;
		}

		*retval = limiter->curr_val;
		LT_UNLOCK(limiter);
	}

	return LT_SUCC;
}

int limiter_decr(void *pshm, int step, long initval, long minval, long *retval, int time_type, unsigned int custom_secs, pthread_mutex_t *smutex) {
	LT_DEBUG("decr pshm[%p], time_type[%d]", pshm, time_type);
	limiter_t *limiter = (limiter_t*)pshm;
	LT_DEBUG("shm info: curr_val=%ld, init_val=%ld, in_use=%d, time=%ld, time_type=%d", limiter->curr_val, limiter->init_val, limiter->in_use, limiter->time, limiter->time_type);

	int retry_times = 0;
retry:

	if (!limiter->in_use) {
		LT_DEBUG("create shm info");
		if (initval - step < minval) {
			LT_DEBUG("current val[%ld] reach min[%ld]", initval-step, minval);
			*retval = limiter->curr_val;
			return LT_FAIL;				
		}
		if (pthread_mutex_trylock(smutex) == 0) { 
			limiter->init_val = initval;
			limiter->curr_val = initval - step;
			limiter->in_use = 1;
			limiter->time = get_round_time(time_type, custom_secs);
			limiter->time_type = time_type;
			limiter->custom_secs = time_type == LT_TIME_TYPE_CUSTOM ? custom_secs : 0;
			limiter->version = LT_VERSION_1;

			LT_LOCK_INIT(limiter);

			*retval = limiter->curr_val;
			pthread_mutex_unlock(smutex);
		} else if (retry_times < 5) {
			retry_times++;
			goto retry;
		} else {
			LT_DEBUG("init failed, reach retry max times[%d]", retry_times);
			*retval = limiter->curr_val;
			return LT_ERR;
		}

	} else {
		
		LT_LOCK(limiter);
		LT_DEBUG("update shm info");	

		unsigned long time = 0UL;

		if (limiter->time_type != time_type || (time_type == LT_TIME_TYPE_CUSTOM && custom_secs != limiter->custom_secs)) {
			// want to change
			time = get_round_time(time_type, custom_secs);
			LT_DEBUG("reset it, want to change, old-time_type[%d], new-time_type[%d], old-time[%ld], new-time[%ld], old-custom_secs[%d], new-custom_secs[%d]", limiter->time_type, time_type, limiter->time, time, limiter->custom_secs, custom_secs);
			if (initval - step < minval) {
				LT_DEBUG("current val[%ld] reach min[%ld]", initval-step, minval);
				LT_UNLOCK(limiter);
				*retval = limiter->curr_val;
				return LT_FAIL;
			}
			limiter->init_val = initval;
			limiter->curr_val = initval - step;
			limiter->time = time;
			limiter->time_type = time_type;
			limiter->custom_secs = time_type == LT_TIME_TYPE_CUSTOM ? custom_secs : 0;
		} else if (time_type != LT_TIME_TYPE_NONE &&
			(time = get_round_time(limiter->time_type, limiter->custom_secs)) != limiter->time) {
			// time_type not change
			LT_DEBUG("reset it, time not same, time_type[%d], old-time[%ld], new-time[%ld], custom_secs[%d]", limiter->time_type, limiter->time, time, limiter->custom_secs);
			if (initval - step < minval) {
				LT_DEBUG("current val[%ld] reach min[%ld]", initval-step, minval);
				LT_UNLOCK(limiter);
				*retval = limiter->curr_val;
				return LT_FAIL;
			}
			limiter->init_val = initval;
			limiter->curr_val = initval - step;
			limiter->time = time;
			limiter->time_type = time_type;
			limiter->custom_secs = time_type == LT_TIME_TYPE_CUSTOM ? custom_secs : 0;
		} else {
			// same, no change
			if (limiter->curr_val - step < minval) {
				LT_DEBUG("current val[%ld] reach min[%ld]", limiter->curr_val-step, minval);
				LT_UNLOCK(limiter);
				*retval = limiter->curr_val;
				return LT_FAIL;
			}
			limiter->curr_val -= step;
		}

		*retval = limiter->curr_val;
		LT_UNLOCK(limiter);
	}

	return LT_SUCC;
}

int limiter_decr_ex(void *pshm, int step, long *retval, pthread_mutex_t *smutex) {
	LT_DEBUG("decr_ex pshm[%p], step[%d]", pshm, step);
	limiter_t *limiter = (limiter_t*)pshm;
	LT_DEBUG("shm info: curr_val=%ld, init_val=%ld, in_use=%d, time=%ld, time_type=%d", limiter->curr_val, limiter->init_val, limiter->in_use, limiter->time, limiter->time_type);

	LT_LOCK(limiter);
	if (!limiter->in_use) {
		LT_DEBUG("shm file not use");
		LT_UNLOCK(limiter);
		*retval = 0;
		return LT_ERR;
	}

	LT_DEBUG("update shm info");

	if (limiter->curr_val - step < limiter->init_val) {
		LT_DEBUG("current val[%ld] reach min[%ld]", limiter->curr_val - step, limiter->init_val);
		LT_UNLOCK(limiter);
		*retval = limiter->curr_val;
		return LT_FAIL;
	}

	limiter->curr_val -= step;
	*retval = limiter->curr_val;

	LT_UNLOCK(limiter);

	return LT_SUCC;
}

void limiter_delete(char *key) {
	// not safe here
	LT_DEBUG("delete key[%s]", key);
	shm_unlink(key);					
}

int limiter_get(void *pshm, long *retval) {
	LT_DEBUG("get pshm[%p]", pshm);
	limiter_t *limiter = (limiter_t*)pshm;
	LT_DEBUG("shm info: curr_val=%ld, init_val=%ld, in_use=%d, time=%ld, time_type=%d", limiter->curr_val, limiter->init_val, limiter->in_use, limiter->time, limiter->time_type);
	
	LT_LOCK(limiter);
	*retval = limiter->curr_val;
	LT_UNLOCK(limiter);

	return LT_SUCC;
}

static unsigned long get_round_time(int time_type, long custom_secs) {
	int round = 1;
	switch (time_type) {
		case LT_TIME_TYPE_NONE:
		case LT_TIME_TYPE_SEC:
			round = 1;
			break;
		case LT_TIME_TYPE_5SEC:
			round = 5;
			break;
		case LT_TIME_TYPE_10SEC:
			round = 10;
			break;
		case LT_TIME_TYPE_MIN:
			round = 60;
			break;
		case LT_TIME_TYPE_HOUR:
			round = 3600;
			break;
		case LT_TIME_TYPE_DAY:
			round = 86400;
			break;
		case LT_TIME_TYPE_CUSTOM:
			round = custom_secs;
			break;
		default:
			break;
	}

	struct timeval tp = {0};
	unsigned long time = 0UL;
	if (!gettimeofday(&tp, NULL)) {
        time = (unsigned long)(tp.tv_sec);
    }
	int mod = time % (round * 2);
	if (mod >= round) {
		time -= (mod % round);
	} else {
		time -= mod;
	}
	return time;
}

int limiter_qps(void *pshm, unsigned short maxqps, long *retval, pthread_mutex_t *mutex) {
	LT_DEBUG("qps pshm[%p], maxqps[%d]", pshm, maxqps);
	limiter_qps_t *limiter_qps = (limiter_qps_t*)pshm;
	LT_DEBUG("shm info: in_use=%d, version=%d", limiter_qps->in_use, limiter_qps->version);

	int retry_times = 0;
retry:

	if (!limiter_qps->in_use) {
		LT_DEBUG("create shm info");
		if (pthread_mutex_trylock(mutex) == 0) {
			memset(limiter_qps, 0, sizeof(limiter_qps_t));
			limiter_qps->in_use = 1;
			limiter_qps->version = LT_VERSION_1;

			unsigned long curr_secs = 0;
			unsigned int curr_msecs = 0;
			CURR_TIME(curr_secs, curr_msecs);

			limiter_qps->qps[0].time = curr_secs;
			limiter_qps->qps[1].time = curr_secs;
			limiter_qps->curr_qps_idx = 0;
			// always success
			unsigned short retval_dummy;
			check_qps(limiter_qps, maxqps, curr_secs, curr_msecs, &retval_dummy);
			*retval = retval_dummy;

			LT_LOCK_INIT(limiter_qps);

			pthread_mutex_unlock(mutex);
		} else if (retry_times < 5) {
			retry_times++;
			goto retry;
		} else {
			LT_DEBUG("init failed, reach retry max times[%d]", retry_times);
			*retval = 0;
			return LT_ERR;
		}

	} else {
		LT_LOCK(limiter_qps);	
		LT_DEBUG("update shm info");	
		unsigned long curr_secs = 0;
		unsigned int curr_msecs = 0;
		CURR_TIME(curr_secs, curr_msecs);
		LT_DEBUG("curr_secs[%d], curr_msecs[%d]", curr_secs, curr_msecs);

		unsigned short retval_dummy;
		if (LT_SUCC != check_qps(limiter_qps, maxqps, curr_secs, curr_msecs, &retval_dummy)) {
			*retval = retval_dummy;
			LT_UNLOCK(limiter_qps);	
			return LT_FAIL;
		}
		*retval = retval_dummy;
		LT_UNLOCK(limiter_qps);	
	}

	return LT_SUCC;
}

static int check_qps(limiter_qps_t *p_qps, unsigned short maxqps, unsigned int timestamp, unsigned int msec, unsigned short *ret_currqps) {
	limiter_qps_info_t *p_curr_qps_info = CURR_QPS_INFO(p_qps);
	limiter_qps_info_t *p_prev_qps_info = PREV_QPS_INFO(p_qps);
	unsigned int idx = 0;
	BUILD_SEP_IDX(idx, msec);
	LT_DEBUG("idx[%d], msec[%d]", idx, msec);
	LT_DEBUG("timestamp[%d], p_curr_qps_info->time[%d]", timestamp, p_curr_qps_info->time);
	if (p_curr_qps_info->time == timestamp) {
		LT_DEBUG("same time, update it. time[%d]", timestamp);
		// same time
		int i = idx;
		int j = 0;
		unsigned short curr_qps = 0;
		for (; j < LT_SEP_NUM; j++, i--) {
			int _idx;
			if (i < 0) {
				_idx = LT_SEP_NUM + i;
				curr_qps += p_prev_qps_info->sep_vals[_idx];
				LT_DEBUG("prev: same time, update it. i < 0, _idx[%d], curr_qps[%d], sep_val[%d]", _idx, curr_qps, p_prev_qps_info->sep_vals[_idx]);
			} else {
				_idx = i;
				curr_qps += p_curr_qps_info->sep_vals[_idx];
				LT_DEBUG("curr: same time, update it. i >= 0, _idx[%d], curr_qps[%d], sep_val[%d]", _idx, curr_qps, p_curr_qps_info->sep_vals[_idx]);
			}
			if (curr_qps >= maxqps) {
				*ret_currqps = curr_qps;
				LT_DEBUG("current val[%d] reach max[%d]", curr_qps, maxqps);
				return LT_FAIL;
			}
		}
		p_curr_qps_info->sep_vals[idx] += 1;
		*ret_currqps = curr_qps + 1;
	} else if (timestamp - p_curr_qps_info->time == 1) {
		LT_DEBUG("next time, swap it. time[%d]", timestamp);
		// swap
		SWAP_QPS_INFO(p_qps);
		// must
		p_curr_qps_info = CURR_QPS_INFO(p_qps);
		p_curr_qps_info->time = timestamp;
		memset(p_curr_qps_info->sep_vals, 0, LT_SEP_NUM*sizeof(unsigned short));

		p_prev_qps_info = PREV_QPS_INFO(p_qps);
		int i = LT_SEP_NUM - 1;
		unsigned short curr_qps = 0;
		for (; i > idx; i--) {
			curr_qps += p_prev_qps_info->sep_vals[i];
			LT_DEBUG("prev: same time, update it. i < 0, i[%d], curr_qps[%d], sep_val[%d]", i, curr_qps, p_prev_qps_info->sep_vals[i]);
			if (curr_qps >= maxqps) {
				*ret_currqps = curr_qps;
				LT_DEBUG("current val[%d] reach max[%d]", curr_qps, maxqps);
				return LT_FAIL;
			}
		}
		
		p_curr_qps_info->sep_vals[idx] = 1;
		*ret_currqps = curr_qps + 1;
	} else {
		LT_DEBUG("not same time, reset it. time[%d]", timestamp);
		// reset
		memset(p_curr_qps_info, 0, sizeof(limiter_qps_info_t));
		memset(p_prev_qps_info, 0, sizeof(limiter_qps_info_t));
		p_curr_qps_info->time = timestamp;
		p_prev_qps_info->time = timestamp;
		p_curr_qps_info->sep_vals[idx] = 1;
		*ret_currqps = 1;
	}

	return LT_SUCC;
}

int limiter_mmap_internal(char *key, size_t size, int open_shm_file_flag, void **ret_shm) {
	LT_DEBUG("mmap key[%s]", key);
	int fd = shm_open(key, open_shm_file_flag, S_IRUSR|S_IWUSR);
	if (fd == -1) {
		LT_DEBUG("shmopen error.");
		return LT_ERR;				
	}

	if (ftruncate(fd, size) == -1) {
		LT_DEBUG("ftruncate error.");				
		shm_unlink(key);
		close(fd);
		return LT_ERR;
	}

	void *shm;
	shm = (void *)mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if ((long)shm == -1) {
		LT_DEBUG("mmap error.");
		shm_unlink(key);
		close(fd);
		return LT_ERR;			
	}

	close(fd);

	LT_DEBUG("mmap shm[%p]", shm);
	*ret_shm = shm;
	return LT_SUCC;
}

void limiter_unmmap_internal(void *pshm, size_t size) {
	LT_DEBUG("unmmap shm[%p]", pshm);
	if (pshm) {
		munmap(pshm, size);
	}
}
