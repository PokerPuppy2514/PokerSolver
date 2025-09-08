#pragma once
#include <cstdint>
#include <memory>
#include <vector>

#include "RangeManager.h"
#include "TerminalNode.h"
#include "Hand.h"

class ShowdownTask {
public:
    ShowdownTask(std::shared_ptr<RangeManager> rangeManager,
                 std::vector<float>* result,
                 TerminalNode* node,
                 int hero, int villain,
                 std::vector<float> villainReachProbs,
                 uint8_t board[5])
        : rangeManager(std::move(rangeManager)),
          result(result),
          node(node),
          hero(hero),
          villain(villain),
          villainReachProbs(std::move(villainReachProbs))
    {
        for (int i = 0; i < 5; ++i) this->board[i] = board[i];
    }

    // simple synchronous worker (replaces old task::execute)
    void run();

private:
    std::shared_ptr<RangeManager> rangeManager;
    std::vector<float>* result;
    TerminalNode* node;
    int hero{0};
    int villain{0};
    std::vector<float> villainReachProbs;
    uint8_t board[5]{};
};
