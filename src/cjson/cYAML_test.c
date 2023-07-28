#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cjson/cJSON.h"
#include "cjson/cYAML.h"
#include "tools.h"

bool run_test(const char *name, const char *json, const char *wanted) {
    bool ret = false;
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        fprintf(stderr, "\nERROR: Could not parse JSON:\n%s\n", json);
        goto bail1;
    }
    char *got = cYAML_Print(root);
    if (!got) {
        fprintf(stderr, "\nERROR: cYAML_Print() returned NULL.\n");
        goto bail2;
    }
    if (strcmp(wanted, got) != 0) {
        fprintf(stderr, "\nERROR: Unexpected YAML ouput.\nWanted:\n%s\nGot:\n%s\n", wanted, got);
        goto bail3;
    }
    ret = true;

bail3:
    free(got);
bail2:
    cJSON_Delete(root);
bail1:
    fprintf(stderr, "\nTest %s: %s\n", name, ret ? "PASSED" : "FAILED");
    return ret;
}

int main(int argc, char *argv[]) {
    run_test("top-level object",

             "{ "
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
             "}",

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
             "- item4\n"
             );

    run_test("top-level list",

             "["
             " \"item1\","
             " \"item2\","
             " ["
             "     \"sub1\","
             "     \"sub2\""
             " ],"
             " \"item3\","
             " ["
             "     [ \"subsub1\" ],"
             "     [ 1, 2, { \"val\": 3, \"spelled\": \"three\" }, 4 ],"
             "     [ \"subsub3\" ]"
             " ],"
             " \"item4\""
             "]",

             "---\n"
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
             "- item4\n"
             );

    run_test("empty objects",

             "{ "
             "  \"object\": {},"
             "  \"array\": [],"
             "  \"string\" : \"\""
             "}",

             "---\n"
             "object: {}\n"
             "array: []\n"
             "string: \"\"\n"
             );

    return 0;
}
