#include <stdio.h>
#include <stdlib.h>
#include "functions.h"

/*------------------------------------------------------------------------------------------------------------------------------*/

typedef void (*LeafHandler)(int page_number, int index);

void btreeWalk(int root_page, int idx, LeafHandler handler, int forensic_mode)
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
        uint16_t num_cells = (ptr[OFFSET3] << 8) | ptr[OFFSET4];
        uint16_t content_area = (ptr[OFFSET5] << 8) | ptr[OFFSET6];

        int is_data_possible = (page_type == TABLE_LEAF_PAGE);
        int is_traversable = (page_type == TABLE_INTERIOR_PAGE);

        // Forensic override: infer from structure, not just page_type
        if (forensic_mode)
        {
            if (num_cells == 0 && content_area == header.page_size)
            {
                // Possibly an orphaned interior page converted to leaf
                is_traversable = 0;
                is_data_possible = 1;
            }
            else if (content_area < header.page_size)
            {
                // Possibly has valid children
                is_traversable = 1;
            }
        }

        if (is_data_possible && handler)
        {
            handler(current_page, idx);
        }

        if (is_traversable)
        {
            // Traverse right-most pointer
            uint32_t rightmost = (ptr[OFFSET8] << 24) | (ptr[OFFSET9] << 16) | (ptr[OFFSET10] << 8) | ptr[OFFSET11];
            if (rightmost > 0 && top < MAX_STACK_SIZE - 1)
                stack[++top] = rightmost;

            for (int i = num_cells - 1; i >= 0; i--)
            {
                uint16_t offset = (ptr[CELL_PTR_ARRAY_OFFSET + ((i + 2) * OFFSET2)] << BYTE_SHIFT_8) | ptr[CELL_PTR_ARRAY_OFFSET + ((i + 2) * OFFSET2) + 1];
                unsigned char *cell = page + offset;
                if (offset >= header.page_size)
                    continue;
                uint32_t child_page = (cell[OFFSET0] << BYTE_SHIFT_24) | (cell[OFFSET1] << BYTE_SHIFT_16) | (cell[OFFSET2] << BYTE_SHIFT_8) | cell[OFFSET3];
                if (top < MAX_STACK_SIZE - 1)
                    stack[++top] = child_page;
            }
        }

        free(page);
    }
}
