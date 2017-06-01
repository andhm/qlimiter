#ifndef HM_LIMITER_H
#define HM_LIMITER_H

#include <pthread.h>

#define LT_SUCC		0
#define LT_ERR		-1
#define LT_FAIL		1

#define DEFAULT_MAX_VAL	(1<<(8*sizeof(long)))
#define DEFAULT_MIN_VAL	(1>>(8*sizeof(long)))

#define LT_TIME_TYPE_NONE	(1<<0)
#define LT_TIME_TYPE_SEC	(1<<1)
#define LT_TIME_TYPE_MIN 	(1<<2)
#define LT_TIME_TYPE_HOUR	(1<<3)
#define LT_TIME_TYPE_DAY	(1<<4)
#define LT_TIME_TYPE_5SEC	(1<<5)
#define LT_TIME_TYPE_10SEC	(1<<6)

//#define DEBUG
#ifdef DEBUG
#define LT_DEBUG(fmt, arg...) php_printf("[DEBUG][%s:%d]" fmt "\n", __FILE__, __LINE__, ##arg)
#else
#define LT_DEBUG(fmt, arg...)
#endif

typedef struct limiter_s {
	pthread_mutex_t mutex;
	long curr_val;
	long init_val;
	unsigned long time;
	int time_type;
	short in_use;
} limiter_t;

int limiter_incr(char *key, int step, long initval, long maxval, long *retval, int time_type, pthread_mutex_t *smutex);
int limiter_decr(char *key, int step, long initval, long minval);
long limiter_get(char *key);
void limiter_delete(char *key);

#endif
