#include <set>
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

V2Generator::V2Generator(const GamePosition &position,
												 const map<Product, vector<LetterString>>& bingos)
  : m_position(position),
		m_bingoMap(&bingos) {
}

V2Generator::~V2Generator() {}

unsigned int V2Generator::m_tiebreakDividend;

MoveList V2Generator::kibitzAll() {
	m_justBest = false;
	findStaticPlays();
	return m_moves;
}

Move V2Generator::kibitz() {
	m_justBest = true;
  findStaticPlays();
	assert(!m_moves.empty());
	int index = m_tiebreakDividend++ % m_moves.size();
	return m_moves[index];
}

void V2Generator::findStaticPlays() {
  //UVcout << "findStaticPlays(): " << endl << m_position << endl;
	m_moves.clear();
	// Only do this if no exchanges exist
	//m_moves.push_back(Move::createPassMove());

  setUpCounts(rack().tiles());
	m_rackBits = 0;
	for (int letter = QUACKLE_ALPHABET_PARAMETERS->firstLetter();
			 letter <= QUACKLE_ALPHABET_PARAMETERS->lastLetter(); ++letter) {
		if (m_counts[letter] > 0) m_rackBits |= (1 << letter);
	}
		
	struct timeval start, end;

	gettimeofday(&start, NULL);
	m_product = QUACKLE_PRIMESET->multiplyTiles(rack());
  m_anagrams = QUACKLE_ANAGRAMMAP->lookUp(rack());
	gettimeofday(&end, NULL);
	// UVcout << "Time checking anagrammap was "
	// 			 << ((end.tv_sec * 1000000 + end.tv_usec)
	// 					 - (start.tv_sec * 1000000 + start.tv_usec))
	// 			 << " microseconds." << endl;
	// if (m_anagrams == NULL) {
	// 	UVcout << "could not find rack anagrams!" << endl;
	// } else {
	// 	UVcout << "found rack anagrams!! usesNoBlanks.thruNone.numPlayed: ";
	// 	UVcout << static_cast<int>(m_anagrams->usesNoBlanks.thruNone.numPlayed)
	// 				 << endl;
	// }

	if (couldHaveBingos()) {
		gettimeofday(&start, NULL);
		if (m_bingoMap != NULL &&
				m_bingoMap->count(m_product) > 0) {
      m_bingos = m_bingoMap->find(m_product)->second;
		} else if (blankOnRack()) {
			findBlankBingos();
		} else {
			findBingos();
		}
		gettimeofday(&end, NULL);
		UVcout << "Time finding bingos was "
				 << ((end.tv_sec * 1000000 + end.tv_usec)
						 - (start.tv_sec * 1000000 + start.tv_usec))
					 << " microseconds." << endl;
		/*
		for (const LetterString& bingo : m_bingos) {
			UVcout << "bingo: "
						 << QUACKLE_ALPHABET_PARAMETERS->userVisible(bingo) << endl;
		}
		*/
	}
	
	// TODO: are enough tiles in the bag?
  // TODO much later: would this end the game?
	gettimeofday(&start, NULL);
	findBestExchange();
	gettimeofday(&end, NULL);
	// UVcout << "Time finding exchanges was "
	// 			 << ((end.tv_sec * 1000000 + end.tv_usec)
	// 					 - (start.tv_sec * 1000000 + start.tv_usec))
	// 			 << " microseconds." << endl;
	if (m_justBest) {
		//UVcout << "best exchange: " << m_moves[0] << endl;
	}
	
	gettimeofday(&start, NULL);
	computeHooks();
	gettimeofday(&end, NULL);
	// UVcout << "Time computing hooks was "
	// 				 << ((end.tv_sec * 1000000 + end.tv_usec)
	// 					 - (start.tv_sec * 1000000 + start.tv_usec)) << " microseconds." << endl;
			
	//debugHooks();

	vector<Spot> spots;
	if (board()->isEmpty()) {
		gettimeofday(&start, NULL);
		findEmptyBoardSpots(&spots);
		gettimeofday(&end, NULL);
		// UVcout << "Time finding spots on empty board was "
		// 			 << ((end.tv_sec * 1000000 + end.tv_usec)
		// 				 - (start.tv_sec * 1000000 + start.tv_usec)) << " microseconds." << endl;
	} else {
		gettimeofday(&start, NULL);
		findSpots(&spots);
		gettimeofday(&end, NULL);
		// UVcout << "Time finding spots on nonempty board was "
		// 			 << ((end.tv_sec * 1000000 + end.tv_usec)
		// 				 - (start.tv_sec * 1000000 + start.tv_usec)) << " microseconds." << endl;
	}
	gettimeofday(&start, NULL);
	std::sort(spots.begin(), spots.end());
	gettimeofday(&end, NULL);
	// UVcout << "Time sorting spots was "
	// 			 << ((end.tv_sec * 1000000 + end.tv_usec)
	// 					 - (start.tv_sec * 1000000 + start.tv_usec)) << " microseconds." << endl;

	for (Spot& spot : spots) {
#ifdef DEBUG_V2GEN
		UVcout << "Spot: (" << spot.anchorRow << ", " << spot.anchorCol
					 << ", " << "blank: " << spot.useBlank
					 << ", " << "dir: " << (spot.horizontal ? "horiz" : "vert")
					 << ", thru: " << spot.numTilesThrough
					 << ", played: " << spot.minPlayed << " to " << spot.maxPlayed << "): "
					 << spot.maxEquity << endl;
#endif
		restrictSpot(&spot);

		if (m_justBest && !bestEnough(spot.maxEquity)) {
			//#ifdef DEBUG_V2GEN
			//UVcout << "no need to check this spot!" << endl;
			//#endif
			continue;
		}
		findMovesAt(&spot);
	}
	/*
  int i = 0;
	for (const Move& move : m_moves) {
		i++;
		UVcout << "best Move " << i << " of " << m_moves.size()
					 << ": " << move << endl;
	}
	*/
}

void V2Generator::restrictSpot(Spot* spot) {
	if (!m_justBest) return;
	restrictByLength(spot);
	restrictByBehind(spot);
}

void V2Generator::restrictByLength(Spot* spot) {
	int oldLongestViable = spot->longestViable;
	for (int i = 1; i <= oldLongestViable; ++i) {
		if (spot->worthChecking[i].couldBeBest &&
				bestEnough(spot->worthChecking[i].maxEquity)) {
			spot->longestViable = i;
		} else {
			spot->worthChecking[i].couldBeBest = false;
		}
	}
#ifdef DEBUG_V2GEN
	for (int i = 1; i <= 7; ++i) {
		if (spot->worthChecking[i].couldBeBest) {
			UVcout << ", [" << i << "]: " << spot->worthChecking[i].maxEquity;
		} else {
			UVcout << ", [" << i << "]: nope";
		}
	}
#endif
}

void V2Generator::restrictByBehind(Spot* spot) {
	int oldHindmostViable = spot->hindmostViable;
	for (int i = 0; i <= oldHindmostViable; ++i) {
		if (spot->worthCheckingBehind[i].couldBeBest &&
				bestEnough(spot->worthCheckingBehind[i].maxEquity)) {
			spot->hindmostViable = i;
		} else {
			spot->worthCheckingBehind[i].couldBeBest = false;
		}
	}
#ifdef DEBUG_V2GEN
	for (int i = 0; i <= 7; ++i) {
		if (spot->worthCheckingBehind[i].couldBeBest) {
			UVcout << ", [b" << i << "]: "
						 << spot->worthCheckingBehind[i].maxEquity;
		} else {
			UVcout << ", [b" << i << "]: nope";
		}
	}
	UVcout << endl;
#endif	
}

bool V2Generator::duplicatePreceding(const Letter* letters, int i) const {
	//UVcout << "is a duplicate preceding "
	//			 << QUACKLE_ALPHABET_PARAMETERS->userVisible(letters[i])
	//			 << " at letters[" << i << "]?" << endl;
	for (int j = i - 1; j >= 0; j--) {
		if (letters[j] == letters[i]) {
			//UVcout << "yes, at " << j << endl;
			return true;
		}
		if (letters[j] == QUACKLE_NULL_MARK) continue;
		if (letters[j] < letters[i]) {
			//UVcout << "no, letters[" << j << "]: "
			//			 << QUACKLE_ALPHABET_PARAMETERS->userVisible(letters[j])
			//			 << endl;
			return false;
		}
	}
	return false;
}

void V2Generator::findBlankBingos() {
		m_bingos.clear();
	const V2Gaddag& gaddag = *(QUACKLE_LEXICON_PARAMETERS->v2Gaddag_7to7());
	LetterString letterString = rack().alphaTiles();
	//UVcout << "letterString: " << QUACKLE_ALPHABET_PARAMETERS->userVisible(letterString) << endl;
	Letter letters[7];
	for (int i = 0; i < 7; i++) letters[i] = letterString[i];
	LetterString answer;
	const unsigned char* root = gaddag.root();
	for (int i0 = 0; i0 < 7; ++i0) {
		if (duplicatePreceding(letters, i0)) continue;
		const unsigned char* child;
		Letter minLetter0 = QUACKLE_GADDAG_SEPARATOR;
		Letter letter0;
		Letter blankLetter0;
		int childIndex0 = 0;
		if (letters[i0] == QUACKLE_BLANK_MARK) {
		next_blank_letter0:
			if (!nextLetter(gaddag, root, EVERY_LETTER, minLetter0,
											&childIndex0, &blankLetter0, &child)) {
				continue;
			}
			letter0 = QUACKLE_ALPHABET_PARAMETERS->setBlankness(blankLetter0);
		} else {
			letter0 = letters[i0];
			child = gaddag.child(root, letter0);
		}
    const unsigned char* node0 = gaddag.followIndex(child);
		child = gaddag.changeDirection(node0);
		node0 = gaddag.followIndex(child);
		answer.push_back(letter0);
		letters[i0] = QUACKLE_NULL_MARK;
		// for (int i = 0; i < 7; ++i) UVcout << QUACKLE_ALPHABET_PARAMETERS->userVisible(letters[i]);
		// UVcout << endl << "answer: " << QUACKLE_ALPHABET_PARAMETERS->userVisible(answer) << endl;
		for (int i1 = 0; i1 < 7; ++i1) {
			if (letters[i1] == QUACKLE_NULL_MARK) continue;
			if (duplicatePreceding(letters, i1)) continue;
		  Letter minLetter1 = QUACKLE_GADDAG_SEPARATOR;
			Letter letter1;
			Letter blankLetter1;
			int childIndex1 = 0;
			if (letters[i1] == QUACKLE_BLANK_MARK) {
			next_blank_letter1:
				if (!nextLetter(gaddag, node0, EVERY_LETTER, minLetter1,
												&childIndex1, &blankLetter1, &child)) {
					continue;
				}
				letter1 = QUACKLE_ALPHABET_PARAMETERS->setBlankness(blankLetter1);
			} else {
				letter1 = letters[i1];
				if (!gaddag.hasChild(node0, letter1)) continue;
				child = gaddag.child(node0, letter1);
			}
			const unsigned char* node1 = gaddag.followIndex(child);
			if (node1 == NULL) continue;
			answer.push_back(letter1);
			letters[i1] = QUACKLE_NULL_MARK;
			// for (int i = 0; i < 7; ++i) UVcout << QUACKLE_ALPHABET_PARAMETERS->userVisible(letters[i]);
			// UVcout << endl << "answer: " << QUACKLE_ALPHABET_PARAMETERS->userVisible(answer) << endl;
			for (int i2 = 0; i2 < 7; ++i2) {
				if (letters[i2] == QUACKLE_NULL_MARK) continue;
				if (duplicatePreceding(letters, i2)) continue;
				Letter minLetter2 = QUACKLE_GADDAG_SEPARATOR;
				Letter letter2;
				Letter blankLetter2;
				int childIndex2 = 0;
				if (letters[i2] == QUACKLE_BLANK_MARK) {
				next_blank_letter2:
					if (!nextLetter(gaddag, node1, EVERY_LETTER, minLetter2,
													&childIndex2, &blankLetter2, &child)) {
						continue;
					}
					letter2 = QUACKLE_ALPHABET_PARAMETERS->setBlankness(blankLetter2);
				} else {
					letter2 = letters[i2];
					if (!gaddag.hasChild(node1, letter2)) continue;
					child = gaddag.child(node1, letter2);
				}
				const unsigned char* node2 = gaddag.followIndex(child);
				if (node2 == NULL) continue;
				answer.push_back(letter2);
				letters[i2] = QUACKLE_NULL_MARK;
				// for (int i = 0; i < 7; ++i) UVcout << QUACKLE_ALPHABET_PARAMETERS->userVisible(letters[i]);
				// UVcout << endl << "answer: " << QUACKLE_ALPHABET_PARAMETERS->userVisible(answer) << endl;
				for (int i3 = 0; i3 < 7; ++i3) {
					if (letters[i3] == QUACKLE_NULL_MARK) continue;
					if (duplicatePreceding(letters, i3)) continue;
					Letter minLetter3 = QUACKLE_GADDAG_SEPARATOR;
					Letter letter3;
					Letter blankLetter3;
					int childIndex3 = 0;
					if (letters[i3] == QUACKLE_BLANK_MARK) {
					next_blank_letter3:
						if (!nextLetter(gaddag, node2, EVERY_LETTER, minLetter3,
														&childIndex3, &blankLetter3, &child)) {
							continue;
						}
						letter3 = QUACKLE_ALPHABET_PARAMETERS->setBlankness(blankLetter3);
					} else {
						letter3 = letters[i3];
						if (!gaddag.hasChild(node2, letter3)) continue;
						child = gaddag.child(node2, letter3);
					}
					const unsigned char* node3 = gaddag.followIndex(child);
					if (node3 == NULL) continue;
					answer.push_back(letter3);
					letters[i3] = QUACKLE_NULL_MARK;
					// for (int i = 0; i < 7; ++i) UVcout << QUACKLE_ALPHABET_PARAMETERS->userVisible(letters[i]);
					// UVcout << endl << "answer: " << QUACKLE_ALPHABET_PARAMETERS->userVisible(answer) << endl;
					for (int i4 = 0; i4 < 7; ++i4) {
						if (letters[i4] == QUACKLE_NULL_MARK) continue;
						if (duplicatePreceding(letters, i4)) continue;
						Letter minLetter4 = QUACKLE_GADDAG_SEPARATOR;
						Letter letter4;
						Letter blankLetter4;
						int childIndex4 = 0;
						if (letters[i4] == QUACKLE_BLANK_MARK) {
						next_blank_letter4:
							if (!nextLetter(gaddag, node3, EVERY_LETTER, minLetter4,
															&childIndex4, &blankLetter4, &child)) {
								continue;
							}
							letter4 = QUACKLE_ALPHABET_PARAMETERS->setBlankness(blankLetter4);
						} else {
							letter4 = letters[i4];
							if (!gaddag.hasChild(node3, letter4)) continue;
							child = gaddag.child(node3, letter4);
						}
						const unsigned char* node4 = gaddag.followIndex(child);
						if (node4 == NULL) continue;
						answer.push_back(letter4);
						letters[i4] = QUACKLE_NULL_MARK;
						// for (int i = 0; i < 7; ++i) UVcout << QUACKLE_ALPHABET_PARAMETERS->userVisible(letters[i]);
						// UVcout << endl << "answer: " << QUACKLE_ALPHABET_PARAMETERS->userVisible(answer) << endl;
						for (int i5 = 0; i5 < 7; ++i5) {
							if (letters[i5] == QUACKLE_NULL_MARK) continue;
							if (duplicatePreceding(letters, i5)) continue;
							Letter minLetter5 = QUACKLE_GADDAG_SEPARATOR;
							Letter letter5;
							Letter blankLetter5;
							int childIndex5 = 0;
							if (letters[i5] == QUACKLE_BLANK_MARK) {
							next_blank_letter5:
								if (!nextLetter(gaddag, node4, EVERY_LETTER, minLetter5,
																&childIndex5, &blankLetter5, &child)) {
									continue;
								}
								letter5 = QUACKLE_ALPHABET_PARAMETERS->setBlankness(blankLetter5);
							} else {
								letter5 = letters[i5];
								if (!gaddag.hasChild(node4, letter5)) continue;
								child = gaddag.child(node4, letter5);
							}
							const unsigned char* node5 = gaddag.followIndex(child);
							if (node5 == NULL) continue;
							answer.push_back(letter5);
							letters[i5] = QUACKLE_NULL_MARK;
							// for (int i = 0; i < 7; ++i) UVcout << QUACKLE_ALPHABET_PARAMETERS->userVisible(letters[i]);
							// UVcout << endl << "answer: " << QUACKLE_ALPHABET_PARAMETERS->userVisible(answer) << endl;
							for (int i6 = 0; i6 < 7; ++i6) {
								if (letters[i6] == QUACKLE_NULL_MARK) continue;
								if (duplicatePreceding(letters, i6)) continue;
								Letter minLetter6 = QUACKLE_GADDAG_SEPARATOR;
								Letter letter6;
								Letter blankLetter6;
								int childIndex6 = 0;
								if (letters[i6] == QUACKLE_BLANK_MARK) {
								next_blank_letter6:
									if (!nextLetter(gaddag, node5, EVERY_LETTER, minLetter6,
																	&childIndex6, &blankLetter6, &child)) {
										continue;
									}
									letter6 = QUACKLE_ALPHABET_PARAMETERS->setBlankness(blankLetter6);
								} else {
									letter6 = letters[i6];
									if (!gaddag.hasChild(node5, letter6)) continue;
									//child = gaddag.child(node5, letter6);
								}
								//if (gaddag.completesWord(child)) {
									answer.push_back(letter6);
									// for (int i = 0; i < 7; ++i) UVcout << QUACKLE_ALPHABET_PARAMETERS->userVisible(letters[i]);
									// UVcout << endl << "answer: " << QUACKLE_ALPHABET_PARAMETERS->userVisible(answer) << endl;
									m_bingos.push_back(answer);
									answer.pop_back();
									//}
								if (QUACKLE_ALPHABET_PARAMETERS->isBlankLetter(letter6)) {
									minLetter6 = blankLetter6 + 1;
									++childIndex6;
									goto next_blank_letter6;
								}
							}
							answer.pop_back();
							if (QUACKLE_ALPHABET_PARAMETERS->isBlankLetter(letter5)) {
								minLetter5 = blankLetter5 + 1;
								++childIndex5;
								letters[i5] = QUACKLE_BLANK_MARK;
								goto next_blank_letter5;
							} else {
								letters[i5] = answer[5];
							}
						}
						answer.pop_back();
						if (QUACKLE_ALPHABET_PARAMETERS->isBlankLetter(letter4)) {
							minLetter4 = blankLetter4 + 1;
							++childIndex4;
							letters[i4] = QUACKLE_BLANK_MARK;
							goto next_blank_letter4;
						} else {
							letters[i4] = answer[4];
						}
					}
					answer.pop_back();
					if (QUACKLE_ALPHABET_PARAMETERS->isBlankLetter(letter3)) {
						minLetter3 = blankLetter3 + 1;
						++childIndex3;
						letters[i3] = QUACKLE_BLANK_MARK;
						goto next_blank_letter3;
					} else {
						letters[i3] = answer[3];
					}
				}
				answer.pop_back();
				if (QUACKLE_ALPHABET_PARAMETERS->isBlankLetter(letter2)) {
					minLetter2 = blankLetter2 + 1;
					++childIndex2;
					letters[i2] = QUACKLE_BLANK_MARK;
					goto next_blank_letter2;
				} else {
					letters[i2] = answer[2];
				}
			}
			answer.pop_back();
			if (QUACKLE_ALPHABET_PARAMETERS->isBlankLetter(letter1)) {
				minLetter1 = blankLetter1 + 1;
				++childIndex1;
				letters[i1] = QUACKLE_BLANK_MARK;
				goto next_blank_letter1;
			} else {
				letters[i1] = answer[1];
			}
		}
		answer.pop_back();
		if (QUACKLE_ALPHABET_PARAMETERS->isBlankLetter(letter0)) {
			minLetter0 = blankLetter0 + 1;
			++childIndex0;
			letters[i0] = QUACKLE_BLANK_MARK;
			goto next_blank_letter0;
		} else {
			letters[i0] = answer[0];
		}
	}
}

void V2Generator::findBingos() {
	m_bingos.clear();
	const V2Gaddag& gaddag = *(QUACKLE_LEXICON_PARAMETERS->v2Gaddag_7to7());
	LetterString letterString = rack().alphaTiles();
	//UVcout << "letterString: " << QUACKLE_ALPHABET_PARAMETERS->userVisible(letterString) << endl;
	Letter letters[7];
	for (int i = 0; i < 7; i++) letters[i] = letterString[i];
	LetterString answer;
	const unsigned char* root = gaddag.root();
	for (int i0 = 0; i0 < 7; ++i0) {
		if (duplicatePreceding(letters, i0)) continue;
		const unsigned char* child = gaddag.child(root, letters[i0]);
    const unsigned char* node0 = gaddag.followIndex(child);
		child = gaddag.changeDirection(node0);
		node0 = gaddag.followIndex(child);
		answer.push_back(letters[i0]);
		letters[i0] = QUACKLE_NULL_MARK;
		//for (int i = 0; i < 7; ++i) UVcout << QUACKLE_ALPHABET_PARAMETERS->userVisible(letters[i]);
		//UVcout << endl << "answer: " << QUACKLE_ALPHABET_PARAMETERS->userVisible(answer) << endl;
		for (int i1 = 0; i1 < 7; ++i1) {
			if (letters[i1] == QUACKLE_NULL_MARK) continue;
			if (duplicatePreceding(letters, i1)) continue;
			if (!gaddag.hasChild(node0, letters[i1])) continue;
			child = gaddag.child(node0, letters[i1]);
			const unsigned char* node1 = gaddag.followIndex(child);
			if (node1 == NULL) continue;
			answer.push_back(letters[i1]);
			letters[i1] = QUACKLE_NULL_MARK;
			//for (int i = 0; i < 7; ++i) UVcout << QUACKLE_ALPHABET_PARAMETERS->userVisible(letters[i]);
			//UVcout << endl << "answer: " << QUACKLE_ALPHABET_PARAMETERS->userVisible(answer) << endl;
			for (int i2 = 0; i2 < 7; ++i2) {
				if (letters[i2] == QUACKLE_NULL_MARK) continue;
				if (duplicatePreceding(letters, i2)) continue;
				if (!gaddag.hasChild(node1, letters[i2])) continue;
				child = gaddag.child(node1, letters[i2]);
				const unsigned char* node2 = gaddag.followIndex(child);
				if (node2 == NULL) continue;
				answer.push_back(letters[i2]);
				letters[i2] = QUACKLE_NULL_MARK;
				//for (int i = 0; i < 7; ++i) UVcout << QUACKLE_ALPHABET_PARAMETERS->userVisible(letters[i]);
				//UVcout << endl << "answer: " << QUACKLE_ALPHABET_PARAMETERS->userVisible(answer) << endl;
				for (int i3 = 0; i3 < 7; ++i3) {
					if (letters[i3] == QUACKLE_NULL_MARK) continue;
					if (duplicatePreceding(letters, i3)) continue;
					if (!gaddag.hasChild(node2, letters[i3])) continue;
					child = gaddag.child(node2, letters[i3]);
					const unsigned char* node3 = gaddag.followIndex(child);
					if (node3 == NULL) continue;
					answer.push_back(letters[i3]);
					letters[i3] = QUACKLE_NULL_MARK;
					//for (int i = 0; i < 7; ++i) UVcout << QUACKLE_ALPHABET_PARAMETERS->userVisible(letters[i]);
					//UVcout << endl << "answer: " << QUACKLE_ALPHABET_PARAMETERS->userVisible(answer) << endl;
					for (int i4 = 0; i4 < 7; ++i4) {
						if (letters[i4] == QUACKLE_NULL_MARK) continue;
						if (duplicatePreceding(letters, i4)) continue;
						if (!gaddag.hasChild(node3, letters[i4])) continue;
						child = gaddag.child(node3, letters[i4]);
						const unsigned char* node4 = gaddag.followIndex(child);
						if (node4 == NULL) continue;
						answer.push_back(letters[i4]);
						letters[i4] = QUACKLE_NULL_MARK;
						//for (int i = 0; i < 7; ++i) UVcout << QUACKLE_ALPHABET_PARAMETERS->userVisible(letters[i]);
						//UVcout << endl << "answer: " << QUACKLE_ALPHABET_PARAMETERS->userVisible(answer) << endl;
						for (int i5 = 0; i5 < 7; ++i5) {
							if (letters[i5] == QUACKLE_NULL_MARK) continue;
							if (duplicatePreceding(letters, i5)) continue;
							if (!gaddag.hasChild(node4, letters[i5])) continue;
							child = gaddag.child(node4, letters[i5]);
							const unsigned char* node5 = gaddag.followIndex(child);
							if (node5 == NULL) continue;
							answer.push_back(letters[i5]);
							letters[i5] = QUACKLE_NULL_MARK;
							//for (int i = 0; i < 7; ++i) UVcout << QUACKLE_ALPHABET_PARAMETERS->userVisible(letters[i]);
							//UVcout << endl << "answer: " << QUACKLE_ALPHABET_PARAMETERS->userVisible(answer) << endl;
							for (int i6 = 0; i6 < 7; ++i6) {
								if (letters[i6] == QUACKLE_NULL_MARK) continue;
								if (duplicatePreceding(letters, i6)) continue;
								if (!gaddag.hasChild(node5, letters[i6])) continue;
								//child = gaddag.child(node5, letters[i6]);
								//if (gaddag.completesWord(child)) {
									answer.push_back(letters[i6]);
									//for (int i = 0; i < 7; ++i) UVcout << QUACKLE_ALPHABET_PARAMETERS->userVisible(letters[i]);
									//UVcout << endl << "answer: " << QUACKLE_ALPHABET_PARAMETERS->userVisible(answer) << endl;
									m_bingos.push_back(answer);
									answer.pop_back();
									//}
							}
							letters[i5] = answer[5];
							answer.pop_back();
						}
						letters[i4] = answer[4];
						answer.pop_back();
					}
					letters[i3] = answer[3];
					answer.pop_back();
				}
				letters[i2] = answer[2];
				answer.pop_back();
			}
			letters[i1] = answer[1];
			answer.pop_back();
		}
		letters[i0] = answer[0];
		answer.pop_back();
	}
}

void V2Generator::addSubsets(const LetterString& rack,
															set<Product>* subsets) {
	vector<Prime> primes;
	primes.reserve(rack.size());
	for (unsigned int i = 0; i < rack.size(); ++i) {
		primes.push_back(QUACKLE_PRIMESET->lookUpTile(rack[i]));
	}
	for (unsigned int i = 0; i < rack.size(); ++i) {
		Product subsetProduct = 1;
		for (unsigned int j = 0; j < rack.size(); ++j) {
			if (i == j) continue;
			subsetProduct *= primes[j];
		}
		if (subsets->count(subsetProduct) == 0) {
			subsets->insert(subsetProduct);
			LetterString subset;
			for (unsigned int j = 0; j < rack.size(); ++j) {
				if (i == j) continue;
				subset.push_back(rack[j]);
			}
			if (subset.size() > 1) addSubsets(subset, subsets);			
		}
	}
}

void V2Generator::findBingos(const set<Product>& subsets,
														 const V2Gaddag& gaddag,
														 const unsigned char* node,
														 LetterString* prefix,
														 Product product,
														 map<Product, vector<LetterString>>* bingos) {
	Letter minLetter = QUACKLE_GADDAG_SEPARATOR;
	Letter letter;
	int childIndex = 0;
	const unsigned char* child;
	const Product blankProduct =
		product * QUACKLE_PRIMESET->lookUpTile(QUACKLE_BLANK_MARK);
	while (nextLetter(gaddag, node, EVERY_LETTER, minLetter,
										&childIndex, &letter, &child)) {
		const Product newProduct =
			product * QUACKLE_PRIMESET->lookUpTile(letter);
		if ((subsets.count(newProduct) == 1) ||
        (subsets.count(blankProduct) == 1)) {
			const unsigned char* newNode = gaddag.followIndex(child);
			if (prefix->size() == 0) {
				child = gaddag.changeDirection(newNode);
				newNode = gaddag.followIndex(child);
			}
			if (subsets.count(newProduct) == 1) {
				prefix->push_back(letter);
				if (prefix->size() == 7) {
					(*bingos)[newProduct].push_back(*prefix);
				} else {
					findBingos(subsets, gaddag, newNode, prefix, newProduct, bingos);
				}
				prefix->pop_back();
			}
			if (subsets.count(blankProduct) == 1) {
				const Letter blankLetter = QUACKLE_ALPHABET_PARAMETERS->setBlankness(letter);
				prefix->push_back(blankLetter);
				if (prefix->size() == 7) {
					(*bingos)[newProduct].push_back(*prefix);
				} else {
					findBingos(subsets, gaddag, newNode, prefix, blankProduct, bingos);
				}
				prefix->pop_back();
			}
		}
		minLetter = letter + 1;
		++childIndex;
	}
}

void V2Generator::findBingos(const set<Product>& subsets,
														 map<Product, vector<LetterString>>* bingos) {
	const V2Gaddag& gaddag = *(QUACKLE_LEXICON_PARAMETERS->v2Gaddag_7to7());
  const unsigned char* root = gaddag.root();
	LetterString prefix;
	findBingos(subsets, gaddag, root, &prefix, 1, bingos);
}

void V2Generator::findBingos(const set<LetterString>& racks,
														 map<Product, vector<LetterString>>* bingos) {
	set<Product> subsets;
	for (const LetterString& rack : racks) {
		assert(rack.size() == 7);
		subsets.insert(QUACKLE_PRIMESET->multiplyTiles(rack));
		addSubsets(rack, &subsets);
	}
	UVcout << "Found " << subsets.size() << " subsets." << endl;
	findBingos(subsets, bingos);
}

void V2Generator::findBestExchange() {
	assert(rack().size() == 7);
	for (int i = 0; i <= 7; ++i) {
		m_bestLeaves[i] = -9999;
	}
	double bestEquity = 0;
	uint64_t primes[7];
	int bestMask = 0;
	const LetterString& tiles = rack().tiles();
  if (!m_justBest) {
		Move exchange = Move::createExchangeMove(tiles);
	  exchange.equity = bestEquity;
		m_moves.push_back(exchange);
	}
	set<LetterString> exchanges;
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
		if (!m_justBest) {
			LetterString exchanged;
			for (int i = 0; i < 7; ++i) {
				if (((1 << i) & mask) == 0) {
					exchanged += tiles[i];
				}
			}
			if (exchanges.count(exchanged) > 0) continue;
			exchanges.insert(exchanged);
			Move exchange = Move::createExchangeMove(exchanged);
			exchange.equity = bestEquity;
			m_moves.push_back(exchange);
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
	// Some exchanges might be tied, but I don't have a problem with breaking
	// the tie arbitrarily.
	//
	// The exchange is definitely best: right now m_moves contains Pass
	if (m_justBest) {	
		Move exchange = Move::createExchangeMove(exchanged);
		exchange.equity = bestEquity;
		m_moves.push_back(exchange);
	}
}

bool V2Generator::couldMakeWord(const Spot& spot, int length) {
	//assert(length > 0);
	//assert(length <= 7);
  if (m_anagrams == NULL) return true;

	if (spot.numTilesThrough > 1) return true;

	const int lengthMask = 1 << length;
	const UsesTiles& usesTiles =
		spot.useBlank ? m_anagrams->mustUseBlank : m_anagrams->usesNoBlanks;
	if (spot.numTilesThrough == 1) {
		Letter thruLetter =
			QUACKLE_ALPHABET_PARAMETERS
			->clearBlankness(boardLetter(spot.anchorRow,
																	 spot.anchorCol));
		uint32_t letterMask = 1 << thruLetter;
		if ((usesTiles.anahooks & letterMask) == 0) return false;
		uint32_t beforeLetterMask = (1 << thruLetter) - 1;
		int index = __builtin_popcount(usesTiles.anahooks &
																	 beforeLetterMask);
		const NTileAnagrams& anagrams = usesTiles.thruOne[index];
		return (anagrams.numPlayed & lengthMask) != 0;
	}
	const NTileAnagrams& anagrams = usesTiles.thruNone;
	return (anagrams.numPlayed & lengthMask) != 0;
}

const V2Gaddag& V2Generator::spotGaddag(Spot* spot) const {
  if (spot->numTilesThrough == 0) {
		if (spot->minPlayed == 7) {
			const V2Gaddag* gaddag = QUACKLE_LEXICON_PARAMETERS->v2Gaddag_7to7();
			//UVcout << "using 7to7!" << endl;
			spot->anchorNode = gaddag->root();
			return *gaddag;
		} else {
			assert (spot->maxPlayed < 7);
			const V2Gaddag* gaddag = QUACKLE_LEXICON_PARAMETERS->v2Gaddag_2to6();
			//UVcout << "using 2to6!" << endl;
			spot->anchorNode = gaddag->root();
			return *gaddag;
		}
	} else {
		return *(QUACKLE_LEXICON_PARAMETERS->v2Gaddag());
	}
}

void V2Generator::findMovesAt(Spot* spot) {
	m_mainWordScore = spot->throughScore;
	m_hookScore = 0;
	//if (m_counts[QUACKLE_BLANK_MARK] == 0) return;
	//UVcout << "spot->useBlank: " << spot->useBlank
	//			 << ", m_counts[QUACKLE_BLANK_MARK]: "
	//			 << static_cast<int>(m_counts[QUACKLE_BLANK_MARK]) << endl;
	const int ahead = (spot->numTilesThrough == 0) ? 1 : 0;
	struct timeval start, end;
	gettimeofday(&start, NULL);
	if (spot->minPlayed == 7 && spot->numTilesThrough == 0) {
		fitBingos(spot);
	} else {
		const V2Gaddag& gaddag = spotGaddag(spot);
		if (spot->useBlank) {
			findBlankRequired(spot, 0, ahead, 0, -1, 1, gaddag, spot->anchorNode);
		} else {
			findBlankless(spot, 0, ahead, 0, -1, 1, gaddag, spot->anchorNode);
		}
	}
	gettimeofday(&end, NULL);
	/*
	UVcout << "Time finding moves for " << rack()
				 << " blank: " << spot->useBlank
				 << ", thru: " << spot->numTilesThrough
				 << ", played: " << spot->minPlayed << " to " << spot->maxPlayed << ": "
				 << ((end.tv_sec * 1000000 + end.tv_usec)
						 - (start.tv_sec * 1000000 + start.tv_usec))
				 << " microseconds." << endl;
	*/
}

namespace {
	UVString debugLetterMask(uint32_t letters) {
		UVString ret;
		for (Letter i = QUACKLE_ALPHABET_PARAMETERS->firstLetter();
				 i <= QUACKLE_ALPHABET_PARAMETERS->lastLetter(); ++i) {
			if ((letters & (1 << i)) != 0) {
				ret += QUACKLE_ALPHABET_PARAMETERS->userVisible(i);
			}
		}
		return ret;
	}
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
														 const unsigned char** child) {
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

// updates m_mainWordScore, returns tile score so it can be subtracted later
int V2Generator::scoreLetter(int pos, Letter letter, int letterMultiplier) {
	//#ifdef DEBUG_V2GEN
	assert(letter >= QUACKLE_ALPHABET_PARAMETERS->firstLetter());
	assert(letter <= QUACKLE_ALPHABET_PARAMETERS->lastLetter());
	//UVcout << "placing " << QUACKLE_ALPHABET_PARAMETERS->userVisible(letter)
	//			 << " at m_placed[" << pos << "]" << endl;
	//#endif
	m_placed[pos] = letter;
	const int letterScore = QUACKLE_ALPHABET_PARAMETERS->score(letter);
	const int tileMainScore = letterScore * letterMultiplier;
	m_mainWordScore += tileMainScore;
	//UVcout << "incrementing m_mainWordScore by " << tileMainScore
	//			 << ", now it's " << m_mainWordScore << endl;
	return tileMainScore;	
}

void V2Generator::debugPlaced(const Spot& spot, int behind, int ahead) const {
	UVcout << "m_rackBits: " << debugLetterMask(m_rackBits) << endl;
	UVcout << "counts: " << counts2string() << endl;
	const int anchorPos = spot.horizontal ? spot.anchorCol : spot.anchorRow;
	const int startPos = anchorPos - behind;
	UVcout << "startPos: " << startPos << endl;
	LetterString word;
	int lastPos = anchorPos + ahead;
	if (spot.numTilesThrough == 0) lastPos--;
	UVcout << "behind: " << behind << ", ahead: " << ahead << endl;
	for (int i = startPos; i <= lastPos; ++i) {
		assert(i >= 0); assert(i < 15);
		if (i == anchorPos && spot.numTilesThrough != 0) {
			word += QUACKLE_PLAYED_THRU_MARK;
		}	else {
			word += m_placed[i];
		}
	}
	UVcout << "m_placed[" << startPos << " to " << lastPos
				 << "]: " << QUACKLE_ALPHABET_PARAMETERS->userVisible(word) << endl;
}

void V2Generator::useLetter(Letter letter, uint32_t* foundLetterMask) {
	assert(m_counts[letter] > 0);
	m_counts[letter]--;
	assert(m_counts[letter] >= 0);
	*foundLetterMask = 1 << letter;
	if (m_counts[letter] == 0) m_rackBits &= ~(*foundLetterMask);
}

void V2Generator::unuseLetter(Letter letter, uint32_t foundLetterMask) {
	m_counts[letter]++;
	m_rackBits |= foundLetterMask;	
}

bool V2Generator::bestEnough(double equity) const {
	assert(m_justBest);
	assert(!m_moves.empty());
	return m_moves[0].equity <= equity + m_bestEquityEpsilon;
}

bool V2Generator::clearlyBetter(double equity) const {
	assert(m_justBest);
	assert(!m_moves.empty());
	return m_moves[0].equity + m_bestEquityEpsilon <= equity;
}

bool V2Generator::maybeRecordMove(const Spot& spot, int wordMultiplier,
																	int behind, int numPlaced) {
	// UVcout << "m_mainWordScore: " << m_mainWordScore
	// 			 << ", wordMultiplier: " <<  wordMultiplier
	//  			 << ", m_hookScore: " << m_hookScore << endl;
	int score = m_mainWordScore * wordMultiplier + m_hookScore;
	if (numPlaced == QUACKLE_PARAMETERS->rackSize()) {
		score += QUACKLE_PARAMETERS->bingoBonus();
	}
	//assert(score == scorePlay(spot, behind, ahead));
	double equity = score;
	//UVcout << "score: " << score << endl;
	//UVcout << "numPlaced: " << numPlaced << endl;
	if (numPlaced < 7) equity += getLeave();
	//UVcout << "equity: " << equity << endl;
	assert(equity <= spot.maxEquity + 2 * m_blankSpendingEpsilon);
	assert(spot.worthChecking[numPlaced].couldBeBest);
	//assert(equity <= spot.worthChecking[numPlaced].maxEquity);
	if (!m_justBest || bestEnough(equity)) {
		LetterString word;
		int startRow = spot.anchorRow;
		int startCol = spot.anchorCol;
		int startPos;
		int anchorPos;
		if (spot.horizontal) {
			startCol -= behind;
			startPos = startCol;
			anchorPos = spot.anchorCol;
		} else {
			startRow -= behind;
			startPos = startRow;
			anchorPos = spot.anchorRow;
		}
		const unsigned int lettersInWord = numPlaced + spot.numTilesThrough;
		//UVcout << "lettersInWord: " << lettersInWord << endl;
		int row = startRow;
		int col = startCol;
		int pos = anchorPos - behind;
		for (unsigned int i = 0; i < lettersInWord; ++i) {
			/*
			UVcout << "i: " << i << ", pos: " << pos
						 << ", row: " << row << ", col: " << col << endl;
			*/
			assert(pos >= 0); assert(pos < 15); assert(col < 15); assert(row < 15);
			if (pos == anchorPos && spot.numTilesThrough > 0) pos++;
			if (isEmpty(row, col)) {
				//UVcout << "m_placed[pos]: " << static_cast<int>(m_placed[pos]) << endl;
				word += m_placed[pos];
				pos++;
			} else {
				word += QUACKLE_PLAYED_THRU_MARK;
			}
			if (spot.horizontal) col++; else row++;
		}
		assert(word.length() == lettersInWord);
		Move move = Move::createPlaceMove(startRow, startCol, spot.horizontal, word);
		move.score = score;
		move.equity = equity;
		bool wasClearlyBetter = false;
		if (m_justBest && clearlyBetter(equity)) {
			m_moves.clear();
			wasClearlyBetter = true;
		}
		m_moves.push_back(move);

		//UVcout << "spot.worthChecking[" << numPlaced << "].maxEquity: "
		//			 << spot.worthChecking[numPlaced].maxEquity << endl;
		if (equity > spot.worthChecking[numPlaced].maxEquity) {
			UVcout << "equity > maxEquity, WTF?" << endl;
		}
		//UVcout << "new best: " << move << endl;
		
		return wasClearlyBetter;
	}
	return false;
}

void V2Generator::getSquare(const Spot& spot, int delta,
														int* row, int* col, int* pos) {
	// UVcout << "getSquare(" << spot.anchorRow << ", " << spot.anchorCol
	// 			 << ", " << ((spot.horizontal) ? "horiz" : "vert")
	// 			 << ", delta: " << delta << ")..." << endl;
	if (spot.horizontal) {
		*row = spot.anchorRow;
		*col = spot.anchorCol + delta;
		*pos = *col;
		if (spot.numTilesThrough > 0) {
			*col = spot.realPositions[*col];
		}
	} else {
		*row = spot.anchorRow + delta;
		*pos = *row;
		if (spot.numTilesThrough > 0) {
			*row = spot.realPositions[*row];
		}
		*col = spot.anchorCol;
	}
#ifdef DEBUG_V2GEN
  UVcout << "row: " << *row << ", col: " << *col << ", pos: " << *pos << endl;
	assert(*col >= 0); assert(*col < 15); assert(*row >= 0); assert(*row < 15);
#endif
}

bool V2Generator::fits(const Spot& spot, const LetterString& word,
											 int ahead, int behind, int* wordHookScore) {
	m_mainWordScore = 0;
	int row = spot.anchorRow;
	int col = spot.anchorCol;
	for (int wordPos = behind; wordPos < behind + ahead; ++wordPos) {
		const Hook& hook =
			spot.horizontal ? m_vertHooks[row][col] : m_horizHooks[row][col];
		if (hook.touches) {
			const Letter letter =
				QUACKLE_ALPHABET_PARAMETERS->clearBlankness(word[wordPos]);
			const uint32_t letterMask = 1 << letter;
			if ((hook.letters & letterMask) == 0) return false;
		}
		if (spot.horizontal) col++; else row++;
	}
	if (spot.horizontal) {
		col = spot.anchorCol - behind;
	} else {
		row = spot.anchorRow - behind;
	}
	for (int wordPos = 0; wordPos < behind; ++wordPos) {
		const Hook& hook =
			spot.horizontal ? m_vertHooks[row][col] : m_horizHooks[row][col];
		if (hook.touches) {
			const Letter letter =
				QUACKLE_ALPHABET_PARAMETERS->clearBlankness(word[wordPos]);
			const uint32_t letterMask = 1 << letter;
			if ((hook.letters & letterMask) == 0) return false;
		}
		if (spot.horizontal) col++; else row++;
	}
	row = spot.anchorRow;
	col = spot.anchorCol;
	int anchorPos = spot.horizontal ? col : row;
	int startPos = anchorPos - behind;
	if (spot.horizontal) col -= behind; else row -= behind;
	for (int wordPos = 0; wordPos < ahead + behind; ++wordPos) {
		int pos = startPos + wordPos;
		m_placed[pos] = word[wordPos];
		if (!QUACKLE_ALPHABET_PARAMETERS->isBlankLetter(word[wordPos])) {
			int letterMultiplier =
				QUACKLE_BOARD_PARAMETERS->letterMultiplier(row, col);
			int hookMultiplier = hookLetterMultiplier(row, col, spot.horizontal);
			scoreLetter(pos, word[wordPos], letterMultiplier);
			const int tileHookScore =
				hookMultiplier * QUACKLE_ALPHABET_PARAMETERS->score(word[wordPos]);
			*wordHookScore += tileHookScore;
		}
		if (spot.horizontal) col++; else row++;
	}
	return true;
}

int V2Generator::getWordMultiplierAndHooks(const Spot& spot, int ahead, int behind) {
	int product = 1;
	m_hookScore = 0;
	int row = spot.anchorRow;
	int col = spot.anchorCol;
	for (int i = 0; i < ahead; ++i) {
		const Hook& hook =
			spot.horizontal ? m_vertHooks[row][col] : m_horizHooks[row][col];
		if (hook.touches) {
			m_hookScore += hook.score;
		}
		product *= QUACKLE_BOARD_PARAMETERS->wordMultiplier(row, col);
		if (spot.horizontal) col++; else row++;
	}
	if (spot.horizontal) {
		col = spot.anchorCol - behind;
	} else {
		row = spot.anchorRow - behind;
	}
	for (int i = 0; i < behind; ++i) {
		const Hook& hook =
			spot.horizontal ? m_vertHooks[row][col] : m_horizHooks[row][col];
		if (hook.touches) {
			m_hookScore += hook.score;
		}
		product *= QUACKLE_BOARD_PARAMETERS->wordMultiplier(row, col);
		if (spot.horizontal) col++; else row++;
	}
	return product;
}

void V2Generator::fitBingos(Spot* spot) {
  if (m_bingos.empty()) return;
	// UVcout << "minTilesAhead: " << spot->minTilesAhead
	// 			 << ", maxTilesAhead: " << spot->maxTilesAhead
	// 			 << ", maxTilesBehind: " << spot->maxTilesBehind << endl;
	for (int ahead = spot->minTilesAhead; ahead <= spot->maxTilesAhead; ++ahead) {
		const int behind = 7 - ahead;
		if (behind > spot->maxTilesBehind) continue;
		int wordMultiplier = getWordMultiplierAndHooks(*spot, ahead, behind);
		for (const LetterString bingo : m_bingos) {
			/*
			UVcout << "trying to fit [";
			for (int i = 0; i < behind; ++i) {
				UVcout << QUACKLE_ALPHABET_PARAMETERS->userVisible(bingo[i]);
			}
			UVcout << "] behind, [";
			for (int i = 0; i < ahead; ++i) {
				UVcout << QUACKLE_ALPHABET_PARAMETERS->userVisible(bingo[behind + i]);
			}
			UVcout << "] ahead" << endl;
			*/
			int wordHookScore = 0;
			if (fits(*spot, bingo, ahead, behind, &wordHookScore)) {
				/*
				UVcout << "bingo "
							 << QUACKLE_ALPHABET_PARAMETERS->userVisible(bingo)
							 << " fits here." << endl;
				*/
			} else {
				continue;
			}
			m_hookScore += wordHookScore;
			if (maybeRecordMove(*spot, wordMultiplier, behind, behind + ahead)) {
				restrictSpot(spot);
			}
			m_hookScore -= wordHookScore;
		}
	}
}

void V2Generator::findMoreBlankless(Spot* spot, int delta, int ahead,
																		int behind, int velocity, int wordMultiplier,
																		const V2Gaddag& gaddag,
																		const unsigned char* node) {
	if (velocity < 0) {
		if (behind < spot->maxTilesBehind &&
				behind + 1 <= spot->hindmostViable) {
			findBlankless(spot, delta - 1, ahead, behind + 1, -1, wordMultiplier,
										gaddag, node);
		}
		if (ahead >= spot->maxTilesAhead ||
				!spot->viableWithBehind(behind)) {
			return;
		}
#ifdef V2GEN
		UVcout << "changing direction..." << endl;
#endif
		const unsigned char* changeChild = gaddag.changeDirection(node);
		if (changeChild != NULL) {
			node = gaddag.followIndex(changeChild);
			if (node != NULL) { 
				findBlankless(spot, 1, ahead + 1, behind, 1, wordMultiplier,
											gaddag, node);
			}
		}
	} else if (ahead < spot->maxTilesAhead) {
		findBlankless(spot, delta + 1, ahead + 1, behind, 1, wordMultiplier,
									gaddag, node);
	}
}

void V2Generator::findMoreBlankRequired(Spot* spot, int delta, int ahead,
																				int behind, int velocity, int wordMultiplier,
																				const V2Gaddag& gaddag,
																				const unsigned char* node) {
	if (velocity < 0) {
		if (behind < spot->maxTilesBehind &&
				behind + 1 <= spot->hindmostViable) {
			findBlankRequired(spot, delta - 1, ahead, behind + 1, -1, wordMultiplier,
												gaddag, node);
		}
		if (ahead >= spot->maxTilesAhead ||
				!spot->viableWithBehind(behind)) {
			return;
		}
#ifdef V2GEN
		UVcout << "changing direction..." << endl;
#endif
		const unsigned char* changeChild = gaddag.changeDirection(node);
		if (changeChild != NULL) {
			node = gaddag.followIndex(changeChild);
			if (node != NULL) { 
				findBlankRequired(spot, 1, ahead + 1, behind, 1, wordMultiplier,
													gaddag, node);
			}
		}
	} else if (ahead < spot->maxTilesAhead) {
		findBlankRequired(spot, delta + 1, ahead + 1, behind, 1, wordMultiplier,
											gaddag, node);
	}
}

const unsigned char* V2Generator::followToRealChild(const V2Gaddag& gaddag,
																										int row, int col,
																										bool horizontal,
																										const unsigned char* child) {
	const unsigned char* node;
	Letter letter;
	for (;;) {
		if (horizontal) col++; else row++;
    if (row > 15 || col > 15 || isEmpty(row, col)) return child;
		node = gaddag.followIndex(child);
		if (node == NULL) {
			//UVcout << "child leads to nothing" << endl;
			return NULL;
		}
		// UVcout << "following "
		// 			 << QUACKLE_ALPHABET_PARAMETERS->userVisible(boardLetter(row, col))
		// 			 << " at " << static_cast<char>('A' + col) << row << endl;
		letter = QUACKLE_ALPHABET_PARAMETERS->clearBlankness(boardLetter(row, col));
		if (!gaddag.hasChild(node, letter)) {
			//UVcout << "Can't extend with that letter, returning NULL" << endl;
			return NULL;
		}
		child = gaddag.child(node, letter);
	}
}
void V2Generator::findBlankless(Spot* spot, int delta, int ahead, int behind,
																int velocity, int wordMultiplier,
																const V2Gaddag& gaddag,
																const unsigned char* node) {
#ifdef DEBUG_V2GEN
  if (!board()->isEmpty()) {
		UVcout << "findBlankless(delta: " << delta << ", ahead: " << ahead
					 << ", behind: " << behind << ", velocity: " << velocity << ")" << endl;
	}
#endif

	if (spot->numTilesThrough > 0 && delta == 0) {
		assert(node != NULL);
		// This is a through spot, and there's nothing to be placed here at the
		// anchor (a tile is already there). Look ahead and behind.
		//UVcout << "'Anchor' of through spot, nothing to place here..." << endl;
		findMoreBlankless(spot, delta, ahead, behind, velocity, wordMultiplier,
											gaddag, node);
		return;
	}
	
	int numPlaced, row, col, pos;
	getSquare(*spot, delta, &row, &col, &pos);
  numPlaced =	ahead + behind;
  //UVcout << "row: " << row << ", col: " << col << endl;
	Letter minLetter = QUACKLE_GADDAG_SEPARATOR;
	int childIndex = 0;
	Letter foundLetter;
	uint32_t possibleLetters = m_rackBits;
	const Hook& hook =
		spot->horizontal ? m_vertHooks[row][col] : m_horizHooks[row][col];
	if (hook.touches) {
		possibleLetters &= hook.letters;
		if (possibleLetters == 0) {
			//UVcout << "no possible letters" << endl;
			return;
		}
		//UVcout << "possible letters: " << debugLetterMask(possibleLetters) << endl;
		//UVcout << "hook.score: " << hook.score << ", m_hookScore: "
		//			 << m_hookScore << " -> " << (m_hookScore + hook.score) << endl;
		m_hookScore += hook.score;
	}
	int newWordMultiplier = wordMultiplier *
		QUACKLE_BOARD_PARAMETERS->wordMultiplier(row, col);
	int letterMultiplier = QUACKLE_BOARD_PARAMETERS->letterMultiplier(row, col);
	int hookMultiplier = hookLetterMultiplier(row, col, spot->horizontal);
	const unsigned char* child = NULL;
	while (nextLetter(gaddag, node, possibleLetters, minLetter,
										&childIndex, &foundLetter, &child)) {
		if (child == NULL) UVcout << "child is still NULL!" << endl;
		if (velocity > 0 && spot->numTilesThrough > 1) {
			child = followToRealChild(gaddag, row, col, spot->horizontal, child);
			if (child == NULL) {
				minLetter = foundLetter + 1;
				++childIndex;
				continue;
			}
		}
    const int tileMainScore = scoreLetter(pos, foundLetter, letterMultiplier);
		const int tileHookScore =
			hookMultiplier * QUACKLE_ALPHABET_PARAMETERS->score(foundLetter);
		// UVcout << "tileHookScore: " << tileHookScore
		//  			 << ", m_hookScore: " << m_hookScore << " -> "
		//  			 << (m_hookScore + tileHookScore) << endl;
		m_hookScore += tileHookScore;
		uint32_t foundLetterMask; // reused below in unuseLetter
    useLetter(foundLetter, &foundLetterMask);
		//#ifdef DEBUG_V2GEN
		//debugPlaced(*spot, behind, ahead);
		//#endif
		if (spot->viableAtLength(numPlaced) &&
				ahead >= spot->minTilesAhead &&
				gaddag.completesWord(child) &&
				maybeRecordMove(*spot, newWordMultiplier, behind, numPlaced)) {
			//debugPlaced(*spot, behind, ahead);
			//#ifdef DEBUG_V2GEN
			//UVcout << "better than " << m_best.equity;
			//#endif
			restrictSpot(spot);
		}
		if (numPlaced + 1 <= spot->longestViable) {
			const unsigned char* newNode = gaddag.followIndex(child);
			if (newNode != NULL) {
				findMoreBlankless(spot, delta, ahead, behind, velocity,
													newWordMultiplier, gaddag, newNode);
			}
		}
#ifdef DEBUG_V2GEN
		else {
			UVcout << "don't need to check longer than " << numPlaced << endl;
		}
#endif

		// UVcout << "tileHookScore: " << tileHookScore
		// 			 << ", m_hookScore: " << m_hookScore << " -> "
		// 			 << (m_hookScore - tileHookScore) << endl;
		m_hookScore -= tileHookScore;
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

	// UVcout << "hook.score: " << hook.score
	// 			 << ", m_hookScore: " << m_hookScore << " -> "
	// 			 << (m_hookScore - hook.score) << endl;
	m_hookScore -= hook.score;
}


void V2Generator::findBlankRequired(Spot* spot, int delta, int ahead, int behind,
																		int velocity, int wordMultiplier,
																		const V2Gaddag& gaddag,
																		const unsigned char* node) {
#ifdef DEBUG_V2GEN
  if (!board()->isEmpty()) {
		UVcout << "findBlankRequired(delta: " << delta << ", ahead: " << ahead
					 << ", behind: " << behind << ", velocity: " << velocity << ")" << endl;
	}
#endif
	
	if (spot->numTilesThrough > 0 && delta == 0) {
		assert(node != NULL);
		// This is a through spot, and there's nothing to be placed here at the
		// anchor (a tile is already there). Look ahead and behind.
		findMoreBlankRequired(spot, delta, ahead, behind, velocity, wordMultiplier,
													gaddag, node);
		return;
	}
	
#ifdef DEBUG_V2GEN
	UVcout << "findBlankRequired(delta: " << delta << ", ahead: " << ahead
				 << ", behind: " << behind << ", velocity: " << velocity << ")" << endl;
#endif
	int numPlaced, row, col, pos;
	getSquare(*spot, delta, &row, &col, &pos);
  numPlaced =	ahead + behind;
  //UVcout << "row: " << row << ", col: " << col << endl;
	int newWordMultiplier = wordMultiplier *
		QUACKLE_BOARD_PARAMETERS->wordMultiplier(row, col);
	Letter minLetter = QUACKLE_GADDAG_SEPARATOR;
	int childIndex = 0;
	Letter foundLetter;
	assert(blankOnRack());
	uint32_t possibleLetters = EVERY_LETTER;
	const Hook& hook =
		spot->horizontal ? m_vertHooks[row][col] : m_horizHooks[row][col];
	if (hook.touches) {
		//UVcout << "hook touches at row: " << row << ", col: " << col << endl;
		//UVcout << "hook.letters: " << debugLetterMask(hook.letters) << endl;
		possibleLetters &= hook.letters;
		if (possibleLetters == 0) return;
    //UVcout << "hook.score: " << hook.score << ", m_hookScore: "
		//			 << m_hookScore << " -> " << (m_hookScore + hook.score) << endl;
		m_hookScore += hook.score;
	}
	const unsigned char* child = NULL;
	m_counts[QUACKLE_BLANK_MARK]--;
  while (nextLetter(gaddag, node, possibleLetters, minLetter,
										&childIndex, &foundLetter, &child)) {
		assert(child != NULL);
		if (velocity > 0 && spot->numTilesThrough > 1) {
			child = followToRealChild(gaddag, row, col, spot->horizontal, child);
			if (child == NULL) {
				minLetter = foundLetter + 1;
				++childIndex;
				continue;
			}
		}
    Letter blankLetter = QUACKLE_ALPHABET_PARAMETERS->setBlankness(foundLetter);
		//#ifdef DEBUG_V2GEN
		assert(foundLetter >= QUACKLE_ALPHABET_PARAMETERS->firstLetter());
		assert(foundLetter <= QUACKLE_ALPHABET_PARAMETERS->lastLetter());
		// UVcout << "placing " << QUACKLE_ALPHABET_PARAMETERS->userVisible(blankLetter)
		// 			 << " at m_placed[" << pos << "]" << endl;
		m_placed[pos] = blankLetter;
		//debugPlaced(*spot, behind, ahead);
		//#endif

		if (spot->viableAtLength(numPlaced) &&
				ahead >= spot->minTilesAhead &&
				gaddag.completesWord(child) &&
				maybeRecordMove(*spot, newWordMultiplier, behind, numPlaced)) {
			//#ifdef DEBUG_V2GEN
			//UVcout << "better than " << m_best.equity;
			//#endif
			restrictSpot(spot);
		}
		if (numPlaced + 1 <= spot->longestViable) {
			const unsigned char* newNode = gaddag.followIndex(child);
			if (newNode != NULL) {
				if (blankOnRack()) {
					findMoreBlankRequired(spot, delta, ahead, behind, velocity,
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

		minLetter = foundLetter + 1;
		++childIndex;
	}
	m_counts[QUACKLE_BLANK_MARK]++;
	if (m_rackBits == 0 ||
			// We must use a blank. If it isn't possibly worth adding another letter
			// after this one, then don't try using a non-blank letter here.
			(!blankWasPlayed() &&
			 (numPlaced + 1 > spot->longestViable))) {
		/*
		UVcout << "hook.score: " << hook.score
					 << ", m_hookScore: " << m_hookScore << " -> "
					 << (m_hookScore - hook.score) << endl;
		*/
		m_hookScore -= hook.score;
		return;
	}
	minLetter = QUACKLE_GADDAG_SEPARATOR;
	childIndex = 0;
	child = NULL;
	possibleLetters = m_rackBits;
	if (hook.touches) {
		//UVcout << "hook touches at row: " << row << ", col: " << col << endl;
		//UVcout << "hook.letters: " << debugLetterMask(hook.letters) << endl;
		possibleLetters &= hook.letters;
		//UVcout << "possibleLetters: " << debugLetterMask(possibleLetters) << endl;
		if (possibleLetters == 0) {
			// UVcout << "hook.score: " << hook.score
			// 	 << ", m_hookScore: " << m_hookScore << " -> "
			// 	 << (m_hookScore - hook.score) << endl;
			m_hookScore -= hook.score;
			return;
		}
	}
	int letterMultiplier = QUACKLE_BOARD_PARAMETERS->letterMultiplier(row, col);
	int hookMultiplier = hookLetterMultiplier(row, col, spot->horizontal);
	while (nextLetter(gaddag, node, possibleLetters, minLetter,
										&childIndex, &foundLetter, &child)) {
		assert(child != NULL);
		if (velocity > 0 && spot->numTilesThrough > 1) {
			child = followToRealChild(gaddag, row, col, spot->horizontal, child);
			if (child == NULL) {
				minLetter = foundLetter + 1;
				++childIndex;
				continue;
			}
		}

    const int tileMainScore = scoreLetter(pos, foundLetter, letterMultiplier);
		const int tileHookScore =
			hookMultiplier * QUACKLE_ALPHABET_PARAMETERS->score(foundLetter);
		// UVcout << "tileHookScore: " << tileHookScore
		// 			 << ", m_hookScore: " << m_hookScore << " -> "
		// 			 << (m_hookScore + tileHookScore) << endl;
		m_hookScore += tileHookScore;
		uint32_t foundLetterMask; // reused below in unuseLetter
    useLetter(foundLetter, &foundLetterMask);
#ifdef DEBUG_V2GEN
		debugPlaced(*spot, behind, ahead);
#endif		
		if (blankWasPlayed() && spot->viableAtLength(numPlaced) &&
				ahead >= spot->minTilesAhead &&
				gaddag.completesWord(child) &&
				maybeRecordMove(*spot, newWordMultiplier, behind, numPlaced)) {
#ifdef DEBUG_V2GEN
			UVcout << "better than " << m_best.equity;
#endif
			restrictSpot(spot);
		}
		if (numPlaced + 1 <= spot->longestViable) {
			const unsigned char* newNode = gaddag.followIndex(child);
			if (newNode != NULL) {
				if (blankOnRack()) {
					findMoreBlankRequired(spot, delta, ahead, behind, velocity,
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

		// UVcout << "tileHookScore: " << tileHookScore
		// 			 << ", m_hookScore: " << m_hookScore << " -> "
		// 			 << (m_hookScore - tileHookScore) << endl;
		m_hookScore -= tileHookScore;
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
	// UVcout << "hook.score: " << hook.score
	// 			 << ", m_hookScore: " << m_hookScore << " -> "
	// 			 << (m_hookScore - hook.score) << endl;
	m_hookScore -= hook.score;
}

float V2Generator::bestLeave(const Spot& spot, int length) {
	assert(length > 0);
	assert(length < 7);
	if (spot.numTilesThrough <= 1 && m_anagrams != NULL) {
		const UsesTiles& usesTiles =
			spot.useBlank ? m_anagrams->mustUseBlank : m_anagrams->usesNoBlanks;
		int index = length - 1;
		if (spot.numTilesThrough == 1) {
			Letter thruLetter =
				QUACKLE_ALPHABET_PARAMETERS->clearBlankness(boardLetter(spot.anchorRow,
																															spot.anchorCol));
			uint32_t letterMask = 1 << thruLetter;
			if ((usesTiles.anahooks & letterMask) == 0) return false;
			uint32_t beforeLetterMask = (1 << thruLetter) - 1;
			int anahookIndex = __builtin_popcount(usesTiles.anahooks &
																						beforeLetterMask);
			const NTileAnagrams& anagrams = usesTiles.thruOne[anahookIndex];
			return anagrams.bestLeaves[index] / 256.0f;
		}
		const NTileAnagrams& anagrams = usesTiles.thruNone;
		return anagrams.bestLeaves[index] / 256.0f;
	}
	return m_bestLeaves[length];
}

int V2Generator::hookLetterMultiplier(int row, int col, bool horiz) {
	const Hook& hook =
			horiz ? m_vertHooks[row][col] : m_horizHooks[row][col];
	if (hook.touches) {
		return
			QUACKLE_BOARD_PARAMETERS->letterMultiplier(row, col) *
			QUACKLE_BOARD_PARAMETERS->wordMultiplier(row, col);
	}
	return 0;
}

void V2Generator::scoreSpot(Spot* spot) {
	spot->canMakeAnyWord = false;
	if (spot->numTilesThrough <= 1) {
		int maxPlayable = 0;
		for (int i = spot->minPlayed; i <= spot->maxPlayed; ++i) {
			if (couldMakeWord(*spot, i)) {
				maxPlayable = i;
			}
		}
		//UVcout << "spot->minPlayed: " << spot->minPlayed << endl;
		//UVcout << "spot->maxPlayed (originally): " << spot->maxPlayed << endl;
		//UVcout << "maxPlayable: " << maxPlayable << endl;
		if (maxPlayable == 0) return;
		spot->maxPlayed = maxPlayable;
	}
	float maxEquity = -9999;
	for (int i = 1; i <= 7; ++i) {
		spot->worthChecking[i].maxEquity = maxEquity;
		spot->worthChecking[i].couldBeBest = false;
	}
	for (int i = 0; i <= 7; ++i) {
		spot->worthCheckingBehind[i].maxEquity = maxEquity;
		spot->worthCheckingBehind[i].couldBeBest = false;
	}
  	
	const int numTiles = rack().size();
	const LetterString& tiles = rack().tiles();
	int tileScores[QUACKLE_MAXIMUM_BOARD_SIZE];
	for (int i = 0; i < numTiles; ++i) {
		tileScores[i] = QUACKLE_ALPHABET_PARAMETERS->score(tiles[i]);
	}
	std::sort(tileScores, tileScores + numTiles, std::greater<int>());
	int wordMultipliers[QUACKLE_MAXIMUM_BOARD_SIZE];
	int hookLetterMultipliers[QUACKLE_MAXIMUM_BOARD_SIZE];
	int letterMultipliers[QUACKLE_MAXIMUM_BOARD_SIZE];
	int hookScores[QUACKLE_MAXIMUM_BOARD_SIZE];
	int row = spot->anchorRow;
	int col = spot->anchorCol;
	int anchorPos = (spot->horizontal) ? col : row;
	for (int i = 0; i <= spot->maxTilesAhead; ++i) {
		if (board()->isNonempty(row, col)) {
			i--;
		} else {
			wordMultipliers[anchorPos + i] =
				QUACKLE_BOARD_PARAMETERS->wordMultiplier(row, col);
			hookLetterMultipliers[anchorPos + i] =
				hookLetterMultiplier(row, col, spot->horizontal);
			letterMultipliers[anchorPos + i] =
				QUACKLE_BOARD_PARAMETERS->letterMultiplier(row, col);
			const Hook& hook =
				spot->horizontal ? m_vertHooks[row][col] : m_horizHooks[row][col];
			hookScores[anchorPos + i] = hook.score;
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
			hookLetterMultipliers[anchorPos - i] =
				hookLetterMultiplier(row, col, spot->horizontal);
			letterMultipliers[anchorPos - i] =
				QUACKLE_BOARD_PARAMETERS->letterMultiplier(row, col);
			const Hook& hook =
				spot->horizontal ? m_vertHooks[row][col] : m_horizHooks[row][col];
			hookScores[anchorPos - i] = hook.score;
		}
	}
  for (int ahead = spot->minTilesAhead; ahead <= spot->maxTilesAhead; ++ahead) {
		// num tiles played behind anchor + ahead of anchor <= num tiles on rack
		//UVcout << "ahead: " << ahead << endl;
		const int minBehind = std::max(0, spot->minPlayed - ahead);
		const int maxBehind = std::min(spot->maxTilesBehind, spot->maxPlayed - ahead);
		//UVcout << "minBehind: " << minBehind << endl;
		//UVcout << "maxBehind: " << maxBehind << endl;
		for (int behind = minBehind; behind <= maxBehind; ++behind) {
			//UVcout << "ahead: " << ahead << ", behind: " << behind << endl;
			//struct timeval start, end;
			//gettimeofday(&start, NULL);

			const int played = ahead + behind;
			//UVcout << "played: " << played << endl;
			if (played == 0) continue; // have to play at least one tile!
			if (spot->numTilesThrough == 0 && played == 1) {
				// Must play a word of at least 2 letters
				continue;
			}
			if (!couldMakeWord(*spot, played)) {
				//UVcout << "can not make word of length " << played << endl;
				continue;
			} else {
				spot->worthChecking[played].couldBeBest = true;
				spot->worthCheckingBehind[behind].couldBeBest = true;
				//UVcout << "can make word of length " << played << endl;
			}    
			spot->canMakeAnyWord = true;
			int wordMultiplier = 1;
			int usedLetterMultipliers[QUACKLE_MAXIMUM_BOARD_SIZE];
			int minPos = anchorPos - behind;
			int hookScore = 0;
			for (int pos = minPos; pos < minPos + played; ++pos) {
				wordMultiplier *= wordMultipliers[pos];
				hookScore += hookScores[pos];
			}
			memcpy(&usedLetterMultipliers, &(letterMultipliers[minPos]),
						 played * sizeof(int));
      for (int i = 0; i < played; ++i) {
				usedLetterMultipliers[i] *= wordMultiplier;
				usedLetterMultipliers[i] += hookLetterMultipliers[i + minPos];
			}
			std::sort(usedLetterMultipliers,
								usedLetterMultipliers + played,
								std::greater<int>());
			int playedScore = 0;
			int maxNonBlankTiles = played;
			//UVcout << "played: " << played << endl;
			if (spot->useBlank) maxNonBlankTiles--;
			for (int i = 0; i < maxNonBlankTiles; ++i) {
				//UVcout << "playedScore += " << tileScores[i] << " * "
				//			 << usedLetterMultipliers[i] << endl;
				playedScore += usedLetterMultipliers[i] * tileScores[i];
			}
			//UVcout << "playedScore: " << playedScore << endl;
			int score = (spot->throughScore * wordMultiplier) + playedScore + hookScore;
			//UVcout << "score: " << playedScore << " + " << hookScore
			//			 << " = " << score << endl;
			if (played == QUACKLE_PARAMETERS->rackSize()) {
				score += QUACKLE_PARAMETERS->bingoBonus();
				//UVcout << "score += 50 for bingo bonus";
			}
			//UVcout << "score: " << score << endl;
			float optimisticEquity = score;
			if (played < 7) {
				optimisticEquity += bestLeave(*spot, played);
				//UVcout << "optimisticEquity += leave (" << bestLeave(*spot, played)
				//			 << ")" << endl;
				/*
				UVcout << "use?:" << spot->useBlank << ", played: " << played
							 << ", minPos: " << minPos << ", score: " << score
							 << ", leave: " << bestLeave(*spot, played)
							 << ", optimisticEquity: " << optimisticEquity << endl;
				*/
			}
			//UVcout << "optimisticEquity: " << optimisticEquity << endl;
			maxEquity = std::max(maxEquity, optimisticEquity);
			spot->worthChecking[played].maxEquity =
				std::max(spot->worthChecking[played].maxEquity, optimisticEquity);
			spot->worthCheckingBehind[behind].maxEquity =
				std::max(spot->worthCheckingBehind[behind].maxEquity,
								 optimisticEquity);

			// gettimeofday(&end, NULL);
			// UVcout << "Time scoring behind: " << behind << " ahead: " << ahead << " "
			// 	 << ((end.tv_sec * 1000000 + end.tv_usec)
			// 			 - (start.tv_sec * 1000000 + start.tv_usec)) << " microseconds." << endl;

		}
	}
	if (!spot->canMakeAnyWord) return;
	spot->maxEquity = maxEquity;
	// UVcout << "Spot: (" << spot->anchorRow << ", " << spot->anchorCol
	// 			 << ", blank: " << spot->useBlank << ") "
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
	while (nextLetter(gaddag, node, EVERY_LETTER, minLetter,
										&childIndex, &foundLetter, &child)) {
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

void V2Generator::updateVerticalHooks(int row, int col) {
	//UVcout << "row: " << row << ", col: " << col << endl;
	assert(isEmpty(row, col));
	int numLettersBefore = 0;
	Hook& hook = m_vertHooks[row][col];
	hook.score = 0;
	for (int beforeRow = row - 1; beforeRow >= 0; beforeRow--) {
		if (isEmpty(beforeRow, col)) break;
		hook.score += tileScore(beforeRow, col);
		++numLettersBefore;
	}
	int numLettersAfter = 0;
	for (int afterRow = row + 1; afterRow < 15; afterRow++) {
		if (isEmpty(afterRow, col)) break;
		hook.score += tileScore(afterRow, col);
		++numLettersAfter;
	}
	//UVcout << "numLettersBefore: " << numLettersBefore << endl;
	//UVcout << "numLettersAfter: " << numLettersAfter << endl;
	if (numLettersBefore + numLettersAfter == 0) {
		hook.touches = false;
		return;
	}
	hook.score *= QUACKLE_BOARD_PARAMETERS->wordMultiplier(row, col);
	hook.touches = true;
	hook.letters = 0;
	const unsigned char* beforeNode = NULL;
	if (numLettersBefore > 0) {
		beforeNode = vertBeforeNode(row, col, numLettersBefore);
		if (beforeNode == NULL) return;
	}
	const unsigned char* afterNode = NULL;
	if (numLettersAfter > 0) {
		afterNode = vertAfterNode(row, col, numLettersAfter);
		if (afterNode == NULL) return;
	}
	if (numLettersAfter == 0) {
		//UVcout << "wordCompleters: " << debugLetterMask(wordCompleters(beforeNode))
		//			 << endl;
		hook.letters = wordCompleters(beforeNode);
		return;
	}
	if (numLettersBefore == 0) {
		/*
		UVcout << "wordCompleters: " << debugLetterMask(wordCompleters(afterNode))
					 << endl;
		*/
		hook.letters = wordCompleters(afterNode);
		return;
	}
	if (beforeNode == NULL || afterNode == NULL) return;
	/*
	UVcout << "between: "
				 << debugLetterMask(vertBetween(row, col, numLettersAfter, beforeNode, afterNode))
				 << endl;
	*/
	hook.letters = vertBetween(row, col, numLettersAfter, beforeNode, afterNode);
}

int V2Generator::tileScore(int row, int col) {
	//UVcout << "tileScore(" << row << ", " << col << ")" << endl;
	const Letter letter = boardLetter(row, col);
	if (QUACKLE_ALPHABET_PARAMETERS->isPlainLetter(letter)) {
		return QUACKLE_ALPHABET_PARAMETERS->score(letter);
	} else {
		return 0;
	}
}

void V2Generator::updateHorizontalHooks(int row, int col) {
	//UVcout << "row: " << row << ", col: " << col << endl;
	assert(isEmpty(row, col));
	Hook& hook = m_horizHooks[row][col];
	hook.score = 0;
	int numLettersBefore = 0;
	for (int beforeCol = col - 1; beforeCol >= 0; beforeCol--) {
		if (isEmpty(row, beforeCol)) break;
    hook.score += tileScore(row, beforeCol);
		++numLettersBefore;
	}
	int numLettersAfter = 0;
	for (int afterCol = col + 1; afterCol < 15; afterCol++) {
		if (isEmpty(row, afterCol)) break;
		hook.score += tileScore(row, afterCol);
		++numLettersAfter;
	}
	//UVcout << "numLettersBefore: " << numLettersBefore << endl;
	//UVcout << "numLettersAfter: " << numLettersAfter << endl;
	if (numLettersBefore + numLettersAfter == 0) {
		hook.touches = false;
		return;
	}
	hook.score *= QUACKLE_BOARD_PARAMETERS->wordMultiplier(row, col);
	hook.touches = true;
	hook.letters = 0;
	const unsigned char* beforeNode = NULL;
	if (numLettersBefore > 0) {
		beforeNode = horizBeforeNode(row, col, numLettersBefore);
		if (beforeNode == NULL) return;
	}
	const unsigned char* afterNode = NULL;
	if (numLettersAfter > 0) {
		afterNode = horizAfterNode(row, col, numLettersAfter);
		if (afterNode == NULL) return;
	}
	if (numLettersAfter == 0) {
		//UVcout << "wordCompleters: " << debugLetterMask(wordCompleters(beforeNode))
		//			 << endl;
		hook.letters = wordCompleters(beforeNode);
		return;
	}
	if (numLettersBefore == 0) {
		//UVcout << "wordCompleters: " << debugLetterMask(wordCompleters(afterNode))
		//			 << endl;
		hook.letters = wordCompleters(afterNode);
		return;
	}
	if (beforeNode == NULL || afterNode == NULL) return;
	/*
	UVcout << "between: "
				 << debugLetterMask(horizBetween(row, col, numLettersAfter, beforeNode, afterNode))
				 << endl;
	*/
	hook.letters = horizBetween(row, col, numLettersAfter, beforeNode, afterNode);
}

void V2Generator::debugVertHook(int row, int col) {
	const Hook& hook = m_vertHooks[row][col];
	UVcout << "  vert hooks at row: " << row << ", col: " << col << endl;
	if (hook.touches) {
		UVcout << "  touches something... ";
	} else {
		UVcout << "  touches nothing." << endl;
		return;
	}
	UVcout << "hook letters: " << debugLetterMask(hook.letters);
	if (hook.letters != 0) {
		UVcout << ", score: " << hook.score;
	}
	UVcout << endl;
}

void V2Generator::debugHorizHook(int row, int col) {
	const Hook& hook = m_horizHooks[row][col];
	UVcout << "  horiz hooks at row: " << row << ", col: " << col << endl;
	if (hook.touches) {
		UVcout << "  touches something... "; 
	} else {
		UVcout << "  touches nothing." << endl;
		return;
	}
	UVcout << "hook letters: " << debugLetterMask(hook.letters);
	if (hook.letters != 0) {
		UVcout << ", score: " << hook.score;
	}
	UVcout << endl;
}

void V2Generator::debugHooks() {
	for (int row = 0; row < 15; ++row) {
		for (int col = 0; col < 15; ++col) {
			const Hook& hook = m_vertHooks[row][col];
			if (hook.touches) {
				debugVertHook(row, col);
			}
		}
	}
	for (int row = 0; row < 15; ++row) {
		for (int col = 0; col < 15; ++col) {
			const Hook& hook = m_horizHooks[row][col];
			if (hook.touches) {
				debugHorizHook(row, col);
			}
		}
	}
}

bool V2Generator::couldHaveBingos() const {
	if (m_anagrams == NULL) return true;
	const int lengthMask = 1 << 7;
	const UsesTiles usesTiles =
		blankOnRack() ? m_anagrams->mustUseBlank : m_anagrams->usesNoBlanks;
	const NTileAnagrams anagrams = usesTiles.thruNone;
	return (anagrams.numPlayed & lengthMask) != 0;
}

bool V2Generator::blankOnRack() const {
	return m_counts[QUACKLE_BLANK_MARK] > 0;
}

bool V2Generator::blankWasPlayed() const {
	return m_counts[QUACKLE_BLANK_MARK] < m_numBlanks;
}

uint32_t V2Generator::otherRackBits(uint32_t rackBits, uint32_t rackHooks) const {
	if (__builtin_popcount(rackHooks) == 1) {
		Letter onlyHook = __builtin_ffs(rackHooks) - 1;
		/*		
		UVcout << "rackBits: " << debugLetterMask(rackBits)
					 << ", counts: " << counts2string()
					 << ", rackHooks: " << debugLetterMask(rackHooks)
					 << ", only hook here is "
					 << QUACKLE_ALPHABET_PARAMETERS->userVisible(onlyHook) << endl;
		*/
		if (m_counts[onlyHook] > 1) return rackBits;
		if (m_counts[onlyHook] == 1) {
			if (blankOnRack()) {
				return rackBits;
			} else {
				uint32_t hookLetterMask = 1 << onlyHook;
				return rackBits & ~hookLetterMask;
			}
		} else {
			assert(blankOnRack());
			if (m_counts[QUACKLE_BLANK_MARK] > 1) {
				return rackBits;
			} else {
				return m_rackBits;
			}
		}
	}
	return rackBits;
}

bool V2Generator::restrictSpotUsingHooks(Spot* spot, uint32_t rackBits,
																				 uint32_t rackHooks) const {
	// Assumes no hooks behind. This may change if I try to optimize adjacent hook
	// anchors such that the most restrictive of them (rather than always the
	// first) is given room behind.

	// If a single tile (natural or blank) is required to hook at the anchor
	// square, remove it from the bit mask when checking other hooks.
	rackBits = otherRackBits(rackBits, rackHooks);
	
	int row = spot->anchorRow;
	int col = spot->anchorCol;
	for (int ahead = 1; ahead <= spot->maxTilesAhead; ahead++) {
		if (spot->horizontal) col++; else row++;
		const Hook& hook =
			spot->horizontal ? m_vertHooks[row][col] : m_horizHooks[row][col];
		if (hook.touches && ((hook.letters & rackBits) == 0)) {
			spot->maxTilesAhead = ahead;
			return true;
		}
	}
	return false;
}
																				 
void V2Generator::findHookSpotsInRow(int row, vector<Spot>* spots) {
	//UVcout << "findHookSpotsInRow(" << row << ", ...)" << endl;
	const V2Gaddag& gaddag = *(QUACKLE_LEXICON_PARAMETERS->v2Gaddag());
	const int numTiles = rack().size();
	int startCol = 0;
	for (int col = 0; col < 15; col++) {
		if (isEmpty(row, col)) {
			const Hook& hook = m_vertHooks[row][col];
			if (hook.touches) {
				uint32_t rackBitsOrBlank = blankOnRack() ? EVERY_LETTER : m_rackBits;
				uint32_t rackHooks = hook.letters & rackBitsOrBlank;
				if (rackHooks != 0) {
					Spot spot;
					spot.anchorRow = row;
					spot.anchorCol = col;
					spot.anchorNode = gaddag.root();
					spot.useBlank = false;
					spot.horizontal = true;
					spot.throughScore = 0;
					spot.numTilesThrough = 0;
					spot.maxTilesBehind = std::min(numTiles - 1, col - startCol);
					spot.minTilesAhead = 1;
					spot.maxTilesAhead = std::min(numTiles, 15 - col);
					for (int reachCol = col + 1;
							 reachCol <= std::min(14, col + spot.maxTilesAhead); ++reachCol) {
						if (!isEmpty(row, reachCol)) {
							// 0 23456 8 
							// Q TWIXT Z
							//   ^     ^
							//   col   reachCol
							spot.maxTilesAhead = reachCol - col - 1;
							break;
						}
					}
					if ((spot.maxTilesAhead >= 1)) {
						spot.longestViable = numTiles;
						spot.hindmostViable = spot.maxTilesBehind;
						spot.minPlayed = 2;
						spot.maxPlayed = std::min(numTiles,
																			spot.maxTilesAhead + spot.maxTilesBehind);
						/*
						UVcout << "Spot: (" << spot.anchorRow << ", "
						  			 << spot.anchorCol << "), "
						  			 << "maxBehind: " << spot.maxTilesBehind
						  			 << ", maxAhead: " << spot.maxTilesAhead << endl;
						*/
						if (restrictSpotUsingHooks(&spot, rackBitsOrBlank, rackHooks)) {
							/*
							UVcout << "restricted Spot: (" << spot.anchorRow << ", "
										 << spot.anchorCol << "), "
										 << "maxBehind: " << spot.maxTilesBehind
										 << ", maxAhead: " << spot.maxTilesAhead << endl;
							*/
						}
						addScoredSpots(&spot, spots);
					}
				}
				startCol = col + 1;
			}
		} else {
			while (!isEmpty(row, col)) {
				col++;
			}
			startCol = col + 1;
		}
	}
}

void V2Generator::findHookSpotsInCol(int col, vector<Spot>* spots) {
	const V2Gaddag& gaddag = *(QUACKLE_LEXICON_PARAMETERS->v2Gaddag());
	const int numTiles = rack().size();
	int startRow = 0;
	for (int row = 0; row < 15; row++) {
		if (isEmpty(row, col)) {
			const Hook& hook = m_horizHooks[row][col];
			if (hook.touches) {
				uint32_t rackBitsOrBlank = blankOnRack() ? EVERY_LETTER : m_rackBits;
				uint32_t rackHooks = hook.letters & rackBitsOrBlank;
				if (rackHooks != 0) {
					Spot spot;
					spot.anchorRow = row;
					spot.anchorCol = col;
					spot.anchorNode = gaddag.root();
					spot.useBlank = false;
					spot.horizontal = false;
					spot.throughScore = 0;
					spot.numTilesThrough = 0;
					spot.maxTilesBehind = std::min(numTiles - 1, row - startRow);
					spot.minTilesAhead = 1;
					spot.maxTilesAhead = std::min(numTiles, 15 - row);
					for (int reachRow = row + 1;
							 reachRow <= std::min(14, row + spot.maxTilesAhead); ++reachRow) {
						if (!isEmpty(reachRow, col)) {
							// 0 23456 8 
							// Q TWIXT Z
							//   ^     ^
							//   row   reachRow
							spot.maxTilesAhead = reachRow - row - 1;
							break;
						}
					}
					if ((spot.maxTilesAhead >= 1) &&
							(spot.maxTilesBehind + spot.maxTilesAhead >= 2)) {
						spot.longestViable = numTiles;
						spot.hindmostViable = numTiles;
						spot.minPlayed = 2;
						spot.maxPlayed = std::min(numTiles,
																			spot.maxTilesAhead + spot.maxTilesBehind);
						/*
						UVcout << "Spot: (" << spot.anchorRow << ", "
									 << spot.anchorCol << "), "
									 << "maxBehind: " << spot.maxTilesBehind
									 << ", maxAhead: " << spot.maxTilesAhead << endl;
						*/
						if (restrictSpotUsingHooks(&spot, rackBitsOrBlank, rackHooks)) {
							/*
							UVcout << "restricted Spot: (" << spot.anchorRow << ", "
										 << spot.anchorCol << "), "
										 << "maxBehind: " << spot.maxTilesBehind
										 << ", maxAhead: " << spot.maxTilesAhead << endl;
							*/
						}
						addScoredSpots(&spot, spots);
					}
				}
				startRow = row + 1;
			}
		} else {
			while (!isEmpty(row, col)) {
				row++;
			}
			startRow = row + 1;
		}
	}
}

void V2Generator::updateThroughAtSquare(int row, int col, int pos) {
	Through* through = &(m_throughs[m_numThroughs]);
	if (m_inThrough && isEmpty(row, col)) {
		through->end = pos - 1;
		m_numThroughs++;
		m_inThrough = false;
	} else if (!isEmpty(row, col)) {
		if (!m_inThrough) {
			m_inThrough = true;
			through->start = pos;
			through->score = 0;
		}
		through->score += tileScore(row, col);
	}
	// else !inThrough and isEmpty(row, col), so nothing to do
}

void V2Generator::finishLastThrough() {
	if (m_inThrough) { 	// a "through" reaches the edge of the board, complete it
		// 15 letter word across the board, nowhere to extend
		if (m_throughs[m_numThroughs].start == 0) return;
		m_throughs[m_numThroughs].end = 14;
		m_numThroughs++;
		assert(m_numThroughs <= 8);
	}
}

// eventually make this findSpotsInRow
void V2Generator::findThroughSpotsInRow(int row, vector<Spot>* spots) {
	const V2Gaddag& gaddag = *(QUACKLE_LEXICON_PARAMETERS->v2Gaddag());
	const int numTiles = rack().size();
	m_numThroughs = 0;
	m_inThrough = false;
	for (int col = 0; col < 15; col++) {
		updateThroughAtSquare(row, col, col);
	}
	finishLastThrough();
	if (m_numThroughs > 0) {
		//UVcout << "Throughs in row " << row << ":";
		for (int i = 0; i < m_numThroughs; ++i) {
			Through* through = &(m_throughs[i]);
			assert(through->end >= through->start);
			assert(through->score >= 0);
			through->node = gaddag.root();
      for (int col = through->end; col >= through->start; --col) {
				through->node = followLetter(gaddag, row, col, through->node);
				if (through->node == NULL) break;
			}
			/*
			UVcout << " [(" << through->start << " to " << through->end << ") for "
						 << through->score << ", ";
			if (through->node == NULL) {
				UVcout << "NULL]";
			} else {
				UVcout << "NONNULL]";
			}
			*/
		}
		//UVcout << endl;
	}
	for (int i = 0; i < m_numThroughs; ++i) {
		const Through& anchorThrough = m_throughs[i];
		if (anchorThrough.node == NULL) continue;
		/*
		UVcout << "anchorThrough: [(" << anchorThrough.start
					 << " to " << anchorThrough.end << ") for "
					 << anchorThrough.score << endl;
		*/
		Spot spot;
		spot.anchorRow = row;
		spot.anchorCol = anchorThrough.start;
		spot.anchorNode = anchorThrough.node;
		spot.horizontal = true;
		spot.throughScore = 0;
		spot.numTilesThrough = 0;
		if (i == 0) {
			spot.maxTilesBehind = anchorThrough.start;
		} else {
			// 0123  6789
			// PREV _ANCH
			// spot.maxTilesBehind = anchorThrough.start - previousThrough.end - 2;
			// spot.maxTilesBehind = 6 - 3 - 2  // 1
			const Through& prevThrough = m_throughs[i - 1];
			spot.maxTilesBehind = anchorThrough.start - prevThrough.end - 2;
		}
		spot.maxTilesBehind = std::min(spot.maxTilesBehind, numTiles);
		// No tiles already on board behind anchor as things currently work,
		// so realPosition is same as spot.anchorCol - behind
		for (int behind = 1; behind <= spot.maxTilesBehind; ++behind) {
			int col = spot.anchorCol - behind;
			spot.realPositions[col] = col;
		}
		spot.minTilesAhead = 0;
		spot.maxTilesAhead = 0;
		spot.longestViable = numTiles;
		spot.hindmostViable = spot.maxTilesBehind;
		for (int j = i; j < m_numThroughs; ++j) {
			const Through& through = m_throughs[j];
			/*
			UVcout << "through: [(" << through.start
						 << " to " << through.end << ") for "
						 << through.score << endl;
			*/
			spot.throughScore += through.score;
			spot.numTilesThrough += through.end - through.start + 1;
			spot.useBlank = false;
			const int oldMaxTilesAhead = spot.maxTilesAhead;
			if (j == m_numThroughs - 1) {
				spot.maxTilesAhead += 14 - through.end;
			} else {
				// 0123  6789
				// THRU_ NEXT
				// spot.maxTilesAhead = nextThrough.start - through.end - 2;
				// spot.maxTilesAhead = 6 - 3 - 2  // 1
				const Through& nextThrough = m_throughs[j + 1];
				spot.maxTilesAhead += nextThrough.start - through.end - 2;
			}
			spot.maxTilesAhead = std::min(spot.maxTilesAhead, numTiles);
			spot.minPlayed = 1;
			spot.maxPlayed = std::min(numTiles,
																spot.maxTilesAhead + spot.maxTilesBehind);
			int numNewAhead = spot.maxTilesAhead - oldMaxTilesAhead;
			for (int newAhead = 1; newAhead <= numNewAhead; ++newAhead) {
				const int pos = spot.anchorCol + oldMaxTilesAhead + newAhead;
				const int col = through.end + newAhead;
				assert(col < 15);
				assert(pos <= col);
        spot.realPositions[pos] = col;				
			}
			/*
			for (int delta = -1; delta >= -spot.maxTilesBehind; --delta) {
				UVcout << "delta: " << delta << ", realPosition: "
							 << spot.realPositions[spot.anchorCol + delta] << endl;
			}
			for (int delta = 1; delta <= spot.maxTilesAhead; ++delta) {
				UVcout << "delta: " << delta << ", realPosition: "
							 << spot.realPositions[spot.anchorCol + delta] << endl;
			}
			UVcout << endl;
			*/
			addScoredSpots(&spot, spots);
			if (j < m_numThroughs - 1) {
				spot.maxTilesAhead++;
				spot.minTilesAhead = spot.maxTilesAhead;
				if (spot.minTilesAhead > numTiles) {
					spot.maxTilesAhead--;
					break;
				}
				const Through& nextThrough = m_throughs[j + 1];
				spot.realPositions[spot.anchorCol + spot.maxTilesAhead] =
					nextThrough.start - 1;					
				spot.maxTilesBehind = std::min(spot.maxTilesBehind,
																			 numTiles - spot.minTilesAhead);
			}
		}
	}
}

// eventually make this findSpotsInCol
void V2Generator::findThroughSpotsInCol(int col, vector<Spot>* spots) {
	const V2Gaddag& gaddag = *(QUACKLE_LEXICON_PARAMETERS->v2Gaddag());
	const int numTiles = rack().size();
	m_numThroughs = 0;
	m_inThrough = false;
	for (int row = 0; row < 15; row++) {
		updateThroughAtSquare(row, col, row);
	}
	finishLastThrough();
	if (m_numThroughs > 0) {
		//UVcout << "Throughs in col " << col << ":";
		for (int i = 0; i < m_numThroughs; ++i) {
			Through* through = &(m_throughs[i]);
			assert(through->end >= through->start);
			assert(through->score >= 0);
			through->node = gaddag.root();
      for (int row = through->end; row >= through->start; --row) {
				through->node = followLetter(gaddag, row, col, through->node);
				if (through->node == NULL) break;
			}
			/*
			UVcout << " [(" << through->start << " to " << through->end << ") for "
						 << through->score << ", ";
			if (through->node == NULL) {
				UVcout << "NULL]";
			} else {
				UVcout << "NONNULL]";
			}
			*/
		}
		//UVcout << endl;
	}
	for (int i = 0; i < m_numThroughs; ++i) {
		const Through& anchorThrough = m_throughs[i];
		if (anchorThrough.node == NULL) continue;
		/*
		UVcout << "anchorThrough: [(" << anchorThrough.start
					 << " to " << anchorThrough.end << ") for "
					 << anchorThrough.score << endl;
		*/
		Spot spot;
		spot.anchorRow = anchorThrough.start;
		spot.anchorCol = col;
		spot.anchorNode = anchorThrough.node;
		spot.horizontal = false;
		spot.throughScore = 0;
		spot.numTilesThrough = 0;
		if (i == 0) {
			spot.maxTilesBehind = anchorThrough.start;
		} else {
			// 0123  6789
			// PREV _ANCH
			// spot.maxTilesBehind = anchorThrough.start - previousThrough.end - 2;
			// spot.maxTilesBehind = 6 - 3 - 2  // 1
			const Through& prevThrough = m_throughs[i - 1];
			spot.maxTilesBehind = anchorThrough.start - prevThrough.end - 2;
		}
		spot.maxTilesBehind = std::min(spot.maxTilesBehind, numTiles);
		// No tiles already on board behind anchor as things currently work,
		// so realPosition is same as spot.anchorRow - behind
		for (int behind = 1; behind <= spot.maxTilesBehind; ++behind) {
			int row = spot.anchorRow - behind;
			spot.realPositions[row] = row;
		}
		spot.minTilesAhead = 0;
		spot.maxTilesAhead = 0;
		spot.longestViable = numTiles;
		spot.hindmostViable = spot.maxTilesBehind;
		for (int j = i; j < m_numThroughs; ++j) {
			const Through& through = m_throughs[j];
			/*
			UVcout << "through: [(" << through.start
						 << " to " << through.end << ") for "
						 << through.score << endl;
			*/
			spot.throughScore += through.score;
			spot.numTilesThrough += through.end - through.start + 1;
			spot.useBlank = false;
			const int oldMaxTilesAhead = spot.maxTilesAhead;
			if (j == m_numThroughs - 1) {
				spot.maxTilesAhead += 14 - through.end;
			} else {
				// 0123  6789
				// THRU_ NEXT
				// spot.maxTilesAhead = nextThrough.start - through.end - 2;
				// spot.maxTilesAhead = 6 - 3 - 2  // 1
				const Through& nextThrough = m_throughs[j + 1];
				spot.maxTilesAhead += nextThrough.start - through.end - 2;
			}
			spot.maxTilesAhead = std::min(spot.maxTilesAhead, numTiles);
			spot.minPlayed = 1;
			spot.maxPlayed = std::min(numTiles,
																spot.maxTilesAhead + spot.maxTilesBehind);
			int numNewAhead = spot.maxTilesAhead - oldMaxTilesAhead;
			for (int newAhead = 1; newAhead <= numNewAhead; ++newAhead) {
				const int pos = spot.anchorRow + oldMaxTilesAhead + newAhead;
				const int row = through.end + newAhead;
				assert(row < 15);
				assert(pos <= row);
        spot.realPositions[pos] = row;				
			}
			/*
			for (int delta = -1; delta >= -spot.maxTilesBehind; --delta) {
				UVcout << "delta: " << delta << ", realPosition: "
							 << spot.realPositions[spot.anchorRow + delta] << endl;
			}
			for (int delta = 1; delta <= spot.maxTilesAhead; ++delta) {
				UVcout << "delta: " << delta << ", realPosition: "
							 << spot.realPositions[spot.anchorRow + delta] << endl;
			}
			UVcout << endl;
			*/
			addScoredSpots(&spot, spots);
			if (j < m_numThroughs - 1) {
				spot.maxTilesAhead++;
				spot.minTilesAhead = spot.maxTilesAhead;
				if (spot.minTilesAhead > numTiles) {
					spot.maxTilesAhead--;
					break;
				}
				const Through& nextThrough = m_throughs[j + 1];
				spot.realPositions[spot.anchorRow + spot.maxTilesAhead] =
					nextThrough.start - 1;					
				spot.maxTilesBehind = std::min(spot.maxTilesBehind,
																			 numTiles - spot.minTilesAhead);
			}
		}
	}
}

void V2Generator::findSpots(vector<Spot>* spots) {
	//UVcout << "findSpots()..." << endl;
	for (int row = 0; row < 15; ++row) {
		findHookSpotsInRow(row, spots);
		findThroughSpotsInRow(row, spots);
	}
	for (int col = 0; col < 15; ++col) {
		findHookSpotsInCol(col, spots);
		findThroughSpotsInCol(col, spots);
	}
}

void V2Generator::addScoredSpots(Spot* spot, vector<Spot>* spots) {
  if (blankOnRack()) {
		if (spot->maxPlayed == 7 && spot->minPlayed < 7) {
      Spot blankBingos = *spot;
			blankBingos.useBlank = true;
			if (spot->numTilesThrough <= 1) blankBingos.minPlayed = 7;
      scoreSpot(&blankBingos);
			if (blankBingos.canMakeAnyWord) spots->push_back(blankBingos);

			if (spot->numTilesThrough <= 1) {
				Spot blankNonbingos = *spot;
				blankNonbingos.useBlank = true;
				blankNonbingos.maxPlayed = 6;
				scoreSpot(&blankNonbingos);
				if (blankNonbingos.canMakeAnyWord) spots->push_back(blankNonbingos);
			}
			// nonbingos (without blank)
		  spot->maxPlayed = 6;
			scoreSpot(spot);
			if (spot->canMakeAnyWord) spots->push_back(*spot);
		} else if (spot->minPlayed == 7) {
			// just bingos (with blank)
			spot->useBlank = true;
			scoreSpot(spot);
			if (spot->canMakeAnyWord) spots->push_back(*spot);
		} else {
			// just nonbingos
			Spot blank = *spot;
			blank.useBlank = true;
			scoreSpot(&blank);
			if (blank.canMakeAnyWord) spots->push_back(blank);

			scoreSpot(spot);
			if (spot->canMakeAnyWord) spots->push_back(*spot);
		}
	} else if (spot->maxPlayed == 7 && spot->minPlayed < 7) {
		if (spot->numTilesThrough <= 1) {
			// bingos (without blank)
			Spot bingos = *spot;
			bingos.minPlayed = 7;
			scoreSpot(&bingos);
			if (bingos.canMakeAnyWord) spots->push_back(bingos);
		
			// nonbingos (without blank)
			spot->maxPlayed = 6;
		}
		scoreSpot(spot);
		if (spot->canMakeAnyWord) spots->push_back(*spot);
	} else {
		// either all bingos or all nonbingos (doesn't matter which,
		// no need to split)
		scoreSpot(spot);
		if (spot->canMakeAnyWord) spots->push_back(*spot);
	}
}

void V2Generator::findEmptyBoardSpots(vector<Spot>* spots) {
	const V2Gaddag& gaddag = *(QUACKLE_LEXICON_PARAMETERS->v2Gaddag());
	const int numTiles = rack().size();
	Spot spot;
	spot.anchorRow = QUACKLE_BOARD_PARAMETERS->startRow();
	spot.anchorCol = QUACKLE_BOARD_PARAMETERS->startColumn();
	spot.anchorNode = gaddag.root();
	spot.useBlank = false;
	spot.horizontal = true;
	spot.throughScore = 0;
	spot.numTilesThrough = 0;
	spot.maxTilesBehind = numTiles - 1;
	spot.minTilesAhead = 1;
	spot.maxTilesAhead = numTiles;
	spot.longestViable = numTiles;
	spot.hindmostViable = spot.maxTilesBehind;
	spot.minPlayed = 2;
	spot.maxPlayed = numTiles;
  addScoredSpots(&spot, spots);
}

void V2Generator::computeHooks() {
	for (int row = 0; row < 15; row++) {
		for (int col = 0; col < 15; col++) {
			if (isEmpty(row, col)) {
				updateVerticalHooks(row, col);
				updateHorizontalHooks(row, col);
			}
		}
	}
}

void V2Generator::setUpCounts(const LetterString &letters) {
  String::counts(letters, m_counts);
	m_numBlanks = m_counts[QUACKLE_BLANK_MARK];
}

UVString V2Generator::counts2string() const {
	UVString ret;

	for (Letter i = 0; i <= QUACKLE_ALPHABET_PARAMETERS->lastLetter(); i++)
		for (int j = 0; j < m_counts[i]; j++)
			ret += QUACKLE_ALPHABET_PARAMETERS->userVisible(i);

	return ret;
}
