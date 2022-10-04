# TGC2

A tiny, precise, generational, mark & sweep, Garbage Collector for C++.

Fork of [crazybie/tgc2](https://github.com/crazybie/tgc2).

What is changed in this fork:
- CMake is used instead of Visual Studio project files,
- better documentation,
- add `clang-format` for automatic file formatting,
- added functions:
    - `gc_collector()->getStats()`: like `gc_collector()->dumpStats()` but returns a string with the most important information,
    - `gc_collector()->getAliveObjectsCount()`: returns the number of currently alive `gc` objects (not pointers) (that were created via `gc_new`),
    - `gc_collector()->getLastFreedObjectsCount()`: returns the number of last freed `gc` objects (not pointers) (since last `collect` call).

TODO:
- more usage documentation,
- use `Catch2` for tests and add more tests,
- add `clang-tidy`
- add `doxygen` and cover all code with the documentation,
- carefully merge everything into one `.hpp` file to make it a single-header library,
- API/architecture imrovements,
- add new features/fixes?

# Motivation
- Scenarios that shared_ptr can't solve, e.g. object dependencies are dynamically constructed with no chance to recognize the usage of shared & weak pointers.
- Try to make things simpler compared to shared_ptr and Oilpan, e.g. networking programs using callbacks for async io operations heavily.     
- A very good experiment to design a GC dedicated to the C++ language and see how the language features can help.    

# Highlights
- Non-intrusive
    - Use like shared_ptr.
    - Do not need to replace the global new operator.
    - Do not need to inherit from a common base.    
    - Can even work with shared_ptr.   
- Generational marking and sweeping
    - Can customize the trigger condtion of full gc.
    - Can manually delete the object to control the destruction order.
- Super lightweight    
    - Only one header & CPP file, easier to integrate.
    - No extra threads to collect garbage.    
- Support most of the containers of STL.        
- Cross-platform, no other dependencies, only dependent on STL.    
- Customization
    - It can work with other memory allocators and pool.
    - It can be extended to use your custom containers.    
- Precise.
    - Ensure no memory leaks as long as objects are correctly traced.

# Improvements compared to tgc1
- better throughputs by using generational algorithm
- gc pointers can be global variables
- fast gc pointer construction
- support continuous vector
- unified way to handle class and containers
- gc pointer has smaller size
- does not support multiple inheritance
- can customize the condition of a full garbage collection

# Multi-threading
The single-threaded version (enabled by default) should be much faster than the multi-threaded version because no locks are required at all. Please define `TGC_MULTI_THREADED` before including the library to enable the multi-threaded version like so:

```C++
#define TGC_MULTI_THREADED
#include "tgc2.h"
```

For the multi-threaded version, the collection function (`gc_collector()->collect()`) should be invoked from the main thread therefore the destructors can be triggered in the main thread as well.

# Containers

To make objects in proper tracing chain, **you must use GC wrappers of STL containers instead**, otherwise, memory leaks may occur. Example:

```C++
std::vector<gc<T>> myBadArray; // don't do that
gc_vector<T> myGoodArray = gc_new_array<T>(); // do that
```

Here is the list of STL wrappers for storing `gc` pointers:

- `gc_vector` for `std::vector`,
- `gc_map` for `std::map`,
- `gc_unordered_map` for `std::unordered_map`,
- `gc_deque` for `std::deque`,
- `gc_list` for `std::list`,
- `gc_set` for `std::set`,
- `gc_unordered_set` for `std::unordered_set`.

There is also a `std::function` wrapper `gc_function` if you want to capture `gc` pointers in it:

```C++
{
    auto pObj = tgc2::gc_new<int>(0);
    gc_function<void()> gc_callback = [pObj](){};
}

tgc2::gc_collector()->collect(); // cleaned everything up correctly
```

# Casting

- use `tgc2::gc_static_pointer_cast<To>(pFrom)` for `static_cast`,
- use `tgc2::gc_dynamic_pointer_cast<To>(pFrom)` for `dynamic_cast`, example:

```C++
gc<ParentClass> pParent = tgc2::gc_new<ParentClass>();
gc<ChildClass> pChild = tgc2::gc_dynamic_pointer_cast<ChildClass>(pParent);
// you have 2 GC pointers now
```

# Internals
- This collector uses the triple color, mark & sweep algorithm internally.    
- Pointers are constructed as roots by default unless detected as children of other object.
- Every class has a global meta-object keeping the necessary meta-information (e.g. class size and offsets of member pointers) used by GC, so programs using lambdas heavily may have some memory overhead. Besides, as the initialization order of global objects is not well defined, you should not use GC pointers as global variables too (there is an assert checking it).
- Construct & copy & modify GC pointers are slower than shared_ptr, much slower than raw pointers(Boehm GC).
    - Every GC pointer must register itself to the collector and unregister on destruction as well.
    - Since C++ does not support ref-qualified constructors, the gc_new returns a temporary GC pointer bringing in some meaningless overhead. Instead, using gc_new_meta can bypass the construction of the temporary making things a bit faster.
    - Member pointers offsets of one class are calculated and recorded at the first time of creating the instance of that class.
    - Modifying a GC pointer will trigger a GC color adjustment which may not be cheap as well.
- Each allocation has a few extra space overhead (size of two pointers at most), which is used for memory tracing.
- Marking & swapping should be much faster than Boehm GC, due to the deterministic pointer management, no scanning inside the memories at all, just iterating pointers registered in the GC.
- You can manually call gc_delete to trigger the destructor of an object and let the GC claim the memory automatically. Besides, double free is also safe.

# Setup (Build)

Prerequisites:
- compiler that supports C++17
- [CMake](https://cmake.org/download/)

First, clone this repository:

```
git clone https://github.com/Flone-dnb/tgc2.git
cd tgc2
git submodule update --init --recursive
```

Then, if you've never used CMake before:

Create a `build` directory next to this file, open created `build` directory and type `cmd` in Explorer's address bar. This will open up a console in which you need to type this:

```
cmake -DCMAKE_BUILD_TYPE=Debug .. // for debug mode
cmake -DCMAKE_BUILD_TYPE=Release .. // for release mode
```

This will generate project files that you will use for development.

# Update

To update this repository:

```
git pull
git submodule update --init --recursive
```

# Usage

Please see the tests in `tests` directory.

# References

- https://www.codeproject.com/Articles/938/A-garbage-collection-framework-for-C-Part-II.
- Boehn GC: https://github.com/ivmai/bdwgc/
- Oilpan GC: https://chromium.googlesource.com/chromium/src/+/master/third_party/blink/renderer/platform/heap/BlinkGCDesign.md#Threading-model
