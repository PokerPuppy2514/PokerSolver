#pragma once
#include <cstdint>
#include <memory>
#include <vector>

#include "RangeManager.h"
#include "Node.h"
#include "ActionNode.h"
#include "ChanceNode.h"
#include "TerminalNode.h"

class BestResponseTask {
public:
    BestResponseTask(std::shared_ptr<RangeManager> rangeManager,
                     std::vector<float>* result,
                     Node* node,
                     int hero,
                     int villain,
                     std::vector<float>* villainReachProbs,
                     uint8_t board[5]);

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

    // helpers
    std::vector<float> terminal_node_best_response(TerminalNode* node, int hero, int villain,
                                                   std::vector<float>& villainReachProbs, uint8_t board[5]);
    std::vector<float> chance_node_best_response(ChanceNode* node, int hero, int villain,
                                                 std::vector<float>& villainReachProbs, uint8_t board[5]);
    std::vector<float> showdown_best_response(TerminalNode* node, int hero, int villain,
                                              std::vector<float>& villainReachProbs, uint8_t board[5]);
    std::vector<float> allin_best_response(TerminalNode* node, int hero, int villain,
                                           std::vector<float>& villainReachProbs, uint8_t board[5]);
    std::vector<float> uncontested_best_response(TerminalNode* node, int hero, int villain,
                                                 std::vector<float>& villainReachProbs, uint8_t board[5]);
};
