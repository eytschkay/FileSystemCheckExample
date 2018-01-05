/*
 * BSHU2 -- EOS32 file system check
 *
 * Hier ist eine (nicht vollstaendige) Liste von moeglichen Fehlern:
 * [x] Ein Block ist weder in einer Datei noch auf der Freiliste: Exit-Code 10.
 * [x] Ein Block ist sowohl in einer Datei als auch auf der Freiliste: Exit-Code 11.
 * [x] Ein Block ist mehr als einmal in der Freiliste: Exit-Code 12.
 * [x] Ein Block ist mehr als einmal in einer Datei oder in mehr als einer Datei: Exit-Code 13.
 * [ ] Die Groesse einer Datei ist nicht konsistent mit den im Inode vermerkten Bloecken: Exit-Code 14.
 * [x] Ein Inode mit Linkcount 0 erscheint in einem Verzeichnis: Exit-Code 15.
 * [x] Ein Inode mit Linkcount 0 ist nicht frei: Exit-Code 16.
 * [x] Ein Inode mit Linkcount n != 0 erscheint nicht in exakt n Verzeichnissen: Exit-Code 17.
 * [ ] Ein Inode hat ein Typfeld mit illegalem Wert: Exit-Code 18.
 * [x] Ein Inode erscheint in einem Verzeichnis, ist aber frei: Exit-Code 19.
 * [x] Der Root-Inode ist kein Verzeichnis: Exit-Code 20.
 * [ ] Ein Verzeichnis kann von der Wurzel aus nicht erreicht werden: Exit-Code 21.
 * [ ] Alle anderen Dateisystem-Fehler: Exit-Code 99.
 *
 *  Andere moegliche Fehler, die geprueft werden muessen:
 * [x] Falscher Aufruf des Programms: Exit-Code 1.
 * [x] Image-Datei nicht gefunden: Exit-Code 2.
 * [x] Datei Ein/Ausgabefehler: Exit-Code 3.
 * [x] Illegale Partitionsnummer: Exit-Code 4.
 * [x] Partition enthaelt kein EOS32-Dateisystem: Exit-Code 5.
 * [x] Erfolgloser Aufruf von malloc(): Exit-Code 6.
 * [ ] Alle anderen Fehler: Exit-Code 9.
 */

//TODO: Correctly implement inodeCounter

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SECTOR_SIZE 512                     //disk sector size in bytes
#define BLOCK_SIZE 4096                     //disk block size in bytes
#define SPB (BLOCK_SIZE / SECTOR_SIZE)      //sectors per block

#define NICINOD 500                         //number of free inodes in superblock
#define NICFREE 500                         //number of free blocks in superblock
#define INOPB 64                            //number of inodes per block
#define DIRPB 64                            //number of directory entries per block
#define DIRSIZ 60                           //max length of path name component

#define IFMT    070000                      //type of file
#define IFREG   040000                      //regular file
#define IFDIR   030000                      //directory
#define IFCHR   020000                      //character special
#define IFBLK   010000                      //block special
#define IFFREE  000000                      //reserved (indicates free inode)

#define ISUID   004000                      //set user id on execution
#define ISGID   002000                      //set group id on execution
#define ISVTX   001000                      //save swapped text even after use
#define IUREAD  000400                      //user's read permission
#define IUWRITE 000200                      //user's write permission
#define IUEXEC  000100                      //user's execute permission
#define IGREAD  000040                      //group's read permission
#define IGWRITE 000020                      //group's write permission
#define IGEXEC  000010                      //group's execute permission
#define IOREAD  000004                      //other's read permission
#define IOWRITE 000002                      //other's write permission
#define IOEXEC  000001                      //other's execute permission


void initInodeCounter();
void inspectInodes(void);
void checkMode(unsigned int);
void getDirectBlocks(unsigned char *);
void getSingleIndirectBlocks(unsigned char *);
void getDoubleIndirectBlocks(unsigned char *);
void inspectFreelist(void);
void followLinkBlock(unsigned int);
void checkBlockCounter(void);
void getRootDir(void);
void checkDirectory(unsigned int);
void readInode(unsigned int);

void help(char *);
unsigned int get4Bytes(const unsigned char *);
void readBlock(unsigned , unsigned char *);

typedef struct blockCounter {
    int free;
    int occupied;
} bCounter_t;

typedef struct inoCounter {
    int refs;
    char visited;
} iCounter_t;

FILE *disk;
unsigned int fsStart;
unsigned int fsSize;
int part;
char *endptr;
unsigned char partTable[SECTOR_SIZE];
unsigned char *ptptr;
unsigned int partType;
unsigned int inodeListSize;
unsigned int numBlocks;

bCounter_t *bCounter;
iCounter_t *inodeCounter;

int main(int argc, char *argv[]) {
    if(argc != 3) {
        //wrong program call
        help(argv[0]);
    }
    disk = fopen(argv[1], "rb");
    if(disk == NULL) {
        //can't open image file
        printf("Error: cannot open disk image file '%s'", argv[1]);
        exit(2);
    }
    if(strcmp(argv[2], "*") == 0) {
        //whole disk contains one single file system
        fsStart = 0;
        fseek(disk, 0, SEEK_END);
        fsSize = ftell(disk) / SECTOR_SIZE;
    } else {
        //argv[2] is partition number of file system
        part = strtoul(argv[2], &endptr, 10);
        if(*endptr != '\0' ||part < 0 || part > 15) {
            printf("Error: illegal partition number '%s'", argv[2]);
            exit(4);
        }
        fseek(disk, 1 * SECTOR_SIZE, SEEK_SET);
        if(fread(partTable, 1 , SECTOR_SIZE, disk) != SECTOR_SIZE) {
            printf("Error: cannot read partition table of disk '%s'", argv[1]);
            exit(3);
        }
        ptptr = partTable + part * 32;
        partType = get4Bytes(ptptr + 0);
        if((partType & 0x7FFFFFFF) != 0x00000058) {
            printf("Error: partition %d of disk '%s' does not contain an EOS32 file system", part, argv[1]);
            exit(5);
        }
        fsStart = get4Bytes(ptptr + 4);
        fsSize = get4Bytes(ptptr + 8);
    }

    numBlocks = fsSize / SPB;

    bCounter = (bCounter_t *) malloc(sizeof(bCounter_t) * numBlocks);
    if(bCounter == NULL) {
        printf("Error: Failed malloc() call\n");
        exit(6);
    }

    initInodeCounter();
    getRootDir();

    //Debug print
    for(int i = 0; i < inodeListSize * 64; i++) {
        printf("%d\n", inodeCounter[i].refs);
    }
    //end Debug print

    inspectInodes();
    checkBlockCounter();

}

void initInodeCounter() {
    unsigned char blockBuffer[BLOCK_SIZE];
    unsigned char *p;

    readBlock(1, blockBuffer);
    p = blockBuffer;

    p += 8;

    inodeListSize = get4Bytes(p);

    inodeCounter = (iCounter_t *) malloc(sizeof(iCounter_t) * inodeListSize * 64);

    if(inodeCounter == NULL) {
        printf("Error: Failed malloc() call\n");
        exit(6);
    }

}

void inspectInodes(void) {
    unsigned int i = 2;
    unsigned int j;
    unsigned int mode;
    unsigned char isFree;
    unsigned int nLink;
    unsigned int size;
    unsigned int block;
    unsigned char blockBuffer[BLOCK_SIZE];
    unsigned char *p;                               //placeholder pointer hold address within the block buffer

    readBlock(1, blockBuffer);
    p = blockBuffer;

    p += 8; //skip to inode list size
    inodeListSize = get4Bytes(p);

    while(i < inodeListSize) {
        readBlock(i, blockBuffer);
        p = blockBuffer;
        bCounter[i].occupied += 1;

        for(j = 0; j < INOPB; j++) {
            if(i == 2 && j == 0) {
                p += 64;
                continue;
            }

            mode = get4Bytes(p);
            p += 4;

            if(mode != 0) {
                checkMode(mode);
                isFree = 0;
            } else {
                //inode is free
                isFree = 1;
            }

            if(((mode && IFMT) == IFCHR) || ((mode && IFMT) == IFBLK)) {
                p += 60;
                continue;
            }

            nLink = get4Bytes(p);
            p += 24;

            if(nLink == 0 && inodeCounter[((i - 2) * INOPB) + j].refs > 0) {
                printf("Error: Inode with a link count of 0 appears in a directory");
                exit(15);
            }
            if(isFree == 0 && nLink == 0) {
                printf("Error: Inode with a link count of 0 is not free");
                exit(16);
            }
            if(nLink != inodeCounter[((i - 2) * INOPB) + j].refs) {
                printf("Error: Inode with a link count higher than 0 does not appear in exactly n directories");
                exit(17);
            }
            if(isFree && inodeCounter[((i - 2) * INOPB) + j].refs) {
                printf("Error: Free inode appears in a directory");
                exit(19);
            }

            size = get4Bytes(p);
            p += 4;

            getDirectBlocks(p);
            p += 24;
            getSingleIndirectBlocks(p);
            p += 4;
            getDoubleIndirectBlocks(p);
            p += 4;

        }

        i++;
    }

    inspectFreelist();

}

void checkMode(unsigned int mode) {

    //TODO

}

void getDirectBlocks(unsigned char *p) {
    int i;
    unsigned int blk;

    for(i = 0; i < 6; i++) {
        blk = get4Bytes(p);
        p += 4;
        if(blk < numBlocks) bCounter[blk].occupied += 1;
    }

}

void getSingleIndirectBlocks(unsigned char *p) {
    int i;
    unsigned char indirectBlockBuffer[BLOCK_SIZE];
    unsigned char *p0;
    unsigned int blk;

    readBlock(get4Bytes(p), indirectBlockBuffer);
    p0 = indirectBlockBuffer;

    for(i = 0; i < BLOCK_SIZE / sizeof(unsigned int); i++) {
        blk = get4Bytes(p0);
        p0 += 4;
        if(blk < numBlocks) bCounter[blk].occupied += 1;

    }
}

void getDoubleIndirectBlocks(unsigned char *p) {
    int i, j;
    unsigned char indirectBlockBuffer[BLOCK_SIZE];
    unsigned char doubleIndirectBlockBuffer[BLOCK_SIZE];
    unsigned char *p0;
    unsigned char *p1;
    unsigned int blk;
    unsigned int blk0;

    readBlock(get4Bytes(p), indirectBlockBuffer);
    p0 = indirectBlockBuffer;

    for(i = 0; i < BLOCK_SIZE / sizeof(unsigned int); i++) {

        blk0 = get4Bytes(p0);
        if(blk0 < numBlocks) readBlock(blk0, doubleIndirectBlockBuffer);
        else continue;
        p1 = doubleIndirectBlockBuffer;

        for(j = 0; j < BLOCK_SIZE / sizeof(unsigned int); j++) {
            blk = get4Bytes(p1);
            p1 += 4;
            if(blk < numBlocks) bCounter[blk].occupied += 1;
        }

        p0 += 4;

    }
}

void inspectFreelist(void) {
    unsigned char blockBuffer[BLOCK_SIZE];
    unsigned char *p;
    unsigned int link;
    unsigned int blk;

    readBlock(1, blockBuffer);
    p = blockBuffer;

    p += 24;
    p += 500 * 4;
    p += 4;

    //Go though free list in super block
    link = get4Bytes(p);
    p += 4;
    bCounter[link].free += 1;

    for(int i = 1; i < NICFREE; i++) {
        blk = get4Bytes(p);
        p += 4;
        if(blk < numBlocks) bCounter[blk].free += 1;
    }

    if(link > 0) followLinkBlock(link);
}

void followLinkBlock(unsigned int link) {
    unsigned char blockBuffer[BLOCK_SIZE];
    unsigned char *p;
    unsigned int linkBlk;
    unsigned int blk;

    readBlock(link, blockBuffer);
    p = blockBuffer;

    p += 4;

    linkBlk = get4Bytes(p);
    p += 4;
    bCounter[link].free += 1;

    for(int i = 1; i < NICFREE; i++) {
        blk = get4Bytes(p);
        p += 4;
        if(blk < numBlocks) bCounter[blk].free++;
    }

    if(linkBlk > 0) followLinkBlock(linkBlk);

}

void checkBlockCounter(void) {
    int free;
    int occupied;

    for(int i = 2; i < numBlocks; i++) {
        free = bCounter[i].free;
        occupied = bCounter[i].occupied;

        if(free == 0 && occupied == 0) {
            printf("Error: Block is neither in a file nor free");
            exit(10);
        }
        if(free == 1 && occupied == 1) {
            printf("Error: Block is in a file and free");
            exit(11);
        }
        if(free > 1) {
            printf("Error: Block is on the free list more than once");
            exit(12);
        }
        if(occupied > 1) {
            printf("Error: Block is in a file more than once or is in more than one file");
            exit(13);
        }
    }
}

void getRootDir(void) {
    unsigned char blockBuffer[BLOCK_SIZE];
    unsigned char *p;
    unsigned int rootDirBlock;
    int mode;

    //Get root directory
    readBlock(2, blockBuffer);
    p = blockBuffer;

    p += 64; //get to first inode

    mode = get4Bytes(p);

    if((mode & IFMT) != IFDIR) {
        printf("Error: Root-inode is not a directory");
        exit(20);
    }

    p += 32; //go to first direct block

    rootDirBlock = get4Bytes(p);
    printf("%d\n", rootDirBlock);

    inodeCounter[1].visited = 1;

    //start recursive run through directories
    checkDirectory(rootDirBlock);
}

void checkDirectory(unsigned int blockNumber) {
    unsigned int inode;
    unsigned char blockBuffer[BLOCK_SIZE];
    unsigned char *p;
    readBlock(blockNumber, blockBuffer);
    p = blockBuffer;

    for(int i = 0; i < DIRPB; i++) {
        inode = get4Bytes(p);

        inodeCounter[inode].refs++;

        if(!inodeCounter[inode].visited) readInode(inode);

        //increase p to the next directory entry
        p += 4;
        p += DIRSIZ;
    }
}

void readInode(unsigned int inodeNumber) {
    int mode;
    unsigned int blk;

    //get block where the inode is located
    unsigned int block = (inodeNumber / INOPB) + 2;
    //location inside the block
    unsigned int inode = inodeNumber % INOPB;

    unsigned char blockBuffer[BLOCK_SIZE];
    unsigned char siBlockBuffer[BLOCK_SIZE];    //si -> single indirect
    unsigned char diBlockBuffer[BLOCK_SIZE];    //di -> double indirect
    unsigned char *p;
    unsigned char *sip;
    unsigned char *dip;

    inodeCounter[inodeNumber].visited = 1;

    readBlock(block, blockBuffer);
    p = blockBuffer;

    p += inode * 64;

    mode = get4Bytes(p);
    if((mode & IFMT) != IFDIR) {
        return;
    }
    p += 32;

    //Direct blocks
    for(int i = 0; i < 6; i++) {
        blk = get4Bytes(p);
        p += 4;
        if(blk != 0) checkDirectory(blk);
    }

    blk = get4Bytes(p);
    p += 4;
    if(blk != 0) {
        readBlock(blk, siBlockBuffer);
        sip = siBlockBuffer;
        //single indirect block
        for(int i = 0; i < BLOCK_SIZE / sizeof(unsigned int); i++) {
            blk = get4Bytes(sip);
            sip += 4;
            if(blk != 0) checkDirectory(blk);
        }
    }


    blk = get4Bytes(p);
    p += 4;
    if(blk != 0) {
        readBlock(blk, siBlockBuffer);
        sip = siBlockBuffer;
        //double indirect block
        for(int i = 0; i < BLOCK_SIZE / sizeof(unsigned int); i++) {
            blk = get4Bytes(sip);
            sip += 4;

            readBlock(blk, diBlockBuffer);
            dip = diBlockBuffer;

            for(int j = 0; j < BLOCK_SIZE / sizeof(unsigned int); j++) {
                blk = get4Bytes(dip);
                dip += 4;
                if(blk != 0) checkDirectory(blk);
            }

        }
    }


}

void readBlock(unsigned int blockNum, unsigned char *blockBuffer) {
    fseek(disk, fsStart * SECTOR_SIZE + blockNum * BLOCK_SIZE, SEEK_SET);
    if(fread(blockBuffer, BLOCK_SIZE, 1, disk) != 1) {
        printf("Error: cannot read block %u (0x%X)", blockNum, blockNum);
        exit(99);
    }
}

unsigned int get4Bytes(const unsigned char *addr) {
    return (unsigned int) addr[0] << 24 |
           (unsigned int) addr[1] << 16 |
           (unsigned int) addr[2] << 8  |
           (unsigned int) addr[3] << 0;
}

void help(char *name) {
    printf("Usage: %s <disk> <partition>\n", name);
    printf("       <disk> is a disk image file name\n");
    printf("       <partition> is a partition number (or '*' for the whole disk\n");
    exit(1);
}
