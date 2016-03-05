#include "microbench.h"

typedef GenericKey<31> keytype;
typedef GenericComparator<31> keycomp;

static const uint64_t key_type=0;
static const uint64_t value_type=1; // 0 = random pointers, 1 = pointers to keys

//==============================================================
// GET INSTANCE
//==============================================================
template<typename KeyType, class KeyComparator>
Index<KeyType, KeyComparator> *getInstance(const int type, const uint64_t kt) {
  if (type == 0)
    return new BtreeIndex<KeyType, KeyComparator>(kt);
  else if (type == 1)
    return new ArtIndex_Generic<KeyType, KeyComparator>(kt);
  else
    return new BtreeIndex<KeyType, KeyComparator>(kt);
}

//==============================================================
// LOAD
//==============================================================
inline void load(int wl, int kt, int index_type, std::vector<keytype> &init_keys, std::vector<keytype> &keys, std::vector<uint64_t> &values, std::vector<int> &ranges, std::vector<int> &ops) {
  std::string init_file;
  std::string txn_file;
  // 0 = a, 1 = c, 2 = e
  if (kt == 0 && wl == 0) {
    init_file = "workloads/email_loada_zipf_int_100M.dat";
    txn_file = "workloads/email_txnsa_zipf_int_100M.dat";
  }
  else if (kt == 0 && wl == 1) {
    init_file = "workloads/email_loadc_zipf_int_100M.dat";
    txn_file = "workloads/email_txnsc_zipf_int_100M.dat";
  }
  else if (kt == 0 && wl == 2) {
    init_file = "workloads/email_loade_zipf_int_100M.dat";
    txn_file = "workloads/email_txnse_zipf_int_100M.dat";
  }
  else {
    init_file = "workloads/email_loada_zipf_int_100M.dat";
    txn_file = "workloads/email_txnsa_zipf_int_100M.dat";
  }

  std::ifstream infile_load(init_file);
  std::ifstream infile_txn(txn_file);

  std::string op;
  std::string key_str;
  keytype key;
  int range;

  std::string insert("INSERT");
  std::string read("READ");
  std::string update("UPDATE");
  std::string scan("SCAN");

  int count = 0;
  while ((count < INIT_LIMIT) && infile_load.good()) {
    infile_load >> op >> key_str;
    if (op.compare(insert) != 0) {
      std::cout << "READING LOAD FILE FAIL!\n";
      return;
    }
    key.setFromString(key_str);
    init_keys.push_back(key);
    count++;
  }

  count = 0;
  uint64_t value = 0;
  void *base_ptr = malloc(8);
  uint64_t base = (uint64_t)(base_ptr);
  free(base_ptr);

  keytype *init_keys_data = init_keys.data();

  if (value_type == 0) {
    while (count < INIT_LIMIT) {
      value = base + rand();
      values.push_back(value);
      count++;
    }
  }
  else {
    while (count < INIT_LIMIT) {
      values.push_back((uint64_t)init_keys_data[count].data);
      count++;
    }
  }

  count = 0;
  while ((count < LIMIT) && infile_txn.good()) {
    infile_txn >> op >> key_str;
    key.setFromString(key_str);
    if (op.compare(insert) == 0) {
      ops.push_back(0);
      keys.push_back(key);
      ranges.push_back(1);
    }
    else if (op.compare(read) == 0) {
      ops.push_back(1);
      keys.push_back(key);
    }
    else if (op.compare(update) == 0) {
      ops.push_back(2);
      keys.push_back(key);
    }
    else if (op.compare(scan) == 0) {
      infile_txn >> range;
      ops.push_back(3);
      keys.push_back(key);
      ranges.push_back(range);
    }
    else {
      std::cout << "UNRECOGNIZED CMD!\n";
      return;
    }
    count++;
  }

}

//==============================================================
// EXEC
//==============================================================
inline void exec(int wl, int index_type, std::vector<keytype> &init_keys, std::vector<keytype> &keys, std::vector<uint64_t> &values, std::vector<int> &ranges, std::vector<int> &ops) {

  Index<keytype, keycomp> *idx = getInstance<keytype, keycomp>(index_type, key_type);

  //WRITE ONLY TEST-----------------
  int count = 0;
  double start_time = get_now();
  while (count < (int)init_keys.size()) {
    idx->insert(init_keys[count], values[count]);
    /*
    if (!idx->insert(init_keys[count], values[count])) {
      std::cout << "LOAD FAIL!\n";
      return;
    }
    */
    count++;
  }
  double end_time = get_now();
  double tput = count / (end_time - start_time) / 1000000; //Mops/sec

  std::cout << "insert " << tput << "\n";
  std::cout << "memory " << (idx->getMemory() / 1000000) << "\n";

  //idx->merge();
  std::cout << "static memory " << (idx->getMemory() / 1000000) << "\n\n";
  //return;

  //READ/UPDATE/SCAN TEST----------------
  start_time = get_now();
  int txn_num = 0;
  uint64_t sum = 0;

#ifdef PAPI_IPC
  //Variables for PAPI
  float real_time, proc_time, ipc;
  long long ins;
  int retval;

  if((retval = PAPI_ipc(&real_time, &proc_time, &ins, &ipc)) < PAPI_OK) {    
    printf("PAPI error: retval: %d\n", retval);
    exit(1);
  }
#endif

#ifdef PAPI_CACHE
  int events[3] = {PAPI_L1_TCM, PAPI_L2_TCM, PAPI_L3_TCM};
  long long counters[3] = {0, 0, 0};
  int retval;

  if ((retval = PAPI_start_counters(events, 3)) != PAPI_OK) {
    fprintf(stderr, "PAPI failed to start counters: %s\n", PAPI_strerror(retval));
    exit(1);
  }
#endif

  while ((txn_num < LIMIT) && (txn_num < (int)ops.size())) {
    if (ops[txn_num] == 0) { //INSERT
      //idx->insert(keys[txn_num] + 1, values[txn_num]);
      idx->insert(keys[txn_num], values[txn_num]);
    }
    else if (ops[txn_num] == 1) { //READ
      sum += idx->find(keys[txn_num]);
    }
    else if (ops[txn_num] == 2) { //UPDATE
      //std::cout << "\n=============================================\n";
      //std::cout << "value before = " << idx->find(keys[txn_num]) << "\n";
      //std::cout << "update value = " << values[txn_num] << "\n";
      idx->upsert(keys[txn_num], values[txn_num]);
      //std::cout << "value after = " << idx->find(keys[txn_num]) << "\n"; 
    }
    else if (ops[txn_num] == 3) { //SCAN
      idx->scan(keys[txn_num], ranges[txn_num]);
    }
    else {
      std::cout << "UNRECOGNIZED CMD!\n";
      return;
    }
    txn_num++;
  }

#ifdef PAPI_IPC
  if((retval = PAPI_ipc(&real_time, &proc_time, &ins, &ipc)) < PAPI_OK) {    
    printf("PAPI error: retval: %d\n", retval);
    exit(1);
  }

  std::cout << "Time = " << real_time << "\n";
  std::cout << "Tput = " << LIMIT/real_time << "\n";
  std::cout << "Inst = " << ins << "\n";
  std::cout << "IPC = " << ipc << "\n";
#endif

#ifdef PAPI_CACHE
  if ((retval = PAPI_read_counters(counters, 3)) != PAPI_OK) {
    fprintf(stderr, "PAPI failed to read counters: %s\n", PAPI_strerror(retval));
    exit(1);
  }

  std::cout << "L1 miss = " << counters[0] << "\n";
  std::cout << "L2 miss = " << counters[1] << "\n";
  std::cout << "L3 miss = " << counters[2] << "\n";
#endif

  end_time = get_now();
  tput = txn_num / (end_time - start_time) / 1000000; //Mops/sec

  std::cout << "sum = " << sum << "\n";

  if (wl == 0) {  
    std::cout << "read/update " << (tput + (sum - sum)) << "\n";
  }
  else if (wl == 1) {
    std::cout << "read " << (tput + (sum - sum)) << "\n";
  }
  else if (wl == 2) {
    std::cout << "insert/scan " << (tput + (sum - sum)) << "\n";
  }
  else {
    std::cout << "read/update " << (tput + (sum - sum)) << "\n";
  }
}

int main(int argc, char *argv[]) {

  if (argc != 4) {
    std::cout << "Usage:\n";
    std::cout << "1. workload type: a, c, e\n";
    std::cout << "2. key distribution: email\n";
    std::cout << "3. index type: btree, art\n";
    return 1;
  }

  int wl = 0;
  // 0 = a
  // 1 = c
  // 2 = e
  if (strcmp(argv[1], "a") == 0)
    wl = 0;
  else if (strcmp(argv[1], "c") == 0)
    wl = 1;
  else if (strcmp(argv[1], "e") == 0)
    wl = 2;
  else
    wl = 0;

  int kt = 0;
  // 0 = email
  if (strcmp(argv[2], "rand") == 0)
    kt = 0;
  else
    kt = 0;


  int index_type = 0;
  // 0 = btree
  // 1 = art
  if (strcmp(argv[3], "btree") == 0)
    index_type = 0;
  else if (strcmp(argv[3], "art") == 0)
    index_type = 1;
  else
    index_type = 0;

  std::vector<keytype> init_keys;
  std::vector<keytype> keys;
  std::vector<uint64_t> values;
  std::vector<int> ranges;
  std::vector<int> ops; //INSERT = 0, READ = 1, UPDATE = 2

  load(wl, kt, index_type, init_keys, keys, values, ranges, ops);

  exec(wl, index_type, init_keys, keys, values, ranges, ops);

  return 0;
}
