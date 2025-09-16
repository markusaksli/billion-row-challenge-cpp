#pragma once
#include <thread>

#include "vector.h"

template <typename K>
struct HashSetNode
{
	K k;
	u32 next = 0;
};

template <typename K>
struct HashSet
{
	Vector<HashSetNode<K>> items;
	Vector<u32> buckets;

	void _init(u64 initBuckets, u64 initItems)
	{
		assert(initBuckets > 0);
		items._init(initItems);
		buckets._init(initBuckets);
		memset(buckets.data, 0, buckets.Bytes());
		buckets.size = buckets.reserved;
	}

	HashSet(u64 initBuckets = 11, u64 initItems = 2)
	{
		_init(initBuckets, initItems);
	}

	~HashSet()
	{
		buckets.~Vector();
		items.~Vector();
	}

	K* InsertHashed(const K& k, HASH_T hash)
	{
		u32* itemPtr = buckets.Get(hash % buckets.size);
		while (true)
		{
			const u32 item = *itemPtr;
			if (item == 0) break;
			if (items[item] == k) return items.Get(item);
			itemPtr = &items[item].next;
		}

		*itemPtr = items.size;
		items.PushReuse(k);
		items.Last().k = k;
		items.Last().next = 0;

		return nullptr;
	}

	K* Insert(const K& k)
	{
		return InsertHashed(k, std::hash<K>(k));
	}
};
