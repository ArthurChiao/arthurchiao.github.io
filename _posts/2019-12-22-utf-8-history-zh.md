---
layout    : post
title     : "[译] 拨乱反正：Ken Thompson 才是 UTF-8 的设计者（2003）"
date      : 2019-12-22
lastupdate: 2019-12-22
categories: utf-8
---

### 译者序

本文翻译自 2003 年的一份网页存档：[UTF-8
history](https://www.cl.cam.ac.uk/~mgk25/ucs/utf-8-history.txt)。

2003 年 4 月的最后一个夜晚即将从键盘上溜走时，作为 UTF-8 设计
过程的亲眼见证者，坐在电脑前的 Rob Pike 终于决定写一封邮件来阻止 “UTF-8 编码是
IBM 设计的” 这一错误的继续传播，本文的故事由此展开。

Rob 在邮件中非常谦虚地隐去了自己作为联合设计者的功劳，只是轻描淡写地说了一句自己
期间承担了 Ken 的鼓励师的角色。

假如对文中几位的名字感到陌生，以下事迹或头衔可略作参考：

* Rob Pike
    * Unix 开发者之一，Ken Thompson 在贝尔实验室的小老弟
    * Plan 9 操作系统作者之一
    * Go 语言设计团队第一任老大
* Ken Thompson
    * 与 Dennis Ritchie 一起发明了 Unix 和 C 语言，经典 C 语言标准 “K&R C” 中的 K
    * Plan 9 操作系统作者之一
    * 图灵奖获得者（1983）
    * Go 语言设计团队元老
* Russ Cox
    * Go 语言开发和设计：2008 年加入 Google 开发 Go，现（2019）为 Go 开发团队 leader

有了以上背景，也就不难理解为什么 Go 语言中的 UTF-8 支持如此原生了。

### 几封邮件及时间线

* `2003.04.30 22:32:32`: [1. Rob Pike：不要再说 UTF-8 是 IBM 设计的了](#ch_1)
* `2003.06.07 18:44:05`: [2. Rob Pike：我请了 Russ Cox 去存档中找当时的往来邮件](#ch_2)
* `2003.06.07  7:46 PM`: [3. Russ Cox：找到的相关邮件](#ch_3)
* `1992.09.04 03:37:39`: [4. Ken：能否让 utf.c 实现更整洁（存档邮件)](#ch_4)
* `1992.09.08 03:22:07`: [5. Ken：UTF-8 提案及代码（存档邮件)](#ch_5)
* `1992.09.08 03:24:58`: [6. Ken：抄送我自己的邮件还没收到（存档邮件)](#ch_6)
* `1992.09.08 03:42:43`: [7. Ken：终于收到抄送了（存档邮件)](#ch_7)
* `1992.09.02         `: [8. 第一份 UTF-8 提案：直接追加在原始 FSS-UTF 提案后面（存档邮件)](#ch_8)

**由于译者水平有限，本文不免存在遗漏或错误之处。如有疑问，请查阅原文。**

以下是译文。

----

<a name="ch_1"></a>

# 1. Rob Pike：不要再说的 UTF-8 是 IBM 设计的了

```
Subject: UTF-8 history
From: "Rob 'Commander' Pike" <r (at) google.com>
Date: Wed, 30 Apr 2003 22:32:32 -0700 (Thu 06:32 BST)
To: mkuhn (at) acm.org, henry (at) spsystems.net
Cc: ken (at) entrisphere.com
```

回望 UTF-8 的过去，我看到一个错误的故事在不断传播。这个故事说：

1. IBM 设计了 UTF-8
1. Plan 9 实现了这份设计方案

这不是事实。

**时间回到 1992 年 9 月前后，在新泽西（New Jersey）的某次晚餐上，我亲眼见
证了 UTF-8 在一张餐桌纸上的诞生**。

下面这些才是事实：

> 我们曾用最初的 ISO 10646 UTF 使 Plan 9 支持 16-bit 字符，但结果令我们非常不满
> 意。在 Plan 9 系统即将交付的某个傍晚，我接到了几个人打来的一通电话，这些人应
> 该是 IBM 的 —— 我记得他们当时在 Austin —— 正在参加一个 X/OPen 委员会会议。他们
> 打电话的目的是希望 Ken 和我评审他们的 FSS/UTF 设计方案。我们当时知道他们为什么
> 要引入一个新的设计，**更重要的是，Ken 和我当时突然意识到，这是一个充分利用我俩
> 的经验来设计一个真正优秀的标准的好机会，而标准推进的事情可以交给他们**。
> 
> 我们向对方表达了这种想法，得到的答复是：**只要我们能在尽量短的时间内给出一套方
> 案，他们愿意去推进这件事情**。然后，我俩就去餐厅吃饭了，**Ken 在吃饭期间确定了
> bit-packing 的设计**，当吃完回到实验室时，我们打给了 X/Open 的这几位朋友，向他
> 们解释了我们的设计方案。
> 
> 我们以邮件的形式将这套规范的大纲（outline of our spec）发给了他们，**他们回复
> 说这比他们自己设计的版本好多了**（我压根就没真正见过他们的提案；反正我是想
> 不起来），并询问我们最快什么时候能实现它？我记得这是一个周三的晚上，我们回复
> 说下周一之前一定可以提供一个完整的、可运行的系统，因为我记得下周一是他们投票表
> 决的时间。
>
> 因此，那个晚上 Ken 编写了 packing 和 unpacking 的代码，我将这些代码拆分成了 C 和
> graphics 库。第二天所有的代码都好了之后，我们开始将系统上所有的文本文件转换成
> UTF-8 编码格式。到了周五的某个时刻，**Plan 9 运行起来了，并且完全运行在这种名为
> UTF-8 的编码之上**。然后我们打给了 X/Open 和其他一些人，但他们后来对此事的描述，
> 却是一段略微修改过的历史。

为什么我们不直接用他们的 FSS/UTF 设计方案呢？根据我的记忆，是因为在他们第一次打
电话过来时，我提出了几个这类编码方案应该解决的问题， 而其中**至少有一个问题是
FSS/UTF 方案无法解决的** —— 对于从中间开始获取的字节流，**在读取不超过一个字
符的前提下就能对字节流进行同步的能力**（synchronize a byte stream picked up
mid-run, with less that one character being consumed before synchronization）。
正是因为缺乏这种能力，所以我们才可以自由地 —— 他们也给了我们自由 —— 来设计我们自
己的方案。

我认为“IBM 设计了 UTF-8，Plan 9 实现了它”的故事最初是从 RFC2279 中来的。当时
看到 UTF-8 如此流行我们非常高兴，因此就没有去纠正这段误传的历史。现在我和 Ken 都
已经不在当初的实验室了，但我相信存档中一定有当时的邮件可以证明前面讲的内容，我
也许能找人把这些邮件捞出来。

最后，**将 UTF-8 变成标准并且推广开来确实是 X/Open 和 IBM 那些人的功劳，但不管历
史书上怎么说，UTF-8 都是 Ken 设计的**（我的贡献是期间中充当了 Ken 的鼓励师）。

（邮件落款）- rob

<a name="ch_2"></a>

# 2. Rob Pike：我请了 Russ Cox 去存档中找当时的往来邮件

```
Date: Sat, 07 Jun 2003 18:44:05 -0700
From: "Rob `Commander' Pike" <r (at) google.com>
To: Markus Kuhn <Markus.Kuhn (at) cl.cam.ac.uk>
cc: henry (at) spsystems.net, ken (at) entrisphere.com, Greger Leijonhufvud <greger (at) friherr.com>
Subject: Re: UTF-8 history
```

我请了 Russ Cox 帮忙去存档中寻找当时这些资料。下面附录了他的邮件。
相信这些内容会让你相信之前邮件中所述均确有其事。

我们发给 X/Open 的邮件（我记得是 Ken 编辑和发送的那份设计文档）包括一个新的 设计
考虑： `#6` 关于检测字符边界（discovering character boundaries）。我们无从知道
X/Open 最初的提案如何影响了我们；这两份提案差别非常大，但确实也有某些相同点（
very different but do share some characteristics）。我不记得我曾仔细地看过他们的
提案，这已经是很久之前的事情了。但我非常清楚地记得 Ken 在餐桌纸上完成了设计
，我多么希望我们把那张纸保存了下来！

（邮件落款）- rob

<a name="ch_3"></a>

## 3. Russ Cox：找到的相关邮件

```
From: Russ Cox <rsc (at) plan9.bell-labs.com>
To: r (at) google.com
Subject: utf digging
Date-Sent: Saturday, June 07, 2003 7:46 PM -0400
```

----

* `1992.09.04 19:51:55`：`/sys/src/libc/port/rune.c` 文件的编码格式从原来老的大
  量使用除法（division-heavy）的 UTF 格式转换成了新的 UTF-8 格式
* `1992.09.04`：`/sys/src/libc/port/rune.c` 添加了注释，然后就没有再修改过了，直到 `1996.11.14`
* `1996.11.14`：对 `runelen` 做了加速优化：显式检查 rune 而不是使用 `runetochar` 的返回值
* `2001.5.26`：下一次也是最后一次修改，添加了 `runenlen()` 函数

下面是在你的邮箱中 `grep` utf 找到的几封邮件。第一封是关于 `utf.c`，这个文件基本
上是照抄了 `wctomb` 和 `mbtowc`，用于处理 32 bit rune 的 full 6-byte utf-8 编码。
这段代码非常丑陋，全部逻辑都在控制流中。**我认为在第一封邮件之后，这段代码变成了
提案中的代码**。

在 `/usr/ken/utf/xutf` 目录中，我找到了一份似乎是最初的无法自同步的（
not-self-synchronizing）编码提案，而 **utf-8 方案追加到了这
份文件的末尾**（从 "We define 7 byte types" 开始）。这些内容也附在了下面。

下面贴的是第一版的邮件，时间是 `1992.09.02 23:44:10`。经过几次修改，在
`1992.09.08` 号形成了第二封邮件，见下面。

邮件日志显式了第二封邮件发了出去，以及过了一段时间后抄送给 Ken 的邮件到达了。

```
helix: Sep  8 03:22:13: ken: upas/sendmail: remote inet!xopen.co.uk!xojig 
> From ken Tue Sep  8 03:22:07 EDT 1992 (xojig@xopen.co.uk) 6833
helix: Sep  8 03:22:13: ken: upas/sendmail: delivered rob From ken Tue Sep 8 03:22:07 EDT 1992 6833
helix: Sep  8 03:22:16: ken: upas/sendmail: remote pyxis!andrew From ken Tue Sep  8 03:22:07 EDT 1992 (andrew) 6833
helix: Sep  8 03:22:19: ken: upas/sendmail: remote coma!dmr From ken Tue Sep  8 03:22:07 EDT 1992 (dmr) 6833
helix: Sep  8 03:25:52: ken: upas/sendmail: delivered rob From ken Tue Sep 8 03:24:58 EDT 1992 141
helix: Sep  8 03:36:13: ken: upas/sendmail: delivered ken From ken Tue Sep 8 03:36:12 EDT 1992 6833
```

<a name="ch_4"></a>

# 4. Ken：能否让 utf.c 实现更整洁

```
>From ken Fri Sep  4 03:37:39 EDT 1992
```

你可能需要看看 `/usr/ken/utf/utf.c`，以及能否让代码更整洁一点。

<a name="ch_5"></a>

# 5. Ken：UTF-8 提案及代码

```
>From ken Tue Sep  8 03:22:07 EDT 1992
```

下面是我们修改之后的 FSS-UTF 提案。其中所用的术语和原提案中一样。我要为此向原提
案的作者致歉。提案中的代码已经经过一定程度的测试（tested to some degree），比较
整洁（should be pretty good shape）。我们已经设置 Plan 9 接下来使用这种编码格式
（converted Plan 9 to use this encoding），并考虑发布一个版本（
issue a distribution）给一部分大学用户使用。

```
File System Safe Universal Character Set Transformation Format (FSS-UTF)
--------------------------------------------------------------------------

With the approval of ISO/IEC 10646 (Unicode) as an international
standard and the anticipated wide spread use of this universal coded
character set (UCS), it is necessary for historically ASCII based
operating systems to devise ways to cope with representation and
handling of the large number of characters that are possible to be
encoded by this new standard.

There are several challenges presented by UCS which must be dealt with
by historical operating systems and the C-language programming
environment.  The most significant of these challenges is the encoding
scheme used by UCS. More precisely, the challenge is the marrying of
the UCS standard with existing programming languages and existing
operating systems and utilities.

The challenges of the programming languages and the UCS standard are
being dealt with by other activities in the industry.  However, we are
still faced with the handling of UCS by historical operating systems
and utilities.  Prominent among the operating system UCS handling
concerns is the representation of the data within the file system.  An
underlying assumption is that there is an absolute requirement to
maintain the existing operating system software investment while at
the same time taking advantage of the use the large number of
characters provided by the UCS.

UCS provides the capability to encode multi-lingual text within a
single coded character set.  However, UCS and its UTF variant do not
protect null bytes and/or the ASCII slash ("/") making these character
encodings incompatible with existing Unix implementations.  The
following proposal provides a Unix compatible transformation format of
UCS such that Unix systems can support multi-lingual text in a single
encoding.  This transformation format encoding is intended to be used
as a file code.  This transformation format encoding of UCS is
intended as an intermediate step towards full UCS support.  However,
since nearly all Unix implementations face the same obstacles in
supporting UCS, this proposal is intended to provide a common and
compatible encoding during this transition stage.


Goal/Objective
--------------

With the assumption that most, if not all, of the issues surrounding
the handling and storing of UCS in historical operating system file
systems are understood, the objective is to define a UCS
transformation format which also meets the requirement of being usable
on a historical operating system file system in a non-disruptive
manner.  The intent is that UCS will be the process code for the
transformation format, which is usable as a file code.

Criteria for the Transformation Format
--------------------------------------

Below are the guidelines that were used in defining the UCS
transformation format:

    1) Compatibility with historical file systems:

	Historical file systems disallow the null byte and the ASCII
	slash character as a part of the file name.

    2) Compatibility with existing programs:

	The existing model for multibyte processing is that ASCII does
	not occur anywhere in a multibyte encoding.  There should be
	no ASCII code values for any part of a transformation format
	representation of a character that was not in the ASCII
	character set in the UCS representation of the character.

    3) Ease of conversion from/to UCS.

    4) The first byte should indicate the number of bytes to
	follow in a multibyte sequence.

    5) The transformation format should not be extravagant in
	terms of number of bytes used for encoding.

    6) It should be possible to find the start of a character
	efficiently starting from an arbitrary location in a byte
	stream.

Proposed FSS-UTF
----------------

The proposed UCS transformation format encodes UCS values in the range
[0,0x7fffffff] using multibyte characters of lengths 1, 2, 3, 4, 5,
and 6 bytes.  For all encodings of more than one byte, the initial
byte determines the number of bytes used and the high-order bit in
each byte is set.  Every byte that does not start 10xxxxxx is the
start of a UCS character sequence.

An easy way to remember this transformation format is to note that the
number of high-order 1's in the first byte signifies the number of
bytes in the multibyte character:

   Bits  Hex Min  Hex Max  Byte Sequence in Binary
1    7  00000000 0000007f 0vvvvvvv
2   11  00000080 000007FF 110vvvvv 10vvvvvv
3   16  00000800 0000FFFF 1110vvvv 10vvvvvv 10vvvvvv
4   21  00010000 001FFFFF 11110vvv 10vvvvvv 10vvvvvv 10vvvvvv
5   26  00200000 03FFFFFF 111110vv 10vvvvvv 10vvvvvv 10vvvvvv 10vvvvvv
6   31  04000000 7FFFFFFF 1111110v 10vvvvvv 10vvvvvv 10vvvvvv 10vvvvvv 10vvvvvv

The UCS value is just the concatenation of the v bits in the multibyte
encoding.  When there are multiple ways to encode a value, for example
UCS 0, only the shortest encoding is legal.

Below are sample implementations of the C standard wctomb() and
mbtowc() functions which demonstrate the algorithms for converting
from UCS to the transformation format and converting from the
transformation format to UCS. The sample implementations include error
checks, some of which may not be necessary for conformance:

typedef
struct
{
	int	cmask;
	int	cval;
	int	shift;
	long	lmask;
	long	lval;
} Tab;

static
Tab	tab[] =
{
	0x80,	0x00,	0*6,	0x7F,		0,		/* 1 byte sequence */
	0xE0,	0xC0,	1*6,	0x7FF,		0x80,		/* 2 byte sequence */
	0xF0,	0xE0,	2*6,	0xFFFF,		0x800,		/* 3 byte sequence */
	0xF8,	0xF0,	3*6,	0x1FFFFF,	0x10000,	/* 4 byte sequence */
	0xFC,	0xF8,	4*6,	0x3FFFFFF,	0x200000,	/* 5 byte sequence */
	0xFE,	0xFC,	5*6,	0x7FFFFFFF,	0x4000000,	/* 6 byte sequence */
	0,							/* end of table */
};

int
mbtowc(wchar_t *p, char *s, size_t n)
{
	long l;
	int c0, c, nc;
	Tab *t;

	if(s == 0)
		return 0;

	nc = 0;
	if(n <= nc)
		return -1;
	c0 = *s & 0xff;
	l = c0;
	for(t=tab; t->cmask; t++) {
		nc++;
		if((c0 & t->cmask) == t->cval) {
			l &= t->lmask;
			if(l < t->lval)
				return -1;
			*p = l;
			return nc;
		}
		if(n <= nc)
			return -1;
		s++;
		c = (*s ^ 0x80) & 0xFF;
		if(c & 0xC0)
			return -1;
		l = (l<<6) | c;
	}
	return -1;
}

int
wctomb(char *s, wchar_t wc)
{
	long l;
	int c, nc;
	Tab *t;

	if(s == 0)
		return 0;

	l = wc;
	nc = 0;
	for(t=tab; t->cmask; t++) {
		nc++;
		if(l <= t->lmask) {
			c = t->shift;
			*s = t->cval | (l>>c);
			while(c > 0) {
				c -= 6;
				s++;
				*s = 0x80 | ((l>>c) & 0x3F);
			}
			return nc;
		}
	}
	return -1;
}
```

<a name="ch_6"></a>

# 6. Ken：抄送我自己的邮件还没收到

```
From ken Tue Sep  8 03:24:58 EDT 1992
```

邮件我已经发出并抄送了自己，但它好像进入了黑洞。我还没收到（i didnt get my copy）。
一定是彗星来临或天降异象，导致我的邮件卡在互联网上了。

<a name="ch_7"></a>

# 7. Ken：终于收到抄送了

```
From ken Tue Sep  8 03:42:43 EDT 1992
```

终于收到抄送了（i finally got my copy）。

<a name="ch_8"></a>

## 8. 第一份 UTF-8 提案：直接追加在原始 FSS-UTF 提案后面（存档邮件）

> 以 "We define 7 byte types" 为分割点，之前的是原始提案，后面是 Ken 和 Pike
> 添加的。 - 译者注

--- /usr/ken/utf/xutf from dump of Sep 2 1992 ---

```
File System Safe Universal Character Set Transformation Format (FSS-UTF)
--------------------------------------------------------------------------

With the approval of ISO/IEC 10646 (Unicode) as an international
standard and the anticipated wide spread use of this universal coded
character set (UCS), it is necessary for historically ASCII based
operating systems to devise ways to cope with representation and
handling of the large number of characters that are possible to be
encoded by this new standard.

There are several challenges presented by UCS which must be dealt with
by historical operating systems and the C-language programming
environment. The most significant of these challenges is the encoding
scheme used by UCS.  More precisely, the challenge is the marrying of
the UCS standard with existing programming languages and existing
operating systems and utilities.

The challenges of the programming languages and the UCS standard are
being dealt with by other activities in the industry.	However, we are
still faced with the handling of UCS by historical operating systems and
utilities. Prominent among the operating system UCS handling concerns is
the representation of the data within the file system. An underlying
assumption is that there is an absolute requirement to maintain the
existing operating system software investment while at the same time
taking advantage of the use the large number of characters provided by
the UCS.

UCS provides the capability to encode multi-lingual text within a single
coded character set.  However, UCS and its UTF variant do not protect
null bytes and/or the ASCII slash ("/") making these character encodings
incompatible with existing Unix implementations.  The following proposal
provides a Unix compatible transformation format of UCS such that Unix
systems can support multi-lingual text in a single encoding.  This
transformation format encoding is intended to be used as a file code.
This transformation format encoding of UCS is intended as an
intermediate step towards full UCS support.  However, since nearly all
Unix implementations face the same obstacles in supporting UCS, this
proposal is intended to provide a common and compatible encoding during
this transition stage.


Goal/Objective
--------------

With the assumption that most, if not all, of the issues surrounding the
handling and storing of UCS in historical operating system file systems
are understood, the objective is to define a UCS transformation format
which also meets the requirement of being usable on a historical
operating system file system in a non-disruptive manner. The intent is
that UCS will be the process code for the transformation format, which
is usable as a file code.

Criteria for the Transformation Format
--------------------------------------

Below are the guidelines that were used in defining the UCS
transformation format:

    1) Compatibility with historical file systems:

   Historical file systems disallow the null byte and the ASCII
   slash character as a part of the file name.

    2) Compatibility with existing programs:

   The existing model for multibyte processing is that ASCII does
   not occur anywhere in a multibyte encoding.  There should be no
   ASCII code values for any part of a transformation format
   representation of a character that was not in the ASCII character
   set in the UCS representation of the character.

    3) Ease of conversion from/to UCS.

    4) The first byte should indicate the number of bytes to follow in a
   multibyte sequence.

    5) The transformation format should not be extravagant in terms of
   number of bytes used for encoding.


Proposed FSS-UTF
----------------

The proposed UCS transformation format encodes UCS values in the range
[0,0x7fffffff] using multibyte characters of lengths 1, 2, 3, 4, and 5
bytes.  For all encodings of more than one byte, the initial byte
determines the number of bytes used and the high-order bit in each byte
is set.

An easy way to remember this transformation format is to note that the
number of high-order 1's in the first byte is the same as the number of
subsequent bytes in the multibyte character:

   Bits  Hex Min  Hex Max         Byte Sequence in Binary
1    7  00000000 0000007f 0zzzzzzz
2   13  00000080 0000207f 10zzzzzz 1yyyyyyy
3   19  00002080 0008207f 110zzzzz 1yyyyyyy 1xxxxxxx
4   25  00082080 0208207f 1110zzzz 1yyyyyyy 1xxxxxxx 1wwwwwww
5   31  02082080 7fffffff 11110zzz 1yyyyyyy 1xxxxxxx 1wwwwwww 1vvvvvvv

The bits included in the byte sequence is biased by the minimum value
so that if all the z's, y's, x's, w's, and v's are zero, the minimum
value is represented.	In the byte sequences, the lowest-order encoded
bits are in the last byte; the high-order bits (the z's) are in the
first byte.

This transformation format uses the byte values in the entire range of
0x80 to 0xff, inclusive, as part of multibyte sequences.  Given the
assumption that at most there are seven (7) useful bits per byte, this
transformation format is close to minimal in its number of bytes used.

Below are sample implementations of the C standard wctomb() and
mbtowc() functions which demonstrate the algorithms for converting from
UCS to the transformation format and converting from the transformation
format to UCS.  The sample implementations include error checks, some
of which may not be necessary for conformance:

#define OFF1   0x0000080
#define OFF2   0x0002080
#define OFF3   0x0082080
#define OFF4   0x2082080

int wctomb(char *s, wchar_t wc)
{
       if (s == 0)
	       return 0;       /* no shift states */
#ifdef wchar_t_is_signed
       if (wc < 0)
	       goto bad;
#endif
       if (wc <= 0x7f)         /* fits in 7 bits */
       {
	       s[0] = wc;
	       return 1;
       }
       if (wc <= 0x1fff + OFF1)        /* fits in 13 bits */
       {
	       wc -= OFF1;
	       s[0] = 0x80 | (wc >> 7);
	       s[1] = 0x80 | (wc & 0x7f);
	       return 2;
       }
       if (wc <= 0x7ffff + OFF2)       /* fits in 19 bits */
       {
	       wc -= OFF2;
	       s[0] = 0xc0 | (wc >> 14);
	       s[1] = 0x80 | ((wc >> 7) & 0x7f);
	       s[2] = 0x80 | (wc & 0x7f);
	       return 3;
       }
       if (wc <= 0x1ffffff + OFF3)     /* fits in 25 bits */
       {
	       wc -= OFF3;
	       s[0] = 0xe0 | (wc >> 21);
	       s[1] = 0x80 | ((wc >> 14) & 0x7f);
	       s[2] = 0x80 | ((wc >> 7) & 0x7f);
	       s[3] = 0x80 | (wc & 0x7f);
	       return 4;
       }
#if !defined(wchar_t_is_signed) || defined(wchar_t_is_more_than_32_bits)
       if (wc > 0x7fffffff)
	       goto bad;
#endif
       wc -= OFF4;
       s[0] = 0xf0 | (wc >> 28);
       s[1] = 0x80 | ((wc >> 21) & 0x7f);
       s[2] = 0x80 | ((wc >> 14) & 0x7f);
       s[3] = 0x80 | ((wc >> 7) & 0x7f);
       s[4] = 0x80 | (wc & 0x7f);
       return 5;
bad:;
       errno = EILSEQ;
       return -1;
}


int mbtowc(wchar_t *p, const char *s, size_t n)
{
       unsigned char *uc;      /* so that all bytes are nonnegative */

       if ((uc = (unsigned char *)s) == 0)
	       return 0;               /* no shift states */
       if (n == 0)
	       return -1;
       if ((*p = uc[0]) < 0x80)
	       return uc[0] != '\0';   /* return 0 for '\0', else 1 */
       if (uc[0] < 0xc0)
       {
	       if (n < 2)
		       return -1;
	       if (uc[1] < 0x80)
		       goto bad;
	       *p &= 0x3f;
	       *p <<= 7;
	       *p |= uc[1] & 0x7f;
	       *p += OFF1;
	       return 2;
       }
       if (uc[0] < 0xe0)
       {
	       if (n < 3)
		       return -1;
	       if (uc[1] < 0x80 || uc[2] < 0x80)
		       goto bad;
	       *p &= 0x1f;
	       *p <<= 14;
	       *p |= (uc[1] & 0x7f) << 7;
	       *p |= uc[2] & 0x7f;
	       *p += OFF2;
	       return 3;
       }
       if (uc[0] < 0xf0)
       {
	       if (n < 4)
		       return -1;
	       if (uc[1] < 0x80 || uc[2] < 0x80 || uc[3] < 0x80)
		       goto bad;
	       *p &= 0x0f;
	       *p <<= 21;
	       *p |= (uc[1] & 0x7f) << 14;
	       *p |= (uc[2] & 0x7f) << 7;
	       *p |= uc[3] & 0x7f;
	       *p += OFF3;
	       return 4;
       }
       if (uc[0] < 0xf8)
       {
	       if (n < 5)
		       return -1;
	       if (uc[1] < 0x80 || uc[2] < 0x80 || uc[3] < 0x80 || uc[4] < 0x80)
		       goto bad;
	       *p &= 0x07;
	       *p <<= 28;
	       *p |= (uc[1] & 0x7f) << 21;
	       *p |= (uc[2] & 0x7f) << 14;
	       *p |= (uc[3] & 0x7f) << 7;
	       *p |= uc[4] & 0x7f;
	       if (((*p += OFF4) & ~(wchar_t)0x7fffffff) == 0)
		       return 5;
       }
bad:;
       errno = EILSEQ;
       return -1;
}

We define 7 byte types:

T0	0xxxxxxx	7 free bits
Tx	10xxxxxx	6 free bits
T1	110xxxxx	5 free bits
T2	1110xxxx	4 free bits
T3	11110xxx	3 free bits
T4	111110xx	2 free bits
T5	111111xx	2 free bits

Encoding is as follows.

>From hex	Thru hex	Sequence		Bits
00000000	0000007f	T0			7
00000080	000007FF	T1 Tx			11
00000800	0000FFFF	T2 Tx Tx		16
00010000	001FFFFF	T3 Tx Tx Tx		21
00200000	03FFFFFF	T4 Tx Tx Tx Tx		26
04000000	FFFFFFFF	T5 Tx Tx Tx Tx Tx	32

Some notes:

1. The 2 byte sequence has 2^11 codes, yet only 2^11-2^7
are allowed. The codes in the range 0-7f are illegal.
I think this is preferable to a pile of magic additive
constants for no real benefit. Similar comment applies
to all of the longer sequences.

2. The 4, 5, and 6 byte sequences are only there for
political reasons. I would prefer to delete these.

3. The 6 byte sequence covers 32 bits, the FSS-UTF
proposal only covers 31.

4. All of the sequences synchronize on any byte that is
not a Tx byte.
```
