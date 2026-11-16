#include <stdint.h>

#include "transpose.h"

#define PORTRAIT_BUFFER_PHYSICAL 0x40110000u
#define CACHED_RAM_OFFSET        0x40000000u
#define UNCACHED_RAM_OFFSET      0x60000000u
#define TE_INPUT_REGISTER        0xb0018940u
#define TE_BIT                   (1u << 16)
#define TE_POLL_LIMIT            200000u
#define LCDIF_CONTROL_REGISTER   0xb0030000u
#define LCDIF_RUN                1u
#define LCDIF_POLL_LIMIT         2000000u
#define LANDSCAPE_WIDTH          480u
#define MAXIMUM_ROWS             320u
#define CACHE_LINE_BYTES         32u
#define DRIVER_TRANSFER          0xc06f70fcu
#define DRIVER_CURSOR_OFF        0xc06fa380u
#define DRIVER_CURSOR_ON         0xc06fa054u
#define DRIVER_MARK_DIRTY        0xc06f7d94u
#define CURSOR_RECTANGLE_OFFSET  0x188u
#define CURSOR_SIZE_OFFSET       0x198u
#define CURSOR_HOTSPOT_OFFSET    0x1a0u
#define HIDDEN_POSITION          (-1)
#define DRIVER_PANEL_WRITE       0xc06f72e4u
#define DRIVER_PANEL_UPDATE      0xc06f63a4u
#define PANEL_COMMAND            0u
#define PANEL_DATA               1u
#define PANEL_COLUMN_ADDRESS     0x2au
#define PANEL_PAGE_ADDRESS       0x2bu
#define PANEL_MEMORY_WRITE       0x2cu
#define CONTROLLER_FULL_REFRESH  4u
#define TE_WAIT_MINIMUM_PIXELS   (LANDSCAPE_WIDTH * MAXIMUM_ROWS / 8u)
#define KERNEL_WRITE_DEBUG_STRING 0x8020c700u
#define DRIVER_ENTER_CRITICAL    0xc06fdde8u
#define DRIVER_LEAVE_CRITICAL    0xc06fddd8u
#define DRIVER_EVENT_MODIFY      0xc06fdc78u
#define DRIVER_COLLECT_DIRTY     0xc06f7eb4u
#define DRIVER_SEND_RECTANGLE    0xc06f80e0u
#define DRIVER_TAKE_DIRTY_TILES  0xc06f93c4u
#define DRIVER_REFRESH_EVENT     0xc0727a44u
#define DRIVER_ROTATION_OFFSET   0x18u
#define DRIVER_TILE_LIMIT_OFFSET 0xb4u
#define DRIVER_REGION_OFFSET     0xb8u
#define DRIVER_LOCK_OFFSET       0x6620u
#define REGION_DIRTY_COUNT_OFFSET 0x20u
#define EVENT_RESET              2u
#define MAXIMUM_DIRTY_TILES      64u
#define DEBUG_LOG_UNCACHED       0xa0180000u
#define DEBUG_LOG_MAGIC          0x474c4543u
#define DEBUG_LOG_BYTES          0x10000u
#define DEBUG_LOG_HEADER_BYTES   16u
#define DEBUG_LOG_CAPACITY       (DEBUG_LOG_BYTES - DEBUG_LOG_HEADER_BYTES)

typedef int (*transfer_function)(uint32_t address, uint32_t line_bytes, uint32_t lines);

struct rectangle {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
};

typedef void (*cursor_function)(uint8_t *driver);
typedef void (*write_debug_string_function)(const uint16_t *text);
typedef void (*critical_section_function)(void *section);
typedef int (*event_modify_function)(void *event, uint32_t action);
typedef void (*rectangle_function)(uint8_t *driver, struct rectangle *area);
typedef void (*take_tiles_function)(void *region, struct rectangle *tiles, uint32_t count);

struct debug_log {
    uint32_t magic;
    uint32_t written;
    uint32_t capacity;
    uint32_t reserved;
    uint8_t text[DEBUG_LOG_CAPACITY];
};
typedef void (*panel_write_function)(uint32_t kind, uint32_t value);
typedef int (*panel_update_function)(uint8_t *controller, uint32_t framebuffer, uint32_t left, uint32_t top, uint32_t right, uint32_t bottom, uint32_t full);
typedef void (*mark_dirty_function)(uint8_t *driver, const struct rectangle *area);

static void invalidate_data_cache_range(uint32_t start, uint32_t end)
{
    uint32_t line;

    for (line = start & ~(CACHE_LINE_BYTES - 1); line < end; line += CACHE_LINE_BYTES)
        __asm__ volatile("mcr p15, 0, %0, c7, c6, 1" : : "r"(line) : "memory");
}

static void wait_for_tearing_effect(void)
{
    volatile const uint32_t *input = (volatile const uint32_t *)TE_INPUT_REGISTER;
    uint32_t polls = 0;

    while (!(*input & TE_BIT) && ++polls < TE_POLL_LIMIT)
        ;
    while ((*input & TE_BIT) && ++polls < TE_POLL_LIMIT)
        ;
}

static void wait_for_previous_transfer(void)
{
    volatile const uint32_t *control = (volatile const uint32_t *)LCDIF_CONTROL_REGISTER;
    uint32_t polls = 0;

    while ((*control & LCDIF_RUN) && ++polls < LCDIF_POLL_LIMIT)
        ;
}

int a7300_display_transfer(uint32_t source, uint32_t line_bytes, uint32_t rows)
{
    transfer_function transfer = (transfer_function)DRIVER_TRANSFER;
    uint32_t stride_pixels = line_bytes / 2;
    uint32_t cached_source = source + CACHED_RAM_OFFSET;

    if (stride_pixels != LANDSCAPE_WIDTH || !rows || rows > MAXIMUM_ROWS)
        return transfer(source, line_bytes, rows);
    wait_for_previous_transfer();
    invalidate_data_cache_range(cached_source, cached_source + rows * line_bytes);
    transpose_band((const uint16_t *)cached_source, stride_pixels, rows, (void *)(PORTRAIT_BUFFER_PHYSICAL + UNCACHED_RAM_OFFSET));
    wait_for_tearing_effect();
    return transfer(PORTRAIT_BUFFER_PHYSICAL, rows * 2, stride_pixels);
}

static void mark_area_dirty(uint8_t *driver, const struct rectangle *area)
{
    mark_dirty_function mark_dirty = (mark_dirty_function)DRIVER_MARK_DIRTY;
    struct rectangle clipped = *area;

    if (clipped.left < 0)
        clipped.left = 0;
    if (clipped.top < 0)
        clipped.top = 0;
    if (clipped.right > (int32_t)LANDSCAPE_WIDTH)
        clipped.right = LANDSCAPE_WIDTH;
    if (clipped.bottom > (int32_t)MAXIMUM_ROWS)
        clipped.bottom = MAXIMUM_ROWS;
    if (clipped.left >= clipped.right || clipped.top >= clipped.bottom)
        return;
    mark_dirty(driver, &clipped);
}

int a7300_move_pointer(uint8_t *driver, int32_t x, int32_t y)
{
    struct rectangle *cursor = (struct rectangle *)(driver + CURSOR_RECTANGLE_OFFSET);
    const int32_t *size = (const int32_t *)(driver + CURSOR_SIZE_OFFSET);
    const int32_t *hotspot = (const int32_t *)(driver + CURSOR_HOTSPOT_OFFSET);
    struct rectangle previous = *cursor;
    int visible = x != HIDDEN_POSITION || y != HIDDEN_POSITION;

    ((cursor_function)DRIVER_CURSOR_OFF)(driver);
    if (visible) {
        cursor->left = x - hotspot[0];
        cursor->top = y - hotspot[1];
        cursor->right = cursor->left + size[0];
        cursor->bottom = cursor->top + size[1];
        ((cursor_function)DRIVER_CURSOR_ON)(driver);
    }
    mark_area_dirty(driver, &previous);
    if (visible)
        mark_area_dirty(driver, cursor);
    return 0;
}

static void send_address_range(uint32_t command, uint32_t first, uint32_t last)
{
    panel_write_function panel_write = (panel_write_function)DRIVER_PANEL_WRITE;

    panel_write(PANEL_COMMAND, command);
    panel_write(PANEL_DATA, first >> 8);
    panel_write(PANEL_DATA, first & 0xff);
    panel_write(PANEL_DATA, last >> 8);
    panel_write(PANEL_DATA, last & 0xff);
}

int a7300_panel_update(uint8_t *controller, uint32_t framebuffer, uint32_t left, uint32_t top, uint32_t right, uint32_t bottom, uint32_t full)
{
    panel_update_function original = (panel_update_function)DRIVER_PANEL_UPDATE;
    transfer_function transfer = (transfer_function)DRIVER_TRANSFER;
    uint32_t rows, columns, cached_framebuffer = framebuffer + CACHED_RAM_OFFSET;

    if (full || *(volatile uint32_t *)(controller + CONTROLLER_FULL_REFRESH))
        return original(controller, framebuffer, left, top, right, bottom, full);
    if (right >= LANDSCAPE_WIDTH)
        right = LANDSCAPE_WIDTH - 1;
    if (bottom >= MAXIMUM_ROWS)
        bottom = MAXIMUM_ROWS - 1;
    if (left > right || top > bottom)
        return 0;
    if (left == 0 && top == 0 && right == LANDSCAPE_WIDTH - 1 && bottom == MAXIMUM_ROWS - 1)
        return original(controller, framebuffer, left, top, right, bottom, full);
    rows = bottom - top + 1;
    columns = right - left + 1;
    wait_for_previous_transfer();
    send_address_range(PANEL_COLUMN_ADDRESS, top, bottom);
    send_address_range(PANEL_PAGE_ADDRESS, left, right);
    ((panel_write_function)DRIVER_PANEL_WRITE)(PANEL_COMMAND, PANEL_MEMORY_WRITE);
    invalidate_data_cache_range(cached_framebuffer + (top * LANDSCAPE_WIDTH + left) * 2,
        cached_framebuffer + (bottom * LANDSCAPE_WIDTH + right + 1) * 2);
    transpose_rectangle((const uint16_t *)cached_framebuffer + top * LANDSCAPE_WIDTH + left, LANDSCAPE_WIDTH, rows, columns,
        (void *)(PORTRAIT_BUFFER_PHYSICAL + UNCACHED_RAM_OFFSET));
    if (rows * columns >= TE_WAIT_MINIMUM_PIXELS)
        wait_for_tearing_effect();
    return transfer(PORTRAIT_BUFFER_PHYSICAL, rows * 2, columns);
}

static int touch_read_message(const uint16_t *text)
{
    return text[0] == 'T' && text[1] == 'P' && text[2] == 'D' && text[3] == '_' && text[4] == 'R' &&
        text[5] == 'e' && text[6] == 'a' && text[7] == 'd' && text[8] == '(' && text[9] == ')';
}

void a7300_write_debug_string(const uint16_t *text)
{
    volatile struct debug_log *log = (volatile struct debug_log *)DEBUG_LOG_UNCACHED;
    const uint16_t *character;

    if (touch_read_message(text)) {
        ((write_debug_string_function)KERNEL_WRITE_DEBUG_STRING)(text);
        return;
    }
    if (log->magic != DEBUG_LOG_MAGIC || log->capacity != DEBUG_LOG_CAPACITY) {
        log->written = 0;
        log->capacity = DEBUG_LOG_CAPACITY;
        log->reserved = 0;
        log->magic = DEBUG_LOG_MAGIC;
    }
    for (character = text; *character; character++) {
        log->text[log->written % DEBUG_LOG_CAPACITY] = *character < 0x80 ? (uint8_t)*character : '?';
        log->written++;
    }
    ((write_debug_string_function)KERNEL_WRITE_DEBUG_STRING)(text);
}

static int adjacent_in_row(const struct rectangle *run, const struct rectangle *tile)
{
    return tile->top == run->top && tile->bottom == run->bottom && tile->left <= run->right + 1 && tile->left >= run->left;
}

static int stacks_below(const struct rectangle *upper, const struct rectangle *lower)
{
    return lower->left == upper->left && lower->right == upper->right && lower->top <= upper->bottom + 1 && lower->top >= upper->top;
}

static uint32_t merge_tiles(struct rectangle *tiles, uint32_t count)
{
    uint32_t runs = 0, index, merged;

    for (index = 0; index < count; index++) {
        if (runs && adjacent_in_row(&tiles[runs - 1], &tiles[index])) {
            if (tiles[index].right > tiles[runs - 1].right)
                tiles[runs - 1].right = tiles[index].right;
        } else {
            tiles[runs++] = tiles[index];
        }
    }
    merged = 0;
    for (index = 0; index < runs; index++) {
        uint32_t candidate;

        for (candidate = 0; candidate < merged; candidate++) {
            if (stacks_below(&tiles[candidate], &tiles[index])) {
                tiles[candidate].bottom = tiles[index].bottom;
                break;
            }
        }
        if (candidate == merged)
            tiles[merged++] = tiles[index];
    }
    return merged;
}

void a7300_refresh(uint8_t *driver)
{
    struct rectangle tiles[MAXIMUM_DIRTY_TILES];
    struct rectangle whole;
    void *lock = driver + DRIVER_LOCK_OFFSET;
    void *region = *(void **)(driver + DRIVER_REGION_OFFSET);
    uint32_t count, rectangles, index;

    ((critical_section_function)DRIVER_ENTER_CRITICAL)(lock);
    count = *(volatile uint32_t *)((uint8_t *)region + REGION_DIRTY_COUNT_OFFSET);
    if (!count || count > MAXIMUM_DIRTY_TILES || count == *(uint32_t *)(driver + DRIVER_TILE_LIMIT_OFFSET) ||
        *(uint32_t *)(driver + DRIVER_ROTATION_OFFSET)) {
        ((critical_section_function)DRIVER_LEAVE_CRITICAL)(lock);
        ((rectangle_function)DRIVER_COLLECT_DIRTY)(driver, &whole);
        ((rectangle_function)DRIVER_SEND_RECTANGLE)(driver, &whole);
        return;
    }
    ((take_tiles_function)DRIVER_TAKE_DIRTY_TILES)(region, tiles, count);
    ((event_modify_function)DRIVER_EVENT_MODIFY)(*(void **)DRIVER_REFRESH_EVENT, EVENT_RESET);
    ((critical_section_function)DRIVER_LEAVE_CRITICAL)(lock);
    rectangles = merge_tiles(tiles, count);
    for (index = 0; index < rectangles; index++)
        ((rectangle_function)DRIVER_SEND_RECTANGLE)(driver, &tiles[index]);
}
