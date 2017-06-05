#include "qlimiter.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

static unsigned long get_round_time(int round, long custom_secs);

int limiter_incr(char *key, int step, long initval, long maxval, long *retval, int time_type, unsigned int custom_secs, pthread_mutex_t *smutex) {
		LT_DEBUG("incr key[%s], time_type[%d]", key, time_type);
		limiter_t *limiter = NULL;
		int fd = shm_open(key, O_CREAT|O_RDWR, S_IRUSR|S_IWUSR);
		if (fd == -1) {
			LT_DEBUG("shmopen error. %s", strerror(errno));
			*retval = 0;
			return LT_ERR;				
		}
		
		if (ftruncate(fd, sizeof(limiter_t)) == -1) {
			LT_DEBUG("ftruncate error. %s", strerror(errno));				
			shm_unlink(key);
			close(fd);
			*retval = 0;
			return LT_ERR;
		}

		void *shm;
		shm = (void *)mmap(NULL, sizeof(limiter_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		if ((long)shm == -1) {
			LT_DEBUG("mmap error. %s", strerror(errno));
			shm_unlink(key);
			close(fd);
			*retval = 0;
			return LT_ERR;			
		}
	
		close(fd);				
	
		limiter = (limiter_t*)shm;
		LT_DEBUG("shm info: curr_val=%ld, init_val=%ld, in_use=%d, time=%ld, time_type=%d", limiter->curr_val, limiter->init_val, limiter->in_use, limiter->time, limiter->time_type);

		int retry_times = 0;
retry:

		if (!limiter->in_use) {
			LT_DEBUG("create shm info");
			if (limiter->init_val + step > maxval) {
				LT_DEBUG("current val[%ld] reach max[%ld]", limiter->init_val+step, maxval);
				*retval = limiter->curr_val;
				munmap(shm, sizeof(limiter_t));
				return LT_FAIL;				
			}
			if (pthread_mutex_trylock(smutex) == 0) { 
				limiter->init_val = initval;
				limiter->curr_val = initval + step;
				limiter->in_use = 1;
				limiter->time = get_round_time(time_type, custom_secs);
				limiter->time_type = time_type;
				limiter->custom_secs = time_type == LT_TIME_TYPE_CUSTOM ? custom_secs : 0;

				pthread_mutexattr_t mutex_shared_attr;
				pthread_mutexattr_init(&mutex_shared_attr);
				pthread_mutexattr_setpshared(&mutex_shared_attr, PTHREAD_PROCESS_SHARED);
				pthread_mutex_init(&limiter->mutex, &mutex_shared_attr);

				pthread_mutex_unlock(smutex);
			} else if (retry_times < 5) {
				retry_times++;
				goto retry;
			} else {
				LT_DEBUG("init failed, reach retry max times[%d]", retry_times);
				*retval = limiter->curr_val;
				munmap(shm, sizeof(limiter_t));
				return LT_ERR;
			}

		} else {
			
			pthread_mutex_lock(&limiter->mutex);
			LT_DEBUG("update shm info");	

			unsigned long time = 0UL;

			if (limiter->time_type != time_type || (time_type == LT_TIME_TYPE_CUSTOM && custom_secs != limiter->custom_secs)) {
				// want to change
				time = get_round_time(time_type, custom_secs);
				LT_DEBUG("reset it, want to change, old-time_type[%d], new-time_type[%d], old-time[%ld], new-time[%ld], old-custom_secs[%d], new-custom_secs[%d]", limiter->time_type, time_type, limiter->time, time, limiter->custom_secs, custom_secs);
				limiter->init_val = initval;
				limiter->curr_val = initval + step;
				limiter->time = time;
				limiter->time_type = time_type;
				limiter->custom_secs = time_type == LT_TIME_TYPE_CUSTOM ? custom_secs : 0;
			} else if (time_type != LT_TIME_TYPE_NONE &&
				(time = get_round_time(limiter->time_type, limiter->custom_secs)) != limiter->time) {
				// time_type not change
				LT_DEBUG("reset it, time not same, time_type[%d], old-time[%ld], new-time[%ld], custom_secs[%d]", limiter->time_type, limiter->time, time, limiter->custom_secs);
				limiter->init_val = initval;
				limiter->curr_val = initval + step;
				limiter->time = time;
				limiter->time_type = time_type;
				limiter->custom_secs = time_type == LT_TIME_TYPE_CUSTOM ? custom_secs : 0;
			} else {
				// same, no change
				if (limiter->curr_val + step > maxval) {
					LT_DEBUG("current val[%ld] reach max[%ld]", limiter->curr_val+step, maxval);
					pthread_mutex_unlock(&limiter->mutex);
					*retval = limiter->curr_val;
					munmap(shm, sizeof(limiter_t));
					return LT_FAIL;
				}
				limiter->curr_val += step;
			}

			pthread_mutex_unlock(&limiter->mutex);
		}

		*retval = limiter->curr_val;
		munmap(shm, sizeof(limiter_t));

		return LT_SUCC;
}

int limiter_decr(char *key, int step, long initval, long minval) {
	return LT_SUCC;				
}

void limiter_delete(char *key) {
	// not safe here
	LT_DEBUG("delete key[%s]", key);
	shm_unlink(key);					
}

long limiter_get(char *key) {
	LT_DEBUG("get key[%s]", key);
	limiter_t *limiter = NULL;
	int fd = shm_open(key, O_CREAT|O_RDWR, S_IRUSR|S_IWUSR);
	if (fd == -1) {
		LT_DEBUG("shmopen error. %s", strerror(errno));
		return LT_ERR;				
	}

	if (ftruncate(fd, sizeof(limiter_t)) == -1) {
		LT_DEBUG("ftruncate error. %s", strerror(errno));				
		shm_unlink(key);
		close(fd);
		return LT_ERR;
	}

	void *shm;
	shm = (void *)mmap(NULL, sizeof(limiter_t), PROT_READ, MAP_SHARED, fd, 0);
	if ((long)shm == -1) {
		LT_DEBUG("mmap error. %s", strerror(errno));				
	}

	close(fd);				

	limiter = (limiter_t*)shm;
	LT_DEBUG("shm info: curr_val=%ld, init_val=%ld, in_use=%d, time=%ld, time_type=%d", limiter->curr_val, limiter->init_val, limiter->in_use, limiter->time, limiter->time_type);
	long curr_val = limiter->curr_val;

	munmap(shm, sizeof(limiter_t));

	return curr_val;
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
