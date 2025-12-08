#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "functions.h"

// Array to hold all freelist page numbers
int freelist_pages[MAX_FREELIST_PAGES];
int freelist_page_count = 0;

/*------------------------------------------------------------------------------------------------------------------------------*/

void parseFreelistPages()
{
    int trunk_page = header.freelist_trunk;
    int total_pages = header.freelist_count;

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

/*------------------------------------------------------------------------------------------------------------------------------*/

void getFreeblockData()
{
    for (int i = 1; i <= header.db_size_pages; i++)
    {
        int page_number = i;
        fseek(fp, (page_number == 1 ? 0 : (page_number - 1) * header.page_size), SEEK_SET);

        unsigned char *page = malloc(header.page_size);
        if (!page)
            continue;

        if (fread(page, 1, header.page_size, fp) != header.page_size)
        {
            fprintf(stderr, "Error reading page %d\n", page_number);
            free(page);
            continue;
        }

        unsigned char *ptr = (page_number == 1) ? page + HEADER_OFFSET : page;
        uint16_t freeblock_offset = (ptr[OFFSET1] << BYTE_SHIFT_8) | ptr[OFFSET2]; // Bytes 1-2 of page header

        while (freeblock_offset != 0 && freeblock_offset < header.page_size - OFFSET4)
        {
            unsigned char *fb_ptr = ptr + freeblock_offset;
            uint16_t next_fb_offset = (fb_ptr[OFFSET0] << BYTE_SHIFT_8) | fb_ptr[OFFSET1];
            uint16_t fb_size = (fb_ptr[OFFSET2] << BYTE_SHIFT_8) | fb_ptr[OFFSET3];

            printf("\n[Freeblock found]");
            printf("\nPage       : %d", page_number);
            printf("\nOffset     : %d", freeblock_offset);
            printf("\nSize       : %d bytes", fb_size);
            printf("\nRaw Content: ");

            // Print content in ASCII where printable
            for (int j = 4; j < fb_size && (freeblock_offset + j) < header.page_size; j++)
            {
                unsigned char c = fb_ptr[j];
                if (c >= 32 && c <= 126)
                    printf("%c", c);
                else if (c != 0)
                    printf(".");
            }
            printf("\n");

            // Move to next freeblock
            freeblock_offset = next_fb_offset;
        }

        free(page);
    }
}

/*------------------------------------------------------------------------------------------------------------------------------*/

void extractCellSlack()
{
    for (int i = 1; i <= header.db_size_pages; i++)
    {
        int page_number = i;
        fseek(fp, (page_number == 1 ? 0 : (page_number - 1) * header.page_size), SEEK_SET);
        unsigned char *page = malloc(header.page_size);
        fread(page, 1, header.page_size, fp);
        unsigned char *ptr = (page_number == 1) ? page + HEADER_OFFSET : page;

        uint16_t cell_count = (ptr[OFFSET3] << BYTE_SHIFT_8) | ptr[OFFSET4];
        uint16_t cell_offsets[cell_count];

        for (int i = 0; i < cell_count; i++)
        {
            cell_offsets[i] = (ptr[CELL_PTR_ARRAY_OFFSET + i * 2] << BYTE_SHIFT_8) | ptr[CELL_PTR_ARRAY_OFFSET + i * 2 + 1];
        }

        // Sort offsets
        for (int i = 0; i < cell_count - 1; i++)
        {
            for (int j = i + 1; j < cell_count; j++)
            {
                if (cell_offsets[i] > cell_offsets[j])
                {
                    uint16_t temp = cell_offsets[i];
                    cell_offsets[i] = cell_offsets[j];
                    cell_offsets[j] = temp;
                }
            }
        }

        printf("\nSlack spaces on page %d:\n", page_number);
        for (int i = 0; i < cell_count - 1; i++)
        {
            unsigned char *end_of_current = ptr + cell_offsets[i];
            unsigned char *start_of_next = ptr + cell_offsets[i + 1];

            int slack_size = start_of_next - (end_of_current + 1);
            if (slack_size > 0)
            {
                printf("Between cells %d and %d: ", i + 1, i + 2);
                for (unsigned char *s = end_of_current + 1; s < start_of_next; s++)
                {
                    // if ((*s >= 48 && *s <= 57) || (*s >= 65 && *s <= 90) || (*s >= 97 && *s <= 122))
                    if (*s >= 32 && *s <= 126)
                        printf("%c", *s);
                    else
                        printf(".");
                }
                printf("\n");
            }
        }
        free(page);
    }
}

/*------------------------------------------------------------------------------------------------------------------------------*/

// void extractPageSlack(int page_number)
// {
//     fseek(fp, (page_number == 1 ? 0 : (page_number - 1) * header.page_size), SEEK_SET);
//     unsigned char *page = malloc(header.page_size);
//     fread(page, 1, header.page_size, fp);
//     unsigned char *ptr = (page_number == 1) ? page + HEADER_OFFSET : page;
// }

/*------------------------------------------------------------------------------------------------------------------------------*/

void carveFreeblockRecords(int page_number, unsigned char *page, int offset, int size)
{
    int end = offset + size;
    while (offset < end)
    {
        int n, header_offset = 0;
        uint64_t payload = readVarint(page + offset, &n);
        if (n <= 0 || offset + n >= end)
            break;
        offset += n;

        uint64_t rowid = readVarint(page + offset, &n);
        if (n <= 0 || offset + n >= end)
            break;
        offset += n;

        uint64_t header_size = readVarint(page + offset, &n);
        if (n <= 0 || offset + n >= end)
            break;
        offset += n;

        unsigned char *header = page + offset;
        header_offset = 0;

        // Try to decode serial types (heuristic limit)
        uint64_t serial_types[MAX_COLUMNS];
        int num_columns = 0;

        while ((offset + header_offset) < end && header_offset < header_size && num_columns < MAX_COLUMNS)
        {
            serial_types[num_columns] = readVarint(header + header_offset, &n);
            if (n <= 0)
                break;
            header_offset += n;
            num_columns++;
        }

        if (num_columns == 0 || offset + header_offset >= end)
        {
            offset++; // shift forward and retry
            continue;
        }

        // Now read actual record content
        unsigned char *content = page + offset + header_offset;
        int content_offset = 0;

        printf("Recovered record at offset %d (page %d): (", offset, page_number);
        for (int i = 0; i < num_columns; i++)
        {
            uint64_t stype = serial_types[i];

            if (stype == 0)
            {
                printf("%llu", rowid);
            }
            else if (stype >= 13 && stype % 2 == 1)
            {
                int len = (stype - 13) / 2;
                if (content_offset + len > size)
                    break;
                char *text = malloc(len + 1);
                memcpy(text, content + content_offset, len);
                text[len] = '\0';
                printf("'%s'", text);
                free(text);
                content_offset += len;
            }
            else if (stype >= 1 && stype <= 6)
            {
                int sz = (stype == 1) ? 1 : (stype == 2) ? 2
                                        : (stype == 3)   ? 3
                                        : (stype == 4)   ? 4
                                        : (stype == 5)   ? 6
                                                         : 8;
                if (content_offset + sz > size)
                    break;
                uint64_t val = 0;
                for (int b = 0; b < sz; b++)
                    val = (val << 8) | content[content_offset + b];
                printf("%llu", val);
                content_offset += sz;
            }
            else
            {
                printf("?");
            }

            if (i < num_columns - 1)
                printf(", ");
        }
        printf(")\n");

        // Skip to next record (very loose guess: entire payload section + headers)
        offset += header_size + content_offset;
    }
}
