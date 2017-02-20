#include <cstdlib>
#include "microbench.h"

typedef uint64_t keytype;
typedef std::less<uint64_t> keycomp;

static const uint64_t key_type=0;

void
load_operations(const std::string &txn_file, std::vector<int> &ops, std::vector<keytype> &keys, std::vector<int> &ranges);

//==============================================================
// GET INSTANCE
//==============================================================
template<typename KeyType, class KeyComparator>
Index<KeyType, KeyComparator> *getInstance(const int type, const uint64_t kt) {
  if (type == 0)
    return new BtreeIndex<KeyType, KeyComparator>(kt);
  else if (type == 1)
    return new ArtIndex<KeyType, KeyComparator>(kt);
  else
    return new BtreeIndex<KeyType, KeyComparator>(kt);
}

//==============================================================
// LOAD
//==============================================================
inline void load(std::string workloadName, int index_type, std::vector<keytype> &init_keys, std::vector<keytype> &keys, std::vector<uint64_t> &values, std::vector<int> &ranges, std::vector<int> &ops) {
  std::string init_file = "workloads/" + workloadName + "_load.dat";
  std::string txn_file = "workloads/" + workloadName + "_txn.dat";

  check_input_files(init_file, txn_file);

  load_initial_keys(init_file, init_keys, values, [](keytype const &key) {
    return key;
  });

  load_operations<keytype>(txn_file, ops, keys, ranges);
}

//==============================================================
// EXEC
//==============================================================
inline void exec(int index_type, std::vector<keytype> &init_keys, std::vector<keytype> &keys, std::vector<uint64_t> &values, std::vector<int> &ranges, std::vector<int> &ops) {

  Index<keytype, keycomp> *idx = getInstance<keytype, keycomp>(index_type, key_type);

  //WRITE ONLY TEST-----------------
  int count = 0;
  double start_time = get_now();
  while (count < (int)init_keys.size()) {
    if (!idx->insert(init_keys[count], values[count])) {
      std::cout << "LOAD FAIL!\n";
      return;
    }
    count++;
  }
  double end_time = get_now();
  double tput = count / (end_time - start_time) / 1000000; //Mops/sec

  std::cout << "insert " << tput << "\n";
  std::cout << "memory " << (idx->getMemory() / 1000000) << "\n\n";

  //idx->merge();
  std::cout << "static memory " << (idx->getMemory() / 1000000) << "\n\n";
  //return;

  //READ/UPDATE/SCAN TEST----------------
  start_time = get_now();
  int txn_num = 0;
  uint64_t sum = 0;
  uint64_t s = 0;

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
  static const int EVENT_COUNT = 3;
  int events[EVENT_COUNT] = {PAPI_L1_TCM, PAPI_L2_TCM, PAPI_L3_TCM};
  long long counters[EVENT_COUNT];
  int retval;

  if ((retval = PAPI_start_counters(events, EVENT_COUNT)) != PAPI_OK) {
    fprintf(stderr, "PAPI failed to start counters: %s\n", PAPI_strerror(retval));
    exit(1);
  }
#endif

  size_t inserts = 0ul;
  size_t reads = 0ul;
  size_t updates = 0ul;
  size_t scans = 0ul;

  while ((txn_num < LIMIT) && (txn_num < (int)ops.size())) {
    if (ops[txn_num] == 0) { //INSERT
      idx->insert(keys[txn_num] + 1, values[txn_num]);
      ++inserts;
	/*
      if (!idx->insert(keys[txn_num] + 1, values[txn_num])) { //need to modify +1
	std::cout << "INSERT FAIL!\n";
	return -1;
      } 
	*/
    }
    else if (ops[txn_num] == 1) { //READ
      sum += idx->find(keys[txn_num]);
      ++reads;
      /*
      s = idx->find(keys[txn_num]);
      if (s == 0)
	std::cout << "read fail\n";
      sum += s;
      */
    }
    else if (ops[txn_num] == 2) { //UPDATE
      idx->upsert(keys[txn_num], values[txn_num]);
      ++updates;
    }
    else if (ops[txn_num] == 3) { //SCAN
      idx->scan(keys[txn_num], ranges[txn_num]);
      ++scans;
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
  if ((retval = PAPI_read_counters(counters, EVENT_COUNT)) != PAPI_OK) {
    fprintf(stderr, "PAPI failed to read counters: %s\n", PAPI_strerror(retval));
    exit(1);
  }

  std::cout << "L1 miss = " << counters[0] << "\n";
  std::cout << "L2 miss = " << counters[1] << "\n";
  std::cout << "L3 miss = " << counters[2] << "\n";
#endif

  std::cout << std::endl;
  std::cout << "Inserts = " << inserts << "\n";
  std::cout << "Updates = " << updates << "\n";
  std::cout << "Reads = " << reads << "\n";
  std::cout << "Scans = " << scans << "\n";
  std::cout << std::endl;

  end_time = get_now();
  tput = txn_num / (end_time - start_time) / 1000000; //Mops/sec

  std::cout << "sum = " << sum << "\n";

  std::vector<std::string> operationsOccured;

  if(reads > 0) {
    operationsOccured.push_back("read");
  }
  if(scans > 0) {
    operationsOccured.push_back("scan");
  }
  if(updates > 0) {
    operationsOccured.push_back("update");
  }
  if(inserts > 0) {
    operationsOccured.push_back("insert");
  }
  std::string operationSummary = "";
  for(size_t i=0; i < operationsOccured.size(); ++i) {
    if(i != 0) {
      operationSummary += "/";
    }
    operationSummary += operationsOccured[i];
  }

  std::cout << operationSummary << " " << tput << " Mops/sec\n";
}

int main(int argc, char *argv[]) {

  if (argc != 3) {
    std::cout << "Usage:\n";
    std::cout << "1. workload-name: basename of workload file (workloads/email_load_<workload-name>.dat) and transaction file (workloads/email_txn_<workload-name>.dat).\n";
    std::cout << "2. index type: btree, art\n";
    return 1;
  }

  std::string workloadName { argv[1] };
  int index_type = get_and_check_index_type(argv[2]);

  std::vector<keytype> init_keys;
  std::vector<keytype> keys;
  std::vector<uint64_t> values;
  std::vector<int> ranges;
  std::vector<int> ops; //INSERT = 0, READ = 1, UPDATE = 2

  load(workloadName, index_type, init_keys, keys, values, ranges, ops);

  exec(index_type, init_keys, keys, values, ranges, ops);

  return 0;
}
