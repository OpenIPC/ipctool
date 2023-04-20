#include <ctype.h>
#include <dirent.h>
#include <string.h>
#include <unistd.h>

#include "anjoy.h"

#include "chipid.h"
#include "tools.h"

bool is_anjoy_board() {
    if (!access("/opt/ch/star.flag", 0)) {
        return true;
    }
    return false;
}

bool gather_anjoy_board_info(cJSON *j_inner) {
    ADD_PARAM("vendor", "Anjoy");

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

            for (int i = 0; i < len; i++)
                entry->d_name[i] = toupper(entry->d_name[i]);
            entry->d_name[len] = 0;
	    ADD_PARAM("model", entry->d_name);
            found = true;
            break;
        }
    }
    closedir(dir);

    return found;
}
