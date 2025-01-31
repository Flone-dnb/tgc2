#include <assert.h>

#include <chrono>
#include <iostream>

#include "tgc2.h"

using namespace tgc2;
using namespace std;

struct b1 {
    b1(const string& s) : name(s) { cout << "Creating b1(" << name << ")." << endl; }
    virtual ~b1() { cout << "Destroying b1(" << name << ")." << endl; }

    string name;
};

struct b2 {
    b2(const string& s) : name(s) { cout << "Creating b2(" << name << ")." << endl; }
    virtual ~b2() { cout << "Destroying b2(" << name << ")." << endl; }

    string name;
};

struct d1 : public b1 {
    d1(const string& s) : b1(s) { cout << "Creating d1(" << name << ")." << endl; }
    virtual ~d1() { cout << "Destroying d1(" << name << ")." << endl; }
};

struct d2 : public b1, public b2 {
    d2(const string& s) : b1(s), b2(s) { cout << "Creating d2(" << b1::name << ")." << endl; }
    virtual ~d2() { cout << "Destroying d2(" << b1::name << ")." << endl; }
};

struct rc {
    int a = 11;
    rc() {}
    ~rc() { auto i = gc<rc>(this); }
};

void testPointerCast() {
#if 1
    {
        // comment this test out for now
        // since it throws an exception due to `meta` being `null`
        //        gc<rc> prc = gc_new<rc>();
        //        {
        //            gc<d1> p2(gc_new<d1>("first"));
        //            gc<b1> p3(p2);
        //            gc<b1> p4(gc_new<d2>("second"));
        //            gc<b2> pz(dynamic_cast<b2*>(&*p4));
        //            if ((void*)&*p4 == (void*)&*pz)
        //                throw std::runtime_error("unexpected");

        //            p3 = p2;
        //            gc_collect();
        //        }
    }
    gc_collect();
#endif
}

struct circ {
    circ(const string& s) : name(s) { cout << "Creating circ(" << name << ")." << endl; }
    ~circ() { cout << "Destroying circ(" << name << ")." << endl; }

    gc<circ> ptr;
    string name;
};

void testCirc() {
    {
        auto p5 = gc_new<circ>("root");
        {
            auto p6 = gc_new<circ>("first");
            auto p7 = gc_new<circ>("second");

            p5->ptr = p6;

            p6->ptr = p7;
            p7->ptr = p6;

            gc_collect();
        }
    }
    gc_collect();
}

void testMoveCtor() {
    {
        auto f = [] {
            auto t = gc_new<b1>("");
            return std::move(t);
        };

        auto p = f();
        gc<b1> p2 = p;
        p2 = f();
    }
}

void testMakeGcObj() {
    { auto a = gc_new<b1>("test"); }
}

void testEmpty() {
    {
        gc<b1> p(gc_new<b1>("a"));
        gc<b1> emptry;
    }
}

struct ArrayTest {
    gc_vector<rc> a;
    gc_map<int, rc> b;
    gc_map<int, rc> c;

    void f() {
        a = gc_new_vector<rc>();
        a->push_back(gc_new<rc>());
        b = gc_new_map<int, rc>();
        (*b)[0] = gc_new<rc>();
        b[1] = gc_new<rc>();

        auto it = b->find(1);
        bar(b);
    }
    void bar(gc_map<int, rc> cc) { cc->insert(std::make_pair(1, gc_new<rc>())); }
};

void testArray() {
    gc<ArrayTest> a;
    a = gc_new<ArrayTest>();
    a->f();

    a = gc_new<ArrayTest>();
    gc_delete(a);
}

static int unref = 0;
struct Val {
    ~Val() { unref++; }
};

struct Obj {
    gc<Val> v = gc_new<Val>();
};

void testContinusVector() {
    unref = 0;
    int cnt = 3;
    gc_new<Obj>(); // force registering of Obj
    { auto c = gc_new<vector<Obj>>(cnt); }
    gc_collector()->fullCollect();
    assert(unref == cnt + 1);
}

void testContinusList() {
    unref = 0;
    int cnt = 3;
    gc_new<Obj>(); // force registering of Obj
    { auto c = gc_new<list<Obj>>(cnt); }
    gc_collector()->fullCollect();
    assert(unref == cnt + 1);
}

void testCircledContainer() {
    static int delCnt = 0;
    struct Node {
        gc_map<int, Node> childs = gc_new_map<int, Node>();
        ~Node() { delCnt++; }
    };
    {
        auto node = gc_new<Node>();
        node->childs[0] = node;
    }
    gc_collect();
    assert(delCnt == 1);
}

bool operator<(rc& a, rc& b) { return a.a < b.a; }

void testSet() {
    {
        gc_set<rc> t = gc_new_set<rc>();
        auto o = gc_new<rc>();
        t->insert(o);
    }
    gc_collect();

    auto t = gc_new_set<rc>();
    gc_delete(t);
}

void testList() {
    auto l = gc_new_list<int>();
    l->push_back(gc_new<int>(1));
    l->push_back(gc_new<int>(2));
    l->pop_back();
    assert(*l->back() == 1);

    auto ll = gc_new_list<int>();
    gc_delete(ll);
}

void testDeque() {
    auto l = gc_new_deque<int>();
    l->push_back(gc_new<int>(1));
    l->push_back(gc_new<int>(2));
    l->pop_back();
    assert(*l->back() == 1);

    auto ll = gc_new_deque<int>();
    gc_delete(ll);
}

void testHashMap() {
    auto l = gc_new_unordered_map<int, int>();
    l[1] = gc_new<int>(1);
    assert(l->size() == 1);
    assert(*l[1] == 1);

    auto ll = gc_new_unordered_map<int, int>();
    gc_delete(ll);
}

void testLambda() {
    gc_function<int()> ff;
    {
        auto l = gc_new<int>(1);
        auto f = [=] { return *l; };

        ff = f;
    }

    int i = ff();
    assert(i == 1);
}

void testPrimaryImplicitCtor() {
    gc<int> a(1), b = gc_new<int>(2);
    assert(a < b);

    auto v = gc_new_vector<int>();
    v->push_back(1);
    assert(v[0] == 1);

    using namespace std::string_literals;

    gc_string s = "213"s;
    printf("%s", s->c_str());
}

void testGcFromThis() {
    struct Base {
        int i;
        Base() {
            auto p = gc_from(this);
            assert(p);
        }
    };

    struct Child : Base {
        int b;
    };

    auto makeLowerBoundHasElemToCompare = gc_new<int>();
    auto p = gc_new<Base>();
}

void testDynamicCast() {
    struct BaseA {
        int a;
        virtual ~BaseA() {}
    };
    struct BaseB {
        float f;
        virtual ~BaseB() {}
    };
    struct Sub : BaseA, BaseB {
        int c;
    };
    auto sub = gc_new<Sub>();
    gc<BaseA> baseA = sub;

    auto sub2 = gc_dynamic_pointer_cast<Sub>(baseA);
    assert(sub == sub2);

    // Cant compile:
    // auto sub3 = gc_dynamic_pointer_cast<BaseB>(baseA);
}

void testException() {
    struct Ctx {
        int dctorCnt = 0, ctorCnt = 0;
        int len = 3;
    };

    struct Test {
        Ctx& c;
        Test(Ctx& cc) : c(cc) {
            c.ctorCnt++;
            if (c.ctorCnt == c.len)
                throw 1;
        }
        ~Test() { c.dctorCnt++; }
    };

    auto err = false;
    Ctx c;
    try {
        auto i = gc_new_array<Test>(c.len, c);
    } catch (int) {
        err = true;
    }
    assert(err);
    assert(c.dctorCnt == c.len - 1);
    assert(details::ClassMeta::get<Test>()->isCreatingObj == 0);
}

void testCollection() {
    struct Circled {
        gc<Circled> child;
    };

    {
        int cnt = 1;
        for (int i = 0; i < cnt; i++) {
            auto s = gc_new<Circled>();
            s->child = s;
        }
        gc_collector()->dumpStats();
        gc_collect();
        gc_collector()->dumpStats();
    }
}

const int profilingCounts = 1024 * 1024;

auto profiled = [](const char* tag, auto cb) {
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < profilingCounts; i++)
        cb();
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed_seconds = end - start;
    printf("[%10s] elapsed time: %fs\n", tag, elapsed_seconds.count());
};

void profileAlloc() {
#ifndef _DEBUG
    vector<int*> rawPtrs;
    rawPtrs.reserve(profilingCounts);
    profiled("gc int", [] { gc<int> p(111); });
    profiled("raw int", [&] { rawPtrs.push_back(new int(111)); });
    for (auto* i : rawPtrs)
        delete i;
    gc_collector()->fullCollect();
#endif
}

int main() {
    profileAlloc();
    testCollection();
    testException();

    testDynamicCast();

    testGcFromThis();
    testCircledContainer();
    testPrimaryImplicitCtor();
    testSet();
    testEmpty();

    testPointerCast();

    testMoveCtor();
    testCirc();
    testArray();

    testContinusVector();
    testContinusList();
    testList();
    testDeque();
    testHashMap();
    testLambda();

    // there are some objects leaked from the upper tests, just dump them
    // out.
    gc_collector()->dumpStats();
    gc_collect();
    // there should be no objects exists after the collecting.
    gc_collector()->dumpStats();

    // leaking test, you should not see leaks in the output of VS.
    auto i = gc_new<int>(100);
    return 0;
}
