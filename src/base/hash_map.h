#pragma once
#include <thread>

#include "vector.h"

#if defined(__SIZEOF_INT128__)
constexpr u64 modMul(u64 a, u64 b, u64 m) {
    return (unsigned __int128(a) * b) % m;
}
#else
constexpr u64 modMul(u64 a, u64 b, u64 m) {
    u64 res = 0;
    while (b > 0) {
        if (b & 1) res = (res + a) % m;
        a = (a + a) % m;
        b >>= 1;
    }
    return res;
}
#endif

constexpr u64 modPow(u64 base, u64 exp, u64 mod) {
    u64 res = 1;
    while (exp > 0) {
        if (exp & 1) res = modMul(res, base, mod);
        base = modMul(base, base, mod);
        exp >>= 1;
    }
    return res;
}

constexpr bool millerRabinTest(u64 n, u64 a, u64 d, int r) {
    u64 x = modPow(a, d, n);
    if (x == 1 || x == n - 1) return true;
    for (int i = 1; i < r; i++) {
        x = modMul(x, x, n);
        if (x == n - 1) return true;
    }
    return false;
}

constexpr bool isPrime(u64 n) {
    if (n < 2) return false;
    // check small primes first
    for (u64 p : {2ull, 3ull, 5ull, 7ull, 11ull, 13ull, 17ull, 19ull, 23ull, 29ull, 31ull, 37ull})
        if (n % p == 0) return n == p;

    // write n-1 = d * 2^r
    u64 d = n - 1;
    int r = 0;
    while ((d & 1) == 0) {
        d >>= 1;
        r++;
    }

    // deterministic bases for 64-bit correctness
    for (u64 a : {2ull, 3ull, 5ull, 7ull, 11ull, 13ull, 17ull, 19ull, 23ull, 29ull, 31ull, 37ull}) {
        if (a >= n) break;
        if (!millerRabinTest(n, a, d, r)) return false;
    }
    return true;
}

constexpr u64 nextPrime(u64 n) {
    if (n <= 2) return 2;
    if ((n & 1) == 0) ++n;
    while (!isPrime(n)) n += 2;
    return n;
}

static_assert(nextPrime(100) == 101, "computed incorrectly");

template <typename K>
struct HashSetNode
{
    K k;
    u32 next = 0;
};

template <typename K>
struct HashSet
{
    typedef HashSetNode<K> NodeType;
	Vector<NodeType> items;
	Vector<u32> buckets;
    u32* lastIndexed = nullptr;

	void Init(u64 initBuckets, u64 initItems)
	{
		assert(initBuckets > 0);
		items.Init(initItems);
		buckets.InitZero(initBuckets);
		buckets.size = buckets.reserved;
	}

	void InitAuto(u64 initItems)
	{
		Init(initItems, nextPrime(initItems * 10));
	}

    explicit HashSet(u64 initItems)
	{
        InitAuto(initItems);
	}

	~HashSet()
	{
		buckets.~Vector();
		items.~Vector();
	}

    void InsertIndexed(const K& k, u32* itemPtr)
	{
        *itemPtr = items.size;
        items.PushReuse();
        items.Last().k = k;
        items.Last().next = 0;
	}

    void InsertLastIndexed(const K& k)
	{
        assert(lastIndexed != nullptr);
        InsertIndexed(k, lastIndexed);
	}

    NodeType* InsertHashed(const K& k, HASH_T hash)
	{
		u32* itemPtr = buckets.Get(hash % buckets.size);
		while (true)
		{
			const u32 item = *itemPtr;
			if (item == 0) break;
			if (items[item].k == k) return items.Get(item);
			itemPtr = &items[item].next;
		}

        InsertIndexed(k, itemPtr);

		return nullptr;
	}

    NodeType* Insert(const K& k)
	{
		return InsertHashed(k, std::hash<K>()(k));
	}

    NodeType* FindHashed(const K& k, HASH_T hash)
    {
        u32* itemPtr = buckets.Get(hash % buckets.size);

        while (true)
        {
            lastIndexed = itemPtr;
            const u32 item = *itemPtr;
            if (item == 0) break;
            if (items[item].k == k) return items.Get(item);
            itemPtr = &items[item].next;
        }

        return nullptr;
    }

    NodeType* Find(const K& k)
	{
        return Find(k, std::hash<K>(k));
	}
};

template <typename K, typename V>
struct HashPairNode
{
    K k;
    V v;
    u32 next = 0;
};

template <typename K, typename V>
struct HashMap
{
    typedef HashPairNode<K, V> NodeType;
    Vector<NodeType> items;
    Vector<u32> buckets;
    u32* lastIndexed = nullptr;

    void Init(u64 initBuckets, u64 initItems)
    {
        assert(initBuckets > 0);
        items.Init(initItems);
        buckets.InitZero(initBuckets);
        buckets.size = buckets.reserved;
    }

    void InitAuto(u64 initItems)
    {
        Init(initItems, nextPrime(initItems * 10));
    }

    explicit HashMap(u64 initItems)
    {
        InitAuto(initItems);
    }

    static HASH_T HashKey(const K& k)
    {
        return std::hash<K>()(k);
    }

    void InsertIndexed(const K& k, const V& v, u32* itemPtr)
    {
        assert(items.size < U32_MAX);
        *itemPtr = items.size + 1;  // NOLINT(clang-diagnostic-shorten-64-to-32)
        items.PushReuse();
        items.Last().k = k;
        items.Last().v = v;
        items.Last().next = 0;
    }

    void InsertLastIndexed(const K& k, const V& v)
    {
        assert(lastIndexed != nullptr);
        InsertIndexed(k, v, lastIndexed);
    }

    NodeType* InsertHashed(const K& k, const V& v, HASH_T hash)
    {
        u32* itemPtr = buckets.Get(hash % buckets.size);
        while (true)
        {
            u32 item = *itemPtr;
            if (item == 0) break;
            item--;
            if (items[item].k == k) return items.Get(item);
            itemPtr = &items[item].next;
        }

        InsertIndexed(k, v, itemPtr);

        return nullptr;
    }

    NodeType* Insert(const K& k, const V& v)
    {
        return InsertHashed(k, v, HashKey(k));
    }

    NodeType* FindHashed(const K& k, HASH_T hash)
    {
        u32* itemPtr = buckets.Get(hash % buckets.size);

        while (true)
        {
            u32 item = *itemPtr;
            if (item == 0) break;
            item--;
            if (items[item].k == k)
            {
                lastIndexed = itemPtr;
                return items.Get(item);
            }
            itemPtr = &items[item].next;
        }

        lastIndexed = itemPtr;
        return nullptr;
    }

    NodeType* Find(const K& k)
    {
        return FindHashed(k, HashKey(k));
    }
};