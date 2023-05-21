import torch
import torch.nn as nn
from torch.nn import functional as F
from graphviz import Digraph

import babygpt # Our own BabyGPT in babygpt.py

def possible_states(n, k): # return all possible lists of k elements, each in range of [0,n)
    if k == 0:
        yield []
        return

    for i in range(n):
        for c in possible_states(n, k - 1):
            yield [i] + c

def plot_model():
    dot = Digraph(comment='Baby GPT', engine='circo')

    print("\nDump BabyGPT state ...")
    for xi in possible_states(gpt.config.vocab_size, gpt.config.block_size):
        # forward the GPT and get probabilities for next token
        x = torch.tensor(xi, dtype=torch.long)[None, ...] # turn the list into a torch tensor and add a batch dimension
        logits = gpt(x) # forward the gpt neural net
        probs = nn.functional.softmax(logits, dim=-1) # get the probabilities
        y = probs[0].tolist() # remove the batch dimension and unpack the tensor into simple list
        print(f"input {xi} ---> {y}")

        # also build up the transition graph for plotting later
        current_node_signature = "".join(str(d) for d in xi)
        dot.node(current_node_signature)
        for t in range(gpt.config.vocab_size):
            next_node = xi[1:] + [t] # crop the context and append the next character
            next_node_signature = "".join(str(d) for d in next_node)
            p = y[t]
            label=f"{t}({p*100:.0f}%)"
            dot.edge(current_node_signature, next_node_signature, label=label)
    return dot

def token_seq_to_tensor(seq):
    # iterate over the sequence and grab every consecutive 3 bits
    # the correct label for what's next is the next bit at each position
    X, Y = [], []
    for i in range(len(seq) - context_length):
        X.append(seq[i:i+context_length])
        Y.append(seq[i+context_length])
        print(f"example {i+1:2d}: {X[-1]} --> {Y[-1]}")
    X = torch.tensor(X, dtype=torch.long)
    Y = torch.tensor(Y, dtype=torch.long)
    print(X.shape, Y.shape)
    return X, Y

def do_training(X, Y):
    # init a GPT and the optimizer
    torch.manual_seed(1337)
    optimizer = torch.optim.AdamW(gpt.parameters(), lr=1e-3, weight_decay=1e-1)

    # train the GPT for some number of iterations
    print("\nTraning BabyGPT ...")
    for i in range(50):
        logits = gpt(X)
        loss = F.cross_entropy(logits, Y)
        loss.backward()
        optimizer.step()
        optimizer.zero_grad()
        print(i, loss.item())

# Init GPT
vocab_size = 2
context_length = 3
config = babygpt.GPTConfig(
    block_size = context_length,
    vocab_size = vocab_size,
    n_layer = 4,
    n_head = 4,
    n_embd = 16,
    bias = False,
)

print("Creating new BabyGPT ...")
gpt = babygpt.GPT(config)

# Init
dot = plot_model()
dot.render('states-1', format='png')

# Training
seq = list(map(int, "111101111011110"))
print("\nTraining data sequence: ", seq)
X, Y = token_seq_to_tensor(seq)
do_training(X, Y)

print("\nTraining data sequence, as a reminder: ", seq)
dot = plot_model()
dot.render('states-2', format='png')
