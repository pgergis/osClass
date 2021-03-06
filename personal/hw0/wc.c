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
    switch(c) {
    case ' ' : // space
    case '\t': // horiz. tab
    case '\n': // newline
    case '\v': // vert. tab
    case '\f': // feed
    case '\r': // carriage return
        return 1;
    }
    return 0;
}

int isNewline(char c) {
    switch(c) {
    case '\n': // newline
    case '\f': // feed
    case '\r': // carriage return
        return 1;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    if(argc < 2) { puts("Err: Missing arg\nUsage: ./wc file_path [file_path]*"); return 0; }

    int bytes;
    int words;
    int lines;

    int totalBytes = 0;
    int totalWords = 0;
    int totalLines = 0;

    int c; // current char in stream
    int p; // previous char in stream

    for(int i = 0; i < argc - 1; i++) {
        FILE *fp;
        if(!(fp = fopen(argv[i+1], "r"))) {
            fprintf(stderr, "wc: %s: No such file or directory\n", argv[i+1]); continue;
        }

        bytes = 0;
        words = 0;
        lines = 0;
        p = ' ';

        c = fgetc(fp);
        while(c != EOF) {
            if(c == EOF) { break; }
            bytes++;
            if(isWhitespace(c) && !isWhitespace(p)) { words++; }
            if(isNewline(c)) { lines++; }
            p = c;
            c = fgetc(fp);
        }

        totalBytes += bytes;
        totalWords += words;
        totalLines += lines;

        printf("%5d%5d%5d %s\n", lines, words, bytes, argv[i+1]);
        fclose(fp);
    }

    if(totalBytes != bytes) { printf("%5d%5d%5d total\n", totalLines, totalWords, totalBytes); }

    return 0;
}
