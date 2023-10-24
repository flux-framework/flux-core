
#define WORD \
	sizeof(uint)*8

static uint
clz(uint x)
{
	return __builtin_clz(x);
}

static uint
ctz(uint x)
{
	return __builtin_ctz(x);
}

static uint
fls(uint x)
{
	return WORD-clz(x);
}
