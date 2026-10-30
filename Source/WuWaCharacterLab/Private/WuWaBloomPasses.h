// Generated source Bloom graph; slots identify current RDG resources.
#pragma once
struct FWuWaBloomPassDefinition { uint8 Kernel, Output, Divisor; int8 Inputs[4]; bool bAdditive; };
static constexpr int32 WuWaBloomKernelCount = 10;
static constexpr int32 WuWaBloomResourceCount = 14;
static constexpr FWuWaBloomPassDefinition WuWaBloomPasses[] = {
    {0, 4, 4, {0, -1, -1, -1}, false},
    {1, 5, 4, {4, 1, 2, 3}, false},
    {2, 4, 4, {5, -1, -1, -1}, false},
    {2, 5, 4, {4, -1, -1, -1}, false},
    {5, 6, 8, {5, -1, -1, -1}, false},
    {5, 7, 16, {6, -1, -1, -1}, false},
    {0, 8, 32, {7, -1, -1, -1}, false},
    {2, 9, 8, {6, -1, -1, -1}, false},
    {2, 6, 8, {9, -1, -1, -1}, false},
    {4, 10, 16, {7, -1, -1, -1}, false},
    {4, 7, 16, {10, -1, -1, -1}, false},
    {7, 11, 32, {8, -1, -1, -1}, false},
    {7, 8, 32, {11, -1, -1, -1}, false},
    {3, 4, 4, {5, 6, 7, 8}, false},
    {8, 5, 4, {1, 4, -1, -1}, false},
    {6, 12, 4, {5, -1, -1, -1}, false},
    {6, 13, 4, {12, -1, -1, -1}, false},
    {6, 12, 4, {13, -1, -1, -1}, false},
    {6, 13, 4, {12, -1, -1, -1}, false},
    {6, 12, 4, {13, -1, -1, -1}, false},
    {6, 13, 4, {12, -1, -1, -1}, false},
    {6, 12, 4, {13, -1, -1, -1}, false},
    {6, 13, 4, {12, -1, -1, -1}, false},
    {9, 4, 4, {13, -1, -1, -1}, true},
};
static constexpr int32 WuWaBloomOutputSlot = 4;
