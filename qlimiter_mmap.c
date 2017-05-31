#include "qlimiter.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

int limiter_incr(char *key, int step, long initval, long maxval, long *retval, long time, int time_type, pthread_mutex_t *smutex) {
		LT_DEBUG("incr key[%s]", key);
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
				*retval = limiter->curr_val;
				munmap(shm, sizeof(limiter_t));
				return LT_FAIL;				
			}
			if (pthread_mutex_trylock(smutex) == 0) { 
				limiter->init_val = initval;
				limiter->curr_val = initval + step;
				limiter->in_use = 1;
				limiter->time = time;
				limiter->time_type = time_type;

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
			if (limiter->curr_val + step > maxval) {
				LT_DEBUG("current val[%ld] reach max[%ld]", limiter->curr_val+step, maxval);
				pthread_mutex_unlock(&limiter->mutex);
				*retval = limiter->curr_val;
				munmap(shm, sizeof(limiter_t));
				return LT_FAIL;
			}
			limiter->curr_val += step;
			if (time_type != LT_TIME_TYPE_NONE && time != limiter->time) {
				LT_DEBUG("reset it, time not same, time_type[%d], old-time[%ld], new-time[%ld]", limiter->time_type, limiter->time, time);
				limiter->init_val = initval;
				limiter->curr_val = initval + step;
				limiter->time = time;
				limiter->time_type = time_type;
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
