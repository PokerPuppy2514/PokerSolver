#include "CfrTask.h"
#include "card_utility.h"
#include "NodeTypeEnum.h"
#include <tbb/task_group.h>
#include <cstring>

using std::vector;
using std::shared_ptr;

CfrTask::CfrTask(shared_ptr<RangeManager> rangeManager, vector<float>* result, Node* node,
                 int hero, int villain, vector<float>* villainReachProbs,
                 uint8_t board[5], int iterationCount)
{
    this->rangeManager = rangeManager;
    this->result = result;
    this->node = node;
    this->hero = hero;
    this->villain = villain;
    this->villainReachProbs = villainReachProbs;
    for (int i = 0; i < 5; i++) this->board[i] = board[i];
    this->iterationCount = iterationCount;
}

void CfrTask::run()
{
    if (typeid(*node) == typeid(TerminalNode)) {
        *result = terminal_node_utility(static_cast<TerminalNode*>(node),
                                        hero, villain, *villainReachProbs, board, iterationCount);
        return;
    }

    if (typeid(*node) == typeid(ChanceNode)) {
        *result = chance_node_utility(static_cast<ChanceNode*>(node),
                                      hero, villain, *villainReachProbs, board, iterationCount);
        return;
    }

    ActionNode* actionNode = static_cast<ActionNode*>(node);

    const int numHeroHands    = rangeManager->get_num_hands(hero,    board);
    const int numVillainHands = rangeManager->get_num_hands(villain, board);
    const int numActions      = actionNode->numActions;

    if (hero == actionNode->player) {
        *result = vector<float>(numHeroHands, 0.0f);
        vector<vector<float>> results(numActions);

        vector<float> strategy = actionNode->get_current_strategy();

        tbb::task_group tg;
        for (int action = 0; action < numActions; ++action) {
            Node* child = actionNode->get_child(action);
            tg.run([=, this, &results] {
                CfrTask sub(rangeManager, &results[action], child,
                            hero, villain, villainReachProbs, board, iterationCount);
                sub.run();
            });
        }
        tg.wait();

        vector<float>& utilities = *result;

        // update regrets (part one) and compute utility under current strategy
        for (int action = 0; action < numActions; ++action) {
            vector<float>& au = results[action];
            actionNode->update_regretSum_part_one(au, action);

            int idx = action * numHeroHands;
            for (int h = 0; h < numHeroHands; ++h) {
                utilities[h] += strategy[idx++] * au[h];
            }
        }

        actionNode->update_regretSum_part_two(*result, iterationCount);
    } else {
        *result = vector<float>(numHeroHands, 0.0f);
        vector<vector<float>> results(numActions);
        vector<vector<float>> newVRPs(numActions);

        vector<float> strategy = actionNode->get_current_strategy();

        // build per-action villain reach probs
        for (int action = 0; action < numActions; ++action) {
            newVRPs[action].resize(numVillainHands);
            vector<float>& nv = newVRPs[action];
            int idx = action * numVillainHands;
            for (int h = 0; h < numVillainHands; ++h) {
                nv[h] = strategy[idx++] * (*villainReachProbs)[h];
            }
        }

        tbb::task_group tg;
        for (int action = 0; action < numActions; ++action) {
            Node* child = actionNode->get_child(action);
            tg.run([=, this, &results, &newVRPs] {
                CfrTask sub(rangeManager, &results[action], child,
                            hero, villain, &newVRPs[action], board, iterationCount);
                sub.run();
            });
        }
        tg.wait();

        vector<float>& utilities = *result;
        for (int action = 0; action < numActions; ++action) {
            vector<float>& su = results[action];
            for (int h = 0; h < numHeroHands; ++h) {
                utilities[h] += su[h];
            }
        }

        actionNode->update_strategySum(strategy, *villainReachProbs, iterationCount);
    }
}

vector<float> CfrTask::chance_node_utility(ChanceNode* node, int hero, int villain,
                                           vector<float>& villainReachProbs, uint8_t board[5], int /*iterationCount*/)
{
    vector<Hand>& heroHands    = rangeManager->get_hands(hero, board);
    vector<unique_ptr<ChanceNodeChild>>& children = node->get_children();
    const int childCount = static_cast<int>(children.size());

    vector<vector<float>> results(childCount);
    vector<vector<float>> newVRPs(childCount);

    // precompute per-child villain reach probs
    for (int i = 0; i < childCount; ++i) {
        uint8_t nb[5];
        for (int j = 0; j < 5; ++j) nb[j] = board[j];

        const uint8_t card = children[i]->card;
        if (board[3] == 52) nb[3] = card; else nb[4] = card;

        newVRPs[i] = rangeManager->get_reach_probs(villain, nb, villainReachProbs);
    }

    tbb::task_group tg;
    for (int i = 0; i < childCount; ++i) {
        Node* child = children[i]->node.get();
        tg.run([=, this, &results, &newVRPs, &children] {
            uint8_t nb[5];
            for (int j = 0; j < 5; ++j) nb[j] = board[j];

            const uint8_t card = children[i]->card;
            if (board[3] == 52) nb[3] = card; else nb[4] = card;

            CfrTask sub(rangeManager, &results[i], child,
                        hero, villain, &newVRPs[i], nb, iterationCount);
            sub.run();
        });
    }
    tg.wait();

    vector<float> utilities(heroHands.size(), 0.0f);

    uint8_t nb[5];
    for (int j = 0; j < 5; ++j) nb[j] = board[j];

    for (int i = 0; i < childCount; ++i) {
        const uint8_t card = children[i]->card;
        if (board[3] == 52) nb[3] = card; else nb[4] = card;

        vector<float>& su = results[i];
        vector<int>& rpm  = rangeManager->get_reach_probs_mapping(hero, nb);

        for (int k = 0; k < static_cast<int>(su.size()); ++k)
            utilities[rpm[k]] += su[k];
    }

    const int weight = (board[3] == 52) ? 45 : 44;
    for (int h = 0; h < static_cast<int>(utilities.size()); ++h) utilities[h] /= weight;

    return utilities;
}

vector<float> CfrTask::terminal_node_utility(TerminalNode* node, int hero, int villain,
                                             vector<float>& villainReachProbs, uint8_t board[5], int iterationCount)
{
    if (node->type == TerminalNodeType::ALLIN)
        return allin_utility(node, hero, villain, villainReachProbs, board, iterationCount);
    else if (node->type == TerminalNodeType::UNCONTESTED)
        return uncontested_utility(node, hero, villain, villainReachProbs, board, iterationCount);
    else
        return showdown_utility(node, hero, villain, villainReachProbs, board, iterationCount);
}

vector<float> CfrTask::allin_utility(TerminalNode* node, int hero, int villain,
                                     vector<float>& villainReachProbs, uint8_t board[5], int /*iterationCount*/)
{
    vector<Hand>& heroHands = rangeManager->get_hands(hero, board);
    vector<float> evs(heroHands.size(), 0.0f);

    uint8_t nb[5];
    for (int j = 0; j < 5; ++j) nb[j] = board[j];

    if (nb[3] != 52) {
        // rivers only
        vector<vector<float>> results;
        results.reserve(44);

        tbb::task_group tg;
        vector<uint8_t> rivers;
        for (uint8_t r = 0; r < 52; ++r) if (!overlap(r, board)) rivers.push_back(r);
        results.resize(rivers.size());

        for (size_t i = 0; i < rivers.size(); ++i) {
            tg.run([=, this, &results, &rivers] {
                uint8_t local[5];
                for (int j = 0; j < 5; ++j) local[j] = nb[j];
                local[4] = rivers[i];

                vector<float> vrp = rangeManager->get_reach_probs(villain, local, villainReachProbs);
                results[i] = showdown_utility(node, hero, villain, vrp, local, 0);
            });
        }
        tg.wait();

        size_t idx = 0;
        for (uint8_t r : rivers) {
            nb[4] = r;
            vector<int>& rpm = rangeManager->get_reach_probs_mapping(hero, nb);
            vector<float>& sub = results[idx++];
            for (int k = 0; k < static_cast<int>(sub.size()); ++k)
                evs[rpm[k]] += sub[k];
        }
        for (int i = 0; i < static_cast<int>(evs.size()); ++i) evs[i] /= 44.0f;
    } else {
        // turn+river pairs
        vector<std::pair<uint8_t,uint8_t>> pairs;
        for (uint8_t t = 0; t < 52; ++t) {
            if (overlap(t, board)) continue;
            for (uint8_t r = t + 1; r < 52; ++r) {
                if (overlap(r, board)) continue;
                pairs.emplace_back(t, r);
            }
        }

        vector<vector<float>> results(pairs.size());
        tbb::task_group tg;

        for (size_t i = 0; i < pairs.size(); ++i) {
            tg.run([=, this, &results, &pairs] {
                uint8_t local[5];
                for (int j = 0; j < 5; ++j) local[j] = nb[j];

                local[3] = pairs[i].first;
                vector<float> vrp_turn = rangeManager->get_reach_probs(villain, local, villainReachProbs);

                local[4] = pairs[i].second;
                vector<float> vrp_river = rangeManager->get_reach_probs(villain, local, vrp_turn);

                results[i] = showdown_utility(node, hero, villain, vrp_river, local, 0);
            });
        }
        tg.wait();

        size_t idx = 0;
        for (uint8_t t = 0; t < 52; ++t) {
            if (overlap(t, board)) continue;

            nb[3] = t;
            vector<int>& rpm_turn = rangeManager->get_reach_probs_mapping(hero, nb);
            vector<float> turnEvs(rpm_turn.size(), 0.0f);

            for (uint8_t r = t + 1; r < 52; ++r) {
                if (overlap(r, board)) continue;

                nb[4] = r;

                vector<int>& rpm_river = rangeManager->get_reach_probs_mapping(hero, nb);
                vector<float>& sub = results[idx++];

                for (int k = 0; k < static_cast<int>(sub.size()); ++k)
                    turnEvs[rpm_river[k]] += sub[k];
            }

            for (int k = 0; k < static_cast<int>(turnEvs.size()); ++k)
                evs[rpm_turn[k]] += 2.0f * turnEvs[k];

            nb[3] = 52;
            nb[4] = 52;
        }

        for (int i = 0; i < static_cast<int>(evs.size()); ++i) evs[i] /= 1980.0f;
    }

    return evs;
}

vector<float> CfrTask::showdown_utility(TerminalNode* node, const int hero, const int villain,
                                        const vector<float>& villainReachProbs, uint8_t board[5], const int /*iterationCount*/)
{
    vector<Hand>& heroHands    = rangeManager->get_hands(hero, board);
    vector<Hand>& villainHands = rangeManager->get_hands(villain, board);

    int numHeroHands    = (int)heroHands.size();
    int numVillainHands = (int)villainHands.size();

    vector<float> utilities(heroHands.size());

    float value = node->value;

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

    return utilities;
}

vector<float> CfrTask::uncontested_utility(TerminalNode* node, int hero, int villain,
                                           vector<float>& villainReachProbs, uint8_t board[5], int /*iterationCount*/)
{
    vector<Hand>& heroHands    = rangeManager->get_hands(hero, board);
    vector<Hand>& villainHands = rangeManager->get_hands(villain, board);

    int numHeroHands    = (int)heroHands.size();
    int numVillainHands = (int)villainHands.size();

    float villainSum = 0.0f;
    float villainCardSum[52];
    std::memset(villainCardSum, 0, sizeof(villainCardSum));

    for (int i = 0; i < numVillainHands; ++i) {
        villainCardSum[villainHands[i].card1] += villainReachProbs[i];
        villainCardSum[villainHands[i].card2] += villainReachProbs[i];
        villainSum += villainReachProbs[i];
    }

    float value = (hero == node->lastToAct) ? -node->value : node->value;

    vector<float> utilities(numHeroHands);
    for (int i = 0; i < numHeroHands; ++i) {
        utilities[i] = (villainSum
            - villainCardSum[heroHands[i].card1]
            - villainCardSum[heroHands[i].card2]
            + villainReachProbs[i]) * value;
    }
    return utilities;
}
