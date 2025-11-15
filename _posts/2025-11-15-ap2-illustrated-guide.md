---
layout    : post
title     : "An Illustrated Guide to AP2 (Agent Payment Protocol) (2025)"
date      : 2025-11-15
lastupdate: 2025-11-15
categories: ai llm ap2 a2a
---

With the rapid evolution of GenAI and the growing trend of <strong><mark><code>accomplishing more and more
tasks through chat</code></mark></strong>, can you imagine a day (perhaps in the near future) we
can <strong><mark><code>buy almost anything simply by chatting</code></mark></strong>? Instead of browsing e-commerce
sites, comparing products yourself, you’ll just tell your agent what you need.
It will <strong><mark><code>handle everything</code></mark></strong>: selecting options, comparing features, negotiating
prices, making payments, and ensuring the product arrives at the right place
and time.

To bring this vision to life, one essential piece is still missing: <strong><mark><code>a payment
protocol designed for agent-to-agent transactions</code></mark></strong>. That’s exactly why AP2 was
created.

This post offers an illustrative guide to this emerging topic.

<p align="center"><img src="/assets/img/ap2-illustrated-guide/shopping-agent-view.png" width="100%" height="100%"></p>
<p align="center">Fig. Shopping agent view of the "Buy a coffee maker" AP2 demo.
</p>

<p align="center"><img src="/assets/img/ap2-illustrated-guide/demo-call-flow.png" width="100%" height="100%"></p>
<p align="center">Fig. 
Call flow of the AP2 demo. Note: for clarity, the "Shopping Agent" shown
in this diagram combines the responsibilities of three distinct agents from the
actual demo: the shopping agent, address collection agent, and payment method
collection agent.
</p>

----

* TOC
{:toc}

----


# 1 Why AP2?

## 1.1 An Era of Agentic Commerce

The digital interaction fashion is likely to enter a new phase:

* Now and the past: people interact directly with <strong><mark><code>websites and applications</code></mark></strong>.
 Such as, people browse websites or apps, select the products they like and add to cart, and finally click the "Buy" or "Pay" button;
* The future: may shift toward an era of <strong><mark><code>conversational and delegated task</code></mark></strong> execution <strong><mark><code>via agents</code></mark></strong>;
  no manually browsing, <strong><mark><code>just chat with your AI assistant</code></mark></strong>.

This means agents will manage various daily tasks for users (humans), such as

* routine purchases
* complex product research
* price negotiations, and more.

This new era of <strong><mark><code>agentic commerce</code></mark></strong> will bring new opportunities for both users and businesses:

- For users: get a highly personalized, seamless shopping experience
- For businesses: open up a new, intelligent channel for reaching customers

## 1.2 AP2: Payment Protocol for Agents

The above mentiond scenario raises new challenges for payments, and it is in this background,
Google introduced the <strong><mark><code>Agent Payments Protocol (AP2)</code></mark></strong> in September, 2025:
[Powering AI commerce with the new Agent Payments Protocol (AP2)](https://cloud.google.com/blog/products/ai-machine-learning/announcing-agents-to-payments-ap2-protocol).

> Today, Google announced the Agent Payments Protocol (AP2), an open protocol
> developed with leading payments and technology companies to securely initiate
> and transact agent-led payments across platforms. The protocol can be used as
> <strong><mark><code>an extension of the Agent2Agent (A2A) protocol</code></mark></strong> and Model Context Protocol (MCP).
> In concert with industry rules and standards, it establishes a
> payment-agnostic framework for users, merchants, and payments providers to
> transact with confidence across all types of payment methods.

# 2 How AP2 Works

In a nutshell: <strong><mark><code>establishing trust via Mandates and Verifiable Credentials (VCs)</code></mark></strong>.

## 2.1 Core Concepts

### 2.1.1 Mandate

* Mandates are tamper-proof, <strong><mark><code>cryptographically-signed digital contracts</code></mark></strong>;
* Mandates serve as <strong><mark><code>verifiable proof of a user's instructions</code></mark></strong>;
* Mandates are <strong><mark><code>signed by VC</code></mark></strong>.

### 2.1.2 VC (Verifiable Credential)

* VC is a special kind of <strong><mark><code>data payload between agents</code></mark></strong>.

## 2.2 Working Fashions (Scenarios)

### 2.2.1 Real-time purchases (human present)

<p align="center"><img src="/assets/img/ap2-illustrated-guide/ap2-human-present.png" width="75%" height="75%"></p>
<p align="center">Image source: [1]</p>

1. `User -> Agent`: “Find me new white running shoes”
2. `Agent`: capture the request in an initial <strong><mark><code>IntentMandate</code></mark></strong>. This provides the auditable context for the entire interaction in a transaction process.
3. `Agent -> Merchant Agents`: find shoes with IntentMandate; get some candidates;
4. `Agent -> User`: present a cart with the shoes users would like;
5. `User`: select the item he/she likes;
6. `Agent`: sign a <strong><mark><code>CartMandate</code></mark></strong>. This is a critical step that creates a secure, unchangeable record of the exact items and price, ensuring what user see is what them pay for.
6. `Agent -> Merchant Agent & Credential Provider Agent`: complete payment with a <strong><mark><code>PaymentMandate</code></mark></strong>.

### 2.2.2 Delegated tasks (human not present)

<p align="center"><img src="/assets/img/ap2-illustrated-guide/ap2-human-not-present.png" width="75%" height="75%"></p>
<p align="center">Image source: [1]</p>

1. `User -> Agent`: “Buy concert tickets the moment they go on sale”.
2. `Agent`: the user signed a detailed Intent Mandate upfront. This mandate specifies the rules of engagement—price limits, timing, and other conditions.
3. `Agent -> Merchant Agent & Credential Provider Agent`: automatically generate a Cart Mandate on behalf of user once the precise conditions are met.

# 3 Demo: Buy A Coffee Maker Through Chat

This is a demo from AP2 community, see [github](https://github.com/google-agentic-commerce/AP2/blob/main/samples/python/scenarios/a2a/human-present/cards/README.md)
for the code and more details.

## 3.1 Components

The demo is a simple multi-agent system based on google ADK, this is what looks like when the demo finished:

<p align="center"><img src="/assets/img/ap2-illustrated-guide/root-agent-call-graph.png" width="100%" height="100%"></p>

It consists of the following components (agents):

1. Root Agent: for orchestrating all the entire demo
1. <strong><mark><code>Shopping agent</code></mark></strong>: chat-based agent that providing shopping services to User;
1. Shipping address collecting agent: utility agent for Root Agent;
1. Payment method collecting agent: utility agent for Root Agent;
1. <strong><mark><code>Merchant agent</code></mark></strong>: commerce agent that selling products;
1. <strong><mark><code>Merchant payment processor agent</code></mark></strong>: utility agent for Merchant agent that that handles payment stuffs for the latter;
1. <strong><mark><code>Payment credential provider agent</code></mark></strong>: providing AP2 auth between shopping agent and merchant agents;

## 3.2 Agent Card & System Prompt

### 3.2.1 Shopping Agent

System prompt to see how it works:

```python
shopper = RetryingLlmAgent(
    name="shopper",
    instruction="""
    You are an agent responsible for helping the user shop for products.

    %s

    When asked to complete a task, follow these instructions:
    1. Find out what the user is interested in purchasing.
    2. Ask clarifying questions one at a time to understand their needs fully.
      The shopping agent delegates responsibility for helping the user shop for
      products to this subagent. Help the user craft an IntentMandate that will
      be used to find relevant products for their purchase. Reason about the
      user's instructions and the information needed for the IntentMandate. The
      IntentMandate will be shown back to the user for confirmation so it's okay
      to make reasonable assumptions about the IntentMandate criteria initially.
      For example, inquire about:
        - A detailed description of the item.
        - Any preferred merchants or specific SKUs.
        - Whether the item needs to be refundable.
    3. After you have gathered what you believe is sufficient information,
      use the 'create_intent_mandate' tool with the collected information
      (user's description, and any other details they provided). Do not include
      any user guidance on price in the intent mandate. Use user's preference for
      the price as a filter when recommending products for the user to select
      from.
    4. Present the IntentMandate to the user in a clear, well-formatted summary.
      Start with the statement: "Please confirm the following details for your
      purchase. Note that this information will be shared with the merchant."
      And then has a row space and a breakdown of the details:
        Item Description: The natural_language_description. Never include any
          user guidance on price in the intent mandate.
        User Confirmation Required: A human-readable version of
        user_cart_confirmation_required (e.g., 'Yes', 'No').
        Merchants: A comma-separated list of merchants, or
        'Any' if not specified.
        SKUs: A comma-separated list of SKUs, or
        'Any' if not specified.
        Refundable: 'Yes' or 'No'.
        Expires: Convert the intent_expiry timestamp into a
        human-readable relative time (e.g., "in 1 hour", "in 2 days").

      After the breakdown, leave a blank line and end with: "Shall I proceed?"
    5. Once the user confirms, use the 'find_products' tool. It will
      return a list of `CartMandate` objects.
    6. For each CartMandate object in the list, create a visually distinct entry
      that includes the following details from the object:
          Item: Display the item_name clearly and in bold.
          Price: Present the total_price with the currency. Format the price
            with commas, and use the currency symbol (e.g., "$1,234.56").
          Expires: Convert the cart_expiry into a human-readable format
            (e.g., "in 2 hours," "by tomorrow at 5 PM").
          Refund Period: Convert the refund_period into a human-readable format
            (e.g., "30 days," "14 days").
      Present these details to the user in a clear way. If there are more than
      one CartMandate object, present them as a numbered list.
      At the bottom, present Sold by: Show the merchant_name
      associate the first Transaction.
      Ensure the cart you think matches the user's intent the most is presented
      at the top of the list. Add a 2-3 line summary of why you recommended the
      first option to the user.
    7. Ask the user which item they would like to purchase.
    8. After they choose, call the update_chosen_cart_mandate tool with the
      appropriate cart ID.
    9. Monitor the tool's output. If the cart ID is not found, you must inform
      the user and prompt them to try again. If the selection is successful,
      signal a successful update and hand off the process to the root_agent.
    """ % DEBUG_MODE_INSTRUCTIONS,
    tools=[
        tools.create_intent_mandate,
        tools.find_products,
        tools.update_chosen_cart_mandate,
    ],
)
```

### 3.2.2 Merchant Agent

A2A agent card:

```json
{
  "name": "MerchantAgent",
  "description": "A sales assistant agent for a merchant.",
  "skills": [
    {
      "description": "Searches the merchant's catalog based on a shopping intent & returns a cart containing the top results.",
      "id": "search_catalog",
      "name": "Search Catalog",
      "tags": [
        "merchant",
        "search",
        "catalog"
      ]
    }
  ],
  "capabilities": {
    "extensions": [
      {
        "description": "Supports the Agent Payments Protocol.",
        "required": true,
        "uri": "https://github.com/google-agentic-commerce/ap2/v1"
      },
      {
        "description": "Supports the Sample Card Network payment method extension",
        "required": true,
        "uri": "https://sample-card-network.github.io/paymentmethod/types/v1"
      }
    ]
  },
  "defaultInputModes": [
    "json"
  ],
  "defaultOutputModes": [
    "json"
  ],
  "preferredTransport": "JSONRPC",
  "protocolVersion": "0.3.0",
  "url": "http://localhost:8001/a2a/merchant_agent",
  "version": "1.0.0"
}
```

### 3.2.3 Merchant Payment Agent

A2A agent card:

```json
{
  "name": "merchant_payment_processor_agent",
  "description": "An agent that processes card payments on behalf of a merchant.",
  "skills": [
    {
      "description": "Processes card payments.",
      "id": "card-processor",
      "name": "Card Processor",
      "tags": [
        "payment",
        "card"
      ]
    }
  ],
  "capabilities": {
    "extensions": [
      {
        "description": "Supports the Agent Payments Protocol.",
        "required": true,
        "uri": "https://github.com/google-agentic-commerce/ap2/v1"
      },
      {
        "description": "Supports the Sample Card Network payment method extension",
        "required": true,
        "uri": "https://sample-card-network.github.io/paymentmethod/types/v1"
      }
    ]
  },
  "defaultInputModes": [
    "text/plain"
  ],
  "defaultOutputModes": [
    "application/json"
  ],
  "preferredTransport": "JSONRPC",
  "protocolVersion": "0.3.0",
  "url": "http://localhost:8003/a2a/merchant_payment_processor_agent",
  "version": "1.0.0"
}
```

### 3.2.4 Payment Credential Provider Agent

A2A agent card:

```json
{
  "name": "CredentialsProvider",
  "description": "An agent that holds a user's payment credentials.",
  "skills": [
    {
      "description": "Initiates a payment with the correct payment processor.",
      "id": "initiate_payment",
      "name": "Initiate Payment",
      "tags": [
        "payments"
      ]
    },
    {
      "description": "Provides a list of eligible payment methods for a particular purchase.",
      "id": "get_eligible_payment_methods",
      "name": "Get Eligible Payment Methods",
      "tags": [
        "eligible",
        "payment",
        "methods"
      ]
    },
    {
      "description": "Fetches the shipping address from a user's wallet.",
      "id": "get_account_shipping_address",
      "name": "Get Shipping Address",
      "tags": [
        "account",
        "shipping"
      ]
    }
  ],
  "capabilities": {
    "extensions": [
      {
        "description": "Supports the Agent Payments Protocol.",
        "required": true,
        "uri": "https://github.com/google-agentic-commerce/ap2/v1"
      },
      {
        "description": "Supports the Sample Card Network payment method extension",
        "required": true,
        "uri": "https://sample-card-network.github.io/paymentmethod/types/v1"
      }
    ]
  },
  "defaultInputModes": [
    "text/plain"
  ],
  "defaultOutputModes": [
    "application/json"
  ],
  "preferredTransport": "JSONRPC",
  "protocolVersion": "0.3.0",
  "url": "http://localhost:8002/a2a/credentials_provider",
  "version": "1.0.0"
}
```

Account Manager (User Database):

```python
"""An in-memory manager of a user's 'account details'.

Each 'account' contains a user's payment methods and shipping address.
For demonstration purposes, several accounts are pre-populated with sample data.
"""

_account_db = {
    "bugsbunny@gmail.com": {
        "shipping_address": {
            "recipient": "Bugs Bunny",
            "organization": "Sample Organization",
            "address_line": ["123 Main St"],
            "city": "Sample City",
            "region": "ST",
            "postal_code": "00000",
            "country": "US",
            "phone_number": "+1-000-000-0000",
        },
        "payment_methods": {
            "card1": {
                "type": "CARD",
                "alias": "American Express ending in 4444",
                "network": [{"name": "amex", "formats": ["DPAN"]}],
                "cryptogram": "fake_cryptogram_abc123",
                "token": "1111000000000000",
                "card_holder_name": "John Doe",
                "card_expiration": "12/2025",
                "card_billing_address": {
                    "country": "US",
                    "postal_code": "00000",
                },
            },
            "card2": {
                "type": "CARD",
                "alias": "American Express ending in 8888",
                "network": [{"name": "amex", "formats": ["DPAN"]}],
                "cryptogram": "fake_cryptogram_ghi789",
                "token": "2222000000000000",
                "card_holder_name": "Bugs Bunny",
                "card_expiration": "10/2027",
                "card_billing_address": {
                    "country": "US",
                    "postal_code": "00000",
                },
            },
            "bank_account1": {
                "type": "BANK_ACCOUNT",
                "account_number": "111",
                "alias": "Primary bank account",
            },
            "digital_wallet1": {
                "type": "DIGITAL_WALLET",
                "brand": "PayPal",
                "account_identifier": "foo@bar.com",
                "alias": "Bugs's PayPal account",
            },
        },
    },
    "daffyduck@gmail.com": {
        "payment_methods": {
            "bank_account1": {
                "type": "BANK_ACCOUNT",
                "brand": "Bank of Money",
                "account_number": "789",
                "alias": "Main checking account",
            }
        },
    },
    "elmerfudd@gmail.com": {
        "payment_methods": {
            "digital_wallet1": {
                "type": "DIGITAL_WALLET",
                "brand": "PayPal",
                "account_identifier": "elmerfudd@gmail.com",
                "alias": "Fudd's PayPal",
            }
        }
    },
}


_token = {}
```

```python
class CredentialsProviderExecutor(BaseServerExecutor):
  """AgentExecutor for the credentials provider agent."""

  _system_prompt = """
    You are a credentials provider agent acting as a secure digital wallet.
    Your job is to manage a user's payment methods and shipping addresses.

    Based on the user's request, identify their intent and select the
    single correct tool to use. Your only output should be a tool call.
    Do not engage in conversation.

    %s
  """ % DEBUG_MODE_INSTRUCTIONS

  def __init__(self, supported_extensions: list[dict[str, Any]] = None):
    agent_tools = [
        tools.handle_create_payment_credential_token,
        tools.handle_get_payment_method_raw_credentials,
        tools.handle_get_shipping_address,
        tools.handle_search_payment_methods,
        tools.handle_signed_payment_mandate,
    ]
```

## 3.3 Run The Demo (Chat to Buy a Coffee Maker)

Just follow the 
[README](https://github.com/google-agentic-commerce/AP2/blob/main/samples/python/scenarios/a2a/human-present/cards/README.md) to deploy it.

> <strong><mark><code>For Chinese users</code></mark></strong>, Gemini may block you by location (return `40x` responses), so you need to setup a proxy:
>
> ```shell
> $ export no_proxy=localhost; export http_proxy=YOUR_PROXY; export https_proxy=YOUR_PROXY; export GOOGLE_API_KEY=YOUR_KEY; bash samples/python/scenarios/a2a/human-present/cards/run.sh
> ```

Below is an intact chat session, from first query to payment completing.
Note that this example is designed to demonstrate the various capabilities and steps
within AP2, which is why it may appear intricate. In practice, the process can
be more streamlined than shown here.

<p align="center"><img src="/assets/img/ap2-illustrated-guide/one-chat-session.png" width="100%" height="100%"></p>

Let's see what's happened in the behind.

## 3.4 Detailed Traces

We have two ways to inspect what's happened in the behind. The first one is via the UI's built-in tracing capability:

<p align="center"><img src="/assets/img/ap2-illustrated-guide/root-agent-trace.png" width="90%" height="90%"></p>
<p align="center">Fig. 
</p>

## 3.5 Detailed A2A/AP2 Messages

The second way is diving into agent logs, which can give us more details.
Just pick some of them, from the `.logs/watch.log`, which combines all the A2A messages between agents in this demo.

### `ShoppingAgent -> MerchantAgent`: Find products matching user's IntentMandate

```shell
POST http://MerchantAgent/a2a/merchant_agent
[Request Body] {'id': '888a4384-2aa8-41c3-adbe-864c767bdba5', 'jsonrpc': '2.0', 'method': 'message/send', 'params': {'configuration': {'acceptedOutputModes': [], 'blocking': True}, 'message': {'kind': 'message', 'messageId': '00162a36c7d645d9840e3fbda5bd625e', 'parts': [{'kind': 'text', 'text': "Find products that match the user's IntentMandate."}, {'data': {'ap2.mandates.IntentMandate': {'user_cart_confirmation_required': True, 'natural_language_description': 'espresso coffee maker', 'merchants': [], 'skus': [], 'requires_refundability': True, 'intent_expiry': '2025-11-12T03:45:42.037007+00:00'}}, 'kind': 'data'}, {'data': {'risk_data': 'eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...fake_risk_data'}, 'kind': 'data'}, {'data': {'shopping_agent_id': 'trusted_shopping_agent'}, 'kind': 'data'}], 'role': 'agent'}}}
  [Extension Header] X-A2A-Extensions: https://github.com/google-agentic-commerce/ap2/v1
  [Request Instructions] ["Find products that match the user's IntentMandate."]
  [An Intent Mandate was in the request Data] {'user_cart_confirmation_required': True, 'natural_language_description': 'espresso coffee maker', 'merchants': [], 'skus': [], 'requires_refundability': True, 'intent_expiry': '2025-11-12T03:45:42.037007+00:00'}
  [Data Part: risk_data] eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...fake_risk_data
  [Data Part: shopping_agent_id] trusted_shopping_agent
[Response Body] {"id":"888a4384-2aa8-41c3-adbe-864c767bdba5","jsonrpc":"2.0","result":{"artifacts":[{"artifactId":"c0dad082-0c54-4f9a-963f-e312f5a4bf24","parts":[{"data":{"ap2.mandates.CartMandate":{"contents":{"id":"cart_1","user_cart_confirmation_required":true,"payment_request":{"method_data":[{"supported_methods":"CARD","data":{"network":["mastercard","paypal","amex"]}}],"details":{"id":"order_1","display_items":[{"label":"Compact espresso maker","amount":{"currency":"USD","value":89.99},"pending":null,"refund_period":30}],"shipping_options":null,"modifiers":null,"total":{"label":"Total","amount":{"currency":"USD","value":89.99},"pending":null,"refund_period":30}},"options":{"request_payer_name":false,"request_payer_email":false,"request_payer_phone":false,"request_shipping":true,"shipping_type":null},"shipping_address":null},"cart_expiry":"2025-11-11T04:15:58.088214+00:00","merchant_name":"Generic Merchant"},"merchant_authorization":null}},"kind":"data"}]},{"artifactId":"33680fca-e0b2-439a-bc1b-0f8ede344cb9","parts":[{"data":{"ap2.mandates.CartMandate":{"contents":{"id":"cart_2","user_cart_confirmation_required":true,"payment_request":{"method_data":[{"supported_methods":"CARD","data":{"network":["mastercard","paypal","amex"]}}],"details":{"id":"order_2","display_items":[{"label":"Automatic espresso and cappuccino machine","amount":{"currency":"USD","value":249.0},"pending":null,"refund_period":30}],"shipping_options":null,"modifiers":null,"total":{"label":"Total","amount":{"currency":"USD","value":249.0},"pending":null,"refund_period":30}},"options":{"request_payer_name":false,"request_payer_email":false,"request_payer_phone":false,"request_shipping":true,"shipping_type":null},"shipping_address":null},"cart_expiry":"2025-11-11T04:15:58.088214+00:00","merchant_name":"Generic Merchant"},"merchant_authorization":null}},"kind":"data"}]},{"artifactId":"d6dd431b-80a9-4892-b612-d4303524b674","parts":[{"data":{"ap2.mandates.CartMandate":{"contents":{"id":"cart_3","user_cart_confirmation_required":true,"payment_request":{"method_data":[{"supported_methods":"CARD","data":{"network":["mastercard","paypal","amex"]}}],"details":{"id":"order_3","display_items":[{"label":"Professional-grade espresso machine","amount":{"currency":"USD","value":599.99},"pending":false,"refund_period":60}],"shipping_options":null,"modifiers":null,"total":{"label":"Total","amount":{"currency":"USD","value":599.99},"pending":null,"refund_period":30}},"options":{"request_payer_name":false,"request_payer_email":false,"request_payer_phone":false,"request_shipping":true,"shipping_type":null},"shipping_address":null},"cart_expiry":"2025-11-11T04:15:58.088214+00:00","merchant_name":"Generic Merchant"},"merchant_authorization":null}},"kind":"data"}]}],"contextId":"6030ebc7-fde8-4489-b655-045443c47af0","history":[{"contextId":"6030ebc7-fde8-4489-b655-045443c47af0","kind":"message","messageId":"00162a36c7d645d9840e3fbda5bd625e","parts":[{"kind":"text","text":"Find products that match the user's IntentMandate."},{"data":{"ap2.mandates.IntentMandate":{"user_cart_confirmation_required":true,"natural_language_description":"espresso coffee maker","merchants":[],"skus":[],"requires_refundability":true,"intent_expiry":"2025-11-12T03:45:42.037007+00:00"}},"kind":"data"},{"data":{"risk_data":"eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...fake_risk_data"},"kind":"data"},{"data":{"shopping_agent_id":"trusted_shopping_agent"},"kind":"data"}],"role":"agent","taskId":"f76fdfa1-b505-4707-b1cc-a7f25bbadc00"}],"id":"f76fdfa1-b505-4707-b1cc-a7f25bbadc00","kind":"task","status":{"state":"completed","timestamp":"2025-11-11T03:45:58.161385+00:00"}}}
```

### `ShoppingAgent -> PaymentCredentialProviderAgent`: Get the user's shipping address

```shell
POST http://CredentialsProvider/a2a/credentials_provider
[Request Body] {'id': '03155305-f224-48c5-9617-d51474022d4c', 'jsonrpc': '2.0', 'method': 'message/send', 'params': {'configuration': {'acceptedOutputModes': [], 'blocking': True}, 'message': {'contextId': '6030ebc7-fde8-4489-b655-045443c47af0', 'kind': 'message', 'messageId': '8517c8ca101c4bde9b2fe4b0d52043af', 'parts': [{'kind': 'text', 'text': "Get the user's shipping address."}, {'data': {'user_email': 'bugsbunny@gmail.com'}, 'kind': 'data'}], 'role': 'agent'}}}
  [Extension Header] X-A2A-Extensions: https://github.com/google-agentic-commerce/ap2/v1
  [Request Instructions] ["Get the user's shipping address."]
  [Data Part: user_email] bugsbunny@gmail.com
[Response Body] {"id":"03155305-f224-48c5-9617-d51474022d4c","jsonrpc":"2.0","result":{"artifacts":[{"artifactId":"04dc9b8b-223d-432d-ad5b-ea513948b3be","parts":[{"data":{"contact_picker.ContactAddress":{"recipient":"Bugs Bunny","organization":"Sample Organization","address_line":["123 Main St"],"city":"Sample City","region":"ST","postal_code":"00000","country":"US","phone_number":"+1-000-000-0000"}},"kind":"data"}]}],"contextId":"6030ebc7-fde8-4489-b655-045443c47af0","history":[{"contextId":"6030ebc7-fde8-4489-b655-045443c47af0","kind":"message","messageId":"8517c8ca101c4bde9b2fe4b0d52043af","parts":[{"kind":"text","text":"Get the user's shipping address."},{"data":{"user_email":"bugsbunny@gmail.com"},"kind":"data"}],"role":"agent","taskId":"b9341fb9-d060-427e-b216-971f5ee3f72f"}],"id":"b9341fb9-d060-427e-b216-971f5ee3f72f","kind":"task","status":{"state":"completed","timestamp":"2025-11-11T03:49:03.656069+00:00"}}}
```

### `ShoppingAgent -> MerchantAgent`: Update the cart with the user's shipping address

```shell
POST http://MerchantAgent/a2a/merchant_agent
[Request Body] {'id': '82fb46ac-4ff8-4012-b70a-d85d528bfede', 'jsonrpc': '2.0', 'method': 'message/send', 'params': {'configuration': {'acceptedOutputModes': [], 'blocking': True}, 'message': {'contextId': '6030ebc7-fde8-4489-b655-045443c47af0', 'kind': 'message', 'messageId': 'bc9c493b5d9640a1a4a902c71ec10f39', 'parts': [{'kind': 'text', 'text': "Update the cart with the user's shipping address."}, {'data': {'cart_id': 'cart_3'}, 'kind': 'data'}, {'data': {'shipping_address': {'recipient': 'Bugs Bunny', 'region': 'ST', 'country': 'US', 'postal_code': '00000', 'organization': 'Sample Organization', 'phone_number': '+1-000-000-0000', 'city': 'Sample City', 'address_line': ['123 Main St']}}, 'kind': 'data'}, {'data': {'shopping_agent_id': 'trusted_shopping_agent'}, 'kind': 'data'}], 'role': 'agent'}}}
  [Extension Header] X-A2A-Extensions: https://github.com/google-agentic-commerce/ap2/v1
  [Request Instructions] ["Update the cart with the user's shipping address."]
  [Data Part: cart_id] cart_3
  [Data Part: shipping_address] {'recipient': 'Bugs Bunny', 'region': 'ST', 'country': 'US', 'postal_code': '00000', 'organization': 'Sample Organization', 'phone_number': '+1-000-000-0000', 'city': 'Sample City', 'address_line': ['123 Main St']}
  [Data Part: shopping_agent_id] trusted_shopping_agent
[Response Body] {"id":"82fb46ac-4ff8-4012-b70a-d85d528bfede","jsonrpc":"2.0","result":{"artifacts":[{"artifactId":"b88f3fa6-70a8-4382-a000-9b76d60c135d","parts":[{"data":{"ap2.mandates.CartMandate":{"contents":{"id":"cart_3","user_cart_confirmation_required":true,"payment_request":{"method_data":[{"supported_methods":"CARD","data":{"network":["mastercard","paypal","amex"]}}],"details":{"id":"order_3","display_items":[{"label":"Professional-grade espresso machine","amount":{"currency":"USD","value":603.49},"pending":false,"refund_period":60},{"label":"Shipping","amount":{"currency":"USD","value":2.0},"pending":null,"refund_period":30},{"label":"Tax","amount":{"currency":"USD","value":1.5},"pending":null,"refund_period":30}],"shipping_options":null,"modifiers":null,"total":{"label":"Total","amount":{"currency":"USD","value":603.49},"pending":null,"refund_period":30}},"options":{"request_payer_name":false,"request_payer_email":false,"request_payer_phone":false,"request_shipping":true,"shipping_type":null},"shipping_address":{"city":"Sample City","country":"US","dependent_locality":null,"organization":"Sample Organization","phone_number":"+1-000-000-0000","postal_code":"00000","recipient":"Bugs Bunny","region":"ST","sorting_code":null,"address_line":["123 Main St"]}},"cart_expiry":"2025-11-11T04:15:58.088214+00:00","merchant_name":"Generic Merchant"},"merchant_authorization":"eyJhbGciOiJSUzI1NiIsImtpZIwMjQwOTA..."}},"kind":"data"},{"data":{"risk_data":"eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...fake_risk_data"},"kind":"data"}]}],"contextId":"6030ebc7-fde8-4489-b655-045443c47af0","history":[{"contextId":"6030ebc7-fde8-4489-b655-045443c47af0","kind":"message","messageId":"bc9c493b5d9640a1a4a902c71ec10f39","parts":[{"kind":"text","text":"Update the cart with the user's shipping address."},{"data":{"cart_id":"cart_3"},"kind":"data"},{"data":{"shipping_address":{"recipient":"Bugs Bunny","region":"ST","country":"US","postal_code":"00000","organization":"Sample Organization","phone_number":"+1-000-000-0000","city":"Sample City","address_line":["123 Main St"]}},"kind":"data"},{"data":{"shopping_agent_id":"trusted_shopping_agent"},"kind":"data"}],"role":"agent","taskId":"43c9c925-df04-48a5-970b-6ec86bd3d27c"}],"id":"43c9c925-df04-48a5-970b-6ec86bd3d27c","kind":"task","status":{"state":"completed","timestamp":"2025-11-11T03:49:16.810434+00:00"}}}
```

### `ShoppingAgent -> PaymentCredentialProviderAgent`: Get a filtered list of the user's payment methods

```shell
POST http://CredentialsProvider/a2a/credentials_provider
[Request Body] {'id': '14885fe8-7637-4096-b997-6d58a0782b29', 'jsonrpc': '2.0', 'method': 'message/send', 'params': {'configuration': {'acceptedOutputModes': [], 'blocking': True}, 'message': {'contextId': '6030ebc7-fde8-4489-b655-045443c47af0', 'kind': 'message', 'messageId': 'f281b2f77101477e82de95aae26bea78', 'parts': [{'kind': 'text', 'text': "Get a filtered list of the user's payment methods."}, {'data': {'user_email': 'bugsbunny@gmail.com'}, 'kind': 'data'}, {'data': {'payment_request.PaymentMethodData': {'supported_methods': 'CARD', 'data': {'network': ['mastercard', 'paypal', 'amex']}}}, 'kind': 'data'}], 'role': 'agent'}}}
  [Extension Header] X-A2A-Extensions: https://github.com/google-agentic-commerce/ap2/v1
  [Request Instructions] ["Get a filtered list of the user's payment methods."]
  [Data Part: user_email] bugsbunny@gmail.com
  [Data Part: payment_request.PaymentMethodData] {'supported_methods': 'CARD', 'data': {'network': ['mastercard', 'paypal', 'amex']}}
[Response Body] {"id":"14885fe8-7637-4096-b997-6d58a0782b29","jsonrpc":"2.0","result":{"artifacts":[{"artifactId":"e605eb15-18ee-49c3-b7c7-05638e4b0ff6","parts":[{"data":{"payment_method_aliases":["American Express ending in 4444","American Express ending in 8888"]},"kind":"data"}]}],"contextId":"6030ebc7-fde8-4489-b655-045443c47af0","history":[{"contextId":"6030ebc7-fde8-4489-b655-045443c47af0","kind":"message","messageId":"f281b2f77101477e82de95aae26bea78","parts":[{"kind":"text","text":"Get a filtered list of the user's payment methods."},{"data":{"user_email":"bugsbunny@gmail.com"},"kind":"data"},{"data":{"payment_request.PaymentMethodData":{"supported_methods":"CARD","data":{"network":["mastercard","paypal","amex"]}}},"kind":"data"}],"role":"agent","taskId":"495725e5-2923-4552-9bfb-5fc0918d28de"}],"id":"495725e5-2923-4552-9bfb-5fc0918d28de","kind":"task","status":{"state":"completed","timestamp":"2025-11-11T03:49:31.574452+00:00"}}}
```

### `ShoppingAgent -> PaymentCredentialProviderAgent`: Get a payment credential token for the user's payment method

```shell
POST http://CredentialsProvider/a2a/credentials_provider 
[Request Body] {'id': 'e707b136-c1f6-4620-b330-59e19c4800d4', 'jsonrpc': '2.0', 'method': 'message/send', 'params': {'configuration': {'acceptedOutputModes': [], 'blocking': True}, 'message': {'contextId': '6030ebc7-fde8-4489-b655-045443c47af0', 'kind': 'message', 'messageId': '50e1e010700242ee995a7b9721e67f09', 'parts': [{'kind': 'text', 'text': "Get a payment credential token for the user's payment method."}, {'data': {'payment_method_alias': 'American Express ending in 4444'}, 'kind': 'data'}, {'data': {'user_email': 'bugsbunny@gmail.com'}, 'kind': 'data'}], 'role': 'agent'}}}
  [Extension Header] X-A2A-Extensions: https://github.com/google-agentic-commerce/ap2/v1
  [Request Instructions] ["Get a payment credential token for the user's payment method."]
  [Data Part: payment_method_alias] American Express ending in 4444
  [Data Part: user_email] bugsbunny@gmail.com
[Response Body] {"id":"e707b136-c1f6-4620-b330-59e19c4800d4","jsonrpc":"2.0","result":{"artifacts":[{"artifactId":"b2f8c50e-1bb0-4398-ae59-dee80926b667","parts":[{"data":{"token":"fake_payment_credential_token_0"},"kind":"data"}]}],"contextId":"6030ebc7-fde8-4489-b655-045443c47af0","history":[{"contextId":"6030ebc7-fde8-4489-b655-045443c47af0","kind":"message","messageId":"50e1e010700242ee995a7b9721e67f09","parts":[{"kind":"text","text":"Get a payment credential token for the user's payment method."},{"data":{"payment_method_alias":"American Express ending in 4444"},"kind":"data"},{"data":{"user_email":"bugsbunny@gmail.com"},"kind":"data"}],"role":"agent","taskId":"5ffdd520-93ab-4237-81e6-25e63765032a"}],"id":"5ffdd520-93ab-4237-81e6-25e63765032a","kind":"task","status":{"state":"completed","timestamp":"2025-11-11T03:49:57.616296+00:00"}}}
```

### `ShoppingAgent -> PaymentCredentialProviderAgent`: This is the signed payment mandate

```shell
POST http://CredentialsProvider/a2a/credentials_provider
[Request Body] {'id': '2fbe086c-6eab-46a1-b5c4-06e61ee3f90c', 'jsonrpc': '2.0', 'method': 'message/send', 'params': {'configuration': {'acceptedOutputModes': [], 'blocking': True}, 'message': {'contextId': '6030ebc7-fde8-4489-b655-045443c47af0', 'kind': 'message', 'messageId': 'cf8e7c0d0c534636bbd34619aea40486', 'parts': [{'kind': 'text', 'text': 'This is the signed payment mandate'}, {'data': {'ap2.mandates.PaymentMandate': {'payment_mandate_contents': {'payment_mandate_id': '848f97b287584cd1aa3085bed1985c22', 'payment_details_id': 'order_3', 'payment_details_total': {'label': 'Total', 'amount': {'currency': 'USD', 'value': 603.49}, 'refund_period': 30}, 'payment_response': {'request_id': 'order_3', 'method_name': 'CARD', 'details': {'token': {'value': 'fake_payment_credential_token_0', 'url': 'http://CredentialsProvider/a2a/credentials_provider'}}, 'shipping_address': {'city': 'Sample City', 'country': 'US', 'organization': 'Sample Organization', 'phone_number': '+1-000-000-0000', 'postal_code': '00000', 'recipient': 'Bugs Bunny', 'region': 'ST', 'address_line': ['123 Main St']}, 'payer_email': 'bugsbunny@gmail.com'}, 'merchant_agent': 'Generic Merchant', 'timestamp': '2025-11-11T03:50:04.532972+00:00'}, 'user_authorization': 'fake_cart_mandate_hash_cart_3_fake_payment_mandate_hash_848f97b287584cd1aa3085bed1985c22'}}, 'kind': 'data'}, {'data': {'risk_data': 'eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...fake_risk_data'}, 'kind': 'data'}], 'role': 'agent'}}}
  [Extension Header] X-A2A-Extensions: https://github.com/google-agentic-commerce/ap2/v1
  [Request Instructions] ['This is the signed payment mandate']
  [A Payment Mandate was in the request Data] {'payment_mandate_contents': {'payment_mandate_id': '848f97b287584cd1aa3085bed1985c22', 'payment_details_id': 'order_3', 'payment_details_total': {'label': 'Total', 'amount': {'currency': 'USD', 'value': 603.49}, 'refund_period': 30}, 'payment_response': {'request_id': 'order_3', 'method_name': 'CARD', 'details': {'token': {'value': 'fake_payment_credential_token_0', 'url': 'http://CredentialsProvider/a2a/credentials_provider'}}, 'shipping_address': {'city': 'Sample City', 'country': 'US', 'organization': 'Sample Organization', 'phone_number': '+1-000-000-0000', 'postal_code': '00000', 'recipient': 'Bugs Bunny', 'region': 'ST', 'address_line': ['123 Main St']}, 'payer_email': 'bugsbunny@gmail.com'}, 'merchant_agent': 'Generic Merchant', 'timestamp': '2025-11-11T03:50:04.532972+00:00'}, 'user_authorization': 'fake_cart_mandate_hash_cart_3_fake_payment_mandate_hash_848f97b287584cd1aa3085bed1985c22'}
  [Data Part: risk_data] eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...fake_risk_data
[Response Body] {"id":"2fbe086c-6eab-46a1-b5c4-06e61ee3f90c","jsonrpc":"2.0","result":{"contextId":"6030ebc7-fde8-4489-b655-045443c47af0","history":[{"contextId":"6030ebc7-fde8-4489-b655-045443c47af0","kind":"message","messageId":"cf8e7c0d0c534636bbd34619aea40486","parts":[{"kind":"text","text":"This is the signed payment mandate"},{"data":{"ap2.mandates.PaymentMandate":{"payment_mandate_contents":{"payment_mandate_id":"848f97b287584cd1aa3085bed1985c22","payment_details_id":"order_3","payment_details_total":{"label":"Total","amount":{"currency":"USD","value":603.49},"refund_period":30},"payment_response":{"request_id":"order_3","method_name":"CARD","details":{"token":{"value":"fake_payment_credential_token_0","url":"http://CredentialsProvider/a2a/credentials_provider"}},"shipping_address":{"city":"Sample City","country":"US","organization":"Sample Organization","phone_number":"+1-000-000-0000","postal_code":"00000","recipient":"Bugs Bunny","region":"ST","address_line":["123 Main St"]},"payer_email":"bugsbunny@gmail.com"},"merchant_agent":"Generic Merchant","timestamp":"2025-11-11T03:50:04.532972+00:00"},"user_authorization":"fake_cart_mandate_hash_cart_3_fake_payment_mandate_hash_848f97b287584cd1aa3085bed1985c22"}},"kind":"data"},{"data":{"risk_data":"eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...fake_risk_data"},"kind":"data"}],"role":"agent","taskId":"fc67d42e-57f9-4efe-8d2a-7d7f3c31d70f"}],"id":"fc67d42e-57f9-4efe-8d2a-7d7f3c31d70f","kind":"task","status":{"state":"completed","timestamp":"2025-11-11T03:51:06.650655+00:00"}}}
```

### `ShoppingAgent -> MerchantAgent`: Initiate a payment

```shell
POST http://MerchantAgent/a2a/merchant_agent
[Request Body] {'id': 'be1ef52c-fd9d-4177-810d-cd14303219f1', 'jsonrpc': '2.0', 'method': 'message/send', 'params': {'configuration': {'acceptedOutputModes': [], 'blocking': True}, 'message': {'contextId': '6030ebc7-fde8-4489-b655-045443c47af0', 'kind': 'message', 'messageId': '14ed8b94ec5a4cc0a516a7b8d62cc6f8', 'parts': [{'kind': 'text', 'text': 'Initiate a payment'}, {'data': {'ap2.mandates.PaymentMandate': {'payment_mandate_contents': {'payment_mandate_id': '848f97b287584cd1aa3085bed1985c22', 'payment_details_id': 'order_3', 'payment_details_total': {'label': 'Total', 'amount': {'currency': 'USD', 'value': 603.49}, 'refund_period': 30}, 'payment_response': {'request_id': 'order_3', 'method_name': 'CARD', 'details': {'token': {'value': 'fake_payment_credential_token_0', 'url': 'http://CredentialsProvider/a2a/credentials_provider'}}, 'shipping_address': {'city': 'Sample City', 'country': 'US', 'organization': 'Sample Organization', 'phone_number': '+1-000-000-0000', 'postal_code': '00000', 'recipient': 'Bugs Bunny', 'region': 'ST', 'address_line': ['123 Main St']}, 'payer_email': 'bugsbunny@gmail.com'}, 'merchant_agent': 'Generic Merchant', 'timestamp': '2025-11-11T03:50:04.532972+00:00'}, 'user_authorization': 'fake_cart_mandate_hash_cart_3_fake_payment_mandate_hash_848f97b287584cd1aa3085bed1985c22'}}, 'kind': 'data'}, {'data': {'risk_data': 'eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...fake_risk_data'}, 'kind': 'data'}, {'data': {'shopping_agent_id': 'trusted_shopping_agent'}, 'kind': 'data'}], 'role': 'agent'}}}
  [Extension Header] X-A2A-Extensions: https://github.com/google-agentic-commerce/ap2/v1
  [Request Instructions] ['Initiate a payment']
  [A Payment Mandate was in the request Data] {'payment_mandate_contents': {'payment_mandate_id': '848f97b287584cd1aa3085bed1985c22', 'payment_details_id': 'order_3', 'payment_details_total': {'label': 'Total', 'amount': {'currency': 'USD', 'value': 603.49}, 'refund_period': 30}, 'payment_response': {'request_id': 'order_3', 'method_name': 'CARD', 'details': {'token': {'value': 'fake_payment_credential_token_0', 'url': 'http://CredentialsProvider/a2a/credentials_provider'}}, 'shipping_address': {'city': 'Sample City', 'country': 'US', 'organization': 'Sample Organization', 'phone_number': '+1-000-000-0000', 'postal_code': '00000', 'recipient': 'Bugs Bunny', 'region': 'ST', 'address_line': ['123 Main St']}, 'payer_email': 'bugsbunny@gmail.com'}, 'merchant_agent': 'Generic Merchant', 'timestamp': '2025-11-11T03:50:04.532972+00:00'}, 'user_authorization': 'fake_cart_mandate_hash_cart_3_fake_payment_mandate_hash_848f97b287584cd1aa3085bed1985c22'}
  [Data Part: risk_data] eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...fake_risk_data
  [Data Part: shopping_agent_id] trusted_shopping_agent
```

### `MerchantAgent -> MerchantPaymentAgent`: Initiate a payment

```shell
POST http://merchant_payment_processor_agent/a2a/merchant_payment_processor_agent
[Request Body] {'id': '7c7fbb73-4bb6-42b2-b5bd-d6d766078cca', 'jsonrpc': '2.0', 'method': 'message/send', 'params': {'configuration': {'acceptedOutputModes': [], 'blocking': True}, 'message': {'contextId': '6030ebc7-fde8-4489-b655-045443c47af0', 'kind': 'message', 'messageId': '85dc6b61ae8e4e23bc8d14fc02ca14eb', 'parts': [{'kind': 'text', 'text': 'initiate_payment'}, {'data': {'ap2.mandates.PaymentMandate': {'payment_mandate_contents': {'payment_mandate_id': '848f97b287584cd1aa3085bed1985c22', 'payment_details_id': 'order_3', 'payment_details_total': {'label': 'Total', 'amount': {'currency': 'USD', 'value': 603.49}, 'pending': None, 'refund_period': 30}, 'payment_response': {'request_id': 'order_3', 'method_name': 'CARD', 'details': {'token': {'value': 'fake_payment_credential_token_0', 'url': 'http://CredentialsProvider/a2a/credentials_provider'}}, 'shipping_address': {'city': 'Sample City', 'country': 'US', 'dependent_locality': None, 'organization': 'Sample Organization', 'phone_number': '+1-000-000-0000', 'postal_code': '00000', 'recipient': 'Bugs Bunny', 'region': 'ST', 'sorting_code': None, 'address_line': ['123 Main St']}, 'shipping_option': None, 'payer_name': None, 'payer_email': 'bugsbunny@gmail.com', 'payer_phone': None}, 'merchant_agent': 'Generic Merchant', 'timestamp': '2025-11-11T03:50:04.532972+00:00'}, 'user_authorization': 'fake_cart_mandate_hash_cart_3_fake_payment_mandate_hash_848f97b287584cd1aa3085bed1985c22'}}, 'kind': 'data'}, {'data': {'risk_data': 'eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...fake_risk_data'}, 'kind': 'data'}], 'role': 'agent'}}}
  [Extension Header] X-A2A-Extensions: https://github.com/google-agentic-commerce/ap2/v1
  [Request Instructions] ['initiate_payment']
  [A Payment Mandate was in the request Data] {'payment_mandate_contents': {'payment_mandate_id': '848f97b287584cd1aa3085bed1985c22', 'payment_details_id': 'order_3', 'payment_details_total': {'label': 'Total', 'amount': {'currency': 'USD', 'value': 603.49}, 'pending': None, 'refund_period': 30}, 'payment_response': {'request_id': 'order_3', 'method_name': 'CARD', 'details': {'token': {'value': 'fake_payment_credential_token_0', 'url': 'http://CredentialsProvider/a2a/credentials_provider'}}, 'shipping_address': {'city': 'Sample City', 'country': 'US', 'dependent_locality': None, 'organization': 'Sample Organization', 'phone_number': '+1-000-000-0000', 'postal_code': '00000', 'recipient': 'Bugs Bunny', 'region': 'ST', 'sorting_code': None, 'address_line': ['123 Main St']}, 'shipping_option': None, 'payer_name': None, 'payer_email': 'bugsbunny@gmail.com', 'payer_phone': None}, 'merchant_agent': 'Generic Merchant', 'timestamp': '2025-11-11T03:50:04.532972+00:00'}, 'user_authorization': 'fake_cart_mandate_hash_cart_3_fake_payment_mandate_hash_848f97b287584cd1aa3085bed1985c22'}
  [Data Part: risk_data] eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...fake_risk_data
[Response Body] {"id":"7c7fbb73-4bb6-42b2-b5bd-d6d766078cca","jsonrpc":"2.0","result":{"contextId":"6030ebc7-fde8-4489-b655-045443c47af0","history":[{"contextId":"6030ebc7-fde8-4489-b655-045443c47af0","kind":"message","messageId":"85dc6b61ae8e4e23bc8d14fc02ca14eb","parts":[{"kind":"text","text":"initiate_payment"},{"data":{"ap2.mandates.PaymentMandate":{"payment_mandate_contents":{"payment_mandate_id":"848f97b287584cd1aa3085bed1985c22","payment_details_id":"order_3","payment_details_total":{"label":"Total","amount":{"currency":"USD","value":603.49},"pending":null,"refund_period":30},"payment_response":{"request_id":"order_3","method_name":"CARD","details":{"token":{"value":"fake_payment_credential_token_0","url":"http://CredentialsProvider/a2a/credentials_provider"}},"shipping_address":{"city":"Sample City","country":"US","dependent_locality":null,"organization":"Sample Organization","phone_number":"+1-000-000-0000","postal_code":"00000","recipient":"Bugs Bunny","region":"ST","sorting_code":null,"address_line":["123 Main St"]},"shipping_option":null,"payer_name":null,"payer_email":"bugsbunny@gmail.com","payer_phone":null},"merchant_agent":"Generic Merchant","timestamp":"2025-11-11T03:50:04.532972+00:00"},"user_authorization":"fake_cart_mandate_hash_cart_3_fake_payment_mandate_hash_848f97b287584cd1aa3085bed1985c22"}},"kind":"data"},{"data":{"risk_data":"eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...fake_risk_data"},"kind":"data"}],"role":"agent","taskId":"799fbe91-a538-497f-904c-d81eda1dedbf"}],"id":"799fbe91-a538-497f-904c-d81eda1dedbf","kind":"task","status":{"message":{"contextId":"6030ebc7-fde8-4489-b655-045443c47af0","kind":"message","messageId":"965f5ae8-0cb6-4687-9a3c-64267fe165da","parts":[{"kind":"text","text":"Please provide the challenge response to complete the payment."},{"data":{"challenge":{"type":"otp","display_text":"The payment method issuer sent a verification code to the phone number on file, please enter it below. It will be shared with the issuer so they can authorize the transaction.(Demo only hint: the code is 123)"}},"kind":"data"}],"role":"agent","taskId":"799fbe91-a538-497f-904c-d81eda1dedbf"},"state":"input-required","timestamp":"2025-11-11T03:51:20.214669+00:00"}}}
[Response Body] {"id":"be1ef52c-fd9d-4177-810d-cd14303219f1","jsonrpc":"2.0","result":{"contextId":"6030ebc7-fde8-4489-b655-045443c47af0","history":[{"contextId":"6030ebc7-fde8-4489-b655-045443c47af0","kind":"message","messageId":"14ed8b94ec5a4cc0a516a7b8d62cc6f8","parts":[{"kind":"text","text":"Initiate a payment"},{"data":{"ap2.mandates.PaymentMandate":{"payment_mandate_contents":{"payment_mandate_id":"848f97b287584cd1aa3085bed1985c22","payment_details_id":"order_3","payment_details_total":{"label":"Total","amount":{"currency":"USD","value":603.49},"refund_period":30},"payment_response":{"request_id":"order_3","method_name":"CARD","details":{"token":{"value":"fake_payment_credential_token_0","url":"http://CredentialsProvider/a2a/credentials_provider"}},"shipping_address":{"city":"Sample City","country":"US","organization":"Sample Organization","phone_number":"+1-000-000-0000","postal_code":"00000","recipient":"Bugs Bunny","region":"ST","address_line":["123 Main St"]},"payer_email":"bugsbunny@gmail.com"},"merchant_agent":"Generic Merchant","timestamp":"2025-11-11T03:50:04.532972+00:00"},"user_authorization":"fake_cart_mandate_hash_cart_3_fake_payment_mandate_hash_848f97b287584cd1aa3085bed1985c22"}},"kind":"data"},{"data":{"risk_data":"eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...fake_risk_data"},"kind":"data"},{"data":{"shopping_agent_id":"trusted_shopping_agent"},"kind":"data"}],"role":"agent","taskId":"57a672e8-478b-4c7a-8885-00388224e886"}],"id":"57a672e8-478b-4c7a-8885-00388224e886","kind":"task","status":{"message":{"contextId":"6030ebc7-fde8-4489-b655-045443c47af0","kind":"message","messageId":"965f5ae8-0cb6-4687-9a3c-64267fe165da","parts":[{"kind":"text","text":"Please provide the challenge response to complete the payment."},{"data":{"challenge":{"type":"otp","display_text":"The payment method issuer sent a verification code to the phone number on file, please enter it below. It will be shared with the issuer so they can authorize the transaction.(Demo only hint: the code is 123)"}},"kind":"data"}],"role":"agent","taskId":"799fbe91-a538-497f-904c-d81eda1dedbf"},"state":"input-required","timestamp":"2025-11-11T03:51:20.217209+00:00"}}}
```

### `ShoppingAgent -> MerchantAgent`: Initiate a payment. Include the challenge response.

```shell
POST http://MerchantAgent/a2a/merchant_agent
[Request Body] {'id': '716d25d2-2541-41b7-bd8a-2f94465a91d1', 'jsonrpc': '2.0', 'method': 'message/send', 'params': {'configuration': {'acceptedOutputModes': [], 'blocking': True}, 'message': {'contextId': '6030ebc7-fde8-4489-b655-045443c47af0', 'kind': 'message', 'messageId': '4f466784348444b58d547a64f42d31ca', 'parts': [{'kind': 'text', 'text': 'Initiate a payment. Include the challenge response.'}, {'data': {'ap2.mandates.PaymentMandate': {'payment_mandate_contents': {'payment_mandate_id': '848f97b287584cd1aa3085bed1985c22', 'payment_details_id': 'order_3', 'payment_details_total': {'label': 'Total', 'amount': {'currency': 'USD', 'value': 603.49}, 'refund_period': 30}, 'payment_response': {'request_id': 'order_3', 'method_name': 'CARD', 'details': {'token': {'value': 'fake_payment_credential_token_0', 'url': 'http://CredentialsProvider/a2a/credentials_provider'}}, 'shipping_address': {'city': 'Sample City', 'country': 'US', 'organization': 'Sample Organization', 'phone_number': '+1-000-000-0000', 'postal_code': '00000', 'recipient': 'Bugs Bunny', 'region': 'ST', 'address_line': ['123 Main St']}, 'payer_email': 'bugsbunny@gmail.com'}, 'merchant_agent': 'Generic Merchant', 'timestamp': '2025-11-11T03:50:04.532972+00:00'}, 'user_authorization': 'fake_cart_mandate_hash_cart_3_fake_payment_mandate_hash_848f97b287584cd1aa3085bed1985c22'}}, 'kind': 'data'}, {'data': {'shopping_agent_id': 'trusted_shopping_agent'}, 'kind': 'data'}, {'data': {'challenge_response': '123'}, 'kind': 'data'}, {'data': {'risk_data': 'eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...fake_risk_data'}, 'kind': 'data'}], 'role': 'agent', 'taskId': '57a672e8-478b-4c7a-8885-00388224e886'}}}
  [Extension Header] X-A2A-Extensions: https://github.com/google-agentic-commerce/ap2/v1
  [Request Instructions] ['Initiate a payment. Include the challenge response.']
  [A Payment Mandate was in the request Data] {'payment_mandate_contents': {'payment_mandate_id': '848f97b287584cd1aa3085bed1985c22', 'payment_details_id': 'order_3', 'payment_details_total': {'label': 'Total', 'amount': {'currency': 'USD', 'value': 603.49}, 'refund_period': 30}, 'payment_response': {'request_id': 'order_3', 'method_name': 'CARD', 'details': {'token': {'value': 'fake_payment_credential_token_0', 'url': 'http://CredentialsProvider/a2a/credentials_provider'}}, 'shipping_address': {'city': 'Sample City', 'country': 'US', 'organization': 'Sample Organization', 'phone_number': '+1-000-000-0000', 'postal_code': '00000', 'recipient': 'Bugs Bunny', 'region': 'ST', 'address_line': ['123 Main St']}, 'payer_email': 'bugsbunny@gmail.com'}, 'merchant_agent': 'Generic Merchant', 'timestamp': '2025-11-11T03:50:04.532972+00:00'}, 'user_authorization': 'fake_cart_mandate_hash_cart_3_fake_payment_mandate_hash_848f97b287584cd1aa3085bed1985c22'}
  [Data Part: shopping_agent_id] trusted_shopping_agent
  [Data Part: challenge_response] 123
  [Data Part: risk_data] eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...fake_risk_data
```

### `MerchantAgent -> MerchantPaymentAgent`: Initiate a payment (include the challenge response)

```shell
POST http://merchant_payment_processor_agent/a2a/merchant_payment_processor_agent
[Request Body] {'id': 'd9bc9bc6-73bb-4667-949b-85a60633089b', 'jsonrpc': '2.0', 'method': 'message/send', 'params': {'configuration': {'acceptedOutputModes': [], 'blocking': True}, 'message': {'contextId': '6030ebc7-fde8-4489-b655-045443c47af0', 'kind': 'message', 'messageId': 'f33f4dc30d3a41878c8d1d7006b2cf0e', 'parts': [{'kind': 'text', 'text': 'initiate_payment'}, {'data': {'ap2.mandates.PaymentMandate': {'payment_mandate_contents': {'payment_mandate_id': '848f97b287584cd1aa3085bed1985c22', 'payment_details_id': 'order_3', 'payment_details_total': {'label': 'Total', 'amount': {'currency': 'USD', 'value': 603.49}, 'pending': None, 'refund_period': 30}, 'payment_response': {'request_id': 'order_3', 'method_name': 'CARD', 'details': {'token': {'value': 'fake_payment_credential_token_0', 'url': 'http://CredentialsProvider/a2a/credentials_provider'}}, 'shipping_address': {'city': 'Sample City', 'country': 'US', 'dependent_locality': None, 'organization': 'Sample Organization', 'phone_number': '+1-000-000-0000', 'postal_code': '00000', 'recipient': 'Bugs Bunny', 'region': 'ST', 'sorting_code': None, 'address_line': ['123 Main St']}, 'shipping_option': None, 'payer_name': None, 'payer_email': 'bugsbunny@gmail.com', 'payer_phone': None}, 'merchant_agent': 'Generic Merchant', 'timestamp': '2025-11-11T03:50:04.532972+00:00'}, 'user_authorization': 'fake_cart_mandate_hash_cart_3_fake_payment_mandate_hash_848f97b287584cd1aa3085bed1985c22'}}, 'kind': 'data'}, {'data': {'risk_data': 'eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...fake_risk_data'}, 'kind': 'data'}, {'data': {'challenge_response': '123'}, 'kind': 'data'}], 'role': 'agent', 'taskId': '799fbe91-a538-497f-904c-d81eda1dedbf'}}}
  [Extension Header] X-A2A-Extensions: https://github.com/google-agentic-commerce/ap2/v1
  [Request Instructions] ['initiate_payment']
  [A Payment Mandate was in the request Data] {'payment_mandate_contents': {'payment_mandate_id': '848f97b287584cd1aa3085bed1985c22', 'payment_details_id': 'order_3', 'payment_details_total': {'label': 'Total', 'amount': {'currency': 'USD', 'value': 603.49}, 'pending': None, 'refund_period': 30}, 'payment_response': {'request_id': 'order_3', 'method_name': 'CARD', 'details': {'token': {'value': 'fake_payment_credential_token_0', 'url': 'http://CredentialsProvider/a2a/credentials_provider'}}, 'shipping_address': {'city': 'Sample City', 'country': 'US', 'dependent_locality': None, 'organization': 'Sample Organization', 'phone_number': '+1-000-000-0000', 'postal_code': '00000', 'recipient': 'Bugs Bunny', 'region': 'ST', 'sorting_code': None, 'address_line': ['123 Main St']}, 'shipping_option': None, 'payer_name': None, 'payer_email': 'bugsbunny@gmail.com', 'payer_phone': None}, 'merchant_agent': 'Generic Merchant', 'timestamp': '2025-11-11T03:50:04.532972+00:00'}, 'user_authorization': 'fake_cart_mandate_hash_cart_3_fake_payment_mandate_hash_848f97b287584cd1aa3085bed1985c22'}
  [Data Part: risk_data] eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...fake_risk_data
  [Data Part: challenge_response] 123
```

### `MerchantPaymentAgent -> PaymentCredentialProviderAgent`: Give me the payment method credentials for the given token

```shell
POST http://CredentialsProvider/a2a/credentials_provider
[Request Body] {'id': '6724cb50-56f1-42e0-9864-64d253828cac', 'jsonrpc': '2.0', 'method': 'message/send', 'params': {'configuration': {'acceptedOutputModes': [], 'blocking': True}, 'message': {'contextId': '6030ebc7-fde8-4489-b655-045443c47af0', 'kind': 'message', 'messageId': '92b1783ecc8f4cc0ac6dc1f853c38297', 'parts': [{'kind': 'text', 'text': 'Give me the payment method credentials for the given token.'}, {'data': {'ap2.mandates.PaymentMandate': {'payment_mandate_contents': {'payment_mandate_id': '848f97b287584cd1aa3085bed1985c22', 'payment_details_id': 'order_3', 'payment_details_total': {'label': 'Total', 'amount': {'currency': 'USD', 'value': 603.49}, 'pending': None, 'refund_period': 30}, 'payment_response': {'request_id': 'order_3', 'method_name': 'CARD', 'details': {'token': {'value': 'fake_payment_credential_token_0', 'url': 'http://CredentialsProvider/a2a/credentials_provider'}}, 'shipping_address': {'city': 'Sample City', 'country': 'US', 'dependent_locality': None, 'organization': 'Sample Organization', 'phone_number': '+1-000-000-0000', 'postal_code': '00000', 'recipient': 'Bugs Bunny', 'region': 'ST', 'sorting_code': None, 'address_line': ['123 Main St']}, 'shipping_option': None, 'payer_name': None, 'payer_email': 'bugsbunny@gmail.com', 'payer_phone': None}, 'merchant_agent': 'Generic Merchant', 'timestamp': '2025-11-11T03:50:04.532972+00:00'}, 'user_authorization': 'fake_cart_mandate_hash_cart_3_fake_payment_mandate_hash_848f97b287584cd1aa3085bed1985c22'}}, 'kind': 'data'}], 'role': 'agent'}}}
[Extension Header] X-A2A-Extensions: https://github.com/google-agentic-commerce/ap2/v1
[Request Instructions] ['Give me the payment method credentials for the given token.']
[A Payment Mandate was in the request Data] {'payment_mandate_contents': {'payment_mandate_id': '848f97b287584cd1aa3085bed1985c22', 'payment_details_id': 'order_3', 'payment_details_total': {'label': 'Total', 'amount': {'currency': 'USD', 'value': 603.49}, 'pending': None, 'refund_period': 30}, 'payment_response': {'request_id': 'order_3', 'method_name': 'CARD', 'details': {'token': {'value': 'fake_payment_credential_token_0', 'url': 'http://CredentialsProvider/a2a/credentials_provider'}}, 'shipping_address': {'city': 'Sample City', 'country': 'US', 'dependent_locality': None, 'organization': 'Sample Organization', 'phone_number': '+1-000-000-0000', 'postal_code': '00000', 'recipient': 'Bugs Bunny', 'region': 'ST', 'sorting_code': None, 'address_line': ['123 Main St']}, 'shipping_option': None, 'payer_name': None, 'payer_email': 'bugsbunny@gmail.com', 'payer_phone': None}, 'merchant_agent': 'Generic Merchant', 'timestamp': '2025-11-11T03:50:04.532972+00:00'}, 'user_authorization': 'fake_cart_mandate_hash_cart_3_fake_payment_mandate_hash_848f97b287584cd1aa3085bed1985c22'}
[Response Body] {"id":"6724cb50-56f1-42e0-9864-64d253828cac","jsonrpc":"2.0","result":{"artifacts":[{"artifactId":"253b8275-f7a1-492b-81c0-b49627e9be9b","parts":[{"data":{"type":"CARD","alias":"American Express ending in 4444","network":[{"name":"amex","formats":["DPAN"]}],"cryptogram":"fake_cryptogram_abc123","token":"1111000000000000","card_holder_name":"John Doe","card_expiration":"12/2025","card_billing_address":{"country":"US","postal_code":"00000"}},"kind":"data"}]}],"contextId":"6030ebc7-fde8-4489-b655-045443c47af0","history":[{"contextId":"6030ebc7-fde8-4489-b655-045443c47af0","kind":"message","messageId":"92b1783ecc8f4cc0ac6dc1f853c38297","parts":[{"kind":"text","text":"Give me the payment method credentials for the given token."},{"data":{"ap2.mandates.PaymentMandate":{"payment_mandate_contents":{"payment_mandate_id":"848f97b287584cd1aa3085bed1985c22","payment_details_id":"order_3","payment_details_total":{"label":"Total","amount":{"currency":"USD","value":603.49},"pending":null,"refund_period":30},"payment_response":{"request_id":"order_3","method_name":"CARD","details":{"token":{"value":"fake_payment_credential_token_0","url":"http://CredentialsProvider/a2a/credentials_provider"}},"shipping_address":{"city":"Sample City","country":"US","dependent_locality":null,"organization":"Sample Organization","phone_number":"+1-000-000-0000","postal_code":"00000","recipient":"Bugs Bunny","region":"ST","sorting_code":null,"address_line":["123 Main St"]},"shipping_option":null,"payer_name":null,"payer_email":"bugsbunny@gmail.com","payer_phone":null},"merchant_agent":"Generic Merchant","timestamp":"2025-11-11T03:50:04.532972+00:00"},"user_authorization":"fake_cart_mandate_hash_cart_3_fake_payment_mandate_hash_848f97b287584cd1aa3085bed1985c22"}},"kind":"data"}],"role":"agent","taskId":"65d8cfea-407e-434f-91b5-9852db1b4fbd"}],"id":"65d8cfea-407e-434f-91b5-9852db1b4fbd","kind":"task","status":{"state":"completed","timestamp":"2025-11-11T03:51:48.590478+00:00"}}}
[Response Body] {"id":"d9bc9bc6-73bb-4667-949b-85a60633089b","jsonrpc":"2.0","result":{"contextId":"6030ebc7-fde8-4489-b655-045443c47af0","history":[{"contextId":"6030ebc7-fde8-4489-b655-045443c47af0","kind":"message","messageId":"85dc6b61ae8e4e23bc8d14fc02ca14eb","parts":[{"kind":"text","text":"initiate_payment"},{"data":{"ap2.mandates.PaymentMandate":{"payment_mandate_contents":{"payment_mandate_id":"848f97b287584cd1aa3085bed1985c22","payment_details_id":"order_3","payment_details_total":{"label":"Total","amount":{"currency":"USD","value":603.49},"pending":null,"refund_period":30},"payment_response":{"request_id":"order_3","method_name":"CARD","details":{"token":{"value":"fake_payment_credential_token_0","url":"http://CredentialsProvider/a2a/credentials_provider"}},"shipping_address":{"city":"Sample City","country":"US","dependent_locality":null,"organization":"Sample Organization","phone_number":"+1-000-000-0000","postal_code":"00000","recipient":"Bugs Bunny","region":"ST","sorting_code":null,"address_line":["123 Main St"]},"shipping_option":null,"payer_name":null,"payer_email":"bugsbunny@gmail.com","payer_phone":null},"merchant_agent":"Generic Merchant","timestamp":"2025-11-11T03:50:04.532972+00:00"},"user_authorization":"fake_cart_mandate_hash_cart_3_fake_payment_mandate_hash_848f97b287584cd1aa3085bed1985c22"}},"kind":"data"},{"data":{"risk_data":"eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...fake_risk_data"},"kind":"data"}],"role":"agent","taskId":"799fbe91-a538-497f-904c-d81eda1dedbf"},{"contextId":"6030ebc7-fde8-4489-b655-045443c47af0","kind":"message","messageId":"965f5ae8-0cb6-4687-9a3c-64267fe165da","parts":[{"kind":"text","text":"Please provide the challenge response to complete the payment."},{"data":{"challenge":{"type":"otp","display_text":"The payment method issuer sent a verification code to the phone number on file, please enter it below. It will be shared with the issuer so they can authorize the transaction.(Demo only hint: the code is 123)"}},"kind":"data"}],"role":"agent","taskId":"799fbe91-a538-497f-904c-d81eda1dedbf"},{"contextId":"6030ebc7-fde8-4489-b655-045443c47af0","kind":"message","messageId":"f33f4dc30d3a41878c8d1d7006b2cf0e","parts":[{"kind":"text","text":"initiate_payment"},{"data":{"ap2.mandates.PaymentMandate":{"payment_mandate_contents":{"payment_mandate_id":"848f97b287584cd1aa3085bed1985c22","payment_details_id":"order_3","payment_details_total":{"label":"Total","amount":{"currency":"USD","value":603.49},"pending":null,"refund_period":30},"payment_response":{"request_id":"order_3","method_name":"CARD","details":{"token":{"value":"fake_payment_credential_token_0","url":"http://CredentialsProvider/a2a/credentials_provider"}},"shipping_address":{"city":"Sample City","country":"US","dependent_locality":null,"organization":"Sample Organization","phone_number":"+1-000-000-0000","postal_code":"00000","recipient":"Bugs Bunny","region":"ST","sorting_code":null,"address_line":["123 Main St"]},"shipping_option":null,"payer_name":null,"payer_email":"bugsbunny@gmail.com","payer_phone":null},"merchant_agent":"Generic Merchant","timestamp":"2025-11-11T03:50:04.532972+00:00"},"user_authorization":"fake_cart_mandate_hash_cart_3_fake_payment_mandate_hash_848f97b287584cd1aa3085bed1985c22"}},"kind":"data"},{"data":{"risk_data":"eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...fake_risk_data"},"kind":"data"},{"data":{"challenge_response":"123"},"kind":"data"}],"role":"agent","taskId":"799fbe91-a538-497f-904c-d81eda1dedbf"}],"id":"799fbe91-a538-497f-904c-d81eda1dedbf","kind":"task","status":{"message":{"contextId":"6030ebc7-fde8-4489-b655-045443c47af0","kind":"message","messageId":"627a4fc6-e3b0-488d-ae9a-3332b612f778","parts":[{"kind":"text","text":"{'status': 'success'}"}],"role":"agent","taskId":"799fbe91-a538-497f-904c-d81eda1dedbf"},"state":"completed","timestamp":"2025-11-11T03:51:48.595556+00:00"}}}
[Response Body] {"id":"716d25d2-2541-41b7-bd8a-2f94465a91d1","jsonrpc":"2.0","result":{"contextId":"6030ebc7-fde8-4489-b655-045443c47af0","history":[{"contextId":"6030ebc7-fde8-4489-b655-045443c47af0","kind":"message","messageId":"14ed8b94ec5a4cc0a516a7b8d62cc6f8","parts":[{"kind":"text","text":"Initiate a payment"},{"data":{"ap2.mandates.PaymentMandate":{"payment_mandate_contents":{"payment_mandate_id":"848f97b287584cd1aa3085bed1985c22","payment_details_id":"order_3","payment_details_total":{"label":"Total","amount":{"currency":"USD","value":603.49},"refund_period":30},"payment_response":{"request_id":"order_3","method_name":"CARD","details":{"token":{"value":"fake_payment_credential_token_0","url":"http://CredentialsProvider/a2a/credentials_provider"}},"shipping_address":{"city":"Sample City","country":"US","organization":"Sample Organization","phone_number":"+1-000-000-0000","postal_code":"00000","recipient":"Bugs Bunny","region":"ST","address_line":["123 Main St"]},"payer_email":"bugsbunny@gmail.com"},"merchant_agent":"Generic Merchant","timestamp":"2025-11-11T03:50:04.532972+00:00"},"user_authorization":"fake_cart_mandate_hash_cart_3_fake_payment_mandate_hash_848f97b287584cd1aa3085bed1985c22"}},"kind":"data"},{"data":{"risk_data":"eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...fake_risk_data"},"kind":"data"},{"data":{"shopping_agent_id":"trusted_shopping_agent"},"kind":"data"}],"role":"agent","taskId":"57a672e8-478b-4c7a-8885-00388224e886"},{"contextId":"6030ebc7-fde8-4489-b655-045443c47af0","kind":"message","messageId":"965f5ae8-0cb6-4687-9a3c-64267fe165da","parts":[{"kind":"text","text":"Please provide the challenge response to complete the payment."},{"data":{"challenge":{"type":"otp","display_text":"The payment method issuer sent a verification code to the phone number on file, please enter it below. It will be shared with the issuer so they can authorize the transaction.(Demo only hint: the code is 123)"}},"kind":"data"}],"role":"agent","taskId":"799fbe91-a538-497f-904c-d81eda1dedbf"},{"contextId":"6030ebc7-fde8-4489-b655-045443c47af0","kind":"message","messageId":"4f466784348444b58d547a64f42d31ca","parts":[{"kind":"text","text":"Initiate a payment. Include the challenge response."},{"data":{"ap2.mandates.PaymentMandate":{"payment_mandate_contents":{"payment_mandate_id":"848f97b287584cd1aa3085bed1985c22","payment_details_id":"order_3","payment_details_total":{"label":"Total","amount":{"currency":"USD","value":603.49},"refund_period":30},"payment_response":{"request_id":"order_3","method_name":"CARD","details":{"token":{"value":"fake_payment_credential_token_0","url":"http://CredentialsProvider/a2a/credentials_provider"}},"shipping_address":{"city":"Sample City","country":"US","organization":"Sample Organization","phone_number":"+1-000-000-0000","postal_code":"00000","recipient":"Bugs Bunny","region":"ST","address_line":["123 Main St"]},"payer_email":"bugsbunny@gmail.com"},"merchant_agent":"Generic Merchant","timestamp":"2025-11-11T03:50:04.532972+00:00"},"user_authorization":"fake_cart_mandate_hash_cart_3_fake_payment_mandate_hash_848f97b287584cd1aa3085bed1985c22"}},"kind":"data"},{"data":{"shopping_agent_id":"trusted_shopping_agent"},"kind":"data"},{"data":{"challenge_response":"123"},"kind":"data"},{"data":{"risk_data":"eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...fake_risk_data"},"kind":"data"}],"role":"agent","taskId":"57a672e8-478b-4c7a-8885-00388224e886"}],"id":"57a672e8-478b-4c7a-8885-00388224e886","kind":"task","status":{"message":{"contextId":"6030ebc7-fde8-4489-b655-045443c47af0","kind":"message","messageId":"627a4fc6-e3b0-488d-ae9a-3332b612f778","parts":[{"kind":"text","text":"{'status': 'success'}"}],"role":"agent","taskId":"799fbe91-a538-497f-904c-d81eda1dedbf"},"state":"completed","timestamp":"2025-11-11T03:51:48.599045+00:00"}}}

```

## 3.6 Summary: Interactions Between Agents

<p align="center"><img src="/assets/img/ap2-illustrated-guide/demo-call-flow.png" width="100%" height="100%"></p>
<p align="center">Fig. 
Call flow of the AP2 demo. Note: for clarity, the "Shopping Agent" shown
in this diagram combines the responsibilities of three distinct agents from the
actual demo: the shopping agent, address collection agent, and payment method
collection agent.
</p>


# References


1. https://a2aprotocol.ai/ap2-protocol
2. https://ap2-protocol.net/en/
3. https://github.com/google-agentic-commerce/AP2/blob/main/samples/python/scenarios/a2a/human-present/cards/README.md

----

<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-white.svg" alt="Written by Human, Not by AI"></a>
<a href="https://notbyai.fyi"><img src="/assets/img/Written-By-Human-Not-By-AI-Badge-black.svg" alt="Written by Human, Not by AI"></a>
