#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define ABORT() do { abort(); } while (0)

#define UNIMPLEMENTED(msg) do { fprintf(stderr, "%s:%d: UNIMPLEMENTED: %s", __FILE__, __LINE__, msg); ABORT(); } while (false)
#define UNREACHABLE(fmt, ...) do { fprintf(stderr, "%s:%d: UNREACHABLE: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__); ABORT(); } while (false)
#define TODO(msg) do { fprintf(stderr, "%s:%d: UNIMPLEMENTED: %s", __FILE__, __LINE__, msg); ABORT(); } while (false)

typedef struct {
    char c;
    bool ok;
} char_result;

typedef struct {
    const char *str;
    size_t size;
} string_view;

typedef struct {
    string_view view;
    bool ok;
} string_result;

#define STRING_VIEW(_str, _size) (string_view){.str = (_str), .size = (_size)}

typedef struct {
    struct {
        char *data;
        size_t size;
        size_t capacity;
        size_t offset;
    } stream;
    int streamfd;
    bool eof;
} string_stream;

#define STREAM_SPAN(_stream, _size) STRING_VIEW((_stream)->stream.data + (_stream)->stream.offset, (_size))
#define STREAM_CURRENT_CHAR(_stream) STREAM_SPAN((_stream), 1)

typedef enum {
    TOKEN_ERROR,
    TOKEN_EOF,
    TOKEN_SPACE,
    TOKEN_TAB,
    TOKEN_NEWLINE,

    TOKEN_IDENTIFIER,                   /* begins with alphabetic char or underscore, contains only alphanumeric and underscore */

    TOKEN_TILDE,                        /* ~ */
    TOKEN_GRAVE,                        /* ` */
    TOKEN_EXCLAMATION,                  /* ! */
    TOKEN_EXCLAMATION_EQUALS,           /* != */
    TOKEN_AT,                           /* @ */
    TOKEN_HASH,                         /* # */
    TOKEN_DOLLAR,                       /* $ */
    TOKEN_PERCENT,                      /* % */
    TOKEN_PERCENT_EQUALS,               /* %= */
    TOKEN_CIRCUMFLEX,                   /* ^ */
    TOKEN_CIRCUMFLEX_EQUALS,            /* ^= */
    TOKEN_AMPERSAND,                    /* & */
    TOKEN_DOUBLE_AMPERSAND,             /* && */
    TOKEN_AMPERSAND_EQUALS,             /* &= */
    TOKEN_ASTERISK,                     /* * */
    TOKEN_ASTERISK_EQUALS,              /* *= */
    TOKEN_PARENTHESIS_OPEN,             /* ( */
    TOKEN_PARENTHESIS_CLOSE,            /* ) */
    TOKEN_DOUBLE_PARENTHESIS_OPEN,      /* (( */
    TOKEN_DOUBLE_PARENTHESIS_CLOSE,     /* )) */
    TOKEN_HYPHEN,                       /* - */
    TOKEN_DOUBLE_HYPHEN,                /* -- */
    TOKEN_HYPHEN_EQUALS,                /* -= */
    TOKEN_EQUALS,                       /* = */
    TOKEN_DOUBLE_EQUALS,                /* == */
    TOKEN_PLUS,                         /* + */
    TOKEN_DOUBLE_PLUS,                  /* ++ */
    TOKEN_PLUS_EQUALS,                  /* += */

    TOKEN_SQUARE_BRACKET_OPEN,          /* [ */
    TOKEN_SQUARE_BRACKET_CLOSE,         /* ] */
    TOKEN_DOUBLE_SQUARE_BRACKET_OPEN,   /* [[ */
    TOKEN_DOUBLE_SQUARE_BRACKET_CLOSE,  /* ]] */
    TOKEN_BRACE_OPEN,                   /* { */
    TOKEN_BRACE_CLOSE,                  /* } */
    TOKEN_DOUBLE_BRACE_OPEN,            /* {{ */
    TOKEN_DOUBLE_BRACE_CLOSE,           /* }} */
    TOKEN_PIPE,                         /* | */
    TOKEN_DOUBLE_PIPE,                  /* || */
    TOKEN_PIPE_EQUALS,                  /* |= */
    TOKEN_PIPE_AMPERSAND,               /* |& */
    TOKEN_COLON,                        /* : */
    TOKEN_SEMI_COLON,                   /* ; */
    TOKEN_SEMI_COLON_AMPERSAND,         /* ;& */
    TOKEN_DOUBLE_SEMI_COLON_AMPERSAND,  /* ;;& */
    TOKEN_SINGLE_QUOTE,                 /* ' */
    TOKEN_DOUBLE_QUOTE,                 /* " */

    TOKEN_LESS_THAN,                    /* < */
    TOKEN_GREATER_THAN,                 /* > */
    TOKEN_DOUBLE_LESS_THAN,             /* << */
    TOKEN_DOUBLE_GREATER_THAN,          /* >> */
    TOKEN_LESS_THAN_EQUAL,              /* <= */
    TOKEN_GREATER_THAN_EQUAL,           /* >= */
    TOKEN_SLASH,                        /* / */
    TOKEN_SLASH_EQUALS,                 /* /= */
    TOKEN_QUESTION,                     /* ? */

    TOKEN_NUM_TOKENS
} token_t;

const char *token_str(token_t token) {
    static_assert(TOKEN_NUM_TOKENS == 61);
    switch (token) {
        case TOKEN_ERROR: return "TOKEN_ERROR";
        case TOKEN_EOF: return "TOKEN_EOF";
        case TOKEN_SPACE: return "TOKEN_SPACE";
        case TOKEN_TAB: return "TOKEN_TAB";
        case TOKEN_NEWLINE: return "TOKEN_NEWLINE";

        case TOKEN_IDENTIFIER: return "TOKEN_IDENTIFIER";

        case TOKEN_TILDE: return "TOKEN_TILDE";
        case TOKEN_GRAVE: return "TOKEN_GRAVE";
        case TOKEN_EXCLAMATION: return "TOKEN_EXCLAMATION";
        case TOKEN_EXCLAMATION_EQUALS: return "TOKEN_EXCLAMATION_EQUALS";
        case TOKEN_AT: return "TOKEN_AT";
        case TOKEN_HASH: return "TOKEN_HASH";
        case TOKEN_DOLLAR: return "TOKEN_DOLLAR";
        case TOKEN_PERCENT: return "TOKEN_PERCENT";
        case TOKEN_PERCENT_EQUALS: return "TOKEN_PERCENT_EQUALS";
        case TOKEN_CIRCUMFLEX: return "TOKEN_CIRCUMFLEX";
        case TOKEN_CIRCUMFLEX_EQUALS: return "TOKEN_CIRCUMFLEX_EQUALS";
        case TOKEN_AMPERSAND: return "TOKEN_AMPERSAND";
        case TOKEN_DOUBLE_AMPERSAND: return "TOKEN_DOUBLE_AMPERSAND";
        case TOKEN_AMPERSAND_EQUALS: return "TOKEN_AMPERSAND_EQUALS";
        case TOKEN_ASTERISK: return "TOKEN_ASTERISK";
        case TOKEN_ASTERISK_EQUALS: return "TOKEN_ASTERISK_EQUALS";
        case TOKEN_PARENTHESIS_OPEN: return "TOKEN_PARENTHESIS_OPEN";
        case TOKEN_PARENTHESIS_CLOSE: return "TOKEN_PARENTHESIS_CLOSE";
        case TOKEN_DOUBLE_PARENTHESIS_OPEN: return "TOKEN_DOUBLE_PARENTHESIS_OPEN";
        case TOKEN_DOUBLE_PARENTHESIS_CLOSE: return "TOKEN_DOUBLE_PARENTHESIS_CLOSE";
        case TOKEN_HYPHEN: return "TOKEN_HYPHEN";
        case TOKEN_DOUBLE_HYPHEN: return "TOKEN_DOUBLE_HYPHEN";
        case TOKEN_EQUALS: return "TOKEN_EQUALS";
        case TOKEN_DOUBLE_EQUALS: return "TOKEN_DOUBLE_EQUALS";
        case TOKEN_PLUS: return "TOKEN_PLUS";
        case TOKEN_DOUBLE_PLUS: return "TOKEN_DOUBLE_PLUS";

        case TOKEN_SQUARE_BRACKET_OPEN: return "TOKEN_SQUARE_BRACKET_OPEN";
        case TOKEN_SQUARE_BRACKET_CLOSE: return "TOKEN_SQUARE_BRACKET_CLOSE";
        case TOKEN_DOUBLE_SQUARE_BRACKET_OPEN: return "TOKEN_DOUBLE_SQUARE_BRACKET_OPEN";
        case TOKEN_DOUBLE_SQUARE_BRACKET_CLOSE: return "TOKEN_DOUBLE_SQUARE_BRACKET_CLOSE";
        case TOKEN_BRACE_OPEN: return "TOKEN_BRACE_OPEN";
        case TOKEN_BRACE_CLOSE: return "TOKEN_BRACE_CLOSE";
        case TOKEN_DOUBLE_BRACE_OPEN: return "TOKEN_DOUBLE_BRACE_OPEN";
        case TOKEN_DOUBLE_BRACE_CLOSE: return "TOKEN_DOUBLE_BRACE_CLOSE";
        case TOKEN_PIPE: return "TOKEN_PIPE";
        case TOKEN_DOUBLE_PIPE: return "TOKEN_DOUBLE_PIPE";
        case TOKEN_PIPE_EQUALS: return "TOKEN_PIPE_EQUALS";
        case TOKEN_PIPE_AMPERSAND: return "TOKEN_PIPE_AMPERSAND";
        case TOKEN_COLON: return "TOKEN_COLON";
        case TOKEN_SEMI_COLON: return "TOKEN_SEMI_COLON";
        case TOKEN_SEMI_COLON_AMPERSAND: return "TOKEN_SEMI_COLON_AMPERSAND";
        case TOKEN_DOUBLE_SEMI_COLON_AMPERSAND: return "TOKEN_DOUBLE_SEMI_COLON_AMPERSAND";
        case TOKEN_SINGLE_QUOTE: return "TOKEN_SINGLE_QUOTE";
        case TOKEN_DOUBLE_QUOTE: return "TOKEN_DOUBLE_QUOTE";

        case TOKEN_LESS_THAN: return "TOKEN_LESS_THAN";
        case TOKEN_GREATER_THAN: return "TOKEN_GREATER_THAN";
        case TOKEN_DOUBLE_LESS_THAN: return "TOKEN_DOUBLE_LESS_THAN";
        case TOKEN_DOUBLE_GREATER_THAN: return "TOKEN_DOUBLE_GREATER_THAN";
        case TOKEN_LESS_THAN_EQUAL: return "TOKEN_LESS_THAN_EQUAL";
        case TOKEN_GREATER_THAN_EQUAL: return "TOKEN_GREATER_THAN_EQUAL";
        case TOKEN_SLASH: return "TOKEN_SLASH";
        case TOKEN_SLASH_EQUALS: return "TOKEN_SLASH_EQUALS";
        case TOKEN_QUESTION: return "TOKEN_QUESTION";

        default: UNREACHABLE("Got token %u", token);
    }
}

token_t single_byte_token(char c) {
    static_assert(TOKEN_NUM_TOKENS == 61);
    switch (c) {
        case ' ': return TOKEN_SPACE;
        case '\t': return TOKEN_TAB;
        case '\n': return TOKEN_NEWLINE;

        case '`': return TOKEN_GRAVE;
        case '!': return TOKEN_EXCLAMATION;
        case '@': return TOKEN_AT;
        case '#': return TOKEN_HASH;
        case '$': return TOKEN_DOLLAR;
        case '%': return TOKEN_PERCENT;
        case '^': return TOKEN_CIRCUMFLEX;
        case '&': return TOKEN_AMPERSAND;
        case '*': return TOKEN_ASTERISK;
        case '(': return TOKEN_PARENTHESIS_OPEN;
        case ')': return TOKEN_PARENTHESIS_CLOSE;
        case '-': return TOKEN_HYPHEN;
        case '=': return TOKEN_EQUALS;
        case '+': return TOKEN_PLUS;

        case '[': return TOKEN_SQUARE_BRACKET_OPEN;
        case ']': return TOKEN_SQUARE_BRACKET_CLOSE;
        case '{': return TOKEN_BRACE_OPEN;
        case '}': return TOKEN_BRACE_CLOSE;
        case '|': return TOKEN_PIPE;
        case ':': return TOKEN_COLON;
        case ';': return TOKEN_SEMI_COLON;
        case '\'': return TOKEN_SINGLE_QUOTE;
        case '"': return TOKEN_DOUBLE_QUOTE;

        case '<': return TOKEN_LESS_THAN;
        case '>': return TOKEN_GREATER_THAN;
        case '/': return TOKEN_SLASH;
        case '?': return TOKEN_QUESTION;

        default: return TOKEN_ERROR;
    }
}

bool possible_two_byte_token(char c) {
    static_assert(TOKEN_NUM_TOKENS == 61);
    switch (c) {
        case '!':
        case '%':
        case '^':
        case '&':
        case '*':
        case '-':
        case '=':
        case '+':
        case '|':
        case '<':
        case '>':
        case '/':
        case '(':
        case ')':
        case '[':
        case ']':
        case ';':
            return true;

        default:
            return false;
    }
}

token_t double_byte_token(char c1, char c2) {
    static_assert(TOKEN_NUM_TOKENS == 61);
    if (c2 == '=') {
        switch (c1) {
            case '!': return TOKEN_EXCLAMATION_EQUALS;
            case '%': return TOKEN_PERCENT_EQUALS;
            case '^': return TOKEN_CIRCUMFLEX_EQUALS;
            case '&': return TOKEN_AMPERSAND_EQUALS;
            case '*': return TOKEN_ASTERISK_EQUALS;
            case '-': return TOKEN_HYPHEN_EQUALS;
            case '=': return TOKEN_DOUBLE_EQUALS;
            case '+': return TOKEN_PLUS_EQUALS;
            case '|': return TOKEN_PIPE_EQUALS;
            case '<': return TOKEN_LESS_THAN_EQUAL;
            case '>': return TOKEN_GREATER_THAN_EQUAL;
            case '/': return TOKEN_SLASH_EQUALS;
        }
    } else if (c2 == c1) {
        switch (c1) {
            case '&': return TOKEN_DOUBLE_AMPERSAND;
            case '(': return TOKEN_DOUBLE_PARENTHESIS_OPEN;
            case ')': return TOKEN_DOUBLE_PARENTHESIS_CLOSE;
            case '-': return TOKEN_DOUBLE_HYPHEN;
            case '=': return TOKEN_DOUBLE_EQUALS;
            case '+': return TOKEN_DOUBLE_PLUS;
            case '[': return TOKEN_DOUBLE_SQUARE_BRACKET_OPEN;
            case ']': return TOKEN_DOUBLE_SQUARE_BRACKET_CLOSE;
            case '|': return TOKEN_DOUBLE_PIPE;
            case '<': return TOKEN_DOUBLE_LESS_THAN;
            case '>': return TOKEN_DOUBLE_GREATER_THAN;
        }
    } else if (c2 == '&') {
        switch (c1) {
            case '|': return TOKEN_PIPE_AMPERSAND;
            case ';': return TOKEN_SEMI_COLON_AMPERSAND;
        }
    }
    return TOKEN_ERROR;
}

bool possible_three_byte_token(char c1, char c2) {
    static_assert(TOKEN_NUM_TOKENS == 61);
    return c1 == ';' && c2 == ';';
}

token_t triple_byte_token(char c1, char c2, char c3) {
    if (c1 == ';' && c2 == ';' && c3 == '&') return TOKEN_DOUBLE_SEMI_COLON_AMPERSAND;
    return TOKEN_ERROR;
}

typedef struct {
    token_t type;
    string_view span;
} token_span;

#define TOKEN(_type, _span) (token_span) {.type = (_type), .span = (_span)}

typedef struct {
    token_span *data;
    size_t size;
    size_t capacity;
} token_array;

bool _token_array_ensure(token_array *arr, size_t size) {
    if (!arr) return false;
    if (size <= arr->capacity) return true;

    size_t cap = arr->capacity ? arr->capacity : 16;
    while (size > cap) cap *= 2;

    arr->data = realloc(arr->data, sizeof(*arr->data) * cap);
    memset(arr->data + arr->capacity, '\0', cap - arr->capacity);

    arr->capacity = cap;

    return true;
}

bool token_array_append(token_array *arr, token_span token) {
    if (!arr) return false;
    if (!_token_array_ensure(arr, arr->size + 1)) return false;

    arr->data[arr->size ++] = token;
    return true;
}

bool _stream_ensure(string_stream *in, size_t size) {
    if (!in) return false;
    if (size <= in->stream.capacity) return true;

    size_t cap = in->stream.capacity ? in->stream.capacity : 1024;
    while (size > cap) cap *= 2;

    in->stream.data = realloc(in->stream.data, sizeof(*in->stream.data) * cap);
    memset(in->stream.data + in->stream.capacity, '\0', cap - in->stream.capacity);

    in->stream.capacity = cap;

    return true;
}

bool fill_stream(string_stream *in, size_t size) {
    if (!in) return false;
    if (in->eof) return false;
    if (in->stream.offset < in->stream.size) return true;
    if (!_stream_ensure(in, size)) return false;

    ssize_t red = read(in->streamfd, in->stream.data + in->stream.size, in->stream.capacity - in->stream.size);
    if (red == -1) return false;
    if (red == 0) in->eof = true;

    in->stream.size += red;
    return true;
}

char_result peek_ahead_stream(string_stream *in, size_t ahead) {
    if (in->stream.offset + ahead >= in->stream.size) {
        if (!fill_stream(in, in->stream.size + ahead + 1)) return (char_result){.ok = false};
    }
    return (char_result){.ok = true, .c = in->stream.data[in->stream.offset + ahead]};
}

char_result peek_stream(string_stream *in) {
    return peek_ahead_stream(in, 0);
}

string_result consume_stream(string_stream *in, size_t bytes) {
    if (in->stream.offset >= in->stream.size) {
        if (!fill_stream(in, in->stream.size + bytes + 1)) return (string_result){.ok = false};
    }
    string_view span = STREAM_SPAN(in, bytes);
    in->stream.offset += bytes;
    return (string_result){.ok = true, .view = span};
}

char_result read_stream(string_stream *in) {
    string_result s = consume_stream(in, 1);
    if (!s.ok || !s.view.size) return (char_result){.ok = false};
    return (char_result){.ok = true, .c = *s.view.str};
}

token_span consume_token(string_stream *in, token_t type, size_t bytes) {
    string_result s = consume_stream(in, bytes);
    if (!s.ok || !s.view.size) return TOKEN(TOKEN_ERROR, STREAM_CURRENT_CHAR(in));
    return TOKEN(type, s.view);
}

token_span parse_token(string_stream *in) {

    while (true) {
        char_result peek = peek_stream(in);
        if (!peek.ok) {
            if (in->eof) {
                return TOKEN(TOKEN_EOF, STREAM_CURRENT_CHAR(in));
            }
            return TOKEN(TOKEN_ERROR, STREAM_CURRENT_CHAR(in));
        }

        if (possible_two_byte_token(peek.c)) {
            char_result next = peek_ahead_stream(in, 1);
            if (next.ok) {
                if (possible_three_byte_token(peek.c, next.c)) {
                    char_result third = peek_ahead_stream(in, 2);
                    if (third.ok) {
                        token_t triple_byte = triple_byte_token(peek.c, next.c, third.c);
                        if (triple_byte != TOKEN_ERROR) {
                            return consume_token(in, triple_byte, 3);
                        }
                    }
                }

                token_t double_byte = double_byte_token(peek.c, next.c);
                if (double_byte != TOKEN_ERROR) {
                    return consume_token(in, double_byte, 2);
                }
            }
        }

        token_t single_byte = single_byte_token(peek.c);
        if (single_byte != TOKEN_ERROR) {
            return consume_token(in, single_byte, 1);
        }

        if (isalnum(peek.c) || peek.c == '_') {
            printf("start of word '%c' (%u)\n", peek.c, peek.c);
        } else {
            printf("unhandled char '%c' (%u)\n", peek.c, peek.c);
        }
        read_stream(in);
    }
    return TOKEN(TOKEN_ERROR, STREAM_CURRENT_CHAR(in));
}

int main() {
    string_stream in = {.streamfd = STDIN_FILENO};

    printf("TOKEN_NUM_TOKENS = %d\n", TOKEN_NUM_TOKENS);
    while (true) {
        token_span token = parse_token(&in);
        printf("%s\n", token_str(token.type));
        if (token.type == TOKEN_EOF || token.type == TOKEN_ERROR) break;
    }

    if (in.stream.data) free(in.stream.data);
    return 0;
}
