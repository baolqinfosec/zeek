// See the file "COPYING" in the main distribution directory for copyright.

#pragma once

#include "zeek-config.h"

#include "List.h"
#include "Hash.h"

ZEEK_FORWARD_DECLARE_NAMESPACED(DictEntry, zeek::detail);
ZEEK_FORWARD_DECLARE_NAMESPACED(IterCookie, zeek);

// Type for function to be called when deleting elements.
typedef void (*dict_delete_func)(void*);

namespace zeek {

// Type indicating whether the dictionary should keep track of the order
// of insertions.
enum DictOrder { ORDERED, UNORDERED };

// A dict_delete_func that just calls delete.
extern void generic_delete_func(void*);

class Dictionary {
public:
	explicit Dictionary(DictOrder ordering = UNORDERED,
	                    int initial_size = 0);

	~Dictionary();

	// Member functions for looking up a key, inserting/changing its
	// contents, and deleting it.  These come in two flavors: one
	// which takes a HashKey, and the other which takes a raw key,
	// its size, and its (unmodulated) hash.
	void* Lookup(const zeek::detail::HashKey* key) const
		{ return Lookup(key->Key(), key->Size(), key->Hash()); }
	void* Lookup(const void* key, int key_size, zeek::detail::hash_t hash) const;

	// Returns previous value, or 0 if none.
	void* Insert(zeek::detail::HashKey* key, void* val)
		{
		return Insert(key->TakeKey(), key->Size(), key->Hash(), val, 0);
		}
	// If copy_key is true, then the key is copied, otherwise it's assumed
	// that it's a heap pointer that now belongs to the Dictionary to
	// manage as needed.
	void* Insert(void* key, int key_size, zeek::detail::hash_t hash, void* val,
			bool copy_key);

	// Removes the given element.  Returns a pointer to the element in
	// case it needs to be deleted.  Returns 0 if no such element exists.
	// If dontdelete is true, the key's bytes will not be deleted.
	void* Remove(const zeek::detail::HashKey* key)
		{ return Remove(key->Key(), key->Size(), key->Hash()); }
	void* Remove(const void* key, int key_size, zeek::detail::hash_t hash,
				bool dont_delete = false);

	// Number of entries.
	int Length() const
		{ return tbl2 ? num_entries + num_entries2 : num_entries; }

	// Largest it's ever been.
	int MaxLength() const
		{
		return tbl2 ?
			max_num_entries + max_num_entries2 : max_num_entries;
		}

	// Total number of entries ever.
	uint64_t NumCumulativeInserts() const
		{
		return cumulative_entries;
		}

	// True if the dictionary is ordered, false otherwise.
	bool IsOrdered() const		{ return order != nullptr; }

	// If the dictionary is ordered then returns the n'th entry's value;
	// the second method also returns the key.  The first entry inserted
	// corresponds to n=0.
	//
	// Returns nil if the dictionary is not ordered or if "n" is out
	// of range.
	void* NthEntry(int n) const
		{
		const void* key;
		int key_len;
		return NthEntry(n, key, key_len);
		}
	void* NthEntry(int n, const void*& key, int& key_len) const;

	// To iterate through the dictionary, first call InitForIteration()
	// to get an "iteration cookie".  The cookie can then be handed
	// to NextEntry() to get the next entry in the iteration and update
	// the cookie.  If NextEntry() indicates no more entries, it will
	// also delete the cookie, or the cookie can be manually deleted
	// prior to this if no longer needed.
	//
	// Unexpected results will occur if the elements of
	// the dictionary are changed between calls to NextEntry() without
	// first calling InitForIteration().
	//
	// If return_hash is true, a HashKey for the entry is returned in h,
	// which should be delete'd when no longer needed.
	IterCookie* InitForIteration() const;
	void* NextEntry(zeek::detail::HashKey*& h, IterCookie*& cookie, int return_hash) const;
	void StopIteration(IterCookie* cookie) const;

	void SetDeleteFunc(dict_delete_func f)		{ delete_func = f; }

	// With a robust cookie, it is safe to change the dictionary while
	// iterating. This means that (i) we will eventually visit all
	// unmodified entries as well as all entries added during iteration,
	// and (ii) we won't visit any still-unseen entries which are getting
	// removed. (We don't get this for free, so only use it if
	// necessary.)
	void MakeRobustCookie(IterCookie* cookie)
		{ cookies.push_back(cookie); }

	// Remove all entries.
	void Clear();

	unsigned int MemoryAllocation() const;

private:
	void Init(int size);
	void Init2(int size);	// initialize second table for resizing
	void DeInit();

	// Internal version of Insert().
	void* Insert(zeek::detail::DictEntry* entry, bool copy_key);

	void* DoRemove(zeek::detail::DictEntry* entry, zeek::detail::hash_t h,
	               zeek::PList<zeek::detail::DictEntry>* chain, int chain_offset);

	int NextPrime(int n) const;
	bool IsPrime(int n) const;
	void StartChangeSize(int new_size);
	void FinishChangeSize();
	void MoveChains();

	// The following get and set the "density" threshold - if the
	// average hash chain length exceeds this threshold, the
	// table will be resized.  The default value is 3.0.
	double DensityThresh() const	{ return den_thresh; }

	void SetDensityThresh(double thresh)
		{
		den_thresh = thresh;
		thresh_entries = int(thresh * double(num_buckets));
		}

	// Same for the second table, when resizing.
	void SetDensityThresh2(double thresh)
		{
		den_thresh2 = thresh;
		thresh_entries2 = int(thresh * double(num_buckets2));
		}

	// Normally we only have tbl.
	// When we're resizing, we'll have tbl (old) and tbl2 (new)
	// tbl_next_ind keeps track of how much we've moved to tbl2
	// (it's the next index we're going to move).
	zeek::PList<zeek::detail::DictEntry>** tbl = nullptr;
	int num_buckets = 0;
	int num_entries = 0;
	int max_num_entries = 0;
	int thresh_entries = 0;
	uint64_t cumulative_entries = 0;
	double den_thresh = 0.0;

	// Resizing table (replicates tbl above).
	zeek::PList<zeek::detail::DictEntry>** tbl2 = nullptr;
	int num_buckets2 = 0;
	int num_entries2 = 0;
	int max_num_entries2 = 0;

	int thresh_entries2 = 0;
	double den_thresh2 = 0;

	zeek::detail::hash_t tbl_next_ind = 0;

	zeek::PList<zeek::detail::DictEntry>* order = nullptr;
	dict_delete_func delete_func = nullptr;

	zeek::PList<IterCookie> cookies;
};

template<typename T>
class PDict : public Dictionary {
public:
	explicit PDict(DictOrder ordering = UNORDERED, int initial_size = 0) :
		Dictionary(ordering, initial_size) {}
	T* Lookup(const char* key) const
		{
		zeek::detail::HashKey h(key);
		return (T*) Dictionary::Lookup(&h);
		}
	T* Lookup(const zeek::detail::HashKey* key) const
		{ return (T*) Dictionary::Lookup(key); }
	T* Insert(const char* key, T* val)
		{
		zeek::detail::HashKey h(key);
		return (T*) Dictionary::Insert(&h, (void*) val);
		}
	T* Insert(zeek::detail::HashKey* key, T* val)
		{ return (T*) Dictionary::Insert(key, (void*) val); }
	T* NthEntry(int n) const
		{ return (T*) Dictionary::NthEntry(n); }
	T* NthEntry(int n, const char*& key) const
		{
		int key_len;
		return (T*) Dictionary::NthEntry(n, (const void*&) key, key_len);
		}
	T* NextEntry(IterCookie*& cookie) const
		{
		zeek::detail::HashKey* h;
		return (T*) Dictionary::NextEntry(h, cookie, 0);
		}
	T* NextEntry(zeek::detail::HashKey*& h, IterCookie*& cookie) const
		{ return (T*) Dictionary::NextEntry(h, cookie, 1); }
	T* RemoveEntry(const zeek::detail::HashKey* key)
		{ return (T*) Remove(key->Key(), key->Size(), key->Hash()); }
	T* RemoveEntry(const zeek::detail::HashKey& key)
		{ return (T*) Remove(key.Key(), key.Size(), key.Hash()); }
};

} //namespace zeek

using Dictionary [[deprecated("Remove in v4.1. Use zeek::Dictionary instead.")]] = zeek::Dictionary;
template<typename T> using PDict [[deprecated("Remove in v4.1. Use zeek::PDict instead.")]] = zeek::PDict<T>;
