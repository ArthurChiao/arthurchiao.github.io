---
layout    : post
title     : "GPU Performance (Data Sheets) Quick Reference (2023)"
date      : 2023-10-25
lastupdate: 2023-11-03
categories: gpu
---

This post provides a concise reference for the performance of popular GPU
models from NVIDIA and Huawei/HiSilicon, primarily intended for personal use.

----

* TOC
{:toc}

----

# 1 Introduction

## Naming convention of NVIDIA GPUs

The first letter in GPU model names denote their GPU architectures, with:

1. **<mark><code>T</code></mark>** for Turing;
1. **<mark><code>A</code></mark>** for Ampere;
1. **<mark><code>V</code></mark>** for Volta;
1. **<mark><code>H</code></mark>** for Hopper; 2022
1. **<mark><code>L</code></mark>** for Ada Lovelace;

# 2 Comparison of `L2/T4/A10/A10G/V100`

|                    | L2               | T4               | A10                | A10G       | A30        | V100 PCIe/SMX2 |
|:-------------------|:-----------------|:-----------------|:-------------------|:-----------|:-----------|:--------|
| Designed for       | Data center      |Data center       | (Desktop) **<mark>Graphics-intensive</mark>** workloads | Desktop | Desktop | Data center |
| Year               | 2023             | 2018             | 2020               |            |            | 2017         |
| Manufacturing      |                  | 12nm             | 12nm               | 12nm       |            |              |
| Architecture       | Ada Lovelace     | Turing           | Ampere             | Ampere     | Ampere     | Volta        |
| Max Power          |                  | 70 watts         | 150 watts          |            | 165 watts  | 250/300watts |
| GPU Mem            | 24GB GDDR6       | 16GB GDDR6       | 24GB GDDR6         | 48GB GDDR6 | 24GB HBM2  | 16/32GB <mark>HBM2</mark> |
| GPU Mem BW         | 300 GB/s         | 400 GB/s         | 600 GB/s           |            | **<mark><code>933GB/s</code></mark>**  | **<mark><code>900 GB/s</code></mark>** |
| Interconnect       | PCIe Gen4 64GB/s | PCIe Gen3 32GB/s | PCIe Gen4 66 GB/s  |            | PCIe Gen4 64GB/s, NVLINK 200GB/s | PCIe Gen3 32GB/s, NVLINK **<mark><code>300GB/s</code></mark>** |
| FP32               | 24.1 TFLOPS      | 8.1 TFLOPS       | 31.2 TFLOPS        |            | 10.3TFLOPS | 14/15.7 TFLOPS |
| TF32               | 48.3 TFLOPS      |                  |                    |            |            |                |
| BFLOAT16 TensorCore| 95.6 TFLOPS      |                  | 125 TFLOPS         |            | 165 TFLOPS |  |
| FP16 TensorCore    |                  |                  | 125 TFLOPS         |            | 165 TFLOPS |  |
| INT8 TensorCore    | 193/193 TFLOPS   |                  | 250 TFLOPS         |            | 330 TOPS   |  |
| INT4 TensorCore    |                  |                  |                    |            | 661 TOPS   |  |

Datasheets:

1. [T4](https://www.nvidia.com/en-us/data-center/tesla-t4/)
1. [A10](https://www.nvidia.com/en-us/data-center/products/a10-gpu/)
1. [A30](https://www.nvidia.com/en-us/data-center/products/a30-gpu/)
1. [V100-PCIe/V100-SXM2/V100S-PCIe](https://www.nvidia.com/en-us/data-center/v100/)

# 3 Comparison of A100/A800/H100/H800/`Ascend 910B`

|                    | A800 (PCIe/SXM)  | A100 (PCIe/SXM)                  | <mark>Huawei Ascend 910B</mark>| H800  (PCIe/SXM) | H100 (PCIe/SXM) |
|:-------------------|:-----------------|:---------------------------------|:-----------------|:-----------------|:--------|
| Year               | 2022             | 2020                             | 2023             | 2022             | 2022 |
| Manufacturing      | 7nm              | 7nm                              | 7+nm             | 4nm              | 4nm |
| Architecture       | Ampere           | Ampere                           | HUAWEI Da Vinci  | Hopper           | Hopper |
| Max Power          | 300/400 watt     | 300/400 watt                     | 400 watt         |                  | 350/700 watt |
| GPU Mem            | 80G HBM2e        | 80G HBM2e                        | 64G HBM2e        | 80G HBM3         | 80G HBM3 |
| GPU Mem BW         |                  | 1935/2039 GB/s                   |                  |                  | 2/3.35 TB/s|
| GPU Interconnect (**<mark>one-to-one max bandwidth</mark>**)| NVLINK 400GB/s   | PCIe Gen4 64GB/s, NVLINK 600GB/s | HCCS **<mark><code>56GB/s</code></mark>** | NVLINK 400GB/s   | PCIe Gen5 128GB/s, NVLINK **<mark><code>900GB/s</code></mark>** |
| GPU Interconnect (**<mark>one-to-many total bw</mark>**)    | NVLINK 400GB/s   | PCIe Gen4 64GB/s, NVLINK 600GB/s | HCCS **<mark><code>392GB/s</code></mark>** | NVLINK 400GB/s   | PCIe Gen5 128GB/s, NVLINK **<mark><code>900GB/s</code></mark>** |
| FP32               |                  | 19.5 TFLOPS                      |                  |                  | 51/67 TFLOPS |
| TF32 (TensorFloat) |                  | 156/312 TFLOPS                   |                  |                  | 756/989 TFLOPS |
| BFLOAT16 TensorCore|                  | 156/312 TFLOPS                   |                  |                  |  |
| FP16 TensorCore    |                  | 312/624 TFLOPS                   | 320 TFLOPS       |                  | 1513/1979 TFLOPS |
| FP8 TensorCore     | NOT support      | NOT support                      |                  |                  | 3026/3958 TFLOPS |
| INT8 TensorCore    |                  | 624/1248 TFLOPS                  | 640 TFLOPS       |                  | 3026/3958 TFLOPS |

H100 vs. A100 in one word: **<mark> 3x performance, 2x price</mark>**.

Datasheets:

1. [A100](https://www.nvidia.com/en-us/data-center/a100/)
1. [H100](https://www.nvidia.com/en-us/data-center/h100/)
1. [~~Huawei Ascend-910B~~](https://www.hisilicon.com/en/products/Ascend/Ascend-910) (404)
1. `910` paper: [Ascend: a Scalable and Unified Architecture for Ubiquitous Deep Neural Network Computing](https://ieeexplore.ieee.org/abstract/document/9407221), HPCA, 2021

## Note on inter-GPU bandwidth: `HCCS vs. NVLINK`

For 8-card A800 and 910B modules: 910B HCCS has a total bandwidth of `392GB/s`,
which appears to be comparable to A800 NVLink (`400GB/s`). However, there are
some differences. To clarify them,

* NVIDIA NVLink: **<mark>full-mesh topology</mark>** as below, so (bi-directional)
  **<mark><code>GPU-to-GPU max bandwidth</code></mark>** is **<mark><code>400GB/s</code></mark>**
  (note that below is `8*A100` module, 600GB/s, `8*A800` shares a similar full-mesh topology);

    <p align="center"><img src="/assets/img/gpu-notes/8x-a100-node-hw-topo.png" width="100%" height="100%"></p>

* Huawei HCCS: **<mark>peer-to-peer topology</mark>** (no stuffs like NVSwitch chip), so (bi-directional)
  **<mark><code>GPU-to-GPU max bandwidth</code></mark>** is **<mark><code>56GB/s</code></mark>**;

    <p align="center"><img src="/assets/img/gpu-notes/ascend-910b-x8-topo.png" width="50%" height="50%"></p>

# 4 Comparison of `H20`/`L20`/`Ascend 910B`

|      | <mark>Huawei Ascend 910B</mark>| L20  (PCIe)      | H20  (PCIe/SXM)  | H100 (PCIe/SXM) |
|:-------------------|:-----------------|:-----------------|:-----------------|:--------|
| Year               | 2023             | 2023             | 2023             | 2022 |
| Manufacturing      | 7+nm             | 4nm              | 4nm              | 4nm |
| Architecture       | HUAWEI Da Vinci  | Ada Lovelace     | Hopper           | Hopper |
| Max Power          | 400 watt         | 275W             | 400W             | 350/700 watt |
| GPU Mem            | 64G HBM2e        | 48G GDDR6        | 80G HBM3         | 80G HBM3 |
| GPU Mem BW         |                  | 864GB/s          | <mark>4.0TB/s</mark> | 2/3.35 TB/s|
| L2 Cache           |                  | 96MB             | 60MB             |             |
| GPU Interconnect (**<mark>one-to-one max bandwidth</mark>**)| HCCS 56GB/s  | PCIe Gen4 64GB/s | PCIe Gen5 128GB/s, **<mark><code>NVLINK 900GB/s</code></mark>** | PCIe Gen5 128GB/s, NVLINK 900GB/s |
| GPU Interconnect (**<mark>one-to-many total bw</mark>**)    | HCCS 392GB/s | PCIe Gen4 64GB/s | PCIe Gen5 128GB/s, **<mark><code>NVLINK 900GB/s</code></mark>** | PCIe Gen5 128GB/s, NVLINK 900GB/s |
| FP32               |                  | 59.8 TFLOPS      | 44 TFLOPS        | 51/67 TFLOPS |
| TF32 (TensorFloat) |                  | 59.8 TFLOPS      | 74 TFLOPS        | 756/989 TFLOPS |
| BFLOAT16 TensorCore|                  | 119/119 TFLOPS   | 148/148 TFLOPS   |  |
| FP16 TensorCore    | 320 TFLOPS       |                  |                  | 1513/1979 TFLOPS |
| FP8 TensorCore     |                  |                  |                  | 3026/3958 TFLOPS |
| INT8 TensorCore    | 640 TFLOPS       | 239/239 TFLOPS   | 296/296 TFLOPS   | 3026/3958 TFLOPS |
