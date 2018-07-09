/* Implements Ubuntu-style 'wc' function, without options:
 * Should print: '[newline count] [word count] [byte count]'
 * A word is a non-zero-length sequence of chars delimited by whitespace.
 */

/* Methodology:
 * 1. Open file in read mode, return error if necessary
 * 2. Read char by char, increment bytes each time
 * 3. Increment lines at '\n', increment words on whitespace
 * 4. Close file
 */

#include <stdio.h>

int isWhitespace(char c) {
    if (   c  == ' '  // space
        || c  == '\t' // horiz. tab
        || c  == '\n' // newline
        || c  == '\v' // vert. tab
        || c  == '\f' // feed
        || c  == '\r' // carriage return
        ){
        return 1;
    }
    return 0;
}

int isNewline(char c) {
    if (   c  == '\n' // newline
        || c  == '\f' // feed
        || c  == '\r' // carriage return
        ){
        return 1;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    if( argc != 2 ) { puts("Err: Missing arg\nUsage: ./wc file_path"); return 0; }

    FILE *fp;
    fp = fopen(argv[1], "r");

    int bytes = 0;
    int words = 0;
    int lines = 0;
    int c; // current char in stream
    int p = -1; // previous char in stream

    for (int i = 0; c != EOF ; i++) {
        c = fgetc(fp);
        if( c == EOF ) { break; }
        bytes++;
        if( isWhitespace(c) && !isWhitespace(p)) { words++; }
        if( isNewline(c) )    { lines++; }
        p = c;
    }

    printf("  %d  %d  %d  [%s]\n", lines, words, bytes, argv[1]);
    fclose(fp);
    return 0;
}
