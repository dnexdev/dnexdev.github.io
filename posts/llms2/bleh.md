# tiramisu, part 02: storage and tensor

*This is part of an ongoing series where I build a machine learning framework from scratch in C++. [Part 01](/llms1/) covers the overall plan. The code is at [github.com/dnexdev/tiramisu](https://github.com/dnexdev/tiramisu).*

---

Before any neural network, before any gradient, before any op — there's a block of memory and something that knows how to interpret it. That's what this part is about. It's the least glamorous part of the whole project but the decisions made here ripple up through every layer above it.

Everything in tiramisu eventually boils down to two classes: `Storage` and `Tensor`.

## Storage: just a buffer

`Storage` is the simplest possible thing — it owns a flat, aligned block of bytes. One constructor, one destructor, no copying.

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
  std::byte* data_ = nullptr;
  std::size_t count_ = 0;
  DType dtype_;
  Device device_;
  std::size_t alignment_;
};
```

The deleted copy constructor and copy assignment aren't an oversight — they're the point. `Storage` is supposed to be the one true owner of the bytes. Nothing else should be copying the whole buffer around. That ownership flows through `shared_ptr<Storage>` from the `Tensor` side, which we'll get to shortly.

### Alignment

The default alignment is 64 bytes. There are two reasons for this:

1. **SIMD.** AVX2 operates on 256-bit (32-byte) registers. For the vectorized matmul we'll build in Part 05, we want the data pointer to be aligned so we can use `_mm256_load_ps` (the aligned variant) instead of `_mm256_loadu_ps` (unaligned). 64 bytes covers this and leaves room for AVX-512.

2. **Cache lines.** A typical x86 cache line is 64 bytes. Aligning allocations to cache line boundaries prevents false sharing when multiple threads touch different elements of the same buffer.

The allocation rounds up to the nearest multiple of the alignment:

```cpp
std::size_t bytes = nbytes();
std::size_t alloc_size = (bytes + alignment_ - 1) & ~(alignment_ - 1);
data_ = static_cast<std::byte*>(std::aligned_alloc(alignment_, alloc_size));
```

The `& ~(alignment_ - 1)` trick works because alignment is a power of two, so `alignment_ - 1` is a bitmask of all the low bits. ANDing with its complement clears them, rounding down to the nearest multiple. Adding `alignment_ - 1` first ensures we round up, not down.

There's a subtle requirement here that took me a second read of the man page to catch: `std::aligned_alloc` requires `alloc_size` to be an exact multiple of `alignment`. The round-up ensures this.

One more thing: the constructor asserts that alignment is a power of two:

```cpp
assert((alignment & (alignment - 1)) == 0);
```

This is one of those things that's obvious once you see the bit trick: every power of two has exactly one bit set, so subtracting 1 gives a mask of all the bits below it, and ANDing them gives zero. Any non-power-of-two would leave some bits set. Without this check, passing `alignment=100` would produce technically undefined behavior from `aligned_alloc` and a near-impossible-to-debug memory corruption.

### dtype and device tags

```cpp
enum class DType  { Float32, Int8 };
enum class Device { CPU, CUDA };
```

These live on `Storage`, not on `Tensor`. `Tensor` is a view — it reads the dtype and device from the `Storage` it points at. This means you can't accidentally create a view that claims to be float32 over int8 storage; the type information travels with the bytes.

Both enums start with just the values we need right now. `Int8` gets used in Part 08 when we quantize the Shakespeare checkpoint for WASM. `CUDA` gets used in Part 08 when we add GPU kernels. The seam exists from day one even though only one value on each side is real for a while — adding a new backend later means adding a kernel implementation, not redesigning the memory abstraction.

---

## Tensor: a view over Storage

Here's the key insight, and it's worth sitting with for a moment: **a `Tensor` doesn't own any data.** It's a description of how to interpret data that lives in a `Storage` somewhere.

```cpp
class Tensor {
 private:
  std::shared_ptr<Storage> storage_;
  std::vector<int64_t> shape_;
  std::vector<int64_t> strides_;
  std::size_t offset_ = 0;
  // ... autograd stuff, covered in Part 04
};
```

Four fields. That's the whole geometric description of a tensor:

- `storage_` — where the bytes actually live
- `shape_` — the logical dimensions, e.g. `{2, 3, 4}` for a 3D tensor
- `strides_` — how many elements to skip to advance one step in each dimension
- `offset_` — where in the storage this tensor's data begins

Multiple `Tensor` objects can point at the same `Storage` with different shape/strides/offset. That's what makes operations like `transpose`, `permute`, and `slice` free — they just return a new `Tensor` struct with different metadata, without touching a single byte of the underlying data.

### Strides: the actual math

For a row-major (C-contiguous) tensor with shape `(d₀, d₁, ..., dₙ₋₁)`, the canonical strides are:

```
stride[n-1] = 1
stride[i]   = stride[i+1] × shape[i+1]     for i = n-2 down to 0
```

Concretely:
- Shape `(4,)` → strides `(1,)`
- Shape `(3, 4)` → strides `(4, 1)`
- Shape `(2, 3, 4)` → strides `(12, 4, 1)`

And to find the flat index of element `[i₀, i₁, ..., iₙ₋₁]`:

```
flat_index = offset + i₀×stride[0] + i₁×stride[1] + ... + iₙ₋₁×stride[n-1]
```

This is a dot product of the index vector and the stride vector, plus the offset. That's all indexing ever is.

Here's the implementation:

```cpp
std::vector<int64_t> contiguous_strides(const std::vector<int64_t>& shape) {
  std::vector<int64_t> strides(shape.size());
  if (shape.empty()) return strides;

  int64_t current_stride = 1;
  for (int i = static_cast<int>(shape.size()) - 1; i >= 0; i--) {
    strides[i] = current_stride;
    current_stride *= shape[i];
  }
  return strides;
}
```

Walk backwards from the last dimension: stride is 1 there (adjacent elements are adjacent in memory), then multiply by each dimension size as you move left.

### Why transpose is free

Here's where strides become interesting. Consider a `(3, 4)` matrix — 12 elements, strides `(4, 1)`:

```
Layout in memory:
[ 0  1  2  3 | 4  5  6  7 | 8  9  10 11 ]
  row 0        row 1        row 2
```

To transpose it to `(4, 3)`, we just swap the shape and stride entries:
- Shape: `(3, 4)` → `(4, 3)`
- Strides: `(4, 1)` → `(1, 4)`

The bytes in memory haven't moved. But now when we ask for element `[row, col]`, the index formula computes `row×1 + col×4` — which walks down columns of the original layout. Same bytes, different walk order.

```cpp
Tensor Tensor::transpose(int64_t dim0, int64_t dim1) const {
  std::vector<int64_t> dims(rank);
  std::iota(dims.begin(), dims.end(), 0);
  std::swap(dims[dim0], dims[dim1]);
  return permute(dims);
}
```

`permute` is the general case — reorder any subset of dimensions by reordering both the shape and stride arrays in the same way:

```cpp
Tensor Tensor::permute(std::vector<int64_t> dims) const {
  // ... validate dims is a genuine permutation (no duplicates, all in range)
  std::vector<int64_t> new_shape(shape_.size());
  std::vector<int64_t> new_strides(strides_.size());
  for (size_t i = 0; i < dims.size(); i++) {
    new_shape[i]   = shape_[dims[i]];
    new_strides[i] = strides_[dims[i]];
  }
  return Tensor(storage_, new_shape, new_strides, offset_);
}
```

One line that's easy to miss: the last line constructs a new `Tensor` using the view constructor (the one that takes an existing `storage_`). The `shared_ptr` reference count on the storage goes up by one. When the transposed tensor eventually goes out of scope, the count goes back down. No copies, no allocations.

### Contiguous vs non-contiguous

After a transpose, the tensor is no longer contiguous — the strides don't match what `contiguous_strides` would compute for its shape. You can check this:

```cpp
bool Tensor::is_contiguous() const {
  return strides_ == contiguous_strides(shape_);
}
```

This matters because some operations (like the inner loop of matmul) assume elements are laid out end-to-end in memory. When a tensor isn't contiguous, `.contiguous()` copies it into a fresh, compactly-laid-out storage:

```cpp
Tensor Tensor::contiguous() const {
  if (is_contiguous()) return *this;   // already fine, no copy
  Tensor result(shape_, dtype(), device());
  // ... element-by-element copy respecting strides
  return result;
}
```

The copy loop has to use the stride formula — it can't just `memcpy`, because the elements of a transposed tensor aren't adjacent in memory anymore.

### Slice

`slice` is the other key view operation — taking a subset of rows along a dimension. For a `(60000, 784)` MNIST dataset, slicing rows `[0, 64)` gives a `(64, 784)` batch:

```cpp
Tensor Tensor::slice(int64_t dim, int64_t start, int64_t end) const {
  std::vector<int64_t> new_shape = shape_;
  new_shape[dim] = end - start;
  std::size_t new_offset = offset_ + start * strides_[dim];
  return Tensor(storage_, new_shape, strides_, new_offset);
}
```

Same storage, same strides, new offset and new shape along the sliced dimension. The whole 60,000-image dataset stays allocated; each batch is just a different view into it.

---

## The bugs

Two real issues that came up and are worth knowing about, since they show up in slightly different forms in almost every tensor library.

**numel() and scalar tensors.** An empty shape `{}` represents a rank-0 scalar tensor — the result of something like `sum()` that reduces everything to a single number. The mathematical convention (and what PyTorch/NumPy do) is that a rank-0 tensor has exactly one element — the product of an empty set of dimensions is 1 (the multiplicative identity). An early version of `numel()` special-cased the empty shape to return 0 instead of 1, which meant `view({})` and `numel()` disagreed about how many elements a scalar has. The fix was to remove the special case:

```cpp
int64_t Tensor::numel() const {
  int64_t count = 1;
  for (int64_t dim : shape_) count *= dim;
  return count;
}
```

Empty product is 1. Done.

**permute() accepting duplicate dimensions.** The early `permute()` checked that each dimension was in `[0, rank)`, but not that the dims were distinct. `tensor.permute({0, 0})` on a 2D tensor would silently return a tensor where both dimensions referenced the same stride — two dims aliasing the same axis, which is not a valid tensor. The fix uses a `seen` boolean array to detect duplicates in a single pass:

```cpp
std::vector<bool> seen(rank, false);
for (size_t i = 0; i < dims.size(); i++) {
  int64_t d = normalize_dim(dims[i], rank);
  if (seen[d]) throw std::runtime_error("Duplicate dimension in permute.");
  seen[d] = true;
}
```

---

## What's next

The `Storage`/`Tensor` layer is complete — strided views, free transpose, O(1) slice, contiguous copy when needed, ASan-clean under test. Every other layer in tiramisu sits on top of this without modifying it.

Next up: ops. We need to be able to actually compute something with these tensors — add, multiply, matrix multiply. And before we trust any of that, we need a way to verify the math is right. That's where gradient checking comes in, and it's the most important engineering discipline in the whole project.

[Part 03 — Ops: Broadcasting, naive matmul, gradient checker →](/llms3/)