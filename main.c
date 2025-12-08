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
        exit(1);
    }

    getDBHeader();
    btreeWalk(1, 0, getSQLiteMaster);

    extractUserTables();
    for (int i = 1; i <= obj_count; i++)
    {
        if (getPageType(objects[i].root_page) == TABLE_INTERIOR_PAGE)
            btreeWalk(objects[i].root_page, i, parseTableLeafPage);

        if (getPageType(objects[i].root_page) == TABLE_LEAF_PAGE)
            parseTableLeafPage(objects[i].root_page, i);

        if (getPageType(objects[i].root_page) == INDEX_INTERIOR_PAGE || getPageType(objects[i].root_page) == INDEX_LEAF_PAGE)
            i++;
    }

    extractCellSlack();
    getFreeblockData();

    if (csv_tables)
        fclose(csv_tables);
    closeDBFile();
    return 0;
}

/*-------------------------------------------------------------------------------------------------------------------------------*/