#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "functions.h"

/*------------------------------------------------------------------------------------------------------------------------------*/

void extractUserTables()
{
    printf("\nUser Tables:\n");
    for (int i = 0; i < obj_count; i++)
    {
        // Skip indexes, views, triggers, and SQLite internal tables
        if (strcmp(objects[i].obj_type, "table") == 0)
        {
            printf("Table Name : %s\n", objects[i].obj_name);
            printf("Name : %s\n", objects[i].name);
            printf("Root Page : %d\n", objects[i].root_page);
            printf("SQL : %s\n\n", objects[i].sql);
        }
        else if (strcmp(objects[i].obj_type, "index") == 0)
        {
            printf("Index Name : %s\n", objects[i].obj_name);
            printf("Name : %s\n", objects[i].name);
            printf("Root Page : %d\n", objects[i].root_page);
            printf("SQL : %s\n\n", objects[i].sql);
        }
    }
}

/*------------------------------------------------------------------------------------------------------------------------------*/

int parseCell(unsigned char *page, int offset, int table_index, ParsedRow *row, int *consumed)
{
    int n, page_size = header.page_size;
    int start_offset = offset;
    int col_count = objects[table_index].column_count;
    row->column_count = col_count;
    int computed_payload = 0;

    if (offset + 9 >= page_size)
        goto fail;

    uint64_t payload_size = readVarint(page + offset, &n);
    if (n <= 0 || payload_size > (page_size - offset))
        goto fail;
    offset += n;

    uint64_t rowid = readVarint(page + offset, &n);
    if (n <= 0 || offset + n >= page_size)
        goto fail;
    offset += n;

    uint64_t header_size = readVarint(page + offset, &n);
    if (n <= 0 || header_size > payload_size)
        goto fail;
    offset += n;
    if (col_count == 0){
        col_count = header_size-1;
        row->column_count=col_count;
    }

    unsigned char *header = page + offset;
    int header_offset = 0;
    uint64_t serial_types[MAX_COLUMNS];

    for (int j = 0; j < col_count; j++)
    {
        if ((offset + header_offset + 9) >= page_size)
            goto fail;
        serial_types[j] = readVarint(header + header_offset, &n);
        if (n <= 0)
            goto fail;
        header_offset += n;
    }

    unsigned char *content = page + offset + header_offset;
    int content_offset = 0;

    // Validate: serial types' declared sizes should not exceed payload
    for (int j = 0; j < col_count; j++)
    {
        uint64_t stype = serial_types[j];
        int size = 0;

        if (stype == 0)
            size = 0;
        else if (stype >= SERIAL_TYPE_TEXT_MIN && stype % 2 == 1)
            size = (stype - SERIAL_TYPE_TEXT_MIN) / 2;
        else if (stype >= SERIAL_TYPE_BLOB_MIN && stype % 2 == 0)
            size = (stype - SERIAL_TYPE_BLOB_MIN) / 2;
        else if (stype >= SERIAL_TYPE_INT8 && stype <= SERIAL_TYPE_INT64)
            size = (stype == SERIAL_TYPE_INT8) ? 1 : (stype == SERIAL_TYPE_INT16) ? 2
                                                 : (stype == SERIAL_TYPE_INT24)   ? 3
                                                 : (stype == SERIAL_TYPE_INT32)   ? 4
                                                 : (stype == SERIAL_TYPE_INT48)   ? 6
                                                                                  : 8;
        else if (stype == SERIAL_TYPE_FLOAT64)
            size = 8;

        else if (stype == SERIAL_TYPE_RESERVED0 || SERIAL_TYPE_RESERVED1)
            size = 0;

        content_offset += size;
        computed_payload += size;
    }
    if (computed_payload == 0)
        goto fail;

    if (payload_size != (computed_payload + header_size))
        goto fail;

    if (header_offset + content_offset > payload_size)
        goto fail;

    content_offset = 0;
    for (int j = 0; j < col_count; j++)
    {
        const char *col_type = objects[table_index].columns[j].type;
        uint64_t stype = serial_types[j];

        if (stype == 0)
        {
            if ((strcasecmp(col_type, "INTEGER") == 0 || strcasecmp(col_type, "INT") == 0))
                snprintf(row->values[j], MAX_CELL_DATA_LENGTH, "%llu", rowid);
            else
                strcpy(row->values[j], "NULL");
        }

        else if (stype >= SERIAL_TYPE_TEXT_MIN && stype % 2 == 1)
        {
            int len = (stype - SERIAL_TYPE_TEXT_MIN) / 2;
            if (content_offset + len > (page_size - offset - header_offset))
                goto fail;
            memcpy(row->values[j], content + content_offset, len);
            row->values[j][len] = '\0';
            content_offset += len;
        }
        else if (stype >= SERIAL_TYPE_INT8 && stype <= SERIAL_TYPE_INT64)
        {
            int size = (stype == SERIAL_TYPE_INT8) ? 1 : (stype == SERIAL_TYPE_INT16) ? 2
                                                     : (stype == SERIAL_TYPE_INT24)   ? 3
                                                     : (stype == SERIAL_TYPE_INT32)   ? 4
                                                     : (stype == SERIAL_TYPE_INT48)   ? 6
                                                                                      : 8;
            if (content_offset + size > (page_size - offset - header_offset))
                goto fail;

            uint64_t val = 0;
            for (int b = 0; b < size; b++)
                val = (val << BYTE_SHIFT_8) | content[content_offset + b];
            snprintf(row->values[j], MAX_CELL_DATA_LENGTH, "%llu", val);
            content_offset += size;
        }
        else if (stype == SERIAL_TYPE_FLOAT64)
        {
            if (content_offset + OFFSET8 > (page_size - offset - header_offset))
                goto fail;
            double d = decodeFloat64(content + content_offset);
            snprintf(row->values[j], MAX_CELL_DATA_LENGTH, "%f", d);
            content_offset += 8;
        }
        else if (stype == SERIAL_TYPE_RESERVED0 || SERIAL_TYPE_RESERVED1)
        {
            snprintf(row->values[j], MAX_CELL_DATA_LENGTH, "%d", stype - SERIAL_TYPE_RESERVED0);
        }
        else
        {
            strcpy(row->values[j], "NULL");
        }
    }

    if (consumed)
        *consumed = (offset - start_offset) + header_offset + content_offset;

    return 1;

fail:
    if (consumed)
        *consumed = 1;

    printf("[RAW SLACK at offset %d]: ", start_offset);
    for (int i = 0; i < 32 && start_offset + i < page_size; i++)
    {
        unsigned char c = page[start_offset + i];
        if (c >= 32 && c <= 126)
            printf("%c", c);
        else
            printf(".");
    }
    printf("\n");
    return 0;
}

/*------------------------------------------------------------------------------------------------------------------------------*/