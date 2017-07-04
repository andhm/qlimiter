#ifndef HM_LIMITER_H
#define HM_LIMITER_H

#include <pthread.h>

#define LT_VERSION_1	0x01

#define LT_SUCC		0
#define LT_ERR		-1
#define LT_FAIL		1

#define LT_TIME_TYPE_NONE	(1<<0)
#define LT_TIME_TYPE_SEC	(1<<1)
#define LT_TIME_TYPE_MIN 	(1<<2)
#define LT_TIME_TYPE_HOUR	(1<<3)
#define LT_TIME_TYPE_DAY	(1<<4)
#define LT_TIME_TYPE_5SEC	(1<<5)
#define LT_TIME_TYPE_10SEC	(1<<6)
#define LT_TIME_TYPE_CUSTOM	(1<<16)

// #define DEBUG
#ifdef DEBUG
#define LT_DEBUG(fmt, arg...) php_printf("[DEBUG][%s:%d]" fmt "\n", __FILE__, __LINE__, ##arg)
#else
#define LT_DEBUG(fmt, arg...)
#endif

// #define LT_USE_SPINLOCK

#ifdef LT_USE_SPINLOCK

typedef volatile unsigned long atomic_t;
#define atomic_cmp_set(a,b,c) __sync_bool_compare_and_swap(a,b,c)
#define LT_LOCK(limiter) limiter_spin_lock(&limiter->lock, 1)
#define LT_UNLOCK(limiter) limiter_spin_unlock(&limiter->lock, 1)
#define LT_LOCK_INIT(limiter) limiter->lock = 0;

#else

#define LT_LOCK(limiter) pthread_mutex_lock(&limiter->mutex)
#define LT_UNLOCK(limiter) pthread_mutex_unlock(&limiter->mutex)
#define LT_LOCK_INIT(limiter) do {												\
	pthread_mutexattr_t mutex_shared_attr;										\
	pthread_mutexattr_init(&mutex_shared_attr);									\
	pthread_mutexattr_setpshared(&mutex_shared_attr, PTHREAD_PROCESS_SHARED);	\
	pthread_mutex_init(&limiter->mutex, &mutex_shared_attr);					\
} while (0);

#endif

typedef struct limiter_s {
#ifdef LT_USE_SPINLOCK
    atomic_t lock;
#else
    pthread_mutex_t mutex;
#endif
	long curr_val;
	long init_val;
	unsigned long time;
	int version;
	int time_type;
	unsigned int custom_secs; // LT_TIME_TYPE_CUSTOM
	short in_use;
} limiter_t;

int limiter_incr(void *pshm, int step, long initval, long maxval, long *retval, int time_type, unsigned int custom_secs, pthread_mutex_t *smutex);
int limiter_decr(void *pshm, int step, long initval, long minval, long *retval, int time_type, unsigned int custom_secs, pthread_mutex_t *smutex);
int limiter_decr_ex(void *pshm, int step, long *retval, pthread_mutex_t *smutex);
int limiter_get(void *pshm, long *retval);
void limiter_delete(char *key);

inline int limiter_mmap_internal(char *key, size_t size, int open_shm_file_flag, void **ret_shm);
inline void limiter_unmmap_internal(void *pshm, size_t size);

#define limiter_mmap(key, ret_shm) limiter_mmap_internal(key, sizeof(limiter_t), O_CREAT|O_RDWR, ret_shm)
#define limiter_unmmap(shm) limiter_unmmap_internal(shm, sizeof(limiter_t))
#define limiter_mmap_ex(key, ret_shm) limiter_mmap_internal(key, sizeof(limiter_t), O_RDWR, ret_shm)

/***************************************
**	just for QPS here andhm@126.com
****************************************/
#define LT_SEP_NUM 10

#define PREV_QPS_INFO(p_qps)  (p_qps->curr_qps_idx == 1 ? &p_qps->qps[0] : &p_qps->qps[1]);
#define CURR_QPS_INFO(p_qps) &(p_qps->qps[p_qps->curr_qps_idx])
#define SWAP_QPS_INFO(p_qps) (p_qps->curr_qps_idx = p_qps->curr_qps_idx == 1 ? 0 : 1)

#define CURR_TIME(secs, msecs)                  \
    do {                                        \
        struct timeval tp = {0};                \
        if (!gettimeofday(&tp, NULL)) {         \
            secs = (unsigned long)(tp.tv_sec);  \
            msecs = (unsigned int)tp.tv_usec;   \
        }                                       \
    } while (0);

#define BUILD_SEP_IDX(idx, msecs)               \
    do {                                        \
        int _div = 100000;                      \
        int _mod = 10;                          \
        if (LT_SEP_NUM <= 10) {                 \
            _div = 100000;                      \
            _mod = 10;                          \
        } else if (LT_SEP_NUM <= 100) {         \
            _div = 10000;                       \
            _mod = 100;                         \
        } else if (LT_SEP_NUM <= 1000) {        \
            _div = 1000;                        \
            _mod = 1000;                        \
        } else {                                \
        }                                       \
        idx = (msecs/_div)%_mod;                \
    } while (0);

typedef struct limiter_qps_info_s {
    unsigned short sep_vals[LT_SEP_NUM];
    unsigned int time;
} limiter_qps_info_t;

typedef struct limiter_qps_s {
    short version;
    short in_use;
    unsigned char curr_qps_idx;
    struct limiter_qps_info_s qps[2];
#ifdef LT_USE_SPINLOCK
    atomic_t lock;
#else
    pthread_mutex_t mutex;
#endif
} limiter_qps_t;

int limiter_qps(void *pshm, unsigned short maxval, long *retval, pthread_mutex_t *mutex);

#define limiter_qps_mmap(key, ret_shm) limiter_mmap_internal(key, sizeof(limiter_qps_t), O_CREAT|O_RDWR, ret_shm)
#define limiter_qps_unmmap(shm) limiter_unmmap_internal(shm, sizeof(limiter_qps_t))

/***************************************
**	spin here andhm@126.com
****************************************/

#ifdef LT_USE_SPINLOCK
extern int ncpu;
static void limiter_spin_lock(atomic_t *lock, int which) {
    LT_DEBUG("spin_lock, ncpu[%d]", ncpu);
    int i, n;
    for ( ;; ) {
        if (*lock == 0 &&
            atomic_cmp_set(lock, 0, which)) {
            return;
        }

        if (ncpu > 1) {
            for (n = 1; n < 129; n << 1) {
                for (i = 0; i < n; i++) {
                    __asm("pause");
                }
                if (*lock == 0 &&
                    atomic_cmp_set(lock, 0, which)) {
                    return;
                }
            }
        }
        sched_yield();
    }
}
 
 
static void limiter_spin_unlock(atomic_t *lock, int which) {
    atomic_cmp_set(lock, which, 0);
}
#endif

#endif
