#include <stdio.h>
#include <stdlib.h>
#include <winsock2.h>
#include "functions.h"

/*------------------------------------------------------------------------------------------------------------------------------*/

int main()
{
    char filename[256];
    printf("Enter SQLite file path: ");
    fgets(filename, sizeof(filename), stdin); // SQLite DB File input
    filename[strcspn(filename, "\n")] = '\0';

    fp = fopen(filename, "rb"); // Reading File as Binary
    if (!fp)
    {
        perror("Error opening file");
        return 1;
    }

    csv_tables = fopen("tables.csv", "w");
    if (!csv_tables)
    {
        perror("Failed to open tables.csv");
        exit(EXIT_FAILURE);
    }

    csv_stalecells = fopen("stalecelldata.csv", "w");
    if (!csv_stalecells)
    {
        perror("Failed to open .csv file");
        exit(EXIT_FAILURE);
    }

    csv_cellslack = fopen("cellslackdata.csv", "w");
    if (!csv_cellslack)
    {
        perror("Failed to open .csv file");
        exit(EXIT_FAILURE);
    }

    csv_pageslack = fopen("pageslackdata.csv", "w");
    if (!csv_pageslack)
    {
        perror("Failed to open .csv file");
        exit(EXIT_FAILURE);
    }

    csv_freeblock = fopen("freeblockdata.csv", "w");
    if (!csv_freeblock)
    {
        perror("Failed to open .csv file");
        exit(EXIT_FAILURE);
    }

    csv_freelist = fopen("freelistdata.csv", "w");
    if (!csv_freelist)
    {
        perror("Failed to open .csv file");
        exit(EXIT_FAILURE);
    }

    csv_orphanpages = fopen("orphanpagedata.csv", "w");
    if (!csv_orphanpages)
    {
        perror("Failed to open .csv file");
        exit(EXIT_FAILURE);
    }
    getDBHeader();
    btreeWalk(1, 0, getSQLiteMaster, 0);
    buildOwnedPages();

    extractUserTables();
    for (int i = 1; i <= obj_count; i++)
    {
        if (getPageType(objects[i - 1].root_page) == TABLE_INTERIOR_PAGE)
            btreeWalk(objects[i - 1].root_page, (i - 1), parseTableLeafPage, 0);

        if (getPageType(objects[i - 1].root_page) == TABLE_LEAF_PAGE)
            parseTableLeafPage(objects[i - 1].root_page, (i - 1));

        if (getPageType(objects[i - 1].root_page) == INDEX_INTERIOR_PAGE || getPageType(objects[i - 1].root_page) == INDEX_LEAF_PAGE)
            continue;
    }

    // parseFreelistPages();
    // carveFreeblocksRecords();
    // extractCellSlack();
    // extractPageSlack();
    // recoverStaleCells();
    recoverOrphanPages();

    // for (int i = 1770; i < 1810; i++)
    // {
    //     parseCellAtOffset(4, i);
    // }
    // parseCellAtOffset(936, 677);
    if (csv_tables)
        fclose(csv_tables);

    if (csv_stalecells)
        fclose(csv_stalecells);

    if (csv_cellslack)
        fclose(csv_cellslack);

    if (csv_pageslack)
        fclose(csv_pageslack);

    if (csv_freeblock)
        fclose(csv_freeblock);

    if (csv_freelist)
        fclose(csv_freelist);

    if (csv_orphanpages)
        fclose(csv_orphanpages);

    closeDBFile();
    return 0;
}

/*-------------------------------------------------------------------------------------------------------------------------------*/