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
  | Author:  andhm@126.com                                               |
  +----------------------------------------------------------------------+
*/

/* $Id$ */

#ifndef PHP_HM_QLIMITER_H
#define PHP_HM_QLIMITER_H

extern zend_module_entry hm_qlimiter_module_entry;
#define phpext_hm_qlimiter_ptr &hm_qlimiter_module_entry

#define PHP_HM_QLIMITER_VERSION "0.1.0" /* Replace with version number for your extension */

#ifdef PHP_WIN32
#	define PHP_HM_QLIMITER_API __declspec(dllexport)
#elif defined(__GNUC__) && __GNUC__ >= 4
#	define PHP_HM_QLIMITER_API __attribute__ ((visibility("default")))
#else
#	define PHP_HM_QLIMITER_API
#endif

#ifdef ZTS
#include "TSRM.h"
#endif

PHP_MINIT_FUNCTION(hm_qlimiter);
PHP_MSHUTDOWN_FUNCTION(hm_qlimiter);
PHP_MINFO_FUNCTION(hm_qlimiter);

PHP_FUNCTION(qlimiter_incr);
PHP_FUNCTION(qlimiter_delete);

ZEND_BEGIN_MODULE_GLOBALS(hm_qlimiter)
  char *full_shm_dir;
  char *short_shm_dir;
  char *prefix;
  char *shm_user;
  zend_bool enable_shm_dir;
ZEND_END_MODULE_GLOBALS(hm_qlimiter)

#ifdef ZTS
#define HM_QLIMITER_G(v) TSRMG(hm_qlimiter_globals_id, zend_hm_qlimiter_globals *, v)
#else
#define HM_QLIMITER_G(v) (hm_qlimiter_globals.v)
#endif

#endif	/* PHP_HM_QLIMITER_H */


/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
