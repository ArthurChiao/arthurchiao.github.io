---
layout    : post
title     : "GPU Prices Quick Reference"
date      : 2023-06-17
lastupdate: 2023-07-21
categories: gpu
---

This post lists some GPU node prices collected from several public cloud
vendors, intended primarily for personal reference. Note that these prices are
subject to change over time, so if you are planning a serious budget, please
consult each cloud vendor's pricing page for the most up-to-date information.

----

* TOC
{:toc}

----

# 1 AWS (as of 2023.06)

[GPU instances types](https://docs.aws.amazon.com/dlami/latest/devguide/gpu.html) (flavors),

 <ul>
  <li>
    <a href="https://aws.amazon.com/ec2/instance-types/p3/">Amazon EC2 P3 Instances</a>  have up to 8 NVIDIA Tesla <mark>V100</mark> GPUs.
  </li>
  <li>
    <a href="https://aws.amazon.com/ec2/instance-types/p4/">Amazon EC2 P4 Instances</a>  have up to 8 NVIDIA Tesla <mark>A100</mark> GPUs.
  </li>
  <li>
     <a href="https://aws.amazon.com/ec2/instance-types/g3/">Amazon EC2 G3 Instances</a>  have up to 4 NVIDIA Tesla M60 GPUs.
  </li>
  <li>
     <a href="https://aws.amazon.com/ec2/instance-types/g4/">Amazon EC2 G4 Instances</a>  have up to 4 NVIDIA <mark>T4</mark> GPUs.
  </li>
  <li>
    <a href="https://aws.amazon.com/ec2/instance-types/g5/">Amazon EC2 G5 Instances</a>  have up to 8 NVIDIA A10G GPUs.
  </li>
  <li>
     <a href="http://aws.amazon.com/ec2/instance-types/g5g/">Amazon EC2 G5g Instances</a> have Arm-based <a href="http://aws.amazon.com/ec2/graviton/">AWS Graviton2 processors</a>.
  </li>
</ul>

Note that some flavors may only be available in specific regions.

## 1.1 Region `ap-southeast-1` (Singapore)

### A100

#### Flavors: **<mark><code>p4d.*</code></mark>**

**<mark>Not available in Singapore</mark>** at the time of this writing (2023-06).

```
| Instance type | vCPUs | Architecture | Memory (GiB) | Network performance | GPUs     |
|---------------|-------|--------------|--------------|---------------------|----------|
| p4d.24xlarge  | 96    | x86_64       | 1152         | 4x 100 Gigabit      | 8        |
```

#### Reference prices & discount prices

TODO.

### V100

#### Flavors: **<mark><code>p3.*</code></mark>**

```
| Instance type | vCPUs | Architecture | Memory (GiB) | Network performance | GPUs     |
|---------------|-------|--------------|--------------|---------------------|----------|
| p3.2xlarge    | 8     | x86_64       |  61          | Up to 10 Gigabit    | 1 * V100 |
| p3.8xlarge    | 32    | x86_64       | 244          | 10 Gigabit          | 4 * V100 |
| p3.16xlarge   | 64    | x86_64       | 488          | 25 Gigabit          | 8 * V100 |
```

#### Reference prices & discount prices

On-demand `OS=Linux` prices, in **<mark><code>USD/hour</code></mark>**:

```
| Instance type | vCPUs | Arch   | Memory (GiB) | Network performance | GPUs     | Ref Price | 30% off | 40% off | 50% off | 60% off | 70% off |
|---------------|-------|--------|--------------|---------------------|----------|-----------|---------|---------|---------|---------|---------|
| p3.2xlarge    | 8     | x86_64 |  61          | Up to 10 Gigabit    | 1 * V100 | 4.234     |   2.964 |   2.540 |   2.117 |   1.694 |   1.270 |
| p3.8xlarge    | 32    | x86_64 | 244          | 10 Gigabit          | 4 * V100 | 16.936    |  11.855 |  10.162 |   8.468 |   6.774 |   5.081 |
| p3.16xlarge   | 64    | x86_64 | 488          | 25 Gigabit          | 8 * V100 | 33.872    |  23.710 |  20.323 |  16.936 |  13.549 |  10.162 |
```

### T4

#### Flavors: **<mark><code>g4dn.*</code></mark>**

```
| Instance type | vCPUs | Architecture | Memory (GiB) | Network performance | GPUs   |
|---------------|-------|--------------|--------------|---------------------|--------|
| g4dn.xlarge   |  4    | x86_64       |  16          | Up to 25 Gigabit    | 1 * T4 |
| g4dn.2xlarge  |  8    | x86_64       |  32          | Up to 25 Gigabit    | 1 * T4 |
| g4dn.4xlarge  | 16    | x86_64       |  64          | Up to 25 Gigabit    | 1 * T4 |
| g4dn.8xlarge  | 32    | x86_64       | 128          | 50 Gigabit          | 1 * T4 |
| g4dn.12xlarge | 48    | x86_64       | 192          | 50 Gigabit          | 4 * T4 |
| g4dn.16xlarge | 64    | x86_64       | 256          | 50 Gigabit          | 1 * T4 |
| g4dn.metal    | 96    | x86_64       | 384          | 100 Gigabit         | 8 * T4 |
```

#### Reference prices & discount prices

On-demand `OS=Linux` prices, in **<mark><code>USD/hour</code></mark>**:

```
| Instance type | vCPUs | Arch   | Memory (GiB) | Network performance | GPUs   | Ref Price | 30% off | 40% off | 50% off | 60% off | 70% off |
|---------------|-------|--------|--------------|---------------------|--------|-----------|---------|---------|---------|---------|---------|
| g4dn.xlarge   |  4    | x86_64 |  16          | Up to 25 Gigabit    | 1 * T4 |  0.736    |   0.515 |   0.442 |   0.368 |   0.294 |   0.221 |
| g4dn.2xlarge  |  8    | x86_64 |  32          | Up to 25 Gigabit    | 1 * T4 |  1.052    |   0.736 |   0.631 |   0.526 |   0.421 |   0.316 |
| g4dn.4xlarge  | 16    | x86_64 |  64          | Up to 25 Gigabit    | 1 * T4 |  1.685    |   1.179 |   1.011 |   0.843 |   0.674 |   0.505 |
| g4dn.8xlarge  | 32    | x86_64 | 128          | 50 Gigabit          | 1 * T4 |  3.045    |   2.131 |   1.827 |   1.522 |   1.218 |   0.913 |
| g4dn.12xlarge | 48    | x86_64 | 192          | 50 Gigabit          | 4 * T4 |  5.474    |   3.832 |   3.284 |   2.737 |   2.190 |   1.642 |
| g4dn.16xlarge | 64    | x86_64 | 256          | 50 Gigabit          | 1 * T4 |  6.089    |   4.262 |   3.653 |   3.045 |   2.436 |   1.827 |
| g4dn.metal    | 96    | x86_64 | 384          | 100 Gigabit         | 8 * T4 | 10.948    |   7.664 |   6.569 |   5.474 |   4.379 |   3.284 |
```

## 1.2 Region `eu-central-1` (Frankfurt)

### A100

#### Flavors: **<mark><code>p4d.*</code></mark>**

**<mark>Not available in Frankfurt</mark>** at the time of this writing (2023-06).

```
| Instance type | vCPUs | Architecture | Memory (GiB) | Network performance | GPUs     |
|---------------|-------|--------------|--------------|---------------------|----------|
| p4d.24xlarge  | 96    | x86_64       | 1152         | 4x 100 Gigabit      | 8        |
```

#### Reference prices & discount prices

TODO.

### V100

#### Flavors: **<mark><code>p3.*</code></mark>**

**<mark>Not available in Frankfurt</mark>** at the time of this writing (2023-06).

```
| Instance type | vCPUs | Architecture | Memory (GiB) | Network performance | GPUs     |
|---------------|-------|--------------|--------------|---------------------|----------|
| p3.2xlarge    | 8     | x86_64       |  61          | Up to 10 Gigabit    | 1 * V100 |
| p3.8xlarge    | 32    | x86_64       | 244          | 10 Gigabit          | 4 * V100 |
| p3.16xlarge   | 64    | x86_64       | 488          | 25 Gigabit          | 8 * V100 |
```

#### Reference prices & discount prices

On-demand `OS=Linux` prices, in **<mark><code>USD/hour</code></mark>**:

```
| Instance type | vCPUs | Arch   | Memory (GiB) | Network performance | GPUs     | Ref Price | 30% off | 40% off | 50% off | 60% off | 70% off |
|---------------|-------|--------|--------------|---------------------|----------|-----------|---------|---------|---------|---------|---------|
| p3.2xlarge    | 8     | x86_64 |  61          | Up to 10 Gigabit    | 1 * V100 | 3.823     |   2.676 |   2.294 |   1.911 |   1.529 |   1.147 |
| p3.8xlarge    | 32    | x86_64 | 244          | 10 Gigabit          | 4 * V100 | 15.292    |  10.704 |   9.175 |   7.646 |   6.117 |   4.588 |
| p3.16xlarge   | 64    | x86_64 | 488          | 25 Gigabit          | 8 * V100 | 30.584    |  21.409 |  18.350 |  15.292 |  12.234 |   9.175 |
```

### T4

#### Flavors: **<mark><code>g4dn.*</code></mark>**

```
| Instance type | vCPUs | Architecture | Memory (GiB) | Network performance | GPUs   |
|---------------|-------|--------------|--------------|---------------------|--------|
| g4dn.xlarge   |  4    | x86_64       |  16          | Up to 25 Gigabit    | 1 * T4 |
| g4dn.2xlarge  |  8    | x86_64       |  32          | Up to 25 Gigabit    | 1 * T4 |
| g4dn.4xlarge  | 16    | x86_64       |  64          | Up to 25 Gigabit    | 1 * T4 |
| g4dn.8xlarge  | 32    | x86_64       | 128          | 50 Gigabit          | 1 * T4 |
| g4dn.12xlarge | 48    | x86_64       | 192          | 50 Gigabit          | 4 * T4 |
| g4dn.16xlarge | 64    | x86_64       | 256          | 50 Gigabit          | 1 * T4 |
| g4dn.metal    | 96    | x86_64       | 384          | 100 Gigabit         | 8 * T4 |
```

#### Reference prices & discount prices

On-demand `OS=Linux` prices, in **<mark><code>USD/hour</code></mark>**:

```
| Instance type | vCPUs | Arch   | Memory (GiB) | Network performance | GPUs   | Ref Price | 30% off | 40% off | 50% off | 60% off | 70% off |
|---------------|-------|--------|--------------|---------------------|--------|-----------|---------|---------|---------|---------|---------|
| g4dn.xlarge   |  4    | x86_64 |  16          | Up to 25 Gigabit    | 1 * T4 | 0.658     |   0.461 |   0.395 |   0.329 |   0.263 |   0.197 |
| g4dn.2xlarge  |  8    | x86_64 |  32          | Up to 25 Gigabit    | 1 * T4 | 1.308     |   0.916 |   0.785 |   0.654 |   0.523 |   0.392 |
| g4dn.4xlarge  | 16    | x86_64 |  64          | Up to 25 Gigabit    | 1 * T4 | 1.505     |   1.053 |   0.903 |   0.752 |   0.602 |   0.451 |
| g4dn.8xlarge  | 32    | x86_64 | 128          | 50 Gigabit          | 1 * T4 | 4.192     |   2.934 |   2.515 |   2.096 |   1.677 |   1.258 |
| g4dn.12xlarge | 48    | x86_64 | 192          | 50 Gigabit          | 4 * T4 | 5.015     |   3.510 |   3.009 |   2.507 |   2.006 |   1.504 |
| g4dn.16xlarge | 64    | x86_64 | 256          | 50 Gigabit          | 1 * T4 | 5.440     |   3.808 |   3.264 |   2.720 |   2.176 |   1.632 |
| g4dn.metal    | 96    | x86_64 | 384          | 100 Gigabit         | 8 * T4 | 9.780     |   6.846 |   5.868 |   4.890 |   3.912 |   2.934 |
```

# 2 AlibabaCloud (as of 2023.06)

## 2.1 Region `cn-shanghai` (Shanghai)

Data from [2].

### 2.1.1 A100

#### Flavors: **<mark><code>ecs.gn7*</code></mark>**

```
| Instance Type (flavor)  | vCPUs | Memory   | GPU (Count*Model)   | GPU Memory| Price (按量/pay-as-you-go) | Price (包月/subscription) |
|:------------------------|:------|:---------|:--------------------|:----------|:---------------------------|:--------------------------|
| ecs.gn7e-c16g1.4xlarge  |  16   |  125 GiB | 1 * NVIDIA A100 80G | 1 * 80GB  |  ￥34.74/Hour              | ￥16676.0/Month |
| ecs.gn7-c12g1.3xlarge   |  12   |   94 GiB | 1 * NVIDIA A100     | 1 * 40GB  |  ￥31.58/Hour              | ￥15160.0/Month |
| ecs.gn7e-c16g1.16xlarge |  64   |  500 GiB | 4 * NVIDIA A100 80G | 4 * 80GB  | ￥138.96/Hour              | ￥66704.0/Month |
| ecs.gn7e-c16g1.32xlarge | 128   | 1000 GiB | 8 * NVIDIA A100 80G | 8 * 80GB  | ￥277.93/Hour              |￥133408.0/Month |
| ecs.gn7-c13g1.13xlarge  |  52   |  378 GiB | 4 * NVIDIA A100     | 4 * 40GB  | ￥126.33/Hour              | ￥60640.0/Month |
| ecs.gn7-c13g1.26xlarge  | 104   |  756 GiB | 8 * NVIDIA A100     | 8 * 40GB  | ￥252.66/Hour              |￥121280.0/Month |
| ecs.ebmgn7e.32xlarge    | 128   | 1024 GiB | 8 * NVIDIA A100 80G | 8 * 80GB  | ￥277.93/Hour              |￥133408.0/Month |
```

#### Reference prices & discount prices

Pay-as-you-go prices (按量计费), **<mark><code>RMB/hour</code></mark>**:

```
| Instance Type (flavor)  | vCPUs | Memory   | GPU (Count*Model)   | GPU Memory| Ref Price | 30% off | 40% off | 50% off | 60% off | 70% off |
|:------------------------|:------|:---------|:--------------------|:----------|:----------|:--------|:--------|:--------|:--------|:--------|
| ecs.gn7e-c16g1.4xlarge  |  16   |  125 GiB | 1 * NVIDIA A100 80G | 1 * 80GB  |  34.7     |  24.3   |  20.8   |  17.4   |  13.9   |  10.4   |
| ecs.gn7-c12g1.3xlarge   |  12   |   94 GiB | 1 * NVIDIA A100     | 1 * 40GB  |  31.5     |  22.0   |  18.9   |  15.8   |  12.6   |   9.4   |
| ecs.gn7e-c16g1.16xlarge |  64   |  500 GiB | 4 * NVIDIA A100 80G | 4 * 80GB  | 138.9     |  97.2   |  83.3   |  69.5   |  55.6   |  41.7   |
| ecs.gn7e-c16g1.32xlarge | 128   | 1000 GiB | 8 * NVIDIA A100 80G | 8 * 80GB  | 277.9     | 194.5   | 166.7   | 138.9   | 111.2   |  83.4   |
| ecs.gn7-c13g1.13xlarge  |  52   |  378 GiB | 4 * NVIDIA A100     | 4 * 40GB  | 126.3     |  88.4   |  75.8   |  63.1   |  50.5   |  37.9   |
| ecs.gn7-c13g1.26xlarge  | 104   |  756 GiB | 8 * NVIDIA A100     | 8 * 40GB  | 252.6     | 176.8   | 151.6   | 126.3   | 101.0   |  75.8   |
| ecs.ebmgn7e.32xlarge    | 128   | 1024 GiB | 8 * NVIDIA A100 80G | 8 * 80GB  | 277.9     | 194.5   | 166.7   | 138.9   | 111.2   |  83.4   |
```

Subscription prices (包月), **<mark><code>RMB/month</code></mark>**:

```
| Instance Type (flavor)  | vCPUs | Memory   | GPU (Count*Model)   | GPU Memory| Ref Price | 30% off | 40% off | 50% off | 60% off | 70% off |
|:------------------------|:------|:---------|:--------------------|:----------|:----------|:--------|:--------|:--------|:--------|:--------|
| ecs.gn7e-c16g1.4xlarge  |  16   |  125 GiB | 1 * NVIDIA A100 80G | 1 * 80GB  |  16676    | 11673.2 | 10005.6 |  8338.0 |  6670.4 |  5002.8 |
| ecs.gn7-c12g1.3xlarge   |  12   |   94 GiB | 1 * NVIDIA A100     | 1 * 40GB  |  15160    | 10612.0 |  9096.0 |  7580.0 |  6064.0 |  4548.0 |
| ecs.gn7e-c16g1.16xlarge |  64   |  500 GiB | 4 * NVIDIA A100 80G | 4 * 80GB  |  66704    | 46692.8 | 40022.4 | 33352.0 | 26681.6 | 20011.2 |
| ecs.gn7e-c16g1.32xlarge | 128   | 1000 GiB | 8 * NVIDIA A100 80G | 8 * 80GB  | 133408    | 93385.6 | 80044.8 | 66704.0 | 53363.2 | 40022.4 |
| ecs.gn7-c13g1.13xlarge  |  52   |  378 GiB | 4 * NVIDIA A100     | 4 * 40GB  |  60640    | 42448.0 | 36384.0 | 30320.0 | 24256.0 | 18192.0 |
| ecs.gn7-c13g1.26xlarge  | 104   |  756 GiB | 8 * NVIDIA A100     | 8 * 40GB  | 121280    | 84896.0 | 72768.0 | 60640.0 | 48512.0 | 36384.0 |
| ecs.ebmgn7e.32xlarge    | 128   | 1024 GiB | 8 * NVIDIA A100 80G | 8 * 80GB  | 133408    | 93385.6 | 80044.8 | 66704.0 | 53363.2 | 40022.4 |
```

### 2.1.2 V100

#### Flavors: **<mark><code>ecs.gn6[v|e]*</code></mark>**

```
| Instance Type (flavor)  | vCPUs | Memory   | GPU (Count*Model)     | GPU Memory| Price (按量/pay-as-you-go) | Price (包月/subscription) |
|:------------------------|:------|:---------|:----------------------|:----------|:---------------------------|:--------------------------|
| ecs.gn6v-c8g1.2xlarge   |  8    |  32 GiB  | 1 * NVIDIA V100       | 1 * 16 GB |  ￥26.46/Hour              |  ￥7620.0/Month |
| ecs.gn6v-c8g1.8xlarge   | 32    | 128 GiB  | 4 * NVIDIA V100       | 4 * 16 GB | ￥105.84/Hour              | ￥30480.0/Month |
| ecs.gn6v-c8g1.16xlarge  | 64    | 256 GiB  | 8 * NVIDIA V100       | 8 * 16 GB | ￥211.68/Hour              | ￥60960.0/Month |
| ecs.gn6v-c10g1.20xlarge | 82    | 336 GiB  | 8 * NVIDIA V100       | 8 * 16 GB | ￥219.64/Hour              | ￥63255.0/Month |
| ecs.gn6e-c12g1.3xlarge  | 12    |  92 GiB  | 1 * NVIDIA V100       | 1 * 32 GB |  ￥19.73/Hour              |  ￥9475.0/Month |
| ecs.gn6e-c12g1.12xlarge | 48    | 368 GiB  | 4 * NVIDIA V100       | 4 * 32 GB |  ￥78.95/Hour              | ￥37900.0/Month |
| ecs.ebmgn6v.24xlarge    | 96    | 384 GiB  | 8 * Nvidia Tesla V100 | 8 * 16 GB | ￥237.12/Hour              | ￥68292.0/Month |
| ecs.gn6e-c12g1.24xlarge | 96    | 736 GiB  | 8 * NVIDIA V100       | 8 * 32 GB | ￥157.91/Hour              | ￥75800.0/Month |
```

#### Reference prices & discount prices

Pay-as-you-go prices (按量计费), **<mark><code>RMB/hour</code></mark>**:

```
| Instance Type (flavor)       | vCPUs | Memory   | GPU (Count*Model)    | GPU Memory| Ref Price | 30% off | 40% off | 50% off | 60% off | 70% off |
|:-----------------------------|:------|:---------|:---------------------|:----------|:----------|:--------|:--------|:--------|:--------|:--------|
| gn6v ecs.gn6v-c8g1.2xlarge   |  8    |  32 GiB  | 1 * NVIDIA V100      | 1 * 16 GB |  26.46    |    18.5 |    15.9 |    13.2 |    10.6 |     7.9 |
| gn6v ecs.gn6v-c8g1.8xlarge   | 32    | 128 GiB  | 4 * NVIDIA V100      | 4 * 16 GB | 105.84    |    74.1 |    63.5 |    52.9 |    42.3 |    31.8 |
| gn6v ecs.gn6v-c8g1.16xlarge  | 64    | 256 GiB  | 8 * NVIDIA V100      | 8 * 16 GB | 211.68    |   148.2 |   127.0 |   105.8 |    84.7 |    63.5 |
| gn6v ecs.gn6v-c10g1.20xlarge | 82    | 336 GiB  | 8 * NVIDIA V100      | 8 * 16 GB | 219.64    |   153.7 |   131.8 |   109.8 |    87.9 |    65.9 |
| gn6e ecs.gn6e-c12g1.3xlarge  | 12    |  92 GiB  | 1 * NVIDIA V100      | 1 * 32 GB |  19.73    |    13.8 |    11.8 |     9.9 |     7.9 |     5.9 |
| gn6e ecs.gn6e-c12g1.12xlarge | 48    | 368 GiB  | 4 * NVIDIA V100      | 4 * 32 GB |  78.95    |    55.3 |    47.4 |    39.5 |    31.6 |    23.7 |
| ecs.ebmgn6v.24xlarge         | 96    | 384 GiB  | 8 * Nvidia Tesla V100| 8 * 16 GB | 237.12    |   166.0 |   142.3 |   118.6 |    94.8 |    71.1 |
| ecs.gn6e-c12g1.24xlarge      | 96    | 736 GiB  | 8 * NVIDIA V100      | 8 * 32 GB | 157.91    |   110.5 |    94.7 |    79.0 |    63.2 |    47.4 |
```

Subscription prices (包月), **<mark><code>RMB/month</code></mark>**:

```
| Instance Type (flavor)       | vCPUs | Memory   | GPU (Count*Model)    | GPU Memory| Ref Price | 30% off | 40% off | 50% off | 60% off | 70% off |
|:-----------------------------|:------|:---------|:---------------------|:----------|:----------|:--------|:--------|:--------|:--------|:--------|
| gn6v ecs.gn6v-c8g1.2xlarge   |  8    |  32 GiB  | 1 * NVIDIA V100      | 1 * 16 GB |  7620.0   |  5334.0 |  4572.0 |  3810.0 |  3048.0 |  2286.0 |
| gn6v ecs.gn6v-c8g1.8xlarge   | 32    | 128 GiB  | 4 * NVIDIA V100      | 4 * 16 GB | 30480.0   | 21336.0 | 18288.0 | 15240.0 | 12192.0 |  9144.0 |
| gn6v ecs.gn6v-c8g1.16xlarge  | 64    | 256 GiB  | 8 * NVIDIA V100      | 8 * 16 GB | 60960.0   | 42672.0 | 36576.0 | 30480.0 | 24384.0 | 18288.0 |
| gn6v ecs.gn6v-c10g1.20xlarge | 82    | 336 GiB  | 8 * NVIDIA V100      | 8 * 16 GB | 63255.0   | 44278.5 | 37953.0 | 31627.5 | 25302.0 | 18976.5 |
| gn6e ecs.gn6e-c12g1.3xlarge  | 12    |  92 GiB  | 1 * NVIDIA V100      | 1 * 32 GB |  9475.0   |  6632.5 |  5685.0 |  4737.5 |  3790.0 |  2842.5 |
| gn6e ecs.gn6e-c12g1.12xlarge | 48    | 368 GiB  | 4 * NVIDIA V100      | 4 * 32 GB | 37900.0   | 26530.0 | 22740.0 | 18950.0 | 15160.0 | 11370.0 |
| ecs.ebmgn6v.24xlarge         | 96    | 384 GiB  | 8 * Nvidia Tesla V100| 8 * 16 GB | 68292.0   | 47804.4 | 40975.2 | 34146.0 | 27316.8 | 20487.6 |
| ecs.gn6e-c12g1.24xlarge      | 96    | 736 GiB  | 8 * NVIDIA V100      | 8 * 32 GB | 75800.0   | 53060.0 | 45480.0 | 37900.0 | 30320.0 | 22740.0 |
```

### 2.1.3 T4

#### Flavors: **<mark><code>ecs.gn6i*</code></mark>**

```
| Instance Type (flavor)  | vCPUs | Memory   | GPU (Count*Model) | GPU Memory| Price (按量/pay-as-you-go) | Price (包月/subscription) |
|:------------------------|:------|:---------|:------------------|:----------|:---------------------------|:--------------------------|
| ecs.gn6i-c4g1.xlarge    |  4    |  15 GiB  | 1 * NVIDIA T4     | 1 * 16 GB | ￥11.63/Hour               |  ￥3348.0/Month |
| ecs.gn6i-c8g1.2xlarge   |  8    |  31 GiB  | 1 * NVIDIA T4     | 1 * 16 GB | ￥14.00/Hour               |  ￥4032.0/Month |
| ecs.gn6i-c16g1.4xlarge  | 16    |  62 GiB  | 1 * NVIDIA T4     | 1 * 16 GB | ￥16.41/Hour               |  ￥4725.0/Month |
| ecs.gn6i-c24g1.6xlarge  | 24    |  93 GiB  | 1 * NVIDIA T4     | 1 * 16 GB | ￥17.19/Hour               |  ￥4950.0/Month |
| ecs.gn6i-c40g1.10xlarge | 40    | 155 GiB  | 1 * NVIDIA T4     | 1 * 16 GB | ￥14.81/Hour               |  ￥7112.9/Month |
| ecs.gn6i-c24g1.12xlarge | 48    | 186 GiB  | 2 * NVIDIA T4     | 2 * 16 GB | ￥34.38/Hour               |  ￥9900.0/Month |
| ecs.gn6i-c24g1.24xlarge | 96    | 372 GiB  | 4 * NVIDIA T4     | 4 * 16 GB | ￥68.75/Hour               | ￥19800.0/Month |
| ecs.ebmgn6i.24xlarge    | 96    | 384 GiB  | 4 * NVIDIA T4     | 4 * 16 GB | ￥68.75/Hour               | ￥19800.0/Month |
```

#### Reference prices & discount prices

Pay-as-you-go prices (按量计费), **<mark><code>RMB/hour</code></mark>**:

```
| Instance Type (flavor)  | vCPUs | Memory   | GPU (Count*Model) | GPU Memory| Ref Price | 30% off | 40% off | 50% off | 60% off | 70% off |
|:------------------------|:------|:---------|:------------------|:----------|:----------|:--------|:--------|:--------|:--------|:--------|
| ecs.gn6i-c4g1.xlarge    |  4    |  15 GiB  | 1 * NVIDIA T4     | 1 * 16 GB | 11.63     |     8.1 |     7.0 |     5.8 |     4.7 |     3.5 |
| ecs.gn6i-c8g1.2xlarge   |  8    |  31 GiB  | 1 * NVIDIA T4     | 1 * 16 GB | 14.00     |     9.8 |     8.4 |     7.0 |     5.6 |     4.2 |
| ecs.gn6i-c16g1.4xlarge  | 16    |  62 GiB  | 1 * NVIDIA T4     | 1 * 16 GB | 16.41     |    11.5 |     9.8 |     8.2 |     6.6 |     4.9 |
| ecs.gn6i-c24g1.6xlarge  | 24    |  93 GiB  | 1 * NVIDIA T4     | 1 * 16 GB | 17.19     |    12.0 |    10.3 |     8.6 |     6.9 |     5.2 |
| ecs.gn6i-c40g1.10xlarge | 40    | 155 GiB  | 1 * NVIDIA T4     | 1 * 16 GB | 14.81     |    10.4 |     8.9 |     7.4 |     5.9 |     4.4 |
| ecs.gn6i-c24g1.12xlarge | 48    | 186 GiB  | 2 * NVIDIA T4     | 2 * 16 GB | 34.38     |    24.1 |    20.6 |    17.2 |    13.8 |    10.3 |
| ecs.gn6i-c24g1.24xlarge | 96    | 372 GiB  | 4 * NVIDIA T4     | 4 * 16 GB | 68.75     |    48.1 |    41.2 |    34.4 |    27.5 |    20.6 |
| ecs.ebmgn6i.24xlarge    | 96    | 384 GiB  | 4 * NVIDIA T4     | 4 * 16 GB | 68.75     |    48.1 |    41.2 |    34.4 |    27.5 |    20.6 |
```

Subscription prices (包月), **<mark><code>RMB/month</code></mark>**:

```
| Instance Type (flavor)  | vCPUs | Memory   | GPU (Count*Model) | GPU Memory| Ref Price | 30% off | 40% off | 50% off | 60% off | 70% off |
|:------------------------|:------|:---------|:------------------|:----------|:----------|:--------|:--------|:--------|:--------|:--------|
| ecs.gn6i-c4g1.xlarge    |  4    |  15 GiB  | 1 * NVIDIA T4     | 1 * 16 GB |   3348.0  |  2343.6 |  2008.8 |  1674.0 |  1339.2 |  1004.4 |
| ecs.gn6i-c8g1.2xlarge   |  8    |  31 GiB  | 1 * NVIDIA T4     | 1 * 16 GB |   4032.0  |  2822.4 |  2419.2 |  2016.0 |  1612.8 |  1209.6 |
| ecs.gn6i-c16g1.4xlarge  | 16    |  62 GiB  | 1 * NVIDIA T4     | 1 * 16 GB |   4725.0  |  3307.5 |  2835.0 |  2362.5 |  1890.0 |  1417.5 |
| ecs.gn6i-c24g1.6xlarge  | 24    |  93 GiB  | 1 * NVIDIA T4     | 1 * 16 GB |   4950.0  |  3465.0 |  2970.0 |  2475.0 |  1980.0 |  1485.0 |
| ecs.gn6i-c40g1.10xlarge | 40    | 155 GiB  | 1 * NVIDIA T4     | 1 * 16 GB |   7112.9  |  4979.0 |  4267.7 |  3556.4 |  2845.2 |  2133.9 |
| ecs.gn6i-c24g1.12xlarge | 48    | 186 GiB  | 2 * NVIDIA T4     | 2 * 16 GB |   9900.0  |  6930.0 |  5940.0 |  4950.0 |  3960.0 |  2970.0 |
| ecs.gn6i-c24g1.24xlarge | 96    | 372 GiB  | 4 * NVIDIA T4     | 4 * 16 GB |  19800.0  | 13860.0 | 11880.0 |  9900.0 |  7920.0 |  5940.0 |
| ecs.ebmgn6i.24xlarge    | 96    | 384 GiB  | 4 * NVIDIA T4     | 4 * 16 GB |  19800.0  | 13860.0 | 11880.0 |  9900.0 |  7920.0 |  5940.0 |
```

### 2.1.4 A10

#### Flavors: **<mark><code>ecs.gn7i*</code></mark>**

```
| Instance Type (flavor)  | vCPUs | Memory   | GPU (Count*Model) | GPU Memory| Price (按量/pay-as-you-go) | Price (包月/subscription) |
|:------------------------|:------|:---------|:------------------|:----------|:---------------------------|:--------------------------|
| ecs.gn7i-c8g1.2xlarge   |   8   |  30 GiB  | 1 * NVIDIA A10    | 1 * 24 GB |  ￥9.53/Hour               |  ￥4575.66/Month |
| ecs.gn7i-c16g1.4xlarge  |  16   |  60 GiB  | 1 * NVIDIA A10    | 1 * 24 GB | ￥10.09/Hour               |  ￥4844.81/Month |
| ecs.gn7i-c32g1.8xlarge  |  32   | 188 GiB  | 1 * NVIDIA A10    | 1 * 24 GB | ￥13.30/Hour               |  ￥6387.98/Month |
| ecs.gn7i-c48g1.12xlarge |  48   | 310 GiB  | 1 * NVIDIA A10    | 1 * 24 GB | ￥17.94/Hour               |  ￥8613.00/Month |
| ecs.gn7i-c56g1.14xlarge |  56   | 346 GiB  | 1 * NVIDIA A10    | 1 * 24 GB | ￥21.53/Hour               | ￥10335.60/Month |
| ecs.gn7i-c32g1.16xlarge |  64   | 376 GiB  | 2 * NVIDIA A10    | 2 * 24 GB | ￥26.61/Hour               | ￥12775.95/Month |
| ecs.gn7i-c32g1.32xlarge | 128   | 752 GiB  | 4 * NVIDIA A10    | 4 * 24 GB | ￥53.23/Hour               | ￥25551.90/Month |
```

#### Reference prices & discount prices

Pay-as-you-go prices (按量计费), **<mark><code>RMB/hour</code></mark>**:

```
| Instance Type (flavor)  | vCPUs | Memory   | GPU (Count*Model) | GPU Memory| Ref Price | 30% off | 40% off | 50% off | 60% off | 70% off |
|:------------------------|:------|:---------|:------------------|:----------|:----------|:--------|:--------|:--------|:--------|:--------|
| ecs.gn7i-c8g1.2xlarge   |   8   |  30 GiB  | 1 * NVIDIA A10    | 1 * 24 GB |  9.53     |   6.671 |   5.718 |   4.765 |   3.812 |   2.859 |
| ecs.gn7i-c16g1.4xlarge  |  16   |  60 GiB  | 1 * NVIDIA A10    | 1 * 24 GB | 10.09     |   7.063 |   6.054 |   5.045 |   4.036 |   3.027 |
| ecs.gn7i-c32g1.8xlarge  |  32   | 188 GiB  | 1 * NVIDIA A10    | 1 * 24 GB | 13.30     |   9.310 |   7.980 |   6.650 |   5.320 |   3.990 |
| ecs.gn7i-c48g1.12xlarge |  48   | 310 GiB  | 1 * NVIDIA A10    | 1 * 24 GB | 17.94     |  12.558 |  10.764 |   8.970 |   7.176 |   5.382 |
| ecs.gn7i-c56g1.14xlarge |  56   | 346 GiB  | 1 * NVIDIA A10    | 1 * 24 GB | 21.53     |  15.071 |  12.918 |  10.765 |   8.612 |   6.459 |
| ecs.gn7i-c32g1.16xlarge |  64   | 376 GiB  | 2 * NVIDIA A10    | 2 * 24 GB | 26.61     |  18.627 |  15.966 |  13.305 |  10.644 |   7.983 |
| ecs.gn7i-c32g1.32xlarge | 128   | 752 GiB  | 4 * NVIDIA A10    | 4 * 24 GB | 53.23     |  37.261 |  31.938 |  26.615 |  21.292 |  15.969 |
```

Subscription prices (包月), **<mark><code>RMB/month</code></mark>**:

```
| Instance Type (flavor)  | vCPUs | Memory   | GPU (Count*Model) | GPU Memory| Ref Price | 30% off | 40% off | 50% off | 60% off | 70% off |
|:------------------------|:------|:---------|:------------------|:----------|:----------|:--------|:--------|:--------|:--------|:--------|
| ecs.gn7i-c8g1.2xlarge   |   8   |  30 GiB  | 1 * NVIDIA A10    | 1 * 24 GB |   4575.66 |  3202.9 |  2745.3 |  2287.8 |  1830.2 | 1372.69 |
| ecs.gn7i-c16g1.4xlarge  |  16   |  60 GiB  | 1 * NVIDIA A10    | 1 * 24 GB |   4844.81 |  3391.3 |  2906.8 |  2422.4 |  1937.9 | 1453.44 |
| ecs.gn7i-c32g1.8xlarge  |  32   | 188 GiB  | 1 * NVIDIA A10    | 1 * 24 GB |   6387.98 |  4471.5 |  3832.7 |  3193.9 |  2555.1 | 1916.39 |
| ecs.gn7i-c48g1.12xlarge |  48   | 310 GiB  | 1 * NVIDIA A10    | 1 * 24 GB |   8613.00 |  6029.1 |  5167.8 |  4306.5 |  3445.2 | 2583.90 |
| ecs.gn7i-c56g1.14xlarge |  56   | 346 GiB  | 1 * NVIDIA A10    | 1 * 24 GB |  10335.60 |  7234.9 |  6201.3 |  5167.8 |  4134.2 | 3100.68 |
| ecs.gn7i-c32g1.16xlarge |  64   | 376 GiB  | 2 * NVIDIA A10    | 2 * 24 GB |  12775.95 |  8943.1 |  7665.5 |  6387.9 |  5110.3 | 3832.78 |
| ecs.gn7i-c32g1.32xlarge | 128   | 752 GiB  | 4 * NVIDIA A10    | 4 * 24 GB |  25551.90 | 17886.3 | 15331.1 | 12775.9 | 10220.7 | 7665.57 |
```

# Appendix

The first letter in GPU model names denote their GPU architectures, with:

1. **<mark><code>T</code></mark>** for Turing;
1. **<mark><code>A</code></mark>** for Ampere;
1. **<mark><code>V</code></mark>** for Volta;
1. **<mark><code>H</code></mark>** for Hopper;

    Introduced in 2022, support hardware implementation of Transformer architecture in AI training;

## A. Quick comparison of T4/A10/A10G/V100

|           | T4      | A10      | A10G   | V100 PCIe/SMX2 |
|:----------|:--------|:---------|:-------|:--------|
| Designed for | **<mark>Data center</mark>** workloads (AI/ML/...) | (Desktop) **<mark>Graphics-intensive</mark>** workloads | Desktop | Data center |
| Year         | 2018             | 2020               |            | 2017 |
| Manufacturing| 12nm             | 12nm               | 12nm       | |
| Architecture | Turing           | Ampere             | Ampere     | Volta |
| Max Power    | 70 watts         | 150 watts          |            | 250/300watts |
| GPU Mem      | 16GB GDDR6       | 24GB GDDR6         | 48GB GDDR6 | 16/32GB <mark>HBM2</mark> |
| GPU Mem BW   | 400 GB/s         | 600 GB/s           |            | **<mark><code>900 GB/s</code></mark>** |
| Interconnect | PCIe Gen3 32GB/s | PCIe Gen4 66 GB/s  |            | PCIe Gen3 32GB/s, NVLINK **<mark><code>300GB/s</code></mark>** |
| FP32         | 8.1 TFLOPS       | 31.2 TFLOPS        |            | 14/15.7 TFLOPS |
| BFLOAT16 TensorCore|            | 125 TFLOPS         |            |  |
| FP16 TensorCore   |             | 125 TFLOPS         |            |  |
| INT8 TensorCore   |             | 250 TFLOPS         |            |  |

Datasheets:

1. [T4](https://www.nvidia.com/en-us/data-center/tesla-t4/)
1. [A10](https://www.nvidia.com/en-us/data-center/products/a10-gpu/)
1. [V100-PCIe/V100-SXM2/V100S-PCIe](https://www.nvidia.com/en-us/data-center/v100/)

## B. Quick comparison of A100/A800/H100/H800

|                    | A800 (PCIe/SXM)  | A100 (PCIe/SXM)    | H800  (PCIe/SXM) | H100 (PCIe/SXM) |
|:-------------------|:-----------------|:-------------------|:-----------------|:--------|
| Year               | 2022             | 2020               | 2022             | 2022 |
| Manufacturing      | 7nm              | 7nm                | 4nm              | 4nm |
| Architecture       | Ampere           | Ampere             | Hopper           | Hopper |
| Max Power          | 300/400 watt     | 300/400 watt       |                  | 350/700 watt |
| GPU Mem            | 80G HBM2e        | 80G HBM2e          | 80G HBM3         | 80G HBM3 |
| GPU Mem BW         |                  | 1935/2039 GB/s     |                  | 2/3.35 TB/s|
| Interconnect       | NVLINK 400GB/s   | PCIe Gen4 64GB/s, NVLINK 600GB/s |  | PCIe Gen5 128GB/s, NVLINK **<mark><code>900GB/s</code></mark>** |
| FP32               |                  | 19.5 TFLOPS        |                  | 51/67 TFLOPS |
| TF32 (TensorFloat) |                  | 156/312 TFLOPS     |                  | 756/989 TFLOPS |
| BFLOAT16 TensorCore|                  | 156/312 TFLOPS     |                  |  |
| FP16 TensorCore    |                  | 312/624 TFLOPS     |                  | 1513/1979 TFLOPS |
| FP8 TensorCore     | NO support       | NO support         |                  | 3026/3958 TFLOPS |
| INT8 TensorCore    |                  | 624/1248 TFLOPS    |                  | 3026/3958 TFLOPS |

H100 vs. A100 in one word: **<mark> 3x performance, 2x price</mark>**.

Datasheets:

1. [A100](https://www.nvidia.com/en-us/data-center/a100/)
1. [H100](https://www.nvidia.com/en-us/data-center/h100/)

# References

1. [AWS EC2 price estimation page](https://ap-southeast-1.console.aws.amazon.com/ec2/home?region=ap-southeast-1#LaunchInstances:)
2. [AlibabaCloud ECS price estimation page](https://ecs-buy.aliyun.com/ecs)

> Tables in this post are partially generated with
> [this script](https://github.com/ArthurChiao/arthurchiao.github.io/tree/master/assets/code/gpu-prices/), and vim.
