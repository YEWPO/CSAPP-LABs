#include "cachelab.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

#define ll long long
#define MAX_SIZE 50

const char *helpInfo = "Usage: ./csim [-hv] -s <num> -E <num> -b <num> -t "
                    "<file>\nOptions:\n  -h Print this help message.\n  -v "
                    "Optional verbose flag.\n  -s <num> Number of set index "
                    "bits.\n  -E <num> Number of lines per set.\n  -b <num> "
                    "Number of block offset bits.\n  -t <file> Trace file.\n\n"
                    "Examples :\n linux> ./csim -s 4 -E 1 -b 4 -t "
                    "traces/yi.trace\n linux>  ./csim -v -s 8 -E 2 "
                    "-b 4 -t traces/yi.trace\n ";

ll nowTime;
bool DEBUG;
int opt;
int S, s;
int E;
int B, b;
char *fileAddr;
FILE *file;
int hitCount, missCount, evictionCount;
char inputStream[MAX_SIZE];

typedef struct {
    bool vaild;
    int tag;
    int lastTime;
}Cache, *pCache;

typedef struct {
    int tag;
    int setIndex;
    int offset;
}Msg, *pMsg;

typedef struct {
    int cmd;
    Msg msg;
    int siz;
}Instr, *pInstr;

pCache cache;

void init() {
    cache = (pCache) malloc(S * E * sizeof(Cache));

    for (int i = 0; i < S * E; ++i) {
        cache[i].vaild = false;
        cache[i].tag = cache[i].lastTime = 0;
    }
}

int readCmd() {
    int ptr = 0;
    char ch = inputStream[ptr];
    while (!isalpha(ch)) {
        ch = inputStream[++ptr];
    }

    switch (ch) {
        case 'I':
            return 0;
        case 'L':
            return 1;
        case 'S':
            return 2;
        case 'M':
            return 3;
        default:
            return -1;
    }
}

int hexToDec(char ch) {
    if (isdigit(ch)) {
        return ch - '0';
    }
    return ch - 'a' + 10;
}

int readMsg() {
    int ptr = 0;
    int s = 0;
    char ch = inputStream[++ptr];

    while (!isxdigit(ch)) {
        ch = inputStream[++ptr];
    }

    while (isxdigit(ch)) {
        s = (s << 4) + hexToDec(ch);
        ch = inputStream[++ptr];
    }

    return s;
}

int readSiz() {
    int ptr = 0;
    int s = 0;
    char ch = inputStream[ptr];

    while (ch != ',') {
        ch = inputStream[++ptr];
    }

    while (!isdigit(ch)) {
        ch = inputStream[++ptr];
    }

    while (isdigit(ch)) {
        s = (s << 3) + (s << 1) + ch - '0';
        ch = inputStream[++ptr];
    }

    return s;
}

Msg transMsg(int msg) {
    Msg ret;
    ret.tag = ret.setIndex = ret.offset = 0;

    for (int i = 0; i < b; ++i) {
        ret.offset = (ret.offset << 1) + (msg & 1);
        msg >>= 1;
    }

    for (int i = 0; i < s; ++i) {
        ret.setIndex = (ret.setIndex << 1) + (msg & 1);
        msg >>= 1;
    }

    ret.tag = msg;

    return ret;
}

int blockIndex(int setIndx, int i) {
    return setIndx * E + i;
}

void loadCache(Msg msg) {
    for (int i = 0; i < E; ++i) {
        int index = blockIndex(msg.setIndex, i);
        if (cache[index].vaild && msg.tag == cache[index].tag) {
            if (DEBUG) {
                fputs("hit", stdout);
            }
            hitCount++;
            nowTime++;
            cache[index].lastTime = nowTime;
            return;
        }
    }

    if (DEBUG) {
        fputs("miss", stdout);
    }
    missCount++;

    for (int i = 0; i < E; ++i) {
        int index = blockIndex(msg.setIndex, i);
        if (!cache[index].vaild) {
            cache[index].tag = msg.tag;
            cache[index].vaild = true;
            nowTime++;
            cache[index].lastTime = nowTime;
            return;
        }
    }

    int earliestIndex, lastTime = nowTime + 1;

    for (int i = 0; i < E; ++i) {
        int index = blockIndex(msg.setIndex, i);

        if (cache[index].lastTime < lastTime) {
            earliestIndex = index;
            lastTime = cache[index].lastTime;
        }
    }

    if (DEBUG) {
        fputs(" eviction", stdout);
    }

    evictionCount++;
    cache[earliestIndex].tag = msg.tag;
    cache[earliestIndex].vaild = true;
    nowTime++;
    cache[earliestIndex].lastTime = nowTime;
}

void readFile() {
    while (fgets(inputStream, MAX_SIZE, file)) {
        pInstr instr = (pInstr) malloc(sizeof(Instr));

        if (DEBUG) {
            fputs(inputStream, stdout);
            fputc(' ', stdout);
        }

        instr->cmd = readCmd();
        instr->msg = transMsg(readMsg());
        instr->siz = readSiz();

        switch(instr->cmd) {
            case 1:
            case 2:
                loadCache(instr->msg);
                if (DEBUG) {
                    fputc(('\n'), stdout);
                }
                break;
            case 3:
                loadCache(instr->msg);
                if (DEBUG) {
                    fputc((' '), stdout);
                }
                loadCache(instr->msg);
                if (DEBUG) {
                    fputc(('\n'), stdout);
                }
                break;
            default:
                break;
        }

        free(instr);
    }
}

void dealFile() {
    init();
    file = fopen(fileAddr, "r");

    if (file == NULL) {
        fprintf(stderr, "Wrong file address!");
        exit(EXIT_FAILURE);
    }
    free(fileAddr);

    readFile();

    fclose(file);
    free(cache);
}

int main(int argc, char *argv[]) {
    while ((opt = getopt(argc, argv, "hvs:E:b:t:")) != -1) {
        switch (opt) {
            case 'h':
                printf("%s", helpInfo);
                return 0;
            case 'v':
                DEBUG = true;
                break;
            case 's':
                s = atoi(optarg);
                S = 1 << s;
                break;
            case 'E':
                E = atoi(optarg);
                break;
            case 'b':
                b = atoi(optarg);
                B = 1 << b;
                break;
            case 't':
                fileAddr = (char *) malloc(sizeof(optarg));
                strcpy(fileAddr, optarg);
                break;
            default:
                fprintf(stderr, "Usage: %s [-hv] -s <num> -E <num> -b <num> -t <file>\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    dealFile();
    
    printSummary(hitCount, missCount, evictionCount);
    return 0;
}
