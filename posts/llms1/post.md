---
title: "let's make an LLM part 1 'overview'"
date: 2026-06-19
tags: ["c++"]
---

Sorry, the title was clickbait.

We won't be anywhere NEAR making an LLM for a while. 
We first need to construct a machine learning framework.

The framework will be written in C++ with no dependencies on PyTorch, TensorFlow, or any other ML library.
The only dependencies I will be using are GoogleTest for testing, pybind11 for Python bindings, and the CUDA toolkit for the GPU backend.

All you need is just basic programming proficiency in C++ to follow along.

## Preview
- **Part 2 - Storage and Tensor**: Strides, transpose
- **Part 3 - Ops**: Broadcasting rules, naive matmul (matrix multiplication), gradient checker
- **Part 4 - Autograd**: 
- **Part 5 - Optimizing matmul**:
- **Part 6 - Neural network modules and MNIST**: Linear, Adam, cross-entropy
- **Part 7 - Transformer**: Attention, layernorm, GPT
- **Part 8 - CUDA**: Kernels
- **Part 9 - Cleaning up**: 
- **Part 10 - Training Shakespeare**: 
- **Part 11 - Tokenizer and data pipeline**:
- **Part 12 - More modern architecture choices and mixed precision**:
- **Part 13 - Flash Attention**:
- **Part 14 - KV cache, sampling, and eval harness**:
- **Part 15 - Training recipe and a real run**:
- **Part 16 - Fused kernels**:
- **Part 17 - Flash Attention**:
- **Part 18 - Tensor core matmul**:
- **Part 19 - Ring all-reduce and DDP**:
- **Part 20 - Activation checkpointing**:
- ???


The actual code is at [github.com/dnexdev/tiramisu](https://github.com/denexdev/tiramisu). 