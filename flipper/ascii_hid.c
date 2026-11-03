#include "ascii_hid.h"

bool civ_ascii_to_hid(char ascii, uint8_t* modifier, uint8_t* key) {
    if(!modifier || !key) return false;
    if(ascii >= 'a' && ascii <= 'z') {
        *modifier = 0;
        *key = 4 + ascii - 'a';
        return true;
    }
    if(ascii >= 'A' && ascii <= 'Z') {
        *modifier = 2;
        *key = 4 + ascii - 'A';
        return true;
    }
    if(ascii >= '1' && ascii <= '9') {
        *modifier = 0;
        *key = 0x1E + ascii - '1';
        return true;
    }
    if(ascii == '0') {
        *modifier = 0;
        *key = 0x27;
        return true;
    }
    switch(ascii) {
    case ' ':
        *modifier = 0;
        *key = 0x2C;
        break;
    case '\t':
        *modifier = 0;
        *key = 0x2B;
        break;
    case '!':
        *modifier = 2;
        *key = 0x1E;
        break;
    case '@':
        *modifier = 2;
        *key = 0x1F;
        break;
    case '#':
        *modifier = 2;
        *key = 0x20;
        break;
    case '$':
        *modifier = 2;
        *key = 0x21;
        break;
    case '%':
        *modifier = 2;
        *key = 0x22;
        break;
    case '^':
        *modifier = 2;
        *key = 0x23;
        break;
    case '&':
        *modifier = 2;
        *key = 0x24;
        break;
    case '*':
        *modifier = 2;
        *key = 0x25;
        break;
    case '(':
        *modifier = 2;
        *key = 0x26;
        break;
    case ')':
        *modifier = 2;
        *key = 0x27;
        break;
    case '-':
    case '_':
        *modifier = ascii == '_' ? 2 : 0;
        *key = 0x2D;
        break;
    case '=':
    case '+':
        *modifier = ascii == '+' ? 2 : 0;
        *key = 0x2E;
        break;
    case '[':
    case '{':
        *modifier = ascii == '{' ? 2 : 0;
        *key = 0x2F;
        break;
    case ']':
    case '}':
        *modifier = ascii == '}' ? 2 : 0;
        *key = 0x30;
        break;
    case '\\':
    case '|':
        *modifier = ascii == '|' ? 2 : 0;
        *key = 0x31;
        break;
    case ';':
    case ':':
        *modifier = ascii == ':' ? 2 : 0;
        *key = 0x33;
        break;
    case '\'':
    case '"':
        *modifier = ascii == '"' ? 2 : 0;
        *key = 0x34;
        break;
    case '`':
    case '~':
        *modifier = ascii == '~' ? 2 : 0;
        *key = 0x35;
        break;
    case ',':
    case '<':
        *modifier = ascii == '<' ? 2 : 0;
        *key = 0x36;
        break;
    case '.':
    case '>':
        *modifier = ascii == '>' ? 2 : 0;
        *key = 0x37;
        break;
    case '/':
    case '?':
        *modifier = ascii == '?' ? 2 : 0;
        *key = 0x38;
        break;
    default:
        return false;
    }
    return true;
}
