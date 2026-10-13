//
// Created by Saurav on 24-06-2025.
//

// Not called math.h, which would shadow the C library header.
#ifndef RUBIKS_CUBE_SOLVER_CUBE_MATH_H
#define RUBIKS_CUBE_SOLVER_CUBE_MATH_H


#include <cstdint>

uint32_t factorial(uint32_t n);
uint32_t pick(uint32_t n, uint32_t k);
uint32_t choose(uint32_t n, uint32_t k);

#endif //RUBIKS_CUBE_SOLVER_CUBE_MATH_H