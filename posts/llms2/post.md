---
title: "let's make an LLM part 02 'storage and tensor'"
date: 2026-06-30
tags: ["c++"]
---

# let's make an LLM part 02: storage and tensor

*This is part of an ongoing series where I build a machine learning framework from scratch in C++. [Part 01]/(llms1) covers the overall plan. The code is at [github.com/dnexdev/tiramisu](https://github.com/dnexdev/tiramisu).*

---

Before we can get started with neural networks and gradients, we first need to figure out what to do with a block of memory.

We need two classes: `Storage` and `Tensor`.

## Storage

`Storage` is just a buffer. It owns a flat, aligned block of bytes. 

```cpp
class Storage {
  public:
    Storage(std::size_t count, DType dtype, Device device,
            std::size_t alignment = 64);
    ~Storage();

    Storage(const Storage&) = delete;
    Storage& operator=(const Storage&) = delete;

    std::byte* data();
    const std::byte* data() const;
    std::size_t numel() const;
    std::size_t nbytes() const;
  
  private:
    std::byte* data = nullptr;
    std::size_t count_ = 0;
    DType dtype_;
    Device device_;
    std::size_t alignment_;
};
```

Notice how there is no copying. This is intentional. `Storage` should be the one true owner of the bytes. Nothing else should copy the whole buffer around. The ownership flows through `shared_ptr<Storage>` from the `Tensor` side.

### dtype and device tags

```cpp
enum class DType { Float32, Int8 };
enum class Device { CPU, CUDA };
```

These live on `Storage`. `Tensor` just reads the dtype and device from `Storage` it points at. This way, you can't accidentally create a view that claims to be float32 over int8 storage.

---

## Tensor: a view over Storage

`Tensor` doesn't own any data. It's just a description of how to interpret data that lives in a `Storage` somewhere.

```cpp
class Tensor {
  private:
    std::shared_ptr<Storage> storage_;
    std::vector<int64_t> shape_;
    std::vector<int64_t> strides_;
};
```