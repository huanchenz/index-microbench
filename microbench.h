#ifndef __MICROBENCH_H__
#define __MICROBENCH_H__

#include <cstring>
#include <cstdlib>
#include <sstream>
#include <vector>
#include <fstream>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <utility>
#include <time.h>
#include <sys/time.h>
#include <papi.h>
#include <unistd.h>

#include "allocatortracker.h"

//#include "btreeIndex.h"
//#include "artIndex.h"
#include "index.h"

//#include <map>
//#include "stx/btree_map.h"
//#include "stx/btree.h"
//#include "stx-compact/btree_map.h"
//#include "stx-compact/btree.h"
//#include "stx-compress/btree_map.h"
//#include "stx-compress/btree.h"

//#define INIT_LIMIT 1001
//#define LIMIT 1000000
#define INIT_LIMIT 50000000
#define LIMIT 10000000

#define VALUES_PER_KEY 10

//#define PAPI_IPC 1
#define PAPI_CACHE 1


//==============================================================
inline double get_now() {
  struct timeval tv;
  gettimeofday(&tv, 0);
  return tv.tv_sec + tv.tv_usec / 1000000.0;
}

inline bool file_exists(std::string const & file_name) {
  return access( file_name.c_str(), F_OK ) != -1;
}

inline void check_input_files(std::string const & init_file, std::string const & txn_file) {
  if(!file_exists(init_file)) {
    std::cerr << "Workload init file " << init_file << " does not exist." << std::endl;
    exit(EXIT_FAILURE);
  }

  if(!file_exists(txn_file)) {
    std::cerr << "Workload txn file " << txn_file << " does not exist." << std::endl;
    exit(EXIT_FAILURE);
  }
}

inline int get_and_check_index_type(char const * index_name) {
	int index_type = 0;
	// 0 = btree
	// 1 = art
	if (strcmp(index_name, "btree") == 0) {
		index_type = 0;
	} else if (strcmp(index_name, "art") == 0) {
		index_type = 1;
	} else {
		std::cout << "Only index types \"btree\" and \"art\" are allowed" << std::endl;
		exit(EXIT_FAILURE);
	}

	return index_type;
}

const char* INSERT_OPERATION_IDENTIFIER = "INSERT";
const char* READ_OPERATION_IDENTIFIER = "READ";
const char* UPDATE_OPERATION_IDENTIFIER = "UPDATE";
const char* SCAN_OPERATION_IDENTIFIER = "SCAN";

template<typename keytype> std::pair<const char*, keytype> read_operation_line(std::string const file_name, size_t line_number, std::string const & line_buffer, std::initializer_list<const char*> allowed_operations) {
  std::istringstream lineStream { line_buffer };
  std::string op;
  keytype key;
  lineStream >> op;
  if (!lineStream.good()) {
    std::cout << "Invalid line format on line " << line_number << " in " << file_name << "!\n";
    exit(EXIT_FAILURE);
  }
  auto found_matching_operation = std::find_if(allowed_operations.begin(), allowed_operations.end(), [&op](const char* allowed_operation) {
    return std::strcmp(allowed_operation, op.c_str()) == 0;
  });

  if(found_matching_operation == allowed_operations.end()) {
    std::cout << "Invalid operation \"" << op << "\" on line " << line_number << " in " << file_name << "!\n";
    exit(EXIT_FAILURE);
  }
  lineStream >> key;

  return { *found_matching_operation, key };
}

template<typename keytype, typename KeyToValueConverter> size_t load_initial_keys(std::string init_file_name, std::vector<keytype> &init_keys, std::vector<uint64_t> &values, KeyToValueConverter convertKeyToValue) {
  std::ifstream infile_load(init_file_name);

  size_t count = 0;
  std::string line_buffer;
  size_t line_number = 0;
  while ((count < INIT_LIMIT) && infile_load.good() && std::getline(infile_load, line_buffer)) {
    ++line_number;
    if(!line_buffer.empty()) {
      keytype key = read_operation_line<keytype>(init_file_name, line_number, line_buffer, { INSERT_OPERATION_IDENTIFIER }).second;
      init_keys.push_back(key);
      values.push_back(convertKeyToValue(init_keys.back()));
      count++;
    }
  }

  if(count == 0) {
    std::cerr << "The init file (" << init_file_name << ") is empty " << std::endl;
    exit(EXIT_FAILURE);
  }

  return count;
}

template<typename keytype> void load_operations(const std::string &txn_file, std::vector<int> &ops, std::vector<keytype> &keys, std::vector<int> &ranges) {
  std::ifstream infile_txn(txn_file);

  std::string line_buffer;
  size_t line_number = 0;
  while (infile_txn.good() && std::getline(infile_txn, line_buffer)) {
    ++line_number;
    if(!line_buffer.empty()) {
      std::pair<const char*, keytype> operation = read_operation_line<keytype>(txn_file, line_number, line_buffer, { INSERT_OPERATION_IDENTIFIER, READ_OPERATION_IDENTIFIER, UPDATE_OPERATION_IDENTIFIER, SCAN_OPERATION_IDENTIFIER });
      const char * operation_identifier = operation.first;
      keys.push_back(operation.second);

      if (operation_identifier == INSERT_OPERATION_IDENTIFIER) {
        ops.push_back(0);
        ranges.push_back(1);
      }
      else if (operation_identifier == READ_OPERATION_IDENTIFIER) {
        ops.push_back(1);
      }
      else if (operation_identifier == UPDATE_OPERATION_IDENTIFIER) {
        ops.push_back(2);
      }
      else if (operation_identifier == SCAN_OPERATION_IDENTIFIER) {
        int range;
        infile_txn >> range;
        ops.push_back(3);
        ranges.push_back(range);
      }
    }
  }
}

#endif
