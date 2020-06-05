/*

TGC: Tiny incremental mark & sweep Garbage Collector.

//////////////////////////////////////////////////////////////////////////

Copyright (C) 2018 soniced@sina.com

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#pragma once

//#define TGC_MULTI_THREADED

#include <cassert>
#include <memory>
#include <set>
#include <typeinfo>
#include <vector>

// for STL wrappers
#include <deque>
#include <list>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace tgc2 {
namespace details {

using namespace std;

using std::size_t;

class ObjMeta;
class ClassMeta;
class PtrBase;
class IPtrEnumerator;

//////////////////////////////////////////////////////////////////////////

namespace helper {

template <typename T>
struct list_slot {
  T* prev;
  T* next;
};

template <typename T>
struct base_iterator {
  T* ptr;
  base_iterator(T* p) : ptr{p} {}
  base_iterator(const base_iterator& r) : ptr{r.ptr} {}
  T* operator*() { return ptr; }
  T* operator->() { return ptr; }
  bool operator!=(const base_iterator& r) const { return ptr != r.ptr; }
};

template <typename T, list_slot<T> T::*slot>
class list {
  T* m_first = nullptr;
  T* m_last = nullptr;
  size_t m_size = 0;

 public:
  struct iterator : base_iterator<T> {
    using base_iterator<T>::base_iterator;
    iterator& operator++() {
      this->ptr = next(this->ptr);
      return *this;
    }
  };

  static T*& prev(T* p) { return (p->*slot).prev; }
  static T*& next(T* p) { return (p->*slot).next; }

  void push_back(T* v) {
    if (m_last)
      next(m_last) = v;
    else
      m_first = v;
    prev(v) = m_last;
    next(v) = nullptr;
    m_last = v;
    m_size++;
  }

  void remove(T* v) {
    if (v == m_first)
      m_first = next(m_first);
    else if (auto p = prev(v))
      next(p) = next(v);

    if (v == m_last)
      m_last = prev(m_last);
    else if (auto n = next(v))
      prev(n) = prev(v);
    m_size--;
  }

  iterator erase(iterator it) {
    auto* o = *it;
    auto n = next(o);
    remove(o);
    return n;
  }

  iterator begin() { return {m_first}; }
  iterator end() { return {nullptr}; }
  size_t size() { return m_size; }
};
}  // namespace helper

//////////////////////////////////////////////////////////////////////////

class ObjMeta {
 public:
  enum class Color : unsigned char { White, Black };

  ClassMeta* klass = nullptr;
  size_t arrayLength = 0;
  unsigned short refCntFromRoot = 0;
  Color color : 2;
  unsigned char scanCountInNewGen : 3;
  bool isOld : 1;
  helper::list_slot<ObjMeta> gen;

  ObjMeta(ClassMeta* c, char* o, size_t n)
      : klass(c),
        arrayLength(n),
        scanCountInNewGen(0),
        color(Color::White),
        isOld(false) {}
  ~ObjMeta() {
    if (arrayLength)
      destroy();
  }
  void operator delete(void* c);
  bool isRoot() const { return refCntFromRoot > 0; }
  bool containsPtr(char* p);
  char* objPtr() const { return (char*)this + sizeof(ObjMeta); }
  void destroy();
};

static_assert(sizeof(ObjMeta) <= sizeof(void*) * 5,
              "too large for small allocation");

//////////////////////////////////////////////////////////////////////////

class IPtrEnumerator {
 public:
  virtual ~IPtrEnumerator() {}
  virtual PtrBase* getNext() = 0;

  static char buf[255];
  void* operator new(size_t) { return buf; }
  void operator delete(void*) {}
};

class ObjPtrEnumerator : public IPtrEnumerator {
  size_t subPtrIdx = 0, arrayElemIdx = 0;
  ObjMeta* meta = nullptr;

 public:
  ObjPtrEnumerator(ObjMeta* m) : meta(m) {}
  PtrBase* getNext() override;
};

template <typename T>
struct PtrEnumerator : ObjPtrEnumerator {
  using ObjPtrEnumerator::ObjPtrEnumerator;
};

//////////////////////////////////////////////////////////////////////////

class ClassMeta {
 public:
  enum class MemRequest { Dctor, NewPtrEnumerator };
  using MemHandler = void* (*)(ClassMeta* cls, MemRequest r, void* param);
  using OffsetType = unsigned short;
  using Alloc = void* (*)(size_t size);
  using Dealloc = void (*)(void* ptr);

  MemHandler memHandler = nullptr;
  vector<OffsetType>* subPtrOffsets = nullptr;
  unsigned short size = 0;

  static int isCreatingObj;
  static Alloc alloc;
  static Dealloc dealloc;

  ClassMeta(MemHandler h, unsigned short sz) : memHandler(h), size(sz) {}
  ~ClassMeta() { delete subPtrOffsets; }
  ObjMeta* newMeta(size_t objCnt);
  void registerSubPtr(ObjMeta* owner, PtrBase* p);
  void endNewMeta(ObjMeta* meta, bool failed);
  IPtrEnumerator* enumPtrs(ObjMeta* m) {
    return (IPtrEnumerator*)memHandler(this, MemRequest::NewPtrEnumerator, m);
  }

  template <typename T>
  static ClassMeta* get() {
    return &Holder<T>::inst;
  }

  char* callAlloc(size_t sz) { return alloc ? (char*)alloc(sz) : new char[sz]; }
  void callDealloc(void* p) { dealloc ? dealloc(p) : delete[](char*)(p); }

 private:
  template <typename T>
  struct Holder {
    static void* MemHandler(ClassMeta* cls, MemRequest r, void* param) {
      switch (r) {
        case MemRequest::Dctor: {
          auto meta = (ObjMeta*)param;
          auto p = (T*)meta->objPtr();
          for (size_t i = 0; i < meta->arrayLength; i++, p++) {
            p->~T();
          }
        } break;
        case MemRequest::NewPtrEnumerator: {
          auto meta = (ObjMeta*)param;
          return new PtrEnumerator<T>(meta);
        } break;
      }
      return nullptr;
    }

    static ClassMeta inst;
  };
};

template <typename T>
ClassMeta ClassMeta::Holder<T>::inst{MemHandler, sizeof(T)};

static_assert(sizeof(ClassMeta) <= sizeof(void*) * 3,
              "too large for small objects");

//////////////////////////////////////////////////////////////////////////

class PtrBase {
  friend class Collector;
  friend class ClassMeta;

 public:
  ObjMeta* getMeta() { return meta; }

 protected:
  PtrBase();
  PtrBase(void* obj);
  ~PtrBase();

  void writeBarrier();
  bool isRoot() const { return !owner; }

 protected:
  ObjMeta* meta = nullptr;
  ObjMeta* owner = nullptr;
};

template <typename T>
class GcPtr : public PtrBase {
 public:
  using pointee = T;
  using element_type = T;  // compatible with std::shared_ptr

  template <typename U>
  friend class GcPtr;

 public:
  // Constructors

  GcPtr() {}
  GcPtr(ObjMeta* m) { reset(m); }
  explicit GcPtr(T* obj) : PtrBase(obj) {}
  template <typename U>
  GcPtr(const GcPtr<U>& r) {
    reset(r.meta);
  }
  GcPtr(const GcPtr& r) { reset(r.meta); }
  GcPtr(GcPtr&& r) {
    reset(r.meta);
    r = nullptr;
  }

  // Operators

  template <typename U>
  GcPtr& operator=(const GcPtr<U>& r) {
    reset(r.meta);
    return *this;
  }
  GcPtr& operator=(const GcPtr& r) {
    reset(r.meta);
    return *this;
  }
  GcPtr& operator=(GcPtr&& r) {
    reset(r.meta);
    r = nullptr;
    return *this;
  }
  T* operator->() const { return ptr(); }
  T& operator*() const { return *ptr(); }
  explicit operator bool() const { return meta; }
  bool operator==(const GcPtr& r) const { return ptr() == r.ptr(); }
  bool operator!=(const GcPtr& r) const { return ptr() != r.ptr(); }
  GcPtr& operator=(T* ptr) = delete;
  GcPtr& operator=(nullptr_t) {
    meta = 0;
    return *this;
  }
  bool operator<(const GcPtr& r) const { return *ptr() < *r.ptr(); }

  // Methods

  void reset(ObjMeta* n) {
    meta = n;
    writeBarrier();
  }

 protected:
  T* ptr() const { return meta ? (T*)meta->objPtr() : nullptr; }
};

static_assert(sizeof(GcPtr<int>) <= sizeof(void*) * 2,
              "too large to pass by value");

template <typename T>
class gc : public GcPtr<T> {
  using base = GcPtr<T>;

 public:
  using GcPtr<T>::GcPtr;
  gc() {}
  gc(nullptr_t) {}
  gc(ObjMeta* o) : base(o) {}
  explicit gc(T* o) : base(o) {}
};

#define TGC_DECL_AUTO_BOX(T, GcAliasName)                    \
  template <>                                                \
  class details::gc<T> : public details::GcPtr<T> {          \
   public:                                                   \
    using GcPtr<T>::GcPtr;                                   \
    gc(const T& i) : GcPtr(details::gc_new_meta<T>(1, i)) {} \
    gc() {}                                                  \
    gc(nullptr_t) {}                                         \
    operator T&() { return operator*(); }                    \
    operator T&() const { return operator*(); }              \
  };                                                         \
  using GcAliasName = gc<T>;

//////////////////////////////////////////////////////////////////////////

class Collector {
  friend class ClassMeta;
  friend class PtrBase;

  using MetaSet = helper::list<ObjMeta, &ObjMeta::gen>;
  // using MetaSet = list<ObjMeta*>;

  unordered_set<PtrBase*> intergenerationalPtrs, delayIntergenerationalPtrs;
  MetaSet newGen, oldGen;
  vector<ObjMeta*> temp;
  vector<tuple<PtrBase*, ObjMeta*>> unrefs;
  vector<ObjMeta*> creatingObjs;

  int freeObjCntOfPrevGc;

  int newGenGcCount = 0;
  int fullGcCount = 0;

  int allocCounter = 0;
  int scanCountToOldGen = 2;
  int newGenObjCntToGc = 1024 * 10;
  size_t oldGenObjCntToFullGc = newGenObjCntToGc * 10;
  bool trace = false;
  bool full = false;

  static Collector* inst;

 public:
  static Collector* get();
  void fullCollect();
  void collect();
  void dumpStats();
  void reserve(int sz);
  void resetCounters() { newGenGcCount = fullGcCount = 0; }

 private:
  Collector();
  ~Collector();

  void sweep(MetaSet& gen);
  void promote(ObjMeta* meta);
  ObjMeta* globalFindOwnerMeta(void* obj);
  void tryRegisterToClass(PtrBase* p);
  void handleUnrefs();
  void handleDelayIntergenerationalPtrs();
  void mark(ObjMeta* meta);
  void fixOwner(ObjMeta* meta);
  void collectNewGen();
  void addMeta(ObjMeta* meta);
};

inline void gc_collect() {
  Collector::get()->collect();
}

inline Collector* gc_collector() {
  return Collector::get();
}

template <typename T, typename... Args>
ObjMeta* gc_new_meta(size_t len, Args&&... args) {
  auto* cls = ClassMeta::get<T>();
  auto* meta = cls->newMeta(len);

  size_t i = 0;
  auto* p = (T*)meta->objPtr();
  try {
    for (; i < len; i++, p++)
      new (p) T(forward<Args>(args)...);
  } catch (...) {
    for (auto j = i; j > 0; j--, p--) {
      p->~T();
    }
    cls->endNewMeta(meta, true);
    throw;
  }

  cls->endNewMeta(meta, false);
  return meta;
}

template <typename T>
void gc_delete(gc<T>& c) {
  if (c) {
    c.getMeta()->destroy();
    c = nullptr;
  }
}

// used as shared_from_this
template <typename T>
gc<T> gc_from(T* o) {
  return gc<T>(o);
}

// used as std::shared_ptr
template <typename To, typename From>
gc<To> gc_static_pointer_cast(gc<From>& from) {
  return from;
}

// used as std::shared_ptr
template <typename To, typename From>
gc<To> gc_dynamic_pointer_cast(gc<From>& from) {
  gc<To> r;
  r.reset(dynamic_cast<To*>(from.operator->()), from.getMeta());
  return r;
}

template <typename T, typename... Args>
gc<T> gc_new(Args&&... args) {
  return gc_new_meta<T>(1, forward<Args>(args)...);
}

template <typename T, typename... Args>
gc<T> gc_new_array(size_t len, Args&&... args) {
  return gc_new_meta<T>(len, forward<Args>(args)...);
}

//////////////////////////////////////////////////////////////////////////
/// Function

template <typename T>
class gc_function;

template <typename R, typename... A>
class gc_function<R(A...)> {
 public:
  gc_function() {}

  template <typename F>
  gc_function(F&& f) : callable(gc_new_meta<Imp<F>>(1, forward<F>(f))) {}

  template <typename F>
  gc_function& operator=(F&& f) {
    callable = gc_new_meta<Imp<F>>(1, forward<F>(f));
    return *this;
  }

  template <typename... U>
  R operator()(U&&... a) const {
    return callable->call(forward<U>(a)...);
  }

  explicit operator bool() const { return (bool)callable; }
  bool operator==(const gc_function& r) const { return callable == r.callable; }
  bool operator!=(const gc_function& r) const { return callable != r.callable; }

 private:
  struct Callable {
    virtual ~Callable() {}
    virtual R call(A... a) = 0;
  };

  template <typename F>
  struct Imp : Callable {
    F f;
    Imp(F&& ff) : f(ff) {}
    R call(A... a) override { return f(a...); }
  };

 private:
  gc<Callable> callable;
};

//////////////////////////////////////////////////////////////////////////
// Wrap STL Containers
//////////////////////////////////////////////////////////////////////////

template <typename C>
struct ContainerPtrEnumerator : IPtrEnumerator {
  C* o;
  typename C::iterator it;
  ContainerPtrEnumerator(ObjMeta* m) : o((C*)m->objPtr()), it(o->begin()) {}
  bool hasNext() { return it != o->end(); }
};

//////////////////////////////////////////////////////////////////////////
/// Vector
/// vector elements are not stored contiguously due to implementation
/// limitation. use gc_new_array for better performance.

template <typename T>
class gc_vector : public gc<vector<gc<T>>> {
 public:
  using gc<vector<gc<T>>>::gc;
  gc<T>& operator[](int idx) { return (*this->ptr())[idx]; }
};

template <typename T>
struct PtrEnumerator<vector<gc<T>>> : ContainerPtrEnumerator<vector<gc<T>>> {
  using ContainerPtrEnumerator<vector<gc<T>>>::ContainerPtrEnumerator;

  PtrBase* getNext() override {
    return this->hasNext() ? &*this->it++ : nullptr;
  }
};

template <typename T, typename... Args>
gc_vector<T> gc_new_vector(Args&&... args) {
  return gc_new_meta<vector<gc<T>>>(1, forward<Args>(args)...);
}

template <typename T>
void gc_delete(gc_vector<T>& p) {
  for (auto& i : *p) {
    gc_delete(i);
  }
  p->clear();
}

//////////////////////////////////////////////////////////////////////////
/// Deque

template <typename T>
class gc_deque : public gc<deque<gc<T>>> {
 public:
  using gc<deque<gc<T>>>::gc;
  gc<T>& operator[](int idx) { return (*this->ptr())[idx]; }
};

template <typename T>
struct PtrEnumerator<deque<gc<T>>> : ContainerPtrEnumerator<deque<gc<T>>> {
  using ContainerPtrEnumerator<deque<gc<T>>>::ContainerPtrEnumerator;

  const PtrBase* getNext() override { return &*this->it++; }
};

template <typename T, typename... Args>
gc_deque<T> gc_new_deque(Args&&... args) {
  return gc_new_meta<deque<gc<T>>>(1, forward<Args>(args)...);
}

template <typename T>
void gc_delete(gc_deque<T>& p) {
  for (auto& i : *p) {
    gc_delete(i);
  }
  p->clear();
}

//////////////////////////////////////////////////////////////////////////
/// List

template <typename T>
using gc_list = gc<list<gc<T>>>;

template <typename T>
struct PtrEnumerator<list<gc<T>>> : ContainerPtrEnumerator<list<gc<T>>> {
  using ContainerPtrEnumerator<list<gc<T>>>::ContainerPtrEnumerator;

  const PtrBase* getNext() override { return &*this->it++; }
};

template <typename T, typename... Args>
gc_list<T> gc_new_list(Args&&... args) {
  return gc_new_meta<list<gc<T>>>(1, forward<Args>(args)...);
}

template <typename T>
void gc_delete(gc_list<T>& p) {
  for (auto& i : *p) {
    gc_delete(i);
  }
  p->clear();
}

//////////////////////////////////////////////////////////////////////////
/// Map
/// TODO: NOT support using gc object as key...

template <typename K, typename V>
class gc_map : public gc<map<K, gc<V>>> {
 public:
  using gc<map<K, gc<V>>>::gc;
  gc<V>& operator[](const K& k) { return (*this->ptr())[k]; }
};

template <typename K, typename V>
struct PtrEnumerator<map<K, gc<V>>> : ContainerPtrEnumerator<map<K, gc<V>>> {
  using ContainerPtrEnumerator<map<K, gc<V>>>::ContainerPtrEnumerator;

  const PtrBase* getNext() override {
    auto* ret = &this->it->second;
    ++this->it;
    return ret;
  }
};

template <typename K, typename V, typename... Args>
gc_map<K, V> gc_new_map(Args&&... args) {
  return gc_new_meta<map<K, gc<V>>>(1, forward<Args>(args)...);
}

template <typename K, typename V>
void gc_delete(gc_map<K, V>& p) {
  for (auto& i : *p) {
    gc_delete(i->value);
  }
  p->clear();
}

//////////////////////////////////////////////////////////////////////////
/// HashMap
/// TODO: NOT support using gc object as key...

template <typename K, typename V>
class gc_unordered_map : public gc<unordered_map<K, gc<V>>> {
 public:
  using gc<unordered_map<K, gc<V>>>::gc;
  gc<V>& operator[](const K& k) { return (*this->ptr())[k]; }
};

template <typename K, typename V>
struct PtrEnumerator<unordered_map<K, gc<V>>>
    : ContainerPtrEnumerator<unordered_map<K, gc<V>>> {
  using ContainerPtrEnumerator<unordered_map<K, gc<V>>>::ContainerPtrEnumerator;

  const PtrBase* getNext() override {
    auto* ret = &this->it->second;
    ++this->it;
    return ret;
  }
};

template <typename K, typename V, typename... Args>
gc_unordered_map<K, V> gc_new_unordered_map(Args&&... args) {
  return gc_new_meta<unordered_map<K, gc<V>>>(1, forward<Args>(args)...);
}
template <typename K, typename V>
void gc_delete(gc_unordered_map<K, V>& p) {
  for (auto& i : *p) {
    gc_delete(i.second);
  }
  p->clear();
}

//////////////////////////////////////////////////////////////////////////
/// Set

template <typename V>
using gc_set = gc<set<gc<V>>>;

template <typename V>
struct PtrEnumerator<set<gc<V>>> : ContainerPtrEnumerator<set<gc<V>>> {
  using ContainerPtrEnumerator<set<gc<V>>>::ContainerPtrEnumerator;

  const PtrBase* getNext() override { return &*this->it++; }
};

template <typename V, typename... Args>
gc_set<V> gc_new_set(Args&&... args) {
  return gc_new_meta<set<gc<V>>>(1, forward<Args>(args)...);
}

template <typename T>
void gc_delete(gc_set<T>& p) {
  for (auto i : *p) {
    gc_delete(i);
  }
  p->clear();
}

}  // namespace details

//////////////////////////////////////////////////////////////////////////
// Public APIs

using details::gc;
using details::gc_collect;
using details::gc_collector;
using details::gc_dynamic_pointer_cast;
using details::gc_from;
using details::gc_function;
using details::gc_new;
using details::gc_new_array;
using details::gc_static_pointer_cast;

using details::gc_new_vector;
using details::gc_vector;

using details::gc_deque;
using details::gc_new_deque;

using details::gc_list;
using details::gc_new_list;

using details::gc_map;
using details::gc_new_map;

using details::gc_new_set;
using details::gc_set;

using details::gc_new_unordered_map;
using details::gc_unordered_map;

TGC_DECL_AUTO_BOX(char, gc_char);
TGC_DECL_AUTO_BOX(unsigned char, gc_uchar);
TGC_DECL_AUTO_BOX(short, gc_short);
TGC_DECL_AUTO_BOX(unsigned short, gc_ushort);
TGC_DECL_AUTO_BOX(int, gc_int);
TGC_DECL_AUTO_BOX(unsigned int, gc_uint);
TGC_DECL_AUTO_BOX(float, gc_float);
TGC_DECL_AUTO_BOX(double, gc_double);
TGC_DECL_AUTO_BOX(long, gc_long);
TGC_DECL_AUTO_BOX(unsigned long, gc_ulong);
TGC_DECL_AUTO_BOX(std::string, gc_string);

}  // namespace tgc2
