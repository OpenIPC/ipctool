#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cjson/cJSON.h"
#include "cjson/cYAML.h"
#include "tools.h"

int main(int argc, char *argv[]) {
    cJSON *root = cJSON_Parse("{ "
        "  \"rom\": {"
        "    \"attr\": \"val\","
        "    \"other\": 1.6"
        "  },"
        "  \"ram\": {"
        "    \"size\": 4096,"
        "    \"name\": \"mRA\bM\""
        "  },"
        "  \"list\" : ["
        "   \"item1\","
        "   \"item2\","
        "   ["
        "       \"sub1\","
        "       \"sub2\""
        "   ],"
        "   \"item3\","
        "   ["
        "       [ \"subsub1\" ],"
        "       [ 1, 2, { \"val\": 3, \"spelled\": \"three\" }, 4 ],"
        "       [ \"subsub3\" ]"
        "   ],"
        "   \"item4\""
        "  ]"
        "}");
    if (!root) {
        fprintf(stderr, "Parse error\n");
        return 1;
    }

    const char *wanted =
        "---\n"
        "rom:\n"
        "  attr: val\n"
        "  other: 1.6\n"
        "ram:\n"
        "  size: 4096\n"
        "  name: \"mRA\\bM\"\n"
        "list:\n"
        "- item1\n"
        "- item2\n"
        "- - sub1\n"
        "  - sub2\n"
        "- item3\n"
        "- - - subsub1\n"
        "  - - 1\n"
        "    - 2\n"
        "    - val: 3\n"
        "      spelled: three\n"
        "    - 4\n"
        "  - - subsub3\n"
        "- item4\n";

    char *got = cYAML_Print(root);
    if (!got) {
        fprintf(stderr, "cYAML_Print() returned NULL.\n");
        return 2;
    }
    if (strcmp(wanted, got) != 0) {
        fprintf(stderr, "Unexpected YAML ouput.\nWanted:\n%s\nGot:\n%s\n\nFAILED\n", wanted, got);
        return 3;
    }

    printf("%s\n\n", got);
    fprintf(stderr, "PASSED\n");

    free(got);
    cJSON_Delete(root);

    return 0;
}
