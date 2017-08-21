ngx_lua_block_check.so: ngx_lua_block_check.cpp
	g++ -O2 -fPIC -shared -o $@ $< -ldl
