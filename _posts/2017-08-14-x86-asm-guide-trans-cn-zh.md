---
layout: post
title:  "[译] 简明x86汇编指南"
date:   2017-08-14
last_modified: 2017-08-25
categories: assembly
---

Translated from [CS216, University of Virginia](http://www.cs.virginia.edu/~evans/cs216/guides/x86.html).

一份非常好的x86汇编教程，国外CS课程所用资料，篇幅简短，逻辑清晰，合适作为
入门参考。以原理为主，有两个例子帮助理解。其开始提到使用
MicroSoft MASM和VisualStudio，但非必须, 事实上如果你有Linux更好。


**本文根据原文内容意译，并非逐词逐句翻译，如需了解更多，推荐阅读**[原文](http://www.cs.virginia.edu/~evans/cs216/guides/x86.html).

---

内容：**寄存器, 内存和寻址, 指令, 函数调用约定（Calling Convention）**

本文介绍**32bit x86汇编**基础，覆盖其中虽小但很有用的一部分。
有多种汇编语言可以生成x86机器代码。我们在CS216课程中使用的是MASM（
Microsoft Macro Assembler）。MASM使用标准Intel语法。

整套x86指令集庞大而复杂（Intel x86指令集手册超过2900页），本文不会全部覆盖。

## 1. 参考资料

* Guide to Using Assembly in Visual Studio — a tutorial on building and debugging assembly code in Visual Studio
* Intel x86 Instruction Set Reference
* Intel's Pentium Manuals (the full gory details)

## 2. 寄存器

<p align="center"><img src="/assets/img/x86-asm-guide/x86-registers.png" width="50%" height="50%"></p>
<p align="center">Fig 2.1 x86 registers</p>

现代x86处理器有8个32 bit寄存器，如图1所示。寄存器名字是早期计算机历史上流传下来的。例如，
EAX表示Accumulator，因为它用作算术运算的累加器，ECX表示Counter，用来存储循环
变量（计数）。大部分寄存器的名字已经失去了原来的意义，但有两个是例外：栈指针寄存
器（Stack Pointer）ESP和基址寄存器（ Base Pointer）EBP。

对于`EAX`, `EBX`, `ECX`, `EDX`四个寄存器，可以再将32bit划分成多个子寄存器，每个
子寄存器有专门的名字。例如`EAX`的高16bit叫`AX`（去掉E, E大概表示**Extended**）,
低8bit叫`AL` (**Low**）, 8-16bit叫`AH` （**High**）。如图1所示。

在汇编语言中，这些寄存器的名字是**大小写无关**的，既可以用`EAX`，也可以写`eax`。

## 3. 内存和寻址模式

### 3.1 声明静态数据区

`.DATA`声明静态数据区。

数据类型修饰原语：

* **DB**: Byte,        1 Byte
* **DW**: Word,        2 Bytes
* **DD**: Double Word, 4 Bytes

例子：

```
.DATA
var     DB 64    ; 声明一个byte值, referred to as location var, containing the value 64.
var2    DB ?     ; 声明一个未初始化byte值, referred to as location var2.
        DB 10    ; 声明一个没有label的byte值, containing the value 10. Its location is var2 + 1.
X       DW ?     ; 声明一个2-byte未初始化值, referred to as location X.
Y       DD 30000 ; 声明一个4-byte值, referred to as location Y, initialized to 30000.
```

和高级语言不同，**在汇编中只有一维数组**，只有没有二维和多维数组。一维数组其实就
是内存中的有空连续区域。另外，`DUP`和字符串常量也是声明数组的两种方法。

例子：

```
Z       DD 1, 2, 3      ; 声明3个 4-byte values, 初始化为 1, 2, and 3. The value of location Z + 8 will be 3.
bytes   DB 10 DUP(?)    ; 声明10个 uninitialized bytes starting at location bytes.
arr     DD 100 DUP(0)   ; 声明100个 4-byte words starting at location arr, all initialized to 0
str     DB 'hello',0    ; 声明 6 bytes starting at the address str, 初始化为 hello and the null (0) byte.
```

### 3.2 内存寻址 (Addressing Memory)

有多个指令可以用于内存寻址，我们先看使用`MOV`的例子。MOV将在内存和寄存器之间移动
数据，接受两个参数：第一个参数是目的地，第二个是源。

合法寻址的例子：

```
mov eax, [ebx]        ; Move the 4 bytes in memory at the address contained in EBX into EAX
mov [var], ebx        ; Move the contents of EBX into the 4 bytes at memory address var. (Note, var is a 32-bit constant).
mov eax, [esi-4]      ; Move 4 bytes at memory address ESI + (-4) into EAX
mov [esi+eax], cl     ; Move the contents of CL into the byte at address ESI+EAX
mov edx, [esi+4*ebx]  ; Move the 4 bytes of data at address ESI+4*EBX into EDX
```

**非法寻址**的例子：

```
mov eax, [ebx-ecx]      ; 只能对寄存器的值相加，不能相减
mov [eax+esi+edi], ebx  ; 最多只能有2个寄存器参与地址计算
```

### 3.3 数据类型(大小)原语（Size Directives）

修饰**指针**类型：

* `BYTE PTR` - 1 Byte
* `WORD PTR` - 2 Bytes
* `DWORD PTR` - 4 Bytes

```
mov BYTE PTR [ebx], 2   ; Move 2 into the single byte at the address stored in EBX.
mov WORD PTR [ebx], 2   ; Move the 16-bit integer representation of 2 into the 2 bytes starting at the address in EBX.
mov DWORD PTR [ebx], 2  ; Move the 32-bit integer representation of 2 into the 4 bytes starting at the address in EBX.
```

## 4. 指令

三大类：

* 数据移动
    1. `mov`
    1. `push`
    1. `pop`
    1. `lea` - Load Effective Address
* 算术/逻辑运算
    1. `add`, `sub`
    1. `inc`, `dec`
    1. `imul`, `idiv`
    1. `and`, `or`, `xor`
    1. `not`
    1. `neg`
    1. `shl`, `shr`
* 控制流
    1. `jmp`
    1. `je`, `jne`, `jz`, `jg`, `jl` ...
    1. `cmp`
    1. `call`, `ret`

## 5. 调用约定

**这是最重要的部分。**

子过程（函数）调用需要遵守一套共同的**调用约定**（***Calling Convention***）。**
调用约定是一个协议，规定了如何调用以及如何从过程返回**。例如，给定一组calling
convention rules，程序员无需查看子函数的定义就可以确定如何将参数传给它。进一步地
，给定一组calling convention rules，高级语言编译器只要遵循这些rules，就可以使得
汇编函数和高级语言函数互相调用。

Calling conventions有多种。我们这里介绍使用最广泛的一种：**C语言调用约定**（***C
Language Calling Convention***）。遵循这个约定，可以使汇编代码安全地被C/C++调用
，也可以从汇编代码调用C函数库。

C调用约定:

* 强烈依赖**硬件栈**的支持(hardwared-supported stack)
* 基于`push`, `pop`, `call`, `ret`指令
* 子过程**参数通过栈传递**: 寄存器保存在栈上，子过程用到的局部变量也放在栈上

在大部分处理器上实现的大部分高级过程式语言，都使用与此相似的调用惯例。

调用惯例分为两部分。第一部分用于 **调用方**（***caller***），第二部分用于**被调
用方**（***callee***）。需要强调的是，错误地使用这些规则将导致**栈被破坏**，程序
很快出错；因此在你自己的子过程中实现calling convention时需要格外仔细。

<p align="center"><img src="/assets/img/x86-asm-guide/stack-convention.png" width="60%" height="60%"></p>
<p align="center">Fig 5.1 Stack during Subroutine Call</p>

### 5.1 调用方规则 (Caller Rules)

在一个子过程调用之前，调用方应该：

1. **保存应由调用方保存的寄存器**（***caller-saved*** registers): `EAX`, `ECX`,
   `EDX`

    这几个寄存器可能会被被调用方（callee）修改，所以先保存它们，以便调用结
    束后恢复栈的状态。

1. **将需要传给子过程的参数入栈**（push onto stack)

    参数按**逆序**push入栈（最后一个参数先入栈）。由于栈是向下生长的，第一个参数
    会被存储在最低地址（**这个特性使得变长参数列表成为可能**）。

1. **使用`call`指令，调用子过程(函数）**

    `call`先将返回地址push到栈上，然后开始执行子过程代码。子过程代码需要遵
    守的callee rules。

子过程返回后（`call`执行结束之后），被调用方会将返回值放到`EAX`寄存器，调用方可
以从中读取。为恢复机器状态，调用方需要做：

1. **从栈上删除传递的参数**

    栈恢复到准备发起调用之前的状态。

1. **恢复由调用方保存的寄存器**（`EAX`, `ECX`, `EDX`）——从栈上pop出来

    调用方可以认为，除这三个之外，其他寄存器的值没有被修改过。

#### 例子

```
push [var] ; Push last parameter first
push 216   ; Push the second parameter
push eax   ; Push first parameter last

call _myFunc ; Call the function (assume C naming)

add esp, 12
```

### 5.2 被调用方规则 (Callee Rules)

1. **将寄存器`EBP`的值入栈，然后copy `ESP` to `EBP`**

   ```
   push ebp
   mov  ebp, esp
   ```

1. **在栈上为局部变量分配空间**

    栈自顶向下生长，故随着变量的分配，栈顶指针不断减小。

1. **保存应有被调用方保存（`callee-saved`）的寄存器** —— 将他们压入栈。包括`EBX`,
   `EDI`, `ESI`

以上工作完成，就可以执行子过程的代码了。当子过程返回后，必须做以下工作：

1. **将返回值保存在`EAX`**

1. **恢复应由被调用方保存的寄存器**(`EDI`, `ESI`) —— 从栈上pop出来

1. **释放局部变量**

1. **恢复调用方base pointer `EBP` —— 从栈上pop出来**

1. **最后，执行`ret`，返回给调用方(caller)**

#### 例子

```
.486
.MODEL FLAT
.CODE
PUBLIC _myFunc
_myFunc PROC
  ; Subroutine Prologue
  push ebp     ; Save the old base pointer value.
  mov ebp, esp ; Set the new base pointer value.
  sub esp, 4   ; Make room for one 4-byte local variable.
  push edi     ; Save the values of registers that the function
  push esi     ; will modify. This function uses EDI and ESI.
  ; (no need to save EBX, EBP, or ESP)

  ; Subroutine Body
  mov eax, [ebp+8]   ; Move value of parameter 1 into EAX
  mov esi, [ebp+12]  ; Move value of parameter 2 into ESI
  mov edi, [ebp+16]  ; Move value of parameter 3 into EDI

  mov [ebp-4], edi   ; Move EDI into the local variable
  add [ebp-4], esi   ; Add ESI into the local variable
  add eax, [ebp-4]   ; Add the contents of the local variable
                     ; into EAX (final result)

  ; Subroutine Epilogue 
  pop esi      ; Recover register values
  pop  edi
  mov esp, ebp ; Deallocate local variables
  pop ebp ; Restore the caller's base pointer value
  ret
_myFunc ENDP
END
```

## References
