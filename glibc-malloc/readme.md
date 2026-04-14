A complete description of glibc-malloc
---

- [Introduction To Userspace Memory Allocators](#introduction-to-userspace-memory-allocators)
- [The Problems](#the-problems)
- [Scope and Reproducibility](#scope-and-reproducibility)
- [Groundwork (Incomplete)](#groundwork-incomplete)
- [Chunk Description](#chunk-description)
  - [Layout Description](#layout-description)
  - [Usage Description](#usage-description)
  - [The Problem: Fragmentation](#the-problem-fragmentation)
  - [Coalescing](#coalescing)
    - [The second use of 'mchunk\_size'](#the-second-use-of-mchunk_size)
    - [The Boundary Tag Method](#the-boundary-tag-method)
  - [The Size Model](#the-size-model)
    - [The use of size\_t](#the-use-of-size_t)
    - [Macro #1 -\> SIZE\_SZ](#macro-1---size_sz)
    - [Macro #2 -\> CHUNK\_HDR\_SZ](#macro-2---chunk_hdr_sz)
    - [Macro #3 -\> MIN\_CHUNK\_SIZE](#macro-3---min_chunk_size)
    - [Macro #4 -\> MALLOC\_ALIGNMENT](#macro-4---malloc_alignment)
    - [Macro #5 -\> MALLOC\_ALIGN\_MASK](#macro-5---malloc_align_mask)
    - [Macro #6 -\> MINSIZE](#macro-6---minsize)
    - [Macro #7 -\> request2size](#macro-7---request2size)
    - [Macro #8 -\> chunk2mem](#macro-8---chunk2mem)
- [Binning, The Bookkeeping Process (Actively writing)](#binning-the-bookkeeping-process-actively-writing)
  - [Implementation of `bins`](#implementation-of-bins)
    - [The problem](#the-problem)
    - [Finding the solution](#finding-the-solution)
    - [Implementing the solution](#implementing-the-solution)
---

# Introduction To Userspace Memory Allocators

***A dynamic memory allocator is a userspace program that requests virtual memory from the kernel and allocates it to a running process.***

Each process gets its own instance of the allocator. When the process actually uses that piece of dynamic memory, the kernel backs it with physical memory (paging).

There are multiple "allocation strategies", each with specific advantages and tradeoffs. This gives rise to multiple allocators. For example:
  - **dlmalloc**, written by Professor Doug Lea is one of the earliest dynamic memory allocators. It's first version was released in 1987 and the last version in 2012.
  - **ptmalloc**, or pthreads malloc, was written by Wolfram Gloger in 1996 as an extension of dlmalloc to provide multithreading support. Improved versions were released around 2001 (ptmalloc2) and 2006 (ptmalloc3).
  - **glibc-malloc** integrated ptmalloc2 in v2.3 and a substantial amount of changes have been made since then. It is the default dynamic memory allocator on GNU Linux.
  - **jemalloc**, written by Jason Evans, was first used in FreeBSD. Later on, Firefox and Facebook adopted it.
  - **tcmalloc**, or thread-caching malloc, was written in Google and later open-sourced in 2005 as part of the "Google Performance Tools" (later changed to gperftools).

---

As it is a userspace program, a process can swap the default allocator provided by the OS. For example: FireFox uses jemalloc.

Therefore, an operating system can have multiple allocators installed in it. I use `Debian GNU/Linux 13 (trixie)`. I can check the presence of some popular userspace allocators:
```bash
$ dpkg -l | grep -E "libjemalloc|libtcmalloc|libmimalloc|libdlmalloc"

ii  libjemalloc2:amd64                      5.3.0-3                              amd64        general-purpose scalable concurrent malloc(3) implementation
```

This proves that I have jemalloc installed on my system and a pkg depends on it. To find that pkg, we can use this command:
```bash
$ apt-cache rdepends libjemalloc2 | grep -E "^\s" | xargs dpkg -l 2>/dev/null | grep "^ii"

ii  bind9-dnsutils   1:9.20.15-1~deb13u1 amd64        Clients provided with BIND 9
ii  bind9-libs:amd64 1:9.20.15-1~deb13u1 amd64        Shared Libraries used by BIND 9
```

Firefox is not in this list because it comes with jemalloc built right in the code. We can confirm the presence of jemalloc related symbols to prove our point.
```bash
strings /proc/1699/exe | grep -i jemalloc | head -20

jemalloc_stats_internal
jemalloc_stats_num_bins
jemalloc_stats_lite
jemalloc_set_main_thread
..
```
There it is.

There is no occurrence of "glibc-malloc" because, it is shipped with `libc.so.6`. We can prove this by writing a simple C program that calls malloc and check the presence of malloc related symbols.
```bash
$ ldd main
      linux-vdso.so.1 (0x00007fc2a467e000)
      libc.so.6 => /lib/x86_64-linux-gnu/libc.so.6 (0x00007fc2a4467000)
      /lib64/ld-linux-x86-64.so.2 (0x00007fc2a4680000)

$ strings main | grep -i malloc | head -20  
malloc
malloc@GLIBC_2.2.5
```

---

In simple words, any project that knows its memory requirements and is certain that a general-purpose allocator might not be able to fulfill it, is almost certain to have it's own userspace dynamic memory allocator.

This writeup aims to be a complete description of glibc-malloc, which is the default general-purpose, userspace dynamic memory allocator on GNU Linux.

---

Before we dive into the details, I want to acknowledge the problems I have incurred while writing this document and the way I have dealt with them.

# The Problems

**Problem0: The original source code is always receiving changes by contributors and maintainers. As a result, it is not formatted for readability.**
  - To solve this, I have formatted the source code and all the code snippets quote the formatted source. The logic remains untouched.**
  - The original source: [sourceware.org](https://sourceware.org/git/?p=glibc.git;a=blob_plain;f=malloc/arena.c;hb=HEAD)
  - The formatted source: [GitHub.com](https://github.com/aggrawal-ankur/glibc-malloc-explore/blob/main/formatted-malloc.c)

---

**Problem1: Circular dependency.**
  - To explain A, B is required. To explain B, C is required. To explain C, A is required.
  - When this happens, I try to find a natural starting point. If there isn't one, I use a forward declaration, which explains the dependency chain on surface and then I dive into it.

---

**Problem2: A concept can't be explained fully in the moment.**
  - I simply acknowledge it and ensure that the concept is properly explained later and the explanation is not hidden or buried under paragraphs.

# Scope and Reproducibility

It is a common pitfall to assume the `glibc` running on a standard Linux distribution (like Ubuntu or Fedora) is identical to the upstream source code hosted by the GNU Project. Distributions prioritize stability. They often "freeze" older glibc versions and apply custom backports, security patches, and compiler flags.

If you attempt to follow this guide using your host OS's default `malloc`, you will likely encounter mismatched source code line numbers and legacy structures that have been phased out of modern upstream code.

This writeup completely focuses on the upstream glibc-malloc, maintained by the GNU project. Because it is an active project, receiving updates all the time, this writeup is anchored at a stable release, called **glibc-2.43** (the current release-tag), which was released on **January 23, 2026**.

---

To ensure **100% reproducibility**, we will setup a lab environment using Docker.
  - **Base Image:** `debian:trixie`.
  - **glibc-version:** glibc-2.43
  - **Commit-id as per the release tag:** `f762ccf84f122d1354f103a151cba8bde797d521`
  - **Commit details:** [sourceware.org](https://sourceware.org/git/?p=glibc.git;a=commit;h=f762ccf84f122d1354f103a151cba8bde797d521) 

**Step1:** Clone this repository on your system.
```bash
git clone https://github.com/aggrawal-ankur/systems-dives.git    /repo
```

**Step2:** Use the Dockerfile in `./glibc-malloc/` to setup the container image.
```bash
cd /repo/glibc-malloc/
sudo docker build -t glibc-exp-img .
```

**Step3:** Setup a container using the custom image.
```bash
sudo docker run -it --name explore-glibc  glibc-exp-img:latest bash
```

**Step4:** The `/experiment-dir/` is where all the source code for the experiments is. It also contains build scripts to ensure seamless setup of the experiments.

**Step5:** To confirm the build succeeded:
```bash
$ /opt/glibc-2.43/lib/libc.so.6 --version

GNU C Library (GNU libc) stable release version 2.43.
Copyright (C) 2026 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.
There is NO warranty; not even for MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE.
Compiled by GNU CC version 14.2.0.
libc ABIs: UNIQUE IFUNC ABSOLUTE
Minimum supported kernel: 3.2.0
For bug reporting instructions, please see:
<https://www.gnu.org/software/libc/bugs.html>.
```
  - It should print **2.43**.

---

Let's start with the groundwork.

# Groundwork (Incomplete)

All the virtual memory is released by the kernel. The kernel releases memory in pages.

Modern Linux distributions have 4 KiB pages. Therefore, the least possible memory the kernel can release is 4096 bytes.

But a process's requirement's are arbitrary. This creates a gap, which is bridged by a "userspace dynamic memory allocator" program.

The allocator requests pages from the kernel, carves them into pieces and allocate to the process.

The allocator has to track the memory it has requested from the kernel, so that it can be returned later. This includes tracking the total pool of available memory, the allocated pieces of memory, and the pieces the process has "freed".

To track memory, the allocator has to manage a bookkeeping. The bookkeeping strategy is what that fundamentally distinguishes two allocators.

This writeup explores how glibc-malloc manages this bookkeeping.


TO BE CONTINUED


# Chunk Description

When malloc is called, the allocator carves a piece of dynamic memory, attaches some bookkeeping and returns it to the process. This bookkeeping is kept in a structure called `malloc_chunk`.

***A chunk is a piece of metadata associated with a portion of dynamic memory.*** But when we say "a chunk of memory", it refers to the *chunk metadata* and the *dynamic memory* together.

This metadata sits right before the payload memory, like this:
```
metadata payload
         ^
         pointer returned to the process
```

---

The layout of this chunk is:
```c
struct malloc_chunk {

  INTERNAL_SIZE_T       mchunk_prev_size;
  INTERNAL_SIZE_T       mchunk_size;

  struct malloc_chunk*  fd;
  struct malloc_chunk*  bk;

  struct malloc_chunk*  fd_nextsize;
  struct malloc_chunk*  bk_nextsize;
};
```

The authors have acknowledged that this layout is "misleading". Line 1082.
```
/*
  This struct declaration is misleading (but accurate and necessary).
  It declares a "view" into memory allowing access to necessary
  fields at known offsets from a given base. See explanation below.
*/
```
But in my view, the layout is well-reasoned but the reasoning is not documented properly, which makes it complicated to understand. The following is my attempt to make that reasoning visible.

Let's start with understanding each field in malloc_chunk.

## Layout Description

The **allocation size** is divided into **small** and **large** based on a threshold. Therefore, we have two types of chunks based on **size**: small chunks and large chunks.

A chunk can exist in two states: **in-use** and **free**.
  - **In-use chunks** (both small and large) are self-managed and require nothing other than the usual chunk metadata.
  - **Free chunks** require extra bookkeeping as they can be reused in servicing future malloc requests. Small free chunks and large free chunks are managed differently.

---

Based on the information above, the allocator has 3 chunk states to manage.
  1. **In-use chunks**: chunks the process is actively using (both small and large).
  2. **Small free chunks**: small chunks the process has freed.
  3. **Large free chunks**: large chunks the process has freed.

Here is a high level description of how malloc_chunk is used to represent these 3 states of chunks.

`mchunk_prev_size` holds the size of the previous chunk and `mchunk_size` holds the size of the current chunk.
  - **Note1: size means (chunk_metadata + dynamic_memory).**
  - **Note2: INTERNAL_SIZE_T is discussed later in the same parent heading. For the time being, treat it like `size_t`.**
  - **Note3: The existence of mchunk_prev_size is discussed later in the same parent heading.**

---

Free chunks are managed via bins, which are "**circular doubly linked lists**". We have small bins for small chunks and large bins for large chunks.

Small bins manage free chunks of exact size classes, while large bins manage free chunks falling in specific size ranges. For example, we have:
  - a small bin of size class 80 bytes. It contains free chunks sized 80 bytes.
  - a large bin of size range [1024, 1088) bytes. It contains free chunks of sizes falling in that range, like 1024, 1040 and so on.

A small bin manage a **linked list** of free chunks using the `fd/bk` pointers in malloc_chunk.

A large bin manage two types of links among its free chunks.
  - The `fd/bk` pointers manage links between each free chunk in the bin.
  - Although a large bin manages chunks falling in a specific size range, there can be free chunks of same size as well. To link these chunks on top of the generic links, we use the `fd_nextsize/bk_nextsize` pointers.
  - For example, a large bin managing chunks in `[1024, 1088)` range had three free chunks of sizes 1024, 1056, 1024. The three chunks will be linked via fd/bk and the two 1024 bytes chunks will have an extra fd_nextsize/bk_nextsize link.

---

In simple words, ***malloc_chunk is a generic implementation, designed to provide a single interface for all the three states in which a chunk can exist.*** This is both advantageous and cumbersome.

This is the reasoning behind the layout. Let's discuss how this layout is used.

## Usage Description

**Note: For simplicity, we do all the calculations for 64-bit Linux. But the rules remains the same for 32-bit Linux. Just use 4 instead of 8.**

On 64-bit Linux, both size_t and pointers are 8-bytes wide. That means, the size of malloc_chunk is (8*6) 48 bytes. We can verify this with sizeof as well. Create a c file, copy the definition, create an instance and use `sizeof()`.

malloc_chunk being a generic implementation is advantageous as it allows all the three 3 states of a chunk to be represented by a single struct definition. But in reality, these 3 states require only a subset of the whole implementation.
  1. mchunk_prev_size and mchunk_size are necessary in all the cases.
  2. In an in-use chunk, the four pointers aren't useful.
  3. In a small free chunk, fd/bk are required and fd_nextsize/bk_nextsize aren't useful.
  4. In a large free chunk, everything is required.

Therefore, we need to use malloc_chunk such that,
  - fd/bk/fd_nextsize/bk_nextsize remain garbage in an in-use chunk, and
  - fd_nextsize/bk_nextsize remain garbage in a small free chunk.

We have two ways to use malloc_chunk.
  - **Method1:** Set the required members appropriately and the not required ones NULL.
  - **Method2:** Only set the required members and leave the rest.

Let's calculate how these methods perform for an in-use chunk.

In method1, we have to allocate full 48 bytes for the metadata, followed by the payload memory. This wastes 24 bytes per in-use chunk, regardless of small or large. Visually:
```
-----------------------------------------------------------------
| prev_size | mchunk_size | fd | bk | fd_nextsize | bk_nextsize |
-----------------------------------------------------------------
                                                                  ^
                                                                  Dynamic memory starts here
```

In method2, only the initial 16 bytes in the metadata struct are usable, followed by the payload memory. Visually:
```
-----------------------------------------------------------------
| prev_size | mchunk_size | fd | bk | fd_nextsize | bk_nextsize |
-----------------------------------------------------------------
                            ^
                            Dynamic memory starts here
```
  - Method2 prevents the wastage of the trailing 24 bytes.
  - Those fields still exist, but are garbage.

Method2 is how glibc does it.

---

**Important Note**

As someone new to this, the design is not very beginner-friendly. If you can't understand it in your first attempt, remember this, I've invested weeks to understand every part of glibc-malloc, fighting this design. The document you are reading is a result of multiple rewrites.

A lot of times, we prevent ourselves from understanding the author's design because, we think that the problem should be solved in a certain different way. This is completely an unconscious act, which is why we are not aware of it.

To improve yourself, you can try this.
  - Acknowledge the author's design even if you have to do it against your will.
  - Write your design on a paper or in your IDE.
  - Contradict your design with the author's design and notice which performs better.

Either this act will make the author's design clearer, or you'll end up finding a better one. Either way, it's a win.

---

This is how a single malloc_chunk is used. But a standard Linux process calls malloc and free several times. This repeated allocation-deallocation fragments the memory and creates a problem for the allocator.

## The Problem: Fragmentation

***When memory is allocated-deallocated multiple times, it creates gaps of "unused memory" in the address space.***

This increases the pressure on physical memory because, the freed chunks are still backed by physical memory but not utilized by the process. The only way to reduce this pressure is to reallocate the freed chunks, which entirely depends on the process asking for a size which is available as a free chunk.

The fragmented memory can exist in two layouts, depending on the malloc-free sequence.
  1. In-use and free chunks in an alternating sequence, like this: {...., in-use, free, in-use, free, ....}
  2. Multiple free chunks adjacent to each other, like this: {...., in-use, free, free, in-use, ....}

Suppose two chunks of 48 bytes were freed. Now we have 96 bytes of memory which can be reused. The next malloc request asked for 96 bytes. Can we reuse the 96 bytes? No. Because, those 96 bytes are not contiguous. That is fragmentation in layout1.

Suppose two adjacent chunks, each of size 48 bytes, were freed and the next malloc request asked for 96 bytes. Can we reuse these 96 bytes? NO. Because, the memory is contiguous yet fragmented across two chunks. That is fragmentation in layout2.

---

The allocator can't do anything about the fragmentation in layout1. But the allocator can manage layout2 fragmentation to some extent. Take this:
  - The probability of the process asking for another 48 bytes bytes chunk might be less, but the probability of asking a size which falls in the range of 48-96 bytes is definitely higher.
  - But this is possible only when the two adjacent free chunks are coalesced, making one big block of free memory. Basically, converting layout2 memory to layout1 memory.

To implement coalescing, we need two things.
  1. A way to identify if the next/prev chunk is free.
  2. If the next/prev chunk is free, we need a way to reach that chunk from the current chunk.

A computer scientist and mathematician named **Donald Knuth** has discussed multiple strategies to manage dynamic memory. One of these strategies describe a way to embed coalescing support directly in the chunk metadata. It is discussed in his book *The Art Of Computer Programming, Volume 1: Fundamental Algorithms*, paragraph 4, page 440. It is called, **the boundary tag method**.

The authors of this allocator have implemented the same strategy. Let's dive into coalescing.

## Coalescing

Coalescing can happen in two ways.
  1. **Forward coalescing**, where we coalesce the n<sup>th</sup> chunk with the (n+1)<sup>th</sup> chunk.
  2. **Backward coalescing**, where we coalesce the n<sup>th</sup> chunk with the (n-1)<sup>th</sup> chunk.

Forward coalescing is simple to implement. Just add the size of the current chunk into the pointer and we are on the next chunk. But backward coalescing is not that simple because, we don't know the size of the previous chunk.

For this reason, malloc_chunk comes with `mchunk_prev_size`. mchunk_prev_size holds the size of the previous chunk and we can use it to offset back to the (n-1)<sup>th</sup> chunk.

Now we need a way to find if the next/prev chunk is free. To do this, we use mchunk_size. Let's understand how.

---

### The second use of 'mchunk_size'

We know that `malloc()` returns a memory which can store the largest fundamental type supported by the ISO C standard.

The largest type in both 32-bit and 64-bit architectures is twice the maximum addressable width, i.e. `double` (8 bytes) on 32-bit and `long double` (16 bytes) on 64-bit.

That means, the size is always a multiple of 8, regardless of the architecture (32-bit or 64-bit). That means, the lower 3 bits in mchunk_size are always 0 (or better, **unused**).

We can use these bits of mchunk_size to store state information. It does change the size value, but we can mask the lower 3 bits to get the actual size.

Here is a description of these bits.

| Bit # | Bit Name | State | Description |
| :---: | :------- | :---- | :---------- |
| 0 | PREV_INUSE (P) | **0** (clear) | The (n-1)<sup>th</sup> chunk is free and the prev_size of the n<sup>th</sup> chunk stores the size of the (n-1)<sup>th</sup> chunk. |
| | | **1** (set) | The (n-1)<sup>th</sup> chunk is in-use and the prev_size of the n<sup>th</sup> chunk doesn't store the size of the (n-1)<sup>th</sup> chunk. |
| 1 | IS_MMAPPED (M) | **0** (clear) | 
| | | **1** (set) | 
| 2 | NON_MAIN_ARENA (A) | **0** (clear) | The chunk belongs to the main arena. |
| | | **1** (set) | The chunk belongs to a non-main arena. |

---

Right now, only the 0th bit concerns us. The remaining two bits are discussed later.

Now we can implement coalescing through the boundary tag method.

---

### The Boundary Tag Method

***Boundary tag method is a dynamic memory management technique, where the size is stored both in the head and the tail of the chunk.***

We can notice a problem here. Boundary tag method suggest to have metadata before and after the payload memory. `malloc_chunk` compensates for what comes before the payload memory, what compensates for the trailing size field?

If we create a separate struct, like `malloc_chunk_trail` and put it after the payload memory, that creates bookkeeping havoc.

How about putting another size field in the front of malloc_chunk and use it as a property of the previous chunk?
  - We don't have to create a new metadata struct.
  - The first chunk's prev_size would be a waste, as nothing exist before it. But we are ready for that tradeoff.
  - There will be some sort of dummy chunk in the end to compensate for the last malloced chunk.

The layout would look something like this:
```
Structurally -> [ Chunk1                                     ] [ Chunk2                                     ] [ Dummy Chunk                                ]
                ---------------------------------------------- ---------------------------------------------- ----------------------------------------------
                | prev_s | chunk_s | fd | bk | fd_ns | bk_ns | | prev_s | chunk_s | fd | bk | fd_ns | bk_ns | | prev_s | chunk_s | fd | bk | fd_ns | bk_ns |
                ---------------------------------------------- ---------------------------------------------- ----------------------------------------------
Functionally ->          [ Chunk1                                       ] [ Chunk2                                     ]
```

***The mchunk_prev_size of the n<sup>th</sup> chunk is "by-use" a part of the (n-1)<sup>th</sup>chunk. Structurally, it is still a part of the n<sup>th</sup> chunk.*** This is boundary tag method in implementation.

-- **Important Note** --

***Again, as someone new to this, the design is not beginner-friendly at all. If you can't understand it in your first attempt, don't worry. What you are reading is months of work and a result of multiple rewrites.***

***I don't how long it will take you to understand it, but it took me more than a month worth of efforts to understand this design. Even after that, I understood the implementation of boundary tag method wrong. After that, I corrected myself.***

***Therefore, give yourself time.***

---

At this point, we completely understand the existence of each field in malloc_chunk, but we still feel empty. To fill that void, we have to understand the last piece in the puzzle.

## The Size Model

Everything in glibc-malloc is directly or indirectly connected with size. Therefore, the allocator has a size model to work with "size" efficiently.

The size model is described using macros. It contains two types of macros.
  1. Macros which resolve to certain numeric values.
  2. Macros that are named blocks of code.

The reason preprocessing is preferred over inlining, **in some scenarios**, is that, modern compilers are intelligent due to decades of compiler research. But optimization techniques like **function inlining** were in a complicated state in the early days.
  - The compilers did support inlining, but it was limited to a single translation unit and not as aggressive as we see it today.
  - Even when C99 formally introduced `inline`, it was still a hint, not a guarantee.

As a result, preprocessing was the only native option for the programmers to achieve similar effects.

Another reason macros are still advantageous (in some scenarios) is that inlining is not a simple copy-paste. 
  - The compiler has to understand the overall situation to inline a function such that everything works in harmony.
  - On the other hand, preprocessing is a simple copy-paste routine with ZERO runtime overhead.

---

The size model is about making the **request size** usable as per the bookkeeping process.

The first thing we need to understand is the allocator's choice for receiving the request size.

### The use of size_t

malloc() takes a size (in bytes) as its argument.

Each data type has a maximum addressable limit. We need a type which can contain the largest addressable value in an architecture. The answer is `size_t`.

***`size_t` is a guarantee from the C standard to be an "unsigned integer data type", which is exactly as wide as the "pointer type" in an architecture, taking the platform's data model in account. In simple words, size_t is a data type which is wide enough to contain the largest addressable value in an architecture.***

| Arch | size_t |
| :--- | :----- |
| 32-bit Linux | 4 bytes |
| 64-bit Linux | 8 bytes |

***`size_t` and the clever use of preprocessing is the basis of an architecture-agnostic implementation.***

---

But size_t is not used directly. It is masked with a type definition.
```c
#define INTERNAL_SIZE_T  size_t
```
This makes `size_t` a tunable parameter.

***A parameter whose value can be tweaked at compile-time is called a tunable parameter (or, a tunable).*** There are multiple such parameters provided for the programmers to customize malloc to their needs.

size_t being a tunable creates a third possibility, where pointers are 8 bytes and INTERNAL_SIZE_T is 4 bytes wide. But this looks counterintuitive to the definition of size_t.

In certain IoT and embedded system configurations, the architecture is modern (64-bit) with memory constraints. Tweaking INTERNAL_SIZE_T to 4 does two things.
  1. The metadata size per in-use chunk is shrunk by half, reducing the overall memory footprint. With INTERNAL_SIZE_T=4, mchunk_prev_size and mchunk_size size 4 bytes each, totaling to 8 bytes.
  2. The maximum request size is reduced drastically to ~4 GiB of virtual memory.

Tuning INTERNAL_SIZE_T is not problematic because, we use size_t to store a size, not an address. The tradeoff is also explicit and acceptable provided the constraints.

Also, INTERNAL_SIZE_T=4 doesn't create possibility for padding bytes in the struct as each member is naturally aligned to its own width given the layout order.

---

To summarize, there are the three configurations the allocator must handle.

| Config # | size_t width | Pointer width |
| :------- | :----------- | :------------ |
| 1 | 4 bytes | 4 bytes |
| 2 | 8 bytes | 8 bytes |
| 3 | 4 bytes | 8 bytes |

---

Now we are set to explore the macros that resolve to a numerical value.

---

### Macro #1 -> SIZE_SZ

It is the width of size_t on the target machine's architecture.
```c
/* The corresponding word size. */
#define SIZE_SZ  (sizeof(INTERNAL_SIZE_T))
```

| Arch   | SIZE_SZ |
| :---   | :------ |
| 32-bit | 4 bytes |
| 64-bit | 8 bytes |

---

### Macro #2 -> CHUNK_HDR_SZ

It stands for "chunk header size", which refers to the metadata bytes that sit at the beginning of an in-use chunk i.e, "mchunk_prev_size and mchunk_size".
```c
#define CHUNK_HDR_SZ    (2 * SIZE_SZ)
```

| Arch   | CHUNK_HDR_SZ |
| :---   | :----------- |
| 32-bit | 8 bytes |
| 64-bit | 16 bytes |

---

**Note: It refers to the structural overhead, not the functional overhead. Because, if it were functional, we wouldn't have count mchunk_prev_size, as it is a property of the previous chunk.**

---

### Macro #3 -> MIN_CHUNK_SIZE

It is the size of the "structurally" smallest possible chunk in an architecture.
```c
#define MIN_CHUNK_SIZE    offsetof(struct malloc_chunk, fd_nextsize)
```

`offsetof` is an ANSI C macro, defined in `stddef.h`, used to determine the byte offset of a specific member from the beginning of its parent structure.

Let's derive it manually on 64-bit.
```
  0-7 bytes -> mchunk_prev_size
 8-15 bytes -> mchunk_size
16-23 bytes -> fd
24-31 bytes -> bk
32-39 bytes -> fd_nextsize
40-47 bytes -> bk_nextsize
```
So, MIN_CHUNK_SIZE would be 32 on 64-bit.

| Config # | MIN_CHUNK_SIZE |
| :------- | :------------- |
| 32-bit   | 16 bytes |
| 64-bit   | 32 bytes |
| INTERNAL_SIZE_T=4 | 24 bytes |

---

### Macro #4 -> MALLOC_ALIGNMENT

It defines the minimum alignment for in-use chunks.
```c
#define MALLOC_ALIGNMENT  (                   \
  (2 * SIZE_SZ) < __alignof__(long double)    \
  ? __alignof__(long double)                  \
  : 2 * SIZE_SZ
)
```

`__alignof__` is an operator that returns the alignment requirement of a data type (in bytes).

| Arch   | \_\_alignof__(long double) |
| :---   | :------------------------- |
| 32-bit |  4 bytes |
| 64-bit | 16 bytes |

The macro becomes:
```c
// 32-bit (size_t=4)
MALLOC_ALIGNMENT == (8 < 4)  ?  4  :  8  == 8

// 64-bit (size_t=8)
MALLOC_ALIGNMENT == (16 < 8)  ?  16  :  16  == 16
```

In both the cases, the alignment is kept twice of the maximum addressable width. This is because malloc is a general purpose allocator. It is unaware of what the caller will store in the returned memory. So it ensures that the returned memory is aligned to all the fundamental types the C standard supports. This is in accord to what we have discussed in the mchunk_size section.

However, it's real importance is in that third configuration, where pointers are 8 bytes and INTERNAL_SIZE_T is 4 bytes wide.
```c
MALLOC_ALIGNMENT = (2 * 4) < 16  ?  16  :  16
```

Notice how the alignment is decided based on the largest fundamental type in the architecture, not the value of INTERNAL_SIZE_T.

---

| Config # | MALLOC_ALIGNMENT |
| :------- | :--------------- |
| 32-bit   |  8 bytes |
| 64-bit   | 16 bytes |
| INTERNAL_SIZE_T=4 | 16 bytes |

---

### Macro #5 -> MALLOC_ALIGN_MASK

MALLOC_ALIGNMENT is a power-of-2 value and MALLOC_ALIGN_MASK is the bit mask of it.
```c
#define MALLOC_ALIGN_MASK    (MALLOC_ALIGNMENT - 1)
```

MALLOC_ALIGN_MASK has the lowest 4 bits set which are clear in MALLOC_ALIGNMENT.
```bash
# 64-bit
MALLOC_ALIGNMENT  = 16 = 0001 0000
MALLOC_ALIGN_MASK = 15 = 0000 1111
```

It is used in a variety of bitwise operations.
  1. Check if a size/address is aligned to the alignment boundary (the lower 4-bits in the addr/size must be all 0 to yield a zero against all 1s of the bit-mask).
     ```c
     (addr & MALLOC_ALIGN_MASK) == 0  :=  aligned
     (addr & MALLOC_ALIGN_MASK) != 0  :=  misaligned
     ```
  2. Round a size/address up to the alignment boundary.
     ```c
     -> (size + MALLOC_ALIGN_MASK) & ~MALLOC_ALIGN_MASK
     -> (34 + 15) & ~15
     -> 49 & -16
     -> 48
     ```
  3. Round a size/address down to the alignment boundary.
     ```c
     -> size & ~MALLOC_ALIGN_MASK
     -> 41 & ~15
     -> 32
     ```
---

| Config # | MALLOC_ALIGN_MASK |
| :------- | :---------------- |
| 32-bit   |  7 |
| 64-bit   | 15 |
| INTERNAL_SIZE_T=4 | 15 |

---

### Macro #6 -> MINSIZE

It is the smallest size that malloc supports.
```c
#define MINSIZE    (unsigned long)( \
  ( (MIN_CHUNK_SIZE + MALLOC_ALIGN_MASK) & ~MALLOC_ALIGN_MASK)
)
```

We know that an in-use chunk requires (2 * SIZE_SZ) bytes for storing metadata and when it is freed, it requires (2 * ptr_width) bytes to manage fd/bk. That totals to 16 bytes on 32-bit and 32 bytes on 64-bit.

In INTERNAL_SIZE_T=4, the metadata overhead is 24 bytes, but 24 is not aligned to the alignment boundary (16-div), so we round it up to the next boundary, making MINSIZE 32 bytes.

| Config # | MINSIZE |
| :------- | :------ |
| 32-bit   | 16 bytes |
| 64-bit   | 32 bytes |
| INTERNAL_SIZE_T=4 | 32 bytes |

---

In the MIN_CHUNK_SIZE section, we have seen that it is the size of the structurally smallest chunk possible in an architecture. MINSIZE is the actual smallest chunk size possible in an architecture after adding alignment constraints.

The values happen to be equal in the first two configurations because the struct layout aligned with the alignment constraints. That broke with INTERNAL_SIZE_T=4.

---

### Macro #7 -> request2size

This macro is responsible for enforcing the size model on the requested size.

**It is the closest we can "statically" see the boundary tag method in implementation.**

It is defined as:
```c
#define request2size(req)    (                     \
  (req + SIZE_SZ + MALLOC_ALIGN_MASK < MINSIZE)    \
  ? MINSIZE                                        \
  : (req + SIZE_SZ + MALLOC_ALIGN_MASK) & ~MALLOC_ALIGN_MASK    \
)
```

Let's take an example on 64-bit architecture: `malloc(20)`.
  - (20 + 8 + 15) < 32; 43 < 32; Therefore, the false case is chosen.
  - aligned_size = (20 + 8 + 15) & ~15
  - aligned_size = 43 & ~15 = 32.
  - In these 32 bytes, we need 20 bytes of usable memory. That leaves us 12 bytes of memory for metadata. But metadata requires 16 bytes of space. We are short on 4 bytes. Visually:
    ```
          8           8         8     8
    -----------------------------------------------------------------
    | prev_size | mchunk_size | fd | bk | fd_nextsize | bk_nextsize |
    -----------------------------------------------------------------
                                ^ ptr_to_mem
    ```
  - But in the boundary tag discussion, we have agreed on a dummy chunk in the end. That would make the situation this:
    ```
          8           8         8     8
    -----------------------------------------------------------------
    | prev_size | mchunk_size | fd | bk | fd_nextsize | bk_nextsize |
    -----------------------------------------------------------------
                                              8
                                        -----------------------------------------------------------------
                                        | prev_size | mchunk_size | fd | bk | fd_nextsize | bk_nextsize |
                                        -----------------------------------------------------------------
                                ^ ptr_to_mem
    ```
  - That dummy chunk has a name. **The top chunk** is a special chunk that sits after all the malloced chunk. We'll talk about it later in detail.

So, request2size deliberately leaves out SIZE_SZ bytes of memory in every chunk because the payload memory of a chunk is allowed to "spill over" and occupy the prev_size of the next chunk. For the last allocated chunk, the prev_size is provided by the top chunk.

---

### Macro #8 -> chunk2mem

`chunk2mem` takes a pointer to a chunk, casts it to `char*` (for pointer arithmetic) and add "chunk header size" to it.
```c
#define chunk2mem(p)    ( (void*) ((char*)(p) + CHUNK_HDR_SZ) )
```

This will land us at the `fd` field in the struct, where the payload memory starts in an in-use chunk, just the way we have discussed.

---

***That's what the author probably meant when he said, "This struct declaration is misleading (but accurate and necessary)."***


Now that we understand chunks, let's explore how freed chunks are managed, basically, **the bookkeeping process**.

---

# Binning, The Bookkeeping Process (Actively writing)

**Important Note**
  - The whole implementation of bins is filled with inconsistencies. The annotations and the macros are not converging.
  - When I was trying to build the exact structure of bins, I have gone through a lot of frustration and agitation. Sometimes, the inconsistencies would simply not make sense in a project like this.
  - The description of bins could have been shorter and simpler iff those inconsistencies were absent. So, the description includes dynamic analysis in substantial amount to verify the reality at runtime.
  - Therefore, if you are perplexed at any moment, questioning "whether I am putting enough efforts", remember, the writer of this writeup has gone through them as well and what you are reading is a culmination of an understanding formed after multiple rewrites.

---

***A bin is a data structure, based on "circular doubly linked lists", used to manage free chunks.*** A bin is also called a "free list".

Conceptually, we have three class of bins: **smallbins** for small chunks, **largebins** for large chunks and a bin to hold chunks temporarily, called **the unsorted bin**.

At the implementation level, we have **only one** data structure, representing all the bins.

Before moving on to the implementation, we need to answer a question. ***Is one data structure capable of representing all the three bins? If yes, should we do it or not?***
  - The difference b/w the three bins is only conceptual. There is no runtime difference. So the answer is simple. We can absolutely use a single data structure to represent all the bins.
  - Should we use a single data structure or multiple is a choice matter, governed by how you want to structure the rest of the binning process. This implementation goes with the "single data structure" option, and we will try to answer why later.

---

To understand how the core bins data structure is implemented, we must understand linked lists, especially the doubly circular linked list.

If you have taken any data structures course, you might be familiar with linked lists, but familiarity is not enough to understand this implementation.

Moreover, data structure questions are generally solved in object oriented languages like C++/Java (this is what I have seen on the internet), which are completely different from C's procedural approach. For this reason, I have created 4 linked list implementations in the [./linked-list-code/](./linked-list-code/) directory. This does a few things.
  - It gives a common ground to base our understanding. As a writer, I can be sure that my reader and I have the same mental model of the problem.
  - Those who feel rusty about their understanding can quickly visit the code to remember it.
  - At first, I was trying to find something on the internet, but I could not find anything suitable.

Let's start with the implementation.

## Implementation of `bins`

A linked list have head and tail pointers to enables operations from both the ends. Let's start with implementing a single bin. We have two ways to do it.
  1. A distinct struct, like this:
     ```c
     struct List{
       struct* Node head;
       struct* Node tail;
     };
     ```
  2. Managing the head and tail pointers individually, like this:
     ```c
     int main(void){
       struct Node* head;
       struct Node* tail;
     }
     ```

Method1 provides an abstraction, making management slightly more intuitive, and method2 initializes and uses head/tail pointers directly.

Both the methods are the same, and based on the tutor's choice, you might have seen both. Personally, I have seen both, especially the first one in the cpp space. The [double circular list implementation](./linked-list-code/1-simple-dll.c) uses method1.

---

But the `bins` data structure is a collection of multiple bins. To manage multiple bins, we have two options.
  1. Manage the head/tail pointers individually, like this:
     ```c
     int main(void){
       struct Node* l1_head;
       struct Node* l1_tail;
       struct Node* l2_head;
       struct Node* l2_tail;
       // and so on....
     }
     ```
  2. Create an array of `Node*` elements.
     ```c
     int main(void){
      unsigned int bin_count = 10;
      struct Node* listHeaders[bin_count*2];
     }
     ```

It is clear that method1 suffers at management. So method2 is what we use.

Therefore, ***the `bins` data structure is implemented as an array of **list headers** (or, bin headers), representing the head and the tail of the bin. n\*2 pointers are required for n bins, making the length of this array `n(bins) * 2`.***

We have an array-based implementation for both `List*` and `Node*`. Checkout [list_ptr-array.c](./linked-list-code/2-list_ptr-array.c) and [node_ptr-array.c](./linked-list-code/3-node_ptr-array.c)

**I'd recommend you to read both the implementations above, at least the "node_ptr-array" implementation, as it is used in the next part.**

Everything is familiar so far, let's talk about the difference.

### The problem

We have an array of `Node*` elements, and an implementation, where
  1. the head/tail pointers point to the first and the last nodes in a list,
  2. the head/tail pointers create **ends** in the list while the next/prev pointers (per node) create circularity, and
  3. an empty list has the head/tail pointers NULL.

The push/delete strategy is divided into: *single node list* and *multiple node list*.

The strategy is simple and works great, but let's come back to what we are doing. The linked list here is a part of a low level code, which has to be as efficient as it is possible.

The push/delete code is one of the most used operations and the bottleneck is sitting right in the start of this path, i.e. the "*single node list*" pathway. *For every list, we have to check if it is a singular list.*

We need a solution that makes the push/delete logic branchless, a complete happy path. Let's start thinking.

### Finding the solution

The listHeaders in `node_ptr-array.c` are fixed to the head/tail nodes in the list. If the list is empty, they are NULL.

That's how a single list look likes:
```c
head<->node1<->tail
```

When a node is added to it, let's say, on the tail, it becomes:
```c
head<->node1<->node2<->tail
```

When we print the list, we anchor the start and the end with the head/tail pointers; We don't rely on the next/prev pointers because, they maintain circularity.

The head/tail pointers are fixed to whatever the head/tail node is in the list at any instance. That's the bottleneck.

The listHeaders only provide the pointer to the head/tail nodes. They themselves don't participate in the push/delete process. That's the reason they must be set NULL when the list empty.

***The solution is to make the listHeaders participate in the process. They must not be treated separately when the list is empty.***

Let's implement the solution.

### Implementing the solution

To implement this solution, we have to change how we use the listHeaders. The listHeaders[] is defined as:
```c
struct Node{
  int data;
  struct Node* next;
  struct Node* prev;
}

struct Node* listHeaders[nbins*2];
```

**Reminder: The struct will have 4 padding bytes after the `data` member to keep alignment.**

For list0, the headers are listHeaders[0] and listHeaders[1]. For list1, the headers are listHeaders[2] and listHeaders[3]. For nth list, the headers will be `listHeaders[i*2]` and `listHeaders[(i*2)+1]`

If we count the lists from 1, the formula will become: `listHeaders[(i-1)*2]` and `listHeaders[((i-1)*2)+1]`

---

If we ask "*what actually participates in the push/delete process*", the answer would be, **nodes**. Does that mean, *to make the headers participate in the process, the headers must be nodes themselves?* Yes. The question is how!

***Moments like these, where you have a rough idea about the outcome, and you need to find the process that can lead to it, but you have absolutely no substrate to think upon, one way to deal with this is to ask as many questions as you can, related or unrelated. Eventually, you will find the right one.*** So, let's ask some questions.

If the headers are nodes themselves,
  - are there distinct nodes for both the headers?, or
  - there is one node that contains both the headers?

In either of the cases, what the fields of this/these fake nodes will contain? The `data` field will be garbage for obvious reasons. What about the next/prev fields?

If we had two fake nodes, the head fake node's next would probably point to the real head node and the tail fake node's prev would point the real tail node. But that still doesn't explain what happens to the prev and next of the head and the tail fake nodes.
  
Does the head fake node's prev point to the real tail node and the tail fake node's next points to the real head node in the list? If that is the question, we will end up with two identical fake nodes. Is it?

Does that mean *we need only one fake node, whose next/prev will point to the real head/tail nodes in the list?* Something like:
```c
....fake_node<->new_node<->exist_node<->fake_node....
```
  - Congratulations. That's the answer.

***We need a fake node whose next and prev align with the head and tail nodes in the list.***

Now we have pinpointed what we need exactly. Let's think about how we will get it.

Let's take an example. The numbers represent 64-bit addresses.
```
1000 :: &listHeaders[0]
1008 :: &listHeaders[1]
```

If we need a fake node, such that, it's next/prev align with the addresses that point to the head/tail nodes in a list represented by the above headers, where should the fake node start in the memory? The answer is 992.

That means, the fake node for the above headers can be obtained this way:
```c
struct Node* fake_node = (Node*)((char*)(&listHeaders[0])-8);

fake_node->next = listHeaders[0];
fake_node->prev = listHeaders[1];
```

To obtain a fake node suitable for the nth bin, we can do it this way:
```c
// 0-based indexing
struct Node* fake_node = (Node*)((char*)(&listHeaders[i*2])-8);

// 1-based indexing
struct Node* fake_node = (Node*)((char*)(&listHeaders[(i-1)*2])-8);
```

When the list is empty, the next/prev of the fake_node will simply point to the head address itself, i.e `(Node*)((char*)(&listHeaders[i*2])-8);` or the other one.

---

Two things:
  1. You don't have to worry about overwriting those 8 bytes if your pointer math is all right.
  2. It will not raise any segmentation faults because, there is stuff that comes before this listHeaders array. The memory is already owned by the process.

---

To complete your understanding, open the [fake-node-impl.c](./linked-list-code/4-fake-node-impl.c). You might notice it still contains the single vs multiple distinction. Just comment that block of code and run again. You'll not be surprised that it works. [fake-node-impl(2).c](./linked-list-code/4-fake-node-impl(2).c) contains the final version of this implementation.

---

If you have understood the above story, congratulations, you have understood this annotation:
```
  To simplify use in double-linked lists, each bin header acts as 
  a malloc_chunk. This avoids special-casing for headers. But to 
  conserve space and improve locality, we allocate only the fd/bk 
  pointers of bins, and then use "repositioning tricks" to treat 
  these as the fields of a malloc_chunk*.
```

The whole story above is what the author meant by "repositioning tricks". 

---

So, to answer how `bins` are implemented, ***they are implemented as an array of bin headers of type mchunkptr (malloc_chunk\*). "Repositioning tricks" are used to find the correct bin.***
