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
    bool ok = true;

    ok &= run_test("top-level object",

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

    ok &= run_test("top-level list",

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

    ok &= run_test("empty objects",

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

    /* Non-ASCII used to take down the whole print, not just one string:
     * `char` is signed, so every UTF-8 byte tested as < 32, fell into the
     * \u expansion and overran its scratch buffer, and cYAML_Print returned
     * NULL. ipctool then emitted nothing at all in its default output mode.
     * Sensor names and U-Boot environments do carry non-ASCII, so this was
     * reachable in ordinary use. UTF-8 is valid YAML and passes through. */
    ok &= run_test("utf-8 passes through",

             "{ \"note\": \"em dash \\u2014 here\" }",

             "---\n"
             "note: em dash \xe2\x80\x94 here\n"
             );

    /* Bytes that are not valid UTF-8 reach cYAML from raw flash -- U-Boot
     * environments are passed through untranscoded -- and must not be emitted
     * raw, or one bad byte in one value stops the whole document parsing. */
    ok &= run_test("invalid utf-8 is escaped, not passed through",

             "{ \"note\": \"latin1 \xe9 here\" }",

             "---\n"
             "note: \"latin1 \\u00e9 here\"\n"
             );

    /* Control characters still get expanded. */
    ok &= run_test("control characters are escaped",

             "{ \"note\": \"bell \\u0007 here\" }",

             "---\n"
             "note: \"bell \\u0007 here\"\n"
             );

    return ok ? 0 : 1;
}
