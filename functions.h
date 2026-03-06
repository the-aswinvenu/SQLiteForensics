#ifndef FUNCTIONS_H
#define FUNCTIONS_H

#include <stdint.h>
#include <stdio.h>
/*---------------------- ENUMS --------------------------------------------------------------------------------------------------*/
typedef enum // Enum for constants
{
    HEADER_OFFSET = 100,       // Offset where the first page data starts (after 100-byte header)
    CELL_PTR_ARRAY_OFFSET = 8, // Offset where the array of cell pointers begins
    MAX_SCHEMA_FIELDS = 5,     // Number of columns in sqlite_master table
    VARINT_PARSE_64 = 9,       // Magic number for parsing each of 8 bits of a Varint in 64-bit Varint parsing
    BYTE_SHIFT_VALUE_7 = 7,    // Constant for Byte Shifting the Varint value in 64-it Varint parsing
    BYTE_SHIFT_24 = 24,        // Byte Shift Constant 24
    BYTE_SHIFT_16 = 16,        // Byte Shift Constant 16
    BYTE_SHIFT_8 = 8,          // Byte Shift Constant 8
    MAX_STACK_SIZE = 1000,     // Stack size
    COL_LIMIT = 64,
    OFFSET0 = 0,
    OFFSET1 = 1,
    OFFSET2 = 2,
    OFFSET3 = 3,
    OFFSET4 = 4,
    OFFSET5 = 5,
    OFFSET6 = 6,
    OFFSET8 = 8,
    OFFSET9 = 9,
    OFFSET10 = 10,
    OFFSET11 = 11,
    OFFSET32 = 32,
    INT_PAGE_HDR_OFFSET = 12,
    LEAF_PAGE_HDR_OFFSET = 8,
    MAX_FREELIST_PAGES = 1000,
    MAX_CELL_DATA_LENGTH = 1024,
    MAX_PAGES_PER_OBJECT = 512,
    MAX_PAGES = 63000,
} constValues;

typedef enum // Enum representing different types of B-tree pages
{
    TABLE_INTERIOR_PAGE = 0x05,
    TABLE_LEAF_PAGE = 0x0D,
    INDEX_INTERIOR_PAGE = 0x02,
    INDEX_LEAF_PAGE = 0x0A
} PageType;

typedef enum // Enum representing SQLite serial types used in record headers
{
    SERIAL_TYPE_NULL = 0,
    SERIAL_TYPE_INT8 = 1,
    SERIAL_TYPE_INT16 = 2,
    SERIAL_TYPE_INT24 = 3,
    SERIAL_TYPE_INT32 = 4,
    SERIAL_TYPE_INT48 = 5,
    SERIAL_TYPE_INT64 = 6,
    SERIAL_TYPE_FLOAT64 = 7,
    SERIAL_TYPE_RESERVED0 = 8,
    SERIAL_TYPE_RESERVED1 = 9,
    SERIAL_TYPE_BLOB_MIN = 12,
    SERIAL_TYPE_TEXT_MIN = 13 // TEXT fields begin at 13 and increment by 2
} SerialType;

typedef enum // Enum representing column indexes in the sqlite_master table
{
    COL_TYPE = 0,
    COL_NAME,
    COL_TBLNAME,
    COL_ROOTPAGE,
    COL_SQL
} SchemaColumn;

typedef enum // Enum representing table limit
{
    MAX_TABLES = 128,
    MAX_COLUMNS = 32,
} TableLimits;

/*---------------------- STRUCTS -----------------------------------------------------------------------------------------------*/

typedef struct // Struct for the File Format Header (First 100 Bytes)
{
    char headerString[16];
    short page_size;
    uint8_t write_version;
    uint8_t read_version;
    uint8_t reserved_space;
    uint8_t max_emb_frac;
    uint8_t min_emb_frac;
    uint8_t leaf_frac;
    int change_counter;
    int db_size_pages;
    int freelist_trunk;
    int freelist_count;
    int schema_cookie;
    int schema_format;
    int cache_size;
    int largest_root;
    int text_encoding;
    int user_version;
    int incr_vacuum;
    int app_id;
    uint8_t unused_reserved[20];
    int version_valid;
    int sqlite_version;
    short pTypeVal;
} SQLiteHeader;

typedef struct
{
    char name[64];
    char type[16];
} Column;

typedef struct
{
    char obj_type[32];
    char name[64];     // Table name
    char obj_name[64]; // Associated table name (usually same as name)
    int root_page;     // Root page number of the table B-tree
    char *sql;         // Original CREATE TABLE SQL
    int column_count;
    Column columns[32];
    int owned_pages[MAX_PAGES_PER_OBJECT]; // NEW: Pages owned by this table/index
    int owned_page_count;                  // NEW: Count of those pages
} schemaObjects;

typedef struct
{
    char values[MAX_COLUMNS][MAX_CELL_DATA_LENGTH];
    int column_count;
} ParsedRow;

/*---------------------- EXTERNAL VARIABLES AND OBJECTS -------------------------------------------------------------------------*/

extern SQLiteHeader header;
extern schemaObjects objects[MAX_TABLES];
extern int schemaCount;
extern int obj_count;
extern FILE *fp;
extern FILE *csv_tables;
extern FILE *csv_stalecells;
extern FILE *csv_cellslack;
extern FILE *csv_pageslack;
extern FILE *csv_freeblock;
extern FILE *csv_freelist;
extern FILE *csv_orphanpages;

// extern int visited_pages[];

/*---------------------- FUNCTIONS ----------------------------------------------------------------------------------------------*/

int getDBHeader();                                               // Function to read the Header File format values
int getPageType(int page_number);                                // Function to get the Page Type of the corresponding page number
uint64_t readVarint(const unsigned char *data, int *bytes_read); // Function to read a varint from a byte array
void getSQLiteMaster(int page_number, int id);                   // Function to parse SQLite Master Table (Schema)
// void btreeWalk(int root_page, int idx, void (*leafHandler)(int, int)); // Function to DFS
void btreeWalk(int root_page, int idx, void (*leafHandler)(int, int), int forensic_mode);
void parseTableLeafPage(int page_number, int index); // Function to parse table leaf pages
void extractUserTables();                            // Function to print SQLite Master Table (Schema)
void closeDBFile();                                  // Function to close File
void getColumnInfo(int tableIndex);
int isValidType(const char *colType);
void extractCellSlack();
double decodeFloat64(const unsigned char *bytes);
void carveFreeblocksRecords();
void extractPageSlack();
int parseCell(unsigned char *page, int offset, int table_index, ParsedRow *row, int *consumed_bytes);
void recoverStaleCells();
int getSchemaIndexByPage(int page_number);
void buildOwnedPages();
void parseFreelistPages();
void recoverOrphanPages(/*int page_number, int table_index*/);

/*sample function*/
void parseCellAtOffset(int page_number, int offset);

/*------------------------------------------------------------------------------------------------------------------------------*/

#endif