#ifndef QUACKLE_V2GADDAG_H
#define QUACKLE_V2GADDAG_H

#include <bitset>

#include "alphabetparameters.h"
#include "datamanager.h"

#define QUACKLE_GADDAG_SEPARATOR QUACKLE_NULL_MARK

namespace Quackle {

class V2Gaddag {
 public:
  V2Gaddag(const unsigned char* data,
	   Letter lastLetter,
	   int bitsetSize,
	   int indexSize,
	   int indexUnit);

  const unsigned char* nextChild(const unsigned char* bitsetData,
			Letter minLetter,
			int childIndex,
			Letter* nextLetter) const;
  bool hasChild(const unsigned char* bitsetData, Letter letter) const;
  int numChildren(const unsigned char* bitsetData) const;
  const unsigned char* child(const unsigned char* bitsetData, Letter letter) const;


  bool completesWord(const unsigned char* indexData) const;
  const unsigned char* followIndex(const unsigned char* indexData) const;
  
  const unsigned char* root() const { return m_data; };
  
 private:
  const unsigned char* m_data;
  const Letter m_lastLetter;
  const int m_bitsetSize;
  const int m_indexSize;
  const int m_indexUnit;
};
 
}  // namespace Quackle

#endif
