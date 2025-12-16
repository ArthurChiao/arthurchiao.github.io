---
layout    : post
title     : "以旅行规划（Trip Planning）为例，看 DeepSeek-V3.2 如何合成高质量训练数据（2025）"
date      : 2025-12-16
lastupdate: 2025-12-16
categories: ai llm deepseek
---

如何基于 Agent/LLM 强大的<strong><mark>规划能力+生成能力+代码执行能力+反思能力</mark></strong>，
自动化合成大批量高质量数据：

<p align="center"><img src="/assets/img/deepseek-agentic-dataset-synthesizing/hypothetical-workflow.png" width="100%" height="100%"></p>
<p align="center">Hypothetical workflow</p>

<p align="center"><img src="/assets/img/deepseek-agentic-dataset-synthesizing/agentic-dataset-synthesizing.png" width="100%" height="100%"></p>
<p align="center">DeepSeek-V3.2: workflow for synthesizing high-quality agentic datasets for RL training (in agentic fashion, without human intervention)</p>

水平及维护精力所限，文中不免存在错误或过时之处，请酌情参考。
**<mark>传播知识，尊重劳动，年满十八周岁，转载请注明<a href="https://arthurchiao.art">出处</a></mark>**。

----

* TOC
{:toc}

----

# 1 场景：增强模型的 Trip Planning 能力

假设你在训练一个<strong><mark>通用模型</mark></strong>或垂域的**<mark>旅游行业模型</mark>**，
那你可能会遇到下面这样的用户诉求：

> 我计划今年十一从杭州出发玩三天，请帮我制定一份行程规划。几个要求：整个行程我
> 不想重复任何一个城市、酒店、景点或餐厅。另外，请务必确保推荐的每家酒店、餐厅和
> 景点都确实位于我当日所在的城市。关于第二天还需要注意：如果当晚入住的豪华酒店
> 价格在800元人民币及以上，则需严格控制其他开销——当日两家餐厅（午餐与晚餐）总消
> 费需低于350元，且两家餐厅评分均不低于4星，下午游览的景点门票需低于120元。若第
> 二天酒店属于中高档（500-800元），则预算可稍放宽：只需确保至少一家餐厅评分达
> 4.0星以上，且景点门票低于180元。若选择经济型酒店（200-500元），则只需保证至少
> 一家餐厅评分在3.2星以上。

要回答好这类问题，就需要对模型的**<mark>行程规划</mark>**（Itinerary）或称
**<mark>旅游规划</mark>**（Trip Planning）能力进行专门训练。

具体该怎么做呢？我们来尝试设计一个方案。

## 1.1 方案拆解

从非常高的 level 来说，要完成以上训练任务只需要做两件事情：

1. **<mark>数据集准备</mark>**：准备一批高质量的 Trip Planning 数据
2. **<mark>后训练</mark>**：基于高质量训练数据，对模型进行微调（SFT）或强化学习（RL）

本文接下来只关注第一个任务，<strong><mark>高质量数据集的准备</mark></strong>。

## 1.2 子任务：准备高质量的 Trip Planning 数据

再次从 high level 来说，这样的高质量数据集有两种来源：

1. <strong><mark>人工标注</mark></strong>：例如，找专业的旅行定制师或资深的旅行家，人工编写高质量的语料；
2. <strong><mark>自动合成</mark></strong>：通过某种不依赖人工的方式自动合成。

考虑到这个数据集不仅要求质量高，样本数量也要比较多，靠专业的人工标注成本是很高的，
而且人工标注方式的可扩展很差，因此我们接下来考虑<strong><mark>自动合成的方式</mark></strong>。

# 2 方案：自动合成高质量 Trip Planning 数据

## 2.1 思考：人（专家）怎么完成这个任务

先来设想一下，如果上面的旅行规划任务给到的是专业的旅游定制师或资深的旅行家，
他们是如何来完成这个任务的（也就是数据标注过程）。可能的工作流程：

1. 定制师或旅行家基于自己丰富的业务知识（城市、交通、景点、酒店、预算、偏好等等），
  初步判断下杭州出发三天能玩的目的地范围，得到一些<strong><mark>备选目的地</mark></strong>；
2. 针对这些备选目的地，以杭州为出发地，通过手动搜索或数据库查询，
  进一步充实交通、住宿、餐饮、景点、预算等需求，
  得到一些<strong><mark>备选线路</mark></strong>；
3. 针对这些备选线路，再进一步<strong><mark>验证</mark></strong>里面的每个具体步骤是否满足用户的要求，
  以及整体方案是否满足用户的要求；如果<strong><mark>满足就留下</mark></strong>这个线路；
  如果<strong><mark>不满足</mark></strong>（例如某一天的预算超了）就<strong><mark>进行相应的调整</mark></strong>直到满足，
  或者多次失败之后直接弃用这个备选路线；
4. 如果用户觉得上一步验证通过的线路还是<strong><mark>不够有吸引力</mark></strong>，
  则回到 step 1 or step 2 并顺序执行到 step 3，针对用户需求<strong><mark>重新设计</mark></strong>一些更有吸引力的线路。

经过以上步骤，最终得到的就是一些符合用户要求的高质量线路规划。

## 2.2 自动化：人工方案的 workflow 化

把以上的人工生产线路过程变成一个 workflow，就得到了一个基于 Agent 的自动化方案：

* 首先，我们得从某些地方获取一些 Trip Planning 相关的<strong><mark>基础旅游数据</mark></strong>，
  例如城市、交通、酒店、景点、价格等等信息，把它们存储起来备用；
* 接下来，得有一些<strong><mark>工具</mark></strong>来从这些数据中<strong><mark>筛选出我们想要的信息</mark></strong>，
  例如查询两个城市之间的交通方案、查询给定城市内的餐厅和景点等；
* 有了前两步的基础，剩下的就是<strong><mark>生成一个具体的旅行规划任务</mark></strong>，
  例如，“规划从上海到北京的三日游”，让 Agent 基于上一步提供的各种工具，帮我们将这个旅行规划方案设计出来。
  这个过程可以<strong><mark>进一步拆解为两个子任务</mark></strong>：
  1. 生成：<strong><mark>生成具体的旅行规划</mark></strong>；
  2. 验证：<strong><mark>验证生成的旅行规划是否符合用户的要求</mark></strong>。

基于以上流程，无需人工参与，就能自动完成一个行程规划任务，

* 如果<strong><mark>验证 OK，就将这个结果输出</mark></strong>；然后继续生成下一个（更难的）旅行规划任务；
* 如果失败，就要看问题是出在哪里，例如可能是工具不够、生成的方案不对、方案对但验证过程有问题等，尝试调整这几个环节，直到方案成功。

## 2.3 这个 workflow 的独特之处

这个 workflow 画成图大概长下面这样，跟普通 workflow 的重要区别是：
Agent 不仅生成任务本身（<strong><mark><code>task</code></mark></strong>），还生成完成这个任务的代码
（<strong><mark><code>solution function</code></mark></strong>）、工具代码（<strong><mark><code>tool functions</code></mark></strong>）
和验证结果的代码（<strong><mark><code>verification function</code></mark></strong>），
并通过动态执行这些代码筛选出符合用户要求的高质量结果。

<p align="center"><img src="/assets/img/deepseek-agentic-dataset-synthesizing/hypothetical-workflow.png" width="100%" height="100%"></p>
<p align="center">Hypothetical workflow</p>

* 图的上半部分可以叫“<strong><mark>生成环境</mark></strong>”，这是常规 LLM 擅长做的；
* 图的下半部分是“<strong><mark>执行环境</mark></strong>”，把上一步生成的代码真正拿来运行，再根据运行结果给 Agent 一个反馈，进入 Agent 的反思和下一次迭代流程。
* 整个方案的输入只有一段<strong><mark>提示词</mark></strong>（如果不算执行环境），其他都是 Agent+Workflow 创建和管理的。

## 2.4 小结

实际上，思考以上问题是因为在看 DeepSeek-V3.2 tech report 时刚好看到它有这样一个 case，觉得玩得很高级。
接下来我们看看 DeepSeek 在这种<strong><mark>合成高质量数据场景</mark></strong>的具体方案设计。

# 3 图解：DeepSeek-V3.2 是怎么做的（"Large-Scale Agentic Tasks"）

DeepSeek-V3.2 tech report 的 <strong><mark><code>3.2.3 Large-Scale Agentic Tasks</code></mark></strong>
介绍了他们是如何强化大规模 Agentic 任务的，其中就涉及到了数据集的合成，我们前面介绍的 "Trip Planning" 例子其实就是来自这里。

## 3.1 方案描述

原文：

> General Agent To scale up agent environments and tasks in RL, we employ an automatic
> environment-synthesis agent that synthesizes 1,827 task-oriented environments. These tasks are
> hard to solve but easy to verify. The synthesis workflow primarily consists of environment and
> toolset construction, task synthesis, and solution generation. Specifically, the workflow proceeds
> as follows.
>
> 1. Given a task category (e.g., planning a travel itinerary) and a sandbox equipped with a
>   bash and a search tool, the agent first uses these tools to generate or retrieve relevant data
>   from the Internet and store them in the sandbox database.
> 2. The agent then synthesizes a set of task-specific tools, each implemented as a function.
> 3. To create tasks that are both challenging and automatically verifiable, the agent initially
>   proposes a simple task based on the current database, along with its
>   solution and verification functions implemented in Python. The solution
>   function is restricted to invoking tool functions or performing logical
>   computations, and cannot call other functions or directly access the
>   database, ensuring the task can only be solved through the tool interface.
>   Additionally, the results produced by the solution function must be
>   validated by the verification function. If the solution is not validated,
>   the agent will modify the solution or verification
>   functions until the solution’s output passes the verification. The agent then iteratively
>   increases the difficulty of the task and updates the corresponding solution and verification
>   functions. During this iterative process, if the current toolset is not sufficient to solve the
>   task, the agent will augment the toolset.

为了扩展 RL 中的 agent 环境和任务，我们采用了一个自动的 environment-synthesis
agent，该 agent 合成了 1,827 个 task-oriented environments。
这些任务的特点是<strong><mark>解决起来很难，但验证很容易</mark></strong>。
该 synthesis workflow 主要包括 environment & toolset 构建、task
synthesis 以及 solution generation。

<strong><mark><code>Trip Planning</code></mark></strong> 是其中的任务类型之一。

## 3.2 方案图解

具体过程如下图所示（根据个人理解画的，仅供参考，因为很多细节原文没提）：

<p align="center"><img src="/assets/img/deepseek-agentic-dataset-synthesizing/agentic-dataset-synthesizing.png" width="100%" height="100%"></p>

核心是一个 Agent，接下来按序号介绍下各步骤。

### Step 0: Agent 输入

给 Agent 输入<strong><mark>任务类型</mark></strong>（e.g. "Trip Planning"）和可用的 sandbox 信息；

* 任务类型有很多种，旅行规划只是其中之一；
* sandbox 可以理解成一个 linux container，例如 <strong><mark><code>Ubuntu</code></mark></strong>，配置了 bash 和 search tool；

### Step 1: Agent 构建旅行数据库

Agent 开始干活，首先进入 sandbox，然后用 internet search tool
<strong><mark>从互联网搜索相关数据，并保存到 local database</mark></strong>；

* **输入**：任务类别（如 "trip planning"）+ 配备 `bash` 和 `search` 工具的 sandbox 环境
* **过程**：Agent 使用搜索工具从互联网爬取或生成结构化数据，包括交通、酒店、景点、门票、餐厅等等，存储到 sandbox 的数据库中
* **输出**：结构化数据表
* local database 可以想象成一个 <strong><mark><code>SQLite</code></mark></strong> 数据库

效果示意：

```
输入指令：请为"杭州三日游规划"任务准备基础数据

执行过程：
- 调用搜索工具查询"杭州 五星级酒店 2025"、"杭州 西湖景点"、"杭州 米其林餐厅"
- 调用 bash 工具解析搜索结果并写入 SQLite 数据库

输出（数据库内容）：
- cities 表: [杭州, 苏州, 上海, 南京]
- hotels 表: 
  ┌─────────────────┬────────┬────────┐
  │ hotel_name      │ city   │ price  │
  ├─────────────────┼────────┼────────┤
  │ Westlake Hotel  │ 杭州   │ 850    │
  │ Jinjiang Inn    │ 杭州   │ 450    │
  │ Nanjing Grand   │ 南京   │ 620    │
  └─────────────────┴────────┴────────┘
- attractions 表: [西湖, 灵隐寺, 中山陵, 拙政园]
- restaurants 表: 含评分、价格等字段
```

### Step 2: Agent 合成 tools（代码生成）

<strong><mark>合成这类任务所需的 tools</mark></strong>。
由于 Agent 非常清楚前一步的存储方式（例如，SQLite 表结构），因此生成 tools 非常简单，
可能就是一些查表的 SQL wrappers：

```python
def get_all_hotels_by_city(city: str) -> List[Dict]:
    """查询指定城市的所有酒店"""
    return db.query("SELECT * FROM hotels WHERE city = ?", city)

def get_infos_by_hotel(info_keywords: List[str], hotel: str) -> Dict:
    """获取酒店的详细信息（设施、政策等）"""
    return {...} # 从数据库或缓存中检索

def get_city_by_attraction(attraction: str) -> str:
    """查询景点所在城市"""
    return db.query_single("SELECT city FROM attractions WHERE name = ?", attraction)

def get_inter_city_transport(from_city: str, to_city: str) -> List[Dict]:
    """查询城市间交通"""
    return [...] # 调用外部 API 或查询本地数据

def submit_result(answer_text: str) -> bool:
    """提交最终答案"""
    return True
```

### Step 3: 合成一个具体旅行规划任务

任务的生成从易到难，既有挑战又要能自动验证，先从最简单的开始。

Agent 会为这个任务生成两个 python 函数：

* solution function：仅能调用 tool functions 或执行逻辑计算，不能调用其他 functions 或直接访问 database，从而确保该 task 只能通过 tool interface 来解决。
* verification function：对 solution function 的运行结果进行验证。

示例：

```python
task_description = "从杭州选择一家价格低于500元的酒店"

def solve_task_1() -> str:
    hotels = get_all_hotels_by_city("杭州")
    affordable = [h for h in hotels if h["price"] < 500]
    return affordable[0]["hotel_name"] if affordable else "无"

def verify_task_1(answer: str) -> bool: # 检查答案是否存在于数据库且满足约束
    if answer == "无": return True
    hotel = db.query("SELECT * FROM hotels WHERE hotel_name = ?", answer)
    return hotel["city"] == "杭州" and hotel["price"] < 500
```

### Step 4：执行 solution function，（基于 tool calling）生成一个线路规划

执行上面的 `solve_task_1()`，得到一个路线规划结果。
转 step 5。

### Step 5：执行 verification function，对上一步生成的线路规划进行验证

执行上面的 `verify_task_1()`，对上一步得到的路线进行验证。
转 step 6。

### Step 6: 如果验证成功，将这条数据输出

将这条数据以 `<environment, tools, task, verifier>` 的格式输出，这就是 DeepSeek-V3.2 下一阶段的一条训练样本；转 step 7。

### Step 7: 返回到 step 3，继续合成下一个更难的任务

难度迭代升级：Agent 会逐步增加约束条件，直到任务具有挑战性但可验证。举例：

| 迭代版本 | 新增约束 | 任务描述 |
|----------|----------|--------------|
| v1     | 无       | 选择一家酒店 |
| v2     | + 不重复 | 选择3家不同城市的酒店，不重复 |
| v3     | + 预算   | 第二天酒店若≥800元，则餐厅+景点总预算 `<` 350元 |
| v4     | + 逻辑链 | 完整的三天行程，含跨城交通，所有地点需满足城市归属验证 |

### Step 8: 如果 step 5 验证失败，也返回到 step 3

尝试修改 solution function 或 verification function，然后继续 step 4；如果是因为 tool 不够导致的失败，进入 step 9；

### Step 9: 将错误返回给 Agent，让 Agent 尝试扩充 toolset

## 3.3 官方 Trip Planning sample

官方文章中给的 Trip Planning 数据 sample 和输出格式、toolset：

<p align="center"><img src="/assets/img/deepseek-agentic-dataset-synthesizing/deepseek-trip-planning-sample.png" width="70%" height="70%"></p>

结构化的输出：

```json
[
{ "time": "2025-10-01", "city": "cite_name", "hotel": "hotel_name", "afternoon_restaurant": "restaurant_name", "afternoon_attraction": "attraction_name", "evening_restaurant": "restaurant_name" },
{ "time": "2025-10-02", "city": "cite_name", "hotel": "hotel_name", "afternoon_restaurant": "restaurant_name", "afternoon_attraction": "attraction_name", "evening_restaurant": "restaurant_name" },
{ "time": "2025-10-03", "city": "cite_name", "hotel": "hotel_name", "afternoon_restaurant": "restaurant_name", "afternoon_attraction": "attraction_name", "evening_restaurant": "restaurant_name" }
]
```

包含的字段：

1. 日期
2. 城市
3. 酒店名称
4. 午餐的餐厅名字
5. 下午游玩的景点的名字
6. 晚餐的餐厅名字

# 4 Kimi 老师补充的一些细节，帮助理解

向 kimi 老师问了几个问题，补充一些可能的细节，帮助更好地理解这个过程。
这一节可能存在误导，<strong><mark>仅供"仅供参考"</mark></strong>。

## 4.1 生成的 Task 示例

```python
# --- Task 4.0 (最终版本) ---
task_prompt = """
I'm planning a three-day trip starting from Hangzhou... [完整论文描述]
Requirements:
1. 不重复任何城市、酒店、景点、餐厅
2. 所有推荐地点必须位于当天住宿城市
3. 第二天预算规则：
   - 豪华酒店(≥800CNY): 餐厅总消费<350CNY且评分≥4.0，景点门票<120CNY
   - 中高档酒店(500-800CNY): 至少一家餐厅评分≥4.0，景点门票<180CNY
   - 经济酒店(200-500CNY): 至少一家餐厅评分≥3.2
"""

# 解决方案函数（Agent 生成）
def solve_trip_planning() -> List[Dict]:
    # 1. 搜索所有可能的城市组合
    cities = ["杭州", "苏州", "上海"]
    
    # 2. 为每天选择符合约束的酒店
    for day2_hotel in get_all_hotels_by_city("苏州"):
        if not validate_budget_rules(day2_hotel): continue
        
        # 3. 验证地点不重复
        used_places = {day2_hotel["hotel_name"]}
        
        # 4. 选择景点和餐厅...
        # 完整实现会涉及组合搜索和回溯
        plan = generate_valid_itinerary(cities, used_places)
        if plan: return plan
    
    return []

# 验证函数（Agent 生成）
def verify_trip_planning(answer: List[Dict]) -> bool:
    # 约束1: 无重复
    all_hotels = [d["hotel"] for d in answer]
    if len(all_hotels) != len(set(all_hotels)): return False
    
    # 约束2: 城市归属验证
    for day in answer:
        if get_city_by_hotel(day["hotel"]) != day["city"]:
            return False
        if get_city_by_restaurant(day["afternoon_restaurant"]) != day["city"]:
            return False
    
    # 约束3: 预算规则验证
    day2 = answer[1]
    hotel_price = get_infos_by_hotel(["price"], day2["hotel"])["price"]
    restaurant_cost = sum(get_infos_by_restaurant(["price"], r)["price"] 
                          for r in [day2["afternoon_restaurant"], day2["evening_restaurant"]])
    
    if hotel_price >= 800 and restaurant_cost >= 350:
        return False
    
    return True
```

## 4.2 输出样本要求

### 关键点

1. **可验证性**：所有任务都带有自动验证函数，支持 RL 训练中的奖励信号计算
2. **难度可控**：通过迭代增加约束，确保任务对当前模型有挑战性（论文表5显示 DeepSeek-V3.2-Exp 在合成任务上仅 12% 准确率）
3. **通用性**：Solution 函数必须**仅通过工具接口**访问数据，不能直接查询数据库，确保 RL 策略可迁移到真实环境
4. **规模**：最终生成了 **1,827 个环境 + 4,417 个任务**，覆盖旅行规划、代码工程、数学推理等多领域

该 workflow 的核心创新在于**将任务生成作为元学习问题**，让模型自动创造高质量、可验证的训练样本，解决了大规模 RL 训练中数据稀缺的瓶颈。

成功样本会被**筛选并持久化存储**，作为后续 RL 训练的离线数据集。

> "We then perform RL on this dataset using DeepSeek-V3.2 and retain only instances with **non-zero pass@100**, resulting in 1,827 environments and their corresponding tasks (4,417 in total)."

### 样本筛选标准

- **Pass@100 > 0**：在 100 次随机尝试中至少能成功一次的任务才保留
- 确保任务**可学习且非平凡**：避免过于简单或不可能完成的任务

### 样本保存格式

样本以 **四元组** 结构存储：

```shell
{
  "environment": { /* 数据库配置 */ },
  "tools": { /* 工具函数定义 */ },
  "task": { /* 任务描述 */ },
  "verifier": { /* 验证逻辑 */ }
}
```

---

### 输出样本示例（Trip Planning 任务）

以下是一个持久化样本：

```json
{
  "environment": {
    "description": "旅行规划数据库，包含长三角城市信息",
    "schema": {
      "cities": ["杭州", "苏州", "上海"],
      "hotels": [
        {"name": "Westlake Hotel", "city": "杭州", "price": 850, "rating": 4.8},
        {"name": "Jinjiang Inn", "city": "杭州", "price": 450, "rating": 4.0},
        {"name": "Suzhou Garden Hotel", "city": "苏州", "price": 720, "rating": 4.5},
        {"name": "Shanghai Grand", "city": "上海", "price": 680, "rating": 4.3}
      ],
      "restaurants": [
        {"name": "知味观", "city": "杭州", "price": 180, "rating": 4.2},
        {"name": "松鹤楼", "city": "苏州", "price": 220, "rating": 4.5},
        {"name": "南翔馒头店", "city": "上海", "price": 120, "rating": 3.8}
      ],
      "attractions": [
        {"name": "西湖", "city": "杭州", "ticket": 0},
        {"name": "拙政园", "city": "苏州", "ticket": 90},
        {"name": "外滩", "city": "上海", "ticket": 0}
      ]
    }
  },
  
  "tools": {
    "get_all_hotels_by_city": {
      "code": "def get_all_hotels_by_city(city):\n    return [h for h in db['hotels'] if h['city'] == city]",
      "signature": "(city: str) -> List[Dict]"
    },
    "get_city_by_hotel": {
      "code": "def get_city_by_hotel(hotel_name):\n    hotel = next((h for h in db['hotels'] if h['name'] == hotel_name), None)\n    return hotel['city'] if hotel else None",
      "signature": "(hotel_name: str) -> str"
    },
    "get_all_restaurants_by_city": {
      "code": "def get_all_restaurants_by_city(city):\n    return [r for r in db['restaurants'] if r['city'] == city]",
      "signature": "(city: str) -> List[Dict]"
    },
    "get_city_by_restaurant": {
      "code": "def get_city_by_restaurant(restaurant_name):\n    rest = next((r for r in db['restaurants'] if r['name'] == restaurant_name), None)\n    return rest['city'] if rest else None",
      "signature": "(restaurant_name: str) -> str"
    },
    "get_all_attractions_by_city": {
      "code": "def get_all_attractions_by_city(city):\n    return [a for a in db['attractions'] if a['city'] == city]",
      "signature": "(city: str) -> List[Dict]"
    },
    "submit_result": {
      "code": "def submit_result(answer_text):\n    return {'status': 'submitted', 'answer': answer_text}",
      "signature": "(answer_text: str) -> Dict"
    }
  },
  
  "task": {
    "id": "trip_planning_001",
    "difficulty_level": 3,
    "prompt": "I'm planning a three-day trip starting from Hangzhou... [完整要求，同论文] ... Can you help me put together this itinerary?",
    "expected_output_format": "[{\"time\":\"2025-10-01\",\"city\":\"...\",\"hotel\":\"...\",...}, {...}, {...}]",
    "max_tool_calls": 20
  },
  
  "verifier": {
    "code": "def verify_answer(answer):\n    import json\n    try:\n        plan = json.loads(answer)\n        # 约束1: 无重复\n        hotels = [d['hotel'] for d in plan]\n        if len(set(hotels)) != len(hotels): return False\n        \n        # 约束2: 城市归属验证\n        for day in plan:\n            if get_city_by_hotel(day['hotel']) != day['city']: return False\n            if get_city_by_restaurant(day['afternoon_restaurant']) != day['city']: return False\n            if get_city_by_restaurant(day['evening_restaurant']) != day['city']: return False\n            if get_city_by_attraction(day['afternoon_attraction']) != day['city']: return False\n        \n        # 约束3: 第二天预算规则验证\n        day2 = plan[1]\n        hotel_price = next(h['price'] for h in db['hotels'] if h['name'] == day2['hotel'])\n        restaurant_names = [day2['afternoon_restaurant'], day2['evening_restaurant']]\n        restaurant_cost = sum(next(r['price'] for r in db['restaurants'] if r['name'] == rn) for rn in restaurant_names)\n        \n        if hotel_price >= 800 and restaurant_cost >= 350:\n            return False\n        \n        return True\n    except Exception as e:\n        return False",
    "expected_reward": 1.0
  }
}
```

# 5 DeepSeek papers

1. 2025.12, [DeepSeek-V3.2 tech report](https://arxiv.org/abs/2512.02556)
2. 2025.09, [DeepSeek-V3.2-Exp tech report](https://github.com/deepseek-ai/DeepSeek-V3.2-Exp/blob/main/DeepSeek_V3_2.pdf)
3. 2025.08, DeepSeek-V3.1，no tech report
4. 2024, [DeepSeek-R1：通过强化学习激励大模型的推理能力]({% link _posts/2025-02-15-deepseek-r1-paper-zh.md %})
5. 2024, [DeepSeek-V3 tech report]()

----

<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-white.svg" alt="Written by Human, Not by AI"></a>
<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-black.svg" alt="Written by Human, Not by AI"></a>
