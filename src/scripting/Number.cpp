/*
 * QMud Project
 * Copyright (c) 2026 Panagiotis Kalogiratos (Nodens)
 *
 * File: Number.cpp
 * Role: Arbitrary-precision number implementation used by the embedded bc engine in scripting workflows.
 */

/*
 * this file is originally from GNU bc-1.06. it was trimmed down by lhf to fix
 * a memory leak in bc_raisemod and to remove the free list, as in php bcmath.
 */

/* number.c: Implements arbitrary precision numbers. */
/*
    Copyright (C) 1991, 1992, 1993, 1994, 1997, 2000 Free Software Foundation, Inc.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License , or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; see the file COPYING.  If not, write to:

      The Free Software Foundation, Inc.
      59 Temple Place, Suite 330
      Boston, MA 02111-1307 USA.


    You may contact the author by:
       e-mail:  philnelson@acm.org
      us-mail:  Philip A. Nelson
                Computer Science Department, 9062
                Western Washington University
                Bellingham, WA 98226-9062

*************************************************************************/

#include "Number.h"
#include "BcConfig.h"
#include <cassert>
#include <cctype> /* Prototypes needed for external utility routines. */
#include <cstdlib>
#include <cstring>

/* Storage used for special numbers. */
QMUD_BC_THREAD_LOCAL bc_num bc_zero;
QMUD_BC_THREAD_LOCAL bc_num bc_one;
QMUD_BC_THREAD_LOCAL bc_num bc_two;

/* new_num allocates a number and sets fields to known values. */

bc_num                      bc_new_num(int length, int scale)
{
	auto temp = static_cast<bc_num>(malloc(sizeof(bc_struct)));
	if (temp == nullptr)
		bc_out_of_memory();
	temp->n_sign  = PLUS;
	temp->n_len   = length;
	temp->n_scale = scale;
	temp->n_refs  = 1;
	temp->n_ptr   = static_cast<char *>(malloc(length + scale));
	if (temp->n_ptr == nullptr)
		bc_out_of_memory();
	temp->n_value = temp->n_ptr;
	memset(temp->n_ptr, 0, length + scale);
	return temp;
}

/* "Frees" a bc_num NUM.  Actually decreases reference count and only
   frees the storage if reference count is zero. */

void bc_free_num(bc_num *num)
{
	if (*num == nullptr)
		return;
	if (*num == bc_zero || *num == bc_one || *num == bc_two)
	{
		*num = nullptr;
		return;
	}
	(*num)->n_refs--;
	if ((*num)->n_refs == 0)
	{
		if ((*num)->n_ptr)
			free((*num)->n_ptr);
		free(*num);
	}
	*num = nullptr;
}

/* Intitialize the number package! */

void bc_init_numbers()
{
	if (bc_zero != nullptr && bc_one != nullptr && bc_two != nullptr)
		return;

	bc_zero            = bc_new_num(1, 0);
	bc_one             = bc_new_num(1, 0);
	bc_one->n_value[0] = 1;
	bc_two             = bc_new_num(1, 0);
	bc_two->n_value[0] = 2;
}

static void bc_ensure_numbers_initialized()
{
	if (bc_zero == nullptr || bc_one == nullptr || bc_two == nullptr)
		bc_init_numbers();
}

/* Make a copy of a number!  Just increments the reference count! */

bc_num bc_copy_num(bc_num num)
{
	bc_ensure_numbers_initialized();
	if (num == bc_zero || num == bc_one || num == bc_two)
		return num;
	num->n_refs++;
	return num;
}

/* Initialize a number NUM by making it a copy of zero. */

void bc_init_num(bc_num *num)
{
	*num = bc_copy_num(bc_zero);
}

/* For many things, we may have leading zeros in a number NUM.
   bc_rm_leading_zeros_internal just moves the data "value" pointer to the
   correct place and adjusts the length. */

static void bc_rm_leading_zeros_internal(bc_num num)
{
	/* We can move n_value to point to the first non-zero digit! */
	while (*num->n_value == 0 && num->n_len > 1)
	{
		num->n_value++;
		num->n_len--;
	}
}

/* Compare two bc numbers.  Return value is 0 if equal, -1 if N1 is less
   than N2 and +1 if N1 is greater than N2.  If USE_SIGN is false, just
   compare the magnitudes. */

static int bc_do_compare_internal(bc_num n1, bc_num n2, int use_sign)
{
	int   count = n1->n_len + MIN(n1->n_scale, n2->n_scale);
	char *n1ptr = n1->n_value;
	char *n2ptr = n2->n_value;

	/* First, compare signs. */
	if (use_sign && n1->n_sign != n2->n_sign)
	{
		if (n1->n_sign == PLUS)
			return (1); /* Positive N1 > Negative N2 */
		else
			return (-1); /* Negative N1 < Positive N1 */
	}

	/* Now compare the magnitude. */
	if (n1->n_len != n2->n_len)
	{
		if (n1->n_len > n2->n_len)
		{
			/* Magnitude of n1 > n2. */
			if (!use_sign || n1->n_sign == PLUS)
				return (1);
			else
				return (-1);
		}
		else
		{
			/* Magnitude of n1 < n2. */
			if (!use_sign || n1->n_sign == PLUS)
				return (-1);
			else
				return (1);
		}
	}

	/* If we get here, they have the same number of integer digits.
	 check the integer part and the equal length part of the fraction. */
	while ((count > 0) && (*n1ptr == *n2ptr))
	{
		n1ptr++;
		n2ptr++;
		count--;
	}
	if (count != 0)
	{
		if (*n1ptr > *n2ptr)
		{
			/* Magnitude of n1 > n2. */
			if (!use_sign || n1->n_sign == PLUS)
				return (1);
			else
				return (-1);
		}
		else
		{
			/* Magnitude of n1 < n2. */
			if (!use_sign || n1->n_sign == PLUS)
				return (-1);
			else
				return (1);
		}
	}

	/* They are equal up to the last part of the equal part of the fraction. */
	if (n1->n_scale != n2->n_scale)
	{
		if (n1->n_scale > n2->n_scale)
		{
			for (count = n1->n_scale - n2->n_scale; count > 0; count--)
				if (*n1ptr++ != 0)
				{
					/* Magnitude of n1 > n2. */
					if (!use_sign || n1->n_sign == PLUS)
						return (1);
					else
						return (-1);
				}
		}
		else
		{
			for (count = n2->n_scale - n1->n_scale; count > 0; count--)
				if (*n2ptr++ != 0)
				{
					/* Magnitude of n1 < n2. */
					if (!use_sign || n1->n_sign == PLUS)
						return (-1);
					else
						return (1);
				}
		}
	}

	/* They must be equal! */
	return (0);
}

/* This is the "user callable" routine to compare numbers N1 and N2. */

int bc_compare(bc_num n1, bc_num n2)
{
	return bc_do_compare_internal(n1, n2, TRUE);
}

/* In some places we need to check if the number is negative. */

char bc_is_neg(bc_num num)
{
	return static_cast<char>(num->n_sign == MINUS);
}

/* In some places we need to check if the number NUM is zero. */

char bc_is_zero(bc_num num)
{
	bc_ensure_numbers_initialized();
	int   count = num->n_len + num->n_scale;
	char *nptr  = num->n_value;

	/* Quick check. */
	if (num == bc_zero)
		return TRUE;

	/* The check */
	while ((count > 0) && (*nptr++ == 0))
		count--;

	if (count != 0)
		return FALSE;
	else
		return TRUE;
}

/* In some places we need to check if the number NUM is almost zero.
   Specifically, all but the last digit is 0 and the last digit is 1.
   Last digit is defined by scale. */

char bc_is_near_zero(bc_num num, int scale)
{
	/* Error checking */
	if (scale > num->n_scale)
		scale = num->n_scale;

	int   count = num->n_len + scale;
	char *nptr  = num->n_value;

	/* The check */
	while ((count > 0) && (*nptr++ == 0))
		count--;

	if (count != 0 && (count != 1 || *--nptr != 1))
		return FALSE;
	else
		return TRUE;
}

/* Perform addition: N1 is added to N2 and the value is
   returned.  The signs of N1 and N2 are ignored.
   SCALE_MIN is to set the minimum scale of the result. */

static bc_num bc_do_add_internal(bc_num n1, bc_num n2, int scale_min)
{
	const int sum_scale  = MAX(n1->n_scale, n2->n_scale);
	const int sum_digits = MAX(n1->n_len, n2->n_len) + 1;
	bc_num    sum        = bc_new_num(sum_digits, MAX(sum_scale, scale_min));

	/* Zero extra digits made by scale_min. */
	if (scale_min > sum_scale)
	{
		char *sumptr = sum->n_value + sum_scale + sum_digits;
		for (int count = scale_min - sum_scale; count > 0; count--)
			*sumptr++ = 0;
	}

	/* Start with the fraction part.  Initialize the pointers. */
	int   n1bytes = n1->n_scale;
	int   n2bytes = n2->n_scale;
	char *n1ptr   = n1->n_value + n1->n_len + n1bytes - 1;
	char *n2ptr   = n2->n_value + n2->n_len + n2bytes - 1;
	char *sumptr  = sum->n_value + sum_scale + sum_digits - 1;
	int   carry   = 0;

	/* Add the fraction part.  First copy the longer fraction.*/
	if (n1bytes != n2bytes)
	{
		if (n1bytes > n2bytes)
			while (n1bytes > n2bytes)
			{
				*sumptr-- = *n1ptr--;
				n1bytes--;
			}
		else
			while (n2bytes > n1bytes)
			{
				*sumptr-- = *n2ptr--;
				n2bytes--;
			}
	}

	/* Now add the remaining fraction part and equal size integer parts. */
	n1bytes += n1->n_len;
	n2bytes += n2->n_len;
	while ((n1bytes > 0) && (n2bytes > 0))
	{
		*sumptr = static_cast<char>(*n1ptr-- + *n2ptr-- + carry);
		if (*sumptr > (BASE - 1))
		{
			carry = 1;
			*sumptr -= BASE;
		}
		else
			carry = 0;
		sumptr--;
		n1bytes--;
		n2bytes--;
	}

	/* Now add carry the longer integer part. */
	if (n1bytes == 0)
	{
		n1bytes = n2bytes;
		n1ptr   = n2ptr;
	}
	while (n1bytes-- > 0)
	{
		*sumptr = static_cast<char>(*n1ptr-- + carry);
		if (*sumptr > (BASE - 1))
		{
			carry = 1;
			*sumptr -= BASE;
		}
		else
			carry = 0;
		sumptr--;
	}

	/* Set final carry. */
	if (carry == 1)
		*sumptr += 1;

	/* Adjust sum and return. */
	bc_rm_leading_zeros_internal(sum);
	return sum;
}

/* Perform subtraction: N2 is subtracted from N1 and the value is
   returned.  The signs of N1 and N2 are ignored.  Also, N1 is
   assumed to be larger than N2.  SCALE_MIN is the minimum scale
   of the result. */

static bc_num bc_do_sub_internal(bc_num n1, bc_num n2, int scale_min)
{
	const int diff_len   = MAX(n1->n_len, n2->n_len);
	const int diff_scale = MAX(n1->n_scale, n2->n_scale);
	const int min_len    = MIN(n1->n_len, n2->n_len);
	const int min_scale  = MIN(n1->n_scale, n2->n_scale);
	bc_num    diff       = bc_new_num(diff_len, MAX(diff_scale, scale_min));
	char     *n1ptr      = n1->n_value + n1->n_len + n1->n_scale - 1;
	char     *n2ptr      = n2->n_value + n2->n_len + n2->n_scale - 1;
	char     *diffptr    = diff->n_value + diff_len + diff_scale - 1;
	int       borrow     = 0;
	int       count;
	int       val;

	/* Zero extra digits made by scale_min. */
	if (scale_min > diff_scale)
	{
		char *scaleptr = diff->n_value + diff_len + diff_scale;
		for (count = scale_min - diff_scale; count > 0; count--)
			*scaleptr++ = 0;
	}

	/* Take care of the longer scaled number. */
	if (n1->n_scale != min_scale)
	{
		/* n1 has the longer scale */
		for (count = n1->n_scale - min_scale; count > 0; count--)
			*diffptr-- = *n1ptr--;
	}
	else
	{
		/* n2 has the longer scale */
		for (count = n2->n_scale - min_scale; count > 0; count--)
		{
			val = -*n2ptr-- - borrow;
			if (val < 0)
			{
				val += BASE;
				borrow = 1;
			}
			else
				borrow = 0;
			*diffptr-- = static_cast<char>(val);
		}
	}

	/* Now do the equal length scale and integer parts. */
	for (count = 0; count < min_len + min_scale; count++)
	{
		val = *n1ptr-- - *n2ptr-- - borrow;
		if (val < 0)
		{
			val += BASE;
			borrow = 1;
		}
		else
			borrow = 0;
		*diffptr-- = static_cast<char>(val);
	}

	/* If n1 has more digits than n2, we now do that subtract. */
	if (diff_len != min_len)
	{
		for (count = diff_len - min_len; count > 0; count--)
		{
			val = *n1ptr-- - borrow;
			if (val < 0)
			{
				val += BASE;
				borrow = 1;
			}
			else
				borrow = 0;
			*diffptr-- = static_cast<char>(val);
		}
	}

	/* Clean up and return. */
	bc_rm_leading_zeros_internal(diff);
	return diff;
}

/* Here is the full subtract routine that takes care of negative numbers.
   N2 is subtracted from N1 and the result placed in RESULT.  SCALE_MIN
   is the minimum scale for the result. */

void bc_sub(bc_num n1, bc_num n2, bc_num *result, int scale_min)
{
	bc_num diff;

	if (n1->n_sign != n2->n_sign)
	{
		diff         = bc_do_add_internal(n1, n2, scale_min);
		diff->n_sign = n1->n_sign;
	}
	else
	{
		/* subtraction must be done. */
		/* Compare magnitudes. */
		const int cmp_res = bc_do_compare_internal(n1, n2, FALSE);
		if (cmp_res < 0)
		{
			/* n1 is less than n2, subtract n1 from n2. */
			diff         = bc_do_sub_internal(n2, n1, scale_min);
			diff->n_sign = (n2->n_sign == PLUS ? MINUS : PLUS);
		}
		else if (cmp_res == 0)
		{
			/* They are equal! return zero! */
			const int res_scale = MAX(scale_min, MAX(n1->n_scale, n2->n_scale));
			diff                = bc_new_num(1, res_scale);
			memset(diff->n_value, 0, res_scale + 1);
		}
		else
		{
			/* n2 is less than n1, subtract n2 from n1. */
			diff         = bc_do_sub_internal(n1, n2, scale_min);
			diff->n_sign = n1->n_sign;
		}
	}

	/* Clean up and return. */
	bc_free_num(result);
	*result = diff;
}

/* Here is the full add routine that takes care of negative numbers.
   N1 is added to N2 and the result placed into RESULT.  SCALE_MIN
   is the minimum scale for the result. */

void bc_add(bc_num n1, bc_num n2, bc_num *result, int scale_min)
{
	bc_num sum;

	if (n1->n_sign == n2->n_sign)
	{
		sum         = bc_do_add_internal(n1, n2, scale_min);
		sum->n_sign = n1->n_sign;
	}
	else
	{
		/* subtraction must be done. */
		const int cmp_res = bc_do_compare_internal(n1, n2, FALSE); /* Compare magnitudes. */
		if (cmp_res < 0)
		{
			/* n1 is less than n2, subtract n1 from n2. */
			sum         = bc_do_sub_internal(n2, n1, scale_min);
			sum->n_sign = n2->n_sign;
		}
		else if (cmp_res == 0)
		{
			/* They are equal! return zero with the correct scale! */
			const int res_scale = MAX(scale_min, MAX(n1->n_scale, n2->n_scale));
			sum                 = bc_new_num(1, res_scale);
			memset(sum->n_value, 0, res_scale + 1);
		}
		else
		{
			/* n2 is less than n1, subtract n2 from n1. */
			sum         = bc_do_sub_internal(n1, n2, scale_min);
			sum->n_sign = n1->n_sign;
		}
	}

	/* Clean up and return. */
	bc_free_num(result);
	*result = sum;
}

/* Recursive vs non-recursive multiply crossover ranges. */
#if defined(MULDIGITS)
#include "muldigits.h"
#else
#define MUL_BASE_DIGITS 80
#endif

int mul_base_digits = MUL_BASE_DIGITS;
#define MUL_SMALL_DIGITS (mul_base_digits / 4)

/* Multiply utility routines */

static bc_num new_sub_num(int length, char *value)
{
	auto temp = static_cast<bc_num>(malloc(sizeof(bc_struct)));
	if (temp == nullptr)
		bc_out_of_memory();
	temp->n_sign  = PLUS;
	temp->n_len   = length;
	temp->n_scale = 0;
	temp->n_refs  = 1;
	temp->n_ptr   = nullptr;
	temp->n_value = value;
	return temp;
}

static void bc_simp_mul_internal(bc_num n1, int n1len, bc_num n2, int n2len, bc_num *prod)
{
	const int prodlen = n1len + n2len + 1;
	int       sum     = 0;

	*prod = bc_new_num(prodlen, 0);

	char *n1end = n1->n_value + n1len - 1; /* To the end of n1 and n2. */
	char *n2end = n2->n_value + n2len - 1;
	char *pvptr = (*prod)->n_value + prodlen - 1;

	/* Here is the loop... */
	for (int indx = 0; indx < prodlen - 1; indx++)
	{
		char *n1ptr = n1end - MAX(0, indx - n2len + 1);
		char *n2ptr = n2end - MIN(indx, n2len - 1);
		while ((n1ptr >= n1->n_value) && (n2ptr <= n2end))
			sum += *n1ptr-- * *n2ptr++;
		*pvptr-- = static_cast<char>(sum % BASE);
		sum      = sum / BASE;
	}
	*pvptr = static_cast<char>(sum);
}

/* A special adder/subtractor for the recursive divide and conquer
   multiply algorithm.  Note: if sub is called, accum must
   be larger that what is being subtracted.  Also, accum and val
   must have n_scale = 0.  (e.g. they must look like integers. *) */
static void bc_shift_addsub_internal(bc_num accum, bc_num val, int shift, int sub)
{
	int count = val->n_len;
	if (val->n_value[0] == 0)
		count--;
	assert(accum->n_len + accum->n_scale >= shift + count);

	/* Set up pointers and others */
	auto *accp  = reinterpret_cast<signed char *>(accum->n_value + accum->n_len + accum->n_scale - shift - 1);
	auto *valp  = reinterpret_cast<signed char *>(val->n_value + val->n_len - 1);
	int   carry = 0;

	if (sub)
	{
		/* Subtraction, carry is really borrow. */
		while (count--)
		{
			const int updated = static_cast<int>(*accp) - static_cast<int>(*valp--) - carry;
			*accp             = static_cast<signed char>(updated);
			if (*accp < 0)
			{
				carry = 1;
				*accp-- += BASE;
			}
			else
			{
				carry = 0;
				accp--;
			}
		}
		while (carry)
		{
			const int updated = static_cast<int>(*accp) - carry;
			*accp             = static_cast<signed char>(updated);
			if (*accp < 0)
				*accp-- += BASE;
			else
				carry = 0;
		}
	}
	else
	{
		/* Addition */
		while (count--)
		{
			const int updated = static_cast<int>(*accp) + static_cast<int>(*valp--) + carry;
			*accp             = static_cast<signed char>(updated);
			if (*accp > (BASE - 1))
			{
				carry = 1;
				*accp-- -= BASE;
			}
			else
			{
				carry = 0;
				accp--;
			}
		}
		while (carry)
		{
			const int updated = static_cast<int>(*accp) + carry;
			*accp             = static_cast<signed char>(updated);
			if (*accp > (BASE - 1))
				*accp-- -= BASE;
			else
				carry = 0;
		}
	}
}

/* Recursive divide and conquer multiply algorithm.
   Based on
   Let u = u0 + u1*(b^n)
   Let v = v0 + v1*(b^n)
   Then uv = (B^2n+B^n)*u1*v1 + B^n*(u1-u0)*(v0-v1) + (B^n+1)*u0*v0

   B is the base of storage, number of digits in u1,u0 close to equal.
*/
static void bc_rec_mul_internal(bc_num u, int ulen, bc_num v, int vlen, bc_num *prod)
{
	bc_num    u0, u1, v0, v1;
	bc_num    m1, m2, m3, d1, d2;
	const int n = (MAX(ulen, vlen) + 1) / 2;

	/* Base case? */
	if ((ulen + vlen) < mul_base_digits || ulen < MUL_SMALL_DIGITS || vlen < MUL_SMALL_DIGITS)
	{
		bc_simp_mul_internal(u, ulen, v, vlen, prod);
		return;
	}

	/* Calculate n -- the u and v split point in digits. */

	/* Split u and v. */
	if (ulen < n)
	{
		u1 = bc_copy_num(bc_zero);
		u0 = new_sub_num(ulen, u->n_value);
	}
	else
	{
		u1 = new_sub_num(ulen - n, u->n_value);
		u0 = new_sub_num(n, u->n_value + ulen - n);
	}
	if (vlen < n)
	{
		v1 = bc_copy_num(bc_zero);
		v0 = new_sub_num(vlen, v->n_value);
	}
	else
	{
		v1 = new_sub_num(vlen - n, v->n_value);
		v0 = new_sub_num(n, v->n_value + vlen - n);
	}
	bc_rm_leading_zeros_internal(u1);
	bc_rm_leading_zeros_internal(u0);
	bc_rm_leading_zeros_internal(v1);
	bc_rm_leading_zeros_internal(v0);

	const int m1zero = bc_is_zero(u1) || bc_is_zero(v1);

	/* Calculate sub results ... */

	bc_init_num(&d1);
	bc_init_num(&d2);
	bc_sub(u1, u0, &d1, 0);
	const int d1len = d1->n_len;
	bc_sub(v0, v1, &d2, 0);
	const int d2len = d2->n_len;

	/* Do recursive multiplies and shifted adds. */
	if (m1zero)
		m1 = bc_copy_num(bc_zero);
	else
		bc_rec_mul_internal(u1, u1->n_len, v1, v1->n_len, &m1);

	if (bc_is_zero(d1) || bc_is_zero(d2))
		m2 = bc_copy_num(bc_zero);
	else
		bc_rec_mul_internal(d1, d1len, d2, d2len, &m2);

	if (bc_is_zero(u0) || bc_is_zero(v0))
		m3 = bc_copy_num(bc_zero);
	else
		bc_rec_mul_internal(u0, u0->n_len, v0, v0->n_len, &m3);

	/* Initialize product */
	const int prodlen = ulen + vlen + 1;
	*prod             = bc_new_num(prodlen, 0);

	if (!m1zero)
	{
		bc_shift_addsub_internal(*prod, m1, 2 * n, 0);
		bc_shift_addsub_internal(*prod, m1, n, 0);
	}
	bc_shift_addsub_internal(*prod, m3, n, 0);
	bc_shift_addsub_internal(*prod, m3, 0, 0);
	bc_shift_addsub_internal(*prod, m2, n, d1->n_sign != d2->n_sign);

	/* Now clean up! */
	bc_free_num(&u1);
	bc_free_num(&u0);
	bc_free_num(&v1);
	bc_free_num(&m1);
	bc_free_num(&v0);
	bc_free_num(&m2);
	bc_free_num(&m3);
	bc_free_num(&d1);
	bc_free_num(&d2);
}

/* The multiply routine.  N2 times N1 is put int PROD with the scale of
   the result being MIN(N2 scale+N1 scale, MAX (SCALE, N2 scale, N1 scale)).
   */

void bc_multiply(bc_num n1, bc_num n2, bc_num *prod, int scale)
{
	const int len1       = n1->n_len + n1->n_scale;
	const int len2       = n2->n_len + n2->n_scale;
	const int full_scale = n1->n_scale + n2->n_scale;
	const int prod_scale = MIN(full_scale, MAX(scale, MAX(n1->n_scale, n2->n_scale)));
	bc_num    pval;

	/* Do the multiply */
	bc_rec_mul_internal(n1, len1, n2, len2, &pval);

	/* Assign to prod and clean up the number. */
	pval->n_sign  = (n1->n_sign == n2->n_sign ? PLUS : MINUS);
	pval->n_value = pval->n_ptr;
	pval->n_len   = len2 + len1 + 1 - full_scale;
	pval->n_scale = prod_scale;
	bc_rm_leading_zeros_internal(pval);
	if (bc_is_zero(pval))
		pval->n_sign = PLUS;
	bc_free_num(prod);
	*prod = pval;
}

/* Some utility routines for the divide:  First a one digit multiply.
   NUM (with SIZE digits) is multiplied by DIGIT and the result is
   placed into RESULT.  It is written so that NUM and RESULT can be
   the same pointers.  */

static void bc_onemult(unsigned char *num, int size, int digit, unsigned char *result)
{
	if (digit == 0)
		memset(result, 0, size);
	else
	{
		if (digit == 1)
			memcpy(result, num, size);
		else
		{
			/* Initialize */
			auto          *nptr  = const_cast<unsigned char *>(num + size - 1);
			unsigned char *rptr  = result + size - 1;
			int            carry = 0;

			while (size-- > 0)
			{
				const int value = *nptr-- * digit + carry;
				*rptr--         = value % BASE;
				carry           = value / BASE;
			}

			if (carry != 0)
				*rptr = carry;
		}
	}
}

/* The full division routine. This computes N1 / N2.  It returns
   0 if the division is ok and the result is in QUOT.  The number of
   digits after the decimal point is SCALE. It returns -1 if division
   by zero is tried.  The algorithm is found in Knuth Vol 2. p237. */

int bc_divide(bc_num n1, bc_num n2, bc_num *quot, int scale)
{
	bc_num qval;
	int    qdigits;
	char   zero;

	/* Test for divide by zero. */
	if (bc_is_zero(n2))
		return -1;

	/* Test for divide by 1.  If it is we must truncate. */
	if (n2->n_scale == 0)
	{
		if (n2->n_len == 1 && *n2->n_value == 1)
		{
			qval         = bc_new_num(n1->n_len, scale);
			qval->n_sign = (n1->n_sign == n2->n_sign ? PLUS : MINUS);
			memset(&qval->n_value[n1->n_len], 0, scale);
			memcpy(qval->n_value, n1->n_value, n1->n_len + MIN(n1->n_scale, scale));
			bc_free_num(quot);
			*quot = qval;
		}
	}

	/* Set up the divide.  Move the decimal point on n1 by n2's scale.
	 Remember, zeros on the end of num2 are wasted effort for dividing. */
	int            scale2 = n2->n_scale;
	unsigned char *n2ptr  = reinterpret_cast<unsigned char *>(n2->n_value) + n2->n_len + scale2 - 1;
	while ((scale2 > 0) && (*n2ptr-- == 0))
		scale2--;

	const int len1   = n1->n_len + scale2;
	const int scale1 = n1->n_scale - scale2;
	const int extra  = (scale1 < scale) ? (scale - scale1) : 0;
	auto     *num1   = static_cast<unsigned char *>(malloc(n1->n_len + n1->n_scale + extra + 2));
	if (num1 == nullptr)
		bc_out_of_memory();
	memset(num1, 0, n1->n_len + n1->n_scale + extra + 2);
	memcpy(num1 + 1, n1->n_value, n1->n_len + n1->n_scale);

	int   len2 = n2->n_len + scale2;
	auto *num2 = static_cast<unsigned char *>(malloc(len2 + 1));
	if (num2 == nullptr)
		bc_out_of_memory();
	memcpy(num2, n2->n_value, len2);
	*(num2 + len2) = 0;
	n2ptr          = num2;
	while (*n2ptr == 0)
	{
		n2ptr++;
		len2--;
	}

	/* Calculate the number of quotient digits. */
	if (len2 > len1 + scale)
	{
		qdigits = scale + 1;
		zero    = TRUE;
	}
	else
	{
		zero = FALSE;
		if (len2 > len1)
			qdigits = scale + 1; /* One for the zero integer part. */
		else
			qdigits = len1 - len2 + scale + 1;
	}

	/* Allocate and zero the storage for the quotient. */
	qval = bc_new_num(qdigits - scale, scale);
	memset(qval->n_value, 0, qdigits);

	/* Allocate storage for the temporary storage mval. */
	auto *mval = static_cast<unsigned char *>(malloc(len2 + 1));
	if (mval == nullptr)
		bc_out_of_memory();

	/* Now for the full divide algorithm. */
	if (!zero)
	{
		/* Normalize */
		const int norm = 10 / (static_cast<int>(*n2ptr) + 1);
		if (norm != 1)
		{
			bc_onemult(num1, len1 + scale1 + extra + 1, norm, num1);
			bc_onemult(n2ptr, len2, norm, n2ptr);
		}

		/* Initialize divide loop. */
		unsigned char *qptr = (len2 > len1) ? reinterpret_cast<unsigned char *>(qval->n_value) + len2 - len1
		                                    : reinterpret_cast<unsigned char *>(qval->n_value);

		/* Loop */
		for (int qdig = 0; qdig <= len1 + scale - len2; qdig++)
		{
			/* Calculate the quotient digit guess. */
			int qguess = (*n2ptr == num1[qdig]) ? 9 : (num1[qdig] * 10 + num1[qdig + 1]) / *n2ptr;

			/* Test qguess. */
			if (n2ptr[1] * qguess >
			    (num1[qdig] * 10 + num1[qdig + 1] - *n2ptr * qguess) * 10 + num1[qdig + 2])
			{
				qguess--;
				/* And again. */
				if (n2ptr[1] * qguess >
				    (num1[qdig] * 10 + num1[qdig + 1] - *n2ptr * qguess) * 10 + num1[qdig + 2])
					qguess--;
			}

			/* Multiply and subtract. */
			int borrow = 0;
			if (qguess != 0)
			{
				*mval = 0;
				bc_onemult(n2ptr, len2, qguess, mval + 1);
				unsigned char *ptr1 = static_cast<unsigned char *>(num1) + qdig + len2;
				unsigned char *ptr2 = static_cast<unsigned char *>(mval) + len2;
				for (int count = 0; count < len2 + 1; count++)
				{
					int digit = static_cast<int>(*ptr1) - static_cast<int>(*ptr2--) - borrow;
					if (digit < 0)
					{
						digit += 10;
						borrow = 1;
					}
					else
						borrow = 0;
					*ptr1-- = digit;
				}
			}

			/* Test for negative result. */
			if (borrow == 1)
			{
				qguess--;
				unsigned char *ptr1  = static_cast<unsigned char *>(num1) + qdig + len2;
				unsigned char *ptr2  = n2ptr + len2 - 1;
				int            carry = 0;
				for (int count = 0; count < len2; count++)
				{
					int digit = static_cast<int>(*ptr1) + static_cast<int>(*ptr2--) + carry;
					if (digit > 9)
					{
						digit -= 10;
						carry = 1;
					}
					else
						carry = 0;
					*ptr1-- = digit;
				}
				if (carry == 1)
					*ptr1 = (*ptr1 + 1) % 10;
			}

			/* We now know the quotient digit. */
			*qptr++ = qguess;
		}
	}

	/* Clean up and return the number. */
	qval->n_sign = (n1->n_sign == n2->n_sign ? PLUS : MINUS);
	if (bc_is_zero(qval))
		qval->n_sign = PLUS;
	bc_rm_leading_zeros_internal(qval);
	bc_free_num(quot);
	*quot = qval;

	/* Clean up temporary storage. */
	free(mval);
	free(num1);
	free(num2);

	return 0; /* Everything is OK. */
}

/* Division *and* modulo for numbers.  This computes both NUM1 / NUM2 and
   NUM1 % NUM2  and puts the results in QUOT and REM, except that if QUOT
   is nullptr then that store will be omitted.
 */

int bc_divmod(bc_num num1, bc_num num2, bc_num *quot, bc_num *rem, int scale)
{
	bc_num temp;

	/* Check for correct numbers. */
	if (bc_is_zero(num2))
		return -1;

	/* Calculate final scale. */
	const int rscale = MAX(num1->n_scale, num2->n_scale + scale);
	bc_init_num(&temp);

	/* Calculate it. */
	bc_divide(num1, num2, &temp, scale);
	bc_num quotient = quot ? bc_copy_num(temp) : nullptr;
	bc_multiply(temp, num2, &temp, rscale);
	bc_sub(num1, temp, rem, rscale);
	bc_free_num(&temp);

	if (quot)
	{
		bc_free_num(quot);
		*quot = quotient;
	}

	return 0; /* Everything is OK. */
}

/* Modulo for numbers.  This computes NUM1 % NUM2  and puts the
   result in RESULT.   */

int bc_modulo(bc_num num1, bc_num num2, bc_num *result, int scale)
{
	return bc_divmod(num1, num2, nullptr, result, scale);
}

/* Raise BASE to the EXPO power, reduced modulo MOD.  The result is
   placed in RESULT.  If a EXPO is not an integer,
   only the integer part is used.  */

int bc_raisemod(bc_num base, bc_num expo, bc_num mod, bc_num *result, int scale)
{
	bc_ensure_numbers_initialized();
	const int rscale = MAX(scale, base->n_scale);

	/* Check for correct numbers. */
	if (bc_is_zero(mod))
		return -1;
	if (bc_is_neg(expo))
		return -1;

	/* Set initial values.  */
	bc_num power    = bc_copy_num(base);
	bc_num exponent = bc_copy_num(expo);
	bc_num temp     = bc_copy_num(bc_one);
	bc_num parity;
	bc_init_num(&parity);

	/* Check the base for scale digits. */
	if (base->n_scale != 0)
		bc_rt_warn("non-zero scale in base");

	/* Check the exponent for scale digits. */
	if (exponent->n_scale != 0)
	{
		bc_rt_warn("non-zero scale in exponent");
		bc_divide(exponent, bc_one, &exponent, 0); /*truncate */
	}

	/* Check the modulus for scale digits. */
	if (mod->n_scale != 0)
		bc_rt_warn("non-zero scale in modulus");

	/* Do the calculation. */
	while (!bc_is_zero(exponent))
	{
		(void)bc_divmod(exponent, bc_two, &exponent, &parity, 0);
		if (!bc_is_zero(parity))
		{
			bc_multiply(temp, power, &temp, rscale);
			(void)bc_modulo(temp, mod, &temp, scale);
		}

		bc_multiply(power, power, &power, rscale);
		(void)bc_modulo(power, mod, &power, scale);
	}

	/* Assign the value. */
	bc_free_num(&power);
	bc_free_num(&exponent);
	bc_free_num(result);
	bc_free_num(&parity);
	*result = temp;
	return 0; /* Everything is OK. */
}

/* Raise NUM1 to the NUM2 power.  The result is placed in RESULT.
   Maximum exponent is LONG_MAX.  If a NUM2 is not an integer,
   only the integer part is used.  */

void bc_raise(bc_num num1, bc_num num2, bc_num *result, int scale)
{
	bc_ensure_numbers_initialized();
	int  rscale;
	char neg;

	/* Check the exponent for scale digits and convert to a long. */
	if (num2->n_scale != 0)
		bc_rt_warn("non-zero scale in exponent");
	long exponent = bc_num2long(num2);
	if (exponent == 0 && (num2->n_len > 1 || num2->n_value[0] != 0))
		bc_rt_error("exponent too large in raise");

	/* Special case if exponent is a zero. */
	if (exponent == 0)
	{
		bc_free_num(result);
		*result = bc_copy_num(bc_one);
		return;
	}

	/* Other initializations. */
	if (exponent < 0)
	{
		neg      = TRUE;
		exponent = -exponent;
		rscale   = scale;
	}
	else
	{
		neg    = FALSE;
		rscale = MIN(num1->n_scale * exponent, MAX(scale, num1->n_scale));
	}

	/* Set initial value of temp.  */
	bc_num power    = bc_copy_num(num1);
	int    pwrscale = num1->n_scale;
	while ((exponent & 1) == 0)
	{
		pwrscale = 2 * pwrscale;
		bc_multiply(power, power, &power, pwrscale);
		exponent = exponent >> 1;
	}
	bc_num temp      = bc_copy_num(power);
	int    calcscale = pwrscale;
	exponent         = exponent >> 1;

	/* Do the calculation. */
	while (exponent > 0)
	{
		pwrscale = 2 * pwrscale;
		bc_multiply(power, power, &power, pwrscale);
		if ((exponent & 1) == 1)
		{
			calcscale = pwrscale + calcscale;
			bc_multiply(temp, power, &temp, calcscale);
		}
		exponent = exponent >> 1;
	}

	/* Assign the value. */
	if (neg)
	{
		bc_divide(bc_one, temp, result, rscale);
		bc_free_num(&temp);
	}
	else
	{
		bc_free_num(result);
		*result = temp;
		if ((*result)->n_scale > rscale)
			(*result)->n_scale = rscale;
	}
	bc_free_num(&power);
}

/* Take the square root NUM and return it in NUM with SCALE digits
   after the decimal place. */

int bc_sqrt(bc_num *num, int scale)
{
	bc_ensure_numbers_initialized();
	int       cscale;
	bc_num    guess, guess1, point5, diff;

	/* Initial checks. */
	const int cmp_zero = bc_compare(*num, bc_zero);
	if (cmp_zero < 0)
		return 0; /* error */
	if (cmp_zero == 0)
	{
		bc_free_num(num);
		*num = bc_copy_num(bc_zero);
		return 1;
	}

	const int cmp_one = bc_compare(*num, bc_one);
	if (cmp_one == 0)
	{
		bc_free_num(num);
		*num = bc_copy_num(bc_one);
		return 1;
	}

	/* Initialize the variables. */
	const int rscale = MAX(scale, (*num)->n_scale);
	bc_init_num(&guess);
	bc_init_num(&guess1);
	bc_init_num(&diff);
	point5             = bc_new_num(1, 1);
	point5->n_value[1] = 5;

	/* Calculate the initial guess. */
	if (cmp_one < 0)
	{
		/* The number is between 0 and 1.  Guess should start at 1. */
		guess  = bc_copy_num(bc_one);
		cscale = (*num)->n_scale;
	}
	else
	{
		/* The number is greater than 1.  Guess should start at 10^(exp/2). */
		bc_int2num(&guess, 10);

		bc_int2num(&guess1, (*num)->n_len);
		bc_multiply(guess1, point5, &guess1, 0);
		guess1->n_scale = 0;
		bc_raise(guess, guess1, &guess, 0);
		bc_free_num(&guess1);
		cscale = 3;
	}

	/* Find the square root using Newton's algorithm. */
	while (TRUE)
	{
		bc_free_num(&guess1);
		guess1 = bc_copy_num(guess);
		bc_divide(*num, guess, &guess, cscale);
		bc_add(guess, guess1, &guess, 0);
		bc_multiply(guess, point5, &guess, cscale);
		bc_sub(guess, guess1, &diff, cscale + 1);
		if (bc_is_near_zero(diff, cscale))
		{
			if (cscale < rscale + 1)
				cscale = MIN(cscale * 3, rscale + 1);
			else
				break;
		}
	}

	/* Assign the number and clean up. */
	bc_free_num(num);
	bc_divide(guess, bc_one, num, rscale);
	bc_free_num(&guess);
	bc_free_num(&guess1);
	bc_free_num(&point5);
	bc_free_num(&diff);
	return 1;
}

/* Convert a number NUM to a long.  The function returns only the integer
   part of the number.  For numbers that are too large to represent as
   a long, this function returns a zero.  This can be detected by checking
   the NUM for zero after having a zero returned. */

long bc_num2long(bc_num num)
{
	long  val   = 0;
	char *nptr  = num->n_value;
	int   index = num->n_len;

	/* Extract the int value, ignore the fraction. */
	for (; (index > 0) && (val <= (LONG_MAX / BASE)); index--)
		val = val * BASE + *nptr++;

	/* Check for overflow.  If overflowed, return zero. */
	if (index > 0)
		val = 0;
	if (val < 0)
		val = 0;

	/* Return the value. */
	if (num->n_sign == PLUS)
		return (val);
	else
		return (-val);
}

/* Convert an integer VAL to a bc number NUM. */

void bc_int2num(bc_num *num, int val)
{
	char buffer[30];
	int  ix  = 1;
	char neg = 0;

	/* Sign. */
	if (val < 0)
	{
		neg = 1;
		val = -val;
	}

	/* Get things going. */
	char *bptr = buffer;
	*bptr++    = static_cast<char>(val % BASE);
	val        = val / BASE;

	/* Extract remaining digits. */
	while (val != 0)
	{
		*bptr++ = static_cast<char>(val % BASE);
		val     = val / BASE;
		ix++; /* Count the digits. */
	}

	/* Make the number. */
	bc_free_num(num);
	*num = bc_new_num(ix, 0);
	if (neg)
		(*num)->n_sign = MINUS;

	/* Assign the digits. */
	char *vptr = (*num)->n_value;
	while (ix-- > 0)
		*vptr++ = *--bptr;
}

/* Convert a numbers to a string.  Base 10 only.*/

char *num2str(bc_num num)
{
	char *str;
	char *nptr = num->n_value;
	int   index;
	int   signch = (num->n_sign == PLUS ? 0 : 1); /* Number of sign chars. */

	/* Allocate the string memory. */
	if (num->n_scale > 0)
		str = static_cast<char *>(malloc(num->n_len + num->n_scale + 2 + signch));
	else
		str = static_cast<char *>(malloc(num->n_len + 1 + signch));
	if (str == nullptr)
		bc_out_of_memory();

	/* The negative sign if needed. */
	char *sptr = str;
	if (signch)
		*sptr++ = '-';

	/* Load the whole number. */
	for (index = num->n_len; index > 0; index--)
		*sptr++ = BCD_CHAR(*nptr++);

	/* Now the fraction. */
	if (num->n_scale > 0)
	{
		*sptr++ = '.';
		for (index = 0; index < num->n_scale; index++)
			*sptr++ = BCD_CHAR(*nptr++);
	}

	/* Terminate the string and return it! */
	*sptr = '\0';
	return (str);
}
/* Convert strings to bc numbers.  Base 10 only.*/

void bc_str2num(bc_num *num, char *str, int scale)
{
	bc_ensure_numbers_initialized();
	int   digits   = 0;
	int   strscale = 0;
	char *ptr      = str;
	char  zero_int = FALSE;

	/* Prepare num. */
	bc_free_num(num);

	/* Check for valid number and count digits. */
	if ((*ptr == '+') || (*ptr == '-'))
		ptr++; /* Sign */
	while (*ptr == '0')
		ptr++; /* Skip leading zeros. */
	while (isdigit(static_cast<int>(*ptr)))
		ptr++, digits++; /* digits */
	if (*ptr == '.')
		ptr++; /* decimal point */
	while (isdigit(static_cast<int>(*ptr)))
		ptr++, strscale++; /* digits */
	if ((*ptr != '\0') || (digits + strscale == 0))
	{
		*num = bc_copy_num(bc_zero);
		return;
	}

	/* Adjust numbers and allocate storage and initialize fields. */
	strscale = MIN(strscale, scale);
	if (digits == 0)
	{
		zero_int = TRUE;
		digits   = 1;
	}
	*num = bc_new_num(digits, strscale);

	/* Build the whole number. */
	ptr = str;
	if (*ptr == '-')
	{
		(*num)->n_sign = MINUS;
		ptr++;
	}
	else
	{
		(*num)->n_sign = PLUS;
		if (*ptr == '+')
			ptr++;
	}
	while (*ptr == '0')
		ptr++; /* Skip leading zeros. */
	char *nptr = (*num)->n_value;
	if (zero_int)
	{
		*nptr++ = 0;
		digits  = 0;
	}
	for (; digits > 0; digits--)
		*nptr++ = CH_VAL(*ptr++);

	/* Build the fractional part. */
	if (strscale > 0)
	{
		ptr++; /* skip the decimal point! */
		for (; strscale > 0; strscale--)
			*nptr++ = CH_VAL(*ptr++);
	}
}

/* Added by NJG to remove a memory leak */

void bc_free_numbers()
{
	bc_free_num(&bc_zero);
	bc_free_num(&bc_one);
	bc_free_num(&bc_two);
}
