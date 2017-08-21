#include <stdlib.h>
#include <stdio.h>
#include <dlfcn.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <string.h>

extern "C" {
#define LUA_OK		0
#define LUA_YIELD	1
#define LUA_ERRRUN	2
#define LUA_ERRSYNTAX	3
#define LUA_ERRMEM	4
#define LUA_ERRERR	5
static const char* THREAD_STATUS_STR[] = {"done", "yield", "errrun", "errsyntax", "errmem", "errerr"};

#define LUA_GLOBALSINDEX	(-10002)
struct lua_State;
#define LUA_IDSIZE	60	/* Size of lua_Debug.short_src. */
struct lua_Debug {
  int event;
  const char *name;	/* (n) */
  const char *namewhat;	/* (n) `global', `local', `field', `method' */
  const char *what;	/* (S) `Lua', `C', `main', `tail' */
  const char *source;	/* (S) */
  int currentline;	/* (l) */
  int nups;		/* (u) number of upvalues */
  int linedefined;	/* (S) */
  int lastlinedefined;	/* (S) */
  char short_src[LUA_IDSIZE]; /* (S) */
  /* private part */
  int i_ci;  /* active function */
};
typedef int (*lua_resume_t)(lua_State *L, int narg);
typedef int (*lua_getstack_t) (lua_State *L, int level, lua_Debug *ar);
typedef int (*lua_getinfo_t) (lua_State *L, const char *what, lua_Debug *ar);
typedef int (*lua_gettop_t) (lua_State *L);
#define LUA_TFUNCTION 6
typedef int (*lua_type_t) (lua_State *L, int index);
typedef const char * (*lua_tostring_t) (lua_State *L, int index, size_t* len);
typedef void  (*lua_pushvalue_t) (lua_State *L, int index);
typedef void (*lua_settop_t)(lua_State *L, int idx);
typedef void (*lua_getfield_t) (lua_State *L, int index, const char *k);

static lua_resume_t lua_resume_f = NULL;
static lua_getstack_t lua_getstack_f = NULL;
static lua_getinfo_t lua_getinfo_f = NULL;
static lua_gettop_t lua_gettop_f = NULL;
static lua_type_t lua_type_f = NULL;
static lua_tostring_t lua_tostring_f = NULL;
static lua_pushvalue_t lua_pushvalue_f = NULL;
static lua_settop_t lua_settop_f = NULL;
static lua_getfield_t lua_getfield_f = NULL;

static int* ngx_pid_ptr = NULL;

static const char* getFuncInfo(lua_State* L) {
	const static size_t buflen = 512;
	static char buf[buflen];
	lua_Debug info;

	if (lua_gettop_f(L) > 0 && lua_type_f(L, 1) == LUA_TFUNCTION) {
		lua_pushvalue_f(L, 1);
		lua_getinfo_f(L, ">nSl", &info);
	} else {
		lua_getstack_f(L, 1, &info);
		lua_getinfo_f(L, "nSl", &info);
	}

	snprintf(buf, buflen, "%s,%s,%d",
			info.name, info.short_src,
			(info.currentline > 0) ? info.currentline : info.linedefined);
	return buf;
}

static void initfunc() {
	if (!lua_resume_f) {
		lua_resume_f = (lua_resume_t)dlsym(RTLD_NEXT, "lua_resume");
		lua_getstack_f = (lua_getstack_t)dlsym(RTLD_NEXT, "lua_getstack");
		lua_getinfo_f = (lua_getinfo_t)dlsym(RTLD_NEXT, "lua_getinfo");
		lua_gettop_f = (lua_gettop_t)dlsym(RTLD_NEXT, "lua_gettop");
		lua_type_f = (lua_type_t)dlsym(RTLD_NEXT, "lua_type");
		lua_tostring_f = (lua_tostring_t)dlsym(RTLD_NEXT, "lua_tolstring");
		lua_pushvalue_f = (lua_pushvalue_t)dlsym(RTLD_NEXT, "lua_pushvalue");
		lua_settop_f = (lua_settop_t)dlsym(RTLD_NEXT, "lua_settop");
		lua_getfield_f = (lua_getfield_t)dlsym(RTLD_NEXT, "lua_getfield");
		ngx_pid_ptr = (int*)dlsym(RTLD_DEFAULT, "ngx_pid");
	}
}

int lua_resume(lua_State *L, int narg) {
	initfunc();

	// read configs from nginx.conf once
	static bool configured = false;
	static bool enabled = true;
	static int NGX_LUA_BLOCK_CHECK_MIN_MS = 10;
	static FILE* fp = NULL;
	static char* fpath = NULL;
	static int ngx_pid = -1;
	if (!configured) {
		char* tmp = getenv("NGX_LUA_BLOCK_CHECK");
		enabled = (tmp && strcmp(tmp, "true") == 0);

		char* threshold = getenv("NGX_LUA_BLOCK_CHECK_MIN_MS");
		if (threshold) {
			NGX_LUA_BLOCK_CHECK_MIN_MS = atoi(threshold);
		}

		fpath = getenv("NGX_LUA_BLOCK_CHECK_OUTPUT_FILE");

		ngx_pid = *ngx_pid_ptr;

		configured = true;
	}

	if (!enabled) {
		return lua_resume_f(L, narg);
	}

	timeval tv1;
	gettimeofday(&tv1, NULL);

	const char* funcInfo1 = getFuncInfo(L);

	int ret = lua_resume_f(L, narg);

	timeval tv2;
	gettimeofday(&tv2, NULL);

	if (tv2.tv_usec < tv1.tv_usec) {
		tv2.tv_sec--;
		tv2.tv_usec += 1000000;
	}
	unsigned long delta = (tv2.tv_sec - tv1.tv_sec) * 1000 + (tv2.tv_usec - tv1.tv_usec) / 1000;

	if (delta >= NGX_LUA_BLOCK_CHECK_MIN_MS) {
		tm now;
		localtime_r(&tv1.tv_sec, &now);
		char tmbuf[64];
		strftime(tmbuf, sizeof(tmbuf), "%Y-%m-%d %H:%M:%S", &now);

		const static size_t urllen = 128;
		char url[urllen];
		lua_getfield_f(L, LUA_GLOBALSINDEX, "ngx");
		lua_getfield_f(L, -1, "var");
		lua_getfield_f(L, -1, "request_uri");
		size_t len;
		const char* str = lua_tostring_f(L, -1, &len);
		len = (len > urllen - 1) ? (urllen - 1) : len;
		strncpy(url, str, len);
		url[len] = 0;
		lua_settop_f(L, -(3)-1);

		if (!fp && fpath) {
			char tmp[64];
			sprintf(tmp, "%s.%d", fpath, ngx_pid);
			fp = fopen(tmp, "w");
		}

		FILE* tfp = fp ?: stdout;
		char ts[64];
		sprintf(ts, "%s.%06d %dms ", tmbuf, tv1.tv_usec, delta);
		fwrite(ts, strlen(ts), 1, tfp);
		fwrite(url, strlen(url), 1, tfp);
		fwrite(" ", 1, 1, tfp);
		fwrite(funcInfo1, strlen(funcInfo1), 1, tfp);
		fwrite(" ", 1, 1, tfp);
		const char* funcInfo2 = getFuncInfo(L);
		fwrite(funcInfo2, strlen(funcInfo2), 1, tfp);
		const char* status = (ret >= 0 && ret < sizeof(THREAD_STATUS_STR)) ? THREAD_STATUS_STR[ret] : "unknown";
		fwrite(" ", 1, 1, tfp);
		fwrite(status, strlen(status), 1, tfp);
		fwrite("\n", 1, 1, tfp);
		fflush(tfp);
	}

	return ret;
}
}
