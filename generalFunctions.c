#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include "functions.h"

SQLiteHeader header;
FILE *fp = NULL;
FILE *csv_tables = NULL;

/*------------------------------------------------------------------------------------------------------------------------------*/

int getDBHeader() // Function to get the Database File Header
{
    if (fread(&header, 1, sizeof(header), fp) != sizeof(header)) // Reading First 100-Bytes to the Struct
    {
        fprintf(stderr, "Failed to read header\n");
        closeDBFile();
        return 1;
    }

    header.page_size = ntohs(header.page_size);
    header.change_counter = ntohl(header.change_counter);
    header.db_size_pages = ntohl(header.db_size_pages);
    header.freelist_trunk = ntohl(header.freelist_trunk);
    header.freelist_count = ntohl(header.freelist_count);
    header.schema_cookie = ntohl(header.schema_cookie);
    header.schema_format = ntohl(header.schema_format);
    header.cache_size = ntohl(header.cache_size);
    header.largest_root = ntohl(header.largest_root);
    header.text_encoding = ntohl(header.text_encoding);
    header.user_version = ntohl(header.user_version);
    header.incr_vacuum = ntohl(header.incr_vacuum);
    header.app_id = ntohl(header.app_id);
    header.version_valid = ntohl(header.version_valid);
    header.sqlite_version = ntohl(header.sqlite_version);

    return 0;
}

/*------------------------------------------------------------------------------------------------------------------------------*/

int getPageType(int page_number) // Function to get the Page Type of the corresponding page number
{
    uint8_t pByte;
    fseek(fp, ((page_number - 1) * header.page_size), SEEK_SET);
    if (fread(&pByte, 1, 1, fp) != 1)
    {
        fprintf(stderr, "Failed to read page header\n");
        fclose(fp);
        return 1;
    }
    if (pByte == TABLE_INTERIOR_PAGE)
    {
        // printf("\n %d Table interior page", page_number);
        return TABLE_INTERIOR_PAGE;
    }
    else if (pByte == TABLE_LEAF_PAGE)
    {
        // printf("\n %d Table leaf page", page_number);
        return TABLE_LEAF_PAGE;
    }
    else if (pByte == INDEX_INTERIOR_PAGE)
    {
        // printf("\n %d Index interior page", page_number);
        return INDEX_INTERIOR_PAGE;
    }
    else if (pByte == INDEX_LEAF_PAGE)
    {
        // printf("\n %d Index leaf page", page_number);
        return INDEX_LEAF_PAGE;
    }
}

/*------------------------------------------------------------------------------------------------------------------------------*/

uint64_t readVarint(const unsigned char *data, int *bytes_read) // Function to read a varint from a byte array
{
    if (!data)
    {
        *bytes_read = 0;
        return 0;
    }

    uint64_t result = 0;
    int i;
    for (i = 0; i < VARINT_PARSE_64; i++)
    {
        result = (result << BYTE_SHIFT_VALUE_7) | (data[i] & 0x7F);
        if (!(data[i] & 0x80))
            break; // Exit if MSB is not set
    }
    *bytes_read = i + 1;
    return result;
}

/*------------------------------------------------------------------------------------------------------------------------------*/

void getColumnInfo(int tableIndex)
{
    char *sql = objects[tableIndex].sql;
    if (!sql || strncmp(sql, "CREATE TABLE", 12) != 0)
        return;

    char *openParen = strchr(sql, '(');
    char *closeParen = strrchr(sql, ')');
    if (!openParen || !closeParen || openParen > closeParen)
        return;

    char colDefs[2048];
    strncpy(colDefs, openParen + 1, closeParen - openParen - 1);
    colDefs[closeParen - openParen - 1] = '\0';

    char *token = strtok(colDefs, ",");
    int col = 0;

    while (token && col < MAX_COLUMNS)
    {
        while (*token == ' ')
            token++; // skip leading space

        char colName[64] = {0}, colType[64] = {0};
        sscanf(token, "%63s %63s", colName, colType); // read column name and type

        if (isValidType(colType) == 1)
        {
            strncpy(objects[tableIndex].columns[col].name, colName, sizeof(objects[tableIndex].columns[col].name));
            strncpy(objects[tableIndex].columns[col].type, colType, sizeof(objects[tableIndex].columns[col].type));
            col++;
        }

        token = strtok(NULL, ",");
    }

    objects[tableIndex].column_count = col;
}

/*------------------------------------------------------------------------------------------------------------------------------*/

int isValidType(const char *colType)
{
    if (!colType)
        return 0;
    // Convert to uppercase (local copy)
    char upperType[64];
    int i;
    for (i = 0; colType[i] && i < sizeof(upperType) - 1; i++)
        upperType[i] = toupper((unsigned char)colType[i]);
    upperType[i] = '\0';

    // Valid SQL type prefixes (case-insensitive)
    const char *validPrefixes[] = {
        "INT", "INTEGER", "BIGINT", "SMALLINT", "TINYINT", "MEDIUMINT", "INT2", "INT8", "TEXT", "CHAR", "VARCHAR",
        "CLOB", "NCHAR", "VARYINGCHARACTER", "REAL", "FLOAT", "DOUBLE", "DOUBLEPRECISION", "NUMERIC", "DECIMAL",
        "DEZIMAL", "BOOLEAN", "DATE", "DATETIME", "DTIME", "TIMESTAMP", "STRING", "BLOB", "NVARCHAR", "CHARACTER",
        "VCHAR", "VARCHAR2", "LONGTEXT", "MEDIUMTEXT", "TINYTEXT", "MONEY", "FLOAT4", "FLOAT8", "REALNUM",
        "INTUNSIGNED", "NUMBER", "BINARY", "BINARYDATA", "Transformable", "ENUM", "UUID"};

    int numTypes = sizeof(validPrefixes) / sizeof(validPrefixes[0]);
    for (i = 0; i < numTypes; i++)
    {
        if (strncmp(upperType, validPrefixes[i], strlen(validPrefixes[i])) == 0)
            return 1;
    }

    return 0;
}

/*------------------------------------------------------------------------------------------------------------------------------*/

double decodeFloat64(const unsigned char *bytes)
{
    uint64_t val = 0;
    for (int i = 0; i < 8; i++)
        val = (val << 8) | bytes[i];

    double d;
    memcpy(&d, &val, sizeof(double));
    return d;
}

/*------------------------------------------------------------------------------------------------------------------------------*/

void closeDBFile()
{
    if (fp)
        fclose(fp);
}