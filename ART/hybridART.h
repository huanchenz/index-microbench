 /*
  Adaptive Radix Tree
  Viktor Leis, 2012
  leis@in.tum.de
 */

#include <stdlib.h>    // malloc, free
#include <string.h>    // memset, memcpy
#include <stdint.h>    // integer types
#include <emmintrin.h> // x86 SSE intrinsics
#include <stdio.h>
#include <assert.h>
#include <sys/time.h>  // gettime
#include <algorithm>   // std::random_shuffle

#include <iostream>
#include <vector>
#include <deque>

//#define MORE_NODE_TYPES 1;

class hybridART {

public:
  static const unsigned MERGE=1;

  // Constants for the node types
  static const int8_t NodeType4=0;
  static const int8_t NodeType16=1;
  static const int8_t NodeType48=2;
  static const int8_t NodeType256=3;

  //huanchen-static
  static const int8_t NodeTypeD=0;
  static const int8_t NodeTypeDP=1;
  static const int8_t NodeTypeF=2;
  static const int8_t NodeTypeFP=3;
#ifdef MORE_NODE_TYPES
  static const int8_t NodeTypeP=4;
  static const int8_t NodeTypePP=5;
#endif

  // The maximum prefix length for compressed paths stored in the
  // header, if the path is longer it is loaded from the database on
  // demand
  static const unsigned maxPrefixLength=9;

  static const unsigned NodeDItemTHold=227;
  //static const unsigned NodeDItemTHold=48;
#ifdef MORE_NODE_TYPES
  static const unsigned NodePItemTHold=16;
#endif

  // Shared header of all inner nodes
  struct Node {
    // length of the compressed path (prefix)
    uint32_t prefixLength;
    // number of non-null children
    uint16_t count;
    // node type
    int8_t type;
    // compressed path (prefix)
    uint8_t prefix[maxPrefixLength];

    Node() : prefixLength(0),count(0),type(NodeType256) {}
    Node(int8_t type) : prefixLength(0),count(0),type(type) {}
  };

  //huanchen
  typedef struct {
    Node* node;
    uint16_t cursor;
  } NodeCursor;

  //huanchen-static
  struct NodeStatic {
    int8_t type;

    NodeStatic() : type(NodeTypeFP) {}
    NodeStatic(int8_t t) : type(t) {}
  };

  struct NodeD : NodeStatic {
    uint8_t count;
    uint8_t data[0];

    NodeD() : NodeStatic(NodeTypeD) {}
    NodeD(uint8_t size) : NodeStatic(NodeTypeD) {
      count = size;
    }

    uint8_t* key() {
      return data;
    }

    uint8_t* key(unsigned pos) {
      return &data[pos];
    }

    NodeStatic** child() {
      return (NodeStatic**)(&data[count]);
    }

    NodeStatic** child(unsigned pos) {
      return (NodeStatic**)(&data[count + sizeof(NodeStatic*) * pos]);
    }
  };

  struct NodeDP : NodeStatic {
    uint8_t count;
    uint32_t prefixLength;
    uint8_t data[0];

    NodeDP() : NodeStatic(NodeTypeDP) {}
    NodeDP(uint8_t size) : NodeStatic(NodeTypeDP) {
      count = size;
    }
    NodeDP(uint8_t size, uint32_t pl) : NodeStatic(NodeTypeDP) {
      count = size;
      prefixLength = pl;
    }

    uint8_t* prefix() {
      return data;
    }

    uint8_t* key() {
      return &data[prefixLength];
    }

    uint8_t* key(unsigned pos) {
      return &data[prefixLength + pos];
    }

    NodeStatic** child() {
      return (NodeStatic**)(&data[prefixLength + count]);
    }

    NodeStatic** child(unsigned pos) {
      return (NodeStatic**)(&data[prefixLength + count + sizeof(NodeStatic*) * pos]);
    }
  };

  struct NodeF : NodeStatic {
    uint16_t count;
    NodeStatic* child[256];

    NodeF() : NodeStatic(NodeTypeF) {}
    NodeF(uint16_t size) : NodeStatic(NodeTypeF) {
      count = size;
      memset(child, 0, sizeof(child));
    }
  };

  struct NodeFP : NodeStatic {
    uint16_t count;
    uint32_t prefixLength;
    uint8_t data[0];

    NodeFP() : NodeStatic(NodeTypeFP) {}
    NodeFP(uint16_t size) : NodeStatic(NodeTypeFP) {
      count = size;
    }
    NodeFP(uint16_t size, uint32_t pl) : NodeStatic(NodeTypeFP) {
      count = size;
      prefixLength = pl;
    }

    uint8_t* prefix() {
      return data;
    }

    NodeStatic** child() {
      return (NodeStatic**)&data[prefixLength];
    }
  };

#ifdef MORE_NODE_TYPES
  static const uint8_t emptyMarker_static=255;

  struct NodeP : NodeStatic {
    uint8_t count;
    uint8_t childIndex[256];
    NodeStatic* child[0];

    NodeP() : NodeStatic(NodeTypeP) {}
    NodeP(uint8_t size) : NodeStatic(NodeTypeP) {
      count = size;
      memset(childIndex, emptyMarker_static, sizeof(childIndex));
    }
  };

  struct NodePP : NodeStatic {
    uint8_t count;
    uint32_t prefixLength;
    uint8_t data[0];

    NodePP() : NodeStatic(NodeTypePP) {}
    NodePP(uint8_t size) : NodeStatic(NodeTypePP) {
      count = size;
      memset(&data[prefixLength], emptyMarker_static, 256);
    }
    NodePP(uint8_t size, uint32_t pl) : NodeStatic(NodeTypePP) {
      count = size;
      prefixLength = pl;
      memset(&data[prefixLength], emptyMarker_static, 256);
    }

    uint8_t* prefix() {
      return data;
    }

    uint8_t* childIndex() {
      return &data[prefixLength];
    }

    NodeStatic** child() {
      return (NodeStatic**)(&data[prefixLength + 256]);
    }
  };
#endif

  // Node with up to 4 children
  struct Node4 : Node {
    uint8_t key[4];
    Node* child[4];

    Node4() : Node(NodeType4) {
      memset(key,0,sizeof(key));
      memset(child,0,sizeof(child));
    }
  };

  // Node with up to 16 children
  struct Node16 : Node {
    uint8_t key[16];
    Node* child[16];

    Node16() : Node(NodeType16) {
      memset(key,0,sizeof(key));
      memset(child,0,sizeof(child));
    }
  };

  static const uint8_t emptyMarker=48;

  // Node with up to 48 children
  struct Node48 : Node {
    uint8_t childIndex[256];
    Node* child[48];

    Node48() : Node(NodeType48) {
      memset(childIndex,emptyMarker,sizeof(childIndex));
      memset(child,0,sizeof(child));
    }
  };

  // Node with up to 256 children
  struct Node256 : Node {
    Node* child[256];

    Node256() : Node(NodeType256) {
      memset(child,0,sizeof(child));
    }
  };

  //huanchen-static
  inline bool isInner(Node* n) {
    switch (n->type) {
    case NodeType4: {
      Node4* node=static_cast<Node4*>(n);
      for (unsigned i = 0; i < node->count; i++)
	if (isLeaf(node->child[i]))
	  return false;
      return true;
    }
    case NodeType16: {
      Node16* node=static_cast<Node16*>(n);
      for (unsigned i = 0; i < node->count; i++)
	if (isLeaf(node->child[i]))
	  return false;
      return true;
    }
    case NodeType48: {
      Node48* node=static_cast<Node48*>(n);
      for (unsigned i = 0; i < node->count; i++)
	if (isLeaf(node->child[i]))
	  return false;
      return true;
    }
    case NodeType256: {
      Node256* node=static_cast<Node256*>(n);
      for (unsigned i = 0; i < 256; i++)
	if (node->child[i] && isLeaf(node->child[i]))
	  return false;
      return true;
    }
    }
    return true;
  }

  inline Node* makeLeaf(uintptr_t tid) {
    // Create a pseudo-leaf
    return reinterpret_cast<Node*>((tid<<1)|1);
  }

  inline uintptr_t getLeafValue(Node* node) {
    // The the value stored in the pseudo-leaf
    return reinterpret_cast<uintptr_t>(node)>>1;
  }

  //huanchen-static
  inline uintptr_t getLeafValue(NodeStatic* node) {
    return reinterpret_cast<uintptr_t>(node)>>1;
  }

  inline bool isLeaf(Node* node) {
    // Is the node a leaf?
    return reinterpret_cast<uintptr_t>(node)&1;
  }

  //huanchen-static
  inline bool isLeaf(NodeStatic* node) {
    return reinterpret_cast<uintptr_t>(node)&1;
  }

  inline uint8_t flipSign(uint8_t keyByte) {
    // Flip the sign bit, enables signed SSE comparison of unsigned values, used by Node16
    return keyByte^128;
  }

  inline void loadKey(uintptr_t tid,uint8_t key[]) {
    // Store the key of the tuple into the key vector
    // Implementation is database specific
    reinterpret_cast<uint64_t*>(key)[0]=__builtin_bswap64(tid);
  }

  //huanchen
  inline void loadKey(uintptr_t tid, uint8_t key[], unsigned keyLength) {
    if (keyLength == 8)
      reinterpret_cast<uint64_t*>(key)[0]=__builtin_bswap64(tid);
    else
      memcpy(reinterpret_cast<void*>(key), (const void*)tid, keyLength);
  }

  // This address is used to communicate that search failed
  Node* nullNode=NULL;
  NodeStatic* nullNode_static=NULL;

  static inline unsigned ctz(uint16_t x) {
    // Count trailing zeros, only defined for x>0
#ifdef __GNUC__
    return __builtin_ctz(x);
#else
    // Adapted from Hacker's Delight
    unsigned n=1;
    if ((x&0xFF)==0) {n+=8; x=x>>8;}
    if ((x&0x0F)==0) {n+=4; x=x>>4;}
    if ((x&0x03)==0) {n+=2; x=x>>2;}
    return n-(x&1);
#endif
  }

  inline Node** findChild(Node* n,uint8_t keyByte) {
    // Find the next child for the keyByte
    switch (n->type) {
    case NodeType4: {
      Node4* node=static_cast<Node4*>(n);
      for (unsigned i=0;i<node->count;i++)
	if (node->key[i]==keyByte)
	  return &node->child[i];
      return &nullNode;
    }
    case NodeType16: {
      Node16* node=static_cast<Node16*>(n);

      __m128i cmp=_mm_cmpeq_epi8(_mm_set1_epi8(flipSign(keyByte)),_mm_loadu_si128(reinterpret_cast<__m128i*>(node->key)));
      unsigned bitfield=_mm_movemask_epi8(cmp)&((1<<node->count)-1);
      if (bitfield)
	return &node->child[ctz(bitfield)];
      else
	return &nullNode;
    }
    case NodeType48: {
      Node48* node=static_cast<Node48*>(n);
      if (node->childIndex[keyByte]!=emptyMarker)
	return &node->child[node->childIndex[keyByte]]; 
      else
	return &nullNode;
    }
    case NodeType256: {
      Node256* node=static_cast<Node256*>(n);
      return &(node->child[keyByte]);
    }
    }
    std::cout << "throw, type = " << (uint64_t)n->type <<"\n";
    throw; // Unreachable
  }

  //huanchen-static
  inline NodeStatic** findChild(NodeStatic* n,uint8_t keyByte) {
    switch (n->type) {

    case NodeTypeD: {
      NodeD* node = static_cast<NodeD*>(n);

      if (node->count < 5) {
	for (unsigned i = 0; i < node->count; i++)
	  if (node->key()[i] == flipSign(keyByte))
	    return &node->child()[i];
	return &nullNode_static;
      }

      for (unsigned i = 0; i < node->count; i += 16) {
	__m128i cmp=_mm_cmpeq_epi8(_mm_set1_epi8(flipSign(keyByte)),_mm_loadu_si128(reinterpret_cast<__m128i*>(node->key(i))));
	unsigned bitfield;
	if (i + 16 >= node->count)
	  bitfield =_mm_movemask_epi8(cmp)&((1<<node->count)-1);
	else
	  bitfield =_mm_movemask_epi8(cmp);
	if (bitfield)
	  return &node->child(i)[ctz(bitfield)]; 
      }
      return &nullNode_static;
    }

    case NodeTypeDP: {
      NodeDP* node = static_cast<NodeDP*>(n);

      if (node->count < 5) {
	for (unsigned i = 0; i < node->count; i++)
	  if (node->key()[i] == flipSign(keyByte))
	    return &node->child()[i];
	return &nullNode_static;
      }

      for (unsigned i = 0; i < node->count; i += 16) {
	__m128i cmp=_mm_cmpeq_epi8(_mm_set1_epi8(flipSign(keyByte)),_mm_loadu_si128(reinterpret_cast<__m128i*>(node->key(i))));
	unsigned bitfield;
	if (i + 16 >= node->count)
	  bitfield =_mm_movemask_epi8(cmp)&((1<<node->count)-1);
	else
	  bitfield =_mm_movemask_epi8(cmp);
	if (bitfield)
	  return &node->child(i)[ctz(bitfield)]; 
      }
      return &nullNode_static;
    }

    case NodeTypeF: {
      NodeF* node = static_cast<NodeF*>(n);
      return &(node->child[keyByte]);
    }

    case NodeTypeFP: {
      NodeFP* node = static_cast<NodeFP*>(n);
      return &(node->child()[keyByte]);
    }

#ifdef MORE_NODE_TYPES
    case NodeTypeP: {
      NodeP* node = static_cast<NodeP*>(n);
      if (node->childIndex[keyByte] != emptyMarker_static)
	return &node->child[node->childIndex[keyByte]];
      else
	return &nullNode_static;
    }

    case NodeTypePP: {
      NodePP* node = static_cast<NodePP*>(n);
      if (node->childIndex()[keyByte] != emptyMarker_static)
	return &node->child()[node->childIndex()[keyByte]];
      else
	return &nullNode_static;
    }
#endif
    }
    std::cout << "throw_static, type = " << (uint64_t)n->type <<"\n";
    throw; // Unreachable
  }


  inline Node* minimum(Node* node) {
    // Find the leaf with smallest key
    if (!node)
      return NULL;

    if (isLeaf(node))
      return node;

    switch (node->type) {
    case NodeType4: {
      Node4* n=static_cast<Node4*>(node);
      return minimum(n->child[0]);
    }
    case NodeType16: {
      Node16* n=static_cast<Node16*>(node);
      return minimum(n->child[0]);
    }
    case NodeType48: {
      Node48* n=static_cast<Node48*>(node);
      unsigned pos=0;
      while (n->childIndex[pos]==emptyMarker)
	pos++;
      return minimum(n->child[n->childIndex[pos]]);
    }
    case NodeType256: {
      Node256* n=static_cast<Node256*>(node);
      unsigned pos=0;
      while (!n->child[pos])
	pos++;
      return minimum(n->child[pos]);
    }
    }
    throw; // Unreachable
  }

  //huanchen-static
  inline NodeStatic* minimum(NodeStatic* node) {
    if (!node)
      return NULL;

    if (isLeaf(node))
      return node;

    switch (node->type) {
    case NodeTypeD: {
      NodeD* n=static_cast<NodeD*>(node);
      return minimum(n->child()[0]);
    }
    case NodeTypeDP: {
      NodeDP* n=static_cast<NodeDP*>(node);
      return minimum(n->child()[0]);
    }
    case NodeTypeF: {
      NodeF* n=static_cast<NodeF*>(node);
      unsigned pos=0;
      while (!n->child[pos])
	pos++;
      return minimum(n->child[pos]);
    }
    case NodeTypeFP: {
      NodeFP* n=static_cast<NodeFP*>(node);
      unsigned pos=0;
      while (!n->child()[pos])
	pos++;
      return minimum(n->child()[pos]);
    }

#ifdef MORE_NODE_TYPES
    case NodeTypeP: {
      NodeP* n=static_cast<NodeP*>(node);
      unsigned pos=0;
      while (n->childIndex[pos]==emptyMarker_static)
	pos++;
      return minimum(n->child[n->childIndex[pos]]);
    }
    case NodeTypePP: {
      NodePP* n=static_cast<NodePP*>(node);
      unsigned pos=0;
      while (n->childIndex()[pos]==emptyMarker_static)
	pos++;
      return minimum(n->child()[n->childIndex()[pos]]);
    }
#endif
    }
    throw; // Unreachable
  }

  inline Node* maximum(Node* node) {
    // Find the leaf with largest key
    if (!node)
      return NULL;

    if (isLeaf(node))
      return node;

    switch (node->type) {
    case NodeType4: {
      Node4* n=static_cast<Node4*>(node);
      return maximum(n->child[n->count-1]);
    }
    case NodeType16: {
      Node16* n=static_cast<Node16*>(node);
      return maximum(n->child[n->count-1]);
    }
    case NodeType48: {
      Node48* n=static_cast<Node48*>(node);
      unsigned pos=255;
      while (n->childIndex[pos]==emptyMarker)
	pos--;
      return maximum(n->child[n->childIndex[pos]]);
    }
    case NodeType256: {
      Node256* n=static_cast<Node256*>(node);
      unsigned pos=255;
      while (!n->child[pos])
	pos--;
      return maximum(n->child[pos]);
    }
    }
    throw; // Unreachable
  }

  //huanchen-static
  inline NodeStatic* maximum(NodeStatic* node) {
    if (!node)
      return NULL;

    if (isLeaf(node))
      return node;

    switch (node->type) {
    case NodeTypeD: {
      NodeD* n=static_cast<NodeD*>(node);
      return maximum(n->child()[0]);
    }
    case NodeTypeDP: {
      NodeDP* n=static_cast<NodeDP*>(node);
      return maximum(n->child()[0]);
    }
    case NodeTypeF: {
      NodeF* n=static_cast<NodeF*>(node);
      unsigned pos=0;
      while (!n->child[pos])
	pos++;
      return maximum(n->child[pos]);
    }
    case NodeTypeFP: {
      NodeFP* n=static_cast<NodeFP*>(node);
      unsigned pos=0;
      while (!n->child()[pos])
	pos++;
      return maximum(n->child()[pos]);
    }

#ifdef MORE_NODE_TYPES
    case NodeTypeP: {
      NodeP* n=static_cast<NodeP*>(node);
      unsigned pos=0;
      while (n->childIndex[pos]==emptyMarker_static)
	pos++;
      return maximum(n->child[n->childIndex[pos]]);
    }
    case NodeTypePP: {
      NodePP* n=static_cast<NodePP*>(node);
      unsigned pos=0;
      while (n->childIndex()[pos]==emptyMarker_static)
	pos++;
      return maximum(n->child()[n->childIndex()[pos]]);
    }
#endif
    }
    throw; // Unreachable
  }

  inline bool leafMatches(Node* leaf,uint8_t key[],unsigned keyLength,unsigned depth,unsigned maxKeyLength) {
    // Check if the key of the leaf is equal to the searched key
    if (depth!=keyLength) {
      uint8_t leafKey[maxKeyLength];
      loadKey(getLeafValue(leaf),leafKey);
      for (unsigned i=depth;i<keyLength;i++)
	if (leafKey[i]!=key[i])
	  return false;
    }
    return true;
  }

  //huanchen-static
  inline bool leafMatches(NodeStatic* leaf,uint8_t key[],unsigned keyLength,unsigned depth,unsigned maxKeyLength) {
    if (depth!=keyLength) {
      uint8_t leafKey[maxKeyLength];
      loadKey(getLeafValue(leaf),leafKey);
      for (unsigned i=depth;i<keyLength;i++)
	if (leafKey[i]!=key[i])
	  return false;
    }
    return true;
  }

  inline unsigned prefixMismatch(Node* node,uint8_t key[],unsigned depth,unsigned maxKeyLength) {
    // Compare the key with the prefix of the node, return the number matching bytes
    unsigned pos;
    if (node->prefixLength>maxPrefixLength) {
      for (pos=0;pos<maxPrefixLength;pos++)
	if (key[depth+pos]!=node->prefix[pos])
	  return pos;
      uint8_t minKey[maxKeyLength];
      loadKey(getLeafValue(minimum(node)),minKey);
      for (;pos<node->prefixLength;pos++)
	if (key[depth+pos]!=minKey[depth+pos])
	  return pos;
    } else {
      for (pos=0;pos<node->prefixLength;pos++)
	if (key[depth+pos]!=node->prefix[pos])
	  return pos;
    }
    return pos;
  }

  //huanchen-static
  inline unsigned prefixMismatch(NodeStatic* node,uint8_t key[],unsigned depth,unsigned maxKeyLength) {
    switch (node->type) {
    case NodeTypeD: {
      return 0;
    }
    case NodeTypeDP: {
      NodeDP* n = static_cast<NodeDP*>(node);
      unsigned pos;
      if (n->prefixLength > maxPrefixLength) {
	for (pos = 0; pos < maxPrefixLength; pos++)
	  if (key[depth+pos] != n->prefix()[pos])
	    return pos;
	uint8_t minKey[maxKeyLength];
	loadKey(getLeafValue(minimum(n)),minKey);
	for (; pos< n->prefixLength; pos++)
	  if (key[depth+pos] != minKey[depth+pos])
	    return pos;
      } else {
	for (pos = 0; pos < n->prefixLength; pos++)
	  if (key[depth+pos] != n->prefix()[pos])
	    return pos;
      }
      return pos;
    }
    case NodeTypeF: {
      return 0;
    }
    case NodeTypeFP: {
      NodeFP* n = static_cast<NodeFP*>(node);
      unsigned pos;
      if (n->prefixLength > maxPrefixLength) {
	for (pos = 0; pos < maxPrefixLength; pos++)
	  if (key[depth+pos] != n->prefix()[pos])
	    return pos;
	uint8_t minKey[maxKeyLength];
	loadKey(getLeafValue(minimum(n)),minKey);
	for (; pos< n->prefixLength; pos++)
	  if (key[depth+pos] != minKey[depth+pos])
	    return pos;
      } else {
	for (pos = 0; pos < n->prefixLength; pos++)
	  if (key[depth+pos] != n->prefix()[pos])
	    return pos;
      }
      return pos;
    }

#ifdef MORE_NODE_TYPES
    case NodeTypeP: {
      return 0;
    }
    case NodeTypePP: {
      NodePP* n = static_cast<NodePP*>(node);
      unsigned pos;
      if (n->prefixLength > maxPrefixLength) {
	for (pos = 0; pos < maxPrefixLength; pos++)
	  if (key[depth+pos] != n->prefix()[pos])
	    return pos;
	uint8_t minKey[maxKeyLength];
	loadKey(getLeafValue(minimum(n)),minKey);
	for (; pos< n->prefixLength; pos++)
	  if (key[depth+pos] != minKey[depth+pos])
	    return pos;
      } else {
	for (pos = 0; pos < n->prefixLength; pos++)
	  if (key[depth+pos] != n->prefix()[pos])
	    return pos;
      }
      return pos;
    }
#endif
    }
    return 0;
  }

  inline Node* lookup(Node* node,uint8_t key[],unsigned keyLength,unsigned depth,unsigned maxKeyLength) {
    // Find the node with a matching key, optimistic version
    bool skippedPrefix=false; // Did we optimistically skip some prefix without checking it?

    while (node!=NULL) {
      if (isLeaf(node)) {
	if (!skippedPrefix&&depth==keyLength) // No check required
	  return node;

	if (depth!=keyLength) {
	  // Check leaf
	  uint8_t leafKey[maxKeyLength];
	  //loadKey(getLeafValue(node),leafKey);
	  loadKey(getLeafValue(node),leafKey, keyLength);
	  for (unsigned i=(skippedPrefix?0:depth);i<keyLength;i++)
	    if (leafKey[i]!=key[i])
	      return NULL;
	}
	return node;
      }

      if (node->prefixLength) {
	if (node->prefixLength<maxPrefixLength) {
	  for (unsigned pos=0;pos<node->prefixLength;pos++)
	    if (key[depth+pos]!=node->prefix[pos])
	      return NULL;
	} else
	  skippedPrefix=true;
	depth+=node->prefixLength;
      }

      node=*findChild(node,key[depth]);
      depth++;
    }
    return NULL;
  }

  //huanchen-static
  inline NodeStatic* lookup(NodeStatic* node,uint8_t key[],unsigned keyLength,unsigned depth,unsigned maxKeyLength) {
    bool skippedPrefix=false; // Did we optimistically skip some prefix without checking it?

    while (node!=NULL) {
      if (isLeaf(node)) {
	if (!skippedPrefix && depth == keyLength) // No check required
	  return node;

	if (depth != keyLength) {
	  // Check leaf
	  uint8_t leafKey[maxKeyLength];
	  //loadKey(getLeafValue(node),leafKey);
	  loadKey(getLeafValue(node), leafKey, keyLength);
	  for (unsigned i=(skippedPrefix?0:depth); i<keyLength; i++)
	    if (leafKey[i] != key[i])
	      return NULL;
	}
	return node;
      }

      switch (node->type) {
      case NodeTypeDP: {
	NodeDP* n = static_cast<NodeDP*>(node);
	if (n->prefixLength < maxPrefixLength) {
	  for (unsigned pos=0; pos<n->prefixLength; pos++)
	    if (key[depth+pos] != n->prefix()[pos])
	      return NULL;
	} else
	  skippedPrefix=true;
	depth += n->prefixLength;
      }
      case NodeTypeFP: {
	NodeFP* n = static_cast<NodeFP*>(node);
	if (n->prefixLength < maxPrefixLength) {
	  for (unsigned pos=0; pos<n->prefixLength; pos++)
	    if (key[depth+pos] != n->prefix()[pos])
	      return NULL;
	} else
	  skippedPrefix=true;
	depth += n->prefixLength;
      }
#ifdef MORE_NODE_TYPES
      case NodeTypePP: {
	NodePP* n = static_cast<NodePP*>(node);
	if (n->prefixLength < maxPrefixLength) {
	  for (unsigned pos=0; pos<n->prefixLength; pos++)
	    if (key[depth+pos] != n->prefix()[pos])
	      return NULL;
	} else
	  skippedPrefix=true;
	depth += n->prefixLength;
      }
#endif
      }

      node=*findChild(node,key[depth]);
      depth++;
    }
    return NULL;
  }

  inline Node* lookupPessimistic(Node* node,uint8_t key[],unsigned keyLength,unsigned depth,unsigned maxKeyLength) {
    // Find the node with a matching key, alternative pessimistic version

    while (node!=NULL) {
      if (isLeaf(node)) {
	if (leafMatches(node,key,keyLength,depth,maxKeyLength))
	  return node;
	return NULL;
      }

      if (prefixMismatch(node,key,depth,maxKeyLength)!=node->prefixLength)
	return NULL; else
	depth+=node->prefixLength;

      node=*findChild(node,key[depth]);
      depth++;
    }

    return NULL;
  }

  //************************************************************************************************
  //Range Query Support
  //huanchen
  //************************************************************************************************

  inline Node* minimum_recordPath(Node* node) {
    //std::cout << "minimum_recordPath\n";
    if (!node)
      return NULL;

    //std::cout << "hz1\n";

    if (isLeaf(node))
      return node;

    //std::cout << "hz2\n";

    NodeCursor nc;
    nc.node = node;
    nc.cursor = 0;
    node_stack.push_back(nc);

    switch (node->type) {
    case NodeType4: {
      //std::cout << "hz3 Node4\n";
      Node4* n=static_cast<Node4*>(node);
      return minimum_recordPath(n->child[0]);
    }
    case NodeType16: {
      //std::cout << "hz3 Node16\n";
      Node16* n=static_cast<Node16*>(node);
      return minimum_recordPath(n->child[0]);
    }
    case NodeType48: {
      //std::cout << "hz3 Node48\n";
      Node48* n=static_cast<Node48*>(node);
      unsigned pos=0;
      while (n->childIndex[pos]==emptyMarker)
	pos++;
      node_stack.back().cursor = pos;
      return minimum_recordPath(n->child[n->childIndex[pos]]);
    }
    case NodeType256: {
      //std::cout << "hz3 Node256\n";
      Node256* n=static_cast<Node256*>(node);
      unsigned pos=0;
      while (!n->child[pos])
	pos++;
      node_stack.back().cursor = pos;
      return minimum_recordPath(n->child[pos]);
    }
    }
    throw; // Unreachable
  }

  inline Node* findChild_recordPath(Node* n,uint8_t keyByte) {
    //std::cout << "findChild_recordPath\n";
    NodeCursor nc;
    nc.node = n;
    switch (n->type) {
    case NodeType4: {
      //std::cout << "Node4\n";
      Node4* node=static_cast<Node4*>(n);
      for (unsigned i=0;i<node->count;i++) {
	if (node->key[i]>=keyByte) {
	  nc.cursor = i;
	  node_stack.push_back(nc);
	  if (node->key[i]==keyByte)
	    return node->child[i];
	  else
	    return minimum_recordPath(node->child[i]);
	}
      }
      node_stack.pop_back();
      return minimum_recordPath(nextSlot());
    }
    case NodeType16: {
      //std::cout << "Node16\n";
      Node16* node=static_cast<Node16*>(n);
      for (unsigned i=0;i<node->count;i++) {
	if (node->key[i]>=keyByte) {
	  nc.cursor = i;
	  node_stack.push_back(nc);
	  if (node->key[i]==keyByte)
	    return node->child[i];
	  else
	    return minimum_recordPath(node->child[i]);
	}
      }
      node_stack.pop_back();
      return minimum_recordPath(nextSlot());
    }
    case NodeType48: {
      //std::cout << "Node48\n";
      Node48* node=static_cast<Node48*>(n);
      if (node->childIndex[keyByte]!=emptyMarker) {
	nc.cursor = keyByte;
	node_stack.push_back(nc);
	return node->child[node->childIndex[keyByte]]; 
      }
      else {
	for (unsigned i=keyByte; i<256; i++) {
	  if (node->childIndex[i]!=emptyMarker) {
	    nc.cursor = i;
	    node_stack.push_back(nc);
	    return node->child[node->childIndex[i]]; 
	  }	  
	}
	node_stack.pop_back();
	return minimum_recordPath(nextSlot());
      }
    }
    case NodeType256: {
      //std::cout << "Node256\n";
      Node256* node=static_cast<Node256*>(n);
      if (node->child[keyByte]!=NULL) {
	nc.cursor = keyByte;
	node_stack.push_back(nc);
	return node->child[keyByte];
      }
      else {
	for (unsigned i=keyByte; i<256; i++) {
	  if (node->child[i]!=NULL) {
	    nc.cursor = i;
	    node_stack.push_back(nc);
	    return node->child[i]; 
	  }	  
	}
	node_stack.pop_back();
	return minimum_recordPath(nextSlot());
      }
    }
    }
    throw; // Unreachable
  }

  inline int CompareToPrefix(Node* node,uint8_t key[],unsigned depth,unsigned maxKeyLength) {
    //std::cout << "CompareToPrefix\n";
    unsigned pos;
    if (node->prefixLength>maxPrefixLength) {
      for (pos=0;pos<maxPrefixLength;pos++) {
	if (key[depth+pos]!=node->prefix[pos]) {
	  if (key[depth+pos]>node->prefix[pos])
	    return 1;
	  else
	    return -1;
	}
      }
      uint8_t minKey[maxKeyLength];
      loadKey(getLeafValue(minimum(node)),minKey);
      for (;pos<node->prefixLength;pos++) {
	if (key[depth+pos]!=minKey[depth+pos]) {
	  if (key[depth+pos]>minKey[depth+pos])
	    return 1;
	  else
	    return -1;
	}
      }
    } else {
      for (pos=0;pos<node->prefixLength;pos++) {
	if (key[depth+pos]!=node->prefix[pos]) {
	  if (key[depth+pos]>node->prefix[pos])
	    return 1;
	  else
	    return -1;
	}
      }
    }
    return 0;
  }

  inline Node* lower_bound(Node* node,uint8_t key[],unsigned keyLength,unsigned depth,unsigned maxKeyLength) {
    //std::cout << "lower_bound\n";
    node_stack.clear();
    while (node!=NULL) {
      if (isLeaf(node)) {
	return node;
      }

      int ctp = CompareToPrefix(node,key,depth,maxKeyLength);
      depth+=node->prefixLength;

      if (ctp > 0) {
	node_stack.pop_back();
	return minimum_recordPath(nextSlot());
      }
      else if (ctp < 0) {
	return minimum_recordPath(node);
      }

      node = findChild_recordPath(node,key[depth]);
      depth++;
    }

    return NULL;
  }

  inline Node* nextSlot() {
    while (!node_stack.empty()) {
      Node* n = node_stack.back().node;
      uint16_t cursor = node_stack.back().cursor;
      cursor++;
      node_stack.back().cursor = cursor;
      switch (n->type) {
      case NodeType4: {
	Node4* node=static_cast<Node4*>(n);
	if (cursor < node->count)
	  return node->child[cursor];
	break;
      }
      case NodeType16: {
	Node16* node=static_cast<Node16*>(n);
	if (cursor < node->count)
	  return node->child[cursor];
	break;
      }
      case NodeType48: {
	Node48* node=static_cast<Node48*>(n);
	for (unsigned i=cursor; i<256; i++)
	  if (node->childIndex[i]!=emptyMarker) {
	    node_stack.back().cursor = i;
	    return node->child[node->childIndex[i]];
	  }
	break;
      }
      case NodeType256: {
	Node256* node=static_cast<Node256*>(n);
	for (unsigned i=cursor; i<256; i++)
	  if (node->child[i]!=nullNode) {
	    node_stack.back().cursor = i;
	    return node->child[i]; 
	  }
	break;
      }
      }
      node_stack.pop_back();
    }
    return NULL;
  }

  inline Node* nextLeaf() {
    return minimum_recordPath(nextSlot());
  }

  //************************************************************************************************


  // Forward references
  //void insertNode4(Node4* node,Node** nodeRef,uint8_t keyByte,Node* child);
  //void insertNode16(Node16* node,Node** nodeRef,uint8_t keyByte,Node* child);
  //void insertNode48(Node48* node,Node** nodeRef,uint8_t keyByte,Node* child);
  //void insertNode256(Node256* node,Node** nodeRef,uint8_t keyByte,Node* child);

  unsigned min(unsigned a,unsigned b) {
    // Helper function
    return (a<b)?a:b;
  }

  void copyPrefix(Node* src,Node* dst) {
    // Helper function that copies the prefix from the source to the destination node
    dst->prefixLength=src->prefixLength;
    memcpy(dst->prefix,src->prefix,min(src->prefixLength,maxPrefixLength));
  }

  inline void insert(Node* node,Node** nodeRef,uint8_t key[],unsigned depth,uintptr_t value,unsigned maxKeyLength) {
    // Insert the leaf value into the tree

    //std::cout << (uint64_t)key[depth] << "\t" << depth << "\t" << value << "\n";

    if (node==NULL) {
      *nodeRef=makeLeaf(value);
      return;
    }

    if (isLeaf(node)) {
      // Replace leaf with Node4 and store both leaves in it
      uint8_t existingKey[maxKeyLength];
      loadKey(getLeafValue(node),existingKey);
      unsigned newPrefixLength=0;
      //huanchen
      while ((depth + newPrefixLength < maxKeyLength) && (existingKey[depth+newPrefixLength]==key[depth+newPrefixLength]))
	newPrefixLength++;
      if (depth + newPrefixLength >= maxKeyLength)
	return;
      //while (existingKey[depth+newPrefixLength]==key[depth+newPrefixLength])
      //newPrefixLength++;

      Node4* newNode=new Node4();
      memory += sizeof(Node4); //h
      node4_count++; //h
      newNode->prefixLength=newPrefixLength;
      memcpy(newNode->prefix,key+depth,min(newPrefixLength,maxPrefixLength));
      *nodeRef=newNode;

      //std::cout << "newnode prefix = " << newPrefixLength << "\n";

      insertNode4(newNode,nodeRef,existingKey[depth+newPrefixLength],node);
      insertNode4(newNode,nodeRef,key[depth+newPrefixLength],makeLeaf(value));
      return;
    }

    // Handle prefix of inner node
    if (node->prefixLength) {
      //std::cout << "prefix = " << node->prefixLength << "\n";
      unsigned mismatchPos=prefixMismatch(node,key,depth,maxKeyLength);
      if (mismatchPos!=node->prefixLength) {
	// Prefix differs, create new node
	Node4* newNode=new Node4();
	memory += sizeof(Node4); //h
	node4_count++; //h
	*nodeRef=newNode;
	newNode->prefixLength=mismatchPos;
	memcpy(newNode->prefix,node->prefix,min(mismatchPos,maxPrefixLength));
	// Break up prefix
	if (node->prefixLength<maxPrefixLength) {
	  insertNode4(newNode,nodeRef,node->prefix[mismatchPos],node);
	  node->prefixLength-=(mismatchPos+1);
	  memmove(node->prefix,node->prefix+mismatchPos+1,min(node->prefixLength,maxPrefixLength));
	} else {
	  node->prefixLength-=(mismatchPos+1);
	  uint8_t minKey[maxKeyLength];
	  loadKey(getLeafValue(minimum(node)),minKey);
	  insertNode4(newNode,nodeRef,minKey[depth+mismatchPos],node);
	  memmove(node->prefix,minKey+depth+mismatchPos+1,min(node->prefixLength,maxPrefixLength));
	}
	insertNode4(newNode,nodeRef,key[depth+mismatchPos],makeLeaf(value));
	return;
      }
      depth+=node->prefixLength;
    }

    // Recurse
    Node** child=findChild(node,key[depth]);
    if (*child) {
      insert(*child,child,key,depth+1,value,maxKeyLength);
      return;
    }

    // Insert leaf into inner node
    Node* newNode=makeLeaf(value);
    switch (node->type) {
    case NodeType4: insertNode4(static_cast<Node4*>(node),nodeRef,key[depth],newNode); break;
    case NodeType16: insertNode16(static_cast<Node16*>(node),nodeRef,key[depth],newNode); break;
    case NodeType48: insertNode48(static_cast<Node48*>(node),nodeRef,key[depth],newNode); break;
    case NodeType256: insertNode256(static_cast<Node256*>(node),nodeRef,key[depth],newNode); break;
    }
  }

  inline void insertNode4(Node4* node,Node** nodeRef,uint8_t keyByte,Node* child) {
    // Insert leaf into inner node
    if (node->count<4) {
      // Insert element
      unsigned pos;
      for (pos=0;(pos<node->count)&&(node->key[pos]<keyByte);pos++);
      memmove(node->key+pos+1,node->key+pos,node->count-pos);
      memmove(node->child+pos+1,node->child+pos,(node->count-pos)*sizeof(uintptr_t));
      node->key[pos]=keyByte;
      node->child[pos]=child;
      node->count++;
    } else {
      // Grow to Node16
      Node16* newNode=new Node16();
      memory += sizeof(Node16); //h
      node16_count++; //h
      *nodeRef=newNode;
      newNode->count=4;
      copyPrefix(node,newNode);
      for (unsigned i=0;i<4;i++)
	newNode->key[i]=flipSign(node->key[i]);
      memcpy(newNode->child,node->child,node->count*sizeof(uintptr_t));
      delete node;
      memory -= sizeof(Node4); //h
      node4_count--; //h
      return insertNode16(newNode,nodeRef,keyByte,child);
    }
  }

  inline void insertNode16(Node16* node,Node** nodeRef,uint8_t keyByte,Node* child) {
    // Insert leaf into inner node
    if (node->count<16) {
      // Insert element
      uint8_t keyByteFlipped=flipSign(keyByte);
      __m128i cmp=_mm_cmplt_epi8(_mm_set1_epi8(keyByteFlipped),_mm_loadu_si128(reinterpret_cast<__m128i*>(node->key)));
      uint16_t bitfield=_mm_movemask_epi8(cmp)&(0xFFFF>>(16-node->count));
      unsigned pos=bitfield?ctz(bitfield):node->count;
      memmove(node->key+pos+1,node->key+pos,node->count-pos);
      memmove(node->child+pos+1,node->child+pos,(node->count-pos)*sizeof(uintptr_t));
      node->key[pos]=keyByteFlipped;
      node->child[pos]=child;
      node->count++;
    } else {
      // Grow to Node48
      Node48* newNode=new Node48();
      memory += sizeof(Node48); //h
      node48_count++; //h
      *nodeRef=newNode;
      memcpy(newNode->child,node->child,node->count*sizeof(uintptr_t));
      for (unsigned i=0;i<node->count;i++)
	newNode->childIndex[flipSign(node->key[i])]=i;
      copyPrefix(node,newNode);
      newNode->count=node->count;
      delete node;
      memory -= sizeof(Node16); //h
      node16_count--; //h
      return insertNode48(newNode,nodeRef,keyByte,child);
    }
  }

  inline void insertNode48(Node48* node,Node** nodeRef,uint8_t keyByte,Node* child) {
    // Insert leaf into inner node
    if (node->count<48) {
      // Insert element
      unsigned pos=node->count;
      if (node->child[pos])
	for (pos=0;node->child[pos]!=NULL;pos++);
      node->child[pos]=child;
      node->childIndex[keyByte]=pos;
      node->count++;
    } else {
      // Grow to Node256
      Node256* newNode=new Node256();
      memory += sizeof(Node256); //h
      node256_count++; //h
      for (unsigned i=0;i<256;i++)
	if (node->childIndex[i]!=48)
	  newNode->child[i]=node->child[node->childIndex[i]];
      newNode->count=node->count;
      copyPrefix(node,newNode);
      *nodeRef=newNode;
      delete node;
      memory -= sizeof(Node48); //h
      node48_count--; //h
      return insertNode256(newNode,nodeRef,keyByte,child);
    }
  }

  inline void insertNode256(Node256* node,Node** nodeRef,uint8_t keyByte,Node* child) {
    // Insert leaf into inner node
    node->count++;
    node->child[keyByte]=child;
  }

  // Forward references
  //void eraseNode4(Node4* node,Node** nodeRef,Node** leafPlace);
  //void eraseNode16(Node16* node,Node** nodeRef,Node** leafPlace);
  //void eraseNode48(Node48* node,Node** nodeRef,uint8_t keyByte);
  //void eraseNode256(Node256* node,Node** nodeRef,uint8_t keyByte);

  inline void erase(Node* node,Node** nodeRef,uint8_t key[],unsigned keyLength,unsigned depth,unsigned maxKeyLength) {
    // Delete a leaf from a tree

    if (!node)
      return;

    if (isLeaf(node)) {
      // Make sure we have the right leaf
      if (leafMatches(node,key,keyLength,depth,maxKeyLength))
	*nodeRef=NULL;
      return;
    }

    // Handle prefix
    if (node->prefixLength) {
      if (prefixMismatch(node,key,depth,maxKeyLength)!=node->prefixLength)
	return;
      depth+=node->prefixLength;
    }

    Node** child=findChild(node,key[depth]);
    if (isLeaf(*child)&&leafMatches(*child,key,keyLength,depth,maxKeyLength)) {
      // Leaf found, delete it in inner node
      switch (node->type) {
      case NodeType4: eraseNode4(static_cast<Node4*>(node),nodeRef,child); break;
      case NodeType16: eraseNode16(static_cast<Node16*>(node),nodeRef,child); break;
      case NodeType48: eraseNode48(static_cast<Node48*>(node),nodeRef,key[depth]); break;
      case NodeType256: eraseNode256(static_cast<Node256*>(node),nodeRef,key[depth]); break;
      }
    } else {
      //Recurse
      erase(*child,child,key,keyLength,depth+1,maxKeyLength);
    }
  }

  inline void eraseNode4(Node4* node,Node** nodeRef,Node** leafPlace) {
    // Delete leaf from inner node
    unsigned pos=leafPlace-node->child;
    memmove(node->key+pos,node->key+pos+1,node->count-pos-1);
    memmove(node->child+pos,node->child+pos+1,(node->count-pos-1)*sizeof(uintptr_t));
    node->count--;

    if (node->count==1) {
      // Get rid of one-way node
      Node* child=node->child[0];
      if (!isLeaf(child)) {
	// Concantenate prefixes
	unsigned l1=node->prefixLength;
	if (l1<maxPrefixLength) {
	  node->prefix[l1]=node->key[0];
	  l1++;
	}
	if (l1<maxPrefixLength) {
	  unsigned l2=min(child->prefixLength,maxPrefixLength-l1);
	  memcpy(node->prefix+l1,child->prefix,l2);
	  l1+=l2;
	}
	// Store concantenated prefix
	memcpy(child->prefix,node->prefix,min(l1,maxPrefixLength));
	child->prefixLength+=node->prefixLength+1;
      }
      *nodeRef=child;
      delete node;
      memory -= sizeof(Node4); //h
      node4_count--; //h
    }
  }

  inline void eraseNode16(Node16* node,Node** nodeRef,Node** leafPlace) {
    // Delete leaf from inner node
    unsigned pos=leafPlace-node->child;
    memmove(node->key+pos,node->key+pos+1,node->count-pos-1);
    memmove(node->child+pos,node->child+pos+1,(node->count-pos-1)*sizeof(uintptr_t));
    node->count--;

    if (node->count==3) {
      // Shrink to Node4
      Node4* newNode=new Node4();
      memory += sizeof(Node4); //h
      node4_count++; //h
      newNode->count=node->count;
      copyPrefix(node,newNode);
      for (unsigned i=0;i<4;i++)
	newNode->key[i]=flipSign(node->key[i]);
      memcpy(newNode->child,node->child,sizeof(uintptr_t)*4);
      *nodeRef=newNode;
      delete node;
      memory -= sizeof(Node16); //h
      node16_count--; //h
    }
  }

  inline void eraseNode48(Node48* node,Node** nodeRef,uint8_t keyByte) {
    // Delete leaf from inner node
    node->child[node->childIndex[keyByte]]=NULL;
    node->childIndex[keyByte]=emptyMarker;
    node->count--;

    if (node->count==12) {
      // Shrink to Node16
      Node16 *newNode=new Node16();
      memory += sizeof(Node16); //h
      node16_count++; //h
      *nodeRef=newNode;
      copyPrefix(node,newNode);
      for (unsigned b=0;b<256;b++) {
	if (node->childIndex[b]!=emptyMarker) {
	  newNode->key[newNode->count]=flipSign(b);
	  newNode->child[newNode->count]=node->child[node->childIndex[b]];
	  newNode->count++;
	}
      }
      delete node;
      memory -= sizeof(Node48); //h
      node48_count--; //h
    }
  }

  inline void eraseNode256(Node256* node,Node** nodeRef,uint8_t keyByte) {
    // Delete leaf from inner node
    node->child[keyByte]=NULL;
    node->count--;

    if (node->count==37) {
      // Shrink to Node48
      Node48 *newNode=new Node48();
      memory += sizeof(Node48); //h
      node48_count++; //h
      *nodeRef=newNode;
      copyPrefix(node,newNode);
      for (unsigned b=0;b<256;b++) {
	if (node->child[b]) {
	  newNode->childIndex[b]=newNode->count;
	  newNode->child[newNode->count]=node->child[b];
	  newNode->count++;
	}
      }
      delete node;
      memory -= sizeof(Node256); //h
      node256_count--; //h
    }
  }

  //huanchen
  inline void tree_info(Node* r) {
    uint64_t num_items = 0;
    uint64_t inner_size = 0;
    uint64_t leaf_size = 0;
    uint64_t num_no_prefix = 0;
    uint64_t num_prefix_1 = 0;
    uint64_t num_prefix_2 = 0;
    uint64_t num_prefix_3 = 0;
    uint64_t num_prefix_4 = 0;
    uint64_t num_prefix_5 = 0;
    uint64_t num_prefix_6 = 0;
    uint64_t num_prefix_7 = 0;
    uint64_t num_prefix_8 = 0;
    uint64_t num_prefix_9 = 0;
    uint64_t num_prefix_large = 0;
    std::deque<Node*> node_queue;
    node_queue.push_back(r);
    while (!node_queue.empty()) {
      Node* n = node_queue.front();
      if (!isLeaf(n)) {
	num_items += n->count;

	if (isInner(n))
	  inner_size += node_size(n);
	else
	  leaf_size += node_size(n);

	if (n->prefixLength == 0)
	  num_no_prefix++;
	else if (n->prefixLength == 1)
	  num_prefix_1++;
	else if (n->prefixLength == 2)
	  num_prefix_2++;
	else if (n->prefixLength == 3)
	  num_prefix_3++;
	else if (n->prefixLength == 4)
	  num_prefix_4++;
	else if (n->prefixLength == 5)
	  num_prefix_5++;
	else if (n->prefixLength == 6)
	  num_prefix_6++;
	else if (n->prefixLength == 7)
	  num_prefix_7++;
	else if (n->prefixLength == 8)
	  num_prefix_8++;
	else if (n->prefixLength == 9)
	  num_prefix_9++;
	else
	  num_prefix_large++;

	switch (n->type) {
	case NodeType4: {
	  Node4* node = static_cast<Node4*>(n);
	  for (unsigned i = 0; i < node->count; i++)
	    node_queue.push_back(node->child[i]);
	  break;
	}
	case NodeType16: {
	  Node16* node = static_cast<Node16*>(n);
	  for (unsigned i = 0; i < node->count; i++)
	    node_queue.push_back(node->child[i]);
	  break;
	}
	case NodeType48: {
	  Node48* node = static_cast<Node48*>(n);
	  for (unsigned i = 0; i < 256; i++)
	    if (node->childIndex[i] != emptyMarker)
	      node_queue.push_back(node->child[node->childIndex[i]]);
	  break;
	}
	case NodeType256: {
	  Node256* node = static_cast<Node256*>(n);
	  for (unsigned i = 0; i < 256; i++)
	    if (node->child[i])
	      node_queue.push_back(node->child[i]);
	  break;
	}
	}
      }
      node_queue.pop_front();
    }
    std::cout << "num items = " << num_items << "\n";
    std::cout << "inner size = " << inner_size << "\n";
    std::cout << "leaf size = " << leaf_size << "\n";
    std::cout << "num prefix 0 = " << num_no_prefix << "\n";
    std::cout << "num prefix 1 = " << num_prefix_1 << "\n";
    std::cout << "num prefix 2 = " << num_prefix_2 << "\n";
    std::cout << "num prefix 3 = " << num_prefix_3 << "\n";
    std::cout << "num prefix 4 = " << num_prefix_4 << "\n";
    std::cout << "num prefix 5 = " << num_prefix_5 << "\n";
    std::cout << "num prefix 6 = " << num_prefix_6 << "\n";
    std::cout << "num prefix 7 = " << num_prefix_7 << "\n";
    std::cout << "num prefix 8 = " << num_prefix_8 << "\n";
    std::cout << "num prefix 9 = " << num_prefix_9 << "\n";
    std::cout << "num prefix large = " << num_prefix_large << "\n";
  }


  //huanchen-static
  inline void Node4_to_NodeD(Node4* n, NodeD* n_static) {
    for (unsigned i = 0; i < n->count; i++) {
      //n_static->key()[i] = n->key[i];
      n_static->key()[i] = flipSign(n->key[i]);
      n_static->child()[i] = (NodeStatic*)n->child[i];
    }
  }

  inline void Node16_to_NodeD(Node16* n, NodeD* n_static) {
    for (unsigned i = 0; i < n->count; i++) {
      //n_static->key()[i] = flipSign(n->key[i]);
      n_static->key()[i] = n->key[i];
      n_static->child()[i] = (NodeStatic*)n->child[i];
    }
  }

  inline void Node48_to_NodeD(Node48* n, NodeD* n_static) {
    unsigned c = 0;
    for (uint8_t i = 0; i < 255; i++) {
      if (n->childIndex[i] != emptyMarker) {
	//n_static->key()[c] = i;
	n_static->key()[c] = flipSign(i);
	n_static->child()[c] = (NodeStatic*)n->child[n->childIndex[i]];
	c++;
      }
    }
    if (n->childIndex[255] != emptyMarker) {
      //n_static->key()[c] = 255;
      n_static->key()[c] = flipSign(255);
      n_static->child()[c] = (NodeStatic*)n->child[n->childIndex[255]];
    }
  }

  inline void Node256_to_NodeD(Node256* n, NodeD* n_static) {
    unsigned int c = 0;
    for (uint8_t i = 0; i < 255; i++) {
      if (n->child[i]) {
	//n_static->key()[c] = i;
	n_static->key()[c] = flipSign(i);
	n_static->child()[c] = (NodeStatic*)n->child[i];
	c++;
      }
    }
    if (n->child[255]) {
      //n_static->key()[c] = 255;
      n_static->key()[c] = flipSign(255);
      n_static->child()[c] = (NodeStatic*)n->child[255];
    }
  }

  inline void Node_to_NodeD(Node* n, NodeD* n_static) {
    n_static->count = n->count;

    if (n->type == NodeType4)
      Node4_to_NodeD(static_cast<Node4*>(n), n_static);
    else if (n->type == NodeType16)
      Node16_to_NodeD(static_cast<Node16*>(n), n_static);
    else if (n->type == NodeType48)
      Node48_to_NodeD(static_cast<Node48*>(n), n_static);
    else if (n->type == NodeType256)
      Node256_to_NodeD(static_cast<Node256*>(n), n_static);
  }


  //huanchen-static
  inline void Node4_to_NodeDP(Node4* n, NodeDP* n_static) {
    for (unsigned i = 0; i < n->count; i++) {
      //n_static->key()[i] = n->key[i];
      n_static->key()[i] = flipSign(n->key[i]);
      n_static->child()[i] = (NodeStatic*)n->child[i];
    }
  }

  inline void Node16_to_NodeDP(Node16* n, NodeDP* n_static) {
    for (unsigned i = 0; i < n->count; i++) {
      //n_static->key()[i] = flipSign(n->key[i]);
      n_static->key()[i] = n->key[i];
      n_static->child()[i] = (NodeStatic*)n->child[i];
    }
  }

  inline void Node48_to_NodeDP(Node48* n, NodeDP* n_static) {
    unsigned c = 0;
    for (uint8_t i = 0; i < 255; i++) {
      if (n->childIndex[i] != emptyMarker) {
	//n_static->key()[c] = i;
	n_static->key()[c] = flipSign(i);
	n_static->child()[c] = (NodeStatic*)n->child[n->childIndex[i]];
	c++;
      }
    }
    if (n->childIndex[255] != emptyMarker) {
      //n_static->key()[c] = 255;
      n_static->key()[c] = flipSign(255);
      n_static->child()[c] = (NodeStatic*)n->child[n->childIndex[255]];
    }
  }

  inline void Node256_to_NodeDP(Node256* n, NodeDP* n_static) {
    unsigned int c = 0;
    for (uint8_t i = 0; i < 255; i++) {
      if (n->child[i]) {
	//n_static->key()[c] = i;
	n_static->key()[c] = flipSign(i);
	n_static->child()[c] = (NodeStatic*)n->child[i];
	c++;
      }
    }
    if (n->child[255]) {
      //n_static->key()[c] = 255;
      n_static->key()[c] = flipSign(255);
      n_static->child()[c] = (NodeStatic*)n->child[255];
    }
  }

  inline void Node_to_NodeDP(Node* n, NodeDP* n_static) {
    n_static->count = n->count;
    n_static->prefixLength = n->prefixLength;
    for (unsigned i = 0; i < maxPrefixLength; i++)
      n_static->prefix()[i] = n->prefix[i];

    if (n->type == NodeType4)
      Node4_to_NodeDP(static_cast<Node4*>(n), n_static);
    else if (n->type == NodeType16)
      Node16_to_NodeDP(static_cast<Node16*>(n), n_static);
    else if (n->type == NodeType48)
      Node48_to_NodeDP(static_cast<Node48*>(n), n_static);
    else if (n->type == NodeType256)
      Node256_to_NodeDP(static_cast<Node256*>(n), n_static);
  }


  //huanchen-static
  inline void Node4_to_NodeF(Node4* n, NodeF* n_static) {
    for (unsigned i = 0; i < n->count; i++)
      n_static->child[n->key[i]] = (NodeStatic*)n->child[i];
  }

  inline void Node16_to_NodeF(Node16* n, NodeF* n_static) {
    for (unsigned i = 0; i < n->count; i++)
      n_static->child[flipSign(n->key[i])] = (NodeStatic*)n->child[i];
  }

  inline void Node48_to_NodeF(Node48* n, NodeF* n_static) {
    for (unsigned i = 0; i < 256; i++)
      if (n->childIndex[i] != emptyMarker)
	n_static->child[i] = (NodeStatic*)n->child[n->childIndex[i]];
  }

  inline void Node256_to_NodeF(Node256* n, NodeF* n_static) {
    for (unsigned i = 0; i < 256; i++)
      n_static->child[i] = (NodeStatic*)n->child[i];
  }

  inline void Node_to_NodeF(Node* n, NodeF* n_static) {
    n_static->count = n->count;

    if (n->type == NodeType4)
      Node4_to_NodeF(static_cast<Node4*>(n), n_static);
    else if (n->type == NodeType16)
      Node16_to_NodeF(static_cast<Node16*>(n), n_static);
    else if (n->type == NodeType48)
      Node48_to_NodeF(static_cast<Node48*>(n), n_static);
    else if (n->type == NodeType256)
      Node256_to_NodeF(static_cast<Node256*>(n), n_static);
  }


  //huanchen-static
  inline void Node4_to_NodeFP(Node4* n, NodeFP* n_static) {
    for (unsigned i = 0; i < n->count; i++)
      n_static->child()[n->key[i]] = (NodeStatic*)n->child[i];
  }

  inline void Node16_to_NodeFP(Node16* n, NodeFP* n_static) {
    for (unsigned i = 0; i < n->count; i++)
      n_static->child()[flipSign(n->key[i])] = (NodeStatic*)n->child[i];
  }

  inline void Node48_to_NodeFP(Node48* n, NodeFP* n_static) {
    for (unsigned i = 0; i < 256; i++)
      if (n->childIndex[i] != emptyMarker)
	n_static->child()[i] = (NodeStatic*)n->child[n->childIndex[i]];
  }

  inline void Node256_to_NodeFP(Node256* n, NodeFP* n_static) {
    for (unsigned i = 0; i < 256; i++)
      n_static->child()[i] = (NodeStatic*)n->child[i];
  }

  inline void Node_to_NodeFP(Node* n, NodeFP* n_static) {
    n_static->count = n->count;
    n_static->prefixLength = n->prefixLength;
    for (unsigned i = 0; i < maxPrefixLength; i++)
      n_static->prefix()[i] = n->prefix[i];

    if (n->type == NodeType4)
      Node4_to_NodeFP(static_cast<Node4*>(n), n_static);
    else if (n->type == NodeType16)
      Node16_to_NodeFP(static_cast<Node16*>(n), n_static);
    else if (n->type == NodeType48)
      Node48_to_NodeFP(static_cast<Node48*>(n), n_static);
    else if (n->type == NodeType256)
      Node256_to_NodeFP(static_cast<Node256*>(n), n_static);
  }

#ifdef MORE_NODE_TYPES
  //huanchen-static
  inline void Node4_to_NodeP(Node4* n, NodeP* n_static) {
    for (uint8_t i = 0; i < n->count; i++) {
      n_static->childIndex[n->key[i]] = i;
      n_static->child[i] = (NodeStatic*)n->child[i];
    }
  }

  inline void Node16_to_NodeP(Node16* n, NodeP* n_static) {
    for (uint8_t i = 0; i < n->count; i++) {
      n_static->childIndex[flipSign(n->key[i])] = i;
      n_static->child[i] = (NodeStatic*)n->child[i];
    }
  }

  inline void Node48_to_NodeP(Node48* n, NodeP* n_static) {
    uint8_t p = 0;
    for (unsigned i = 0; i < 256; i++) {
      if (n->childIndex[i] != emptyMarker) {
	n_static->childIndex[i] = p;
	n_static->child[p] = (NodeStatic*)n->child[n->childIndex[i]];
	p++;
      }
    }
  }

  inline void Node256_to_NodeP(Node256* n, NodeP* n_static) {
    uint8_t p = 0;
    for (unsigned i = 0; i < 256; i++) {
      if (n->child[i]) {
	n_static->childIndex[i] = p;
	n_static->child[p] = (NodeStatic*)n->child[i];
	p++;
      }
    }
  }

  inline void Node_to_NodeP(Node* n, NodeP* n_static) {
    n_static->count = n->count;

    if (n->type == NodeType4)
      Node4_to_NodeP(static_cast<Node4*>(n), n_static);
    else if (n->type == NodeType16)
      Node16_to_NodeP(static_cast<Node16*>(n), n_static);
    else if (n->type == NodeType48)
      Node48_to_NodeP(static_cast<Node48*>(n), n_static);
    else if (n->type == NodeType256)
      Node256_to_NodeP(static_cast<Node256*>(n), n_static);
  }


  //huanchen-static
  inline void Node4_to_NodePP(Node4* n, NodePP* n_static) {
    for (uint8_t i = 0; i < n->count; i++) {
      n_static->childIndex()[n->key[i]] = i;
      n_static->child()[i] = (NodeStatic*)n->child[i];
    }
  }

  inline void Node16_to_NodePP(Node16* n, NodePP* n_static) {
    for (uint8_t i = 0; i < n->count; i++) {
      n_static->childIndex()[flipSign(n->key[i])] = i;
      n_static->child()[i] = (NodeStatic*)n->child[i];
    }
  }

  inline void Node48_to_NodePP(Node48* n, NodePP* n_static) {
    uint8_t p = 0;
    for (unsigned i = 0; i < 256; i++) {
      if (n->childIndex[i] != emptyMarker) {
	n_static->childIndex()[i] = p;
	n_static->child()[p] = (NodeStatic*)n->child[n->childIndex[i]];
	p++;
      }
    }
  }

  inline void Node256_to_NodePP(Node256* n, NodePP* n_static) {
    uint8_t p = 0;
    for (unsigned i = 0; i < 256; i++) {
      if (n->child[i]) {
	n_static->childIndex()[i] = p;
	n_static->child()[p] = (NodeStatic*)n->child[i];
	p++;
      }
    }
  }

  inline void Node_to_NodePP(Node* n, NodePP* n_static) {
    n_static->count = n->count;
    n_static->prefixLength = n->prefixLength;
    for (unsigned i = 0; i < maxPrefixLength; i++)
      n_static->prefix()[i] = n->prefix[i];

    if (n->type == NodeType4)
      Node4_to_NodePP(static_cast<Node4*>(n), n_static);
    else if (n->type == NodeType16)
      Node16_to_NodePP(static_cast<Node16*>(n), n_static);
    else if (n->type == NodeType48)
      Node48_to_NodePP(static_cast<Node48*>(n), n_static);
    else if (n->type == NodeType256)
      Node256_to_NodePP(static_cast<Node256*>(n), n_static);
  }
#endif

  inline size_t node_size(Node* n) {
    switch (n->type) {
    case NodeType4: {
      return sizeof(Node4);
    }
    case NodeType16: {
      return sizeof(Node16);
    }
    case NodeType48: {
      return sizeof(Node48);
    }
    case NodeType256: {
      return sizeof(Node256);
    }
    }
    return 0;
  }

  inline size_t node_size(NodeStatic* n) {
    switch (n->type) {
    case NodeTypeD: {
      NodeD* node = static_cast<NodeD*>(n);
      return sizeof(NodeD) + node->count * (sizeof(uint8_t) + sizeof(NodeStatic*));
    }
    case NodeTypeDP: {
      NodeDP* node = static_cast<NodeDP*>(n);
      return sizeof(NodeDP) + node->prefixLength * sizeof(uint8_t) + node->count * (sizeof(uint8_t) + sizeof(NodeStatic*));
    }
    case NodeTypeF: {
      return sizeof(NodeF);
    }
    case NodeTypeFP: {
      NodeFP* node = static_cast<NodeFP*>(n);
      return sizeof(NodeFP) + node->prefixLength * sizeof(uint8_t) + 256 * sizeof(NodeStatic*);
    }

#ifdef MORE_NODE_TYPES
    case NodeTypeP: {
      NodeP* node = static_cast<NodeP*>(n);
      return sizeof(NodeP) + node->count * sizeof(NodeStatic*);
    }
    case NodeTypePP: {
      NodePP* node = static_cast<NodePP*>(n);
      return sizeof(NodePP) + node->prefixLength * sizeof(uint8_t) + 256 * sizeof(uint8_t) + node->count * sizeof(NodeStatic*);
    }
#endif
    }
    return 0;
  }


  inline void first_merge() {
    if (!root) return;

    Node* n = root;
    NodeStatic* n_new = NULL;
    NodeStatic* n_new_parent = NULL;
    int parent_pos = -1;

    std::deque<Node*> node_queue;
    std::deque<NodeStatic*> new_node_queue;

    node_queue.push_back(n);
    int count = 0;
    while (!node_queue.empty()) {
      n = node_queue.front();
      if (!isLeaf(n)) {
	if ((n->count > NodeDItemTHold) || isInner(n)) {
	  if (n->prefixLength) {
	    size_t size = sizeof(NodeFP) + n->prefixLength * sizeof(uint8_t) + 256 * sizeof(NodeStatic*);
	    void* ptr = malloc(size);
	    NodeFP* n_static = new(ptr) NodeFP(n->count, n->prefixLength);
	    nodeFP_count++; //h
	    Node_to_NodeFP(n, n_static);
	    n_new = n_static;
	    for (unsigned i = 0; i < 256; i++)
	      if ((n_static->child()[i]) && (!isLeaf(n_static->child()[i])))
		node_queue.push_back((Node*)n_static->child()[i]);
	  }
	  else {
	    size_t size = sizeof(NodeF);
	    void* ptr = malloc(size);
	    NodeF* n_static = new(ptr) NodeF(n->count);
	    nodeF_count++; //h
	    Node_to_NodeF(n, n_static);
	    n_new = n_static;
	    for (unsigned i = 0; i < 256; i++)
	      if ((n_static->child[i]) && (!isLeaf(n_static->child[i])))
		node_queue.push_back((Node*)n_static->child[i]);
	  }
	}

#ifdef MORE_NODE_TYPES
	else if (n->count > NodePItemTHold) {
	  if (n->prefixLength) {
	    size_t size = sizeof(NodePP) + n->prefixLength * sizeof(uint8_t) + 256 * sizeof(uint8_t) + n->count * sizeof(NodeStatic*);
	    void* ptr = malloc(size);
	    NodePP* n_static = new(ptr) NodePP(n->count, n->prefixLength);
	    nodePP_count++; //h
	    Node_to_NodePP(n, n_static);
	    n_new = n_static;
	    for (unsigned i = 0; i < n_static->count; i++)
	      if (!isLeaf(n_static->child()[i]))
		node_queue.push_back((Node*)n_static->child()[i]);
	  }
	  else {
	    size_t size = sizeof(NodeP) + n->count * sizeof(NodeStatic*);
	    void* ptr = malloc(size);
	    NodeP* n_static = new(ptr) NodeP(n->count);
	    nodeP_count++; //h
	    Node_to_NodeP(n, n_static);
	    n_new = n_static;
	    for (unsigned i = 0; i < n_static->count; i++)
	      if (!isLeaf(n_static->child[i]))
		node_queue.push_back((Node*)n_static->child[i]);
	  }
	}
#endif
	else {
	  if (n->prefixLength) {
	    size_t size = sizeof(NodeDP) + n->prefixLength * sizeof(uint8_t) + n->count * (sizeof(uint8_t) + sizeof(NodeStatic*));
	    void* ptr = malloc(size);
	    NodeDP* n_static = new(ptr) NodeDP(n->count, n->prefixLength);
	    nodeDP_count++; //h
	    Node_to_NodeDP(n, n_static);
	    n_new = n_static;
	    for (unsigned i = 0; i < n_static->count; i++)
	      if (!isLeaf(n_static->child()[i]))
		node_queue.push_back((Node*)n_static->child()[i]);
	  }
	  else {
	    size_t size = sizeof(NodeD) + n->count * (sizeof(uint8_t) + sizeof(NodeStatic*));
	    void* ptr = malloc(size);
	    NodeD* n_static = new(ptr) NodeD(n->count);
	    nodeD_count++; //h
	    Node_to_NodeD(n, n_static);
	    n_new = n_static;
	    for (unsigned i = 0; i < n_static->count; i++)
	      if (!isLeaf(n_static->child()[i]))
		node_queue.push_back((Node*)n_static->child()[i]);
	  }
	}

	static_memory += node_size(n_new);
	new_node_queue.push_back(n_new);

	bool next_parent = false;

	if (n_new_parent) {

	  if (n_new_parent->type == NodeTypeD) {
	    NodeD* node = static_cast<NodeD*>(n_new_parent);
	    node->child()[parent_pos] = n_new;
	  }
	  else if (n_new_parent->type == NodeTypeDP) {
	    NodeDP* node = static_cast<NodeDP*>(n_new_parent);
	    node->child()[parent_pos] = n_new;
	  }
	  else if (n_new_parent->type == NodeTypeF) {
	    NodeF* node = static_cast<NodeF*>(n_new_parent);
	    node->child[parent_pos] = n_new;
	  }
	  else if (n_new_parent->type == NodeTypeFP) {
	    NodeFP* node = static_cast<NodeFP*>(n_new_parent);
	    node->child()[parent_pos] = n_new;
	  }

#ifdef MORE_NODE_TYPES
	  else if (n_new_parent->type == NodeTypeP) {
	    NodeP* node = static_cast<NodeP*>(n_new_parent);
	    node->child[parent_pos] = n_new;
	  }
	  else if (n_new_parent->type == NodeTypePP) {
	    NodePP* node = static_cast<NodePP*>(n_new_parent);
	    node->child()[parent_pos] = n_new;
	  }
#endif
	  else {
	    std::cout << "Node Type Error1!\t" << (uint64_t)n_new_parent->type << "\n";
	    break;
	  }
	}
	else {
	  n_new_parent = new_node_queue.front();
	  static_root = new_node_queue.front();
	}

	do {
	  next_parent = false;

	  if (n_new_parent->type == NodeTypeD) {
	    NodeD* node = static_cast<NodeD*>(n_new_parent);
	    do {
	      parent_pos++;
	      if (parent_pos >= node->count)
		next_parent = true;
	    } while ((parent_pos < node->count) && isLeaf(node->child()[parent_pos]));
	  }
	  else if (n_new_parent->type == NodeTypeDP) {
	    NodeDP* node = static_cast<NodeDP*>(n_new_parent);
	    do {
	      parent_pos++;
	      if (parent_pos >= node->count)
		next_parent = true;
	    } while ((parent_pos < node->count) && isLeaf(node->child()[parent_pos]));
	  }
	  else if (n_new_parent->type == NodeTypeF) {
	    NodeF* node = static_cast<NodeF*>(n_new_parent);
	    do {
	      parent_pos++;
	      if (parent_pos >= 256)
		next_parent = true;
	    } while ((parent_pos < 256) && (!node->child[parent_pos] || isLeaf(node->child[parent_pos])));
	  }
	  else if (n_new_parent->type == NodeTypeFP) {
	    NodeFP* node = static_cast<NodeFP*>(n_new_parent);
	    do {
	      parent_pos++;
	      if (parent_pos >= 256)
		next_parent = true;
	    } while ((parent_pos < 256) && (!node->child()[parent_pos] || isLeaf(node->child()[parent_pos])));
	  }

#ifdef MORE_NODE_TYPES
	  else if (n_new_parent->type == NodeTypeP) {
	    NodeP* node = static_cast<NodeP*>(n_new_parent);
	    do {
	      parent_pos++;
	      if (parent_pos >= node->count)
		next_parent = true;
	    } while ((parent_pos < node->count) && isLeaf(node->child[parent_pos]));
	  }
	  else if (n_new_parent->type == NodeTypePP) {
	    NodePP* node = static_cast<NodePP*>(n_new_parent);
	    do {
	      parent_pos++;
	      if (parent_pos >= node->count)
		next_parent = true;
	    } while ((parent_pos < node->count) && isLeaf(node->child()[parent_pos]));
	  }
#endif
	  else {
	    std::cout << "Node Type Error2!\t" << (uint64_t)n_new_parent->type << "\n";
	    break;
	  }

	  if (next_parent) {
	    new_node_queue.pop_front();
	    if (!new_node_queue.empty())
	      n_new_parent = new_node_queue.front();
	    else
	      next_parent = false;
	    parent_pos = -1;
	  }
	} while (next_parent);

	delete node_queue.front();
	node_queue.pop_front();
      }
    }
  }


  void merge_trees() {
    if (!MERGE)
      return;
    static_memory = 0;
    if (!static_root)
      first_merge();
    //root = NULL;
    memory = 0;
  }

public:
  hybridART()
    : root(NULL), static_root(NULL), memory(0), static_memory(0),
    node4_count(0), node16_count(0), node48_count(0), node256_count(0), nodeD_count(0), nodeDP_count(0), nodeF_count(0), nodeFP_count(0), nodeP_count(0), nodePP_count(0)
  { }

  hybridART(Node* r, NodeStatic* sr)
    : root(r), static_root(sr), memory(0), static_memory(0),
    node4_count(0), node16_count(0), node48_count(0), node256_count(0), nodeD_count(0), nodeDP_count(0), nodeF_count(0), nodeFP_count(0), nodeP_count(0), nodePP_count(0)
  { }

  void insert(uint8_t key[], unsigned depth, uintptr_t value, unsigned maxKeyLength) {
    insert(root, &root, key, depth, value, maxKeyLength);
  }

  void insert(uint8_t key[], uintptr_t value, unsigned maxKeyLength) {
    insert(root, &root, key, 0, value, maxKeyLength);
  }
  /*
  uintptr_t lookup(uint8_t key[], unsigned keyLength, unsigned depth, unsigned maxKeyLength) {
    Node* leaf = lookup(root, key, keyLength, depth, maxKeyLength);
    if (isLeaf(leaf))
      return getLeafValue(leaf);
    return (uintptr_t)0;
  }

  uint64_t lookup(uint8_t key[], unsigned keyLength, unsigned maxKeyLength) {
    Node* leaf = lookup(root, key, keyLength, 0, maxKeyLength);
    if (isLeaf(leaf))
      return getLeafValue(leaf);
    return (uint64_t)0;
  }
  */
  /*
  uint64_t lookup(uint8_t key[], unsigned keyLength, unsigned maxKeyLength) {
    Node* leaf = lookup(root, key, keyLength, 0, maxKeyLength);
    if (!leaf) {
      NodeStatic* leaf_static = lookup(static_root, key, keyLength, 0, maxKeyLength);
      if (isLeaf(leaf_static))
	return getLeafValue(leaf_static);
      return (uint64_t)0;
    }
    if (isLeaf(leaf))
      return getLeafValue(leaf);
    return (uint64_t)0;
  }
  */

  uint64_t lookup(uint8_t key[], unsigned keyLength, unsigned maxKeyLength) {
    NodeStatic* leaf_static = lookup(static_root, key, keyLength, 0, maxKeyLength);
    if (isLeaf(leaf_static))
      return getLeafValue(leaf_static);
    return (uint64_t)0;
  }

  uint64_t lower_bound(uint8_t key[], unsigned keyLength, unsigned maxKeyLength) {
    Node* leaf = lower_bound(root, key, keyLength, 0, maxKeyLength);
    if (isLeaf(leaf))
      return getLeafValue(leaf);
    return (uint64_t)0;
  }

  uint64_t next() {
    Node* leaf = nextLeaf();
    if (isLeaf(leaf))
      return getLeafValue(leaf);
    return (uint64_t)0;
  }

  void erase(uint8_t key[], unsigned keyLength, unsigned depth, unsigned maxKeyLength) {
    erase(root, &root, key, keyLength, depth, maxKeyLength);
  }

  void erase(uint8_t key[], unsigned keyLength, unsigned maxKeyLength) {
    erase(root, &root, key, keyLength, 0, maxKeyLength);
  }

  void merge() {
    merge_trees();
  }

  Node* getRoot() {
    return root;
  }

  NodeStatic* getStaticRoot() {
    return static_root;
  }

  uint64_t getMemory() {
    std::cout << "Node4 = " << node4_count << "\n";
    std::cout << "Node16 = " << node16_count << "\n";
    std::cout << "Node48 = " << node48_count << "\n";
    std::cout << "Node256 = " << node256_count << "\n";
    std::cout << "=============================\n";
    std::cout << "NodeD = " << nodeD_count << "\n";
    std::cout << "NodeDP = " << nodeDP_count << "\n";
    std::cout << "NodeF = " << nodeF_count << "\n";
    std::cout << "NodeFP = " << nodeFP_count << "\n";
    std::cout << "NodeP = " << nodeP_count << "\n";
    std::cout << "NodePP = " << nodePP_count << "\n";

    return memory + static_memory;
  }

  uint64_t getStaticMemory() {
    return static_memory;
  }

  void tree_info() {
    tree_info(root);
  }

private:
  Node* root;
  NodeStatic* static_root;

  uint64_t memory;
  uint64_t static_memory;

  std::vector<NodeCursor> node_stack;

  //node stats
  uint64_t node4_count;
  uint64_t node16_count;
  uint64_t node48_count;
  uint64_t node256_count;
  uint64_t nodeD_count;
  uint64_t nodeDP_count;
  uint64_t nodeF_count;
  uint64_t nodeFP_count;
  uint64_t nodeP_count;
  uint64_t nodePP_count;
};

static double gettime(void) {
  struct timeval now_tv;
  gettimeofday (&now_tv,NULL);
  return ((double)now_tv.tv_sec) + ((double)now_tv.tv_usec)/1000000.0;
}

