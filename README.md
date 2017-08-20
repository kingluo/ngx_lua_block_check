# ngx_lua_block_check

This tool is used to find out long running coroutine resume,
which means CPU intensive computations and/or blocking on system calls (e.g. disk read/write, os.execute).

Long resume would cause many issues, e.g. cosocket timeout.

**Just like systemtap, this tool do not touch openresty source codes, and it is NOT nginx module too!**

**It just hooks `lua_resume()` via LD_PRELOAD to do the check.**

## Why not systemtap?

* systemtap would impact the runtime performance more or less, but this tool would not.
* it seems hard to obtain infomations inside `lua_State` via systemtap.

## Compile

```
# build ngx_lua_block_check.so
make
```

## Configure nginx.conf

```
# enabled or not
env NGX_LUA_BLOCK_CHECK=true;
# log threshold, in ms
env NGX_LUA_BLOCK_CHECK_MIN_MS=10;
# log file prefix
env NGX_LUA_BLOCK_CHECK_OUTPUT_FILE=/tmp/ngx_lua_block_check.txt;
```

if you change configs, you could reload the nginx to take effect.

## Restart openresty (only once)

```
/usr/local/openresty/bin/openresty -s stop
LD_PRELOAD=./ngx_lua_block_check.so /usr/local/openresty/bin/openresty
```

## Check the log file

```
tail -f /tmp/ngx_lua_block_check.log*
```

```
==> /tmp/ngx_lua_block_check.log.52349 <==
2017-08-20 20:13:02.894690 11ms /test (null),=content_by_lua(nginx.conf:56),-1 test,/usr/local/openresty/lualib/test.lua,7 yield
2017-08-20 20:13:10.013846 10ms /test (null),=content_by_lua(nginx.conf:56),-1 test,/usr/local/openresty/lualib/test.lua,7 yield

==> /tmp/ngx_lua_block_check.log.52353 <==
2017-08-20 20:13:15.990393 11ms /test (null),=content_by_lua(nginx.conf:56),-1 test,/usr/local/openresty/lualib/test.lua,7 yield
```

The log files are suffix by worker process pid.

## Log format

```
<resume begin timestamp> <resume duration> <url> <func1,src1,lineno1> <func2,src2,lineno2> <resume status>
```

Here the `func1` is the resume entry function, while the `func2` is the resume exit function.
