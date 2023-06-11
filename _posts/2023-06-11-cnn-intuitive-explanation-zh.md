---
layout    : post
title     : "[译] 以图像识别为例，关于卷积神经网络（CNN）的直观解释（2016）"
date      : 2023-06-11
lastupdate: 2023-06-11
categories: cnn ai
---

### 译者序

本文翻译自 2016 年的一篇文章：
[An Intuitive Explanation of Convolutional Neural Networks](https://ujjwalkarn.me/2016/08/11/intuitive-explanation-convnets/)。

作者以图像识别为例，用图文而非数学公式的方式解释了卷积神经网络的工作原理，
适合初学者和外行扫盲。

**译者水平有限，不免存在遗漏或错误之处。如有疑问，敬请查阅原文。**

以下是译文。

----

* TOC
{:toc}

----

# 1 卷积神经网络（CNN）

## 1.1 应用场景

卷积神经网络（ConvNets 或 CNN）是**<mark>一类</mark>**神经网络（a category of
[Neural Networks]("https://ujjwalkarn.me/2016/08/09/quick-intro-neural-networks/)），
在图像识别和分类等领域已经证明非常有效。CNN 已经成功用于
人脸识别、物体和交通标志识别，机器人视觉，自动驾驶等等。下面看两个具体例子。

图 1 是给一个图片，自动识别其中的内容并生成一句描述，

<p align="center"><img src="/assets/img/cnn-intuitive-explanation/1.png" width="80%" height="80%"></p>
<p align="center">图 1：图像识别和自动生成描述。<a href="http://cs.stanford.edu/people/karpathy/neuraltalk2/demo.html">cs.stanford.edu</a></p>

图 2 则是 CNN 用于识别日常人和物，

<p align="center"><img src="/assets/img/cnn-intuitive-explanation/2.png" width="80%" height="80%"></p>
<p align="center">图 2：图像识别。来自 <a href="https://arxiv.org/pdf/1506.01497v3.pdf">paper pdf</a></p>

此外，CNN 在一些自然语言处理任务（如句子分类）中也展现出很不错的效果。
因此，CNN 在机器学习领域将是一个非常重要的工具。不过，新手学习起来经常比较受挫。
本文试图拿 **<mark>CNN for image processing</mark>** 为例，向大家直观解释一下 CNN 是如何工作的。

> * 想了解 neural networks 可戳 <a href="https://ujjwalkarn.me/2016/08/09/quick-intro-neural-networks/"> this short tutorial on Multi Layer Perceptrons</a>，不了解也没关系。
> * Multi Layer Perceptrons（多层感知器/多层感知机）在本文中将称为 “Fully Connected Layers”。

## 1.2 起源：LeNet, 1990s

LeNet 由 Yann LeCun 在 1988 提出，是最早推动深度学习领域发展的卷积神经网络之一。
后来经过多次改进，直到 [LeNet5](http://yann.lecun.com/exdb/publis/pdf/lecun-01a.pdf) [3]。
当时 LeNet 架构主要用于**<mark>字符识别</mark>**，
例如读取邮政编码、数字等。

## 1.3 现代架构

近年来提出了几种新的架构，都是**<mark>对 LeNet 的改进</mark>**，它们都继承了 LeNet 的主要概念。
因此如果对 LeNet 比较了解，理解现代架构将容易很多。
下图展示了这类架构是**<mark>如何学习识别图像</mark>**的：

<p align="center"><img src="/assets/img/cnn-intuitive-explanation/3.png" width="95%" height="95%"></p>
<p align="center">图 3：基于卷积神经网络识别图像。<a href="https://www.clarifai.com/technology">image credit </a></p>

* 图中的卷积神经网络在架构上与早期 LeNet 差不多，它对输入图像进行分类（LeNet 主要用于字符识别）；
* 这个例子中会分成四类：dog, cat, boat, bird，所有概率的总和是 100%（后文会解释）；
* 从图中可以明显看出，在 boat 图像作为输入时，该网络最终的分类结果中，boat 的概率最高（0.94）。

根据上图，我们可以看到**<mark>现代 CNN 主要有四种操作</mark>**：

1. 卷积（Convolution）
1. 非线性（Non Linearity，ReLU)）
1. 池化或降采样（Pooling or Sub Sampling）
1. 分类/全连接层（Classification, Fully Connected Layer）

这几个功能是所有卷积神经网络的基本模块。下面我们尝试从直观上来理解每个操作的含义。

# 2 CNN：直观解释

## 2.1 输入：图像（像素值组成的矩阵）

每个图像（image）本质上就是一个由**<mark>像素值</mark>**（pixel values）组成的矩阵：

<p align="center"><img src="/assets/img/cnn-intuitive-explanation/8-gif.webp" width="35%" height="35%"></p>
<p align="center">图 4：每个图像都是一个像素值组成的矩阵。[6]</p>

[Channel](https://en.wikipedia.org/wiki/Channel_(digital_image))（通道）是一个传统术语，指图像的某一部分数据。
例如，

* 数码相机拍出的图像通常有三个通道 —— red/green/blue —— 可以想象成从下往上依次**<mark>堆叠的 3 个二维矩阵</mark>**（每种颜色一个），每个像素值都在 0 到 255 范围内。
* [grayscale](https://en.wikipedia.org/wiki/Grayscale)（灰度）图像只有一个通道。

简单起见，本文将只考虑**<mark>灰度图像</mark>**。
这意味着我们将有一个表示图像的二维矩阵，其中中每个像素的值范围从 0 到 255 —— 0 表示黑色，255 表示白色。

## 2.2 第一步：卷积运算

CNN（卷积神经网络）的名字来源于[“卷积”](http://en.wikipedia.org/wiki/Convolution)运算，

* 卷积的**<mark>主要目的</mark>**是**<mark>从输入图像中提取特征</mark>**；
* 通过一个**<mark>小矩阵</mark>**（称为 filter）对输入矩阵进行运算，来学习图像特征（image features）；
* filter 保留了像素之间的**<mark>空间关系</mark>**（spatial relationship between pixels）。

这里不深入探讨卷积的数学细节，只尝试了解它如何处理图像。

### 2.2.2 动图直观解释

每个图像都是像素值组成的矩阵。考虑一个 5x5 的图像，其像素值只有 0 和 1（灰度图像的像素值范围为 0 到 255，这里我们进一步简化，像素值只有 0 和 1）：

<p align="center"><img src="/assets/img/cnn-intuitive-explanation/binary-image.png" width="25%" height="25%"></p>

然后引入一个 3x3 矩阵作为 filter，

<p align="center"><img src="/assets/img/cnn-intuitive-explanation/3x3-matrix.png" width="15%" height="15%"></p>

那么，用 5x5 和 3x3 矩阵来计算卷积的过程就如下面的动图所示，

<p align="center"><img src="/assets/img/cnn-intuitive-explanation/convolution_schematic.webp" width="40%" height="40%"></p>
<p align="center">图 5：卷积的计算。计算得到的结果矩阵称为 Convolved Feature or Feature Map（卷积特征，或特征图）。[7]</p>

计算过程：

1. 在 5x5 的输入矩阵上，覆盖一个 3x3 矩阵，
2. 对当前 3x3 覆盖的部分，分别与 3x3 矩阵按像素相乘，然后把 9 个值加起来得到一个整数，
3. 按这种方式，以一个像素为单位依次滑动和计算，最后就得到一个输出矩阵（右边）。

用 CNN 术语来说，

1. 3x3 矩阵叫做 **<mark>filter（滤波器），或 kernel、feature detector 等</mark>**；
2. 滑动的粒度（这里是一个像素）叫做 **<mark>stride（步长）</mark>**；
2. 最后得到的矩阵叫 Convolved Feature（**<mark>卷积特征</mark>**）或 feature map（**<mark>特征图</mark>**）；

显然，对于同一个图像，**<mark>用的卷积矩阵（filter matrix）不同，得到的特征图（feature maps）也就不同</mark>**。
或者说**<mark>不同的 filter 可以检测不同的特征</mark>**。例如对于下面这张图片，

<p align="center"><img src="/assets/img/cnn-intuitive-explanation/animal.png" width="20%" height="20%"></p>

我们可以根据目的（边缘检测、锐化、模糊化等）的不同，选用不同的 filter，得到的效果如下 [8]，

<p align="center"><img src="/assets/img/cnn-intuitive-explanation/animal-filter-results.png" width="50%" height="50%"></p>

下面是另一个例子，用动图展示两个不同的 filter 计算卷积得到的 feature map，

<!-- this gif is too big, i won't like to include it into /assets/image/... as a static file, so reference it from vanilla URL -->
<p align="center"><img src="https://ujwlkarn.files.wordpress.com/2016/08/giphy.gif?w=480&zoom=2" width="60%" height="60%"></p>
<p align="center">图 6：卷积运算。Source [<a href="http://cs.nyu.edu/~fergus/tutorials/deep_learning_cvpr12/">9</a>]</p>

> 这里需要再提醒一下，image 和 filter 都只是一个数值矩阵（numeric matrix）。

### 2.2.2 特征图/卷积特征参数

实际上，CNN 在训练过程中自行**<mark>学习</mark>**（learn）这些 filter 的值
（尽管我们仍然需要在训练时指定某些参数，例如过 filter 数量、filter size、网络架构等）。
filter 数量越多，提取的图像特征就越多，训练出来的网络在识别未见过的图像时效果就越好。

Feature Map（Convolved Feature）的大小由三个参数 [4] 控制，在训练之前确定：

1. **<mark>深度</mark>**（depth）：将要用来做卷积运算的 **<mark>filter 数量</mark>**。

    在下图所示的网络中，我们使用三个不同的 filter 对原始图像执行卷积，从而生成三个不同的特征图。
    可以将这三个特征图视为三个自下向上**<mark>堆叠（stacking）在一起</mark>**的二维矩阵，
    这也是为什么成它为特征图的**<mark>“深度”</mark>**。

    <p align="center"><img src="/assets/img/cnn-intuitive-explanation/convolution-operation.png" width="60%" height="60%"></p>
    <p align="center">图 7：特征图的深度</p>

2. **<mark>步长</mark>**（stride）：在输入矩阵上滑动滤波器（filter）矩阵的**<mark>像素数</mark>**。

    * 步长为 1 就是一次将 filter 移动一个像素，步长为 2 就是一次滑动 2 个像素。
    * **<mark>步长越大，得到的特征图越小</mark>**。

3. **<mark>零值填充</mark>**（Zero-padding）：有时需要在图像边界用零填充，这样就可以对输入图像的边界像素应用 filter。

    * zero-padding 会使生成的特征图稍大一些；
    * 使用了 zero-padding 称为 **<mark>wide convolution</mark>**，不使用的称为 **<mark>narrow convolution</mark>** [14]。

## 2.3 第二步：非线性（Non Linearity / ReLU）运算

每次卷积运算之后，还会跟着一个称为 ReLU（**<mark>Rectified Linear Unit</mark>**，修正线性单元）的运算。
这是一个**<mark>非线性运算</mark>**，输入输出公式为，

<p align="center"><img src="/assets/img/cnn-intuitive-explanation/ReLU.png" width="70%" height="70%"></p>
<p align="center">图 8：ReLU 公式与效果（负值置为零）</p>

ReLU 是一个**<mark>像素级别的操作</mark>**，**<mark>将特征图中所有负值置为零</mark>**。
ReLU 的目的是在 CNN 中引入非线性，因为真实世界中的大部分数据都是非线性的，
我们希望 CNN 能与现实更加匹配。

> 卷积是一种线性运算 —— 逐像素做矩阵乘法和加法，因此我们通过引入像 ReLU 这样的非线性函数来解释（account for）非线性。

下图可以清楚地看到 ReLU 操作之后的效果，

<p align="center"><img src="/assets/img/cnn-intuitive-explanation/ReLU-result.png" width="85%" height="85%"></p>
<p align="center">图 9：真实特征图执行 ReLU 操作之后的效果。Source [<a href="http://mlss.tuebingen.mpg.de/2015/slides/fergus/Fergus_1.pdf">10</a>]</p>

* 左边的特征图是卷积运算之后得到的；
* 右边是接着做了 ReLU 运算（负数全部置零）之后的效果。也称为**<mark>“修正之后的”特征图</mark>**。

除了 ReLU，还可以使用 `tanh` 或 `sigmoid` 等**<mark>非线性函数</mark>**，不过在大部分场景下，ReLU 的性能都更好。

## 2.4 第三步：降采样

Spatial Pooling，又称为**<mark>降采样、下采样</mark>**（subsampling、downsampling），

* 目的是在降低 feature map 维度的同时，保留尽量多的重要信息。
* 有不同的类型：**<mark><code>Max/Average/Sum</code></mark>** 等。

### 2.4.1 原理：直观解释

图 10 是对一个卷积 + ReLU 之后得到的特征图执行 **<mark>2x2 降采样</mark>**，

<p align="center"><img src="/assets/img/cnn-intuitive-explanation/down-sampling.png" width="40%" height="40%"></p>
<p align="center">图 10: Max Pooling. Source [<a href="http://cs231n.github.io/convolutional-networks/">4</a>]</p>

* 首先定义一个 spatial neighborhood (2x2 窗口)，
* 然后执行降采样函数，这里用的是 `Max` 函数，也就是取其中最大的那个值作为结果，
* 注意这里滑动窗口的步长是 2，也就是降采样矩阵的尺寸；

最终**<mark>把 4x4 特征图降维到了 2x2</mark>**。

下图是对三个卷积结果（rectified feature map）依次执行降采样，

<p align="center"><img src="/assets/img/cnn-intuitive-explanation/down-sampling-2.png" width="50%" height="50%"></p>
<p align="center">图 11：对三个维度的特征图执行降采样。</p>

Average Pooling 和 sum 类似，只不过取的是平均值和累加和。
实际中，Max Pooling 效果更好一些。下面是一个效果对比：

<p align="center"><img src="/assets/img/cnn-intuitive-explanation/down-sampling-3.png" width="80%" height="80%"></p>
<p align="center">图 12：Max/Sum 降采样函数效果对比。Source [<a href="http://mlss.tuebingen.mpg.de/2015/slides/fergus/Fergus_1.pdf">10</a>]</p>

Pooling 的作用是主动降低输入表示（input representation）的空间大小（spatial size） [4]，

1. 使输入表示**<mark>（特征维度）更小</mark>**且更易于管理；
1. **<mark>减少网络参数和计算量</mark>**，避免过度拟合（overfitting）[4]；
1. 使网络能**<mark>容忍输入图像中较小的变换、扭曲和平移</mark>**：小扭曲不会改变降采样的输出 —— 因为我们采用相邻区域内的最大值/平均值；
1. 使我们能获得一个几乎**<mark>跟输入分辨率无关的特征表示</mark>**（确切的术语是 "equivariant representation"）：
  这一点非常重要，因有了这种能力，那无论一个目标是在图像中什么位置，我们都能把它检测出来 [18,19]。

### 2.4.2 小结：卷积层 + ReLU + 降采样层

至此，我们已经了解了卷积、ReLU 和降采样的工作原理，它们是构成任何 CNN 的基本构建块。
如图 13 所示，

<p align="center"><img src="/assets/img/cnn-intuitive-explanation/basic-block.png" width="80%" height="80%"></p>
<p align="center">图 13：两个基本处理单元构成的 CNN</p>

* **<mark>卷积层 + ReLU + 降采样层</mark>**构成一个**<mark>基本处理单元</mark>**；
* 图中级联了两个这样的处理单元。

这些层联合在一起，从图像中提取有用特征、引入非线性、减少特征维度，同时在使特征在某种程度上做到**<mark>缩放和平移不变</mark>** [18]。
降采样之后的输出就可以对图像分类了，具体的分类过程由称为**<mark>全连接层</mark>**的模块来完成。

## 2.5 第四步：全连接层：基于特征分类

全连接层（Fully Connected Layer）是一种传统的**<mark>多层感知器</mark>**（Multi Layer Perceptron），
在输出层使用 **<mark><code>softmax()</code></mark>** 函数（也可以使用 SVM 等其他分类器，但本文中使用 softmax）。 
术语“全连接”表示**<mark>上一层中的每个神经元都连接到下一层中的每个神经元</mark>**。

> 想了解多层感知器，可参考[这篇](https://ujjwalkarn.me/2016/08/09/quick-intro-neural-networks/)文章。

卷积层和降采样层的输出表示了输入图像的 high-level 特征。
**<mark>全连接层的目的</mark>**是基于这些特征，**<mark>对输入图像进行分类</mark>**。

例如，假设我们的图像分类有四种可能，如下图所示（图中省略了全连接层中节点之间的连接），

<p align="center"><img src="/assets/img/cnn-intuitive-explanation/full-connected-layer.png" width="80%" height="80%"></p>
<p align="center">图 14：全连接层</p>

除了分类的作用，加一个全连接层（通常）还是一种**<mark>低成本的学习这些特性的非线性组合</mark>**的方式
（learning non-linear combinations of these features）。
卷积层和降采样层的输出对于分类来说可能已经很好了，但组合二者能达到更好的性能 [11]。

全连接层 output 的所有可能性加起来是 100%，这是由 softmax activation function 保证的：
softmax 接收任何实值向量输入，将其中的元素归一化到 [0,1] 之间，并且总和等于 1。

## 2.6 第五步：反向传播：形成闭环

分类结果（各种可能性的概率）出来之后，再
使用反向传播（backpropagation）算法计算所有概率的误差梯度，
并使用**<mark>梯度下降算法</mark>**更新所有滤波器值/权重和参数值，以最小化输出误差。

然后就又可以从头开始了，有了这种反馈，下一次的迭代会更加准确。

# 3 CNN 完整架构和工作流

**<mark>卷积层 + 降采样层</mark>**充当输入图像的**<mark>特征提取器</mark>**（Feature Extractors），
而**<mark>全连接层</mark>**充当**<mark>分类器</mark>**（classifier）。

下图中，由于输入图像是 boat，因此 Boat 类的目标概率为 1，其他三个类的目标概率为 0，即

1. Input Image = Boat
1. Target Vector = [0, 0, 1, 0]

<p align="center"><img src="/assets/img/cnn-intuitive-explanation/train-cnn.png" width="80%" height="80%"></p>
<p align="center">图 15: Training the CNN</p>

卷积网络的训练过程大致如下：

1. 用**<mark>随机值</mark>**初始化所有 filter 和参数/权重。
2. 将训练图像作为输入，经过 **<mark>forward propagation</mark>** 步骤（卷积、
   ReLU 和降采样以及全连接层中的前向传播），**<mark>计算出每个类别的输出概率</mark>**。

    * 假设上面的 boat 图像的输出概率是 [0.2, 0.4, 0.1, 0.3]
    * 由于第一个训练示例的权重是随机分配的，因此输出概率也是随机的。

3. 计算输出层的总误差（所有 4 个类的总和）

    * <strong>Total Error = ∑  ½ (target probability – output probability) ²</strong>

4. 使用反向传播计算网络中所有权重的误差梯度，并使用**<mark>梯度下降算法</mark>**
  更新所有滤波器值/权重和参数值，以最小化输出误差。

    * 权重根据它们对总误差的贡献按比例调整。
    * 当再次输入相同的图像时，输出概率可能就是 [0.1, 0.1, 0.7, 0.1]，更接近目标向量 [0, 0, 1, 0]，
      这意味着网络已经学会通过调整其权重/过滤器来正确分类该特定图像，从而减少输出误差。
    * 过滤器数量、过滤器大小、网络架构等参数在步骤 1 之前都已固定，并且在训练过程中不会更改——只有过滤器矩阵和连接权重的值会更新。

5. 对训练集中的所有图像重复步骤 2-4。

以上就是训练 CNN 的步骤，结束之后 CNN 的所有权重和参数都已经过优化，能够正确地对训练集中的图像进行分类。

当一个新的（没见过的）图像被输入到 CNN 中时，网络将通过 forward propagation
计算输出每个类别的概率。 如果训练集足够大，网络将大概率将新图像分类到正确的类别中。

> 以上步骤已经过简化，以避免过多数学细节，让读者对训练过程有个直觉上的理解。
> 想了解进一步有关细节，可参考 [4,12]。

上面的例子中使用了两组卷积层和降采样层。实际上可以在单个 CNN 中重复任意层。
另外，每个卷积层之后不是必须要跟着一个降采样层。如图 16 所示，

<p align="center"><img src="/assets/img/cnn-intuitive-explanation/car.png" width="80%" height="80%"></p>
<p align="center">图 16：多层 CNN 及每层的效果。Source [<a href="http://cs231n.github.io/convolutional-networks/">4</a>]</p>

# 4 案例：CNN 学习识别字符 `8`

## 4.1 多层特征

一般来说，卷积步骤越多，网络能学到的特征就可以越复杂。 例如，在图像分类中，CNN
可能会

* 在第一层学习从原始像素**<mark>检测物体边缘</mark>**，然后
* 基于边缘检测判断第二层中的**<mark>简单形状</mark>**，然后
* 使用这些形状来**<mark>检测更高级别的特征</mark>**，例如**<mark>面部形状</mark>** [14]。

下图是用
<a href="http://web.eecs.umich.edu/~honglak/icml09-ConvolutionalDeepBeliefNetworks.pdf">Convolutional Deep Belief Network</a>
学习到的特征，

<p align="center"><img src="/assets/img/cnn-intuitive-explanation/deep-belief.png" width="50%" height="50%"></p>
<p class="graf--p graf-after--p" style="text-align:center;">图 17：Convolutional Deep Belief Network 学习特征：
第一层检测边缘；第二次检测形状；第三张检测人脸。
Source [<a href="http://web.eecs.umich.edu/~honglak/icml09-ConvolutionalDeepBeliefNetworks.pdf">21</a>]</p>

> 这里只是一个简单的例子，现实中的卷积滤波器可能会检测到对人类无意义的物体。

## 4.2 学习字符 `8`

> Adam Harley 在 MNIST 手写数字数据库 [13] 上训练卷积神经网络，创造了惊人的可视化效果。

下面看一个 CNN 网络如何处理输入字符 “8”。输入图像包含 1024 像素（32x32），

<p align="center"><img src="/assets/img/cnn-intuitive-explanation/conv-all.png" width="80%" height="80%"></p>
<p align="center">图 18: Visualizing a CNN trained on handwritten digits. 
注意 ReLU 没画出来。
Source [<a href="http://scs.ryerson.ca/~aharley/vis/conv/flat.html">13</a>]</p>

1. 第一层卷积：由 6 个独立的 5×5（步长 1）滤波器与输入图像卷积，产生**<mark>深度为 6 </mark>**的特征图。
2. 第一层降采样：分别对 1 中的六个特征图进行 **<mark>2×2 Max 降采样</mark>**（步长为 2）。
  注意由于用的是 Max，因此 2x2 网格中的最大值（**<mark>最亮的像素</mark>**）的像素进入了降采样结果层。

    <p align="center"><img src="/assets/img/cnn-intuitive-explanation/8-downsample.png" width="60%" height="60%"></p>
    <p align="center">图 19：卷积 + 降采样。Source [<a href="http://scs.ryerson.ca/~aharley/vis/conv/flat.html">13</a>]</p>

3. 第二层卷积：对 2 的结果进行 **<mark>5x5 卷积</mark>**（步长 1）；
4. 第二层降采样：对 3 的结果进行**<mark>降采样</mark>**；
5. 第一层全连接：120 neurons；
6. 第二层全连接：100 neurons；
7. 第三层全连接：10 neurons，分别代表 **<mark>0-9 这 10 个字符</mark>**作为输出 —— 也称为 Output layer

    注意到 output 层中 node `8` **<mark>最亮</mark>**，这表示分类结果中 `8` 的概率最大。

    <p align="center"><img src="/assets/img/cnn-intuitive-explanation/8-output.png" width="100%" height="100%"></p>
    <p align="center">图 20: Visualizing the Filly Connected Layers. Source [<a href="http://scs.ryerson.ca/~aharley/vis/conv/flat.html">13</a>]</p>

# 5 其他 CNN 架构

上世纪 90 年代初期就出现卷积神经网络了，本文介绍的 LeNet 只是其中一种，

1. **<mark>LeNet (1990s)</mark>**: 本文介绍的就是这个；
1. <strong>1990s to 2012:</strong> In the years from late 1990s to early 2010s
   convolutional neural network were in incubation. As more and more data and
   computing power became available, tasks that convolutional neural networks
   could tackle became more and more interesting.
1. <strong>AlexNet (2012) &#8211; </strong>In 2012, Alex Krizhevsky (and
   others) released <a href="https://papers.nips.cc/paper/4824-imagenet-classification-with-deep-convolutional-neural-networks.pdf">AlexNet</a>
   which was a deeper and much wider version of the LeNet and won by a large
   margin the difficult ImageNet Large Scale Visual Recognition Challenge
   (ILSVRC) in 2012. It was a significant breakthrough with respect to the
   previous approaches and the current widespread application of CNNs can be
   attributed to this work.
1. <strong>ZF Net (2013) &#8211;</strong> The ILSVRC 2013 winner was a
   Convolutional Network from Matthew Zeiler and Rob Fergus. It became known as
   the <a href="http://arxiv.org/abs/1311.2901">ZFNet</a> (short for Zeiler &
   Fergus Net). It was an improvement on AlexNet by tweaking the architecture
   hyperparameters.
1. <strong>GoogLeNet (2014) &#8211; </strong>The ILSVRC 2014 winner was a
   Convolutional Network from <a href="http://arxiv.org/abs/1409.4842">Szegedy
   et al.</a> from Google. Its main contribution was the development of an
   *Inception Module* that dramatically reduced the number of parameters in the
   network (4M, compared to AlexNet with 60M).
1. <strong>VGGNet (2014) &#8211;</strong> The runner-up in ILSVRC 2014 was the
   network that became known as the <a href="http://www.robots.ox.ac.uk/~vgg/research/very_deep/">VGGNet</a>. Its
   main contribution was in showing that the depth of the network (number of
   layers) is a critical component for good performance.
1. **<mark>ResNets (2015)</mark>** <a href="http://arxiv.org/abs/1512.03385">Residual Network</a> developed by
   Kaiming He (and others), winner of ILSVRC 2015. 目前最好的 CNN 模型（as of May 2016）。
1. <strong>DenseNet (August 2016) &#8211; </strong>Recently published by Gao
   Huang (and others), the <a href="http://arxiv.org/abs/1608.06993" >Densely Connected Convolutional Network</a>
    has each layer directly connected to every other layer in a
   feed-forward fashion. The DenseNet has been shown to obtain significant
   improvements over previous state-of-the-art architectures on five highly
   competitive object recognition benchmark tasks. Check out the Torch
   implementation <a href="https://github.com/liuzhuang13/DenseNet">here</a>.

# 6 总结

本文试图用尽量简单的术语来解释卷积神经网络（CNN）背后的主要概念，
希望能让读者对其工作原理有一些直观理解。

# 参考资料

1. <a href="https://github.com/karpathy/neuraltalk2">karpathy/neuraltalk2</a>: Efficient Image Captioning code in Torch, <a href="http://cs.stanford.edu/people/karpathy/neuraltalk2/demo.html">Examples</a>
1. Shaoqing Ren, *et al, *&#8220;Faster R-CNN: Towards Real-Time Object Detection with Region Proposal Networks&#8221;, 2015, <a href="http://arxiv.org/pdf/1506.01497v3.pdf">arXiv:1506.01497 </a>
1. <a href="https://medium.com/towards-data-science/neural-network-architectures-156e5bad51ba">Neural Network Architectures</a>, Eugenio Culurciello&#8217;s blog
1. <a href="http://cs231n.github.io/convolutional-networks/">CS231n Convolutional Neural Networks for Visual Recognition, Stanford</a>
1. <a href="https://www.clarifai.com/technology">Clarifai / Technology</a>
1. <a href="https://medium.com/@ageitgey/machine-learning-is-fun-part-3-deep-learning-and-convolutional-neural-networks-f40359318721#.2gfx5zcw3">Machine Learning is Fun! Part 3: Deep Learning and Convolutional Neural Networks</a>
1. <a href="http://deeplearning.stanford.edu/wiki/index.php/Feature_extraction_using_convolution">Feature extraction using convolution, Stanford</a>
1. <a href="https://en.wikipedia.org/wiki/Kernel_(image_processing)">Wikipedia article on Kernel (image processing) </a>
1. <a href="http://cs.nyu.edu/~fergus/tutorials/deep_learning_cvpr12">Deep Learning Methods for Vision, CVPR 2012 Tutorial </a>
1. <a href="http://mlss.tuebingen.mpg.de/2015/slides/fergus/Fergus_1.pdf">Neural Networks by Rob Fergus, Machine Learning Summer School 2015</a>
1. <a href="http://stats.stackexchange.com/a/182122/53914">What do the fully connected layers do in CNNs? </a>
1. <a href="http://andrew.gibiansky.com/blog/machine-learning/convolutional-neural-networks/">Convolutional Neural Networks, Andrew Gibiansky </a>
1. A. W. Harley, &#8220;An Interactive Node-Link Visualization of Convolutional Neural Networks,&#8221; in ISVC, pages 867-877, 2015 (<a href="http://scs.ryerson.ca/~aharley/vis/harley_vis_isvc15.pdf">link</a>). <a href="http://scs.ryerson.ca/~aharley/vis/conv/flat.html">Demo</a>
1. <a href="http://www.wildml.com/2015/11/understanding-convolutional-neural-networks-for-nlp/">Understanding Convolutional Neural Networks for NLP</a>
1. <a href="http://andrew.gibiansky.com/blog/machine-learning/convolutional-neural-networks/">Backpropagation in Convolutional Neural Networks</a>
1. <a href="https://adeshpande3.github.io/adeshpande3.github.io/A-Beginner's-Guide-To-Understanding-Convolutional-Neural-Networks-Part-2/">A Beginner&#8217;s Guide To Understanding Convolutional Neural Networks</a>
1. Vincent Dumoulin, *et al*, &#8220;A guide to convolution arithmetic for deep learning&#8221;, 2015, <a href="http://arxiv.org/pdf/1603.07285v1.pdf">arXiv:1603.07285</a>
1. <a href="https://github.com/rasbt/python-machine-learning-book/blob/master/faq/difference-deep-and-normal-learning.md">What is the difference between deep learning and usual machine learning?</a>
1. <a href="https://www.quora.com/How-is-a-convolutional-neural-network-able-to-learn-invariant-features">How is a convolutional neural network able to learn invariant features?</a>
1. <a href="http://journal.frontiersin.org/article/10.3389/frobt.2015.00036/full">A Taxonomy of Deep Convolutional Neural Nets for Computer Vision</a>
1. Honglak Lee, *et al*, &#8220;Convolutional Deep Belief Networks for Scalable Unsupervised Learning of Hierarchical Representations&#8221; (<a href="http://web.eecs.umich.edu/~honglak/icml09-ConvolutionalDeepBeliefNetworks.pdf">link</a>)
