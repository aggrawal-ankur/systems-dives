#ifndef void
#define void      void
#endif /*void*/

#include <stddef.h>    /* for size_t */
#include <stdlib.h>    /* for getenv(), abort() */
#include <unistd.h>    /* for __libc_enable_secure */

#include <string.h>
#include <atomic.h>
#include <_itoa.h>
#include <bits/wordsize.h>
#include <sys/sysinfo.h>

#include <ldsodefs.h>
#include <setvmaname.h>

#include <unistd.h>
#include <stdio.h>     /* needed for malloc_stats */
#include <errno.h>
#include <assert.h>
#include <intprops.h>

#include <shlib-compat.h>

#include <stdint.h>                /* For uintptr_t */
#include <stdbit.h>                /* For stdc_count_ones */
#include <stdarg.h>                /* For va_arg, va_start, va_end  */
#include <sys/param.h>             /* For MIN, MAX, powerof2 */
#include <libc-pointer-arith.h>    /* For ALIGN_UP et. al. */
#include <libc-diag.h>             /* For DIAG_PUSH/POP_NEEDS_COMMENT et al. */
#include <libc-mtag.h>             /* For memory tagging. */
#include <sysdep-cancel.h>         /* For SINGLE_THREAD_P. */
#include <malloc/malloc-internal.h>
#include <libc-internal.h>

/* For tcache double-free check. */
#include <random-bits.h>
#include <sys/random.h>
#include <not-cancel.h>


/* Debugging */
#ifndef MALLOC_DEBUG
#define MALLOC_DEBUG 0
#endif

#if USE_TCACHE
/* We want 64 entries.  This is an arbitrary limit, which tunables can reduce.  */
# define TCACHE_SMALL_BINS		64
# define TCACHE_LARGE_BINS		12 /* Up to 4M chunks */
# define TCACHE_MAX_BINS	(TCACHE_SMALL_BINS + TCACHE_LARGE_BINS)
# define MAX_TCACHE_SMALL_SIZE	tidx2csize (TCACHE_SMALL_BINS-1)

# define tidx2csize(idx)	(((size_t) idx) * MALLOC_ALIGNMENT + MINSIZE)
# define tidx2usize(idx)	(((size_t) idx) * MALLOC_ALIGNMENT + MINSIZE - SIZE_SZ)

/* When "x" is from chunksize().  */
# define csize2tidx(x) (((x) - MINSIZE) / MALLOC_ALIGNMENT)
/* When "x" is a user-provided size.  */
# define usize2tidx(x) csize2tidx (checked_request2size (x))

/* With rounding and alignment, the bins are...
   idx 0   bytes 0..24 (64-bit) or 0..12 (32-bit)
   idx 1   bytes 25..40 or 13..20
   idx 2   bytes 41..56 or 21..28
   etc. 
*/

/* This is another arbitrary limit, which tunables can change.
   Each tcache bin will hold at most this number of chunks.  */
# define TCACHE_FILL_COUNT 16

/* Maximum chunks in tcache bins for tunables.
   This value must fit the range of tcache->num_slots[] entries, 
    else they may overflow.  */
# define MAX_TCACHE_COUNT UINT16_MAX
#endif


/* Safe-Linking:
   Use randomness from ASLR (mmap_base) to protect single-linked lists
    of TCache. That is, mask the "next" pointers of the lists' chunks, 
    and also perform allocation alignment checks on them.
   This mechanism reduces the risk of pointer hijacking, as was done with
    Safe-Unlinking in the double-linked lists of Small-Bins.
   It assumes a minimum page size of 4096 bytes (12 bits). Systems with
    larger pages provide less entropy, although the pointer mangling
    still works.
*/
#define PROTECT_PTR(pos, ptr)    ((__typeof (ptr)) ((((size_t) pos) >> 12) ^ ((size_t) ptr)))
#define REVEAL_PTR(ptr)  PROTECT_PTR (&ptr, ptr)

/*
  The REALLOC_ZERO_BYTES_FREES macro controls the behavior of realloc (p, 0)
  when p is nonnull.
  
  - If the macro is nonzero, the realloc call returns NULL; otherwise, 
    the call returns what malloc (0) would. In either case, p is freed. 
  - Glibc uses a nonzero REALLOC_ZERO_BYTES_FREES, which implements 
    common historical practice.

  ISO C17 says the realloc call has implementation-defined behavior,
  and it might not even free p.
*/
#ifndef REALLOC_ZERO_BYTES_FREES
#define REALLOC_ZERO_BYTES_FREES 1
#endif

/* Definition for getting more memory from the OS.  */
#include "morecore.c"

#define MORECORE         (*__glibc_morecore)
#define MORECORE_FAILURE  NULL


/* Memory Tagging */
#ifdef USE_MTAG
static bool mtag_enabled = false;
static int  mtag_mmap_flags = 0;
#else
# define mtag_enabled false
# define mtag_mmap_flags 0
#endif /* USE_MTAG */

static __always_inline void*
tag_region (void *ptr, size_t size)
{
  if (__glibc_unlikely (mtag_enabled))
    return __libc_mtag_tag_region (ptr, size);
  return ptr;
}

static __always_inline void*
tag_new_zero_region (void *ptr, size_t size)
{
  if (__glibc_unlikely (mtag_enabled))
    return __libc_mtag_tag_zero_region (__libc_mtag_new_tag (ptr), size);
  return memset (ptr, 0, size);
}

/* Defined later.  */
static void* tag_new_usable (void *ptr);

static __always_inline void*
tag_at (void *ptr)
{
  if (__glibc_unlikely (mtag_enabled))
    return __libc_mtag_address_get_tag (ptr);
  return ptr;
}


/*
  MORECORE-related declarations. By default, rely on sbrk.
  It is the routine to call to obtain more memory from the system.
*/
#ifndef MORECORE
#define MORECORE sbrk
#endif

/*
  MORECORE_FAILURE is the value returned upon failure of MORECORE
    as well as mmap. 
  Since it cannot be an otherwise valid memory address, and must 
    reflect values of standard sys calls, you probably ought not
    try to redefine it.
*/
#ifndef MORECORE_FAILURE
#define MORECORE_FAILURE (-1)
#endif

/*
  If MORECORE_CONTIGUOUS is true, consecutive calls to MORECORE 
    with positive arguments always return contiguous increasing 
    addresses. This is true of unix sbrk.
  Even if not defined, when regions happen to be contiguous, 
    malloc will permit allocations spanning regions obtained from 
    different calls. But defining this when applicable enables 
    some stronger consistency checks and space efficiencies.
*/
#ifndef MORECORE_CONTIGUOUS
#define MORECORE_CONTIGUOUS 1
#endif

/*
  Define MORECORE_CANNOT_TRIM if your version of MORECORE cannot 
    release space back to the system when given negative
    arguments.
  This is generally necessary only if you are using a hand-crafted 
    MORECORE function that cannot handle negative arguments.
*/
// #define MORECORE_CANNOT_TRIM

/*
  MORECORE_CLEARS           (default 1)
    The degree to which the routine mapped to MORECORE zeroes out
    memory:
      -: never (0), 
      -: only for newly allocated space (1), or 
      -: always (2).
    The distinction between (1) and (2) is necessary because on
      some systems, if the application first decrements and then
      increments the break value, the contents of the reallocated space
      are unspecified.
*/
#ifndef MORECORE_CLEARS
#define MORECORE_CLEARS 1
#endif


/*
  MMAP_AS_MORECORE_SIZE is the minimum mmap size argument to use if
  sbrk fails, and mmap is used as a backup. 
  - The value must be a multiple of the page size.
  - This backup strategy generally applies only when systems have 
    "holes" in address space, so sbrk cannot perform contiguous 
    expansion, but there is still space available on system.
  - On systems for which this is known to be useful (i.e. most linux
    kernels), this occurs only when programs allocate huge amounts of
    memory.
  - Between this, and the fact that mmap regions tend to be
    limited, the size should be large, to avoid too many mmap calls 
    and thus avoid running out of kernel resources. 
*/
#ifndef MMAP_AS_MORECORE_SIZE
#define MMAP_AS_MORECORE_SIZE (1024 * 1024)
#endif

/*
  Define HAVE_MREMAP to make realloc() use mremap() to re-allocate
  large blocks.
*/
#ifndef HAVE_MREMAP
#define HAVE_MREMAP 0
#endif

/*
  This version of malloc supports the standard SVID/XPG mallinfo
  routine that returns a struct containing usage properties and
  statistics.
  - It should work on any SVID/XPG compliant system that has a 
    /usr/include/malloc.h defining struct mallinfo.
  - If you'd like to install such a thing yourself, cut out the 
    preliminary declarations as described above and below and 
    save them in a malloc.h file. But there's no compelling 
    reason to bother to do this.)

  - The main declaration needed is the mallinfo struct that is 
    returned (by-copy) by mallinfo().
  - The SVID/XPG malloinfo struct contains a bunch of fields 
    that are not even meaningful in this version of malloc. These 
    fields are are instead filled by mallinfo() with other numbers 
    that might be of interest.
*/


/* ---------- description of public routines ------------ */

#if IS_IN (libc)
/*
  malloc(size_t n)
  - Returns a pointer to a newly allocated chunk of at least n bytes, 
    or null if no space is available. 
  - On failure, errno is set to ENOMEM on ANSI C systems.

  If n is zero, malloc returns a minimum-sized chunk. (The minimum
  size is 16 bytes on most 32bit systems, and 24 or 32 bytes on 64bit
  systems.)
  - On most systems, size_t is an unsigned type, so calls with 
    negative arguments are interpreted as requests for huge amounts
    of space, which will often fail.
  - The maximum supported value of n differs across systems, but is in 
    all cases less than the maximum representable value of a size_t.
*/
void *__libc_malloc (size_t);
libc_hidden_proto (__libc_malloc)

static void *__libc_calloc2 (size_t);
static void *__libc_malloc2 (size_t);

/*
  free(void* p)
  Releases the chunk of memory pointed to by p, that had been previously
  allocated using malloc or a related routine such as realloc.
  - It has no effect if p is null.
  - It can have arbitrary (i.e., bad!) effects if p has already been freed.

  Unless disabled (using mallopt), freeing very large spaces will when 
    possible, automatically trigger operations that give back unused 
    memory to the system, thus reducing program footprint.
*/
void __libc_free(void*);
libc_hidden_proto (__libc_free)

/*
  calloc(size_t n_elements, size_t element_size);
  Returns a pointer to (n_elements * element_size) bytes, 
    with all locations set to zero.
*/
void*  __libc_calloc(size_t, size_t);

/*
  realloc(void* p, size_t n)
  - Returns a pointer to a chunk of size n that contains the same data
    as does chunk p up to the minimum of (n, p's size) bytes, or null
    if no space is available.

  - The returned pointer may or may not be the same as p.
  - The algorithm prefers extending p when possible, otherwise it 
    employs the equivalent of a malloc-copy-free sequence.

  - If p is null, realloc is equivalent to malloc.
  - If space is not available, realloc returns null, errno is set 
    (if on ANSI) and p is NOT freed.

  - If n is for fewer bytes than already held by p, the newly unused
    space is lopped off and freed if possible. Unless 
    REALLOC_ZERO_BYTES_FREES is set, realloc with a size argument of
    zero (re)allocates a minimum-sized chunk.

  - Large chunks that were internally obtained via mmap will always be
    grown using malloc-copy-free sequences unless the system supports
    MREMAP (currently only linux).

  The old unix realloc convention of allowing the last-free'd chunk
  to be used as an argument to realloc is not supported.
*/
void*  __libc_realloc(void*, size_t);
libc_hidden_proto (__libc_realloc)

/*
  memalign(size_t alignment, size_t n);
  - Returns a pointer to a newly allocated chunk of n bytes, aligned
  in accord with the alignment argument.

  - The alignment argument should be a power of two. If the argument 
    is not a power of two, the nearest greater power is used.
  - 8-byte alignment is guaranteed by normal malloc calls, so don't
  bother calling memalign with an argument of 8 or less.

  Overreliance on memalign is a sure way to fragment space.
*/
void*  __libc_memalign(size_t, size_t);
libc_hidden_proto (__libc_memalign)

/*
  valloc(size_t n);
  Equivalent to memalign(pagesize, n), where pagesize is the page
  size of the system. If the pagesize is unknown, 4096 is used.
*/
void*  __libc_valloc(size_t);


/*
  mallinfo()
  Returns (by copy) a struct containing various summary statistics:

  arena:     current total non-mmapped bytes allocated from system
  ordblks:   the number of free chunks
  hblks:     current number of mmapped regions
  hblkhd:    total bytes held in mmapped regions
  usmblks:   always 0
  uordblks:  current total allocated space (normal or mmapped)
  fordblks:  total free space
  keepcost:  the maximum number of bytes that could ideally be released
              back to system via malloc_trim. ("ideally" means that it 
              ignores page restrictions etc.)

  Because these fields are ints, but internal bookkeeping may
  be kept as longs, the reported values may wrap around zero and
  thus be inaccurate.
*/
struct mallinfo2 __libc_mallinfo2(void);
libc_hidden_proto (__libc_mallinfo2)

struct mallinfo __libc_mallinfo(void);


/*
  pvalloc(size_t n);
  Equivalent to valloc(minimum-page-that-holds(n)), that is,
  round up n to nearest pagesize.
 */
void*  __libc_pvalloc(size_t);

/*
  malloc_trim(size_t pad);

  If possible, gives memory back to the system (via negative
  arguments to sbrk) if there is unused memory at the `high' end of
  the malloc pool.
  - It can be called after freeing large blocks of memory to 
    potentially reduce the system-level memory requirements of a 
    program.
  - However, it cannot guarantee to reduce memory. Under some 
    allocation patterns, some large free blocks of memory will be
    locked between two used chunks, so they cannot be given back to
    the system.

  The `pad' argument to malloc_trim represents the amount of free
  trailing space to leave untrimmed.
  - If it is zero, only the minimum amount of memory to maintain 
    the internal data structures will be left (one page or less).
  - Non-zero arguments can be supplied to maintain enough trailing 
    space to service future expected allocations without having to 
    re-obtain memory from the system.

  Malloc_trim returns 1 if it actually released any memory, else 0.
  On systems that do not support "negative sbrks", it will always
  return 0.
*/
int __malloc_trim(size_t);

/*
  malloc_usable_size(void* p);

  - Returns the number of bytes you can actually use in an allocated 
  chunk, which may be more than you requested (although often not) 
  due to alignment and minimum size constraints.
  - You can use this many bytes without worrying about overwriting 
  other allocated objects. This is not a particularly great
  programming practice.
  
  malloc_usable_size can be more useful in debugging and assertions, 
  for example:

  p = malloc(n);
  assert(malloc_usable_size(p) >= 256);

*/
size_t __malloc_usable_size(void*);

/*
  malloc_stats();
  Prints on stderr
    -> the amount of space obtained from the system (both via sbrk 
       and mmap), 
    -> the maximum amount (which may be more than current if 
       malloc_trim and/or munmap got called), and
    -> the current number of bytes allocated via malloc (realloc, etc) 
       but not yet freed. Note that this is the number of bytes 
       allocated, not the number requested. It will be larger than 
       the number requested because of alignment and bookkeeping 
       overhead. Because it includes alignment wastage as being in use,
       this figure may be greater than zero even when no user-level 
       chunks are allocated.

  The reported current and maximum system memory can be inaccurate if
  a program makes other calls to system memory allocation functions
  (normally sbrk) outside of malloc.

  malloc_stats prints only the most commonly interesting statistics.
  More information can be obtained by calling mallinfo.
*/
void __malloc_stats(void);

/*
  posix_memalign(void **memptr, size_t alignment, size_t size);

  POSIX wrapper like memalign(), checking for validity of size.
*/
int __posix_memalign(void **, size_t, size_t);
#endif /* IS_IN (libc) */


/*
  mallopt(int parameter_number, int parameter_value)
  -: Sets tunable parameters
  
  - The format is to provide a (parameter-number, parameter-value) pair.
    mallopt then sets the corresponding parameter to the argument value 
    if it can (i.e., so long as the value is meaningful), and returns 1 
    if successful else 0.
  - SVID/XPG/ANSI defines four standard param numbers for mallopt,
    normally defined in malloc.h. These params (M_MXFAST, M_NLBLKS, M_GRAIN,
    M_KEEP) don't apply to our malloc, so setting them has no effect. But this
    malloc also supports four other options in mallopt. See below for details.
  - Briefly, supported parameters are as follows (listed defaults are for
    "typical" configurations).

  Symbol            param#    default    allowed param values
  M_MXFAST            1            64        0-80  (deprecated)
  M_TRIM_THRESHOLD   -1      128*1024        any   (-1U disables trimming)
  M_TOP_PAD          -2             0        any
  M_MMAP_THRESHOLD   -3      128*1024        any   (or 0 if no MMAP support)
  M_MMAP_MAX         -4         65536        any   (0 disables use of mmap)
*/
int __libc_mallopt(int, int);
#if IS_IN (libc)
libc_hidden_proto (__libc_mallopt)
#endif


/* mallopt tuning options */

/* M_TRIM_THRESHOLD
  
  It is the maximum amount of unused top-most memory to keep 
  before releasing via malloc_trim in free().

  Automatic trimming is mainly useful in long-lived programs.
  - Because trimming via sbrk can be slow on some systems, and 
    can sometimes be wasteful (in cases where programs immediately
    afterward allocate more large chunks) the value should be high
    enough so that your overall system performance would improve by
    releasing this much memory.

  The trim threshold and the mmap control parameters (see below)
  can be traded off with one another.
  - Trimming and mmapping are two different ways of releasing 
    unused memory back to the system. Between these two, it is 
    often possible to keep system-level demands of a long-lived 
    program down to a bare minimum. 
  - For example, in one test suite of sessions measuring the 
    XF86 X server on Linux, using a trim threshold of 128K and a
    mmap threshold of 192K led to near-minimal long term resource
    consumption.

  If you are using this malloc in a long-lived program, it should
  pay to experiment with these values.
  - As a rough guide, you might set to a value close to the average 
    size of a process (program) running on your system. Releasing 
    this much memory would allow such a process to run in memory.
  - Generally, it's worth it to tune for trimming rather than 
    memory mapping when a program undergoes phases where several 
    large chunks are allocated and released in ways that can reuse 
    each other's storage, perhaps mixed with phases where there are 
    no such chunks at all. And in well-behaved long-lived programs,
    controlling release of large blocks via trimming versus mapping
    is usually faster.

  However, in most programs, these parameters serve mainly as
  protection against the system-level effects of carrying around
  massive amounts of unneeded memory.
  - Since frequent calls to sbrk, mmap, and munmap otherwise 
    degrade performance, the default parameters are set to 
    relatively high values that serve only as safeguards.

  The trim value must be greater than page size to have any useful
  effect.
  - To disable trimming completely, you can set to (unsigned long)(-1)

  You can force an attempted trim by calling malloc_trim.

  Also, trimming is not generally possible in cases where the main 
  arena is obtained via mmap.

  Note that the trick some people use of mallocing a huge space and
  then freeing it at program startup, in an attempt to reserve system
  memory, doesn't have the intended effect under automatic trimming,
  since that memory will immediately be returned to the system.
*/
#define M_TRIM_THRESHOLD    -1

#ifndef DEFAULT_TRIM_THRESHOLD
#define DEFAULT_TRIM_THRESHOLD (128 * 1024)
#endif

/*
  M_TOP_PAD is the amount of extra `padding' space to allocate or
  retain whenever sbrk is called. It is used in two ways internally:
  - When sbrk is called to extend the top of the arena to satisfy a 
    new malloc request, this much padding is added to the sbrk request.
  - When malloc_trim is called automatically from free(), it is used 
    as the `pad' argument.

  In both cases, the actual amount of padding is rounded so that the 
  end of the arena is always a system page boundary.

  The main reason for using padding is to avoid calling sbrk so often. 
  - Having even a small pad greatly reduces the likelihood that nearly 
    every malloc request during program start-up (or after trimming) 
    will invoke sbrk, which needlessly wastes time.

  Automatic rounding-up to page-size units is normally sufficient
  to avoid measurable overhead, so the default is 0.
  - However, in systems where sbrk is relatively slow, it can pay 
    to increase this value, at the expense of carrying around more 
    memory than the program needs.
*/
#define M_TOP_PAD    -2

#ifndef DEFAULT_TOP_PAD
#define DEFAULT_TOP_PAD    (0)
#endif

/*
  MMAP_THRESHOLD_MAX and _MIN are the bounds on the dynamically
  adjusted MMAP_THRESHOLD.
*/
#ifndef DEFAULT_MMAP_THRESHOLD_MIN
#define DEFAULT_MMAP_THRESHOLD_MIN (128 * 1024)
#endif

/*
  For 32-bit platforms, we cannot increase the maximum mmap
  threshold much because it is also the minimum value for the
  maximum heap size and its alignment.
  
  Going above 512k (i.e., 1M for new heaps) wastes too much 
  address space.
*/
#ifndef DEFAULT_MMAP_THRESHOLD_MAX
# if __WORDSIZE == 32
#  define DEFAULT_MMAP_THRESHOLD_MAX (512 * 1024)
# else
#  define DEFAULT_MMAP_THRESHOLD_MAX (4 * 1024 * 1024 * sizeof(long))
# endif
#endif

/*
  M_MMAP_THRESHOLD is the request size threshold for using mmap()
  to service a request.
  - Requests of at least this size that cannot be allocated using 
    already-existing space will be serviced via mmap. (If enough 
    normal freed space already exists it is used instead.)

  A request serviced through mmap is never reused by any
  other request (at least not directly; the system may just so
  happen to remap successive requests to the same locations).

  Using mmap segregates relatively large chunks of memory so that
  they can be individually obtained and released from the host
  system. Segregating space this way has the benefits that:

   1. Mmapped space can ALWAYS be individually released back to the 
      system, which helps keep the system level memory demands of a 
      long-lived program low.
   2. Mapped memory can never become `locked` between other chunks, 
      as can happen with normally allocated chunks, which means that 
      even trimming via malloc_trim would not release them.
   3. On some systems with "holes" in address spaces, mmap can obtain
      memory that sbrk cannot.

  However, it has the disadvantages that:

   1. The space cannot be reclaimed, consolidated, and then used to 
      service later requests, as happens with normal chunks.
   2. It can lead to more wastage because of mmap page alignment
      requirements.
   3. It causes malloc performance to be more dependent on the host
      system memory management support routines which may vary in
      implementation quality and may impose arbitrary limitations.
      Generally, servicing a request via normal malloc steps is 
      faster than going through a system's mmap.

  The advantages of mmap nearly always outweigh disadvantages for
  "large" chunks, but the value of "large" varies across systems. The
  default is an empirically derived value that works well in most
  systems.


  Update in 2006:
  The above was written in 2001. Since then the world has changed a lot.
  Memory got bigger. Applications got bigger. The virtual address space
  layout in 32 bit linux changed.

  In the new situation, brk() and mmap space is shared and there are no
  artificial limits on brk size imposed by the kernel. What is more,
  applications have started using transient allocations larger than the
  128Kb as was imagined in 2001.

  The price for mmap is also high now; each time glibc mmaps from the
  kernel, the kernel is forced to zero out the memory it gives to the
  application.
  - Zeroing memory is expensive and eats a lot of cache and memory 
  bandwidth. This has nothing to do with the efficiency of the virtual 
  memory system, by doing mmap the kernel just has no choice but to zero.

  In 2001, the kernel had a maximum size for brk() which was about 800
  megabytes on 32 bit x86, at that point brk() would hit the first
  mmaped shared libraries and couldn't expand anymore.
  - With current 2.6 kernels, the VA space layout is different and brk()
    and mmap both can span the entire heap at will.

  Rather than using a static threshold for the brk/mmap tradeoff, we're
  now using a simple dynamic one. The goal is still to avoid 
  fragmentation. The old goals we kept are:
    1) try to get the long lived large allocations to use mmap()
    2) really large allocations should always use mmap()

    and we're adding now:
    3) transient allocations should use brk() to avoid forcing the kernel
       having to zero memory over and over again

  The implementation works with a sliding threshold, which is by default
  limited to go between 128Kb and 32Mb (64Mb for 64-bit machines) and 
  starts out at 128Kb as per the 2001 default.

  This allows us to satisfy requirement under the assumption that long
  lived allocations are made early in the process' lifespan, before it
  has started doing dynamic allocations of the same size (which will
  increase the threshold).
  The upperbound on the threshold satisfies requirement.

  The threshold goes up in value when the application frees memory that was
  allocated with the mmap allocator.
  - The idea is that once the application starts freeing memory of a certain 
    size, it's highly probable that this is a size the application uses for 
    transient allocations.
  - This estimator is there to satisfy the new third requirement.
*/
#define M_MMAP_THRESHOLD      -3

#ifndef DEFAULT_MMAP_THRESHOLD
#define DEFAULT_MMAP_THRESHOLD DEFAULT_MMAP_THRESHOLD_MIN
#endif

/*
  M_MMAP_MAX is the maximum number of requests to simultaneously
  service using mmap.
  - This parameter exists because some systems have a limited 
    number of internal tables for use by mmap, and using more 
    than a few of them may degrade performance.

  The default is set to a value that serves only as a safeguard.
  Setting to 0 disables use of mmap for servicing large requests.
*/
#define M_MMAP_MAX    (-4)

#ifndef DEFAULT_MMAP_MAX
#define DEFAULT_MMAP_MAX       (65536)
#endif


#include <malloc.h>

#ifndef RETURN_ADDRESS
#define RETURN_ADDRESS(X_) (NULL)
#endif

/* Forward declarations.  */
struct malloc_chunk;
typedef struct malloc_chunk* mchunkptr;

/* Internal routines.  */

static void* _int_malloc(mstate, size_t);

static void  _int_free_chunk(mstate, mchunkptr, INTERNAL_SIZE_T, int);

static void  _int_free_merge_chunk(mstate, mchunkptr, INTERNAL_SIZE_T);

static INTERNAL_SIZE_T _int_free_create_chunk(
  mstate, mchunkptr, 
  INTERNAL_SIZE_T,
	mchunkptr, INTERNAL_SIZE_T
);

static void  _int_free_maybe_trim(mstate, INTERNAL_SIZE_T);

static void* _int_realloc(mstate, mchunkptr, INTERNAL_SIZE_T, INTERNAL_SIZE_T);

static void* _int_memalign(mstate, size_t, size_t);

#if IS_IN(libc)
static void* _mid_memalign(size_t, size_t);
#endif

#if USE_TCACHE
static void malloc_printerr_tail(const char *str);
#endif

static void malloc_printerr(const char *str) __attribute__ ((noreturn));

static void munmap_chunk(mchunkptr p);

#if HAVE_MREMAP
static mchunkptr mremap_chunk(mchunkptr p, size_t new_size);
#endif

static size_t musable (void *mem);


/* ------------------ MMAP support ------------------  */

#include <fcntl.h>
#include <sys/mman.h>

#if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
# define MAP_ANONYMOUS MAP_ANON
#endif

#define MMAP(addr, size, prot, flags) \
  __mmap((addr), (size), (prot), (flags)|MAP_ANONYMOUS|MAP_PRIVATE, -1, 0)


/*
  `ATTEMPT_TRIMMING_THRESHOLD` is the size of a chunk in free() 
  that may attempt trimming of an arena's heap.
  - This is a heuristic, so the exact value should not matter too much.
  - It is defined at half the default trim threshold as a compromise 
    heuristic to only attempt trimming if it is likely to release a 
    significant amount of memory.
*/
#define ATTEMPT_TRIMMING_THRESHOLD    65536UL

/*
  NONCONTIGUOUS_BIT indicates that MORECORE does not return contiguous
  regions.
  - Otherwise, contiguity is exploited in merging together, when 
    possible, results from consecutive MORECORE calls.

  The initial value comes from MORECORE_CONTIGUOUS, but is changed 
  dynamically if mmap is ever used as an sbrk substitute.
*/
#define NONCONTIGUOUS_BIT    2U
