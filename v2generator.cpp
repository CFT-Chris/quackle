#include <time.h>
#include <sys/time.h>

#include "board.h"
#include "boardparameters.h"
#include "datamanager.h"
#include "evaluator.h"
#include "gameparameters.h"
#include "lexiconparameters.h"
#include "strategyparameters.h"
#include "v2gaddag.h"
#include "v2generator.h"

//#define DEBUG_V2GEN

using namespace std;
using namespace Quackle;

V2Generator::V2Generator() {}

V2Generator::V2Generator(const GamePosition &position)
  : m_position(position) {
}

V2Generator::~V2Generator() {}

Move V2Generator::kibitz() {
  return findStaticBest();
}

Move V2Generator::findStaticBest() {
  UVcout << "findStaticBest(): " << endl << m_position << endl;
	m_best = Move::createPassMove();

  m_moveList.clear();

	struct timeval start, end;

	// TODO: are enough tiles in the bag?
  // TODO much later: would this end the game?
	gettimeofday(&start, NULL);
	findBestExchange();
	gettimeofday(&end, NULL);
	UVcout << "Time finding exchanges was "
				 << ((end.tv_sec * 1000000 + end.tv_usec)
						 - (start.tv_sec * 1000000 + start.tv_usec)) << " microseconds." << endl;
	UVcout << "best exchange: " << m_best << endl;
  setUpCounts(rack().tiles());
	gettimeofday(&start, NULL);
  m_anagrams = QUACKLE_ANAGRAMMAP->lookUp(rack());
	gettimeofday(&end, NULL);
	UVcout << "Time checking anagrammap was "
				 << ((end.tv_sec * 1000000 + end.tv_usec)
						 - (start.tv_sec * 1000000 + start.tv_usec)) << " microseconds." << endl;
	
	if (m_anagrams == NULL) {
		UVcout << "could not find rack anagrams!" << endl;
	} else {
		UVcout << "found rack anagrams!! usesWhatever.thruNone.numPlayed: ";
		UVcout << static_cast<int>(m_anagrams->usesWhatever.thruNone.numPlayed) << endl;
	}
	vector<Spot> spots;
	
	if (board()->isEmpty()) {
		gettimeofday(&start, NULL);
		findEmptyBoardSpots(&spots);
		gettimeofday(&end, NULL);
		UVcout << "Time finding spots on empty board was "
					 << ((end.tv_sec * 1000000 + end.tv_usec)
						 - (start.tv_sec * 1000000 + start.tv_usec)) << " microseconds." << endl;
	} else {
		gettimeofday(&start, NULL);
		findSpots(&spots);
		gettimeofday(&end, NULL);
		UVcout << "Time finding spots on nonempty board was "
					 << ((end.tv_sec * 1000000 + end.tv_usec)
						 - (start.tv_sec * 1000000 + start.tv_usec)) << " microseconds." << endl;

	}
	gettimeofday(&start, NULL);
	std::sort(spots.begin(), spots.end());
	gettimeofday(&end, NULL);
	UVcout << "Time sorting spots was "
				 << ((end.tv_sec * 1000000 + end.tv_usec)
						 - (start.tv_sec * 1000000 + start.tv_usec)) << " microseconds." << endl;

	for (Spot& spot : spots) {
		//#ifdef DEBUG_V2GEN
		UVcout << "Spot: (" << spot.anchorRow << ", " << spot.anchorCol
					 << ", " << "blank: " << spot.canUseBlank << "): "
					 << spot.maxEquity;
		//#endif
		restrictByLength(&spot);
		UVcout << endl;

		if (spot.maxEquity < m_best.equity) {
#ifdef DEBUG_V2GEN
			UVcout << "no need to check this spot!" << endl;
#endif
			continue;
		}
		findMovesAt(&spot);
	}
	UVcout << "best Move: " << m_best << endl;	
	return m_best;
}

void V2Generator::restrictByLength(Spot* spot) {
	int oldLongestViable = spot->longestViable;
	for (int i = 1; i <= oldLongestViable; ++i) {
		if (spot->worthChecking[i].couldBeBest &&
				(spot->worthChecking[i].maxEquity > m_best.equity)) {
			spot->longestViable = i;
		} else {
			spot->worthChecking[i].couldBeBest = false;
		}
	}
#ifdef DEBUG_V2GEN
	for (int i = 1; i <= 7; ++i) {
		if (spot->worthChecking[i].couldBeBest) {
			UVcout << ", [" << i << "]: " << spot->worthChecking[i].maxEquity;
		}
	}
#endif
}

void V2Generator::findExchanges(const uint64_t* rackPrimes,
																const LetterString& tiles,
																uint64_t product,
																int pos, int numExchanged) {
	if (pos == 7) {
    if (numExchanged == 0) return;
		double leave = product == 1 ? 0 :
			QUACKLE_STRATEGY_PARAMETERS->primeleave(product);
		//UVcout << "leave: " << leave << endl;
		// TODO: heuristics for num exchanged?
		if (leave > m_best.equity) {
			Move move;
			move.action = Move::Exchange;
			LetterString exchanged;
			for (int i = 0; i < numExchanged; ++i) {
				exchanged += m_placed[i];
			}
			move.setTiles(exchanged);
			move.score = 0;
			move.equity = leave;
			m_best = move;
		}
		if (leave > m_bestLeaves[numExchanged]) {
			m_bestLeaves[numExchanged] = leave;
		}
		return;
	}
	findExchanges(rackPrimes, tiles, product * rackPrimes[pos], pos + 1, numExchanged);
	m_placed[numExchanged] = tiles[pos];
	findExchanges(rackPrimes, tiles, product, pos + 1, numExchanged + 1);
}

void V2Generator::findBestExchange() {
	assert(rack().size() == 7);
	for (int i = 0; i <= 7; ++i) {
		m_bestLeaves[i] = -9999;
	}
	double bestEquity = 0;
	uint64_t primes[7];
	int bestMask = 127;
	const LetterString& tiles = rack().tiles();
	for (int i = 0; i < 7; ++i) {
		primes[i] = QUACKLE_PRIMESET->lookUpTile(tiles[i]);
	}
	uint64_t products[127];
	for (int i = 1; i < 127; ++i) products[i] = 1;
	int b = 1;
	for (int i = 0; i < 7; ++i) {
    for (int j = b; j < 127; j += b) {
			for (int k = 0; k < b; ++j, ++k) {
	      products[j] *= primes[i];
			}
		}
		b *= 2;
	}
	for (int mask = 1; mask < 127; mask++) {
#ifdef DEBUG_V2GEN
		{
			uint64_t product = 1;
			int numExchanged = 7;
			for (int i = 0; i < 7; ++i) {
				if (((1 << i) & mask) != 0) {
					--numExchanged;
					product *= primes[i];
				}
			}
			assert(numExchanged == (7 - __builtin_popcount(mask)));
			assert(product == products[mask]);
		}
#endif
		double leave = QUACKLE_STRATEGY_PARAMETERS->primeleave(products[mask]);
		if (leave > bestEquity) {
			bestEquity = leave;
			bestMask = mask;
		}
		int numExchanged = 7 - __builtin_popcount(mask);
		if (leave > m_bestLeaves[numExchanged]) {
			m_bestLeaves[numExchanged] = leave;
		}
	}
	LetterString exchanged;
	for (int i = 0; i < 7; ++i) {
		if (((1 << i) & bestMask) == 0) {
			exchanged += tiles[i];
		}
	}			
	m_best = Move::createExchangeMove(exchanged);
	m_best.equity = bestEquity;
}

bool V2Generator::couldMakeWord(const Spot& spot, int length) const {
	//assert(length > 0);
	//assert(length <= 7);
	//assert(spot.playedThrough.length() == 0);
	if (m_anagrams == NULL) return true;
	const int lengthMask = 1 << length;
	const UsesTiles& usesTiles =
		spot.canUseBlank ? m_anagrams->usesWhatever : m_anagrams->usesNoBlanks;
	const NTileAnagrams& anagrams = usesTiles.thruNone;
	return (anagrams.numPlayed & lengthMask) != 0;
}

void V2Generator::findMovesAt(Spot* spot) {
  m_rackBits = 0;
	for (int letter = QUACKLE_ALPHABET_PARAMETERS->firstLetter();
			 letter <= QUACKLE_ALPHABET_PARAMETERS->lastLetter(); ++letter) {
		if (m_counts[letter] > 0) m_rackBits |= (1 << letter);
	}
	m_mainWordScore = spot->throughScore;
	UVcout << "spot->canUseBlank: " << spot->canUseBlank
				 << ", m_counts[QUACKLE_BLANK_MARK]: "
				 << static_cast<int>(m_counts[QUACKLE_BLANK_MARK]) << endl;
	if (!spot->canUseBlank || m_counts[QUACKLE_BLANK_MARK] == 0) {
		struct timeval start, end;
		gettimeofday(&start, NULL);
		findBlankless(spot, 0, 0, 0, -1, 1,
									QUACKLE_LEXICON_PARAMETERS->v2Gaddag()->root());
		gettimeofday(&end, NULL);
		UVcout << "Time finding moves (without blank) was "
					 << ((end.tv_sec * 1000000 + end.tv_usec)
							 - (start.tv_sec * 1000000 + start.tv_usec))
					 << " microseconds." << endl;
	} else {
		struct timeval start, end;
		gettimeofday(&start, NULL);
		findBlankable(spot, 0, 0, 0, -1, 1,
									QUACKLE_LEXICON_PARAMETERS->v2Gaddag()->root());
		gettimeofday(&end, NULL);
		UVcout << "Time finding moves (with blank) was "
					 << ((end.tv_sec * 1000000 + end.tv_usec)
							 - (start.tv_sec * 1000000 + start.tv_usec))
					 << " microseconds." << endl;		
	}
}

namespace {
	UVString debugLetterMask(uint32_t letters) {
		UVString ret;
		for (Letter i = 0; i <= QUACKLE_ALPHABET_PARAMETERS->lastLetter(); ++i) {
			if ((letters & (1 << i)) != 0) {
				ret += QUACKLE_ALPHABET_PARAMETERS->userVisible(i);
			}
		}
		return ret;
	}
}

int V2Generator::scorePlay(const Spot& spot, int behind, int ahead) {
	int mainScore = spot.throughScore;
#ifdef DEBUG_V2GEN			
	UVcout << "throughScore: " << spot.throughScore << endl;
#endif
	int wordMultiplier = 1;
	int row = spot.anchorRow;
	int col = spot.anchorCol;
	int pos;
	int anchorPos;
	if (spot.horizontal) {
    anchorPos = col;
		col -= behind;
		pos = col;
	} else {
		anchorPos = row;
		row -= behind;
		pos = row;
	}
	for (; pos < anchorPos; ++pos) {
#ifdef DEBUG_V2GEN
		assert(pos >= 0);
		assert(pos < 15);
		UVcout << "pos: " << pos << endl;
		UVcout << "m_placed[pos]: " << static_cast<int>(m_placed[pos]) << endl;
#endif

#ifdef DEBUG_V2GEN			
		UVcout << "QUACKLE_BOARD_PARAMETERS->wordMultiplier(" << row << ", "
					 << col << "): "
					 << QUACKLE_BOARD_PARAMETERS->wordMultiplier(row, col) << endl;
#endif
		wordMultiplier *= QUACKLE_BOARD_PARAMETERS->wordMultiplier(row, col);
		if (QUACKLE_ALPHABET_PARAMETERS->isPlainLetter(m_placed[pos])) {
			int letterMultiplier = QUACKLE_BOARD_PARAMETERS->letterMultiplier(row, col);
#ifdef DEBUG_V2GEN			
			UVcout << "letterMultiplier: " << letterMultiplier << endl;
#endif			
			mainScore += letterMultiplier *
				QUACKLE_ALPHABET_PARAMETERS->score(m_placed[pos]);
		}
		//UVcout << "col: " << col << ", row: " << row << endl;
		//		if (spot.horizontal) ++col; else ++row;
		col++;
		//UVcout << "(incremented) col: " << col << ", row: " << row << endl;		
	}
	for (int i = 0; i < ahead; ++i) {
		//FIXME: needs to handle playThrough
		//if (m_placed[pos] != QUACKLE_NULL_MARK) {
#ifdef DEBUG_V2GEN			
		UVcout << "QUACKLE_BOARD_PARAMETERS->wordMultiplier(" << row << ", "
					 << col << "): "
					 << QUACKLE_BOARD_PARAMETERS->wordMultiplier(row, col) << endl;
#endif
		wordMultiplier *= QUACKLE_BOARD_PARAMETERS->wordMultiplier(row, col);
		//UVcout << "pos: " << pos << " m_placed[pos]: " << pos << endl;
		//assert(QUACKLE_ALPHABET_PARAMETERS->isPlainLetter(m_placed[pos]));
		if (QUACKLE_ALPHABET_PARAMETERS->isPlainLetter(m_placed[pos])) {
			int letterMultiplier = QUACKLE_BOARD_PARAMETERS->letterMultiplier(row, col);
#ifdef DEBUG_V2GEN			
			UVcout << "letterMultiplier: " << letterMultiplier << endl;
#endif
			mainScore += letterMultiplier *
				QUACKLE_ALPHABET_PARAMETERS->score(m_placed[pos]);
		}
		++pos;
		//UVcout << "col: " << col << ", row: " << row << endl;
		//if (spot.horizontal) ++col; else ++row;
		col++;
		//UVcout << "(incremented) col: " << col << ", row: " << row << endl;		
	}
#ifdef DEBUG_V2GEN			
	UVcout << "wordMultiplier: " << wordMultiplier << endl;
#endif	
	mainScore *= wordMultiplier;
	// TODO: add scores for hooks!
	if (ahead + 1 + behind == QUACKLE_PARAMETERS->rackSize()) {
		mainScore += QUACKLE_PARAMETERS->bingoBonus();
	}
	return mainScore;
}

double V2Generator::getLeave() const {
	uint64_t product = 1;
	for (int i = QUACKLE_BLANK_MARK; i <= QUACKLE_ALPHABET_PARAMETERS->lastLetter(); ++i) {
		for (int j = 0; j < m_counts[i]; ++j) {
			product *= QUACKLE_PRIMESET->lookUpTile(i);
		}
	}
	return QUACKLE_STRATEGY_PARAMETERS->primeleave(product);
}

bool V2Generator::nextLetter(const V2Gaddag& gaddag, const unsigned char* node,
														 uint32_t restriction,
														 Letter minLetter, int* childIndex, Letter* foundLetter,
														 const unsigned char** child) const {
	*child = gaddag.nextRackChild(node, minLetter, restriction, childIndex,
																foundLetter);
#ifdef DEBUG_V2GEN
	if (*child == NULL) {
		UVcout << "child == NULL; no more children on rack." << endl;
	} else {
		UVcout << "foundLetter: " << static_cast<int>(*foundLetter) << endl;
	}
#endif
	return *child != NULL;
}

bool V2Generator::nextLetter(const V2Gaddag& gaddag, const unsigned char* node,
														 Letter minLetter, int* childIndex, Letter* foundLetter,
														 const unsigned char** child) const {
	return nextLetter(gaddag, node, m_rackBits, minLetter, childIndex,
										foundLetter, child);
}

bool V2Generator::nextBlankLetter(const V2Gaddag& gaddag, const unsigned char* node,
																	Letter minLetter, int* childIndex,
																	Letter* foundLetter,
																	const unsigned char** child) const {
	*child = gaddag.nextChild(node, minLetter, *childIndex, foundLetter);
#ifdef DEBUG_V2GEN
	if (*child == NULL) {
		UVcout << "child == NULL; no more children." << endl;
	} else {
		UVcout << "foundLetter: " << static_cast<int>(*foundLetter) << endl;
	}
#endif

	if (*child == NULL) return false;
	if (*foundLetter == QUACKLE_GADDAG_SEPARATOR) {
		(*childIndex)++;
		*child = gaddag.nextChild(node, (*foundLetter) + 1, *childIndex, foundLetter);
#ifdef DEBUG_V2GEN
		if (*child == NULL) {
			UVcout << "child == NULL; no child besides gaddag separator." << endl;
		} else {
			UVcout << "foundLetter: " << static_cast<int>(*foundLetter) << endl;
		}
#endif
    if (*child == NULL) return false;
	}
	return true;
}

// updates m_mainWordScore, returns tile score so it can be subtracted later
int V2Generator::scoreLetter(int pos, Letter letter, int letterMultiplier) {
#ifdef DEBUG_V2GEN
	assert(letter >= QUACKLE_ALPHABET_PARAMETERS->firstLetter());
	assert(letter <= QUACKLE_ALPHABET_PARAMETERS->lastLetter());
	UVcout << "placing " << QUACKLE_ALPHABET_PARAMETERS->userVisible(letter)
				 << " at m_placed[" << pos << "]" << endl;
#endif
	m_placed[pos] = letter;
	const int letterScore = QUACKLE_ALPHABET_PARAMETERS->score(letter);
	const int tileMainScore = letterScore * letterMultiplier;
	m_mainWordScore += tileMainScore;
	return tileMainScore;	
}

void V2Generator::debugPlaced(const Spot& spot, int behind, int ahead) const {
	UVcout << "m_rackBits: " << debugLetterMask(m_rackBits) << endl;
	UVcout << "counts: " << counts2string() << endl;
	int startCol = spot.anchorCol - behind;
	LetterString word;
	for (int i = startCol; i < spot.anchorCol + ahead + 1; ++i) {
		assert(i >= 0); assert(i < 15);
		word += m_placed[i];
	}
	UVcout << "m_placed[" << startCol << " to " << spot.anchorCol + ahead
				 << "]: " << QUACKLE_ALPHABET_PARAMETERS->userVisible(word) << endl;
}

void V2Generator::useLetter(Letter letter, uint32_t* foundLetterMask) {
	//assert(m_counts[letter] > 0);
	m_counts[letter]--;
	//assert(m_counts[letter] >= 0);
	*foundLetterMask = 1 << letter;
	if (m_counts[letter] == 0) m_rackBits &= ~(*foundLetterMask);
}

void V2Generator::unuseLetter(Letter letter, uint32_t foundLetterMask) {
	m_counts[letter]++;
	m_rackBits |= foundLetterMask;	
}

bool V2Generator::maybeRecordMove(const Spot& spot, int wordMultiplier,
																	int behind, int ahead, int numPlaced) {
	int score = m_mainWordScore * wordMultiplier;
	if (numPlaced == QUACKLE_PARAMETERS->rackSize()) {
		score += QUACKLE_PARAMETERS->bingoBonus();
	}
	//assert(score == scorePlay(spot, behind, ahead));
	double equity = score;
	if (numPlaced < 7) equity += getLeave();
	if (equity > m_best.equity) {
		LetterString word;
		int startCol = spot.anchorCol - behind;
		for (int i = startCol; i < spot.anchorCol + ahead + 1; ++i) {
			//assert(i >= 0); assert(i < 15);
			//UVcout << "letter: " << static_cast<int>(m_placed[i]) << endl;
			word += m_placed[i];
		}
		Move move = Move::createPlaceMove(7, startCol, spot.horizontal, word);
		move.score = score;
		move.equity = equity;
		m_best = move;
		return true;
	}
	return false;
}

// TODO: Make this clever enough to handle all the "through" tiles.
// Should look up board positions of squares relative to anchor+delta
// before calling findBlankless etc
void V2Generator::getSquare(const Spot& spot, int delta,
														int* row, int* col, int* pos) const {
	if (spot.horizontal) {
		*row = spot.anchorRow;
		*col = spot.anchorCol + delta;
		*pos = *col;
	} else {
		*row = spot.anchorRow + delta;
		*col = spot.anchorCol;
		*pos = *row;
	}
#ifdef DEBUG_V2GEN
  UVcout << "row: " << *row << ", col: " << *col << ", pos: " << *pos << endl;
	assert(*col >= 0); assert(*col < 15); assert(*row >= 0); assert(*row < 15);
#endif
}

void V2Generator::findMoreBlankless(Spot* spot, int delta, int ahead,
																		int behind, int velocity, int wordMultiplier,
																		const V2Gaddag& gaddag, const unsigned char* node) {
	// FIXME: all this assumes horizontal
	if (velocity < 0) {
		findBlankless(spot, delta - 1, 0, behind + 1, -1, wordMultiplier, node);
#ifdef V2GEN
		UVcout << "changing direction..." << endl;
#endif
		const unsigned char* changeChild = gaddag.changeDirection(node);
		if (changeChild != NULL) {
			node = gaddag.followIndex(changeChild);
			if (node != NULL) { 
				findBlankless(spot, 1, 1, behind, 1, wordMultiplier, node);
			}
		}
	} else {
		findBlankless(spot, delta + 1, ahead + 1, behind, 1, wordMultiplier, node);
	}
}

void V2Generator::findMoreBlankable(Spot* spot, int delta, int ahead,
																		int behind, int velocity, int wordMultiplier,
																		const V2Gaddag& gaddag, const unsigned char* node) {
	// FIXME: all this assumes horizontal
	if (velocity < 0) {
		findBlankable(spot, delta - 1, 0, behind + 1, -1, wordMultiplier, node);
#ifdef V2GEN
		UVcout << "changing direction..." << endl;
#endif
		const unsigned char* changeChild = gaddag.changeDirection(node);
		if (changeChild != NULL) {
			node = gaddag.followIndex(changeChild);
			if (node != NULL) { 
				findBlankable(spot, 1, 1, behind, 1, wordMultiplier, node);
			}
		}
	} else {
		findBlankable(spot, delta + 1, ahead + 1, behind, 1, wordMultiplier, node);
	}
}

void V2Generator::findBlankless(Spot* spot, int delta, int ahead, int behind,
																int velocity, int wordMultiplier,
																const unsigned char* node) {
	// TODO: use more specific gaddags based on min/max word length for this spot
	const V2Gaddag& gaddag = *(QUACKLE_LEXICON_PARAMETERS->v2Gaddag());
#ifdef DEBUG_V2GEN
	UVcout << "findBlankless(delta: " << delta<< ", ahead: " << ahead
				 << ", behind: " << behind << ", velocity: " << velocity << ")" << endl;
#endif
	int numPlaced, row, col, pos;
	// Not true for "through" spots!
  numPlaced =	1 + ahead + behind;
	getSquare(*spot, delta, &row, &col, &pos);
	int newWordMultiplier = wordMultiplier *
		QUACKLE_BOARD_PARAMETERS->wordMultiplier(row, col);
	int letterMultiplier = QUACKLE_BOARD_PARAMETERS->letterMultiplier(row, col);

	Letter minLetter = QUACKLE_GADDAG_SEPARATOR;
	int childIndex = 0;
	Letter foundLetter;
	const unsigned char* child = NULL;
	while (nextLetter(gaddag, node, minLetter, &childIndex, &foundLetter, &child)) {
		if (child == NULL) UVcout << "child is still NULL!" << endl;
		//uint64_t childOffset = child - gaddag.root();
		//UVcout << "child: m_data+" << childOffset << endl;
    const int tileMainScore = scoreLetter(pos, foundLetter, letterMultiplier);		
		uint32_t foundLetterMask; // reused below in unuseLetter
    useLetter(foundLetter, &foundLetterMask);
#ifdef DEBUG_V2GEN
		debugPlaced(*spot, behind, ahead);
#endif		
		// FIXME: all this assumes horizontal
		// Test it by making the empty-board spot vertie!
		if (spot->viableAtLength(numPlaced) &&
				gaddag.completesWord(child) &&
				maybeRecordMove(*spot, newWordMultiplier, behind, ahead, numPlaced)) {
#ifdef DEBUG_V2GEN
			UVcout << "better than " << m_best.equity;
#endif
			restrictByLength(spot);
#ifdef DEBUG_V2GEN
			UVcout << endl;
#endif
		}
		if (numPlaced + 1 <= spot->longestViable) {
			const unsigned char* newNode = gaddag.followIndex(child);
			if (newNode != NULL) {
				findMoreBlankless(spot, delta, ahead, behind, velocity, newWordMultiplier,
													gaddag, newNode);
			}
		}
#ifdef DEBUG_V2GEN
		else {
			UVcout << "don't need to check longer than " << numPlaced << endl;
		}
#endif
		
		m_mainWordScore -= tileMainScore;
		unuseLetter(foundLetter, foundLetterMask);

		if (numPlaced > spot->longestViable) {
#ifdef DEBUG_V2GEN
			UVcout << "don't need to check any more of length " << numPlaced << endl;
#endif
			break;
		}
		minLetter = foundLetter + 1;
		++childIndex;
	}
}


void V2Generator::findBlankable(Spot* spot, int delta, int ahead, int behind,
																int velocity, int wordMultiplier,
																const unsigned char* node) {
	// TODO: use more specific gaddags based on min/max word length for this spot
	const V2Gaddag& gaddag = *(QUACKLE_LEXICON_PARAMETERS->v2Gaddag());
#ifdef DEBUG_V2GEN
	UVcout << "findBlankable(delta: " << delta<< ", ahead: " << ahead
				 << ", behind: " << behind << ", velocity: " << velocity << ")" << endl;
#endif
	int numPlaced, row, col, pos;
	// Not true for "through" spots!
  numPlaced =	1 + ahead + behind;
	getSquare(*spot, delta, &row, &col, &pos);
	int newWordMultiplier = wordMultiplier *
		QUACKLE_BOARD_PARAMETERS->wordMultiplier(row, col);

	Letter minLetter = QUACKLE_GADDAG_SEPARATOR;
	int childIndex = 0;
	Letter foundLetter;
	assert(m_counts[QUACKLE_BLANK_MARK] > 0);
	const unsigned char* child = NULL;
	// TODO: since there might be hooks, should also have a version of this that
	// is restricted to letters that can hook. (nextLetter already good enough?)
  while (nextBlankLetter(gaddag, node, minLetter, &childIndex, &foundLetter,
												 &child)) {
		assert(child != NULL);
    m_counts[QUACKLE_BLANK_MARK]--;
		
    Letter blankLetter = QUACKLE_ALPHABET_PARAMETERS->setBlankness(foundLetter);
#ifdef DEBUG_V2GEN
		assert(foundLetter >= QUACKLE_ALPHABET_PARAMETERS->firstLetter());
		assert(foundletter <= QUACKLE_ALPHABET_PARAMETERS->lastLetter());
		UVcout << "placing " << QUACKLE_ALPHABET_PARAMETERS->userVisible(blankLetter)
					 << " at m_placed[" << pos << "]" << endl;
#endif
		m_placed[pos] = blankLetter;

		// FIXME: all this assumes horizontal
		// Test it by making the empty-board spot vertie!
		if (spot->viableAtLength(numPlaced) &&
				gaddag.completesWord(child) &&
				maybeRecordMove(*spot, newWordMultiplier, behind, ahead, numPlaced)) {
#ifdef DEBUG_V2GEN
			UVcout << "better than " << m_best.equity;
#endif
			restrictByLength(spot);
#ifdef DEBUG_V2GEN
			UVcout << endl;
#endif
		}
		if (numPlaced + 1 <= spot->longestViable) {
			const unsigned char* newNode = gaddag.followIndex(child);
			if (newNode != NULL) {
				if (m_counts[QUACKLE_BLANK_MARK] > 0) {
					findMoreBlankable(spot, delta, ahead, behind, velocity,
														newWordMultiplier, gaddag, newNode);

				} else {
					findMoreBlankless(spot, delta, ahead, behind, velocity,
														newWordMultiplier, gaddag, newNode);
				}
			}
		}
#ifdef DEBUG_V2GEN
		else {
			UVcout << "don't need to check longer than " << numPlaced << endl;
		}
#endif

		m_counts[QUACKLE_BLANK_MARK]++;
		minLetter = foundLetter + 1;
		++childIndex;
	}
	if (m_rackBits == 0) return;
	minLetter = QUACKLE_GADDAG_SEPARATOR;
	childIndex = 0;
	child = NULL;
	int letterMultiplier = QUACKLE_BOARD_PARAMETERS->letterMultiplier(row, col);
	while (nextLetter(gaddag, node, minLetter, &childIndex, &foundLetter, &child)) {
		assert(child != NULL);
    const int tileMainScore = scoreLetter(pos, foundLetter, letterMultiplier);		
		uint32_t foundLetterMask; // reused below in unuseLetter
    useLetter(foundLetter, &foundLetterMask);
#ifdef DEBUG_V2GEN
		debugPlaced(*spot, behind, ahead);
#endif		
		// FIXME: all this assumes horizontal
		// Test it by making the empty-board spot vertie!
		if (spot->viableAtLength(numPlaced) &&
				gaddag.completesWord(child) &&
				maybeRecordMove(*spot, newWordMultiplier, behind, ahead, numPlaced)) {
			//#ifdef DEBUG_V2GEN
			UVcout << "better than " << m_best.equity;
			//#endif
			restrictByLength(spot);
			//#ifdef DEBUG_V2GEN
			UVcout << endl;
			//#endif
		}
		if (numPlaced + 1 <= spot->longestViable) {
			const unsigned char* newNode = gaddag.followIndex(child);
			if (newNode != NULL) {
				findMoreBlankless(spot, delta, ahead, behind, velocity, newWordMultiplier,
													gaddag, newNode);
			}
		}
#ifdef DEBUG_V2GEN
		else {
			UVcout << "don't need to check longer than " << numPlaced << endl;
		}
#endif
		
		m_mainWordScore -= tileMainScore;
		unuseLetter(foundLetter, foundLetterMask);

		if (numPlaced > spot->longestViable) {
#ifdef DEBUG_V2GEN
			UVcout << "don't need to check any more of length " << numPlaced << endl;
#endif
			break;
		}
		minLetter = foundLetter + 1;
		++childIndex;
	}
}

float V2Generator::bestLeave(const Spot& spot, int length) const {
	assert(length > 0);
	assert(length < 7);
	// TODO: Handle single letter playedThrough too
	assert(spot.playedThrough.length() == 0);
	if (m_anagrams != NULL) {
		const UsesTiles& usesTiles =
			spot.canUseBlank ? m_anagrams->usesWhatever : m_anagrams->usesNoBlanks;
		const NTileAnagrams& anagrams = usesTiles.thruNone;
		int index = length - 1;
		return anagrams.bestLeaves[index] / 256.0f;
	}
	return m_bestLeaves[length];
}

void V2Generator::scoreSpot(Spot* spot) {
	spot->canMakeAnyWord = false;
	const int numTiles = rack().size();
	const LetterString& tiles = rack().tiles();
	int tileScores[QUACKLE_MAXIMUM_BOARD_SIZE];
	bool assumeBlankPlayed = false;
	for (int i = 0; i < numTiles; ++i) {
		tileScores[i] = QUACKLE_ALPHABET_PARAMETERS->score(tiles[i]);
		if (tileScores[i] == 0) assumeBlankPlayed = true;
	}
	std::sort(tileScores, tileScores + numTiles, std::greater<int>());
	if (!spot->canUseBlank) assumeBlankPlayed = false;
	int throughScore = 0;
	for (const Letter letter : spot->playedThrough) {
		if (letter == QUACKLE_NULL_MARK) continue;
		if (QUACKLE_ALPHABET_PARAMETERS->isBlankLetter(letter)) {
			throughScore += QUACKLE_ALPHABET_PARAMETERS->score(QUACKLE_BLANK_MARK);
		} else {
			throughScore += QUACKLE_ALPHABET_PARAMETERS->score(letter);
		}
	}
	float maxEquity = -9999;
	for (int i = 1; i <= 7; ++i) {
		spot->worthChecking[i].maxEquity = maxEquity;
		spot->worthChecking[i].couldBeBest = false;
	}
	
	int wordMultipliers[QUACKLE_MAXIMUM_BOARD_SIZE];
	int letterMultipliers[QUACKLE_MAXIMUM_BOARD_SIZE];
	int row = spot->anchorRow;
	int col = spot->anchorCol;
	int anchorPos = (spot->horizontal) ? col : row;
	for (int i = 0; i <= spot->maxTilesAhead; ++i) {
		if (board()->isNonempty(row, col)) {
			i--;
		} else {
			wordMultipliers[anchorPos + i] =
				QUACKLE_BOARD_PARAMETERS->wordMultiplier(row, col);
			letterMultipliers[anchorPos + i] =
				QUACKLE_BOARD_PARAMETERS->letterMultiplier(row, col);
		}
		if (spot->horizontal) col++; else row++;
	}
	row = spot->anchorRow;
	col = spot->anchorCol;
	for (int i = 1; i <= spot->maxTilesBehind; ++i) {
		if (spot->horizontal) col--; else row--;
		if (board()->isNonempty(row, col)) {
			i--;
		} else {
			wordMultipliers[anchorPos - i] =
				QUACKLE_BOARD_PARAMETERS->wordMultiplier(row, col);
			letterMultipliers[anchorPos - i] =
				QUACKLE_BOARD_PARAMETERS->letterMultiplier(row, col);
		}
	}
  for (int ahead = spot->minTilesAhead; ahead <= spot->maxTilesAhead; ++ahead) {
		// num tiles played behind anchor + ahead of anchor <= num tiles on rack
		const int maxBehind = std::min(spot->maxTilesBehind, numTiles - ahead);
		for (int behind = 0; behind <= maxBehind; ++behind) {
			//struct timeval start, end;
			//gettimeofday(&start, NULL);

			// This is all wrong except for empty boards
			const int played = ahead + behind;
			if (played < 2) continue; // have to play at least one tile!
			
			if (!couldMakeWord(*spot, played)) {
				//UVcout << "can not make word of length " << played << endl;
				continue;
			} else {
				spot->worthChecking[played].couldBeBest = true;
				//UVcout << "can make word of length " << played << endl;
			}
			spot->canMakeAnyWord = true;
			int wordMultiplier = 1;
			int usedLetterMultipliers[QUACKLE_MAXIMUM_BOARD_SIZE];
			int minPos = anchorPos - behind;
			for (int pos = minPos; pos < minPos + played; ++pos) {
				wordMultiplier *= wordMultipliers[pos];
			}
			memcpy(&usedLetterMultipliers, &(letterMultipliers[minPos]),
						 played * sizeof(int));
			std::sort(usedLetterMultipliers,
								usedLetterMultipliers + played,
								std::greater<int>());
			int playedScore = 0;
			int maxNonBlankTiles = played;
			if (assumeBlankPlayed) maxNonBlankTiles--;
			for (int i = 0; i < maxNonBlankTiles; ++i) {
				playedScore += usedLetterMultipliers[i] * tileScores[i];
			}
			int score = (throughScore + playedScore) * wordMultiplier;
			if (played == QUACKLE_PARAMETERS->rackSize()) {
				score += QUACKLE_PARAMETERS->bingoBonus();
			}
			float optimisticEquity = score;
			if (played < 7) {
				optimisticEquity += bestLeave(*spot, played);
				/*
				UVcout << "use?:" << spot->canUseBlank << ", played: " << played
							 << ", minPos: " << minPos << ", score: " << score
							 << ", leave: " << bestLeave(*spot, played)
							 << ", optimisticEquity: " << optimisticEquity << endl;
				*/
			}
			maxEquity = std::max(maxEquity, optimisticEquity);
			spot->worthChecking[played].maxEquity =
				std::max(spot->worthChecking[played].maxEquity, optimisticEquity);
			// gettimeofday(&end, NULL);
			// UVcout << "Time scoring behind: " << behind << " ahead: " << ahead << " "
			// 	 << ((end.tv_sec * 1000000 + end.tv_usec)
			// 			 - (start.tv_sec * 1000000 + start.tv_usec)) << " microseconds." << endl;

		}
	}
	if (!spot->canMakeAnyWord) return;
	spot->maxEquity = maxEquity;
	// UVcout << "Spot: (" << spot->anchorRow << ", " << spot->anchorCol
	// 			 << ", blank: " << spot->canUseBlank << ") "
	// 			 << spot->maxEquity << endl;
}

bool V2Generator::isEmpty(int row, int col) {
	return board()->letter(row, col) == QUACKLE_NULL_MARK;
}

Letter V2Generator::boardLetter(int row, int col) {
	return board()->letter(row, col);
}

const unsigned char* V2Generator::followLetter(const V2Gaddag& gaddag,
																							 int row, int col,
																							 const unsigned char* node) {
	Letter letter = QUACKLE_ALPHABET_PARAMETERS->clearBlankness(boardLetter(row, col));
	if (!gaddag.hasChild(node, letter)) return NULL;
	const unsigned char* child = gaddag.child(node, letter);
	return gaddag.followIndex(child);
}

const unsigned char* V2Generator::vertBeforeNode(int anchorRow, int col,
																								 int numLetters) {
	const V2Gaddag& gaddag = *(QUACKLE_LEXICON_PARAMETERS->v2Gaddag());
	const unsigned char* node = gaddag.root();
	int startRow = anchorRow - numLetters;
	Letter letter = boardLetter(startRow, col);
	letter = QUACKLE_ALPHABET_PARAMETERS->clearBlankness(letter);
	if (!gaddag.hasChild(node, letter)) return NULL;
	const unsigned char* child = gaddag.child(node, letter);
	node = QUACKLE_LEXICON_PARAMETERS->v2Gaddag()->followIndex(child);
	const unsigned char* changeChild = gaddag.changeDirection(node);
  node = QUACKLE_LEXICON_PARAMETERS->v2Gaddag()->followIndex(changeChild);	
	for (int row = startRow + 1; row < anchorRow; ++row) {
		node = followLetter(gaddag, row, col, node);
		if (node == NULL) break;
	}
	return node;
}

const unsigned char* V2Generator::vertAfterNode(int anchorRow, int col,
																								int numLetters) {
	const V2Gaddag& gaddag = *(QUACKLE_LEXICON_PARAMETERS->v2Gaddag());
	const unsigned char* node = gaddag.root();
	int startRow = anchorRow + numLetters;
	Letter letter = boardLetter(startRow, col);
	letter = QUACKLE_ALPHABET_PARAMETERS->clearBlankness(letter);
	if (!gaddag.hasChild(node, letter)) return NULL;
	const unsigned char* child = gaddag.child(node, letter);
	node = QUACKLE_LEXICON_PARAMETERS->v2Gaddag()->followIndex(child);
	for (int row = startRow - 1; row > anchorRow; --row) {
		node = followLetter(gaddag, row, col, node);
		if (node == NULL) break;
	}
	return node;
}

const unsigned char* V2Generator::horizBeforeNode(int row, int anchorCol,
																									int numLetters) {
	const V2Gaddag& gaddag = *(QUACKLE_LEXICON_PARAMETERS->v2Gaddag());
	const unsigned char* node = gaddag.root();
	int startCol = anchorCol - numLetters;
	Letter letter = boardLetter(row, startCol);
	letter = QUACKLE_ALPHABET_PARAMETERS->clearBlankness(letter);
	if (!gaddag.hasChild(node, letter)) return NULL;
	const unsigned char* child = gaddag.child(node, letter);
	node = QUACKLE_LEXICON_PARAMETERS->v2Gaddag()->followIndex(child);
	const unsigned char* changeChild = gaddag.changeDirection(node);
  node = QUACKLE_LEXICON_PARAMETERS->v2Gaddag()->followIndex(changeChild);	
	for (int col = startCol + 1; col < anchorCol; ++col) {
		node = followLetter(gaddag, row, col, node);
		if (node == NULL) break;
	}
	return node;
}

const unsigned char* V2Generator::horizAfterNode(int row, int anchorCol,
																								 int numLetters) {
	const V2Gaddag& gaddag = *(QUACKLE_LEXICON_PARAMETERS->v2Gaddag());
	const unsigned char* node = gaddag.root();
	int startCol = anchorCol + numLetters;
	Letter letter = boardLetter(row, startCol);
	letter = QUACKLE_ALPHABET_PARAMETERS->clearBlankness(letter);
	if (!gaddag.hasChild(node, letter)) return NULL;
	const unsigned char* child = gaddag.child(node, letter);
	node = QUACKLE_LEXICON_PARAMETERS->v2Gaddag()->followIndex(child);
	for (int col = startCol - 1; col > anchorCol; --col) {
		letter = QUACKLE_ALPHABET_PARAMETERS->clearBlankness(boardLetter(row, col));
		if (!gaddag.hasChild(node, letter)) return NULL;
		child = gaddag.child(node, letter);
		node = gaddag.followIndex(child);
		if (node == NULL) break;
	}
	return node;
}

uint32_t V2Generator::wordCompleters(const unsigned char* node) {
	uint32_t completers = 0;
	const V2Gaddag& gaddag = *(QUACKLE_LEXICON_PARAMETERS->v2Gaddag());
	Letter minLetter = QUACKLE_GADDAG_SEPARATOR;
	int childIndex = 0;
	Letter foundLetter;
  const unsigned char* child;
	while (nextBlankLetter(gaddag, node, minLetter, &childIndex, &foundLetter,
												 &child)) {
		assert(child != NULL);
		if (gaddag.completesWord(child)) completers |= (1 << foundLetter);
		minLetter = foundLetter + 1;
		++childIndex;
	}
	return completers;
}

bool V2Generator::vertCompletesWord(const V2Gaddag& gaddag, int row, int col,
																		const unsigned char* node,
																		int numLettersAfter) {
	for (int i = 0; i < numLettersAfter - 1; ++i) {
		node = followLetter(gaddag, row, col, node);
		if (node == NULL) return false;
		row++;
	}
	Letter letter = QUACKLE_ALPHABET_PARAMETERS->clearBlankness(boardLetter(row, col));
	if (!gaddag.hasChild(node, letter)) return false;
	const unsigned char* child = gaddag.child(node, letter);
	if (child == NULL) return false;
	return gaddag.completesWord(child);
}

bool V2Generator::horizCompletesWord(const V2Gaddag& gaddag, int row, int col,
																		 const unsigned char* node,
																		 int numLettersAfter) {
	for (int i = 0; i < numLettersAfter - 1; ++i) {
		node = followLetter(gaddag, row, col, node);
		if (node == NULL) return false;
		col++;
	}
	Letter letter = QUACKLE_ALPHABET_PARAMETERS->clearBlankness(boardLetter(row, col));
	if (!gaddag.hasChild(node, letter)) return false;
	const unsigned char* child = gaddag.child(node, letter);
	if (child == NULL) return false;
	return gaddag.completesWord(child);
}

uint32_t V2Generator::vertBetween(int row, int col, int numLettersAfter,
																	const unsigned char* beforeNode,
																	const unsigned char* afterNode) {
	const V2Gaddag& gaddag = *(QUACKLE_LEXICON_PARAMETERS->v2Gaddag());
	uint32_t between = 0;
	const uint32_t restriction = gaddag.sharedChildren(beforeNode, afterNode);
	Letter minLetter = QUACKLE_GADDAG_SEPARATOR;
	int childIndex = 0;
	Letter foundLetter;
	const unsigned char* child = NULL;
	while (nextLetter(gaddag, beforeNode, restriction, minLetter,
										&childIndex, &foundLetter, &child)) {
		const unsigned char* node = gaddag.followIndex(child);
		if ((node != NULL) &&
				vertCompletesWord(gaddag, row + 1, col, node, numLettersAfter)) {
			between |= (1 << foundLetter);
		}
		minLetter = foundLetter + 1;
		++childIndex;
	}
	return between;
}

uint32_t V2Generator::horizBetween(int row, int col, int numLettersAfter,
																	const unsigned char* beforeNode,
																	const unsigned char* afterNode) {
	const V2Gaddag& gaddag = *(QUACKLE_LEXICON_PARAMETERS->v2Gaddag());
	uint32_t between = 0;
	uint32_t restriction = gaddag.sharedChildren(beforeNode, afterNode);
	Letter minLetter = QUACKLE_GADDAG_SEPARATOR;
	int childIndex = 0;
	Letter foundLetter;
	const unsigned char* child = NULL;
	while (nextLetter(gaddag, beforeNode, restriction, minLetter,
										&childIndex, &foundLetter, &child)) {
		const unsigned char* node = gaddag.followIndex(child);
		if ((node != NULL) &&
				horizCompletesWord(gaddag, row, col + 1, node, numLettersAfter)) {
			between |= (1 << foundLetter);
		}
		minLetter = foundLetter + 1;
		++childIndex;
	}
	return between;
}

uint32_t V2Generator::verticalHooks(int row, int col) {
	//UVcout << "row: " << row << ", col: " << col << endl;
	assert(isEmpty(row, col));
	int numLettersBefore = 0;
	for (int beforeRow = row - 1; beforeRow >= 0; beforeRow--) {
		if (isEmpty(beforeRow, col)) break;
		++numLettersBefore;
	}
	int numLettersAfter = 0;
	for (int afterRow = row + 1; afterRow < 15; afterRow++) {
		if (isEmpty(afterRow, col)) break;
		++numLettersAfter;
	}
	//UVcout << "numLettersBefore: " << numLettersBefore << endl;
	//UVcout << "numLettersAfter: " << numLettersAfter << endl;
	assert(numLettersBefore > 0 || numLettersAfter > 0);
	const unsigned char* beforeNode = NULL;
	if (numLettersBefore > 0) {
		beforeNode = vertBeforeNode(row, col, numLettersBefore);
		if (beforeNode == NULL) return 0;
	}
	const unsigned char* afterNode = NULL;
	if (numLettersAfter > 0) {
		afterNode = vertAfterNode(row, col, numLettersAfter);
		if (afterNode == NULL) return 0;
	}
	if (numLettersAfter == 0) {
		UVcout << "wordCompleters: " << debugLetterMask(wordCompleters(beforeNode))
					 << endl;
		return wordCompleters(beforeNode);
	}
	if (numLettersBefore == 0) {
		/*
		UVcout << "wordCompleters: " << debugLetterMask(wordCompleters(afterNode))
					 << endl;
		*/
		return wordCompleters(afterNode);
	}
	if (beforeNode == NULL || afterNode == NULL) return 0;
	/*
	UVcout << "between: "
				 << debugLetterMask(vertBetween(row, col, numLettersAfter, beforeNode, afterNode))
				 << endl;
	*/
	return vertBetween(row, col, numLettersAfter, beforeNode, afterNode);
}

uint32_t V2Generator::horizontalHooks(int row, int col) {
	//UVcout << "row: " << row << ", col: " << col << endl;
	assert(isEmpty(row, col));
	int numLettersBefore = 0;
	for (int beforeCol = col - 1; beforeCol >= 0; beforeCol--) {
		if (isEmpty(row, beforeCol)) break;
		++numLettersBefore;
	}
	int numLettersAfter = 0;
	for (int afterCol = col + 1; afterCol < 15; afterCol++) {
		if (isEmpty(row, afterCol)) break;
		++numLettersAfter;
	}
	//UVcout << "numLettersBefore: " << numLettersBefore << endl;
	//UVcout << "numLettersAfter: " << numLettersAfter << endl;
	assert(numLettersBefore > 0 || numLettersAfter > 0);
	const unsigned char* beforeNode = NULL;
	if (numLettersBefore > 0) {
		beforeNode = horizBeforeNode(row, col, numLettersBefore);
	}
	const unsigned char* afterNode = NULL;
	if (numLettersAfter > 0) {
		afterNode = horizAfterNode(row, col, numLettersAfter);
	}
	if (numLettersAfter == 0) {
		UVcout << "wordCompleters: " << debugLetterMask(wordCompleters(beforeNode))
					 << endl;
		return wordCompleters(beforeNode);
	}
	if (numLettersBefore == 0) {
		//UVcout << "wordCompleters: " << debugLetterMask(wordCompleters(afterNode))
		//			 << endl;
		return wordCompleters(afterNode);
	}
	if (beforeNode == NULL || afterNode == NULL) return 0;
	/*
	UVcout << "between: "
				 << debugLetterMask(horizBetween(row, col, numLettersAfter, beforeNode, afterNode))
				 << endl;
	*/
	return horizBetween(row, col, numLettersAfter, beforeNode, afterNode);
}

void V2Generator::addThroughSpots(bool horiz, vector<Spot>* spots,
																	int* row, int* col) {
}

void V2Generator::maybeAddHookSpot(int row, int col, bool horiz,
																	 vector<Spot>* spots) {
}

void V2Generator::findSpots(vector<Spot>* spots) {
	UVcout << "findSpots()..." << endl;
	bool rackHasBlank = false;
	for (unsigned int i = 0; i < rack().size(); ++i) {
		if (rack().tiles()[i] == QUACKLE_BLANK_MARK) {
			rackHasBlank = true;
			break;
		}
	}
	const int numTiles = rack().size();
	for (bool horiz : {false, true})  {
		for (int row = 0; row < 15; ++row) {
			for (int col = 0; col < 15; ++col) {
				if (board()->isNonempty(row, col)) {
					addThroughSpots(horiz, spots, &row, &col);
				} else {
					maybeAddHookSpot(row, col, horiz, spots);
				}
			}
		}
	}
}

void V2Generator::findEmptyBoardSpots(vector<Spot>* spots) {
	bool rackHasBlank = false;
	for (unsigned int i = 0; i < rack().size(); ++i) {
		if (rack().tiles()[i] == QUACKLE_BLANK_MARK) {
			rackHasBlank = true;
			break;
		}
	}
	const int numTiles = rack().size();

	const int anchorRow = QUACKLE_BOARD_PARAMETERS->startRow();
	Spot spot;
	spot.anchorRow = anchorRow;
	spot.anchorCol = QUACKLE_BOARD_PARAMETERS->startColumn();
	spot.canUseBlank = true;
	spot.horizontal = true;
	spot.throughScore = 0;
	spot.maxTilesBehind = numTiles - 1;
	spot.minTilesAhead = 1;
	spot.maxTilesAhead = numTiles;
	spot.longestViable = 7;
	//struct timeval start, end;
	//gettimeofday(&start, NULL);
	scoreSpot(&spot);
	// gettimeofday(&end, NULL);
	// UVcout << "Time scoring spot was "
	// 			 << ((end.tv_sec * 1000000 + end.tv_usec)
	// 					 - (start.tv_sec * 1000000 + start.tv_usec)) << " microseconds." << endl;
	if (spot.canMakeAnyWord) {
		if (rackHasBlank) {
			Spot blankSavingSpot = spot;
			blankSavingSpot.canUseBlank = false;
			scoreSpot(&blankSavingSpot);
			if (blankSavingSpot.canMakeAnyWord) {
				spots->push_back(blankSavingSpot);
				spot.maxEquity -= m_blankSpendingEpsilon;
			}
		} 
		spots->push_back(spot);
	}
}

void V2Generator::setUpCounts(const LetterString &letters) {
  String::counts(letters, m_counts);
}

UVString V2Generator::counts2string() const {
	UVString ret;

	for (Letter i = 0; i <= QUACKLE_ALPHABET_PARAMETERS->lastLetter(); i++)
		for (int j = 0; j < m_counts[i]; j++)
			ret += QUACKLE_ALPHABET_PARAMETERS->userVisible(i);

	return ret;
}
