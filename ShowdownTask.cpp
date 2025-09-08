#include "ShowdownTask.h"
#include <cstring>

using std::vector;
using std::shared_ptr;

ShowdownTask::ShowdownTask(shared_ptr<RangeManager> rangeManager,
                           vector<float>* result,
                           TerminalNode* node,
                           int hero, int villain,
                           vector<float> villainReachProbs,
                           uint8_t board[5])
{
    this->rangeManager = std::move(rangeManager);
    this->result = result;
    this->node = node;
    this->hero = hero;
    this->villain = villain;
    this->villainReachProbs = std::move(villainReachProbs);
    for (int i = 0; i < 5; ++i) this->board[i] = board[i];
}

void ShowdownTask::run() {
    vector<Hand>& heroHands    = rangeManager->get_hands(hero, board);
    vector<Hand>& villainHands = rangeManager->get_hands(villain, board);

    const int numHeroHands    = static_cast<int>(heroHands.size());
    const int numVillainHands = static_cast<int>(villainHands.size());

    vector<float> utilities(numHeroHands, 0.0f);
    const float value = node->value;

    // Sweep-based computation, matching CFR showdown logic
    float sum = 0.0f;
    float cardSum[52];
    std::memset(cardSum, 0, sizeof(cardSum));

    // Initialize with negatives so we can add as we sweep upward
    for (int i = 0; i < numVillainHands; ++i) {
        cardSum[villainHands[i].card1] -= villainReachProbs[i];
        cardSum[villainHands[i].card2] -= villainReachProbs[i];
        sum -= villainReachProbs[i];
    }

    int i = 0;
    int j = 0;
    while (i < numHeroHands) {
        // Add all villain hands strictly worse than current hero rank
        while (j < numVillainHands && heroHands[i].rank > villainHands[j].rank) {
            cardSum[villainHands[j].card1] += villainReachProbs[j];
            cardSum[villainHands[j].card2] += villainReachProbs[j];
            sum += villainReachProbs[j];
            ++j;
        }
        int k = j;

        // Include ties at this rank
        while (j < numVillainHands && heroHands[i].rank == villainHands[j].rank) {
            cardSum[villainHands[j].card1] += villainReachProbs[j];
            cardSum[villainHands[j].card2] += villainReachProbs[j];
            sum += villainReachProbs[j];
            ++j;
        }

        // Compute utilities for this hero rank block
        utilities[i] = (sum
            - cardSum[heroHands[i].card1]
            - cardSum[heroHands[i].card2]) * value;

        int m = i + 1;
        for (; m < numHeroHands && heroHands[m].rank == heroHands[i].rank; ++m) {
            utilities[m] = (sum
                - cardSum[heroHands[m].card1]
                - cardSum[heroHands[m].card2]) * value;
        }

        // Finalize (advance) the window
        while (k < j) {
            cardSum[villainHands[k].card1] += villainReachProbs[k];
            cardSum[villainHands[k].card2] += villainReachProbs[k];
            sum += villainReachProbs[k];
            ++k;
        }

        i = m;
    }

    *result = std::move(utilities);
}
