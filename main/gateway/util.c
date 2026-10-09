/********************************** (C) COPYRIGHT *******************************
 * File Name          : util.c
 * Description        : 通用工具（-nostdlib 下缺失的 libc 函数）
 *******************************************************************************/

#include <stddef.h>

/* atoi：把字符串转为整数（-nostdlib 无 libc 实现） */
int atoi(const char *s)
{
	int v = 0;
	int neg = 0;
	if (!s) return 0;
	while (*s == ' ' || *s == '\t') s++;
	if (*s == '-') { neg = 1; s++; }
	else if (*s == '+') { s++; }
	while (*s >= '0' && *s <= '9') {
		v = v * 10 + (*s - '0');
		s++;
	}
	return neg ? -v : v;
}
