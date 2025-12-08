#include <stdio.h>
#include <stdlib.h>
#include "functions.h"

/*------------------------------------------------------------------------------------------------------------------------------*/

typedef void (*LeafHandler)(int page_number, int index);

void btreeWalk(int root_page, int idx, LeafHandler handler)
{
    int stack[MAX_STACK_SIZE];
    int top = -1;

    stack[++top] = root_page;

    while (top >= 0)
    {
        int current_page = stack[top--];

        fseek(fp, (current_page == 1 ? 0 : (current_page - 1) * header.page_size), SEEK_SET);
        unsigned char *page = malloc(header.page_size);
        if (!page)
        {
            perror("malloc");
            return;
        }
        if (fread(page, 1, header.page_size, fp) != header.page_size)
        {
            fprintf(stderr, "Failed to read page %d\n", current_page);
            free(page);
            continue;
        }

        unsigned char *ptr = (current_page == 1) ? page + HEADER_OFFSET : page;
        uint8_t page_type = ptr[0];

        if (page_type == TABLE_LEAF_PAGE)
        {
            handler(current_page, idx);
        }
        else if (page_type == TABLE_INTERIOR_PAGE)
        {
            uint16_t num_cells = (ptr[OFFSET3] << BYTE_SHIFT_8) | ptr[OFFSET4];

            // Push right-most child first (so it’s processed last)
            uint32_t rightmost = (ptr[OFFSET8] << BYTE_SHIFT_24) | (ptr[OFFSET9] << BYTE_SHIFT_16) | (ptr[OFFSET10] << BYTE_SHIFT_8) | ptr[OFFSET11];
            if (top < MAX_STACK_SIZE - 1)
                stack[++top] = rightmost;

            for (int i = num_cells - 1; i >= 0; i--)
            { // Reverse order to preserve left-first DFS
                uint16_t offset = (ptr[CELL_PTR_ARRAY_OFFSET + ((i + 2) * OFFSET2)] << BYTE_SHIFT_8) | ptr[CELL_PTR_ARRAY_OFFSET + ((i + 2) * OFFSET2) + 1];
                unsigned char *cell = page + offset;
                uint32_t child_page = (cell[OFFSET0] << BYTE_SHIFT_24) | (cell[OFFSET1] << BYTE_SHIFT_16) | (cell[OFFSET2] << BYTE_SHIFT_8) | cell[OFFSET3];
                if (top < MAX_STACK_SIZE - 1)
                    stack[++top] = child_page;
            }
        }

        free(page);
    }
}
