#ifndef A7300_DISPLAY_TRANSPOSE_H
#define A7300_DISPLAY_TRANSPOSE_H

#include <stdint.h>

#define TRANSPOSE_TILE_PIXELS 16u

static inline uint32_t transpose_tile_width(uint32_t columns, uint32_t first_column)
{
    return columns - first_column < TRANSPOSE_TILE_PIXELS ? columns - first_column : TRANSPOSE_TILE_PIXELS;
}

static inline void transpose_rectangle_paired(const uint16_t *landscape, uint32_t stride_pixels, uint32_t rows, uint32_t columns, uint32_t *portrait)
{
    uint32_t first_column, row, pixel, tile_width;

    for (first_column = 0; first_column < columns; first_column += TRANSPOSE_TILE_PIXELS) {
        tile_width = transpose_tile_width(columns, first_column);
        for (row = 0; row < rows; row += 2) {
            const uint16_t *upper = landscape + row * stride_pixels + first_column;
            const uint16_t *lower = upper + stride_pixels;

            for (pixel = 0; pixel < tile_width; pixel++)
                portrait[((first_column + pixel) * rows + row) / 2] = upper[pixel] | (uint32_t)lower[pixel] << 16;
        }
    }
}

static inline void transpose_rectangle_single(const uint16_t *landscape, uint32_t stride_pixels, uint32_t rows, uint32_t columns, uint16_t *portrait)
{
    uint32_t first_column, row, pixel, tile_width;

    for (first_column = 0; first_column < columns; first_column += TRANSPOSE_TILE_PIXELS) {
        tile_width = transpose_tile_width(columns, first_column);
        for (row = 0; row < rows; row++) {
            const uint16_t *source = landscape + row * stride_pixels + first_column;

            for (pixel = 0; pixel < tile_width; pixel++)
                portrait[(first_column + pixel) * rows + row] = source[pixel];
        }
    }
}

static inline void transpose_rectangle(const uint16_t *landscape, uint32_t stride_pixels, uint32_t rows, uint32_t columns, void *portrait)
{
    if (rows & 1)
        transpose_rectangle_single(landscape, stride_pixels, rows, columns, (uint16_t *)portrait);
    else
        transpose_rectangle_paired(landscape, stride_pixels, rows, columns, (uint32_t *)portrait);
}

static inline void transpose_band(const uint16_t *landscape, uint32_t stride_pixels, uint32_t rows, void *portrait)
{
    transpose_rectangle(landscape, stride_pixels, rows, stride_pixels, portrait);
}

#endif
