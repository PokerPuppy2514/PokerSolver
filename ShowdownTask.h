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
                 std::vector<float> villainReachProbs, // moved in
                 uint8_t board[5]);

    // Replaces the old TBB task::execute(); runs synchronously.
    void run();

private:
    std::shared_ptr<RangeManager> rangeManager;
    std::vector<float>* result;
    TerminalNode* node;
    int hero;
    int villain;
    std::vector<float> villainReachProbs; // owned copy for this task
    uint8_t board[5];
};
