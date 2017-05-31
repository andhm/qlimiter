#ifndef HM_LIMITER_H
#define HM_LIMITER_H

#include <pthread.h>

#define LT_SUCC		0
#define LT_ERR		-1
#define LT_FAIL		1

#define DEFAULT_MAX_VAL	(1<<(8*sizeof(long)))
#define DEFAULT_MIN_VAL	(1>>(8*sizeof(long)))

#define LT_TIME_TYPE_NONE	0
#define LT_TIME_TYPE_SEC	1
#define LT_TIME_TYPE_MIN 	2
#define LT_TIME_TYPE_HOUR	3
#define LT_TIME_TYPE_DAY	4

#ifdef DEBUG
#define LT_DEBUG(fmt, arg...) php_printf("[DEBUG][%s:%d]" fmt "\n", __FILE__, __LINE__, ##arg)
#else
#define LT_DEBUG(fmt, arg...)
#endif

typedef struct limiter_s {
	long curr_val;
	long init_val;
	long time;
	int time_type;
	int in_use;
	pthread_mutex_t mutex;
} limiter_t;

int limiter_incr(char *key, int step, long initval, long maxval, long *retval, long time, int time_type, pthread_mutex_t *smutex);
int limiter_decr(char *key, int step, long initval, long minval);
long limiter_get(char *key);
void limiter_delete(char *key);

#endif
