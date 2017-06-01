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
  | Author:                                                              |
  +----------------------------------------------------------------------+
*/

/* $Id$ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "php_hm_qlimiter.h"
#include "qlimiter.h"

//ZEND_DECLARE_MODULE_GLOBALS(hm_qlimiter)

/* True global resources - no need for thread safety here */
static int le_hm_qlimiter;
pthread_mutex_t smutex;

#define BUILD_SHM_KEY(pkey, key_len, pnew_key)		\
	pnew_key = emalloc(key_len + 14);				\
	snprintf(pnew_key, key_len + 14, "qlimiter_%s.shm", pkey);

#define FREE_SHM_KEY(pkey) efree(pkey);

ZEND_BEGIN_ARG_INFO_EX(arginfo_incr, 0, 0, 2)
	ZEND_ARG_INFO(0, key)
	ZEND_ARG_INFO(0, step)
	ZEND_ARG_INFO(0, initval)
	ZEND_ARG_INFO(0, maxval)
	ZEND_ARG_INFO(1, success)
	ZEND_ARG_INFO(0, time_type)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_delete, 0, 0, 1)
	ZEND_ARG_INFO(0, key)
ZEND_END_ARG_INFO()

/* {{{ hm_qlimiter_functions[]
 *
 * Every user visible function must have an entry in hm_qlimiter_functions[].
 */
const zend_function_entry hm_qlimiter_functions[] = {
	PHP_FE(qlimiter_incr,	arginfo_incr)
	PHP_FE(qlimiter_delete,	arginfo_delete)
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


/* {{{ PHP_MINIT_FUNCTION
 */
PHP_MINIT_FUNCTION(hm_qlimiter)
{
	/* If you have INI entries, uncomment these lines 
	REGISTER_INI_ENTRIES();
	*/

	REGISTER_LONG_CONSTANT("QLIMITER_TIME_TYPE_NONE", LT_TIME_TYPE_NONE, CONST_CS | CONST_PERSISTENT);
	REGISTER_LONG_CONSTANT("QLIMITER_TIME_TYPE_SEC", LT_TIME_TYPE_SEC, CONST_CS | CONST_PERSISTENT);
	REGISTER_LONG_CONSTANT("QLIMITER_TIME_TYPE_5SEC", LT_TIME_TYPE_5SEC, CONST_CS | CONST_PERSISTENT);
	REGISTER_LONG_CONSTANT("QLIMITER_TIME_TYPE_10SEC", LT_TIME_TYPE_10SEC, CONST_CS | CONST_PERSISTENT);
	REGISTER_LONG_CONSTANT("QLIMITER_TIME_TYPE_MIN", LT_TIME_TYPE_MIN, CONST_CS | CONST_PERSISTENT);
	// REGISTER_LONG_CONSTANT("QLIMITER_TIME_TYPE_HOUR", LT_TIME_TYPE_HOUR, CONST_CS | CONST_PERSISTENT);
	// REGISTER_LONG_CONSTANT("QLIMITER_TIME_TYPE_DAY", LT_TIME_TYPE_DAY, CONST_CS | CONST_PERSISTENT);

	pthread_mutexattr_t mutex_shared_attr;
  	pthread_mutexattr_init(&mutex_shared_attr);
  	pthread_mutexattr_setpshared(&mutex_shared_attr, PTHREAD_PROCESS_SHARED);
  	pthread_mutex_init(&smutex, &mutex_shared_attr);	
	
	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MSHUTDOWN_FUNCTION
 */
PHP_MSHUTDOWN_FUNCTION(hm_qlimiter)
{
	/* uncomment this line if you have INI entries
	UNREGISTER_INI_ENTRIES();
	*/
	pthread_mutex_destroy(&smutex);
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

	/* Remove comments if you have entries in php.ini
	DISPLAY_INI_ENTRIES();
	*/
}
/* }}} */


PHP_FUNCTION(qlimiter_incr)
{
	char *key; 
	long key_len;
	long step, initval, maxval; 
	long time_type = 0;
	zval *success = NULL;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s|lllzl", &key, &key_len, &step, &initval, &maxval, &success, &time_type) == FAILURE || !key_len || step <=0 || initval >= maxval) {
		RETURN_FALSE
	}

	// time_type 校验
	if (time_type != LT_TIME_TYPE_SEC && time_type != LT_TIME_TYPE_5SEC &&
		time_type != LT_TIME_TYPE_10SEC && time_type != LT_TIME_TYPE_MIN &&
		time_type != LT_TIME_TYPE_HOUR && time_type != LT_TIME_TYPE_DAY &&
		time_type != LT_TIME_TYPE_NONE) {
		time_type = LT_TIME_TYPE_SEC;
	}

	char *new_key;
	BUILD_SHM_KEY(key, key_len, new_key);
	
	long retval;
	int ret = limiter_incr(new_key, step, initval, maxval,  &retval, time_type, &smutex);
	FREE_SHM_KEY(new_key);

	if (ret == LT_SUCC) {
		if (success) {
			ZVAL_TRUE(success);
		}
		RETURN_LONG(retval);
	}
	if (success) {
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

	char *new_key;
	
	BUILD_SHM_KEY(key, key_len, new_key);
	limiter_delete(new_key);
	FREE_SHM_KEY(new_key);
	
	RETURN_TRUE
}

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
