#include "ShowdownTask.h"
#include <cstring>

void ShowdownTask::run() {
    std::vector<Hand>& heroHands    = rangeManager->get_hands(hero, board);
    std::vector<Hand>& villainHands = rangeManager->get_hands(villain, board);

    const int numHeroHands    = (int)heroHands.size();
    const int numVillainHands = (int)villainHands.size();

    std::vector<float> utilities(numHeroHands);

    const float value = node->value;

    float sum = 0.0f;
    float cardSum[52];
    std::memset(cardSum, 0, sizeof(cardSum));

    for (int i = 0; i < numVillainHands; ++i) {
        cardSum[villainHands[i].card1] -= villainReachProbs[i];
        cardSum[villainHands[i].card2] -= villainReachProbs[i];
        sum -= villainReachProbs[i];
    }

    int j = 0;
    int i = 0;
    while (i < numHeroHands) {
        while (j < numVillainHands && heroHands[i].rank > villainHands[j].rank) {
            cardSum[villainHands[j].card1] += villainReachProbs[j];
            cardSum[villainHands[j].card2] += villainReachProbs[j];
            sum += villainReachProbs[j];
            ++j;
        }
        int k = j;
        while (j < numVillainHands && heroHands[i].rank == villainHands[j].rank) {
            cardSum[villainHands[j].card1] += villainReachProbs[j];
            cardSum[villainHands[j].card2] += villainReachProbs[j];
            sum += villainReachProbs[j];
            ++j;
        }

        int m = i;
        do {
            utilities[m] = (sum
                - cardSum[heroHands[m].card1]
                - cardSum[heroHands[m].card2]) * value;
            ++m;
        } while (m < numHeroHands && heroHands[m].rank == heroHands[i].rank);

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
