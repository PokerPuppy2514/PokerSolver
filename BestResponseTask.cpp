#include "BestResponseTask.h"
#include "card_utility.h"

#include <typeinfo>
#include <cstring>
#include <limits>
#include <vector>
#include <memory>
#include <utility>

#include "ChanceNodeTypeEnum.h"
#include "Hand.h"
#include "TerminalNodeTypeEnum.h"
#include "NodeTypeEnum.h"

#include <tbb/task_group.h>

using std::vector;
using std::unique_ptr;
using std::shared_ptr;
using std::pair;
using std::memset;

BestResponseTask::BestResponseTask(shared_ptr<RangeManager> rangeManager,
                                   vector<float>* result,
                                   Node* node,
                                   int hero,
                                   int villain,
                                   vector<float>* villainReachProbs,
                                   uint8_t board[5])
{
    this->rangeManager = rangeManager;
    this->result = result;
    this->node = node;
    this->hero = hero;
    this->villain = villain;
    this->villainReachProbs = villainReachProbs;
    for (int i = 0; i < 5; i++)
        this->board[i] = board[i];
}

void BestResponseTask::run() {
    // Terminal node
    if (typeid(*node) == typeid(TerminalNode)) {
        *result = terminal_node_best_response(static_cast<TerminalNode*>(node),
                                              hero, villain, *villainReachProbs, board);
        return;
    }

    // Chance node
    if (typeid(*node) == typeid(ChanceNode)) {
        *result = chance_node_best_response(static_cast<ChanceNode*>(node),
                                            hero, villain, *villainReachProbs, board);
        return;
    }

    // Action node
    ActionNode* actionNode = static_cast<ActionNode*>(node);
    const int numHeroHands    = rangeManager->get_num_hands(hero,    board);
    const int numVillainHands = rangeManager->get_num_hands(villain, board);
    const int numActions      = actionNode->numActions;

    if (hero == actionNode->player) {
        // Hero to act: take element-wise max across child EVs
        *result = vector<float>(numHeroHands, -std::numeric_limits<float>::max());
        vector<vector<float>> results(numActions);

        tbb::task_group tg;
        for (int action = 0; action < numActions; ++action) {
            Node* childNode = actionNode->get_child(action);
            tg.run([this, childNode, action, &results] {
                BestResponseTask sub(rangeManager, &results[action],
                                     childNode, hero, villain, villainReachProbs, board);
                sub.run();
            });
        }
        tg.wait();

        vector<float>& maxSubgameEvs = *result;
        for (int action = 0; action < numActions; ++action) {
            vector<float>& subgameEvs = results[action];
            for (int hand = 0; hand < numHeroHands; ++hand) {
                if (subgameEvs[hand] > maxSubgameEvs[hand]) {
                    maxSubgameEvs[hand] = subgameEvs[hand];
                }
            }
        }
    } else {
        // Villain to act: expectation over villain strategy
        vector<float> avgStrategy = actionNode->get_average_strategy();

        *result = vector<float>(numHeroHands, 0.0f);
        vector<vector<float>> results(numActions);
        vector<vector<float>> newVillainReachProbss(numActions);
        for (int a = 0; a < numActions; ++a) {
            newVillainReachProbss[a].resize(numVillainHands);
        }

        // Per-action villain reach probs
        for (int action = 0; action < numActions; ++action) {
            vector<float>& newVRP = newVillainReachProbss[action];
            int index = action * numVillainHands;
            for (int hand = 0; hand < numVillainHands; ++hand) {
                newVRP[hand] = avgStrategy[index++] * (*villainReachProbs)[hand];
            }
        }

        tbb::task_group tg;
        for (int action = 0; action < numActions; ++action) {
            Node* childNode = actionNode->get_child(action);
            tg.run([this, childNode, action, &results, &newVillainReachProbss] {
                BestResponseTask sub(rangeManager, &results[action],
                                     childNode, hero, villain,
                                     &newVillainReachProbss[action], board);
                sub.run();
            });
        }
        tg.wait();

        vector<float>& cumSubgameEvs = *result;
        for (int action = 0; action < numActions; ++action) {
            vector<float>& subgameEvs = results[action];
            for (int hand = 0; hand < numHeroHands; ++hand) {
                cumSubgameEvs[hand] += subgameEvs[hand];
            }
        }
    }
}

vector<float> BestResponseTask::chance_node_best_response(
    ChanceNode* node, int hero, int villain,
    vector<float>& villainReachProbs, uint8_t board[5])
{
    vector<Hand>& heroHands = rangeManager->get_hands(hero, board);
    vector<unique_ptr<ChanceNodeChild>>& children = node->get_children();
    const int childCount = static_cast<int>(children.size());

    vector<vector<float>> results(childCount);
    vector<vector<float>> newVillainReachProbss(childCount);

    // Precompute per-child villain reach probs
    for (int i = 0; i < childCount; ++i) {
        uint8_t nb[5];
        for (int j = 0; j < 5; ++j) nb[j] = board[j];

        const uint8_t card = children[i]->card;
        if (board[3] == 52) nb[3] = card; else nb[4] = card;

        newVillainReachProbss[i] = rangeManager->get_reach_probs(villain, nb, villainReachProbs);
    }

    // Spawn children in parallel
    tbb::task_group tg;
    for (int i = 0; i < childCount; ++i) {
        Node* child = children[i]->node.get();
        tg.run([this, i, child, &results, &newVillainReachProbss, &children] {
            uint8_t nb[5];
            for (int j = 0; j < 5; ++j) nb[j] = this->board[j];

            const uint8_t card = children[i]->card;
            if (this->board[3] == 52) nb[3] = card; else nb[4] = card;

            BestResponseTask sub(this->rangeManager, &results[i],
                                 child, this->hero, this->villain,
                                 &newVillainReachProbss[i], nb);
            sub.run();
        });
    }
    tg.wait();

    // Combine
    vector<float> utilities(heroHands.size(), 0.0f);

    uint8_t nb[5];
    for (int j = 0; j < 5; ++j) nb[j] = board[j];

    for (int i = 0; i < childCount; ++i) {
        const uint8_t card = children[i]->card;
        if (board[3] == 52) nb[3] = card; else nb[4] = card;

        vector<float>& subgameUtilities = results[i];
        vector<int>& reachProbsMapping  = rangeManager->get_reach_probs_mapping(hero, nb);

        for (int k = 0; k < static_cast<int>(subgameUtilities.size()); ++k)
            utilities[reachProbsMapping[k]] += subgameUtilities[k];
    }

    const int weight = (board[3] == 52) ? 45 : 44;
    for (int h = 0; h < static_cast<int>(utilities.size()); ++h) utilities[h] /= weight;

    return utilities;
}

vector<float> BestResponseTask::terminal_node_best_response(TerminalNode* node,
                                                            int hero, int villain,
                                                            vector<float>& villainReachProbs,
                                                            uint8_t board[5])
{
    if (node->type == TerminalNodeType::ALLIN)
        return allin_best_response(node, hero, villain, villainReachProbs, board);
    else if (node->type == TerminalNodeType::UNCONTESTED)
        return uncontested_best_response(node, hero, villain, villainReachProbs, board);
    else
        return showdown_best_response(node, hero, villain, villainReachProbs, board);
}

vector<float> BestResponseTask::showdown_best_response(TerminalNode* node,
                                                       int hero, int villain,
                                                       vector<float>& villainReachProbs,
                                                       uint8_t board[5])
{
    vector<Hand>& heroHands    = rangeManager->get_hands(hero, board);
    vector<Hand>& villainHands = rangeManager->get_hands(villain, board);

    int numHeroHands    = static_cast<int>(heroHands.size());
    int numVillainHands = static_cast<int>(villainHands.size());

    vector<float> evs(numHeroHands, 0);

    float value = node->value;

    float winSum = 0;
    float cardWinSum[52];
    memset(cardWinSum, 0, sizeof(cardWinSum));

    int k;

    int j = 0;
    for (int i = 0; i < numHeroHands;) {
        while (heroHands[i].rank > villainHands[j].rank) {
            winSum += villainReachProbs[j];

            cardWinSum[villainHands[j].card1] += villainReachProbs[j];
            cardWinSum[villainHands[j].card2] += villainReachProbs[j];

            j++;
        }

        for (k = i; (k < numHeroHands) && (heroHands[k].rank == heroHands[i].rank); k++)
            evs[k] = (winSum
                - cardWinSum[heroHands[k].card1]
                - cardWinSum[heroHands[k].card2]) * value;
        i = k;
    }

    float loseSum = 0;
    float cardLoseSum[52];
    memset(cardLoseSum, 0, sizeof(cardLoseSum));

    j = numVillainHands - 1;
    for (int i = numHeroHands - 1; i >= 0;) {
        while (heroHands[i].rank < villainHands[j].rank) {
            loseSum += villainReachProbs[j];

            cardLoseSum[villainHands[j].card1] += villainReachProbs[j];
            cardLoseSum[villainHands[j].card2] += villainReachProbs[j];

            j--;
        }

        for (k = i; (k >= 0) && (heroHands[k].rank == heroHands[i].rank); k--)
            evs[k] -= (loseSum
                - cardLoseSum[heroHands[k].card1]
                - cardLoseSum[heroHands[k].card2]) * value;
        i = k;
    }

    return evs;
}

vector<float> BestResponseTask::allin_best_response(
    TerminalNode* node, int hero, int villain,
    vector<float>& villainReachProbs, uint8_t board[5])
{
    vector<Hand>& heroHands    = rangeManager->get_hands(hero, board);
    (void)heroHands; // silence unused warning in some paths

    vector<float> evs(heroHands.size(), 0.0f);

    uint8_t nb[5];
    for (int j = 0; j < 5; ++j) nb[j] = board[j];

    if (board_has_turn(board)) {
        // ---- river only (44 rivers) ----
        vector<uint8_t> rivers;
        rivers.reserve(44);
        for (uint8_t r = 0; r < 52; ++r)
            if (!overlap(r, board)) rivers.push_back(r);

        vector<vector<float>> results(rivers.size());

        tbb::task_group tg;
        for (size_t idx = 0; idx < rivers.size(); ++idx) {
            tg.run([this, idx, &results, node, &rivers] {
                uint8_t nb_local[5];
                for (int j = 0; j < 5; ++j) nb_local[j] = this->board[j];
                nb_local[4] = rivers[idx];

                vector<float> vrp = this->rangeManager->get_reach_probs(
                    this->villain, nb_local, *this->villainReachProbs);

                results[idx] = this->showdown_best_response(node, this->hero, this->villain,
                                                            vrp, nb_local);
            });
        }
        tg.wait();

        for (size_t idx = 0; idx < rivers.size(); ++idx) {
            nb[4] = rivers[idx];
            vector<int>& rpm = rangeManager->get_reach_probs_mapping(hero, nb);
            vector<float>& sub = results[idx];
            for (int k = 0; k < static_cast<int>(sub.size()); ++k)
                evs[rpm[k]] += sub[k];
        }
        for (int i = 0; i < static_cast<int>(evs.size()); ++i) evs[i] /= 44.0f;
    } else {
        // ---- turn + river pairs (49*48/2) ----
        vector<std::pair<uint8_t,uint8_t>> pairs;
        pairs.reserve(49 * 48 / 2);
        for (uint8_t t = 0; t < 52; ++t) {
            if (overlap(t, board)) continue;
            for (uint8_t r = t + 1; r < 52; ++r) {
                if (overlap(r, board)) continue;
                pairs.emplace_back(t, r);
            }
        }

        vector<vector<float>> results(pairs.size());

        tbb::task_group tg;
        for (size_t idx = 0; idx < pairs.size(); ++idx) {
            tg.run([this, idx, &results, node, &pairs] {
                const auto [t, r] = pairs[idx];

                uint8_t nb_local[5];
                for (int j = 0; j < 5; ++j) nb_local[j] = this->board[j];

                nb_local[3] = t;
                vector<float> vrp_turn = this->rangeManager->get_reach_probs(
                    this->villain, nb_local, *this->villainReachProbs);

                nb_local[4] = r;
                vector<float> vrp_river = this->rangeManager->get_reach_probs(
                    this->villain, nb_local, vrp_turn);

                results[idx] = this->showdown_best_response(node, this->hero, this->villain,
                                                            vrp_river, nb_local);
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
                vector<float>& sub     = results[idx++];

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

vector<float> BestResponseTask::uncontested_best_response(TerminalNode* node,
                                                          int hero, int villain,
                                                          vector<float>& villainReachProbs,
                                                          uint8_t board[5])
{
    vector<Hand>& heroHands    = rangeManager->get_hands(hero, board);
    vector<Hand>& villainHands = rangeManager->get_hands(villain, board);

    int numHeroHands    = static_cast<int>(heroHands.size());
    int numVillainHands = static_cast<int>(villainHands.size());

    float villainSum = 0;
    float villainCardSum[52];
    memset(villainCardSum, 0, sizeof(villainCardSum));

    for (int i = 0; i < numVillainHands; i++) {
        villainSum += villainReachProbs[i];
        villainCardSum[villainHands[i].card1] += villainReachProbs[i];
        villainCardSum[villainHands[i].card2] += villainReachProbs[i];
    }

    float value = (hero == node->lastToAct) ? -node->value : node->value;

    vector<float> evs(numHeroHands);

    for (int i = 0; i < numHeroHands; i++) {
        evs[i] = (villainSum
            - villainCardSum[heroHands[i].card1]
            - villainCardSum[heroHands[i].card2]
            + villainReachProbs[i]) * value;
    }

    return evs;
}
