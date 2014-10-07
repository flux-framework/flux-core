#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include "veb.h"
#include "veb_mach.c"

/*
Copyright (c) 2010 Jani Lahtinen <jani.lahtinen8@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

static uint
bytes(uint x)
{
	return x/8+(x%8>0);
}

static uint
zeros(uint k)
{
	return ~0<<k;
}

static uint
ones(uint k)
{
	return ~zeros(k);
}

static uint
ipow(uint k)
{
	return 1<<k;
}

static uint
lowbits(uint x, uint k)
{
	return x&ones(k);
}

static uint
highbits(uint x, uint k)
{
	return x>>k;
}

static uint
decode(uchar D[], uint b)
{
	uint x = 0;
	int i;

	for (i = 0; i < b; ++i)
		x |= D[i]<<8*i;
	return x;
}

static void
encode(uchar D[], uint b, uint x)
{
	int i;
	for (i = 0; i < b; ++i)
		D[i] = (x>>8*i)&0xff;
}

static void
set(uchar D[], uint x)
{
	D[x/8] |= (1<<x%8);
}

static void
unset(uchar D[], uint x)
{
	D[x/8] &= ~(1<<x%8);
}

static uint
low(Veb T)
{
	if (T.M <= WORD) {
		uint x = decode(T.D,bytes(T.M));
		if (x == 0)
			return T.M;
		return ctz(x);
	}
	return decode(T.D,bytes(T.k));
}

static void
setlow(Veb T, uint x)
{
	if (T.M <= WORD)
		set(T.D,x);
	else
		encode(T.D,bytes(T.k),x);
}

static uint
high(Veb T)
{
	if (T.M <= WORD) {
		uint x = decode(T.D,bytes(T.M));
		if (x == 0)
			return T.M;
		return fls(x)-1;
	}
	return decode(T.D+bytes(T.k),bytes(T.k));
}

static void
sethigh(Veb T, uint x)
{
	if (T.M <= WORD)
		set(T.D,x);
	else
		encode(T.D+bytes(T.k),bytes(T.k),x);
}

uint
vebsize(uint M)
{
	if (M <= WORD)
		return bytes(M);
	uint k = fls(M-1);
	uint m = highbits(M-1,k/2)+1;
	uint n = ipow(k/2);
	return 2*bytes(k)+vebsize(m)+(m-1)*vebsize(n)+vebsize(M-(m-1)*n);
}

static Veb
aux(Veb S)
{
	Veb T;
	T.k = S.k-S.k/2;
	T.D = S.D+2*bytes(S.k);
	T.M = highbits(S.M-1,S.k/2)+1;
	return T;
}

static Veb
branch(Veb S, uint i)
{
	Veb T;
	uint k = S.k/2;
	uint m = highbits(S.M-1,k)+1;
	uint n = ipow(k);
	if (i < m-1) {
		T.M = n;
		T.k = k;
	} else {
		T.M = S.M-(m-1)*n;
		T.k = fls(T.M-1);
	}
	T.D = S.D+2*bytes(S.k)+vebsize(m)+i*vebsize(n);
	return T;
}

static int
empty(Veb T)
{
	if (T.M <= WORD)
		return decode(T.D,bytes(T.M))==0;
	if (low(T) <= high(T))
		return 0;
	return 1;
}

static void
mkempty(Veb T)
{
	int i;
	if (T.M <= WORD) {
		encode(T.D,bytes(T.M),0);
		return;
	}
	setlow(T,1);
	sethigh(T,0);
	mkempty(aux(T));
	uint m = highbits(T.M-1,T.k/2)+1;
	for (i = 0; i < m; ++i)
		mkempty(branch(T,i));
}

static void
mkfull(Veb T)
{
	int i;
	if (T.M <= WORD) {
		encode(T.D,bytes(T.M),ones(T.M));
		return;
	}
	setlow(T,0);
	sethigh(T,T.M-1);
	mkfull(aux(T));
	uint m = highbits(T.M-1,T.k/2)+1;
	for (i = 0; i < m; ++i) {
		Veb B = branch(T,i);
		mkfull(B);
		if (i == 0)
			vebdel(B,0);
		if (i == m-1)
			vebdel(B,lowbits(T.M-1,T.k/2));
	}
}

Veb
vebnew(uint M, int full)
{
	Veb T;
	T.k = fls(M-1);
	T.D = malloc(vebsize(M));
	if (T.D) {
		T.M = M;
		if (full)
			mkfull(T);
		else
			mkempty(T);
	}
	return T;
}

void
vebput(Veb T, uint x)
{
	if (x >= T.M)
		return;
	if (T.M <= WORD) {
		set(T.D,x);
		return;
	}
	if (empty(T)) {
		setlow(T,x);
		sethigh(T,x);
		return;
	}
	uint lo = low(T);
	uint hi = high(T);
	if (x == lo || x == hi)
		return;
	if (x < lo) {
		setlow(T,x);
		if (lo == hi)
			return;
		x = lo;
	} else if (x > hi) {
		sethigh(T,x);
		if (lo == hi)
			return;
		x = hi;
	}
	uint i = highbits(x,T.k/2);
	uint j = lowbits(x,T.k/2);
	Veb B = branch(T,i);
	vebput(B,j);
	if (low(B) == high(B))
		vebput(aux(T),i);
}

void
vebdel(Veb T, uint x)
{
	if (empty(T) || x >= T.M)
		return;
	if (T.M <= WORD) {
		unset(T.D,x);
		return;
	}
	uint lo = low(T);
	uint hi = high(T);
	if (x < lo || x > hi)
		return;
	if (lo == hi && x == lo) {
		sethigh(T,0);
		setlow(T,1);
		return;
	}
	uint i,j;
	Veb B, A = aux(T);
	if (x == lo) {
		if (empty(A)) {
			setlow(T,hi);
			return;
		} else {
			i = low(A);
			B = branch(T,i);
			j = low(B);
			setlow(T,i*ipow(T.k/2)+j);
		}
	} else if (x == hi) {
		if (empty(A)) {
			sethigh(T,lo);
			return;
		} else {
			i = high(A);
			B = branch(T,i);
			j = high(B);
			sethigh(T,i*ipow(T.k/2)+j);
		}
	} else {
		i = highbits(x,T.k/2);
		j = lowbits(x,T.k/2);
		B = branch(T,i);
	}
	vebdel(B,j);
	if (empty(B))
		vebdel(A,i);
}

uint
vebsucc(Veb T, uint x)
{
	uint hi = high(T);
	if (empty(T) || x > hi)
		return T.M;
	if (T.M <= WORD) {
		uint y = decode(T.D,bytes(T.M));
		y &= zeros(x);
		if (y > 0)
			return ctz(y);
		return T.M;
	}
	uint lo = low(T);
	if (x <= lo)
		return lo;
	Veb A = aux(T);
	if (empty(A) || x == hi)
		return hi;
	uint i = highbits(x,T.k/2);
	uint j = lowbits(x,T.k/2);
	Veb B = branch(T,i);
	if (!empty(B) && j <= high(B))
		return i*ipow(T.k/2)+vebsucc(B,j);
	i = vebsucc(A,i+1);
	if (i == A.M)
		return hi;
	B = branch(T,i);
	return i*ipow(T.k/2)+low(B);
}

uint
vebpred(Veb T, uint x)
{
	uint lo = low(T);
	if (empty(T) || x < lo || x >= T.M)
		return T.M;
	if (T.M <= WORD) {
		uint y = decode(T.D,bytes(T.M));
		y &= ones(x+1);
		if (y > 0)
			return fls(y)-1;
		return T.M;
	}
	uint hi = high(T);
	if (x >= hi)
		return hi;
	Veb A = aux(T);
	if (empty(A) || x == lo)
		return lo;
	uint i = highbits(x,T.k/2);
	uint j = lowbits(x,T.k/2);
	Veb B = branch(T,i);
	if (!empty(B) && j >= low(B))
		return i*ipow(T.k/2)+vebpred(B,j);
	i = vebpred(A,i-1);
	if (i == A.M)
		return lo;
	B = branch(T,i);
	return i*ipow(T.k/2)+high(B);
}
