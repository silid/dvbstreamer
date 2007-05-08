/* UTF8 code from http://www.cprogramming.com/ */
#ifndef _UTF8_H
#define _UTF8_H
#include <stdarg.h>

/* is c the start of a utf8 sequence? */
#define isutf(c) (((c)&0xC0)!=0x80)

/* convert UTF-8 data to wide character */
int UTF8_toucs(u_int32_t *dest, int sz, char *src, int srcsz);

/* the opposite conversion */
int UTF8_toutf8(char *dest, int sz, u_int32_t *src, int srcsz);

/* single character to UTF-8 */
int UTF8_wc_toutf8(char *dest, u_int32_t ch);

/* character number to byte offset */
int UTF8_offset(char *str, int charnum);

/* byte offset to character number */
int UTF8_charnum(char *s, int offset);

/* return next character, updating an index variable */
u_int32_t UTF8_nextchar(char *s, int *i);

/* move to next character */
void UTF8_inc(char *s, int *i);

/* move to previous character */
void UTF8_dec(char *s, int *i);

/* returns length of next utf-8 sequence */
int UTF8_seqlen(char *s);

/* assuming src points to the character after a backslash, read an
   escape sequence, storing the result in dest and returning the number of
   input characters processed */
int UTF8_read_escape_sequence(char *src, u_int32_t *dest);

/* given a wide character, convert it to an ASCII escape sequence stored in
   buf, where buf is "sz" bytes. returns the number of characters output. */
int UTF8_escape_wchar(char *buf, int sz, u_int32_t ch);

/* convert a string "src" containing escape sequences to UTF-8 */
int UTF8_unescape(char *buf, int sz, char *src);

/* convert UTF-8 "src" to ASCII with escape sequences.
   if escape_quotes is nonzero, quote characters will be preceded by
   backslashes as well. */
int UTF8_escape(char *buf, int sz, char *src, int escape_quotes);

/* return a pointer to the first occurrence of ch in s, or NULL if not
   found. character index of found character returned in *charn. */
char *UTF8_strchr(char *s, u_int32_t ch, int *charn);

/* same as the above, but searches a buffer of a given size instead of
   a NUL-terminated string. */
char *UTF8_memchr(char *s, u_int32_t ch, size_t sz, int *charn);

/* count the number of characters in a UTF-8 string */
int UTF8_strlen(char *s);

int UTF8_is_locale_utf8(char *locale);

/* printf where the format string and arguments may be in UTF-8.
   you can avoid this function and just use ordinary printf() if the current
   locale is UTF-8. */
int UTF8_vprintf(char *fmt, va_list ap);
int UTF8_printf(char *fmt, ...);

#endif

