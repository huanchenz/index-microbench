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

class hybridART {

public:
  // Constants for the node types

  static const int8_t NodeType4=0;
  static const int8_t NodeType16=1;
  static const int8_t NodeType48=2;
  static const int8_t NodeType256=3;
  static const int8_t NodeTypeStatic=4; //huanchen-static
  static const int8_t NodeTypeStaticInner=5; //huanchen-static

  // The maximum prefix length for compressed paths stored in the
  // header, if the path is longer it is loaded from the database on
  // demand
  static const unsigned maxPrefixLength=9;

  //static const unsigned NodeStaticItemTHold=227;
  static const unsigned NodeStaticItemTHold=16;

  static const unsigned MERGE=1;

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
  /*
  struct NodeStatic : Node {
    char* data;

    NodeStatic() : Node(NodeTypeStatic) {}
    NodeStatic(uint16_t size) : Node(NodeTypeStatic) {
      count = size;
      //data = (char*)malloc((sizeof(uint8_t) + sizeof(Node*)) * size);
      data = new char[(sizeof(uint8_t) + sizeof(Node*)) * size];
      memset(data, 0, (sizeof(uint8_t) + sizeof(Node*)) * size);
    }

    ~NodeStatic() {
      delete data;
    }

    uint8_t* key() {
      return (uint8_t*)(data);
    }

    uint8_t* key(unsigned pos) {
      //return (uint8_t*)(data + sizeof(uint8_t) * pos);
      return (uint8_t*)(data + pos);
    }

    Node** child() {
      //return (Node**)(data + sizeof(uint8_t) * count);
      return (Node**)(data + count);
    }

    Node** child(unsigned pos) {
      //return (Node**)(data + sizeof(uint8_t) * count);
      return (Node**)(data + count + sizeof(Node*) * pos);
    }
  };
  */

  struct NodeStatic : Node {
    uint8_t keys[16];
    Node* children[16];

    NodeStatic() : Node(NodeTypeStatic) {
      memset(keys,0,sizeof(keys));
      memset(children,0,sizeof(children));
    }

    NodeStatic(uint16_t size) : Node(NodeTypeStatic) {
      memset(keys,0,sizeof(keys));
      memset(children,0,sizeof(children));
    }

    uint8_t* key() {
      return keys;
    }

    uint8_t* key(unsigned pos) {
      return &keys[pos];
    }

    Node** child() {
      return children;
    }

    Node** child(unsigned pos) {
      return &children[pos];
    }
  };

  struct NodeStaticInner : Node {
    uint8_t childIndex[256];
    Node** child;

    NodeStaticInner() : Node(NodeTypeStaticInner) {}
    NodeStaticInner(uint16_t size) : Node(NodeTypeStaticInner) {
      count = size;
      memset(childIndex, emptyMarker_static, sizeof(childIndex));
      child = (Node**)malloc(sizeof(Node*) * size);
      memset(child, 0, sizeof(Node*) * size);
    }

    ~NodeStaticInner() {
      delete child;
    }
  };

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
  static const uint8_t emptyMarker_static=255;

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
  //Return true is the node does NOT contain any leaf child
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
	if ((!node->child[i]) && isLeaf(node->child[i]))
	  return false;
      return true;
    }
    case NodeTypeStatic: {
      NodeStatic* node=static_cast<NodeStatic*>(n);
      for (unsigned i = 0; i < node->count; i++)
	if (isLeaf(node->child()[i]))
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

  inline bool isLeaf(Node* node) {
    // Is the node a leaf?
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

  // This address is used to communicate that search failed
  Node* nullNode=NULL;

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
    throw; // Unreachable
  }

  //huanchen-static
  inline Node** findChild_static(Node* n,uint8_t keyByte) {
    switch (n->type) {

    case NodeTypeStaticInner: {
      NodeStaticInner* node=static_cast<NodeStaticInner*>(n);
      if (node->childIndex[keyByte]!=emptyMarker_static)
	return &node->child[node->childIndex[keyByte]]; 
      else
	return &nullNode;
    }

    case NodeType256: {
      Node256* node=static_cast<Node256*>(n);
      return &(node->child[keyByte]);
    }

    case NodeTypeStatic: {
      NodeStatic* node=static_cast<NodeStatic*>(n);

      if (node->count < 5) {
	for (unsigned i = 0; i < node->count; i++)
	  if (node->key()[i] == flipSign(keyByte))
	    return &node->child()[i];
	return &nullNode;
      }

      for (unsigned i = 0; i < node->count; i += 16) {
	__m128i cmp=_mm_cmpeq_epi8(_mm_set1_epi8(flipSign(keyByte)),_mm_loadu_si128(reinterpret_cast<__m128i*>(node->key(i))));
	unsigned bitfield;
	if (i + 16 >= n->count)
	  bitfield =_mm_movemask_epi8(cmp)&((1<<node->count)-1);
	else
	  bitfield =_mm_movemask_epi8(cmp);
	if (bitfield)
	  return &node->child(i)[ctz(bitfield)]; 
      }
      return &nullNode;
    }
    }
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
      //huanchen-static
    case NodeTypeStatic: {
      NodeStatic* n=static_cast<NodeStatic*>(node);
      return minimum(n->child()[0]);
    }
    case NodeTypeStaticInner: {
      NodeStaticInner* n=static_cast<NodeStaticInner*>(node);
      return minimum(n->child[0]);
    }
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
      //huanchen-static
    case NodeTypeStatic: {
      NodeStatic* n=static_cast<NodeStatic*>(node);
      return maximum(n->child()[n->count-1]);
    }
    case NodeTypeStaticInner: {
      NodeStaticInner* n=static_cast<NodeStaticInner*>(node);
      return maximum(n->child[n->count-1]);
    }
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
	  loadKey(getLeafValue(node),leafKey);
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
  inline Node* lookup_static(Node* node,uint8_t key[],unsigned keyLength,unsigned depth,unsigned maxKeyLength) {
    bool skippedPrefix=false; // Did we optimistically skip some prefix without checking it?

    while (node!=NULL) {
      if (isLeaf(node)) {
	if (!skippedPrefix&&depth==keyLength) // No check required
	  return node;

	if (depth!=keyLength) {
	  // Check leaf
	  uint8_t leafKey[maxKeyLength];
	  loadKey(getLeafValue(node),leafKey);
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

      node=*findChild_static(node,key[depth]);
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
    if (!node)
      return NULL;

    if (isLeaf(node))
      return node;

    NodeCursor nc;
    nc.node = node;
    nc.cursor = 0;
    node_stack.push_back(nc);

    switch (node->type) {
    case NodeType4: {
      Node4* n=static_cast<Node4*>(node);
      return minimum_recordPath(n->child[0]);
    }
    case NodeType16: {
      Node16* n=static_cast<Node16*>(node);
      return minimum_recordPath(n->child[0]);
    }
    case NodeType48: {
      Node48* n=static_cast<Node48*>(node);
      unsigned pos=0;
      while (n->childIndex[pos]==emptyMarker)
	pos++;
      node_stack.back().cursor = pos;
      return minimum_recordPath(n->child[n->childIndex[pos]]);
    }
    case NodeType256: {
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
    NodeCursor nc;
    nc.node = n;
    switch (n->type) {
    case NodeType4: {
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

      insertNode4(newNode,nodeRef,existingKey[depth+newPrefixLength],node);
      insertNode4(newNode,nodeRef,key[depth+newPrefixLength],makeLeaf(value));
      return;
    }

    // Handle prefix of inner node
    if (node->prefixLength) {
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

  //huanchen-static
  inline void Node4_to_NodeStatic(Node4* n, NodeStatic* n_static) {
    for (unsigned i = 0; i < n->count; i++) {
      n_static->key()[i] = flipSign(n->key[i]);
      n_static->child()[i] = n->child[i];
    }
  }

  inline void Node16_to_NodeStatic(Node16* n, NodeStatic* n_static) {
    for (unsigned i = 0; i < n->count; i++) {
      n_static->key()[i] = n->key[i];
      n_static->child()[i] = n->child[i];
    }
  }

  inline void Node48_to_NodeStatic(Node48* n, NodeStatic* n_static) {
    unsigned c = 0;
    for (uint8_t i = 0; i < 255; i++) {
      if (n->childIndex[i] != emptyMarker) {
	n_static->key()[c] = flipSign(i);
	n_static->child()[c] = n->child[n->childIndex[i]];
	c++;
      }
    }
    if (n->childIndex[255] != emptyMarker) {
      n_static->key()[c] = flipSign(255);
      n_static->child()[c] = n->child[n->childIndex[255]];
    }
  }

  inline void Node256_to_NodeStatic(Node256* n, NodeStatic* n_static) {
    unsigned int c = 0;
    for (uint8_t i = 0; i < 255; i++) {
      if (n->child[i]) {
	n_static->key()[c] = flipSign(i);
	n_static->child()[c] = n->child[i];
	c++;
      }
    }
    if (n->child[255]) {
      n_static->key()[c] = flipSign(255);
      n_static->child()[c] = n->child[255];
    }
  }

  inline void Node_to_NodeStatic(Node* n, NodeStatic* n_static) {
    n_static->count = n->count;
    n_static->prefixLength = n->prefixLength;
    for (unsigned i = 0; i < maxPrefixLength; i++)
      n_static->prefix[i] = n->prefix[i];

    if (n->type == NodeType4)
      Node4_to_NodeStatic(reinterpret_cast<Node4*>(n), n_static);
    else if (n->type == NodeType16)
      Node16_to_NodeStatic(reinterpret_cast<Node16*>(n), n_static);
    else if (n->type == NodeType48)
      Node48_to_NodeStatic(reinterpret_cast<Node48*>(n), n_static);
    else if (n->type == NodeType256)
      Node256_to_NodeStatic(reinterpret_cast<Node256*>(n), n_static);
  }


  //huanchen-static ====================================================
  inline void Node4_to_NodeStaticInner(Node4* n, NodeStaticInner* n_static) {
    for (uint8_t i = 0; i < n->count; i++) {
      n_static->childIndex[n->key[i]] = i;
      n_static->child[i] = n->child[i];
    }
  }

  inline void Node16_to_NodeStaticInner(Node16* n, NodeStaticInner* n_static) {
    for (uint8_t i = 0; i < n->count; i++) {
      n_static->childIndex[n->key[i]] = flipSign(i);
      n_static->child[i] = n->child[i];
    }
  }

  inline void Node48_to_NodeStaticInner(Node48* n, NodeStaticInner* n_static) {
    uint8_t c = 0;
    for (unsigned i = 0; i < 256; i++) {
      if (n->childIndex[i] != emptyMarker) {
	n_static->childIndex[i] = c;
	n_static->child[c] = n->child[n->childIndex[i]];
	c++;
      }
    }
  }

  inline void Node256_to_NodeStaticInner(Node256* n, NodeStaticInner* n_static) {
    uint8_t c = 0;
    for (unsigned i = 0; i < 256; i++) {
      if (n->child[i]) {
	n_static->childIndex[i] = c;
	n_static->child[c] = n->child[i];
	c++;
      }
    }
  }

  inline void Node_to_NodeStaticInner(Node* n, NodeStaticInner* n_static) {
    n_static->count = n->count;
    n_static->prefixLength = n->prefixLength;
    for (unsigned i = 0; i < maxPrefixLength; i++)
      n_static->prefix[i] = n->prefix[i];

    if (n->type == NodeType4)
      Node4_to_NodeStaticInner(reinterpret_cast<Node4*>(n), n_static);
    else if (n->type == NodeType16)
      Node16_to_NodeStaticInner(reinterpret_cast<Node16*>(n), n_static);
    else if (n->type == NodeType48)
      Node48_to_NodeStaticInner(reinterpret_cast<Node48*>(n), n_static);
    else if (n->type == NodeType256)
      Node256_to_NodeStaticInner(reinterpret_cast<Node256*>(n), n_static);
  }


  //huanchen-static
  inline void Node4_to_Node256(Node4* n, Node256* n_static) {
    for (unsigned i = 0; i < n->count; i++)
      n_static->child[n->key[i]] = n->child[i];
  }

  inline void Node16_to_Node256(Node16* n, Node256* n_static) {
    for (unsigned i = 0; i < n->count; i++)
      n_static->child[flipSign(n->key[i])] = n->child[i];
  }

  inline void Node48_to_Node256(Node48* n, Node256* n_static) {
    for (unsigned i = 0; i < 256; i++)
      if (n->childIndex[i] != emptyMarker)
	n_static->child[i] = n->child[n->childIndex[i]];
  }

  inline void Node256_to_Node256(Node256* n, Node256* n_static) {
    for (unsigned i = 0; i < 256; i++)
      n_static->child[i] = n->child[i];
  }

  inline void Node_to_Node256(Node* n, Node256* n_static) {
    n_static->count = n->count;
    n_static->prefixLength = n->prefixLength;
    for (unsigned i = 0; i < maxPrefixLength; i++)
      n_static->prefix[i] = n->prefix[i];

    if (n->type == NodeType4)
      Node4_to_Node256(reinterpret_cast<Node4*>(n), n_static);
    else if (n->type == NodeType16)
      Node16_to_Node256(reinterpret_cast<Node16*>(n), n_static);
    else if (n->type == NodeType48)
      Node48_to_Node256(reinterpret_cast<Node48*>(n), n_static);
    else if (n->type == NodeType256)
      Node256_to_Node256(reinterpret_cast<Node256*>(n), n_static);
  }


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
    case NodeTypeStatic: {
      return sizeof(NodeStatic) + n->count * (sizeof(uint8_t) + sizeof(Node*));
    }
    case NodeTypeStaticInner: {
      return sizeof(NodeStaticInner) + n->count * sizeof(Node*);
    }
    }
    return 0;
  }

  inline void first_merge() {
    if (!root) return;

    Node* n = root;
    Node* n_new = NULL;
    Node* n_new_parent = NULL;
    int parent_pos = -1;

    std::deque<Node*> node_queue;
    std::deque<Node*> new_node_queue;

    node_queue.push_back(n);
    int count = 0;
    while (!node_queue.empty()) {
      n = node_queue.front();
      if (!isLeaf(n)) {

	if (n->count <= NodeStaticItemTHold) {
	  if (isInner(n)) {
	    NodeStaticInner* n_static = new NodeStaticInner(n->count);
	    nodeStaticInner_count++; //h
	    Node_to_NodeStaticInner(n, n_static);
	    n_new = n_static;
	    for (unsigned i = 0; i < n_static->count; i++)
	      if (!isLeaf(n_static->child[i]))
		node_queue.push_back(n_static->child[i]);
	  }
	  else {
	    NodeStatic* n_static = new NodeStatic(n->count);
	    nodeStatic_count++; //h
	    Node_to_NodeStatic(n, n_static);
	    n_new = n_static;
	    for (unsigned i = 0; i < n_static->count; i++)
	      if (!isLeaf(n_static->child()[i]))
		node_queue.push_back(n_static->child()[i]);
	  }
	}
	else {
	  Node256* n_static = new Node256();
	  nodeStatic256_count++; //h
	  Node_to_Node256(n, n_static);
	  n_new = n_static;
	  for (unsigned i = 0; i < 256; i++)
	    if ((n_static->child[i]) && (!isLeaf(n_static->child[i])))
	      node_queue.push_back(n_static->child[i]);
	}

	static_memory += node_size(n_new);
	new_node_queue.push_back(n_new);

	bool next_parent = false;

	if (n_new_parent) {

	  if (n_new_parent->type == NodeTypeStaticInner) {
	    NodeStaticInner* node = reinterpret_cast<NodeStaticInner*>(n_new_parent);
	    node->child[parent_pos] = n_new;
	  }
	  else if (n_new_parent->type == NodeTypeStatic) {
	    NodeStatic* node = reinterpret_cast<NodeStatic*>(n_new_parent);
	    node->child()[parent_pos] = n_new;
	  }
	  else if (n_new_parent->type == NodeType256) {
	    Node256* node = reinterpret_cast<Node256*>(n_new_parent);
	    node->child[parent_pos] = n_new;
	  }
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

	  if (n_new_parent->type == NodeTypeStaticInner) {
	    NodeStaticInner* node = reinterpret_cast<NodeStaticInner*>(n_new_parent);
	    do {
	      parent_pos++;
	      if (parent_pos >= node->count)
		next_parent = true;
	    } while ((parent_pos < node->count) && isLeaf(node->child[parent_pos]));
	  }
	  else if (n_new_parent->type == NodeTypeStatic) {
	    NodeStatic* node = reinterpret_cast<NodeStatic*>(n_new_parent);
	    do {
	      parent_pos++;
	      if (parent_pos >= node->count)
		next_parent = true;
	    } while ((parent_pos < node->count) && isLeaf(node->child()[parent_pos]));
	  }
	  else if (n_new_parent->type == NodeType256) {
	    Node256* node = reinterpret_cast<Node256*>(n_new_parent);
	    do {
	      parent_pos++;
	      if (parent_pos >= 256)
		next_parent = true;
	    } while ((parent_pos < 256) && (!node->child[parent_pos] || isLeaf(node->child[parent_pos])));
	  }
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
    root = NULL;
    memory = 0;
  }

public:
  hybridART()
    : root(NULL), static_root(NULL), memory(0), static_memory(0),
    node4_count(0), node16_count(0), node48_count(0), node256_count(0), nodeStatic_count(0), nodeStatic256_count(0), nodeStaticInner_count(0)
  { }

  hybridART(Node* r, Node* sr)
    : root(r), static_root(sr), memory(0), static_memory(0),
    node4_count(0), node16_count(0), node48_count(0), node256_count(0), nodeStatic_count(0), nodeStatic256_count(0), nodeStaticInner_count(0)
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

  uint64_t lookup(uint8_t key[], unsigned keyLength, unsigned maxKeyLength) {
    Node* leaf = lookup(root, key, keyLength, 0, maxKeyLength);
    if (!leaf) {
      Node* leaf_static = lookup_static(static_root, key, keyLength, 0, maxKeyLength);
      if (isLeaf(leaf_static))
	return getLeafValue(leaf_static);
      return (uint64_t)0;
    }
    if (isLeaf(leaf))
      return getLeafValue(leaf);
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

  Node* getStaticRoot() {
    return static_root;
  }

  uint64_t getMemory() {
    std::cout << "Node4 = " << node4_count << "\n";
    std::cout << "Node16 = " << node16_count << "\n";
    std::cout << "Node48 = " << node48_count << "\n";
    std::cout << "Node256 = " << node256_count << "\n";
    std::cout << "=============================\n";
    std::cout << "NodeStatic = " << nodeStatic_count << "\n";
    std::cout << "NodeStatic256 = " << nodeStatic256_count << "\n";
    std::cout << "NodeStaticInner = " << nodeStaticInner_count << "\n";

    return memory + static_memory;
  }

  uint64_t getStaticMemory() {
    return static_memory;
  }

private:
  Node* root;
  Node* static_root;

  uint64_t memory;
  uint64_t static_memory;

  std::vector<NodeCursor> node_stack;

  //node stats
  uint64_t node4_count;
  uint64_t node16_count;
  uint64_t node48_count;
  uint64_t node256_count;
  uint64_t nodeStatic_count;
  uint64_t nodeStatic256_count;
  uint64_t nodeStaticInner_count;
};

static double gettime(void) {
  struct timeval now_tv;
  gettimeofday (&now_tv,NULL);
  return ((double)now_tv.tv_sec) + ((double)now_tv.tv_usec)/1000000.0;
}

