#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "functions.h"

/*------------------------------------------------------------------------------------------------------------------------------*/

schemaObjects objects[MAX_TABLES]; // Instance of struct schemaObjects
int obj_count = 0;                 // Number of objects

/*------------------------------------------------------------------------------------------------------------------------------*/

void getSQLiteMaster(int page_number, int id) // Parses the B-tree page and extracts schema information
{
    int page_size = header.page_size;
    fseek(fp, (page_number == 1 ? 0 : (page_number - 1) * header.page_size), SEEK_SET); // Move to the first byte location of the page except for page 1
    unsigned char *page = malloc(header.page_size);                                     // Allocate memory for one page
    if (!page)
    {
        perror("malloc");
        return;
    }
    if (fread(page, 1, header.page_size, fp) != header.page_size) // Read one full page from the file
    {
        fprintf(stderr, "Error reading page from file\n");
        free(page);
        return;
    }

    unsigned char *ptr = (page_number == 1) ? page + HEADER_OFFSET : page; // Skipping the first 100-Bytes in the case of the first page
    uint8_t page_type = ptr[0];
    if (page_type != TABLE_LEAF_PAGE)
    {
        printf("Page is not a table leaf page (0x0D). Found: 0x%02X\n", page_type);
        free(page);
        return;
    }

    uint16_t num_cells = (ptr[OFFSET3] << CELL_PTR_ARRAY_OFFSET) | ptr[OFFSET4]; // No. of cells from the 3rd and 4th bytes of the page header
    for (int i = 0; i < num_cells; i++)
    {
        uint16_t cell_offset = (ptr[CELL_PTR_ARRAY_OFFSET + i * 2] << CELL_PTR_ARRAY_OFFSET) | ptr[CELL_PTR_ARRAY_OFFSET + i * 2 + 1]; // Assinging cell offset
        if (cell_offset >= header.page_size)
        {
            fprintf(stderr, "Invalid cell offset %u at cell %d\n", cell_offset, i);
            continue;
        }

        unsigned char *cell = page + cell_offset; // Traversing to cell in the page
        int offset = 0, n;
        uint64_t payload_size = readVarint(cell + offset, &n);
        offset += n;
        uint64_t rowid = readVarint(cell + offset, &n);
        offset += n;
        uint64_t header_size = readVarint(cell + offset, &n);
        offset += n;

        unsigned char *header = cell + offset;
        int header_offset = 0;
        uint64_t serial_types[MAX_SCHEMA_FIELDS];

        for (int j = 0; j < MAX_SCHEMA_FIELDS; j++)
        {
            serial_types[j] = readVarint(header + header_offset, &n);
            header_offset += n;
        }

        unsigned char *content = cell + offset + header_offset;
        int content_offset = 0;

        char type[32] = {0}, name[64] = {0}, tbl_name[64] = {0};
        char *sql = NULL;
        int root_page = 0;

        for (int j = 0; j < MAX_SCHEMA_FIELDS; j++)
        {
            if (serial_types[j] >= SERIAL_TYPE_TEXT_MIN && serial_types[j] % 2 == 1)
            {
                int text_len = (serial_types[j] - SERIAL_TYPE_TEXT_MIN) / 2;

                if (text_len < 0 || content_offset + text_len > page_size)
                {
                    fprintf(stderr, "Invalid text length (%d) for field %d in cell %d. Skipping field.\n", text_len, j, i + 1);
                    break;
                }

                unsigned char *field_ptr = content + content_offset;
                if (field_ptr + text_len > page + page_size)
                {
                    fprintf(stderr, "Memory overflow detected in cell %d field %d\n", i + 1, j);
                    break;
                }

                switch ((SchemaColumn)j)
                {
                case COL_TYPE:
                    memcpy(type, field_ptr, text_len);
                    type[text_len] = '\0';
                    break;
                case COL_NAME:
                    memcpy(name, field_ptr, text_len);
                    name[text_len] = '\0';
                    break;
                case COL_TBLNAME:
                    memcpy(tbl_name, field_ptr, text_len);
                    tbl_name[text_len] = '\0';
                    break;
                case COL_SQL:
                    sql = malloc(text_len + 1);
                    if (!sql)
                    {
                        fprintf(stderr, "Memory allocation failed for SQL field in cell %d\n", i + 1);
                        break;
                    }
                    memcpy(sql, field_ptr, text_len);
                    sql[text_len] = '\0';
                    break;
                default:
                    break;
                }
                content_offset += text_len;
            }
            else if (j == COL_ROOTPAGE && (serial_types[j] >= SERIAL_TYPE_INT8 && serial_types[j] <= SERIAL_TYPE_INT64))
            {
                uint64_t val = 0;
                int size = 0;
                switch (serial_types[j])
                {
                case SERIAL_TYPE_INT8:
                    size = SERIAL_TYPE_INT8;
                    break;
                case SERIAL_TYPE_INT16:
                    size = SERIAL_TYPE_INT16;
                    break;
                case SERIAL_TYPE_INT24:
                    size = SERIAL_TYPE_INT24;
                    break;
                case SERIAL_TYPE_INT32:
                    size = SERIAL_TYPE_INT32;
                    break;
                case SERIAL_TYPE_INT48:
                    size = SERIAL_TYPE_INT64;
                    break;
                case SERIAL_TYPE_INT64:
                    size = SERIAL_TYPE_RESERVED0;
                    break;
                default:
                    size = 0;
                    break;
                }

                if (content_offset + size <= page_size)
                {
                    for (int b = 0; b < size; b++)
                    {
                        val = (val << BYTE_SHIFT_8) | content[content_offset + b];
                    }
                    root_page = (int)val;
                    content_offset += size;
                }
            }
        }

        if (strcmp(type, "table") != 0 && strcmp(type, "index") != 0 && strncmp(tbl_name, "sqlite_a", BYTE_SHIFT_8) != 0 && root_page == 0) // Skipping Triggers, Views and Auto-Indexes
            continue;                                                                                                                       // Not a table or index, skip to next cell

        strcpy(objects[obj_count].obj_name, tbl_name);
        strcpy(objects[obj_count].name, name);
        objects[obj_count].sql = strdup(sql);
        strcpy(objects[obj_count].obj_type, type);
        objects[obj_count].root_page = root_page;
        getColumnInfo(obj_count);
        obj_count++;
        free(sql);
    }
    free(page);
    return;
}

/*------------------------------------------------------------------------------------------------------------------------------*/

void parseTableLeafPage(int page_number, int index)
{
    fseek(fp, ((page_number - 1) * header.page_size), SEEK_SET);
    unsigned char *page = malloc(header.page_size);
    if (!page)
        return;

    if (fread(page, 1, header.page_size, fp) != header.page_size)
    {
        fprintf(stderr, "Error reading page %d\n", page_number);
        free(page);
        return;
    }

    unsigned char *ptr = page;
    if (ptr[0] != TABLE_LEAF_PAGE)
    {
        free(page);
        return;
    }

    uint16_t num_cells = (ptr[OFFSET3] << CELL_PTR_ARRAY_OFFSET) | ptr[OFFSET4];
    int col_count = objects[index].column_count;

    // Write CSV header only once per table
    static int printed_headers[MAX_TABLES] = {0};
    if (csv_tables && printed_headers[index] == 0)
    {
        fprintf(csv_tables, "%s", objects[index].name);
        for (int j = 0; j < col_count; j++)
        {
            fprintf(csv_tables, ",%s", objects[index].columns[j].name);
        }
        fprintf(csv_tables, "\n");
        printed_headers[index] = 1;
    }

    for (int i = 0; i < num_cells; i++)
    {
        uint16_t cell_offset = (ptr[CELL_PTR_ARRAY_OFFSET + i * 2] << BYTE_SHIFT_8) | ptr[CELL_PTR_ARRAY_OFFSET + i * 2 + 1];
        unsigned char *cell = ptr + cell_offset;
        int offset = 0, n;

        uint64_t payload_size = readVarint(cell + offset, &n);
        offset += n;
        uint64_t rowid = readVarint(cell + offset, &n);
        offset += n;
        uint64_t header_size = readVarint(cell + offset, &n);
        offset += n;

        unsigned char *header = cell + offset;
        int header_offset = 0;
        uint64_t serial_types[MAX_COLUMNS];

        for (int j = 0; j < col_count; j++)
        {
            serial_types[j] = readVarint(header + header_offset, &n);
            header_offset += n;
        }

        unsigned char *content = cell + offset + header_offset;
        int content_offset = 0;

        // Write data row
        if (csv_tables)
        {
            fprintf(csv_tables, " ");
            for (int j = 0; j < col_count; j++)
            {
                const char *col_type = objects[index].columns[j].type;
                uint64_t stype = serial_types[j];
                char field_str[1024] = {0};

                if (stype == 0 && (strcmp(col_type, "INTEGER") == 0 || strcmp(col_type, "INT") == 0))
                {
                    snprintf(field_str, sizeof(field_str), "%llu", rowid);
                }
                else if (stype >= SERIAL_TYPE_TEXT_MIN && stype % 2 == 1)
                {
                    int len = (stype - SERIAL_TYPE_TEXT_MIN) / 2;
                    memcpy(field_str, content + content_offset, len);
                    field_str[len] = '\0';
                    content_offset += len;
                }
                else if (stype >= SERIAL_TYPE_INT8 && stype <= SERIAL_TYPE_INT64)
                {
                    int size = (stype == SERIAL_TYPE_INT8) ? 1 : (stype == SERIAL_TYPE_INT16) ? 2
                                                             : (stype == SERIAL_TYPE_INT24)   ? 3
                                                             : (stype == SERIAL_TYPE_INT32)   ? 4
                                                             : (stype == SERIAL_TYPE_INT48)   ? 6
                                                                                              : 8;
                    uint64_t val = 0;
                    for (int b = 0; b < size; b++)
                        val = (val << BYTE_SHIFT_8) | content[content_offset + b];
                    snprintf(field_str, sizeof(field_str), "%llu", val);
                    content_offset += size;
                }
                else if (stype == SERIAL_TYPE_FLOAT64)
                {
                    double dval = decodeFloat64(content + content_offset);
                    snprintf(field_str, sizeof(field_str), "%f", dval);
                    content_offset += 8;
                }
                else if (stype == SERIAL_TYPE_RESERVED0 || stype == SERIAL_TYPE_RESERVED1)
                {
                    snprintf(field_str, sizeof(field_str), "%d", stype-SERIAL_TYPE_RESERVED0);
                }
                else
                {
                    strcpy(field_str, "NULL");
                }

                // Quote text in CSV
                if (stype >= SERIAL_TYPE_TEXT_MIN && stype % 2 == 1)
                    fprintf(csv_tables, ",\"%s\"", field_str);
                else
                    fprintf(csv_tables, ",%s", field_str);
            }
            fprintf(csv_tables, "\n");
        }
    }

    free(page);
}