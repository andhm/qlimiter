# qlimiter
qps限流插件／php

# 背景
针对复杂业务逻辑，需要在php端做流量限制端需求
（共享内存实现，支持原子操作，互斥操作）

# 要求
php5.4+ (含php7)

# 使用
### qlimiter_incr
```php
$key = 'get_list_'.(time()%2); 			// 取模减少key的数量
$step = 1;					// 自增步长
$initval = 0;					// 初始值，(只有首次调用起作用，后续调整不起作用)
$maxval = 100;					// 最大值，超过该值，success置false (只有首次调用起作用，后续调整不起作用)
$success = false;				// true：自增成功；false：内部错误+超过最大值错误
$time_type = QLIMITER_TIME_TYPE_SEC;		// 可选参数，默认按照每秒限流，（如没有时间限制设置为 QLIMITER_TIME_TYPE_NONE）(只有首次调用起作用，后续调整不起作用)
$retval = qlimiter_incr($key, $step, $initval, $maxval, &$success, $time_type);	// 返回自增后的值
if ($success) {
	echo '不限流，当前值：',$retval;
} else {
	echo '限流，当前值：',$retval;
}
```
### qlimiter_delete
```php
$key = 'get_list_'.(time()%2);
qlimiter_delete($key);
```
