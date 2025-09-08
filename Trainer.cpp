#include "Trainer.h"
#include <iostream>
#include "card_utility.h"
#include "ChanceNodeTypeEnum.h"
#include "TerminalNodeTypeEnum.h"
#include "CfrTask.h"
#include <chrono>
#include <cstring>
#include <tbb/task_group.h>

using std::cout;
using chronoClock = std::chrono::system_clock;
using sec = std::chrono::duration<double>;
using std::make_unique;
using std::vector;

Trainer::Trainer(std::shared_ptr<RangeManager> rangeManager, uint8_t initialBoard[5], int initialPot, int inPositionPlayer)
{
    this->rangeManager = rangeManager;
    for (int i = 0; i < 5; i++) this->initialBoard[i] = initialBoard[i];
    this->initialPot = initialPot;
    this->inPositionPlayer = inPositionPlayer;
}

void Trainer::train(Node* root, int numIterations)
{
    br = make_unique<BestResponse>(rangeManager, root, initialBoard, initialPot, inPositionPlayer);
    br->print_exploitability();
    cout << '\n';

    const auto before = chronoClock::now();

    for (int i = 1; i <= numIterations; i++) {
        cfr(1, 2, root, i);
        cfr(2, 1, root, i);

        if (i % 25 == 0) {
            br->print_exploitability();
            const sec duration = chronoClock::now() - before;
            cout << i << " cfr iterations took: " << duration.count() << "s\n\n";
        }
    }
}

vector<float> Trainer::cfr(int hero, int villain, Node* root, int iterationCount)
{
    vector<float> villainReachProbs = rangeManager->get_initial_reach_probs(villain);

    vector<float> result;
    tbb::task_group tg;
    CfrTask task(rangeManager, &result, root, hero, villain, &villainReachProbs, initialBoard, iterationCount);
    tg.run([&]{ task.run(); });
    tg.wait();

    return result;
}
