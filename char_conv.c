/*
    Copyright (C) 2009 - 2010
    Artem Makhutov <artem@makhutov.org>
    http://www.makhutov.org

    Dmitry Vagin <dmitry2004@yandex.ru>

    Copyright (C) 2010 - 2011
    bg <bg_one@mail.ru>

    Copyright (C) 2018 Google
*/
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <sys/types.h>

#include <iconv.h>			/* iconv_t iconv() */
#include <string.h>			/* memcpy() */
#include <stdio.h>			/* sscanf() snprintf() */
#include <errno.h>			/* EINVAL */

#include "char_conv.h"
#include "mutils.h"			/* ITEMS_OF() */

static ssize_t convert_string (const char* in, size_t in_length, char* out, size_t out_size, char* from, char* to)
{
	ICONV_CONST char*	in_ptr = (ICONV_CONST char*) in;
	size_t			in_bytesleft = in_length;
	char*			out_ptr = out;
	size_t			out_bytesleft = out_size - 1;
	ICONV_T			cd = (ICONV_T) -1;
	ssize_t			res;

	cd = iconv_open (to, from);
	if (cd == (ICONV_T) -1)
	{
		return -2;
	}

	res = iconv (cd, &in_ptr, &in_bytesleft, &out_ptr, &out_bytesleft);
	if (res < 0)
	{
		iconv_close (cd);
		return -3;
	}

	iconv_close (cd);

	*out_ptr = '\0';

	return (out_ptr - out);
}

#/* convert 1 hex digits of PDU to byte, return < 0 on error */
EXPORT_DEF int parse_hexdigit(int hex)
{
	if(hex >= '0' && hex <= '9')
		return hex - '0';
	if(hex >= 'a' && hex <= 'f')
		return hex - 'a' + 10;
	if(hex >= 'A' && hex <= 'F')
		return hex - 'A' + 10;
	return -1;
}

static ssize_t hexstr_to_8bitchars (const char* in, size_t in_length, char* out, size_t out_size, str_encoding_t encoding)
{
	int d1, d2;

	/* odd number of chars check */
	if (in_length & 0x1)
		return -EINVAL;

	in_length = in_length >> 1;

	if (out_size - 1 < in_length)
	{
		return -ENOMEM;
	}
	out_size = in_length;
	
	for (; in_length; --in_length)
	{
		d1 = parse_hexdigit(*in++);
		if(d1 < 0)
			return -EINVAL;
		d2 = parse_hexdigit(*in++);
		if(d2 < 0)
			return -EINVAL;
		*out++ = (d1 << 4) | d2;
	}

	*out = 0;

	return out_size;
}

static ssize_t chars8bit_to_hexstr (const char* in, size_t in_length, char* out, size_t out_size, str_encoding_t encoding)
{
	static const char hex_table[] = "0123456789ABCDEF";
	const unsigned char *in2 = (const unsigned char *)in;	/* for save time of first & 0x0F */

	if (out_size - 1 < in_length * 2)
	{
		return -1;
	}
	out_size = in_length * 2;
	
	for (; in_length; --in_length, ++in2)
	{
		*out++ = hex_table[*in2 >> 4];
		*out++ = hex_table[*in2 & 0xF];
	}

	*out = 0;

	return out_size;
}

static ssize_t hexstr_ucs2_to_utf8 (const char* in, size_t in_length, char* out, size_t out_size, str_encoding_t encoding)
{
	char	buf[out_size];
	ssize_t	res;

	if (out_size - 1 < in_length / 2)
	{
		return -1;
	}

	res = hexstr_to_8bitchars (in, in_length, buf, out_size, encoding);
	if (res < 0)
	{
		return res;
	}

	res = convert_string (buf, res, out, out_size, "UCS-2BE", "UTF-8");

	return res;
}

static ssize_t utf8_to_hexstr_ucs2 (const char* in, size_t in_length, char* out, size_t out_size, str_encoding_t encoding)
{
	char	buf[out_size];
	ssize_t	res;

	if (out_size - 1 < in_length * 4)
	{
		return -1;
	}

	res = convert_string (in, in_length, buf, out_size, "UTF-8", "UCS-2BE");
	if (res < 0)
	{
		return res;
	}

	res = chars8bit_to_hexstr (buf, res, out, out_size, encoding);

	return res;
}

static ssize_t char_to_hexstr_7bit (const char* in, size_t in_length, char* out, size_t out_size, str_encoding_t encoding)
{
	size_t		i;
	size_t		x = 0;
	size_t		s;
	unsigned char	c;
	unsigned char	b;
	char	buf[] = { 0x0, 0x0, 0x0 };
	int s0;

	x = (in_length - in_length / 8) * 2;
	if (out_size - 1 < x)
	{
		return -1;
	}

	s0 = (0 + ((encoding & STR_ENCODING_7BIT_OFFSET_MASK) >> STR_ENCODING_7BIT_OFFSET_SHIFT)) & 7;

	if(in_length > 0)
	{
		in_length--;
		for (i = 0, x = 0, s = s0; i < in_length; i++)
		{
			if (s == 7)
			{
				s = 0;
				continue;
			}

			c = in[i] >> s;
			b = in[i + 1] << (7 - s);
			c = c | b;
			s++;

			snprintf (buf, sizeof(buf), "%.2X", c);

			memcpy (out + x, buf, 2);
			x = x + 2;
		}

		c = in[i] >> s;
		snprintf (buf, sizeof(buf), "%.2X", c);
		memcpy (out + x, buf, 2);
		x = x + 2;
	}
	out[x] = '\0';

	return x;
}

/* Table from GSM 7-bit to UTF-8.  */
static const char table_7bit_to_utf8[0x80][3] = {
  "@",        "\xc2\xa3", "$",        "\xc2\xa5", "\xc3\xa8", "\xc3\xa9", "\xc3\xb9", "\xc3\xac", 
  "\xc3\xb2", "\xc3\x87", "\x0d",     "\xc3\x98", "\xc3\xb8", "\x0a",     "\xc3\x85", "\xc3\xa5", 
  "\xce\x94", "_",        "\xce\xa6", "\xce\x93", "\xce\x9b", "\xce\xa9", "\xce\xa0", "\xce\xa8", 
  "\xce\xa3", "\xce\x98", "\xce\x9e", " ",        "\xc3\x86", "\xc3\xa6", "\xc3\x9f", "\xc3\x89", 
  " ",        "!",        "\"",       "#",        "\xc2\xa4", "%",        "&",        "'",        
  "(",        ")",        "*",        "+",        ",",        "-",        ".",        "/",        
  "0",        "1",        "2",        "3",        "4",        "5",        "6",        "7",        
  "8",        "9",        ":",        ";",        "<",        "=",        ">",        "?",        
  "\xc2\xa1", "A",        "B",        "C",        "D",        "E",        "F",        "G",        
  "H",        "I",        "J",        "K",        "L",        "M",        "N",        "O",        
  "P",        "Q",        "R",        "S",        "T",        "U",        "V",        "W",        
  "X",        "Y",        "Z",        "\xc3\x84", "\xc3\x96", "\xc3\x91", "\xc3\x9c", "\xc2\xa7", 
  "\xc2\xbf", "a",        "b",        "c",        "d",        "e",        "f",        "g",        
  "h",        "i",        "j",        "k",        "l",        "m",        "n",        "o",        
  "p",        "q",        "r",        "s",        "t",        "u",        "v",        "w",        
  "x",        "y",        "z",        "\xc3\xa4", "\xc3\xb6", "\xc3\xb1", "\xc3\xbc", "\xc3\xa0", 
};

static ssize_t hexstr_7bit_to_char (const char* in, size_t in_length, char* out, size_t out_size, str_encoding_t encoding)
{
	size_t		i;
	char		*x;
	size_t		s;
	size_t          xlen;
	int		hexval;
	unsigned char	c;
	unsigned char	b;
	char	buf[] = { 0x0, 0x0, 0x0 };
	int s0;

	in_length = in_length / 2;
	xlen = in_length + in_length / 7;
	if (out_size - 1 < xlen)
	{
		return -1;
	}

	s0 = (1 + ((encoding & STR_ENCODING_7BIT_OFFSET_MASK) >> STR_ENCODING_7BIT_OFFSET_SHIFT)) & 7;

	for (i = 0, x = out, s = s0, b = 0; i < in_length; i++)
	{
		const char *utf8c;
		memcpy (buf, in + i * 2, 2);
		if (sscanf (buf, "%x", &hexval) != 1)
		{
			return -1;
		}

		c = ((unsigned char) hexval) << s;
		c = (c >> 1) | b;
		b = ((unsigned char) hexval) >> (8 - s);

		/* TODO: support escape sequences.  */
		utf8c = table_7bit_to_utf8[c & 0x7f];
		if (x + strlen(utf8c) >= out + out_size)
			return -1;
		x = stpcpy (x, utf8c);
		s++;

		if (s == 8)
		  {
			  utf8c = table_7bit_to_utf8[b & 0x7f];
			  if (x + strlen(utf8c) >= out + out_size)
				  return -1;
			  x = stpcpy (x, utf8c);
			  s = 1; b = 0;
		  }
	}

	*x = '\0';

	return x - out;
}


#/* */
ssize_t just_copy (const char* in, size_t in_length, char* out, size_t out_size, str_encoding_t encoding)
{
	// FIXME: or copy out_size-1 bytes only ?
	if (in_length <= out_size - 1)
	{
		memcpy(out, in, in_length);
		out[in_length] = 0;
		return in_length;
	}
	return -ENOMEM;
}

typedef ssize_t (*coder) (const char* in, size_t in_length, char* out, size_t out_size, str_encoding_t encoding);

/* array in order of values RECODE_*  */
static const coder recoders[][2] =
{
/* in order of values STR_ENCODING_*  */
	{ hexstr_7bit_to_char, char_to_hexstr_7bit },		/* STR_ENCODING_7BIT_HEX */
	{ hexstr_to_8bitchars, chars8bit_to_hexstr },		/* STR_ENCODING_8BIT_HEX */
	{ hexstr_ucs2_to_utf8, utf8_to_hexstr_ucs2 },		/* STR_ENCODING_UCS2_HEX */
	{ just_copy, just_copy },				/* STR_ENCODING_7BIT */
};

#/* */
EXPORT_DEF ssize_t str_recode(recode_direction_t dir, str_encoding_t encoding, const char* in, size_t in_length, char* out, size_t out_size)
{
	unsigned idx = encoding & STR_ENCODING_TYPE_MASK;
	if((dir == RECODE_DECODE || dir == RECODE_ENCODE) && idx < ITEMS_OF(recoders))
		return (recoders[idx][dir])(in, in_length, out, out_size, encoding);
	return -EINVAL;
}

#/* */
EXPORT_DEF str_encoding_t get_encoding(recode_direction_t hint, const char* in, size_t length)
{
	if(hint == RECODE_ENCODE)
	{
		for(; length; --length, ++in)
			if(*in & 0x80)
				return STR_ENCODING_UCS2_HEX;
		return STR_ENCODING_7BIT_HEX;
	}
	else
	{
		size_t x;
		for(x = 0; x < length; ++x)
		{
			if(parse_hexdigit(in[x]) < 0) {
				return STR_ENCODING_7BIT;
			}
		}
		// TODO: STR_ENCODING_7BIT_HEX or STR_ENCODING_8BIT_HEX or STR_ENCODING_UCS2_HEX
	}

	return STR_ENCODING_UNKNOWN;
}
