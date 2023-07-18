#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cJSON.h"
#include "cYAML.h"


#define TRY(op)                     \
    do {                            \
        if (!(op)) return false;    \
    } while(0)


typedef struct string_buffer {
    char *data;
    size_t len;
    size_t cap;
} string_buffer;


static bool print_value(string_buffer *buf, const cJSON *item, int depth, int dashes, bool in_list);


static string_buffer *strbuf_new() {
    string_buffer *buf = calloc(1, sizeof(string_buffer));
    if (!buf) return NULL;

    buf->cap = 256;
    buf->data = calloc(buf->cap, sizeof(char));
    if (!buf->data) goto bail;

    return buf;

bail:
    free(buf);
    return NULL;
}

static void strbuf_free(string_buffer *buf) {
    free(buf->data);
    free(buf);
}

static bool strbuf_expand(string_buffer *buf, size_t need_cap) {
    /* Make sure to at least double the buffer. */
    if (need_cap < buf->cap) need_cap = buf->cap;

    char *data = realloc(buf->data, need_cap * 2);
    TRY(data);

    buf->data = data;
    buf->cap = need_cap * 2;

    return true;
}

static bool strbuf_push(string_buffer *buf, const char *s) {
    size_t s_len = strlen(s);

    if (buf->len + s_len + 1 > buf->cap) {
        TRY(strbuf_expand(buf, s_len + 1));
    }
    strcpy(buf->data + buf->len, s);
    buf->len += s_len;

    return true;
}

static bool print_string(string_buffer *buf, const char *s) {
    if (!s) {
        TRY(strbuf_push(buf, "\"\""));
        return true;
    }

    static const char *ESCAPES = "\"\\\b\f\n\r\t";
    static const char *REPLACEMENTS = "\"\\bfnrt";

    bool needs_escaping = false;
    for (const char *c = s; *c; c++) {
        if (*c < 32 || *c == ':' || index(ESCAPES, *c) != NULL) {
            needs_escaping = true;
            break;
        }
    }

    if (!needs_escaping) {
        TRY(strbuf_push(buf, s));
        return true;
    }

    TRY(strbuf_push(buf, "\""));
    for (const char *c = s; *c; c++) {
        char *found = index(ESCAPES, *c);
        if (found != NULL) {
            char repl[] = "\\_";
            repl[1] = REPLACEMENTS[found - ESCAPES];
            TRY(strbuf_push(buf, repl));
            continue;
        }

        if (*c < 32) {
            /* Expand non-printable characters. */
            char repl[10];
            TRY(snprintf(repl, sizeof(repl), "\\u%04x", *c) < sizeof(repl));
            TRY(strbuf_push(buf, repl));
            continue;
        }

        char repl[] = "_";
        repl[0] = *c;
        TRY(strbuf_push(buf, repl));
    }
    TRY(strbuf_push(buf, "\""));

    return true;
}

/* Get the decimal point character of the current locale. */
static char get_decimal_point(void) {
#ifdef ENABLE_LOCALES
    struct lconv *lconv = localeconv();
    return (char)lconv->decimal_point[0];
#else
    return '.';
#endif
}

/* Securely compare floating-point variables. */
static bool compare_double(double a, double b) {
    double maxVal = fabs(a) > fabs(b) ? fabs(a) : fabs(b);
    return (fabs(a - b) <= maxVal * DBL_EPSILON);
}

static bool print_number(string_buffer *buf, double d) {
    /* Handle NaN and Infinity. */
    if (isnan(d) || isinf(d)) {
        TRY(strbuf_push(buf, "null"));
        return true;
    }

    char tmp[26];
    /* Try 15 decimal places to avoid nonsignificant nonzero digits. */
    TRY(snprintf(tmp, sizeof(tmp), "%1.15g", d) < sizeof(tmp));

    /* Check whether the original double can be recovered. */
    double test = 0.0;
    if ((sscanf(tmp, "%lg", &test) != 1) || !compare_double(test, d)) {
        /* If not, print with 17 decimal places of precision. */
        TRY(snprintf(tmp, sizeof(tmp), "%1.17g", d) < sizeof(tmp));
    }

    /* Replace locale dependent decimal point with '.'. */
    char decimal_point = get_decimal_point();
    for (char *c = tmp; *c; c++) {
        if (*c == decimal_point) {
            *c = '.';
            break;
        }
    }
    TRY(strbuf_push(buf, tmp));

    return true;
}

static bool do_indent(string_buffer *buf, int depth, int dashes, bool in_list) {
    if (depth <= 0) return true;

    /* In the YAML 1.2.2 spec, top-level lists, and lists one level below the
     * top are both rendered with the dash in column 0. */
    if (in_list) depth--;

    int i = 0;
    for (; i < depth - dashes; i++) {
        TRY(strbuf_push(buf, "  "));
    }
    for (; i < depth; i++) {
        TRY(strbuf_push(buf, "- "));
    }

    return true;
}

static bool print_key_value(string_buffer *buf, const cJSON *item, int depth, int dashes, bool in_list) {
    TRY(item->string);

    TRY(do_indent(buf, depth, dashes, in_list));

    /* print the key */
    TRY(print_string(buf, item->string));
    TRY(strbuf_push(buf, ":"));

    switch (item->type & 0xFF) {
    case cJSON_Object:
    case cJSON_Array:
        TRY(strbuf_push(buf, "\n"));
        TRY(print_value(buf, item, depth + 1, dashes, in_list));
        break;
    default:
        TRY(strbuf_push(buf, " "));
        TRY(print_value(buf, item, 0, 0, false));
    }

    return true;
}

static bool print_object(string_buffer *buf, const cJSON *item, int depth, int dashes, bool in_list) {
    cJSON *it = item->child;

    while (it) {
        TRY(print_key_value(buf, it, depth, dashes, in_list));
        dashes = 0;
        it = it->next;
    }

    return true;
}

static bool print_array(string_buffer *buf, const cJSON *item, int depth, int dashes, bool in_list) {
    cJSON *it = item->child;

    while (it) {
        TRY(print_value(buf, it, depth + 1, dashes + 1, true));
        dashes = 0;
        it = it->next;
    }

    return true;
}

static bool print_value(string_buffer *buf, const cJSON *item, int depth, int dashes, bool in_list) {
    switch ((item->type) & 0xFF) {
    case cJSON_NULL:
        TRY(do_indent(buf, depth, dashes, in_list));
        return strbuf_push(buf, "null\n");

    case cJSON_False:
        TRY(do_indent(buf, depth, dashes, in_list));
        return strbuf_push(buf, "false\n");

    case cJSON_True:
        TRY(do_indent(buf, depth, dashes, in_list));
        return strbuf_push(buf, "true\n");

    case cJSON_Number:
        TRY(do_indent(buf, depth, dashes, in_list));
        TRY(print_number(buf, item->valuedouble));
        return strbuf_push(buf, "\n");

    case cJSON_String:
        TRY(do_indent(buf, depth, dashes, in_list));
        TRY(print_string(buf, item->valuestring));
        return strbuf_push(buf, "\n");

    case cJSON_Array:
        return print_array(buf, item, depth, dashes, in_list);

    case cJSON_Object:
        return print_object(buf, item, depth, dashes, in_list);

    case cJSON_Raw:
        TRY(item->valuestring);
        return strbuf_push(buf, item->valuestring);

    default:
        return false;
    }
}

CJSON_PUBLIC(char *) cYAML_Print(const cJSON *item) {
    char *ret = NULL;

    string_buffer *buf = strbuf_new();
    if (!buf) return NULL;

    if (!strbuf_push(buf, "---\n")) goto bail;
    if (!print_value(buf, item, 0, 0, false)) goto bail;

    ret = strdup(buf->data);

bail:
    strbuf_free(buf);
    return ret;
}
