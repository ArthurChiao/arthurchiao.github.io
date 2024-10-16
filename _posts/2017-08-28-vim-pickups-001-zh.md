---
layout    : post
title     : "Vim Pickups 001（2017）"
date      : 2017-08-28
lastupdate: 2024-06-30
---

整理点 vim 小技巧。如无特殊说明，本文所列命令都是 VIM 内置的，无需额外插件。

----

* TOC
{:toc}

----

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

```
$ vimdiff file1 file2

]c 跳到下一个差异点
[c 跳到前一个
:diffupdate 手动重新加载文件内容（正常会自动加载）
```


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

## 3.2 删除空行


1. `g/^\s*$/d`：以空格类字符（`^\s`）开头，连续匹配直到行结束（`*$`），删除（`d`）。

    https://stackoverflow.com/questions/706076/vim-delete-blank-lines

## 3.3 删除匹配行前后 n/m 行

来自 [Deleting a range of n lines before and after a matched
line?](https://vi.stackexchange.com/questions/3411/deleting-a-range-of-n-lines-before-and-after-a-matched-line)：

1. `:%g/<pattern>/-10,+28d`：找到包含 `<pattern>` 的行，然后从该行前面 10 行一直删除到该行后面 28 行。
1. `:%g/<pattern>/-10,.d`：删除从匹配行 `-10` 行直到当前行，小数点表示当前行。

## 3.4 删除不匹配的行 `:g!/<pattern>/d`

## 3.5 删除文档中所有空行

```
:g/^\s*$/d     # 删除空行，其中可能包括若干个空格或 tab
:g/^$/d        # 删除纯空行（其中不包含空格或 tab）

:%s/\s\+$//g   # 删除 trailing 空格或 tab

:g!/pattern/d  # 删除不匹配的行
```

# 4 替换（substitution）

## 4.1 将多个连续空行合并为一行

1. 通用方式，[将多个连续空行合并为一行](https://vim.fandom.com/wiki/Remove_unwanted_empty_lines)：`:%s/\n\{3,}/\r\r/e`
2. Linux 上，[用 cat 命令合并空行](https://stackoverflow.com/questions/706076/vim-delete-blank-lines), `:%!cat -s`。

    `-s, --squeeze-blank`: suppress repeated empty output lines。

## 4.2 追加相同内容到所有行

几种方式：

1. `%s/$/<your content>/gc`
2. 用替换 \n 实现，例如，在每行后面追加一个分号，注意后面的 pattern 里换行符得用 \r：`%s/\n/;\r/gc`

## 4.3 将 windows 下的换行（^M） 替换为 Linux 换行

`:%s/<Ctrl-V><Ctrl-M>/\r/g`

`<C-V><C-M>` 才能按出 ^M 符号。

## 4.4 将指定 pattern 替换为换行符

搜索文本中的换行符需要使用 `\n` 作为 pattern，
替换需要使用 **<mark><code>\r</code></mark>** 作为换行符，`\n`不行：

`:%s/<pattern>/\r/gc`

# 5 待归类

## 插入内容时不要将 tab 自动替换为空格

```
:set noexpandtab
:set expandtab
:set list 能看到到底是 tab 还是空格
```

## 匹配直到某个 pattern 出现

https://stackoverflow.com/questions/3625596/vim-select-until-first-match/3625645

使用非贪婪搜索（non-greedy）模式：**<mark><code>{-}</code></mark>**

* `/^.\{-\}hello`   从头开始匹配，直到遇到 hello
* `%s/<h1.\{-\}>/# /g`   将 h1 tag 替换为一个 # 加空格。例如
  `<h1 id="xx" name="xx">Head 1</h1>` 会被替换为 `# Head 1 </h1>`，
  再将 </h1> 替换为空，就实现了将 html 元素转为 markdown 元素的目的。

## 另存为文件

`:saveas <filename>`

## 统计字数、行数等：**<mark><code>g Ctl-g</code></mark>**

https://superuser.com/questions/149854/how-can-i-get-gvim-to-display-the-character-count-of-the-current-file

先按 g，再按 Ctl-g。效果：

```
Col 1 of 113-77; Line 19 of 217; Word 30 of 434; Char 261 of 5084; Byte 383 of 6104
```

## 计算

### 计算表达式并将结算插入到光标位置

插入模式下：`<ctl-r>=`（先按 ctl-r，再按 `=`） 进入编辑表达式状态，再输入 `3*2` 之后，结果 `6` 就会插入到光标位置。
这是使用了 Vim 表达式寄存器 `"=`。

### 操作数字（`+1/-1`）：`ctl-a/ctl-x`

```
c-a 对光标下的数字 +1
c-x 对光标下的数字 -1
visusal 选中列，g c-a 将依次加 1，例如 1/1/1 变成 2/3/4
```

## 重绘窗口，将光标所在行移动到平面正中间、顶部、底部

* `zz`：移动到正中间
* `zt`：移动到顶部（top）
* `zb`：移动到底部（bottom）

# References

1. VIM Man Manual (`man 1 vim`)
