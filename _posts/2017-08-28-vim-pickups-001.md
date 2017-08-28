---
layout: post
title:  "Vim Pickups 001"
date:   2017-08-28
---

今日有闲，整理点vim小技巧。本文所列命令都是基于VIM MAN page (运行`man vim`)，
全部为VIM内置命令，不需要额外插件。

## 1. 基于VIM的编辑器

Linux平台上，有多个不同名字的编辑器**后台均是VIM**，只是启动参数不同：

* `vim` - 正常启动VIM
* `ex` - 以`Ex`模式启动VIM
* `view` - 以只读模式启动VIM，和`vim -R`效果一样
* 其他：`gvim`, `gview`, `rview`等等

用`file`命令看一下：

```
$ file /usr/bin/ex
/usr/bin/ex: symbolic link to `/etc/alternatives/ex'
$ file /etc/alternatives/ex
/etc/alternatives/ex: symbolic link to `/usr/bin/vim.gtk'
$ file /usr/bin/vim.gtk
/usr/bin/vim.gtk: ELF 64-bit LSB  executable, x86-64, version 1 (SYSV), dynamically linked (uses shared libs), for GNU/Linux 2.6.24, BuildID[sha1]=3544bf336449c36023788f68ed25a6d75e575a08, stripped
```

## 2. VIM启动参数

启动VIM的时候可以附加额外参数，完成很多神奇炫酷的功能。

### 2.1 以二进制模式打开文件

```
$ vim -b <file>
```

### 2.2 启动时通过`+`指定额外命令

#### 打开文件，直接跳到第N行

```
$ vim +[N] <file>

# 例子：跳转到第5行
$ vim +5 <file>
```

#### 打开文件，跳转到{pattern}第一次出现的位置
```
$ vim +/{pattern} <file>

# 例：跳转到第一个‘vim’字符串出现的地方
$ vim +/vim _drafts/2017-08-28-vim-pickups-001.md
```

#### 其他额外命令

```
$ vim "+{command}" <file>
$ vim -c {command} <file>

# example: turn off showing number on opening index.html
$ vim "+set nonu" index.html
```

### 2.3 以diff模式打开多个文件

```
$ vim -d <file1> <file2> ... <fileN>
```

效果和`vimdiff`一样。

### 2.4 一次打开多个文件

分别显示在不同的窗口。

```
# 窗口上下排列, N可省略
$ vim -o[N] <file1> <file2> ... <fileN>

# 窗口左右排列, N可省略
$ vim -O[N] <file1> <file2> ... <fileN>
```

## References

1. VIM Man Manual (`man 1 vim`)
