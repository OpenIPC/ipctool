#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "cYAML.h"

#define internal_malloc malloc
#define internal_free free
#define internal_realloc realloc

typedef struct internal_hooks {
    void *(CJSON_CDECL *allocate)(size_t size);
    void(CJSON_CDECL *deallocate)(void *pointer);
    void *(CJSON_CDECL *reallocate)(void *pointer, size_t size);
} internal_hooks;

static internal_hooks global_hooks = {internal_malloc, internal_free,
                                      internal_realloc};

typedef struct {
    unsigned char *buffer;
    size_t length;
    size_t offset;
    size_t depth; /* current nesting depth (for formatted printing) */
    cJSON_bool noalloc;
    cJSON_bool format; /* is this print a formatted print */
    internal_hooks hooks;
    cJSON_bool next_dash;
} printbuffer;

#define cjson_min(a, b) (((a) < (b)) ? (a) : (b))

#define EXTEND_OUT_TO(length)                                                  \
    output_pointer = ensure(output_buffer, length);                            \
    if (output_pointer == NULL) {                                              \
        return false;                                                          \
    }

#define OUT_CHAR(c)                                                            \
    *output_pointer++ = c;                                                     \
    output_buffer->offset++

/* calculate the new length of the string in a printbuffer and update the offset
 */
static void update_offset(printbuffer *const buffer) {
    const unsigned char *buffer_pointer = NULL;
    if ((buffer == NULL) || (buffer->buffer == NULL)) {
        return;
    }
    buffer_pointer = buffer->buffer + buffer->offset;

    buffer->offset += strlen((const char *)buffer_pointer);
}

/* realloc printbuffer if necessary to have at least "needed" bytes more */
static unsigned char *ensure(printbuffer *const p, size_t needed) {
    unsigned char *newbuffer = NULL;
    size_t newsize = 0;

    if ((p == NULL) || (p->buffer == NULL)) {
        return NULL;
    }

    if ((p->length > 0) && (p->offset >= p->length)) {
        /* make sure that offset is valid */
        return NULL;
    }

    if (needed > INT_MAX) {
        /* sizes bigger than INT_MAX are currently not supported */
        return NULL;
    }

    needed += p->offset + 1;
    if (needed <= p->length) {
        return p->buffer + p->offset;
    }

    if (p->noalloc) {
        return NULL;
    }

    /* calculate new buffer size */
    if (needed > (INT_MAX / 2)) {
        /* overflow of int, use INT_MAX if possible */
        if (needed <= INT_MAX) {
            newsize = INT_MAX;
        } else {
            return NULL;
        }
    } else {
        newsize = needed * 2;
    }

    if (p->hooks.reallocate != NULL) {
        /* reallocate with realloc if available */
        newbuffer = (unsigned char *)p->hooks.reallocate(p->buffer, newsize);
        if (newbuffer == NULL) {
            p->hooks.deallocate(p->buffer);
            p->length = 0;
            p->buffer = NULL;

            return NULL;
        }
    } else {
        /* otherwise reallocate manually */
        newbuffer = (unsigned char *)p->hooks.allocate(newsize);
        if (!newbuffer) {
            p->hooks.deallocate(p->buffer);
            p->length = 0;
            p->buffer = NULL;

            return NULL;
        }
        if (newbuffer) {
            memcpy(newbuffer, p->buffer, p->offset + 1);
        }
        p->hooks.deallocate(p->buffer);
    }
    p->length = newsize;
    p->buffer = newbuffer;

    return newbuffer + p->offset;
}

/* get the decimal point character of the current locale */
static unsigned char get_decimal_point(void) {
#ifdef ENABLE_LOCALES
    struct lconv *lconv = localeconv();
    return (unsigned char)lconv->decimal_point[0];
#else
    return '.';
#endif
}

/* securely comparison of floating-point variables */
static cJSON_bool compare_double(double a, double b) {
    double maxVal = fabs(a) > fabs(b) ? fabs(a) : fabs(b);
    return (fabs(a - b) <= maxVal * DBL_EPSILON);
}

/* Render the number nicely from the given item into a string. */
static cJSON_bool print_number(const cJSON *const item,
                               printbuffer *const output_buffer) {
    unsigned char *output_pointer = NULL;
    double d = item->valuedouble;
    int length = 0;
    size_t i = 0;
    unsigned char number_buffer[26] = {
        0}; /* temporary buffer to print the number into */
    unsigned char decimal_point = get_decimal_point();
    double test = 0.0;

    if (output_buffer == NULL) {
        return false;
    }

    /* This checks for NaN and Infinity */
    if (isnan(d) || isinf(d)) {
        length = sprintf((char *)number_buffer, "null");
    } else {
        /* Try 15 decimal places of precision to avoid nonsignificant nonzero
         * digits */
        length = sprintf((char *)number_buffer, "%1.15g", d);

        /* Check whether the original double can be recovered */
        if ((sscanf((char *)number_buffer, "%lg", &test) != 1) ||
            !compare_double((double)test, d)) {
            /* If not, print with 17 decimal places of precision */
            length = sprintf((char *)number_buffer, "%1.17g", d);
        }
    }

    /* sprintf failed or buffer overrun occurred */
    if ((length < 0) || (length > (int)(sizeof(number_buffer) - 1))) {
        return false;
    }

    /* reserve appropriate space in the output */
    output_pointer = ensure(output_buffer, (size_t)length + sizeof(""));
    if (output_pointer == NULL) {
        return false;
    }

    /* copy the printed number to the output and replace locale
     * dependent decimal point with '.' */
    for (i = 0; i < ((size_t)length); i++) {
        if (number_buffer[i] == decimal_point) {
            output_pointer[i] = '.';
            continue;
        }

        output_pointer[i] = number_buffer[i];
    }
    output_pointer[i] = '\0';

    output_buffer->offset += (size_t)length;

    return true;
}

/* Render the cstring provided to an escaped version that can be printed. */
static cJSON_bool print_string_ptr(const unsigned char *const input,
                                   printbuffer *const output_buffer,
                                   bool is_value) {
    const unsigned char *input_pointer = NULL;
    unsigned char *output = NULL;
    unsigned char *output_pointer = NULL;
    size_t output_length = 0;
    /* numbers of additional characters needed for escaping */
    size_t escape_characters = 0;

    if (output_buffer == NULL) {
        return false;
    }

#if 1
    if (!is_value && output_buffer->depth) {
        size_t ident = (output_buffer->depth - 1) * 2;
        output = ensure(output_buffer, ident);
        if (output == NULL) {
            return false;
        }
        for (size_t i = 0; i < ident; i++) {
            char space = ' ';
            if (output_buffer->next_dash && i == ident - 2) {
                space = '-';
                output_buffer->next_dash = false;
            }
            *output++ = space;
        }
        output_buffer->offset += ident;
    }
#endif

    /* empty string */
    if (input == NULL) {
        const char *qstr = "\"\"";
        if (is_value)
            qstr = " \"\"\n";
        output = ensure(output_buffer, strlen(qstr));
        if (output == NULL) {
            return false;
        }
        strcpy((char *)output, qstr);

        return true;
    }

    bool need_quotes = false;

    /* set "flag" to 1 if something needs to be escaped */
    for (input_pointer = input; *input_pointer; input_pointer++) {
        switch (*input_pointer) {
        case '\"':
        case '\\':
        case '\b':
        case '\f':
        case '\n':
        case '\r':
        case '\t':
            /* one character escape sequence */
            escape_characters++;
            break;
        case ':':
            need_quotes = true;
            break;
        default:
            if (*input_pointer < 32) {
                /* UTF-16 escape sequence uXXXX */
                escape_characters += 3;
            }
            break;
        }
    }
    output_length = (size_t)(input_pointer - input) + escape_characters;

    output = ensure(output_buffer, output_length + 2);
    if (output == NULL) {
        return false;
    }

    /* no characters have to be escaped */
    if (!need_quotes && escape_characters == 0) {
        if (is_value) {
            output[0] = ' ';
        }
        memcpy(output + is_value, input, output_length);
        if (is_value) {
            output_length++;
            output[output_length++] = '\n';
        }
        output[output_length] = '\0';

        return true;
    }

    output_pointer = output;
    if (is_value)
        *output_pointer++ = ' ';
    *output_pointer++ = '\"';
    /* copy the string */
    for (input_pointer = input; *input_pointer != '\0';
         (void)input_pointer++, output_pointer++) {
        if ((*input_pointer > 31) && (*input_pointer != '\"') &&
            (*input_pointer != '\\')) {
            /* normal character, copy */
            *output_pointer = *input_pointer;
        } else {
            /* character needs to be escaped */
            *output_pointer++ = '\\';
            switch (*input_pointer) {
            case '\\':
                *output_pointer = '\\';
                break;
            case '\"':
                *output_pointer = '\"';
                break;
            case '\b':
                *output_pointer = 'b';
                break;
            case '\f':
                *output_pointer = 'f';
                break;
            case '\n':
                *output_pointer = 'n';
                break;
            case '\r':
                *output_pointer = 'r';
                break;
            case '\t':
                *output_pointer = 't';
                break;
            default:
                /* escape and print as unicode codepoint */
                sprintf((char *)output_pointer, "u%04x", *input_pointer);
                output_pointer += 4;
                break;
            }
        }
    }
    *output_pointer++ = '\"';
    if (is_value)
        *output_pointer++ = '\n';
    *output_pointer = '\0';

    return true;
}

/* Invoke print_string_ptr (which is useful) on an item. */
static cJSON_bool print_string(const cJSON *const item, printbuffer *const p) {
    return print_string_ptr((unsigned char *)item->valuestring, p, true);
}

static cJSON_bool print_value(const cJSON *const item,
                              printbuffer *const output_buffer, bool newlined);

static bool print_item(cJSON *current_item, unsigned char *output_pointer,
                       printbuffer *const output_buffer) {
    /* print key */
    if (!print_string_ptr((unsigned char *)current_item->string, output_buffer,
                          false)) {
        return false;
    }
    update_offset(output_buffer);

    EXTEND_OUT_TO(2);
    OUT_CHAR(':');

    switch (current_item->type & 0xFF) {
    case cJSON_Array:
    case cJSON_Object:
        OUT_CHAR('\n');
        break;
    }

    if (!print_value(current_item, output_buffer, false)) {
        return false;
    }

    update_offset(output_buffer);

    return true;
}

/* Render an array to text */
static cJSON_bool print_array(const cJSON *const item,
                              printbuffer *const output_buffer) {
    unsigned char *output_pointer = NULL;
    size_t length = 0;
    cJSON *current_element = item->child;

    if (output_buffer == NULL) {
        return false;
    }

    /* Compose the output array. */
    length = 0;
    output_pointer = ensure(output_buffer, length + 1);
    if (output_pointer == NULL) {
        return false;
    }

    bool ident = true;
    if (((current_element->type) & 0xFF) == cJSON_Object) {
        ident = false;
    }

    if (ident)
        output_buffer->depth++;
    output_buffer->offset += length;

    while (current_element != NULL) {
        output_buffer->next_dash = true;
        switch ((current_element->type) & 0xFF) {
        case cJSON_String:
            if (!print_string_ptr((unsigned char *)current_element->valuestring,
                                  output_buffer, false))
                return false;
            break;
        case cJSON_Array:
            print_item(current_element, output_pointer, output_buffer);
            break;
        default:
            if (!print_value(current_element, output_buffer, true)) {
                return false;
            }
        }
        update_offset(output_buffer);
        if (current_element->next) {
            length = 1;
            output_pointer = ensure(output_buffer, length + 1);
            if (output_pointer == NULL) {
                return false;
            }
            *output_pointer++ = '\n';
            *output_pointer = '\0';
            output_buffer->offset += length;
        }
        current_element = current_element->next;
    }

    output_pointer = ensure(output_buffer, 2);
    if (output_pointer == NULL) {
        return false;
    }
    *output_pointer++ = '\n';
    *output_pointer = '\0';
    if (ident)
        output_buffer->depth--;

    return true;
}

/* Render an object to text. */
static cJSON_bool print_object(const cJSON *const item,
                               printbuffer *const output_buffer) {
    if (output_buffer == NULL) {
        return false;
    }

    cJSON *current_item = item->child;

    /* Compose the output: */
    unsigned char *output_pointer;
    EXTEND_OUT_TO(0);

    output_buffer->depth++;

    while (current_item) {
        if (!print_item(current_item, output_pointer, output_buffer))
            return false;
        current_item = current_item->next;
    }

    EXTEND_OUT_TO(1);
    *output_pointer = '\0';
    output_buffer->depth--;

    return true;
}

/* Render a value to text. */
static cJSON_bool print_value(const cJSON *const item,
                              printbuffer *const output_buffer, bool newlined) {
    unsigned char *output = NULL;

    if ((item == NULL) || (output_buffer == NULL)) {
        return false;
    }

#if 1
    if (newlined && output_buffer->depth) {
        size_t ident = (output_buffer->depth - 1) * 2;
        output = ensure(output_buffer, ident);
        if (output == NULL) {
            return false;
        }
        for (size_t i = 0; i < ident; i++) {
            char space = ' ';
            if (output_buffer->next_dash && i == ident - 2) {
                space = '-';
                output_buffer->next_dash = false;
            }
            *output++ = space;
        }
        output_buffer->offset += ident;
    }
#endif

    switch ((item->type) & 0xFF) {
    case cJSON_NULL:
        output = ensure(output_buffer, 5);
        if (output == NULL) {
            return false;
        }
        strcpy((char *)output, "null");
        return true;

    case cJSON_False:
        output = ensure(output_buffer, 6);
        if (output == NULL) {
            return false;
        }
        strcpy((char *)output, "false");
        return true;

    case cJSON_True:
        output = ensure(output_buffer, 5);
        if (output == NULL) {
            return false;
        }
        strcpy((char *)output, "true");
        return true;

    case cJSON_Number:
        return print_number(item, output_buffer);

    case cJSON_Raw: {
        size_t raw_length = 0;
        if (item->valuestring == NULL) {
            return false;
        }

        raw_length = strlen(item->valuestring) + sizeof("");
        output = ensure(output_buffer, raw_length);
        if (output == NULL) {
            return false;
        }
        memcpy(output, item->valuestring, raw_length);
        return true;
    }

    case cJSON_String:
        return print_string(item, output_buffer);

    case cJSON_Array:
        return print_array(item, output_buffer);

    case cJSON_Object:
        return print_object(item, output_buffer);

    default:
        return false;
    }
}

static unsigned char *print(const cJSON *const item, cJSON_bool format,
                            const internal_hooks *const hooks) {
    static const size_t default_buffer_size = 256;
    printbuffer buffer[1];
    unsigned char *printed = NULL;

    memset(buffer, 0, sizeof(buffer));

    /* create buffer */
    buffer->buffer = (unsigned char *)hooks->allocate(default_buffer_size);
    buffer->length = default_buffer_size;
    buffer->format = format;
    buffer->hooks = *hooks;
    if (buffer->buffer == NULL) {
        goto fail;
    }

    /* print the value */
    if (!print_value(item, buffer, false)) {
        goto fail;
    }
    update_offset(buffer);

    /* check if reallocate is available */
    if (hooks->reallocate != NULL) {
        printed = (unsigned char *)hooks->reallocate(buffer->buffer,
                                                     buffer->offset + 1);
        if (printed == NULL) {
            goto fail;
        }
        buffer->buffer = NULL;
    } else /* otherwise copy the JSON over to a new buffer */
    {
        printed = (unsigned char *)hooks->allocate(buffer->offset + 1);
        if (printed == NULL) {
            goto fail;
        }
        memcpy(printed, buffer->buffer,
               cjson_min(buffer->length, buffer->offset + 1));
        printed[buffer->offset] = '\0'; /* just to be sure */

        /* free the buffer */
        hooks->deallocate(buffer->buffer);
    }

    return printed;

fail:
    if (buffer->buffer != NULL) {
        hooks->deallocate(buffer->buffer);
    }

    if (printed != NULL) {
        hooks->deallocate(printed);
    }

    return NULL;
}

CJSON_PUBLIC(char *) cYAML_Print(const cJSON *item) {
    return (char *)print(item, true, &global_hooks);
}
