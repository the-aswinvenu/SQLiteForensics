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