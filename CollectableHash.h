#pragma once
#include "Collectable.h"
#include "spooky.h"


/* A simple hash table with the following assumptions:
* 
1) the keys are any collectable type.  I added a hash function to collectables that defaults to being based on the address.  But I also added a string type.  All hashing is based on spookyhash.
2) the value type should be a collectable or at least something that has the total_instance_vars() and index_into_instance_vars(int) methods.  They will all be stored in a single block and the whole thing collected at once.
3) The table length has to be a power of 2 and will grow as necessary to mostly avoid collisions

The hash table uses linear probing and stays at least 4 times as big as the number of elements.  You can delete from this hash table, it handles that by marking elements "deleted" and not moving anything.

 */

inline int nearest_power_of_2(int i)
{
	int k = 1024;
	while (k < i)k <<= 1;
	return k;
}

#define INITIAL_HASH_SIZE 1024

extern CollectableSentinel CollectableNull;


template<typename K, typename V>
struct CollectableKeyHashEntry
{
	bool skip;
	bool empty;
	InstancePtr<K> key;

	alignas(alignof(V)) uint8_t value_bytes[sizeof(V)];
	V& value() { return *(V*)&value_bytes[0]; }
	const V& value() const { return *(V*)&value_bytes[0]; }
	CollectableKeyHashEntry() :skip(false), empty(true), key(static_cast<K *>(collectable_null)) {}
	int total_instance_vars() const { return 1; }
	InstancePtrBase* index_into_instance_vars(int num) { return &key;  }
	~CollectableKeyHashEntry() { if (!empty) value().~V(); }
};

template<typename K, typename V>
struct CollectableKeyHashTable :public Collectable
{
	int HASH_SIZE;
	int used;
	mutable int wasted;
	InstancePtr<CollectableInlineVector<struct CollectableKeyHashEntry<K, V>>> data;
	V empty_v;

	CollectableKeyHashTable(const V& ev,int s = INITIAL_HASH_SIZE) :HASH_SIZE(nearest_power_of_2(s)), used(0), wasted(0), data(new CollectableInlineVector<CollectableKeyHashEntry<K, V>>(HASH_SIZE)),empty_v(ev) {}

	void inc_used()
	{
		++used;
		if (((used + wasted) << 2) > HASH_SIZE)
		{
			int OLD_HASH_SIZE = HASH_SIZE;
			HASH_SIZE <<= 1;
			RootPtr<CollectableInlineVector<CollectableKeyHashEntry<K, V> > > t = data;
			data = new CollectableInlineVector<CollectableKeyHashEntry<K, V> >(HASH_SIZE);
			used = 0;
			wasted = 0;
			for (int i = 0; i < OLD_HASH_SIZE; ++i) {
				GC::safe_point();
				if (!t[i]->empty) insert(t[i]->key, t[i]->value());
			}
		}
	}
	bool findu(CollectableKeyHashEntry<K, V>** pair, const RootPtr<K>& key, bool for_insert) const
	{
		uint64_t h = key->hash();
		int start = h & (HASH_SIZE - 1);
		int i = start;
		CollectableKeyHashEntry<K, V>* recover = nullptr;
		GC::safe_point();
		do {
			CollectableKeyHashEntry<K, V>* e = (*data)[i];
			if (e->empty && !e->skip) {
				if (recover != nullptr) {
					*pair = recover;
					recover->skip = false;
					--wasted;
				}
				else *pair = e;
				return false;
			}
			bool skip = e->skip;
			if (for_insert && skip)
			{
				recover = e;
				for_insert = false;
			}
			if (!skip && h == e->key->hash() && e->key->equal(key.get())) {
				*pair = e;
				return true;
			}

			i = (i + 1) & (HASH_SIZE - 1);
		} while (i != start);
		assert(false);
		return false;
	}

	bool contains(const RootPtr<K>& key) const {
		CollectableKeyHashEntry<K, V>* pair = nullptr;
		return findu(&pair, key, false);
	}
	V operator[](const RootPtr<K>& key)
	{
		CollectableKeyHashEntry<K, V>* pair = nullptr;
		if (findu(&pair, key, false)) {

			return pair->value();
		}
		return empty_v;
	}
	bool insert(const RootPtr<K>& key, const V& value)
	{
		CollectableKeyHashEntry<K, V>* pair = nullptr;
		if (!findu(&pair, key, true)) {
			pair->key = key;
			//pair->value.~V();
			new(&pair->value_bytes[0]) V(value);
			pair->empty = false;
			inc_used();
			return true;
		}
		return false;
	}
	void insert_or_assign(const RootPtr<K>& key, const V& value)
	{
		CollectableKeyHashEntry<K, V>* pair = nullptr;
		bool replacing=findu(&pair, key, true);
		pair->key = key;
		if (replacing) pair->value().~V();
		new(&pair->value_bytes[0]) V(value);
		pair->empty = false;
		if (!replacing) inc_used();
	}
	bool erase(const RootPtr<K>& key)
	{
		CollectableKeyHashEntry<K, V>* pair = nullptr;
		if (findu(&pair, key, false)) {
			pair->skip = true;
			pair->key = (K*)collectable_null;
			pair->value().~V();
			pair->empty = true;
			used = used - 1;
			++wasted;
			return true;
		}
		return false;
	}
	int size() const { return used; }

	virtual int total_instance_vars() const {
		return 1;
	}
	virtual size_t my_size() const { return sizeof(*this); }
	virtual InstancePtrBase* index_into_instance_vars(int num) { return &data; }
};

template<typename K, typename V>
struct CollectableValueHashEntry
{
	bool skip;
	bool empty;
	alignas(alignof(K)) uint8_t key_bytes[sizeof(K)];
	K& key() { return *(K*)&key_bytes[0]; }
	const K& key() const { return *(K*)&key_bytes[0]; }
	InstancePtr<V> value;
	CollectableValueHashEntry() :skip(false),empty(true), value((const V *)collectable_null) {}
	int total_instance_vars() const { return 1; }
	InstancePtrBase* index_into_instance_vars(int num) { return &value; }
	~CollectableValueHashEntry()
	{
		if (!empty) key().~K();
	}
};

template<typename K, typename V>
struct CollectableValueHashTable :public Collectable
{
	int HASH_SIZE;
	int used;
	mutable int wasted;
	InstancePtr<CollectableInlineVector<CollectableValueHashEntry<K, V>>> data;


	CollectableValueHashTable(int s = INITIAL_HASH_SIZE) :HASH_SIZE(nearest_power_of_2(s)), used(0), wasted(0), data(new CollectableInlineVector<CollectableValueHashEntry<K, V>>(HASH_SIZE)) {}

	void inc_used()
	{
		++used;
		if (((used + wasted) << 2) > HASH_SIZE)
		{
			int OLD_HASH_SIZE = HASH_SIZE;
			HASH_SIZE <<= 1;
			RootPtr<CollectableInlineVector<CollectableValueHashEntry<K, V> > > t = data;
			data = new CollectableInlineVector<CollectableValueHashEntry<K, V> >(HASH_SIZE);
			used = 0;
			wasted = 0;
			for (int i = 0; i < OLD_HASH_SIZE; ++i) {
				GC::safe_point();
				if (!t[i]->empty && !t[i]->skip) insert(t[i]->key(), t[i]->value);
			}
		}
	}
	bool findu(CollectableValueHashEntry<K, V>** pair, const K& key, bool for_insert) const
	{
		size_t h = std::hash<K>{}(key);
		int start = h & (HASH_SIZE - 1);
		int i = start;
		CollectableValueHashEntry<K, V>* recover = nullptr;
		GC::safe_point();
		do {
			CollectableValueHashEntry<K, V>* e = data[i];
			if (e->empty && !e->skip) {
				if (recover != nullptr) {
					*pair = recover;
					recover->skip = false;
					--wasted;
				}
				else *pair = e;
				return false;
			}
			bool skip = e->skip;
			if (for_insert && skip)
			{
				recover = e;
				for_insert = false;
			}
			if (!skip && h == std::hash<K>{}(e->key()) && e->key() == key) {
				*pair = e;
				return true;
			}

			i = (i + 1) & (HASH_SIZE - 1);
		} while (i != start);
		assert(false);
		return false;
	}

	bool contains(const K& key) const {
		CollectableValueHashEntry<K, V>* pair = nullptr;
		return findu(&pair, key, false);
	}
	RootPtr<V> operator[](const K& key)
	{
		CollectableValueHashEntry<K, V>* pair = nullptr;
		if (findu(&pair, key, false)) {

			return pair->value;
		}
		return (V*)collectable_null;
	}
	bool insert(const K& key, const RootPtr<V>& value)
	{
		CollectableValueHashEntry<K, V>* pair = nullptr;
		if (!findu(&pair, key, true)) {
			new(&pair->key_bytes[0]) K(key);
			pair->value = value;
			pair->empty = false;
			inc_used();
			return true;
		}
		return false;
	}
	void insert_or_assign(const K& key, const RootPtr<V>& value)
	{
		CollectableValueHashEntry<K, V>* pair = nullptr;
		bool replacing=findu(&pair, key, true);
		if(replacing) pair->key().~K();
		new(&pair->key_bytes[0]) K(key);
		pair->value = value;
		pair->empty = false;
		if (!replacing) inc_used();
	}
	bool erase(const K& key)
	{
		CollectableValueHashEntry<K, V>* pair = nullptr;
		if (findu(&pair, key, false)) {
			pair->skip = true;
			pair->value = (V*)collectable_null;
			pair->empty = true;
			pair->key().~K();
			used = used - 1;
			++wasted;
			return true;
		}
		return false;
	}
	int size() const { return used; }

	virtual int total_instance_vars() const {
		return 1;
	}
	virtual size_t my_size() const { return sizeof(*this); }
	virtual InstancePtrBase* index_into_instance_vars(int num) { return &data; }
};



template<typename K, typename V>
struct CollectableHashEntry 
{
	bool skip;
	bool empty;
	InstancePtr<K> key;
	InstancePtr<V> value;
	CollectableHashEntry() :skip(false), empty(true) {}
	int total_instance_vars() const { return 2; }
	InstancePtrBase* index_into_instance_vars(int num) { if (num == 0) return &key; return &value; }
};

template<typename K, typename V>
struct CollectableHashTable :public Collectable
{
	int HASH_SIZE;
	int used;
	mutable int wasted;
	InstancePtr<CollectableInlineVector<CollectableHashEntry<K,V>>> data;


	CollectableHashTable(int s= INITIAL_HASH_SIZE) :HASH_SIZE(nearest_power_of_2(s)), used(0), wasted(0), data(new CollectableInlineVector<CollectableHashEntry<K,V>>(HASH_SIZE)) {}

	void inc_used()
	{
		++used;
		if (((used+wasted) << 2) > HASH_SIZE)
		{
			int OLD_HASH_SIZE = HASH_SIZE;
			HASH_SIZE <<= 1;
			RootPtr<CollectableInlineVector<CollectableHashEntry<K,V> > > t ( data);
			data = new CollectableInlineVector<CollectableHashEntry<K,V> >(HASH_SIZE);
			used = 0;
			wasted = 0;
			for (int i = 0; i < OLD_HASH_SIZE; ++i) {
				GC::safe_point();
				if (!t[i]->empty) insert(t[i]->key, t[i]->value);
			}
		}
	}
	bool findu(CollectableHashEntry<K, V>**pair ,const RootPtr<K> &key, bool for_insert) const
	{
		uint64_t h = key->hash();
		int start = h & (HASH_SIZE - 1);
		int i = start;
		CollectableHashEntry<K, V>* recover=nullptr;
		GC::safe_point();
		do {
			CollectableHashEntry<K, V>* e = data[i];
			if (e->empty && !e->skip) {
				if (recover != nullptr) {
					*pair = recover;
					recover->skip = false;
					--wasted;
				}
				else *pair = e;
				return false;
			}
			bool skip = e->skip;
			if (for_insert && skip)
			{
				recover = e;
				for_insert = false;
			}
			if (!skip && h == e->key->hash() && e->key->equal(key.get())) {
				*pair = e;
				return true;
			}

			i = (i + 1) & (HASH_SIZE - 1);
		} while (i != start);
		assert(false);
		return false;
	}

	bool contains(const RootPtr<K> &key) const {
		CollectableHashEntry<K, V>* pair = nullptr;
		return findu(&pair, key, false);
	}
	RootPtr<V> operator[](const RootPtr<K>& key)
	{
		CollectableHashEntry<K, V>* pair = nullptr;
		if (findu(&pair, key, false)) {

			return pair->value;
		}
		return (V *)collectable_null;
	}
	bool insert(const RootPtr<K>& key, const RootPtr<V>& value)
	{
		CollectableHashEntry<K, V>* pair = nullptr;
		if (!findu(&pair, key, true)) {
			pair->key = key;
			pair->value = value;
			pair->empty = false;
			inc_used();
			return true;
		}
		return false;
	}
	void insert_or_assign(const RootPtr<K>& key, const RootPtr<V>& value)
	{
		CollectableHashEntry<K, V>* pair = nullptr;
		bool replacing =findu(&pair, key, true);
		pair->key = key;
		pair->value = value;
		pair->empty = false;

		if (!replacing) inc_used();
	}
	bool erase(const RootPtr<K>& key)
	{
		CollectableHashEntry<K, V>* pair = nullptr;
		if (findu(&pair, key, false)) {
			pair->skip = true;
			pair->key = (K*)collectable_null;
			pair->value = (V*)collectable_null;
			pair->empty = true;
			used = used - 1;
			++wasted;
			return true;
		}
		return false;
	}
	int size() const { return used; }

	virtual int total_instance_vars() const {
		return 1;
	}
	virtual size_t my_size() const { return sizeof(*this);  }
	virtual InstancePtrBase* index_into_instance_vars(int num) { return &data; }
};


template<typename K, typename V>
struct HashEntry
{
	bool skip;
	bool empty;
	alignas(alignof(K)) uint8_t key_bytes[sizeof(K)];
	K& key() { return *(K*)&key_bytes[0]; }
	const K& key() const { return *(K*)&key_bytes[0]; }
	alignas(alignof(V)) uint8_t value_bytes[sizeof(V)];
	V& value() { return *(V*)&value_bytes[0]; }
	const V& value() const { return *(V*)&value_bytes[0]; }
	HashEntry() :skip(false),empty(true) {}
	HashEntry(const HashEntry& e) : skip(e.skip), empty(e.empty){
		if (!empty) {
			new (&key_bytes[0]) K(e.key());
			new (&value_bytes[0]) V(e.value());
		}
	}
	~HashEntry()
	{
		if (!empty) {
			key().~K();
			value().~V();
		}
	}
};

template<typename K, typename V>
struct HashTable :public Collectable
{
	int HASH_SIZE;
	int used;
	mutable int wasted;
	std::vector<HashEntry<K, V>> data;
	V empty_v;


	HashTable(const V& ev,int s = INITIAL_HASH_SIZE) :HASH_SIZE(nearest_power_of_2(s)), used(0), wasted(0), data(HASH_SIZE),empty_v(ev) {}

	void inc_used()
	{
		++used;
		if (((used + wasted) << 2) > HASH_SIZE)
		{
			int OLD_HASH_SIZE = HASH_SIZE;
			HASH_SIZE <<= 1;
			std::vector<HashEntry<K, V>> t = data;
			data.clear();
			data.resize(HASH_SIZE);
			used = 0;
			wasted = 0;
			for (int i = 0; i < OLD_HASH_SIZE; ++i) {
				GC::safe_point();
				if (!t[i].empty) insert(t[i].key(), t[i].value());
			}
		}
	}
	bool findu(HashEntry<K, V>** pair, const K& key, bool for_insert) const
	{
		size_t h = std::hash<K>{}(key);
		int start = h & (HASH_SIZE - 1);
		int i = start;
		HashEntry<K, V>* recover = nullptr;
		GC::safe_point();
		do {
			HashEntry<K, V>* e = const_cast<HashEntry<K, V>*>(&data[i]);
			if (e->empty && !e->skip) {
				if (recover != nullptr) {
					*pair = recover;
					recover->skip = false;
					--wasted;
				}
				else *pair = e;
				return false;
			}
			bool skip = e->skip;
			if (for_insert && skip)
			{
				recover = e;
				for_insert = false;
			}
			if (!skip && h == std::hash<K>{}(e->key()) && e->key() == key) {
				*pair = e;
				return true;
			}

			i = (i + 1) & (HASH_SIZE - 1);
		} while (i != start);
		assert(false);
		return false;
	}

	bool contains(const K& key) const {
		HashEntry<K, V>* pair = nullptr;
		return findu(&pair, key, false);
	}
	V operator[](const K& key)
	{
		HashEntry<K, V>* pair = nullptr;
		if (findu(&pair, key, false)) {

			return pair->value();
		}
		return empty_v;
	}
	bool insert(const K& key, const V& value)
	{
		HashEntry<K, V>* pair = nullptr;
		if (!findu(&pair, key, true)) {

			new(&pair->key_bytes[0]) K(key);

			new(&pair->value_bytes[0]) V(value);
			pair->empty = false;
			inc_used();
			return true;
		}
		return false;
	}
	void insert_or_assign(const K& key, const V& value)
	{
		HashEntry<K, V>* pair = nullptr;
		bool replacing=findu(&pair, key, true);
		if (replacing) {
			pair->key().~K();
			pair->value().~V();
		}
		new(&pair->key_bytes[0]) K(key);
		new(&pair->value_bytes[0]) V(value);
		pair->empty = false;
		if (!replacing) inc_used();
	}
	void clear()
	{
		if (used > 0) {
			for (int i = 0; i < HASH_SIZE; ++i) {
				HashEntry<K, V>* pair = &data[i];
				if (!pair->empty) {
					pair->key().~K();
					pair->value().~V();
				}
				pair->empty = true;
				pair->skip = false;
			}
			wasted = 0;
			used = 0;
		}
	}
	bool erase(const K& key)
	{
		HashEntry<K, V>* pair = nullptr;
		if (findu(&pair, key, false)) {
			pair->skip = true;
			pair->empty = true;
			used = used - 1;
			pair->key().~K();
			pair->value().~V();
			++wasted;
			return true;
		}
		return false;
	}
	int size() const { return used; }

	virtual int total_instance_vars() const {
		return 0;
	}
	virtual size_t my_size() const { return sizeof(*this); }
	virtual InstancePtrBase* index_into_instance_vars(int num) { return nullptr; }
};

