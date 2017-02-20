#include <cstring>
#include <iostream>
#include <string>

template <std::size_t keySize>
class GenericKey {
public:
  inline void setFromString(std::string key) {
    memset(data, 0, keySize);
    strcpy(data, key.c_str());
  }

  char data[keySize];

  friend std::istream &operator>>( std::istream  &input, GenericKey & key ) {
    std::string temp_string;
    input >> temp_string;
    key.setFromString(temp_string);
    return input;
  }
};

template <std::size_t keySize>
class GenericComparator {
public:
  GenericComparator() {}

  inline bool operator()(const GenericKey<keySize> &lhs, const GenericKey<keySize> &rhs) const {
    int diff = strcmp(lhs.data, rhs.data);
    return diff < 0;
  }
};

