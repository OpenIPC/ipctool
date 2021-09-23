#include <ctype.h>
#include <dirent.h>
#include <string.h>
#include <unistd.h>

#include "anjoy.h"

#include "chipid.h"
#include "tools.h"

bool is_anjoy_board() {
    if (!access("/opt/ch/star.flag", 0)) {
        strcpy(board_manufacturer, "Anjoy");
        return true;
    }
    return false;
}

bool gather_anjoy_board_info() {
    DIR *dir = opendir("/opt/ch");
    if (!dir)
        return false;

    bool found = false;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (*entry->d_name && *entry->d_name == 'm' &&
            (entry->d_name[1] == 'c' || entry->d_name[1] == 's' ||
             entry->d_name[1] == 't')) {
            int len = strlen(entry->d_name) - 5;
            if (len < 0 || strcmp(entry->d_name + len, ".flag"))
                continue;

            strcpy(board_id, entry->d_name);
            board_id[len] = 0;
            for (int i = 0; i < len; i++)
                board_id[i] = toupper(board_id[i]);
            found = true;
            break;
        }
    }
    closedir(dir);

    return found;
}
