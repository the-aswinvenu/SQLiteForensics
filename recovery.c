#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "functions.h"

// Array to hold all freelist page numbers
int freelist_pages[MAX_FREELIST_PAGES];
int freelist_page_count = 0;
FILE *csv_stalecells = NULL;
FILE *csv_cellslack = NULL;
FILE *csv_pageslack = NULL;
FILE *csv_freeblock = NULL;
FILE *csv_freelist = NULL;
FILE *csv_orphanpages = NULL;

/*------------------------------------------------------------------------------------------------------------------------------*/

void parseFreelistPages()
{
    int trunk_page = header.freelist_trunk;
    int total_pages = header.freelist_count - 1;

    printf("\n[+] Freelist Metadata:\n");
    printf("    Trunk Page Start : %d\n", trunk_page);
    printf("    Freelist Page Count : %d\n", total_pages);

    while (trunk_page && freelist_page_count < MAX_FREELIST_PAGES)
    {
        // Read the trunk page
        fseek(fp, ((trunk_page - 1) * header.page_size), SEEK_SET);
        unsigned char *page = malloc(header.page_size);
        if (!page)
        {
            fprintf(stderr, "Failed to read trunk page: %d\n", trunk_page);
            break;
        }
        if (fread(page, 1, header.page_size, fp) != header.page_size)
        {
            fprintf(stderr, "Failed to read page %d\n", trunk_page);
            free(page);
            continue;
        }

        freelist_pages[freelist_page_count++] = trunk_page;

        // Get next trunk page
        int next_trunk = (page[OFFSET0] << BYTE_SHIFT_24) | (page[OFFSET1] << BYTE_SHIFT_16) | (page[OFFSET2] << BYTE_SHIFT_8) | page[OFFSET3];

        // Get leaf page count
        int leaf_count = (page[OFFSET4] << BYTE_SHIFT_8) | page[OFFSET5];

        printf("\n    Trunk Page: %d -> Next Trunk: %d, Leaf Pages: %d\n", trunk_page, next_trunk, leaf_count);

        // Read leaf page numbers (each 4 bytes starting from byte 8)
        for (int i = 0; i < leaf_count && freelist_page_count < MAX_FREELIST_PAGES; i++)
        {
            int offset = 8 + i * 4;
            int leaf_page = (page[offset] << BYTE_SHIFT_24) | (page[offset + OFFSET1] << BYTE_SHIFT_16) |
                            (page[offset + OFFSET2] << BYTE_SHIFT_8) | page[offset + OFFSET3];
            freelist_pages[freelist_page_count++] = leaf_page;
            printf("        Leaf Page: %d\n", leaf_page);
        }

        trunk_page = next_trunk;
        free(page);
    }

    printf("\n[+] Total Freelist Pages Found: %d\n", freelist_page_count);
}

void analyzeFreelistPages()
{
    fseek(fp, 0x20, SEEK_SET);
    uint32_t trunk_page;
    fread(&trunk_page, 4, 1, fp);
    trunk_page = __builtin_bswap32(trunk_page); // convert BE to host

    while (trunk_page != 0)
    {
        uint32_t trunk_offset = (trunk_page - 1) * header.page_size;
        fseek(fp, trunk_offset, SEEK_SET);

        // Read next trunk page
        uint32_t next_trunk;
        fread(&next_trunk, 4, 1, fp);
        next_trunk = __builtin_bswap32(next_trunk);

        // Read number of leaf pages
        uint32_t leaf_count;
        fread(&leaf_count, 4, 1, fp);
        leaf_count = __builtin_bswap32(leaf_count);

        for (uint32_t i = 0; i < leaf_count; i++)
        {
            uint32_t leaf_page;
            fseek(fp, trunk_offset + 8 + (i * 4), SEEK_SET);
            fread(&leaf_page, 4, 1, fp);
            leaf_page = __builtin_bswap32(leaf_page);

            uint32_t leaf_offset = (leaf_page - 1) * header.page_size;
            fseek(fp, leaf_offset, SEEK_SET);
            unsigned char *page = malloc(header.page_size);
            if (!page || fread(page, 1, header.page_size, fp) != header.page_size)
            {
                free(page);
                continue;
            }

            // Analyze leaf page using parseCell()
            int table_index = getSchemaIndexByPage(leaf_page);
            if (table_index == -1)
            {
                free(page);
                continue;
            }

            printf("[FREEPAGE %u] analyzing from offset 0\n", leaf_page);
            for (int offset = 0; offset < header.page_size - 8;)
            {
                ParsedRow row;
                int consumed = 0;
                if (parseCell(page, offset, table_index, &row, &consumed) && consumed > 0)
                {
                    fprintf(csv_freelist, "[RECOVERED: FREE-PAGE %u OFFSET %d],", leaf_page, offset);
                    for (int j = 0; j < row.column_count; j++)
                    {
                        fprintf(csv_freelist, "%s", row.values[j]);
                        if (j < row.column_count - 1)
                            fprintf(csv_freelist, ",");
                    }
                    fprintf(csv_freelist, "\n");
                    offset += consumed;
                }
                else
                {
                    offset++;
                }
            }

            free(page);
        }

        trunk_page = next_trunk;
    }
}

/*------------------------------------------------------------------------------------------------------------------------------*/

void extractCellSlack()
{
    fprintf(csv_cellslack, "\n[Slack Between Active Cells]\n");
    for (int page_number = 1; page_number <= header.db_size_pages; page_number++)
    {
        printf("\n Page number: %d\n", page_number);
        fseek(fp, (page_number - 1) * header.page_size, SEEK_SET);
        unsigned char *page = malloc(header.page_size);
        if (!page || fread(page, 1, header.page_size, fp) != header.page_size)
        {
            free(page);
            continue;
        }

        unsigned char *ptr = (page_number == 1) ? page + HEADER_OFFSET : page;
        uint8_t page_type = ptr[OFFSET0];

        if (page_type != TABLE_LEAF_PAGE && page_type != TABLE_INTERIOR_PAGE &&
            page_type != INDEX_LEAF_PAGE && page_type != INDEX_INTERIOR_PAGE)
        {
            free(page);
            continue;
        }

        uint16_t cell_count = (ptr[OFFSET3] << 8) | ptr[OFFSET4];
        uint16_t content_start_offset = (ptr[OFFSET5] << 8) | ptr[OFFSET6];
        if (cell_count == 0 && content_start_offset == header.page_size)
        {
            free(page);
            continue;
        }

        // Read all cell offsets
        int hdr_offset = (page_type == TABLE_INTERIOR_PAGE || page_type == INDEX_INTERIOR_PAGE) ? 12 : 8;
        uint16_t cell_offsets[cell_count];

        for (int i = 0; i < cell_count; i++)
        {
            cell_offsets[i] = (ptr[hdr_offset + i * 2] << 8) | ptr[hdr_offset + i * 2 + 1];
        }

        // Sort cell offsets (lowest to highest)
        for (int i = 0; i < cell_count - 1; i++)
        {
            for (int j = i + 1; j < cell_count; j++)
            {
                if (cell_offsets[i] > cell_offsets[j])
                {
                    uint16_t tmp = cell_offsets[i];
                    cell_offsets[i] = cell_offsets[j];
                    cell_offsets[j] = tmp;
                }
            }
        }

        printf("\nPage %d:\n", page_number);
        for (int i = 0; i < cell_count - 1; i++)
        {
            uint16_t cur_offset = cell_offsets[i];
            int consumed = 0;
            int table_index = getSchemaIndexByPage(page_number);
            if (table_index == -1)
                continue;

            ParsedRow row;
            if (!parseCell(ptr, cur_offset, table_index, &row, &consumed) || consumed <= 0)
                continue;

            uint16_t end_of_cell = cur_offset + consumed;
            uint16_t next_cell = cell_offsets[i + 1];

            // If no slack in between, skip
            if (next_cell <= end_of_cell)
                continue;

            // Parse the slack space
            for (int offset = end_of_cell; offset + 8 < next_cell;)
            {
                ParsedRow slack_row;
                int slack_consumed = 0;

                if (parseCell(ptr, offset, table_index, &slack_row, &slack_consumed) && slack_consumed > 0)
                {
                    // fprintf(csv_recovered, "%s", slack_row.values[j]);
                    printf("[Slack-Recovered at %d]: ", offset);
                    fprintf(csv_cellslack, "(\n");
                    for (int j = 0; j < slack_row.column_count; j++)
                    {
                        // printf("%s", slack_row.values[j]);
                        fprintf(csv_cellslack, "%s", slack_row.values[j]);
                        if (j < slack_row.column_count - 1)
                            fprintf(csv_cellslack, ",");
                    }
                    fprintf(csv_cellslack, ")\n");
                    offset += slack_consumed;
                }
                else
                {
                    offset++;
                }
            }
        }

        free(page);
    }
}

/*------------------------------------------------------------------------------------------------------------------------------*/

void extractPageSlack()
{
    fprintf(csv_pageslack, "\n[Recovered from page slack]\n");
    for (int page_number = 1; page_number <= header.db_size_pages; page_number++)
    {
        printf("\n Page number: %d\n", page_number);
        fseek(fp, (page_number - 1) * header.page_size, SEEK_SET);
        unsigned char *page = malloc(header.page_size);
        if (!page || fread(page, 1, header.page_size, fp) != header.page_size)
        {
            free(page);
            continue;
        }

        unsigned char *ptr = (page_number == 1) ? page + HEADER_OFFSET : page;
        uint8_t page_type = ptr[0];

        if (page_type != TABLE_LEAF_PAGE && page_type != TABLE_INTERIOR_PAGE &&
            page_type != INDEX_LEAF_PAGE && page_type != INDEX_INTERIOR_PAGE)
        {
            printf("\nPage no: %d\n", page_number);
            free(page);
            continue;
        }

        uint16_t num_cells = (ptr[OFFSET3] << 8) | ptr[OFFSET4];
        uint16_t content_start = (ptr[OFFSET5] << 8) | ptr[OFFSET6];
        if (content_start == 0)
            content_start = header.page_size;

        // Skip empty interior pages
        if (num_cells == 0 && content_start == header.page_size)
        {
            free(page);
            continue;
        }

        // Calculate start of cell pointer array end
        int header_offset = (page_type == TABLE_INTERIOR_PAGE || page_type == INDEX_INTERIOR_PAGE) ? 12 : 8;
        int slack_start = header_offset + (2 * num_cells);
        int slack_end = content_start;

        int offset = slack_start;

        while (offset < slack_end)
        {
            if (ptr[offset] == 0x00)
            {
                offset++;
                continue;
            }

            int table_index = getSchemaIndexByPage(page_number);
            if (table_index == -1)
            {
                offset++;
                continue;
            }

            ParsedRow row;
            int consumed = 0;

            if (parseCell(ptr, offset, table_index, &row, &consumed) && consumed > 0)
            {
                static int header_written[MAX_TABLES] = {0};
                if (!header_written[table_index])
                {
                    fprintf(csv_pageslack, "Recovered from page %d\n", page_number);
                    fprintf(csv_pageslack, "Possible Table: %s\n", objects[table_index].name);
                    for (int j = 0; j < row.column_count; j++)
                    {
                        fprintf(csv_pageslack, "%s", objects[table_index].columns[j].name);
                        if (j < row.column_count - 1)
                            fprintf(csv_pageslack, ",");
                    }
                    fprintf(csv_pageslack, "\n");
                    header_written[table_index] = 1;
                }

                // fprintf(csv_recovered, "[RECOVERED: pg %d @ offset %d],", page_number, offset);
                for (int j = 0; j < row.column_count; j++)
                {
                    fprintf(csv_pageslack, "%s", row.values[j]);
                    if (j < row.column_count - 1)
                        fprintf(csv_pageslack, ",");
                }
                fprintf(csv_pageslack, "\n");

                offset += consumed;
            }
            else
                offset++;
        }
        free(page);
    }
}
/*------------------------------------------------------------------------------------------------------------------------------*/

void carveFreeblocksRecords()
{
    fprintf(csv_freeblock, "\n[FreeBlock Data]\n");
    for (int page_number = 1; page_number <= header.db_size_pages; page_number++)
    {
        fseek(fp, ((page_number - 1) * header.page_size), SEEK_SET);
        unsigned char *page = malloc(header.page_size);
        if (!page || fread(page, 1, header.page_size, fp) != header.page_size)
        {
            free(page);
            continue;
        }

        unsigned char *ptr = (page_number == 1) ? page + HEADER_OFFSET : page;
        uint16_t offset = (ptr[OFFSET1] << BYTE_SHIFT_8) | ptr[OFFSET2];

        if (ptr[0] != TABLE_LEAF_PAGE && ptr[0] != TABLE_INTERIOR_PAGE &&
            ptr[0] != INDEX_LEAF_PAGE && ptr[0] != INDEX_INTERIOR_PAGE)
        {
            free(page);
            continue;
        }

        while (offset && offset < header.page_size - 4)
        {
            unsigned char *fb = ptr + offset;
            uint16_t next = (fb[OFFSET0] << BYTE_SHIFT_8) | fb[OFFSET1];
            uint16_t size = (fb[OFFSET2] << BYTE_SHIFT_8) | fb[OFFSET3];

            for (int i = 4; i < size - 8;)
            {
                ParsedRow row;
                int consumed = 0;
                if (parseCell(fb, i, 0, &row, &consumed) && consumed > 0)
                {
                    fprintf(csv_freeblock, "Page %d Offset %d,", page_number, offset + i);
                    for (int j = 0; j < row.column_count; j++)
                    {
                        fprintf(csv_freeblock, "%s", row.values[j]);
                        if (j < row.column_count - 1)
                            fprintf(csv_freeblock, ",");
                    }
                    fprintf(csv_freeblock, "\n");

                    i += consumed;
                }
                else
                {
                    i++; // move forward byte by byte if not valid
                }
            }

            offset = next;
        }

        free(page);
    }
}

/*------------------------------------------------------------------------------------------------------------------------------*/

void recoverStaleCells()
{
    fprintf(csv_stalecells, "[Stale cell data]\n");
    for (int page_number = 1; page_number <= header.db_size_pages; page_number++)
    {
        fseek(fp, (page_number - 1) * header.page_size, SEEK_SET);
        unsigned char *page = malloc(header.page_size);
        if (!page || fread(page, 1, header.page_size, fp) != header.page_size)
        {
            free(page);
            continue;
        }
        unsigned char *ptr = (page_number == 1) ? page + HEADER_OFFSET : page;
        uint8_t page_type = ptr[0];

        if (page_type != TABLE_LEAF_PAGE && page_type != TABLE_INTERIOR_PAGE &&
            page_type != INDEX_LEAF_PAGE && page_type != INDEX_INTERIOR_PAGE)
        {
            free(page);
            continue;
        }

        uint16_t cell_count = (ptr[OFFSET3] << 8) | ptr[OFFSET4];
        uint16_t content_area = (ptr[OFFSET5] << 8) | ptr[OFFSET6];
        if (content_area == 0)
            content_area = header.page_size;

        int header_offset = (page_type == TABLE_INTERIOR_PAGE || page_type == INDEX_INTERIOR_PAGE) ? 12 : 8;
        int scan_start = header_offset + 2 * cell_count;
        int offset = scan_start;
        int consecutive_zeros = 0;

        int table_index = getSchemaIndexByPage(page_number);
        if (table_index == -1)
        {
            free(page);
            continue;
        }

        int printed_header = 0;

        while (offset + 1 < content_area)
        {
            if (ptr[offset] == 0x00 && ptr[offset + 1] == 0x00)
            {
                consecutive_zeros += 2;
                if (consecutive_zeros >= 4)
                    break;
                offset++;
                continue;
            }
            else
            {
                consecutive_zeros = 0;
            }

            uint16_t potential_offset = (ptr[offset] << 8) | ptr[offset + 1];
            if (potential_offset >= header.page_size || potential_offset < header_offset)
            {
                offset++;
                continue;
            }

            ParsedRow row;
            int consumed = 0;
            if (parseCell(ptr, potential_offset, table_index, &row, &consumed) && consumed > 0)
            {
                if (!printed_header)
                {
                    fprintf(csv_stalecells, "\nRecovered from Page no: %d at offset: %d\n", page_number, potential_offset);
                    fprintf(csv_stalecells, "Possible table: %s\n", objects[table_index].name);
                    printed_header = 1;
                }

                for (int i = 0; i < row.column_count; i++)
                {
                    fprintf(csv_stalecells, "%s", row.values[i]);
                    if (i < row.column_count - 1)
                        fprintf(csv_stalecells, ", ");
                }
                fprintf(csv_stalecells, "\n");
                offset += 2;
            }
            else
            {
                offset++;
            }
        }

        free(page);
    }
}

/*------------------------------------------------------------------------------------------------------------------------------*/

// void recoverOrphanPages(/*int page_number, int table_index*/)
// {
//     printf("\n[ORPHAN PAGE SCAN]\n");
//     for (int page_number = 2; page_number < header.db_size_pages; page_number++)
//     {
//         int table_index = getSchemaIndexByPage(page_number);
//         // Load page
//         fseek(fp, (page_number - 1) * header.page_size, SEEK_SET);
//         unsigned char *page = malloc(header.page_size);
//         if (!page || fread(page, 1, header.page_size, fp) != header.page_size)
//         {
//             free(page);
//             return;
//         }

//         unsigned char *ptr = (page_number == 1) ? page + HEADER_OFFSET : page;
//         uint8_t page_type = ptr[0];
//         uint16_t num_cells = (ptr[OFFSET3] << 8) | ptr[OFFSET4];
//         uint16_t content_start = (ptr[OFFSET5] << 8) | ptr[OFFSET6];

//         printf("Orphan Page %d (Type 0x%02X):\n", page_number, page_type);

//         // Case 1: Try to parse like a table leaf
//         if (page_type == TABLE_LEAF_PAGE || (num_cells == 0 && content_start == header.page_size))
//         {
//             // Attempt to parse as data rows
//             for (int offset = (page_type == TABLE_INTERIOR_PAGE) ? 12 : 8; offset < content_start - 8;)
//             {
//                 ParsedRow row;
//                 int consumed = 0;
//                 if (parseCell(ptr, offset, table_index, &row, &consumed) && consumed > 0)
//                 {
//                     fprintf(csv_orphanpages, "Recovered from orphan page %d at offset %d,", page_number, offset);
//                     for (int j = 0; j < row.column_count; j++)
//                     {
//                         fprintf(csv_orphanpages, "%s", row.values[j]);
//                         if (j < row.column_count - 1)
//                             fprintf(csv_orphanpages, ",");
//                     }
//                     fprintf(csv_orphanpages, "\n");
//                     offset += consumed;
//                 }
//                 else
//                 {
//                     offset++;
//                 }
//             }
//         }
//         else if (page_type == TABLE_INTERIOR_PAGE || (page_type == TABLE_LEAF_PAGE && num_cells == 0 && content_start == header.page_size))
//         {
//             // Suspected interior-like orphan page
//             printf("  Might contain orphaned child pointers:\n");
//             int offset = 12;
//             while (offset + 4 < header.page_size)
//             {
//                 uint32_t child_page = (ptr[offset] << 24) | (ptr[offset + 1] << 16) |
//                                       (ptr[offset + 2] << 8) | ptr[offset + 3];
//                 if (child_page != 0 && child_page <= header.db_size_pages)
//                 {
//                     printf("    ↳ Possibly linked child page: %d\n", child_page);
//                 }
//                 offset += 4;
//             }
//         }
//         free(page);
//     }
// }

// void recoverOrphanPages()
// {
//     for (int page_number = 2; page_number <= header.db_size_pages; page_number++)
//     {
//         fseek(fp, (page_number - 1) * header.page_size, SEEK_SET);
//         unsigned char *page = malloc(header.page_size);
//         if (!page || fread(page, 1, header.page_size, fp) != header.page_size)
//         {
//             free(page);
//             continue;
//         }

//         unsigned char *ptr = (page_number == 1) ? page + HEADER_OFFSET : page;
//         uint8_t page_type = ptr[OFFSET0];
//         if (page_type != TABLE_LEAF_PAGE && page_type != TABLE_INTERIOR_PAGE &&
//             page_type != INDEX_LEAF_PAGE && page_type != INDEX_INTERIOR_PAGE)
//         {
//             free(page);
//             continue;
//         }

//         uint16_t num_cells = (ptr[OFFSET3] << 8) | ptr[OFFSET4];
//         uint16_t content_start = (ptr[OFFSET5] << 8) | ptr[OFFSET6];
//         uint32_t right_ptr = (ptr[OFFSET8] << 24) | (ptr[OFFSET9] << 16) | (ptr[OFFSET10] << 8) | ptr[OFFSET11];
//         if (right_ptr > header.db_size_pages)
//             page_type == TABLE_LEAF_PAGE;

//         int table_index = getSchemaIndexByPage(page_number);

//         if ((table_index == -1) || (content_start == header.page_size && num_cells == 0))
//         {
//             fprintf(csv_orphanpages, "\n[Orphaned Page] Page no: %d\n", page_number);
//             fprintf(csv_orphanpages, "Possible Table: %s\n", objects[table_index].name);
//         } //--
//         else
//             continue;

//         int header_offset = (page_type == TABLE_INTERIOR_PAGE || page_type == INDEX_INTERIOR_PAGE) ? 12 : 8;
//         for (int offset = header_offset; offset < content_start;)
//         {
//             ParsedRow row;
//             int consumed = 0;
//             if (parseCell(ptr, offset, table_index, &row, &consumed) && consumed > 0)
//             {
//                 fprintf(csv_orphanpages, "[RECOVERED: Page %d at offset %d],", page_number, offset);
//                 for (int j = 0; j < row.column_count; j++)
//                 {
//                     fprintf(csv_orphanpages, "%s", row.values[j]);
//                     if (j < row.column_count - 1)
//                         fprintf(csv_orphanpages, ",");
//                 }
//                 fprintf(csv_orphanpages, "\n");
//                 offset += consumed;
//             }
//             else
//             {
//                 // Skip nulls
//                 if (ptr[offset] == 0x00 && ptr[offset + 1] == 0x00)
//                     offset += 2;
//                 else
//                     offset++;
//             }
//         }
//         free(page);
//     }
// }

void recoverOrphanPages()
{
    for (int page_number = 2; page_number <= header.db_size_pages; page_number++) // Skip page 1
    {
        fseek(fp, (page_number - 1) * header.page_size, SEEK_SET);
        unsigned char *page = malloc(header.page_size);
        if (!page || fread(page, 1, header.page_size, fp) != header.page_size)
        {
            free(page);
            continue;
        }
        unsigned char *ptr = page;
        uint8_t page_type = ptr[OFFSET0];

        // Skip pages with invalid types
        if (page_type != TABLE_LEAF_PAGE && page_type != TABLE_INTERIOR_PAGE &&
            page_type != INDEX_LEAF_PAGE && page_type != INDEX_INTERIOR_PAGE)
        {
            free(page);
            continue;
        }

        uint16_t num_cells = (ptr[OFFSET3] << 8) | ptr[OFFSET4];
        uint16_t content_start = (ptr[OFFSET5] << 8) | ptr[OFFSET6];
        if (content_start == 0)
            content_start = header.page_size;

        int schema_index = getSchemaIndexByPage(page_number);

        // Only consider orphans or empty-wiped pages
        if (!(schema_index == -1 || (num_cells == 0 && content_start == header.page_size)))
        {
            free(page);
            continue;
        }

        fprintf(csv_orphanpages, "\n[Recovered from Page %d]\n", page_number);
        if (schema_index != -1)
        {
            fprintf(csv_orphanpages, "Possible Table: %s\n", objects[schema_index].name);
        }

        int offset = 8; // Starting after header (8 or 12 doesn't matter for parseCell)
        while (offset < header.page_size)
        {
            // Skip sequences of 0x00
            while (offset < header.page_size && ptr[offset] == 0x00)
                offset++;

            if (offset >= header.page_size)
                break;

            ParsedRow row;
            int consumed = 0;
            if (parseCell(ptr, offset, schema_index, &row, &consumed) && consumed > 0)
            {
                for (int j = 0; j < row.column_count; j++)
                {
                    fprintf(csv_orphanpages, "%s", row.values[j]);
                    if (j < row.column_count - 1)
                        fprintf(csv_orphanpages, ",");
                }
                fprintf(csv_orphanpages, "\n");
                offset += consumed;
            }
            else
            {
                offset++; // Try next byte
            }
        }

        free(page);
    }
}
/*------------------------------------------------------------------------------------------------------------------------------*/