/*
 * Fast implementation of greatest common divisor using the binary algorithm.
 * Copyright (c) 2007 Nicholas Kain
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/* computes gcd using binary algorithm */
static int gcd(int a, int b)
{
	int s,d;

	if (!a || !b)
		return a | b;

	for (s=0; ((a|b)&1) == 0; ++s) {
		a >>= 1;
		b >>= 1;
	}

	while ((a&1) == 0)
		a >>= 1;
	
	do {
		while ((b&1) == 0) {
			b >>= 1;
		}
		if (a<b) {
			b -= a;
		} else {
			d = a-b;
			a = b;
			b = d;
		}
		b >>= 1;
	} while (b);

	return a << s;
}

