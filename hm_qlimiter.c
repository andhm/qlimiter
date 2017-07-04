/*
  +----------------------------------------------------------------------+
  | PHP Version 5                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2014 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author: andhm@126.com                                                |
  +----------------------------------------------------------------------+
*/

/* $Id$ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "SAPI.h"
#include <fcntl.h>
#include "php_hm_qlimiter.h"
#include "qlimiter.h"

// cpu number
int ncpu = 1;

ZEND_DECLARE_MODULE_GLOBALS(hm_qlimiter)

/* True global resources - no need for thread safety here */
static int le_hm_qlimiter;
static pthread_mutex_t smutex;
static HashTable shm_list;

static void unmmap_shm_dtor(zval *shm);

#define BUILD_SHM_KEY(pkey, key_len, pnew_key)	do {				\
		pnew_key = (char *)emalloc(key_len + 14);					\
		snprintf(pnew_key, key_len + 14, "qlimiter_%s.shm", pkey); 	\
	} while (0)

#define FREE_SHM_KEY(pkey) efree(pkey);

#define BUILD_SHM_KEY2(pkey, key_len, pnew_key_buf, type)						\
	do {																		\
		if (type == 1) {														\
			snprintf(pnew_key_buf, key_len + 18, "qlimiter_qps_%s.shm", pkey);	\
		} else {																\
			snprintf(pnew_key_buf, key_len + 14, "qlimiter_%s.shm", pkey);		\
		}																		\
	} while (0);

#define SHM_PATH_MAX 256

#define LT_DEFAULT_SHM_INFO	0
#define LT_EX_SHM_INFO		1
#define LT_QPS_SHM_INFO		2

typedef struct qlimiter_shm_info_s {
	void *p_shm;
	char shm_type;
} qlimiter_shm_info_t;

#define MMAP_SHM_INTERNAL(key, type, p_shm) do {								\
	key_len = strlen(key);														\
	qlimiter_shm_info_t *shm_info = NULL;										\
	zval *zval_shm = NULL;														\
	LT_DEBUG("shm_list ele num: %d", zend_hash_num_elements(&shm_list));		\
	if ((zval_shm = zend_hash_str_find(&shm_list, key, key_len+1)) != NULL) {	\
		LT_DEBUG("found shm in hashtable, key[%s]", key);						\
		shm_info = (qlimiter_shm_info_t*)Z_PTR_P(zval_shm);						\
	} else {																	\
		LT_DEBUG("not found shm in hashtable, key[%s]", key);					\
		shm_info = (qlimiter_shm_info_t*)malloc(sizeof(qlimiter_shm_info_t));	\
		shm_info->shm_type = type;												\
		int ret_mmap = LT_ERR;													\
		if (type == LT_QPS_SHM_INFO) {											\
			ret_mmap = limiter_qps_mmap(key, &(shm_info->p_shm));				\
		} else if (type == LT_DEFAULT_SHM_INFO) {								\
			ret_mmap = limiter_mmap(key, &(shm_info->p_shm));					\
		} else if (type == LT_EX_SHM_INFO) {									\
			ret_mmap = limiter_mmap_ex(key, &(shm_info->p_shm));				\
		} else {																\
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "mmap unknown type");	\
			free(shm_info);														\
			break;																\
		}																		\
		if (ret_mmap != LT_SUCC) {												\
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "mmap failed");			\
			free(shm_info);														\
			break;																\
		} else {																\
			zval zval_new_shm;													\
			ZVAL_PTR(&zval_new_shm, shm_info);									\
			if (zend_hash_str_add(&shm_list, key, key_len+1, &zval_new_shm) == NULL) {				\
				php_error_docref(NULL TSRMLS_CC, E_WARNING, "mmap: add shm to shm_list failed");	\
				free(shm_info);																		\
				break;																				\
			}																						\
		}																							\
	}																								\
	p_shm = shm_info->p_shm;																		\
} while (0);

#define MMAP_SHM(key, p_shm) MMAP_SHM_INTERNAL(key, LT_DEFAULT_SHM_INFO, p_shm)
#define MMAP_SHM_EX(key, p_shm) MMAP_SHM_INTERNAL(key, LT_EX_SHM_INFO, p_shm)
#define MMAP_SHM_QPS(key, p_shm) MMAP_SHM_INTERNAL(key, LT_QPS_SHM_INFO, p_shm)

ZEND_BEGIN_ARG_INFO_EX(arginfo_incr, 0, 0, 4)
	ZEND_ARG_INFO(0, key)
	ZEND_ARG_INFO(0, step)
	ZEND_ARG_INFO(0, initval)
	ZEND_ARG_INFO(0, maxval)
	ZEND_ARG_INFO(1, success)
	ZEND_ARG_INFO(0, time_type)
	ZEND_ARG_INFO(0, custom_secs)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_decr, 0, 0, 4)
	ZEND_ARG_INFO(0, key)
	ZEND_ARG_INFO(0, step)
	ZEND_ARG_INFO(0, initval)
	ZEND_ARG_INFO(0, minval)
	ZEND_ARG_INFO(1, success)
	ZEND_ARG_INFO(0, time_type)
	ZEND_ARG_INFO(0, custom_secs)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_decr_ex, 0, 0, 2)
	ZEND_ARG_INFO(0, key)
	ZEND_ARG_INFO(0, step)
	ZEND_ARG_INFO(1, success)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_delete, 0, 0, 1)
	ZEND_ARG_INFO(0, key)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_get, 0, 0, 1)
	ZEND_ARG_INFO(0, key)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_qps, 0, 0, 2)
	ZEND_ARG_INFO(0, key)
	ZEND_ARG_INFO(0, maxqps)
	ZEND_ARG_INFO(1, success)
ZEND_END_ARG_INFO()

PHP_INI_BEGIN()
	STD_PHP_INI_ENTRY("hm_qlimiter.prefix", "", PHP_INI_ALL, OnUpdateString, prefix, zend_hm_qlimiter_globals, hm_qlimiter_globals)
	STD_PHP_INI_ENTRY("hm_qlimiter.shm_user", "", PHP_INI_SYSTEM, OnUpdateString, shm_user, zend_hm_qlimiter_globals, hm_qlimiter_globals)
	STD_PHP_INI_BOOLEAN("hm_qlimiter.enable_shm_dir", "0", PHP_INI_SYSTEM, OnUpdateBool, enable_shm_dir, zend_hm_qlimiter_globals, hm_qlimiter_globals)
PHP_INI_END()

/* {{{ hm_qlimiter_functions[]
 *
 * Every user visible function must have an entry in hm_qlimiter_functions[].
 */
const zend_function_entry hm_qlimiter_functions[] = {
	PHP_FE(qlimiter_incr,	arginfo_incr)
	PHP_FE(qlimiter_decr,	arginfo_decr)
	PHP_FE(qlimiter_decr_ex,	arginfo_decr_ex)
	PHP_FE(qlimiter_delete,	arginfo_delete)
	PHP_FE(qlimiter_get,	arginfo_get)
	PHP_FE(qlimiter_qps,	arginfo_qps)
	PHP_FE_END	/* Must be the last line in hm_qlimiter_functions[] */
};
/* }}} */

/* {{{ hm_qlimiter_module_entry
 */
zend_module_entry hm_qlimiter_module_entry = {
#if ZEND_MODULE_API_NO >= 20010901
	STANDARD_MODULE_HEADER,
#endif
	"hm_qlimiter",
	hm_qlimiter_functions,
	PHP_MINIT(hm_qlimiter),
	PHP_MSHUTDOWN(hm_qlimiter),
	NULL,
	NULL,
	PHP_MINFO(hm_qlimiter),
#if ZEND_MODULE_API_NO >= 20010901
	PHP_HM_QLIMITER_VERSION,
#endif
	STANDARD_MODULE_PROPERTIES
};
/* }}} */

#ifdef COMPILE_DL_HM_QLIMITER
ZEND_GET_MODULE(hm_qlimiter)
#endif

// PHP_GINIT_FUNCTION(hm_qlimiter) 
// {
// 	HM_QLIMITER_G(prefix) = "";
// }

/* {{{ PHP_MINIT_FUNCTION
 */
PHP_MINIT_FUNCTION(hm_qlimiter)
{
	REGISTER_INI_ENTRIES();

	REGISTER_LONG_CONSTANT("QLIMITER_TIME_TYPE_NONE", LT_TIME_TYPE_NONE, CONST_CS | CONST_PERSISTENT);
	REGISTER_LONG_CONSTANT("QLIMITER_TIME_TYPE_SEC", LT_TIME_TYPE_SEC, CONST_CS | CONST_PERSISTENT);
	REGISTER_LONG_CONSTANT("QLIMITER_TIME_TYPE_5SEC", LT_TIME_TYPE_5SEC, CONST_CS | CONST_PERSISTENT);
	REGISTER_LONG_CONSTANT("QLIMITER_TIME_TYPE_10SEC", LT_TIME_TYPE_10SEC, CONST_CS | CONST_PERSISTENT);
	REGISTER_LONG_CONSTANT("QLIMITER_TIME_TYPE_MIN", LT_TIME_TYPE_MIN, CONST_CS | CONST_PERSISTENT);
	REGISTER_LONG_CONSTANT("QLIMITER_TIME_TYPE_HOUR", LT_TIME_TYPE_HOUR, CONST_CS | CONST_PERSISTENT);
	REGISTER_LONG_CONSTANT("QLIMITER_TIME_TYPE_DAY", LT_TIME_TYPE_DAY, CONST_CS | CONST_PERSISTENT);
	REGISTER_LONG_CONSTANT("QLIMITER_TIME_TYPE_CUSTOM", LT_TIME_TYPE_CUSTOM, CONST_CS | CONST_PERSISTENT);

	pthread_mutexattr_t mutex_shared_attr;
  	pthread_mutexattr_init(&mutex_shared_attr);
  	pthread_mutexattr_setpshared(&mutex_shared_attr, PTHREAD_PROCESS_SHARED);
  	pthread_mutex_init(&smutex, &mutex_shared_attr);	

  	zend_hash_init(&shm_list, 0, NULL, unmmap_shm_dtor, 1);

  	// cpu number
  	ncpu = sysconf(_SC_NPROCESSORS_CONF);

  	// delete shm files (not cli)
  	DIR *dir;
	char *shm_dir = "/dev/shm";
    if (strcmp(sapi_module.name, "cli") && (dir = opendir(shm_dir)) != NULL) {
   		struct dirent *dir_info;
        char file_path[SHM_PATH_MAX] = {'\0'};
		char *qlimiter_prefix = "qlimiter";
        char cmp_qlimiter_prefix[9] = {'\0'};
		while ((dir_info = readdir(dir)) != NULL) {
			if (dir_info->d_type != DT_REG) {
				continue;
			}
			if (strlen(dir_info->d_name) < 9) {
				continue;
			}
			snprintf(cmp_qlimiter_prefix, 9, dir_info->d_name);
			if (strcmp(qlimiter_prefix, cmp_qlimiter_prefix) != 0) {
				continue;
			}
            strcpy(file_path, shm_dir);
            strcat(file_path, "/");
            strcat(file_path, dir_info->d_name);
            shm_unlink(dir_info->d_name);
            // double delete
			remove(file_path);
        }
		closedir(dir);
	}

	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MSHUTDOWN_FUNCTION
 */
PHP_MSHUTDOWN_FUNCTION(hm_qlimiter)
{
	UNREGISTER_INI_ENTRIES();

	pthread_mutex_destroy(&smutex);

	zend_hash_destroy(&shm_list);

	return SUCCESS;
}
/* }}} */


/* {{{ PHP_MINFO_FUNCTION
 */
PHP_MINFO_FUNCTION(hm_qlimiter)
{
	php_info_print_table_start();
	php_info_print_table_header(2, "hm_qlimiter support", "enabled");
	php_info_print_table_end();

	DISPLAY_INI_ENTRIES();
}
/* }}} */


PHP_FUNCTION(qlimiter_incr)
{
	char *key; 
	long key_len;
	long step, initval, maxval; 
	long time_type = 0, custom_secs = 0;
	zval *success = NULL;
#if PHP_VERSION_ID >= 70000
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "slll|z/ll", &key, &key_len, &step, &initval, &maxval, &success, &time_type, &custom_secs) == FAILURE || 
		!key_len || step <=0 || 
		initval >= maxval || 
		(time_type == LT_TIME_TYPE_CUSTOM && custom_secs <= 0)) {
#else
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "slll|zll", &key, &key_len, &step, &initval, &maxval, &success, &time_type, &custom_secs) == FAILURE || 
		!key_len || 
		step <=0 || 
		initval >= maxval ||
		(time_type == LT_TIME_TYPE_CUSTOM && custom_secs <= 0)) {
#endif	
		RETURN_FALSE
	}
	// time_type 校验
	if (time_type != LT_TIME_TYPE_SEC && time_type != LT_TIME_TYPE_5SEC &&
		time_type != LT_TIME_TYPE_10SEC && time_type != LT_TIME_TYPE_MIN &&
		time_type != LT_TIME_TYPE_HOUR && time_type != LT_TIME_TYPE_DAY &&
		time_type != LT_TIME_TYPE_CUSTOM && time_type != LT_TIME_TYPE_NONE) {
		time_type = LT_TIME_TYPE_SEC;
	}
	
	char new_key[SHM_PATH_MAX] = {'\0'};
	BUILD_SHM_KEY2(key, key_len, new_key, 0);

	void *p_shm = NULL;
	MMAP_SHM(new_key, p_shm);

	LT_DEBUG("qlimiter_incr key[%s]", new_key);

	long retval = 0;
	int ret = LT_ERR;
	if (p_shm != NULL) {
		ret = limiter_incr(p_shm, step, initval, maxval,  &retval, time_type, custom_secs, &smutex);
	}

	if (ret == LT_SUCC) {
		if (success) {
#if PHP_VERSION_ID >= 70000
			zval_ptr_dtor(success);
#endif
			ZVAL_TRUE(success);
		}
		RETURN_LONG(retval);
	}
	if (success) {
#if PHP_VERSION_ID >= 70000
		zval_ptr_dtor(success);
#endif
		ZVAL_FALSE(success);
	}
	RETURN_LONG(retval);
}

PHP_FUNCTION(qlimiter_decr)
{
	char *key; 
	long key_len;
	long step, initval, minval; 
	long time_type = 0, custom_secs = 0;
	zval *success = NULL;
#if PHP_VERSION_ID >= 70000
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "slll|z/ll", &key, &key_len, &step, &initval, &minval, &success, &time_type, &custom_secs) == FAILURE || 
		!key_len || step <=0 || 
		initval <= minval || 
		(time_type == LT_TIME_TYPE_CUSTOM && custom_secs <= 0)) {
#else
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "slll|zll", &key, &key_len, &step, &initval, &minval, &success, &time_type, &custom_secs) == FAILURE || 
		!key_len || 
		step <=0 || 
		initval <= minval ||
		(time_type == LT_TIME_TYPE_CUSTOM && custom_secs <= 0)) {
#endif	
		RETURN_FALSE
	}
	// time_type 校验
	if (time_type != LT_TIME_TYPE_SEC && time_type != LT_TIME_TYPE_5SEC &&
		time_type != LT_TIME_TYPE_10SEC && time_type != LT_TIME_TYPE_MIN &&
		time_type != LT_TIME_TYPE_HOUR && time_type != LT_TIME_TYPE_DAY &&
		time_type != LT_TIME_TYPE_CUSTOM && time_type != LT_TIME_TYPE_NONE) {
		time_type = LT_TIME_TYPE_SEC;
	}
	
	char new_key[SHM_PATH_MAX] = {'\0'};
	BUILD_SHM_KEY2(key, key_len, new_key, 0);

	void *p_shm = NULL;
	MMAP_SHM(new_key, p_shm);

	LT_DEBUG("qlimiter_decr key[%s]", new_key);

	long retval = 0;
	int ret = LT_ERR;
	if (p_shm != NULL) {
		ret = limiter_decr(p_shm, step, initval, minval,  &retval, time_type, custom_secs, &smutex);
	}

	if (ret == LT_SUCC) {
		if (success) {
#if PHP_VERSION_ID >= 70000
			zval_ptr_dtor(success);
#endif
			ZVAL_TRUE(success);
		}
		RETURN_LONG(retval);
	}
	if (success) {
#if PHP_VERSION_ID >= 70000
		zval_ptr_dtor(success);
#endif
		ZVAL_FALSE(success);
	}
	RETURN_LONG(retval);
}

PHP_FUNCTION(qlimiter_decr_ex)
{
	char *key; 
	long key_len;
	long step; 
	zval *success = NULL;
#if PHP_VERSION_ID >= 70000
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "sl|z/", &key, &key_len, &step, &success) == FAILURE || 
		!key_len || step <=0) {
#else
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "sl|z", &key, &key_len, &step, &success) == FAILURE || 
		!key_len || 
		step <=0) {
#endif	
		RETURN_FALSE
	}

	char new_key[SHM_PATH_MAX] = {'\0'};
	BUILD_SHM_KEY2(key, key_len, new_key, 0);

	void *p_shm = NULL;
	MMAP_SHM_EX(new_key, p_shm);

	LT_DEBUG("qlimiter_decr_ex key[%s]", new_key);	

	long retval = 0;
	int ret = LT_ERR;
	if (p_shm != NULL) {
		ret = limiter_decr_ex(p_shm, step, &retval, &smutex);
	}
	
	if (ret == LT_SUCC) {
		if (success) {
#if PHP_VERSION_ID >= 70000
			zval_ptr_dtor(success);
#endif
			ZVAL_TRUE(success);
		}
		RETURN_LONG(retval);
	}
	if (success) {
#if PHP_VERSION_ID >= 70000
		zval_ptr_dtor(success);
#endif
		ZVAL_FALSE(success);
	}
	RETURN_LONG(retval);
}

PHP_FUNCTION(qlimiter_delete)
{
	char *key; 
	long key_len;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s", &key, &key_len) == FAILURE || !key_len) {
		RETURN_FALSE
	}
	
	char new_key[SHM_PATH_MAX] = {'\0'};
	BUILD_SHM_KEY2(key, key_len, new_key, 0);

	limiter_delete(new_key);
	
	RETURN_TRUE
}

PHP_FUNCTION(qlimiter_get)
{
	char *key; 
	long key_len;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s", &key, &key_len) == FAILURE || !key_len) {
		RETURN_FALSE
	}
	
	char new_key[SHM_PATH_MAX] = {'\0'};
	BUILD_SHM_KEY2(key, key_len, new_key, 0);

	void *p_shm = NULL;
	MMAP_SHM_EX(new_key, p_shm);

	LT_DEBUG("qlimiter_get key[%s]", new_key);	

	long retval = 0;
	int ret = LT_ERR;
	if (p_shm != NULL) {
		ret = limiter_get(p_shm, &retval);
	}

	if (ret == LT_SUCC) {
		RETURN_LONG(retval);
	}
	
	RETURN_FALSE
}

PHP_FUNCTION(qlimiter_qps)
{
	char *key; 
	long key_len;
	long maxqps; 
	zval *success = NULL;
#if PHP_VERSION_ID >= 70000
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "sl|z/", &key, &key_len, &maxqps, &success) == FAILURE || 
		!key_len || maxqps <=0) {
#else
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "sl|z", &key, &key_len, &maxqps, &success) == FAILURE || 
		!key_len || 
		maxqps <=0) {
#endif	
		RETURN_FALSE
	}

	char new_key[SHM_PATH_MAX] = {'\0'};
	BUILD_SHM_KEY2(key, key_len, new_key, 1);

	void *p_shm = NULL;
	MMAP_SHM_QPS(new_key, p_shm);

	LT_DEBUG("qlimiter_qps key[%s]", new_key);	

	long retval = 0;
	int ret = LT_ERR;
	if (p_shm != NULL) {
		ret = limiter_qps(p_shm, maxqps, &retval, &smutex);
	}

	if (ret == LT_SUCC) {
		if (success) {
#if PHP_VERSION_ID >= 70000
			zval_ptr_dtor(success);
#endif
			ZVAL_TRUE(success);
		}
		RETURN_LONG(retval);
	}
	if (success) {
#if PHP_VERSION_ID >= 70000
		zval_ptr_dtor(success);
#endif
		ZVAL_FALSE(success);
	}
	RETURN_LONG(retval);
}

static void unmmap_shm_dtor(zval *shm) {
	qlimiter_shm_info_t *shm_info = (qlimiter_shm_info_t*)Z_PTR_P(shm);
	if (shm_info->shm_type == LT_DEFAULT_SHM_INFO || shm_info->shm_type == LT_EX_SHM_INFO) {
		limiter_unmmap(shm_info->p_shm);
	} else if (shm_info->shm_type == LT_QPS_SHM_INFO) {
		limiter_qps_unmmap(shm_info->p_shm);
	}
	free(shm_info);
}

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
