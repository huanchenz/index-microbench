#include <iostream>
#include "indexkey.h"
#include "stx/btree_map.h"
#include "stx/btree.h"
#include "ART/hybridART.h"

template<typename KeyType, class KeyComparator>
class Index
{
 public:
  virtual bool insert(KeyType key, uint64_t value) = 0;

  virtual uint64_t find(KeyType key) = 0;

  virtual bool upsert(KeyType key, uint64_t value) = 0;

  virtual uint64_t scan(KeyType key, int range) = 0;

  virtual int64_t getMemory() const = 0;

  virtual void merge() = 0;
};


template<typename KeyType, class KeyComparator>
class BtreeIndex : public Index<KeyType, KeyComparator>
{
 public:

  typedef AllocatorTracker<std::pair<const KeyType, uint64_t> > AllocatorType;
  typedef stx::btree_map<KeyType, uint64_t, KeyComparator, stx::btree_default_map_traits<KeyType, uint64_t>, AllocatorType> MapType;

  ~BtreeIndex() {
    delete idx;
    delete alloc;
  }

  bool insert(KeyType key, uint64_t value) {
    std::pair<typename MapType::iterator, bool> retval = idx->insert(key, value);
    return retval.second;
  }

  uint64_t find(KeyType key) {
    iter = idx->find(key);
    if (iter == idx->end()) {
      std::cout << "READ FAIL\n";
      return 0;
    }
    return iter->second;
  }

  bool upsert(KeyType key, uint64_t value) {
    (*idx)[key] = value;
    return true;
  }

  uint64_t scan(KeyType key, int range) {
    iter = idx->lower_bound(key);

    if (iter == idx->end()) {
      std::cout << "SCAN FIRST READ FAIL\n";
      return 0;
    }
    
    uint64_t sum = 0;
    sum += iter->second;
    for (int i = 0; i < range; i++) {
      ++iter;
      if (iter == idx->end()) {
	break;
      }
      sum += iter->second;
    }
    return sum;
  }

  int64_t getMemory() const {
    return memory;
  }

  void merge() {
    return;
  }

  BtreeIndex(uint64_t kt) {
    memory = 0;
    alloc = new AllocatorType(&memory);
    idx = new MapType(KeyComparator(), (*alloc));
  }

  MapType *idx;
  int64_t memory;
  AllocatorType *alloc;
  typename MapType::const_iterator iter;
};


template<typename KeyType, class KeyComparator>
class ArtIndex : public Index<KeyType, KeyComparator>
{
 public:

  ~ArtIndex() {
    delete idx;
    delete key_bytes;
  }

  bool insert(KeyType key, uint64_t value) {
    //std::cout << "insert " << key << "\n";
    loadKey(key);
    idx->insert(key_bytes, value, key_length);
    return true;
  }

  uint64_t find(KeyType key) {
    loadKey(key);
    return idx->lookup(key_bytes, key_length, key_length);
  }

  bool upsert(KeyType key, uint64_t value) {
    loadKey(key);
    //idx->insert(key_bytes, value, key_length);
    idx->upsert(key_bytes, value, key_length, key_length);
    return true;
  }

  uint64_t scan(KeyType key, int range) {
    loadKey(key);
    uint64_t sum = idx->lower_bound(key_bytes, key_length, key_length);
    for (int i = 0; i < range - 1; i++)
      sum += idx->next();
    return sum;
  }

  int64_t getMemory() const {
    return idx->getMemory();
  }

  void merge() {
    idx->tree_info();
    idx->merge();
  }

  ArtIndex(uint64_t kt) {
    key_type = kt;
    if (kt == 0) {
      key_length = 8;
      key_bytes = new uint8_t [8];
    }
    else {
      key_length = 8;
      key_bytes = new uint8_t [8];
    }

    idx = new hybridART(key_length);
  }

 private:

  inline void loadKey(KeyType key) {
    if (key_type == 0) {
      reinterpret_cast<uint64_t*>(key_bytes)[0]=__builtin_bswap64(key);
    }
  }

  inline void printKeyBytes() {
    for (int i = 0; i < key_length; i ++)
      std::cout << (uint64_t)key_bytes[i] << " ";
    std::cout << "\n";
  }

  hybridART *idx;
  uint64_t key_type; // 0 = uint64_t
  unsigned key_length;
  uint8_t* key_bytes;
};


template<typename KeyType, class KeyComparator>
class ArtIndex_Generic : public Index<KeyType, KeyComparator>
{
 public:

  ~ArtIndex_Generic() {
    delete idx;
    delete key_bytes;
  }

  bool insert(KeyType key, uint64_t value) {
    loadKey(key);
    idx->insert(key_bytes, value, key_length);
    return true;
  }

  uint64_t find(KeyType key) {
    loadKey(key);
    return idx->lookup(key_bytes, key_length, key_length);
  }

  bool upsert(KeyType key, uint64_t value) {
    loadKey(key);
    //idx->insert(key_bytes, value, key_length);
    idx->upsert(key_bytes, value, key_length, key_length);
    return true;
  }

  uint64_t scan(KeyType key, int range) {
    loadKey(key);
    uint64_t sum = idx->lower_bound(key_bytes, key_length, key_length);
    for (int i = 0; i < range - 1; i++)
      sum += idx->next();
    return sum;
  }

  int64_t getMemory() const {
    return idx->getMemory();
  }

  void merge() {
    idx->tree_info();
    idx->merge();
  }

  ArtIndex_Generic(uint64_t kt) {
    key_type = kt;
    if (kt == 0) {
      key_length = 31;
      key_bytes = new uint8_t [31];
    }
    else {
      key_length = 31;
      key_bytes = new uint8_t [31];
    }

    idx = new hybridART(key_length);
  }

 private:

  inline void loadKey(KeyType key) {
    if (key_type == 0) {
      key_bytes = (uint8_t*)key.data;
    }
  }

  inline void printKeyBytes() {
    for (int i = 0; i < key_length; i ++)
      std::cout << (uint64_t)key_bytes[i] << " ";
    std::cout << "\n";
  }

  hybridART *idx;
  uint64_t key_type; // 0 = GenericKey<31>
  unsigned key_length;
  uint8_t* key_bytes;
};

