#ifndef QUACKLE_V2_GENERATOR_H
#define QUACKLE_V2_GENERATOR_H

#include <vector>

#include "alphabetparameters.h"
#include "anagrammap.h"
#include "game.h"
#include "move.h"
#include "v2gaddag.h"

// TODO: generate this from alphabet
using namespace std;

#define EVERY_LETTER 0x7FFFFFE0

namespace Quackle {

  class V2Generator {

  public:
    V2Generator();
    V2Generator(const Quackle::GamePosition &position);
    V2Generator(const Quackle::GamePosition &position,
		const map<Product, vector<LetterString>>& bingos);
		
    ~V2Generator();

    MoveList kibitzAll();
    Move kibitz();

    const MoveList& bestMoves() const {
      assert(m_justBest);
      return m_moves;
    }

    static void findBingos(const set<LetterString>& racks,
			   map<Product, vector<LetterString>>* bingos);

    static void initializeTiebreaker() { m_tiebreakDividend = 0; }
    static unsigned int m_tiebreakDividend;
    
    const unsigned char* vertBeforeNode(int row, int col, int numLetters);
    const unsigned char* vertAfterNode(int row, int col, int numLetters);
    const unsigned char* horizBeforeNode(int row, int col, int numLetters);
    const unsigned char* horizAfterNode(int row, int col, int numLetters);
    void updateVerticalHooks(int row, int col);
    void updateHorizontalHooks(int row, int col);
    void computeHooks();
    void debugHooks();
    void debugVertHook(int row, int col);
    void debugHorizHook(int row, int col);

  private:
    struct Through {
      int start;
      int end;
      int score;
      const unsigned char* node;
    };
    
    struct WorthChecking {
      bool couldBeBest;
      float maxEquity;
    };
    
    struct Spot {
      int anchorRow;
      int anchorCol;
      bool horizontal;
      bool useBlank;
      bool canMakeAnyWord;
      int numTilesThrough;
      int throughScore;
      float maxEquity;
      int maxTilesBehind;
      int minTilesAhead;
      int maxTilesAhead;
      int minPlayed;
      int maxPlayed;
      WorthChecking worthChecking[8];
      WorthChecking worthCheckingBehind[8];
      int longestViable;
      int hindmostViable;
      const unsigned char* anchorNode;
      int realPositions[15];
      
      bool operator<(const Spot& rhs) const {
	if (maxEquity == rhs.maxEquity) {
	  return useBlank < rhs.useBlank;
	}
	return maxEquity > rhs.maxEquity;
      }

      inline bool viableAtLength(int length) const {
	//UVcout << "viableAtLength(" << length << "): "
	//       << worthChecking[length].couldBeBest << endl;
	return worthChecking[length].couldBeBest;
      }

      inline bool viableWithBehind(int behind) const {
	return worthCheckingBehind[behind].couldBeBest;
      }
    };

    void findStaticPlays();

    inline double getLeave() const;
    int hookLetterMultiplier(int row, int col, bool horiz);
    void scoreSpot(Spot* spot);
    bool couldHaveBingos() const;
    inline bool blankOnRack() const;
    inline bool blankWasPlayed() const;
    inline uint32_t otherRackBits(uint32_t rackBits, uint32_t rackHooks) const;
    bool restrictSpotUsingHooks(Spot* spot, uint32_t rackBits,
				uint32_t rackHooks) const;
    void findHookSpotsInRow(int row, vector<Spot>* spots);
    void findHookSpotsInCol(int col, vector<Spot>* spots);
    void updateThroughAtSquare(int row, int col, int pos);
    inline void finishLastThrough();
    void findThroughSpotsInRow(int row, vector<Spot>* spots);
    void findThroughSpotsInCol(int col, vector<Spot>* spots);
    void findSpots(vector<Spot>* spots);
    void addScoredSpots(Spot* spot, vector<Spot>* spots);
    void findEmptyBoardSpots(vector<Spot>* spots);
    void restrictSpot(Spot* spot);
    inline void restrictByLength(Spot* spot);
    inline void restrictByBehind(Spot* spot);
    inline const V2Gaddag& spotGaddag(Spot* spot) const;
    void findMovesAt(Spot* spot);
    inline bool duplicatePreceding(const Letter* letters, int i) const;
    void findBlankBingos();
    void findBingos();
    static void findBingos(const set<Product>& subsets,
			   const V2Gaddag& gaddag,
			   const unsigned char* node,
			   LetterString* prefix,
			   Product product,
			   map<Product, vector<LetterString>>* bingos);
    static void findBingos(const set<Product>& subsets,
			   map<Product, vector<LetterString>>* bingos);
    static void addSubsets(const LetterString& rack, set<Product>* subsets);
    void findBestExchange();
    inline Letter boardLetter(int row, int col);
    inline int tileScore(int row, int col);
    inline bool isEmpty(int row, int col);

    const unsigned char* followLetter(const V2Gaddag& gaddag, int row, int col,
				      const unsigned char* node);

    struct Hook {
      bool touches;
      uint32_t letters;
      int score;
    };
    Hook m_vertHooks[15][15];
    Hook m_horizHooks[15][15];
    
    uint32_t wordCompleters(const unsigned char* node);
    uint32_t vertBetween(int row, int col, int numLettersAfter,
			 const unsigned char* beforeNode,
			 const unsigned char* afterNode);
    uint32_t horizBetween(int row, int col, int numLettersAfter,
			  const unsigned char* beforeNode,
			  const unsigned char* afterNode);
    bool vertCompletesWord(const V2Gaddag& gaddag, int row, int col,
			   const unsigned char* node,
			   int numLettersAfter);
    bool horizCompletesWord(const V2Gaddag& gaddag, int row, int col,
			    const unsigned char* node,
			    int numLettersAfter);
    
    // restriction is a letter index mask, restricing to letters on rack
    // that can hook in this context
    static inline
      bool nextLetter(const V2Gaddag& gaddag, const unsigned char* node,
		      uint32_t restiction,
		      Letter minLetter, int* childIndex, Letter* foundLetter,
		      const unsigned char** child);
    
    inline int scoreLetter(int pos, Letter letter, int letterMultiplier);
    
    void debugPlaced(const Spot& spot, int behind, int ahead) const;
    inline void useLetter(Letter letter, uint32_t* foundLetterMask);
    inline void unuseLetter(Letter letter, uint32_t foundLetterMask);
    inline bool maybeRecordMove(const Spot& spot, int wordMultiplier,
				int behind, int numPlaced);
    inline void getSquare(const Spot& spot, int delta,
			  int* row, int* col, int* pos);
    inline const unsigned char* followToRealChild(const V2Gaddag& gaddag,
						  int row, int col,
						  bool horizontal,
						  const unsigned char* child);
    inline void findMoreBlankless(Spot* spot, int delta, int ahead,
				  int behind, int velocity, int wordMultiplier,
				  const V2Gaddag& gaddag, const unsigned char* node);
    inline void findMoreBlankRequired(Spot* spot, int delta, int ahead,
				      int behind, int velocity, int wordMultiplier,
				  const V2Gaddag& gaddag, const unsigned char* node);
    inline bool fits(const Spot& spot, const LetterString& word,
		     int ahead, int behind, int* wordHookScore);
    inline int getWordMultiplierAndHooks(const Spot& spot, int ahead, int behind);
    void fitBingos(Spot* spot);
    void findBlankless(Spot* spot, int delta, int ahead, int behind, int velocity,
		       int wordMultiplier, const V2Gaddag& gaddag,
		       const unsigned char* node);
    void findBlankRequired(Spot* spot, int delta, int ahead, int behind,
			   int velocity, int wordMultiplier,
			   const V2Gaddag& gaddag, const unsigned char* node);
    inline bool couldMakeWord(const Spot& spot, int length);
    float bestLeave(const Spot& spot, int length);
    Move v2generate();
    void setUpCounts(const LetterString &letters);

    int m_hookScore;
    int m_mainWordScore;

    // decremented and incremented while finding moves
    char m_counts[QUACKLE_FIRST_LETTER + QUACKLE_MAXIMUM_ALPHABET_SIZE];

    // constant while finding moves, allows us to check if a blank was played
    int m_numBlanks;
    
    vector<LetterString> m_bingos;
    uint32_t m_rackBits;
    Letter m_placed[QUACKLE_MAXIMUM_BOARD_SIZE];
    double m_bestLeaves[8];
    int m_numThroughs;
    bool m_inThrough;
    Through m_throughs[8];

    inline bool bestEnough(double equity) const;
    inline bool clearlyBetter(double equity) const;
    bool m_justBest;
    MoveList m_moves;

    double m_bestEquityEpsilon = 0.0001;
    double m_blankSpendingEpsilon = 0.0001;

    // debug stuff
    UVString counts2string() const;

    GamePosition m_position;
    Board* board() { return &m_position.underlyingBoardReference(); }
    const Rack& rack() const {
      return m_position.currentPlayer().rack();
    }
    Product m_product;
    const RackAnagrams* m_anagrams;
    const map<Product, vector<LetterString>>* m_bingoMap;
    
  };
}  // namespace Quackle

#endif
