---
layout    : post
title     : "Vim Pickups 001"
date      : 2017-08-28
lastupdate: 2021-01-11
---

今日有闲，整理点 vim 小技巧。本文所列命令都是 VIM 内置的，无需额外插件。

* TOC
{:toc}

# 1. VIM 别名

一些编辑器看似独立，其实后台都是调用的 VIM，只是启动参数不同：

* `vim` - 正常启动 VIM
* `ex` - 以 `Ex` 模式启动 VIM
* `view` - 以只读模式启动 VIM，和 `vim -R` 效果一样
* 其他：`gvim`, `gview`, `rview` 等等

用 `file` 命令看一下：

```shell
$ file /usr/bin/ex
/usr/bin/ex: symbolic link to '/etc/alternatives/ex'

$ file /etc/alternatives/ex
/etc/alternatives/ex: symbolic link to '/usr/bin/vim.gtk'

$ file /usr/bin/vim.gtk
/usr/bin/vim.gtk: ELF 64-bit LSB  executable, x86-64, version 1 (SYSV), dynamically linked (uses shared libs), for GNU/Linux 2.6.24, BuildID[sha1]=3544bf336449c36023788f68ed25a6d75e575a08, stripped
```

# 2. VIM 打开文件：高级功能

启动 VIM 时可以加额外参数，完成很多神奇炫酷的功能。

## 2.1 以二进制模式打开文件

```
$ vim -b <file>
```

## 2.2 `+` 指定额外命令

### 打开文件后，跳转到第 N 行

```shell
$ vim +5 test.txt
```

### 打开文件后，跳转到 `{pattern}` 第一次出现的位置

```
$ vim +/{pattern} <file>

# 跳转到 'vim' 字符串第一次出现的位置
$ vim +/vim test.txt
```

### 其他命令

```shell
$ vim "+{command}" <file>
$ vim -c {command} <file>

# example: turn off showing number on opening index.html
$ vim "+set nonu" index.html
```

## 2.3 以 diff 模式打开多个文件

```shell
$ vim -d <file1> <file2> ... <fileN>
```

效果和 `vimdiff` 一样。

## 2.4 一次打开多个文件

分别显示在不同的窗口。

```
# 窗口上下排列， N 可省略
$ vim -o[N] <file1> <file2> ... <fileN>

# 窗口左右排列， N 可省略
$ vim -O[N] <file1> <file2> ... <fileN>
```

# 3 匹配然后删除（match then delete）

## 3.1 基础匹配删除操作

1. 删除匹配的字符串：`%s/<pattern>//g`

    本质上是**替换**（substitute），用空字符替换了 `<pattern>`。

1. 删除匹配行：`:%g/<pattern>/d`

    例如 `:%g/cloud/d`，表示删除当前文件内包含 `cloud` 的行。

## 3.2 删除匹配行前后 n/m 行

来自 [Deleting a range of n lines before and after a matched
line?](https://vi.stackexchange.com/questions/3411/deleting-a-range-of-n-lines-before-and-after-a-matched-line)：

1. `:%g/<pattern>/-10,+28d`：找到包含 `<pattern>` 的行，然后从该行前面 10 行一直
   删除到该行后面 28 行。[2]
1. `:%g/<pattern>/-10,.d`：删除从匹配行 `-10` 行直到当前行，小数点表示当前行。

# 4 替换（substitution）

* [将多个连续空行合并为一行](https://vim.fandom.com/wiki/Remove_unwanted_empty_lines)：`:%s/\n\{3,}/\r\r/e`

# References

1. VIM Man Manual (`man 1 vim`)
