#pragma once
#include <cstdint>
#include <memory>
#include <vector>

#include "RangeManager.h"
#include "Node.h"
#include "ActionNode.h"
#include "ChanceNode.h"
#include "TerminalNode.h"

class CfrTask {
public:
    CfrTask(std::shared_ptr<RangeManager> rangeManager,
            std::vector<float>* result,
            Node* node,
            int hero, int villain,
            std::vector<float>* villainReachProbs,
            uint8_t board[5],
            int iterationCount);

    // run the computation (replaces old task::execute)
    void run();

private:
    std::shared_ptr<RangeManager> rangeManager;
    std::vector<float>* result;
    Node* node;
    int hero{0};
    int villain{0};
    std::vector<float>* villainReachProbs;
    uint8_t board[5]{};
    int iterationCount{0};

    // helpers
    std::vector<float> chance_node_utility(ChanceNode* node, int hero, int villain,
                                           std::vector<float>& villainReachProbs, uint8_t board[5], int iterationCount);
    std::vector<float> terminal_node_utility(TerminalNode* node, int hero, int villain,
                                             std::vector<float>& villainReachProbs, uint8_t board[5], int iterationCount);
    std::vector<float> allin_utility(TerminalNode* node, int hero, int villain,
                                     std::vector<float>& villainReachProbs, uint8_t board[5], int iterationCount);
    std::vector<float> showdown_utility(TerminalNode* node, const int hero, const int villain,
                                        const std::vector<float>& villainReachProbs, uint8_t board[5], const int iterationCount);
    std::vector<float> uncontested_utility(TerminalNode* node, int hero, int villain,
                                           std::vector<float>& villainReachProbs, uint8_t board[5], int iterationCount);
};
