#include <iostream>
#include <assert.h>
#include <stdio.h>

#include "Array.hh"
#include "Hashtable.hh"
#include "MassTrans.hh"
#include "List.hh"
#include "Queue.hh"
#include "Transaction.hh"

#define N 100

#define HASHTABLE 0

kvepoch_t global_log_epoch = 0;
volatile uint64_t globalepoch = 1;     // global epoch, updated by main thread regularly
kvtimestamp_t initial_timestamp;
volatile bool recovering = false; // so don't add log entries, and free old value immediately

using namespace std;

void queueTests() {
    Queue<int> q;

    // NONEMPTY TESTS
    {
        Transaction t;
        q.transPush(t, 1);
        q.transPush(t, 2);
        assert(t.commit());
    }

    {
        // front with no pops
        Transaction t;
        int *p = q.transFront(t);
        assert(*p == 1);
        int *s = q.transFront(t);
        assert(*s == 1);
        assert(t.commit());
    }

    {
        // pop until empty
        Transaction t;
        assert(q.transPop(t));
        assert(q.transPop(t));
        assert (!q.transPop(t));
        
        // prepare pushes for next test
        q.transPush(t, 1);
        q.transPush(t, 2);
        q.transPush(t, 3); 
        assert(t.commit());
    }

    {
        // fronts intermixed with pops
        Transaction t;
        int *p = q.transFront(t);
        assert(*p == 1);
        assert(q.transPop(t));
        p = q.transFront(t);
        assert(*p == 2);
        assert(q.transPop(t));
        p = q.transFront(t);
        assert(*p == 3);
        assert(q.transPop(t));
        assert(!q.transPop(t));
        
        // set up for next test
        q.transPush(t, 1);
        q.transPush(t, 2);
        q.transPush(t, 3);  
        assert(t.commit());
    }

    {
        // front intermixed with pushes on nonempty
        Transaction t;
        int *p = q.transFront(t);
        assert(*p == 1);
        p = q.transFront(t);
        assert(*p == 1);
        q.transPush(t,4);
        p = q.transFront(t);
        assert(*p == 1);
        assert(t.commit());
    }

    {
        // pops intermixed with pushes and front on nonempty
        // q = [1 2 3 4]
        Transaction t;
        assert(q.transPop(t));
        int *p = q.transFront(t);
        assert(*p == 2);
        q.transPush(t, 5);
        // q = [2 3 4 5]
        assert(q.transPop(t));
        p = q.transFront(t);
        assert(*p == 3);
        q.transPush(t, 6);
        // q = [3 4 5 6]
        assert(t.commit());
    }

    // EMPTY TESTS
    {
        // front with empty queue
        Transaction t;
        
        // empty the queue
        assert(q.transPop(t));
        assert(q.transPop(t));
        assert(q.transPop(t));
        assert(q.transPop(t));
        assert(!q.transPop(t));
        
        int* p = q.transFront(t);
        assert(!p);
       
        q.transPush(t, 1);
        p = q.transFront(t);
        assert(*p == 1);
        p = q.transFront(t);
        assert(*p == 1);
        assert(t.commit());
    }

    {
        // pop with empty queue
        Transaction t;
        
        // empty the queue
        assert(q.transPop(t));
        assert(!q.transPop(t));
        
        int* p = q.transFront(t);
        assert(!p);
       
        q.transPush(t, 1);
        assert(q.transPop(t));
        assert(!q.transPop(t));
        assert(t.commit());
    }

    {
        // pop and front with empty queue
        Transaction t;
        
        int* p = q.transFront(t);
        assert(!p);
       
        q.transPush(t, 1);
        p = q.transFront(t);
        assert(*p == 1);
        assert(q.transPop(t));
       
        q.transPush(t, 1);
        assert(q.transPop(t));
        p = q.transFront(t);
        assert(!p); 
        assert(!q.transPop(t));
        assert(t.commit());
    }

    {
        Transaction t;
    }
}

void linkedListTests() {
  List<int> l;
  
  {
    Transaction t;
    assert(!l.transFind(t, 5));
    assert(l.transInsert(t, 5));
    int *p = l.transFind(t, 5);
    assert(*p == 5);
    assert(t.commit());
  }

  {
    Transaction t;
    assert(!l.transInsert(t, 5));
    assert(*l.transFind(t, 5) == 5);
    assert(!l.transFind(t, 7));
    assert(l.transInsert(t, 7));
    assert(t.commit());
  }

  {
    Transaction t;
    assert(l.transSize(t) == 2);
    assert(l.transInsert(t, 10));
    assert(l.transSize(t) == 3);
    auto it = l.transIter(t);
    int i = 0;
    int elems[] = {5,7,10};
    while (it.transHasNext(t)) {
      assert(*it.transNext(t) == elems[i++]);
    }
    assert(t.commit());
  }


  {
    Transaction t;
  }
}

void stringKeyTests() {
#if 1
  Hashtable<std::string, std::string> h;
  
  Transaction t;
  {
  std::string s1("bar");
  assert(h.transInsert(t, "foo", s1));
  }
  std::string s;
  assert(h.transGet(t, "foo", s));
  assert(s == "bar");
  assert(t.commit());

  Transaction t2;
  assert(h.transGet(t2, "foo", s));
  assert(s == "bar");
  assert(t2.commit());
#endif
}

void insertDeleteTest(bool shouldAbort) {
  MassTrans<int> h;
  Transaction t;
  for (int i = 10; i < 25; ++i) {
    assert(h.transInsert(t, i, i+1));
  }
  assert(t.commit());

  Transaction t2;
  assert(h.transInsert(t2, 25, 26));
  int x;
  assert(h.transGet(t2, 25, x));
  assert(!h.transGet(t2, 26, x));

  assert(h.transDelete(t2, 25));

  if (shouldAbort) {
    Transaction t3;
    assert(h.transInsert(t3, 26, 27));
    assert(t3.commit());

    try {
      t2.commit();
      assert(0);
    } catch (Transaction::Abort E) {}
  } else
    assert(t2.commit());
}

void insertDeleteSeparateTest() {
  MassTrans<int> h;
  Transaction t_init;
  for (int i = 10; i < 12; ++i) {
    assert(h.transInsert(t_init, i, i+1));
  }
  assert(t_init.commit());

  Transaction t;
  int x;
  assert(!h.transGet(t, 12, x));

  Transaction t2;
  assert(h.transInsert(t2, 12, 13));
  assert(h.transDelete(t2, 10));
  assert(t2.commit());
  
  try {
    t.commit();
    assert(0);
  } catch (Transaction::Abort E) {}


  Transaction t3;
  assert(!h.transGet(t3, 13, x));
  
  Transaction t4;
  assert(h.transInsert(t4, 10, 11));
  assert(h.transInsert(t4, 13, 14));
  assert(h.transDelete(t4, 11));
  assert(h.transDelete(t4, 12));
  assert(t4.commit());

  try {
    t3.commit();
    assert(0);
  } catch (Transaction::Abort E) {}

}

void rangeQueryTest() {
  MassTrans<int> h;
  
  Transaction t_init;
  int n = 99;
  char ns[64];
  sprintf(ns, "%d", n);
  for (int i = 10; i <= n; ++i) {
    assert(h.transInsert(t_init, i, i+1));
  }
  assert(t_init.commit());

  Transaction t;
  int x = 0;
  h.transQuery(t, "10", Masstree::Str(), [&] (Masstree::Str , int ) { x++; return true; });
  assert(x == n-10+1);
  
  x = 0;
  h.transQuery(t, "10", ns, [&] (Masstree::Str , int) { x++; return true; });
  assert(x == n-10);

  x = 0;
  h.transRQuery(t, ns, Masstree::Str(), [&] (Masstree::Str , int ) { x++; return true; });
  assert(x == n-10+1);
  
  x = 0;
  h.transRQuery(t, ns, "90", [&] (Masstree::Str , int ) { x++; return true; });
  assert(x == n-90);

  x = 0;
  h.transQuery(t, "10", "25", [&] (Masstree::Str , int ) { x++; return true; });
  assert(x == 25-10);

  x = 0;
  h.transQuery(t, "10", "26", [&] (Masstree::Str , int ) { x++; return true; });
  assert(x == 26-10);

  assert(t.commit());
}

int main() {
/*
  typedef int Key;
  typedef int Value;
#if HASHTABLE
  Hashtable<Key, Value> h;
#else
  MassTrans<Value> h;
  h.thread_init();
#endif

  Transaction t;

  Value v1,v2,vunused;
  //  assert(!h.transGet(t, 0, v1));

  assert(h.transInsert(t, 0, 1));
  h.transPut(t, 1, 3);
  
  assert(t.commit());

  Transaction tm;
  assert(h.transUpdate(tm, 1, 2));
  assert(tm.commit());

  Transaction t2;
  assert(h.transGet(t2, 1, v1));
  assert(t2.commit());

  Transaction t3;
  h.transPut(t3, 0, 4);
  assert(t3.commit());
  Transaction t4;
  assert(h.transGet(t4, 0, v2));
  assert(t4.commit());

  Transaction t5;
  assert(!h.transInsert(t5, 0, 5));
  assert(t5.commit());

  Transaction t6;
  assert(!h.transUpdate(t6, 2, 1));
  assert(t6.commit());

  Transaction t7;
  assert(!h.transGet(t7, 2, vunused));
  Transaction t8;
  assert(h.transInsert(t8, 2, 2));
  assert(t8.commit());

  try {
    t7.commit();
    assert(0);
  } catch(Transaction::Abort E) {}

  Transaction t9;
  assert(h.transInsert(t9, 3, 0));
  Transaction t10;
  assert(h.transInsert(t10, 4, 4));
  try{
    // t9 inserted invalid node, so we are forced to abort
    h.transUpdate(t10, 3, vunused);
    assert(0);
  } catch (Transaction::Abort E) {}
  Transaction t10_2;
  try {
    // deletes should also force abort from invalid nodes
    h.transDelete(t10_2, 3);
    assert(0);
  } catch (Transaction::Abort E) {}
  assert(t9.commit());
  assert(!t10.commit() && !t10_2.commit());

  Transaction t11;
  assert(h.transInsert(t11, 4, 5));
  assert(t11.commit());

  // basic delete
  Transaction t12;
  assert(!h.transDelete(t12, 5));
  assert(h.transUpdate(t12, 4, 0));
  assert(h.transDelete(t12, 4));
  assert(!h.transGet(t12, 4, vunused));
  assert(!h.transUpdate(t12, 4, 0));
  assert(!h.transDelete(t12, 4));
  assert(t12.commit());

  // delete-then-insert
  Transaction t13;
  assert(h.transGet(t13, 3, vunused));
  assert(h.transDelete(t13, 3));
  assert(h.transInsert(t13, 3, 1));
  assert(h.transGet(t13, 3, vunused));
  assert(t13.commit());

  // insert-then-delete
  Transaction t14;
  assert(!h.transGet(t14, 4, vunused));
  assert(h.transInsert(t14, 4, 14));
  assert(h.transGet(t14, 4, vunused));
  assert(h.transDelete(t14, 4));
  assert(!h.transGet(t14, 4, vunused));
  assert(t14.commit());

  // blind update success
  Transaction t15;
  assert(h.transUpdate(t15, 3, 15));
  Transaction t16;
  assert(h.transUpdate(t16, 3, 16));
  assert(t16.commit());
  assert(t15.commit());

  // update aborts after delete
  Transaction t17;
  Transaction t18;
  assert(h.transUpdate(t17, 3, 17));
  assert(h.transDelete(t18, 3));
  assert(t18.commit());
  try {
    t17.commit();
    assert(0);
  } catch (Transaction::Abort E) {}

#if !HASHTABLE
  Transaction t19;
  h.transQuery(t19, "0", "2", [] (Masstree::Str s, int val) { printf("%s, %d\n", s.data(), val); return true; });
  h.transQuery(t19, "4", "4", [] (Masstree::Str s, int val) { printf("%s, %d\n", s.data(), val); return true; });
  assert(t19.commit());
#endif

  // insert-then-delete node test
  insertDeleteTest(false);
  insertDeleteTest(true);

  // insert-then-delete problems with masstree version numbers (currently fails)
  insertDeleteSeparateTest();

  rangeQueryTest();

  h.print();

  cout << v1 << " " << v2 << endl;

  // string key testing
  stringKeyTests();

  linkedListTests();
*/  
  queueTests();
  
}
