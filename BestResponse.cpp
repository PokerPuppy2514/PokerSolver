#include "BestResponse.h"
#include "card_utility.h"
#include <iostream>
#include <cstring>
#include <tbb/task_group.h>

#include "BestResponseTask.h"
#include "ChanceNodeTypeEnum.h"
#include "Hand.h"
#include "TerminalNodeTypeEnum.h"

using std::cout;

BestResponse::BestResponse(std::shared_ptr<RangeManager> rangeManager, Node* root,
                           uint8_t initialBoard[5], int initialPot, int inPositionPlayer)
{
    this->rangeManager = rangeManager;
    this->root = root;
    for (int i = 0; i < 5; i++) this->initialBoard[i] = initialBoard[i];
    this->initialPot = initialPot;
    this->inPositionPlayer = inPositionPlayer;
    set_relative_probabilities(initialBoard);
}

float BestResponse::get_best_response_Ev(int hero, int villain)
{
    float totalEv = 0.0f;

    std::vector<Hand>& heroHands    = rangeManager->get_starting_hands(hero);
    std::vector<Hand>& villainHands = rangeManager->get_starting_hands(villain);

    std::vector<float>& relativeProbs = (hero == 1) ? p1RelativeProbs : p2RelativeProbs;
    std::vector<float> villainReachProbs = rangeManager->get_initial_reach_probs(villain);

    std::vector<float> result;
    // modern oneTBB kickoff
    tbb::task_group tg;
    BestResponseTask br(rangeManager, &result, root, hero, villain, &villainReachProbs, initialBoard);
    tg.run([&]{ br.run(); });
    tg.wait();

    std::vector<float>& evs = result;
    for (int i = 0; i < static_cast<int>(heroHands.size()); i++) {
        totalEv += evs[i] / get_unblocked_combo_count(heroHands[i], villainHands) * relativeProbs[i];
    }
    return totalEv;
}

float BestResponse::get_unblocked_combo_count(Hand& heroHand, std::vector<Hand>& villainHands)
{
    float sum = 0.0f;
    for (int i = 0; i < static_cast<int>(villainHands.size()); i++)
        if (!overlap(villainHands[i], heroHand))
            sum += villainHands[i].probability;
    return sum;
}

void BestResponse::set_relative_probabilities(uint8_t initialBoard[5])
{
    for (int player = 1; player <= 2; player++) {
        std::vector<float>& relativeProbs = (player == 1) ? p1RelativeProbs : p2RelativeProbs;
        std::vector<Hand>& heroStartingHands    = rangeManager->get_starting_hands(player);
        std::vector<Hand>& villainStartingHands = rangeManager->get_starting_hands(player ^ 1 ^ 2);
        relativeProbs.resize(heroStartingHands.size());

        float relativeSum = 0.0f;

        for (int i = 0; i < static_cast<int>(heroStartingHands.size()); i++) {
            Hand& heroHand = heroStartingHands[i];
            float villainSum = 0.0f;

            for (int j = 0; j < static_cast<int>(villainStartingHands.size()); j++) {
                Hand& villainHand = villainStartingHands[j];
                if (overlap(heroHand, villainHand)) continue;
                villainSum += villainHand.probability;
            }

            relativeProbs[i] = villainSum * heroHand.probability;
            relativeSum += relativeProbs[i];
        }

        for (int i = 0; i < static_cast<int>(relativeProbs.size()); i++)
            relativeProbs[i] /= relativeSum;
    }
}

void BestResponse::print_exploitability()
{
    float oopEv = 0.0f;
    float ipEv  = 0.0f;

    if (inPositionPlayer == 1) {
        ipEv  = get_best_response_Ev(1, 2);
        oopEv = get_best_response_Ev(2, 1);
    } else {
        ipEv  = get_best_response_Ev(2, 1);
        oopEv = get_best_response_Ev(1, 2);
    }

    float exploitability = (oopEv + ipEv) / 2 / initialPot * 100.0f;

    cout << "OOP BR EV: " << oopEv << "\n";
    cout << "IP BR EV: " << ipEv << "\n";
    cout << "Exploitability: " << exploitability << "%%\n";
}
