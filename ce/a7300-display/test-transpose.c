#include <stdio.h>
#include <stdlib.h>

#include "transpose.h"

#define STRIDE 480u
#define MAXIMUM_ROWS 320u

static uint16_t landscape[STRIDE * MAXIMUM_ROWS];
static uint16_t portrait[STRIDE * MAXIMUM_ROWS];

static unsigned int check_rectangle(uint32_t top, uint32_t left, uint32_t rows, uint32_t columns)
{
    unsigned int failures = 0;

    transpose_rectangle(landscape + top * STRIDE + left, STRIDE, rows, columns, portrait);
    for (uint32_t panel_row = 0; panel_row < columns; panel_row++)
        for (uint32_t column = 0; column < rows; column++)
            if (portrait[panel_row * rows + column] != landscape[(top + column) * STRIDE + left + panel_row])
                failures++;
    return failures;
}

int main(void)
{
    static const uint32_t row_counts[] = { 1, 2, 3, 17, 160, 319, 320 };
    unsigned int failures = 0;

    for (uint32_t index = 0; index < STRIDE * MAXIMUM_ROWS; index++)
        landscape[index] = (uint16_t)rand();
    for (unsigned int test = 0; test < sizeof row_counts / sizeof row_counts[0]; test++) {
        uint32_t rows = row_counts[test];
        for (uint32_t first = 0; first + rows <= MAXIMUM_ROWS; first += 97)
            failures += check_rectangle(first, 0, rows, STRIDE);
    }
    for (unsigned int test = 0; test < 2000; test++) {
        uint32_t top = rand() % MAXIMUM_ROWS;
        uint32_t left = rand() % STRIDE;
        uint32_t rows = 1 + rand() % (MAXIMUM_ROWS - top);
        uint32_t columns = 1 + rand() % (STRIDE - left);

        failures += check_rectangle(top, left, rows, columns);
    }
    printf("%s: %u mismatches\n", failures ? "FAIL" : "PASS", failures);
    return failures != 0;
}
