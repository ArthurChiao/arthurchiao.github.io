---
layout    : post
title     : "But What Is MCP (Model Context Protocol)? (2025)"
date      : 2025-03-23
lastupdate: 2025-03-23
categories: ai llm
---

There are already some good documents for MCP,

* [Model Context Protocol documentation](https://modelcontextprotocol.io)
* [Model Context Protocol specification](https://spec.modelcontextprotocol.io)
* [Officially supported servers](https://github.com/modelcontextprotocol/servers)

but developers and architects may still feel confusing on how it works in the
underlying, and this post try to fill the gap.

<p align="center"><img src="/assets/img/but-what-is-mcp/illustration-ai-app-with-mcp.png" width="100%" height="100%"></p>
<p align="center">Fig. Integrate external services to AI applications with MCP. Note
that MCP also supports connecting to local services (co-located with the AI
application) with the same client-server architecture.</p>

----

* TOC
{:toc}

----


# 1 What's MCP?

## 1.1 Naming

MCP is an abbreviation for **<mark><code>Model Context Protocol</code></mark>**. From the name, we can see that

* First of all, it's a **<mark>communication protocol</mark>**,
* Then, it's for **<mark>models</mark>** (LLMs),
* At last, it is used for **<mark>exchanging/passing model context</mark>**.

## 1.2 Why MCP?

When building agents or complex workflows on top of LLMs, it is often necessary to
**<mark>integrate with external data or tools</mark>** (e.g. external MySQL, Google Maps).
MCP provides a standardized way to do this.

Let's use an analogy to better explain it.

## 1.3 Analogy

Traditionally, personal computers have a variety of hardware connectors, such as USB, HDMI, DP, RJ45, etc.

<p align="center"><img src="/assets/img/but-what-is-mcp/hw-connectors.jpg" width="60%" height="60%"></p>
<p align="center">Various kinds of hardware connectors.<a href="https://www.ukposters.co.uk/usb-hdmi-ethernet-and-other-cable-and-port-icon-set-with-plugs-f369038160">Image Source</a></p>

Computer designers have to decide what devices that they would like to support during
the **<mark><code>design phase</code></mark>**, and then **<mark><code>pre-install the corresponding hardware interfaces</code></mark>** on the motherboard.
When new kinds of hardware connectors come in, it's impossible to support them
without changing the motherboard, or introducing new kinds of hardware
adapters.

### 1.3.1 USB type-c for computer

With the introduction of USB type-c specification, things have changed. USB type-c
is becoming the standard connector for most devices. As illustrated below,

<p align="center"><img src="/assets/img/but-what-is-mcp/illustration-usb-type-c.png" width="100%" height="100%"></p>
<p align="center">Fig. Peripheral devices connected to a computer's USB type-c hub with adapters.</p>

When the computer needs to connect to many peripherals, it first plugs in a USB
**<mark><code>type-c hub</code></mark>** (the actual hub generally supports multiple interfaces, not just
type-c), and for those peripheral devices,

* If they are already of type-c, they can connect to the hub directly;
* Otherwise, such as they are some old devices or professional devices in
  specific fields, they can be converted to type-c through a adapter first, then connecting to the hub.

So, **<mark>as long as a device supports (directly or through a converter) the type-c interface</mark>**,
it can be easily integrated to the computer.

### 1.3.2 MCP for AI Apps

MCP is like a USB-C port for AI applications. Just as USB-C
provides a standardized way to connect your devices to various peripherals and
accessories, MCP provides a standardized way to connect AI models to different
data sources and tools.

An analogy is shown below,

<p align="center"><img src="/assets/img/but-what-is-mcp/illustration-ai-app-with-mcp.png" width="100%" height="100%"></p>
<p align="center">Fig. Integrate external services to AI applications with MCP. Note
that MCP also supports connecting to local services (co-located with the AI
application) with the same client-server architecture.</p>

From the left to right,

| **<mark><code>Personal Computer</code></mark>** case | **<mark><code>AI App</code></mark>** case | Notes |
|:-----|:----|:----|
| Peripherals, such as monitors | **<mark><code>External data or services</code></mark>**, such as Google Translate | To be integrated into the AI application. They may use **<mark><code>various protocols</code></mark>**, such as HTTP, WebSocket, gRPC, Redis protocol, etc. |
| Connector adapters | **<mark><code>Protocol adaptation layer (server-side)</code></mark>** | **<mark><code>One MCP server for each external service</code></mark>**, providing a standardized interface (JSON-RPC) to the MCP client. |
| USB type-c hub | **<mark><code>Protocol adaptation layer (client-side)</code></mark>** | One MCP client for each external service, connecting the corresponding MCP server with standard protocol. |
| The personal computer | The AI app | The main part, integrate external services with the MCP clients. |
| | LLM layer | AI apps rely on LLM services for **<mark><code>function calling</code></mark>** to the external services with MCP. |

## 1.4 Summary

MCP is an open protocol that standardizes how applications provide context to LLMs. Think of MCP like a USB-C port for AI applications. Just as USB-C
provides a standardized way to connect your devices to various peripherals and
accessories, MCP provides a standardized way to connect AI models to different
data sources and tools.

# 2 Architecture & Spec

MCP follows the classic **<mark><code>client-server architecture</code></mark>**.

## 2.1 Base Protocol

* [JSON-RPC](https://www.jsonrpc.org/) message format
* Stateful connections
* Server and client capability negotiation

## 2.2 Server side

### MCP Primitives

The MCP protocol defines three core primitives that servers can implement:

| Primitive | Control                | Description                                       | Example Use                  |
| --------- | ---------------------- | ------------------------------------------------- | ---------------------------- |
| Prompts   | User-controlled        | Interactive templates invoked by user choice      | Slash commands, menu options |
| Resources | Application-controlled | Contextual data managed by the client application | File contents, API responses |
| Tools     | Model-controlled       | Functions exposed to the LLM to take actions      | API calls, data updates      |

### Server Capabilities

MCP servers declare capabilities during initialization:

| Capability   | Feature Flag              | Description                     |
| ------------ | ------------------------- | ------------------------------- |
| `prompts`    | `listChanged`             | Prompt template management      |
| `resources`  | `subscribe` `listChanged` | Resource exposure and updates   |
| `tools`      | `listChanged`             | Tool discovery and execution    |
| `logging`    | -                         | Server logging configuration    |
| `completion` | -                         | Argument completion suggestions |

## 2.3 Client side

Clients may offer the following feature to servers:

* **Sampling**: Server-initiated agentic behaviors and recursive LLM interactions

MCP client gets the server's capabilities through APIs such as `list_tools`.

Note that LLM is only responsible for selecting functions, the actual function calling is triggered inside the AI app.


## 2.4 Programming examples

* https://modelcontextprotocol.io/quickstart/client
* https://github.com/modelcontextprotocol/python-sdk

# 3 Function Call vs. MCP

Conceptually, MCP and Function call are both for AI applications to easily call
external services, but their work in different ways.  Let's take a look at the
workflow of a specific example —— accessing the Google Translate API —— and see the
difference between these two methods.

## 3.1 Function Call

<p align="center"><img src="/assets/img/but-what-is-mcp/function-call-flow.png" width="100%" height="100%"></p>
<p align="center">Fig. Function call workflow for accessing Google Translate.</p>

Steps:

1. AI app: **<mark><code>build prompt</code></mark>**, include the **<mark><code>function information</code></mark>**
  of the Google Translate API in the prompt;
2. AI app: **<mark><code>call LLM with the prompt</code></mark>**;
3. LLM: model response, with the **<mark><code>selected function</code></mark>** included;
4. AI app: **<mark><code>calling into the Google Translate API</code></mark>** with (HTTP/HTTPS);

## 3.2 MCP

The same scenario for MCP:

<p align="center"><img src="/assets/img/but-what-is-mcp/mcp-flow.png" width="100%" height="100%"></p>
<p align="center">Fig. MCP workflow for accessing Google Translate.</p>

Steps:

1. AI app: **<mark><code>init MCP client with the MCP server address</code></mark>** of Google Translate service;
2. MCP client: get the capabilities of Google Translate MCP server via MCP server's built-in **<mark><code>list_tools</code></mark>** API;
3. AI app: **<mark><code>build prompt</code></mark>**, include all the **<mark><code>function information</code></mark>**
  of the Google Translate API (got from step 2) in the prompt;
4. AI app: **<mark><code>call LLM with the prompt</code></mark>**;
5. LLM: model response, with the **<mark><code>selected function</code></mark>** included;
6. AI app: **<mark><code>calling into the proper Google Translate API</code></mark>** with MCP.

## 3.3 Comparison

|      | Function Call | MCP |
|:-----|:----|:----|
| Prior knowledge of the AI app (configurations) | **<mark>Exact function names and parameters</mark>** | **<mark>MCP server addresses</mark>** |
| Functions the AI apps can use | Static, only the pre-configured functions | **<mark>Dynamic</mark>**, all functions the MCP server exposed via `list_tools` interface |
| Flexibility | Low | High |
| Token consumption | Low | High. When building a prompt, too many functions' descriptions may be included into the prompt |

# 4 Limitations of current MCP

## 4.1 Cursor

https://docs.cursor.com/context/model-context-protocol#limitations

MCP is a very new protocol and is still in active development. There are some known caveats to be aware of:

### Tool Quantity

Some MCP servers, or user’s with many MCP servers active, may have many tools available for Cursor to use. Currently, Cursor will only send the first 40 tools to the Agent.

### Remote Development

Cursor directly communicates with MCP servers from your local machine, either directly through `stdio` or via the network using `sse`. Therefore, MCP servers may not work properly when accessing Cursor over SSH or other development environments. We are hoping to improve this in future releases.

### MCP Resources

MCP servers offer two main capabilities: tools and resources. Tools are availabe in Cursor today, and allow Cursor to execute the tools offered by an MCP server, and use the output in it’s further steps. However, resources are not yet supported in Cursor. We are hoping to add resource support in future releases.

# 问题总结

## MCP vs. A2A

TLDR; Agentic applications needs both A2A and MCP. We recommend MCP for tools and A2A for agents.

https://google.github.io/A2A/#/topics/a2a_and_mcp.md

![](https://google.github.io/A2A/images/a2a_mcp.png)

SSE: Server-Sent Events
https://medium.com/deliveryherotechhub/what-is-server-sent-events-sse-and-how-to-implement-it-904938bffd73

Websocket is a very popular technology that provides bi-directional data transfer for client and server communication on real-time applications. Websocket is not based on HTTP protocol, so it requires additional installation and integrations to use it.

![](https://miro.medium.com/v2/resize:fit:828/format:webp/1*ZOvd7h41rtYPVvxUcyP5Kw.png)

## 1. MCP 客户端问题/对接不同大模型的工作量

## 1. function 数量的问题

不管是 function call 还是 MCP，最后都需要将 function 列表作为 tools 传给 LLM，这个列表可能会很长，如何处理这个问题？

首先，太长可能会超过模型的上下文；

其次，即使没超过上下文长度，也会导致 token 的消耗过多。或者存在一些隐形限制，跟应用有关，例如

1. OpenAI 的最佳实践里建议不要超过 20 个 functions；https://platform.openai.com/docs/guides/function-calling/function-calling#best-practices-for-defining-functions
2. cursor 里只会发送前 40 个 tools 给 agent。

## 3. 强依赖提示词 + 大模型的 planning 能力

llamaindex 之类的框架，已经封装好了 mcp client 的能力，能避免这个问题。
https://docs.llamaindex.ai/en/stable/api_reference/tools/mcp/

----

<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-white.svg" alt="Written by Human, Not by AI"></a>
<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-black.svg" alt="Written by Human, Not by AI"></a>
