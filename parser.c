#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
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
    char *str;
    size_t size;
} string_view;

#define sv_fmt "%.*s"
#define sv_args(sv) (int)((sv).size), (sv).str

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
    size_t line;
    size_t lineoffset;
    const char *filename;
} string_stream;

#define STREAM_SPAN(_stream, _size) STRING_VIEW((_stream)->stream.data + (_stream)->stream.offset, (_size))
#define STREAM_CURRENT_CHAR(_stream) STREAM_SPAN((_stream), 1)

typedef enum {
    TOKEN_ERROR,
    TOKEN_EOF,
    TOKEN_NEWLINE,

    TOKEN_SEPARATOR,                    /* separates words */
    TOKEN_NUMBER,                       /* consists of only digits */
    TOKEN_IDENTIFIER,                   /* begins with alphabetic char or underscore, contains only alphanumeric and underscore */
    TOKEN_WORD,                         /* any series of chars that don't match anything else */
    TOKEN_DOUBLE_QUOTED_WORD,           /* " til matching ", allowing \" to skip */
    TOKEN_SINGLE_QUOTED_WORD,           /* ' til next ' */
    TOKEN_GRAVE_QUOTED_WORD,            /* ` til next `, \` is meant to start nesting, command substitution */

    TOKEN_DOLLAR_SINGLE_PAREN_WORD,     /* $( til matching ), command substitution */
    TOKEN_DOLLAR_DOUBLE_PAREN_WORD,     /* $(( til matching )), arithmetic substitution */
    TOKEN_DOLLAR_SQUARE_BRACKET_WORD,   /* $[ til matching ], deprecated arithmetic substitution */
    TOKEN_DOLLAR_BRACE_WORD,            /* ${ til matching }, parameter expansion */

    TOKEN_SINGLE_PAREN_WORD,            /* ( til matching ), subshell statement */
    TOKEN_DOUBLE_PAREN_WORD,            /* (( til matching )), arithmetic statement */
    TOKEN_SINGLE_SQUARE_BRACKET_WORD,   /* [ til matching ], POSIX conditional expression */
    TOKEN_DOUBLE_SQUARE_BRACKET_WORD,   /* [[ til matching ]], conditional expression */
    TOKEN_SINGLE_BRACE_WORD,            /* { til matching }, brace expansion (if there is a comma in there) */

    TOKEN_TILDE_WORD,                   /* word starting with ~, user home expansion */
    TOKEN_EXCLAMATION_WORD,             /* !xxx, history expansion */

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
    TOKEN_HYPHEN,                       /* - */
    TOKEN_DOUBLE_HYPHEN,                /* -- */
    TOKEN_HYPHEN_EQUALS,                /* -= */
    TOKEN_EQUALS,                       /* = */
    TOKEN_DOUBLE_EQUALS,                /* == */
    TOKEN_PLUS,                         /* + */
    TOKEN_DOUBLE_PLUS,                  /* ++ */
    TOKEN_PLUS_EQUALS,                  /* += */

    TOKEN_PIPE,                         /* | */
    TOKEN_DOUBLE_PIPE,                  /* || */
    TOKEN_PIPE_EQUALS,                  /* |= */
    TOKEN_PIPE_AMPERSAND,               /* |& */
    TOKEN_BACKSLASH,                    /* \ */
    TOKEN_DOUBLE_BACKSLASH,             /* \\ */
    TOKEN_BACKSLASH_GRAVE,              /* \` */
    TOKEN_BACKSLASH_EXCLAMATION,        /* \! */
    TOKEN_BACKSLASH_DOLLAR,             /* \$ */
    TOKEN_BACKSLASH_DOUBLE_QUOTE,       /* \" */
    TOKEN_BACKSLASH_NEWLINE,            /* \<newline> */
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
    static_assert(TOKEN_NUM_TOKENS == 69);
    switch (token) {
        case TOKEN_ERROR: return "TOKEN_ERROR";
        case TOKEN_EOF: return "TOKEN_EOF";
        case TOKEN_NEWLINE: return "TOKEN_NEWLINE";
        case TOKEN_SEPARATOR: return "TOKEN_SEPARATOR";

        case TOKEN_NUMBER: return "TOKEN_NUMBER";
        case TOKEN_IDENTIFIER: return "TOKEN_IDENTIFIER";
        case TOKEN_WORD: return "TOKEN_WORD";
        case TOKEN_DOUBLE_QUOTED_WORD: return "TOKEN_DOUBLE_QUOTED_WORD";
        case TOKEN_SINGLE_QUOTED_WORD: return "TOKEN_SINGLE_QUOTED_WORD";
        case TOKEN_GRAVE_QUOTED_WORD: return "TOKEN_GRAVE_QUOTED_WORD";
        case TOKEN_DOLLAR_SINGLE_PAREN_WORD: return "TOKEN_DOLLAR_SINGLE_PAREN_WORD";
        case TOKEN_DOLLAR_DOUBLE_PAREN_WORD: return "TOKEN_DOLLAR_DOUBLE_PAREN_WORD";
        case TOKEN_DOLLAR_SQUARE_BRACKET_WORD: return "TOKEN_DOLLAR_SQUARE_BRACKET_WORD";
        case TOKEN_DOLLAR_BRACE_WORD: return "TOKEN_DOLLAR_BRACE_WORD";

        case TOKEN_SINGLE_PAREN_WORD: return "TOKEN_SINGLE_PAREN_WORD";
        case TOKEN_DOUBLE_PAREN_WORD: return "TOKEN_DOUBLE_PAREN_WORD";
        case TOKEN_SINGLE_SQUARE_BRACKET_WORD: return "TOKEN_SINGLE_SQUARE_BRACKET_WORD";
        case TOKEN_DOUBLE_SQUARE_BRACKET_WORD: return "TOKEN_DOUBLE_SQUARE_BRACKET_WORD";
        case TOKEN_SINGLE_BRACE_WORD: return "TOKEN_SINGLE_BRACE_WORD";
        case TOKEN_TILDE_WORD: return "TOKEN_TILDE_WORD";
        case TOKEN_EXCLAMATION_WORD: return "TOKEN_EXCLAMATION_WORD";

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
        case TOKEN_HYPHEN: return "TOKEN_HYPHEN";
        case TOKEN_DOUBLE_HYPHEN: return "TOKEN_DOUBLE_HYPHEN";
        case TOKEN_HYPHEN_EQUALS: return "TOKEN_HYPHEN_EQUALS";
        case TOKEN_EQUALS: return "TOKEN_EQUALS";
        case TOKEN_DOUBLE_EQUALS: return "TOKEN_DOUBLE_EQUALS";
        case TOKEN_PLUS: return "TOKEN_PLUS";
        case TOKEN_DOUBLE_PLUS: return "TOKEN_DOUBLE_PLUS";
        case TOKEN_PLUS_EQUALS: return "TOKEN_PLUS_EQUALS";

        case TOKEN_PIPE: return "TOKEN_PIPE";
        case TOKEN_DOUBLE_PIPE: return "TOKEN_DOUBLE_PIPE";
        case TOKEN_PIPE_EQUALS: return "TOKEN_PIPE_EQUALS";
        case TOKEN_PIPE_AMPERSAND: return "TOKEN_PIPE_AMPERSAND";
        case TOKEN_BACKSLASH: return "TOKEN_BACKSLASH";
        case TOKEN_DOUBLE_BACKSLASH: return "TOKEN_DOUBLE_BACKSLASH";
        case TOKEN_BACKSLASH_GRAVE: return "TOKEN_BACKSLASH_GRAVE";
        case TOKEN_BACKSLASH_EXCLAMATION: return "TOKEN_BACKSLASH_EXCLAMATION";
        case TOKEN_BACKSLASH_DOLLAR: return "TOKEN_BACKSLASH_DOLLAR";
        case TOKEN_BACKSLASH_DOUBLE_QUOTE: return "TOKEN_BACKSLASH_DOUBLE_QUOTE";
        case TOKEN_BACKSLASH_NEWLINE: return "TOKEN_BACKSLASH_NEWLINE";
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
    static_assert(TOKEN_NUM_TOKENS == 69);
    switch (c) {
        case '!': return TOKEN_EXCLAMATION;
        case '@': return TOKEN_AT;
        case '#': return TOKEN_HASH;
        case '$': return TOKEN_DOLLAR;
        case '%': return TOKEN_PERCENT;
        case '^': return TOKEN_CIRCUMFLEX;
        case '&': return TOKEN_AMPERSAND;
        case '*': return TOKEN_ASTERISK;
        case '-': return TOKEN_HYPHEN;
        case '=': return TOKEN_EQUALS;
        case '+': return TOKEN_PLUS;

        case '|': return TOKEN_PIPE;
        case '\\': return TOKEN_BACKSLASH;
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
    static_assert(TOKEN_NUM_TOKENS == 69);
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
        case ';':
        case '\\':
            return true;

        default:
            return false;
    }
}

token_t double_byte_token(char c1, char c2) {
    static_assert(TOKEN_NUM_TOKENS == 69);
    if (c2 == c1) {
        switch (c1) {
            case '&': return TOKEN_DOUBLE_AMPERSAND;
            case '-': return TOKEN_DOUBLE_HYPHEN;
            case '=': return TOKEN_DOUBLE_EQUALS;
            case '+': return TOKEN_DOUBLE_PLUS;
            case '|': return TOKEN_DOUBLE_PIPE;
            case '\\': return TOKEN_DOUBLE_BACKSLASH;
            case '<': return TOKEN_DOUBLE_LESS_THAN;
            case '>': return TOKEN_DOUBLE_GREATER_THAN;
        }
    }

    switch (c2) {
        case '=':
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
            break;

        case '&':
            switch (c1) {
                case '|': return TOKEN_PIPE_AMPERSAND;
                case ';': return TOKEN_SEMI_COLON_AMPERSAND;
            }
            break;

        case '\\':
            switch (c1) {
                case '`': return TOKEN_BACKSLASH_GRAVE;
                case '!': return TOKEN_BACKSLASH_EXCLAMATION;
                case '$': return TOKEN_BACKSLASH_DOLLAR;
                case '"': return TOKEN_BACKSLASH_DOUBLE_QUOTE;
                case '\n': return TOKEN_BACKSLASH_NEWLINE;
            }
            break;
    }

    return TOKEN_ERROR;
}

bool possible_three_byte_token(char c1, char c2) {
    static_assert(TOKEN_NUM_TOKENS == 69);
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
    if (size < in->stream.size) return true;
    if (in->streamfd == -1) return false;
    if (!_stream_ensure(in, size)) return false;

    ssize_t red = read(in->streamfd, in->stream.data + in->stream.size, in->stream.capacity - in->stream.size);
    if (red == -1) return false;
    if (red == 0) {
        in->eof = true;
        return false;
    }

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
    if (!s.ok) return (char_result){.ok = false};
    return (char_result){.ok = true, .c = *s.view.str};
}

token_span consume_token(string_stream *in, token_t type, size_t bytes) {
    string_result s = consume_stream(in, bytes);
    if (!s.ok) {
        if (in->eof) {
            return TOKEN(TOKEN_EOF, STREAM_CURRENT_CHAR(in));
        } else {
            return TOKEN(TOKEN_ERROR, STREAM_CURRENT_CHAR(in));
        }
    }
    return TOKEN(type, s.view);
}

char matching_char(char c) {
    switch (c) {
        case '(': return ')';
        case '[': return ']';
        case '{': return '}';
    }
    return '\0';
}

ssize_t find_matching(string_stream *in, char c, size_t offset) {
    char match = matching_char(c);
    if (!match) return 0;

    size_t ahead = offset;
    while (true) {
        char_result peek = peek_ahead_stream(in, ahead++);
        if (!peek.ok) return -1;
        else if (peek.c == '\\') ahead ++;
        else if (peek.c == match) return ahead - offset - 1;
        else switch (peek.c) {
            case '(':
            case '[':
            case '{':
            {
                ssize_t found = find_matching(in, peek.c, ahead);
                if (found == -1) return -1;
                ahead += found + 1;
            }; break;

            case ')':
            case ']':
            case '}':
                return -1;
        }
    }
    UNREACHABLE("");
}

bool is_whitespace(char c) {
    /* FIXME use $IFS */
    switch (c) {
        case ' ':
        case '\t':
            return true;
    }
    return false;
}

#define error(s, fmt, ...) do { fprintf(stderr, "%s:%d:%d: ERROR: " fmt "\n", (s)->filename ? (s)->filename : "stdin", (int)(s)->line, (int)((s)->stream.offset - (s)->lineoffset), ##__VA_ARGS__); abort(); } while (false)

token_span whitespace(string_stream *in) {
    size_t length = 0;
    char_result next = peek_ahead_stream(in, length++);
    if (!next.ok || !is_whitespace(next.c)) {
        if (!next.ok) {
            if (in->eof) UNREACHABLE("whitespace() expected whitespace at start, found EOF");
            UNREACHABLE("whitespace() expected whitespace at start, got read error: %s: %s (%d)", strerrorname_np(errno), strerrordesc_np(errno), errno);
        }
        UNREACHABLE("whitespace() expected whitespace at start, got '%c' (%u)", next.c, next.c);
        return TOKEN(TOKEN_ERROR, STREAM_CURRENT_CHAR(in));
    }

    while (true) {
        next = peek_ahead_stream(in, length++);
        if (!next.ok) break;
        if (is_whitespace(next.c)) continue;
        break;
    }

    return consume_token(in, TOKEN_SEPARATOR, length - 1);
}

token_span double_quote(string_stream *in) {
    size_t length = 0;
    char_result next = peek_ahead_stream(in, length++);
    if (!next.ok || next.c != '"') {
        if (!next.ok) {
            if (in->eof) UNREACHABLE("double_quote() expected '\"' at start, found EOF");
            UNREACHABLE("double_quote() expected '\"' at start, got read error: %s: %s (%d)", strerrorname_np(errno), strerrordesc_np(errno), errno);
        }
        UNREACHABLE("double_quote() expected '\"' at start, got '%c' (%u)", next.c, next.c);
    }

    while (true) {
        next = peek_ahead_stream(in, length++);
        if (next.c == '\\') length++;
        if (!next.ok || next.c == '"') break;
    }

    if (!next.ok) error(in, "Could not find matching \"");

    read_stream(in);
    token_span ret = consume_token(in, TOKEN_DOUBLE_QUOTED_WORD, length - 2);
    read_stream(in);
    return ret;
}

token_span grave_quote(string_stream *in) {
    size_t length = 0;
    char_result next = peek_ahead_stream(in, length++);
    if (!next.ok || next.c != '`') {
        if (!next.ok) {
            if (in->eof) UNREACHABLE("grave_quote() expected '`' at start, found EOF");
            UNREACHABLE("grave_quote() expected '`' at start, got read error: %s: %s (%d)", strerrorname_np(errno), strerrordesc_np(errno), errno);
        }
        UNREACHABLE("grave_quote() expected '`' at start, got '%c' (%u)", next.c, next.c);
    }

    while (true) {
        next = peek_ahead_stream(in, length++);
        /* FIXME a \` should match the next \` */
        if (next.c == '\\') length++;
        if (!next.ok || next.c == '`') break;
    }

    if (!next.ok) error(in, "Could not find matching `");

    read_stream(in);
    token_span ret = consume_token(in, TOKEN_GRAVE_QUOTED_WORD, length - 2);
    read_stream(in);
    return ret;
}

token_span single_quote(string_stream *in) {
    size_t length = 0;
    char_result next = peek_ahead_stream(in, length++);
    if (!next.ok || next.c != '\'') {
        if (!next.ok) {
            if (in->eof) UNREACHABLE("single_quote() expected '\'' at start, found EOF");
            UNREACHABLE("single_quote() expected '\'' at start, got read error: %s: %s (%d)", strerrorname_np(errno), strerrordesc_np(errno), errno);
        }
        UNREACHABLE("single_quote() expected '\'' at start, got '%c' (%u)", next.c, next.c);
    }

    while (true) {
        next = peek_ahead_stream(in, length++);
        if (!next.ok || next.c == '\'') break;
    }

    if (!next.ok) error(in, "Could not find matching '");

    read_stream(in);
    token_span ret = consume_token(in, TOKEN_SINGLE_QUOTED_WORD, length - 2);
    read_stream(in);
    return ret;
}

token_span newline(string_stream *in) {
    size_t length = 0;
    char_result next = peek_ahead_stream(in, length++);
    if (!next.ok || next.c != '\n') {
        return TOKEN(TOKEN_ERROR, STREAM_CURRENT_CHAR(in));
    }

    while (true) {
        in->line ++;
        in->lineoffset = length;
        next = peek_ahead_stream(in, length++);
        if (!next.ok) break;
        if (next.c == '\n') continue;
        break;
    }

    return consume_token(in, TOKEN_NEWLINE, length - 1);
}

token_span parse_token(string_stream *in) {
    while (true) {
        char_result peek = peek_stream(in);
        if (!peek.ok) {
            if (in->eof) {
                return TOKEN(TOKEN_EOF, STREAM_CURRENT_CHAR(in));
            }
            error(in, "Read error: %s: %s (%d)", strerrorname_np(errno), strerrordesc_np(errno), errno);
        }

        if (is_whitespace(peek.c)) {
            return whitespace(in);
        }

        switch (peek.c) {
            case '\n': return newline(in);
            case '\\':
            {
                // if next char is \n then add no tokens, but count lines

            }; break;

            case '"': return double_quote(in);
            case '`': return grave_quote(in);
            case '\'': return single_quote(in);

            case '$':
            {
                size_t offset = 1;
                char_result peek;
                peek = peek_ahead_stream(in, offset++);
                if (peek.ok) {
                    switch (peek.c) {
                        case '(':
                        {
                            char_result next;
                            next = peek_ahead_stream(in, offset);

                            if (next.ok && next.c == '(') {
                                ssize_t found = find_matching(in, next.c, ++offset);
                                if (found == -1) {
                                    fprintf(stderr, "Could not find closing %c\n", matching_char(next.c));
                                    return TOKEN(TOKEN_ERROR, STREAM_CURRENT_CHAR(in));
                                }

                                next = peek_ahead_stream(in, offset + found + 1);
                                if (!next.ok) break;
                                if (next.c != matching_char(peek.c)) {
                                    fprintf(stderr, "Could not find second closing %c\n", matching_char(peek.c));
                                    return TOKEN(TOKEN_ERROR, STREAM_CURRENT_CHAR(in));
                                }
                                consume_stream(in, 3);
                                token_span ret = consume_token(in, TOKEN_DOLLAR_DOUBLE_PAREN_WORD, found);
                                consume_stream(in, 2);
                                return ret;
                            }
                        }; /* fallthrough */

                        case '{':
                        case '[':
                        {
                            ssize_t found = find_matching(in, peek.c, offset);
                            if (found == -1) {
                                fprintf(stderr, "Could not find closing %c\n", matching_char(peek.c));
                                return TOKEN(TOKEN_ERROR, STREAM_CURRENT_CHAR(in));
                            }
                            token_t token = TOKEN_ERROR;
                            switch (peek.c) {
                                case '(': token = TOKEN_DOLLAR_SINGLE_PAREN_WORD; break;
                                case '{': token = TOKEN_DOLLAR_BRACE_WORD; break;
                                case '[': token = TOKEN_DOLLAR_SQUARE_BRACKET_WORD; break;
                            }
                            if (token == TOKEN_ERROR) UNREACHABLE("Unknown token type for $%c", peek.c);
                            consume_stream(in, 2);
                            token_span ret = consume_token(in, token, found);
                            read_stream(in);
                            return ret;
                        }; break;
                    }
                } else {
                    fprintf(stderr, "Could not find character after $\n");
                    return TOKEN(TOKEN_ERROR, STREAM_CURRENT_CHAR(in));
                }
            }; break;

            case '(':
            case '[':
            case '{':
            {
                char_result next;
                next = peek_ahead_stream(in, 1);
                char matching = matching_char(peek.c);
                if (!matching) UNREACHABLE("Unhandled matching char for %c", peek.c);
                token_t token = TOKEN_ERROR;
                if (next.ok) {
                    size_t offset;
                    if (next.c == peek.c) {
                        offset = 2;
                        switch (peek.c) {
                            case '(': token = TOKEN_DOUBLE_PAREN_WORD; break;
                            case '[': token = TOKEN_DOUBLE_SQUARE_BRACKET_WORD; break;
                            case '{':
                                /* There is no {{, it is two single braces */
                                token = TOKEN_SINGLE_BRACE_WORD;
                                offset = 1;
                                break;
                        }
                    } else {
                        offset = 1;
                        switch (peek.c) {
                            case '(': token = TOKEN_SINGLE_PAREN_WORD; break;
                            case '[': token = TOKEN_SINGLE_SQUARE_BRACKET_WORD; break;
                            case '{': token = TOKEN_SINGLE_BRACE_WORD; break;
                        }
                    }
                    if (token == TOKEN_ERROR) UNREACHABLE("Unhandled token type for %c", peek.c);

                    ssize_t found = find_matching(in, peek.c, offset);
                    if (peek.c == '{') {
                        bool found_comma = false;
                        size_t ahead = 0;
                        while (found == -1 || ahead < (unsigned)found) {
                            next = peek_ahead_stream(in, offset + ahead++);
                            if (!next.ok) break;
                            if (next.c == ',') {
                                found_comma = true;
                                break;
                            }
                        }
                        /* If there was no , inbetween {..}, then it should be a word, not expansion */
                        if (!found_comma) break;
                    }
                    if (found == -1) {
                        fprintf(stderr, "Could not find matching %c\n", matching_char(peek.c));
                        return TOKEN(TOKEN_ERROR, STREAM_CURRENT_CHAR(in));
                    }

                    if (offset == 2) {
                        next = peek_ahead_stream(in, offset + found);
                        if (!next.ok || next.c != matching_char(peek.c)) {
                            fprintf(stderr, "Could not find second closing %c\n", matching_char(peek.c));
                            return TOKEN(TOKEN_ERROR, STREAM_CURRENT_CHAR(in));
                        }
                    }
                    consume_stream(in, offset);
                    token_span ret = consume_token(in, token, found);
                    consume_stream(in, offset);
                    return ret;
                } else {
                    fprintf(stderr, "Could not find character after %c\n", peek.c);
                    return TOKEN(TOKEN_ERROR, STREAM_CURRENT_CHAR(in));
                }
            }; break;


            case '~':
            {
                bool valid = true;
                size_t offset = 1;
                while (true) {
                    char_result next = peek_ahead_stream(in, offset++);
                    if (!next.ok) break;
                    if (isalpha(next.c)) continue;
                    switch (next.c) {
                        case '+':
                        case '-':
                            if (offset == 2) {
                                next = peek_ahead_stream(in, offset++);
                                if (!next.ok) break;
                                if (next.c != ' ' && next.c != '\t' && next.c != '\n') {
                                    printf("invalid at %c\n", next.c);
                                    valid = false;
                                }
                            } else {
                                printf("invalid at %c\n", next.c);
                                valid = false;
                            }
                            break;

                        case ' ':
                        case '\t':
                        case '\n':
                            break;

                        default:
                            printf("invalid at %c\n", next.c);
                            valid = false;
                    }
                    break;
                }

                if (valid) {
                    read_stream(in);
                    return consume_token(in, TOKEN_TILDE_WORD, offset - 2);
                }
            }; break;
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

        if (iscntrl(peek.c) || peek.c < 0) {
            UNREACHABLE("Control character %u", peek.c);
        }

        if (isalpha(peek.c) || peek.c == '_') {
            size_t ahead = 1;
            char_result next;
            while (true) {
                next = peek_ahead_stream(in, ahead++);
                if (!next.ok) break;
                if (next.c != '_' && !isalnum(next.c)) break;
            }
            return consume_token(in, TOKEN_IDENTIFIER, ahead - 1);
        } else if (isdigit(peek.c)) {
            size_t ahead = 1;
            char_result next;
            while (true) {
                next = peek_ahead_stream(in, ahead++);
                if (!next.ok) break;
                if (next.c != '_' && !isdigit(next.c)) break;
            }
            return consume_token(in, TOKEN_NUMBER, ahead - 1);
        } else {
            size_t ahead = 1;
            char_result next;
            while (true) {
                next = peek_ahead_stream(in, ahead++);
                if (!next.ok) break;
                if (possible_two_byte_token(next.c) || single_byte_token(next.c) != TOKEN_ERROR) {
                    break;
                }
                if (iscntrl(next.c) || next.c < 0) {
                    break;
                }
            }
            return consume_token(in, TOKEN_WORD, ahead - 1);
        }

        fprintf(stderr, "Could not parse %c (%u)\n", peek.c, peek.c);
        return TOKEN(TOKEN_ERROR, STREAM_CURRENT_CHAR(in));
    }
    UNREACHABLE("");
}

int main() {
    string_stream in = {.streamfd = STDIN_FILENO};

    while (true) {
        token_span token = parse_token(&in);
        switch (token.type) {
            case TOKEN_NEWLINE:
            case TOKEN_SEPARATOR:
                printf("%s\n", token_str(token.type));
                continue;
            default:
        }
        printf("%s => \"" sv_fmt "\" (@ %ld)\n", token_str(token.type), sv_args(token.span), token.span.str ? (token.span.str - in.stream.data) : -1);
        if (token.type == TOKEN_EOF || token.type == TOKEN_ERROR) break;
    }

    if (in.stream.data) free(in.stream.data);
    return 0;
}
