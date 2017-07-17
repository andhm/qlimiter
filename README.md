# qlimiter
qps限流插件／php

# 背景
针对复杂业务逻辑，需要在php本机做流量限制
（多进程【php-fpm】共享内存实现，支持原子操作）
## 支持三种限流方式
### 1) 绝对时间限流	
在指定的自然时间内，如果超过指定阈值即限流。
涉及的方法：qlimiter_incr
### 2) 同一时刻限流
服务器同时处理的请求数超过指定阈值即限流。
涉及的方法：qlimiter_incr qlimiter_decr_ex
### 3) 任意一秒内限流
任意一秒时间段内，如果超过指定阈值即限流。
涉及的方法：qlimiter_qps

# 要求
php5.4+ (含php7)

# 使用
### qlimiter_incr
```php
$key = 'get_list'; 				// 键值
$step = 1;					// 自增步长
$initval = 0;					// 初始值，(只有首次调用起作用，后续调整不起作用)
$maxval = 100;					// 最大值，超过该值，success置false (只有首次调用起作用，后续调整不起作用)
$success = false;				// true：自增成功；false：内部错误+超过最大值错误
$time_type = QLIMITER_TIME_TYPE_SEC;		// 可选参数，默认按照每秒限流，（如没有时间限制设置为 QLIMITER_TIME_TYPE_NONE）(只有首次调用起作用，后续调整不起作用)
						// 各个time_type枚举值如下：
						// LT_TIME_TYPE_NONE	没有时间限制（可以当作普通计数器使用）
						// LT_TIME_TYPE_SEC	每秒限流
						// LT_TIME_TYPE_MIN	每分钟限流
						// LT_TIME_TYPE_HOUR	每小时限流
						// LT_TIME_TYPE_DAY	每天限流
						// LT_TIME_TYPE_5SEC	每5秒限流
						// LT_TIME_TYPE_10SEC	每10秒限流
						// LT_TIME_TYPE_CUSTOM	自定义时间限流，单位s
$retval = qlimiter_incr($key, $step, $initval, $maxval, $success, $time_type);	// 返回自增后的值
if ($success) {
	echo '不限流，当前值：',$retval;
} else {
	echo '限流，当前值：',$retval;
}
```
### qlimiter_decr 
参考qlimiter_incr

### qlimiter_decr_ex 
只针对已经存在的key的自减
```php
$key = 'get_list';
$step = 1;
$success = false;
$retval = qlimiter_decr_ex($key, $step, $success);
var_dump($retval, $success);
```

### qlimiter_get
```php
$key = 'get_list';
$retval = qlimiter_get($key);
var_dump(retval);
```

### qlimiter_qps 
针对qps限流，能做到任意一秒内不超过指定阈值限流, key和以上方式均不同，无法使用 类似 qlimiter_get获取此种方式的qps值
```php
$key = 'get_list_1';
$maxval = 100; // 最大值不能超过 65535，否则结果不可预知
$success = false;
$retval = qlimiter_qps($key, $maxval, $success);
if ($success) {
	echo '不限流，当前值：',$retval;
} else {
	echo '限流，当前值：',$retval;
}
```



