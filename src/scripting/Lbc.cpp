/*
 * QMud Project
 * Copyright (c) 2026 Panagiotis Kalogiratos (Nodens)
 *
 * File: Lbc.cpp
 * Role: Embedded bc calculator core implementation used by scripting functions requiring arbitrary-precision
 * arithmetic.
 */

/*
 * lbc.c
 * big-number library for Lua 5.1 based on GNU bc-1.06 core library
 * Luiz Henrique de Figueiredo <lhf@tecgraf.puc-rio.br>
 * 04 Apr 2010 22:40:22
 * This code is hereby placed in the public domain.
 */

// Implements the "bc" library.

// In particular:

//   bc.add
//   bc.compare
//   bc.digits
//   bc.div
//   bc.divmod
//   bc.isneg
//   bc.iszero
//   bc.mod
//   bc.mul
//   bc.neg
//   bc.number
//   bc.pow
//   bc.powmod
//   bc.sqrt
//   bc.sub
//   bc.tonumber
//   bc.tostring
//   bc.trunc
//   bc.version

#include <cctype>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>

#include "BcConfig.h"
#include "Number.h"

#include "LuaHeaders.h"

#define MYNAME "bc"
#define MYVERSION                                                                                            \
	MYNAME " library for " LUA_VERSION " / Apr 2010 / "                                                      \
	       "based on GNU bc-1.06"
#define MYTYPE MYNAME " bignumber"

#ifndef QMUD_BC_THREAD_LOCAL
#if defined(_MSC_VER)
#define QMUD_BC_THREAD_LOCAL __declspec(thread)
#else
#define QMUD_BC_THREAD_LOCAL __thread
#endif
#endif

static QMUD_BC_THREAD_LOCAL lua_State *g_bcCurrentLuaState = nullptr;
static char                            g_bcDigitsRegistryKey;

static void                            luaBoxPointer(lua_State *L, void *pointer)
{
	*static_cast<void **>(lua_newuserdata(L, sizeof(void *))) = pointer;
}

static int bcDigits(lua_State *L)
{
	int digits = 0;
	lua_pushlightuserdata(L, static_cast<void *>(&g_bcDigitsRegistryKey));
	lua_gettable(L, LUA_REGISTRYINDEX);
	if (lua_isnumber(L, -1))
		digits = static_cast<int>(lua_tointeger(L, -1));
	lua_pop(L, 1);
	return digits;
}

static void bcSetDigits(lua_State *L, int digits)
{
	lua_pushlightuserdata(L, static_cast<void *>(&g_bcDigitsRegistryKey));
	lua_pushinteger(L, digits);
	lua_settable(L, LUA_REGISTRYINDEX);
}

static int bcParseExponentOrZero(const char *text)
{
	if (text == nullptr || *text == '\0')
		return 0;

	errno            = 0;
	char      *end   = nullptr;
	const long value = strtol(text, &end, 10);
	if (end == text || (end != nullptr && *end != '\0'))
		return 0;
	if ((errno == ERANGE && value == LONG_MAX) || value > INT_MAX)
		return INT_MAX;
	if ((errno == ERANGE && value == LONG_MIN) || value < INT_MIN)
		return INT_MIN;
	return static_cast<int>(value);
}

[[noreturn]] void bc_error(const char *mesg)
{
	lua_State *L = g_bcCurrentLuaState;
	if (L == nullptr)
		std::abort();
	luaL_error(L, "(bc) %s", mesg ? mesg : "not enough memory");
	std::abort();
}

static void Bnew(lua_State *L, bc_num x)
{
	luaBoxPointer(L, x);
	luaL_getmetatable(L, MYTYPE);
	lua_setmetatable(L, -2);
}

static bc_num Bget(lua_State *L, int i)
{
	g_bcCurrentLuaState = L;
	const int digits    = bcDigits(L);
	switch (lua_type(L, i))
	{
	case LUA_TNUMBER:
	case LUA_TSTRING:
	{
		bc_num      x = nullptr;
		const char *s = luaL_checkstring(L, i);
		for (; isspace(*s); s++)
			; /* bc_str2num chokes on spaces */
		bc_str2num(&x, const_cast<char *>(s), digits);
		if (bc_is_zero(x)) /* bc_str2num chokes on sci notation */
		{
			const char *t = strchr(s, 'e');
			if (t == nullptr)
				t = strchr(s, 'E');
			if (t != nullptr)
			{
				bc_num     y = nullptr, n = nullptr;
				const auto mantissaLength = static_cast<size_t>(t - s);
				char      *mantissa       = static_cast<char *>(malloc(mantissaLength + 1));
				if (mantissa == nullptr)
				{
					bc_out_of_memory();
				}
				memcpy(mantissa, s, mantissaLength);
				mantissa[mantissaLength] = '\0';
				bc_str2num(&x, mantissa, digits);
				free(mantissa);
				bc_int2num(&y, 10);
				bc_int2num(&n, bcParseExponentOrZero(t + 1));
				bc_raise(y, n, &y, digits);
				bc_multiply(x, y, &x, digits);
				bc_free_num(&y);
				bc_free_num(&n);
			}
		}
		Bnew(L, x);
		lua_replace(L, i);
		return x;
	}
	default:
		return static_cast<bc_num>(*static_cast<void **>(luaL_checkudata(L, i, MYTYPE)));
	}
	// return nullptr;
}

static int Bdo1(lua_State *L, void (*f)(bc_num a, bc_num b, bc_num *c, int n))
{
	const int digits = bcDigits(L);
	bc_num    a      = Bget(L, 1);
	bc_num    b      = Bget(L, 2);
	bc_num    c      = nullptr;
	f(a, b, &c, digits);
	if (c == nullptr)
		return luaL_error(L, "(bc) operation failed to produce a numeric result");
	Bnew(L, c);
	return 1;
}

static int Bdigits(lua_State *L) /** digits([n]) */
{
	const int digits = bcDigits(L);
	lua_pushinteger(L, digits);
	bcSetDigits(L, static_cast<int>(luaL_optinteger(L, 1, digits)));
	return 1;
}

static int Btostring(lua_State *L) /** tostring(x) */
{
	bc_num a = Bget(L, 1);
#if 0
 if (lua_toboolean(L,2))
 {
  lua_pushlstring(L,a->n_value,a->n_len+a->n_scale);
  lua_pushinteger(L,a->n_len);
  return 2;
 }
 else
#endif
	{
		char *s = bc_num2str(a);
		lua_pushstring(L, s);
		free(s);
		return 1;
	}
}

static int Btonumber(lua_State *L) /** tonumber(x) */
{
	Btostring(L);
	lua_pushnumber(L, lua_tonumber(L, -1));
	return 1;
}

static int Biszero(lua_State *L) /** iszero(x) */
{
	bc_num a = Bget(L, 1);
	lua_pushboolean(L, bc_is_zero(a));
	return 1;
}

static int Bisneg(lua_State *L) /** isneg(x) */
{
	bc_num a = Bget(L, 1);
	lua_pushboolean(L, bc_is_neg(a));
	return 1;
}

static int Bnumber(lua_State *L) /** number(x) */
{
	Bget(L, 1);
	lua_settop(L, 1);
	return 1;
}

static int Bcompare(lua_State *L) /** compare(x,y) */
{
	bc_num a = Bget(L, 1);
	bc_num b = Bget(L, 2);
	lua_pushinteger(L, bc_compare(a, b));
	return 1;
}

static int Beq(lua_State *L)
{
	bc_num a = Bget(L, 1);
	bc_num b = Bget(L, 2);
	lua_pushboolean(L, bc_compare(a, b) == 0);
	return 1;
}

static int Blt(lua_State *L)
{
	bc_num a = Bget(L, 1);
	bc_num b = Bget(L, 2);
	lua_pushboolean(L, bc_compare(a, b) < 0);
	return 1;
}

static int Badd(lua_State *L) /** add(x,y) */
{
	return Bdo1(L, bc_add);
}

static int Bsub(lua_State *L) /** sub(x,y) */
{
	return Bdo1(L, bc_sub);
}

static int Bmul(lua_State *L) /** mul(x,y) */
{
	return Bdo1(L, bc_multiply);
}

static int Bpow(lua_State *L) /** pow(x,y) */
{
	return Bdo1(L, bc_raise);
}

static int Bdiv(lua_State *L) /** div(x,y) */
{
	const int digits = bcDigits(L);
	bc_num    a      = Bget(L, 1);
	bc_num    b      = Bget(L, 2);
	bc_num    c      = nullptr;
	if (bc_divide(a, b, &c, digits) != 0)
		return 0;
	Bnew(L, c);
	return 1;
}

static int Bmod(lua_State *L) /** mod(x,y) */
{
	bc_num a = Bget(L, 1);
	bc_num b = Bget(L, 2);
	bc_num c = nullptr;
	if (bc_modulo(a, b, &c, 0) != 0)
		return 0;
	Bnew(L, c);
	return 1;
}

static int Bdivmod(lua_State *L) /** divmod(x,y) */
{
	bc_num a = Bget(L, 1);
	bc_num b = Bget(L, 2);
	bc_num q = nullptr;
	bc_num r = nullptr;
	if (bc_divmod(a, b, &q, &r, 0) != 0)
		return 0;
	Bnew(L, q);
	Bnew(L, r);
	return 2;
}

static int Bgc(lua_State *L)
{
	bc_num x = Bget(L, 1);
	bc_free_num(&x);
	lua_pushnil(L);
	lua_setmetatable(L, 1);
	return 0;
}

static int Bneg(lua_State *L) /** neg(x) */
{
	const int digits = bcDigits(L);
	bc_num    a      = bc_zero;
	bc_num    b      = Bget(L, 1);
	bc_num    c      = nullptr;
	bc_sub(a, b, &c, digits);
	Bnew(L, c);
	return 1;
}

static int Btrunc(lua_State *L) /** trunc(x,[n]) */
{
	bc_num a = Bget(L, 1);
	bc_num c = nullptr;
	bc_divide(a, bc_one, &c, static_cast<int>(luaL_optinteger(L, 2, 0)));
	Bnew(L, c);
	return 1;
}

static int Bpowmod(lua_State *L) /** powmod(x,y,m) */
{
	bc_num a = Bget(L, 1);
	bc_num k = Bget(L, 2);
	bc_num m = Bget(L, 3);
	bc_num c = nullptr;
	if (bc_raisemod(a, k, m, &c, 0) != 0)
		return 0;
	Bnew(L, c);
	return 1;
}

static int Bsqrt(lua_State *L) /** sqrt(x) */
{
	const int digits = bcDigits(L);
	bc_num    a      = Bget(L, 1);
	bc_num    b      = bc_zero;
	bc_num    c      = nullptr;
	bc_add(a, b, &c, digits); /* bc_sqrt works inplace! */
	if (bc_sqrt(&c, digits) == 0)
		return 0;
	Bnew(L, c);
	return 1;
}

static const luaL_Reg R[] = {
    {"__add",      Badd     }, /** __add(x,y) */
    {"__div",      Bdiv     }, /** __div(x,y) */
    {"__eq",       Beq      }, /** __eq(x,y) */
    {"__gc",       Bgc      },
    {"__lt",       Blt      }, /** __lt(x,y) */
    {"__mod",      Bmod     }, /** __mod(x,y) */
    {"__mul",      Bmul     }, /** __mul(x,y) */
    {"__pow",      Bpow     }, /** __pow(x,y) */
    {"__sub",      Bsub     }, /** __sub(x,y) */
    {"__tostring", Btostring}, /** __tostring(x) */
    {"__unm",      Bneg     }, /** __unm(x) */
    {"add",        Badd     },
    {"compare",    Bcompare },
    {"digits",     Bdigits  },
    {"div",        Bdiv     },
    {"divmod",     Bdivmod  },
    {"isneg",      Bisneg   },
    {"iszero",     Biszero  },
    {"mod",        Bmod     },
    {"mul",        Bmul     },
    {"neg",        Bneg     },
    {"number",     Bnumber  },
    {"pow",        Bpow     },
    {"powmod",     Bpowmod  },
    {"sqrt",       Bsqrt    },
    {"sub",        Bsub     },
    {"tonumber",   Btonumber},
    {"tostring",   Btostring},
    {"trunc",      Btrunc   },
    {nullptr,      nullptr  }
};

extern "C"
{
	LUALIB_API int luaopen_bc(lua_State *L)
	{
		// bc_init_numbers();      // done once in MUSHclient.cpp

		luaL_newmetatable(L, MYTYPE);
		luaL_setfuncs(L, R, 0);
		lua_pushvalue(L, -1);
		lua_setglobal(L, MYNAME);
		lua_pushliteral(L, "version"); /** version */
		lua_pushliteral(L, MYVERSION);
		lua_settable(L, -3);
		lua_pushliteral(L, "__index");
		lua_pushvalue(L, -2);
		lua_settable(L, -3);
		return 1;
	}
}
