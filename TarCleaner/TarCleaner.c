#include "TarCleaner.h"
#include <stdio.h>
#define FILE_SIZE_INDEX 124
#define USTAR_INDEX 257
#define BLOCKSIZE 512
#define true 0
#define false 1

FILE *fp;
unsigned int tarSize;
FILE *fcopy;

const char* leaked_strings[] = {
"I:Closing tar\n",
"storing xattr user.default\n",
"storing xattr user.inode_cache\n",
"storing xattr user.inode_code_cache\n"
};

/**
 * Find the string instance at a specific index
 * @param str - the char array to match to
 * @param index of the first character to compare
 * @return 0 if it's a match, 1 if not
 */
int findStr(const char *str, int index){
    fseek(fp, index, SEEK_SET);
    int size = strlen(str);
    char c[size + 1];
    fread(c, 1, size, fp);
    c[size] = '\0';
    return strcmp(c, str);
}

/**
 * Find the number of bytes in a file
 * @return the size in bytes
 */
int findSize(){
    int size;
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    size -= ftell(fp);
    return size;
}

/**
 * Check for ustar indicator at a file index
 * @param index of the first character to compare
 * @return 0 if match, 1 if not
 */
int checkUstar(int index){
    return findStr("ustar", index);

}

/**
 * Parse the tar header file for info
 * at a specific index
 * @param index of the first byte of the header
 * @return a data structure with file name and size
 */
header parseHeader(int index){
    header h;
    h.headerIndex = index;
    fseek(fp, h.headerIndex, SEEK_SET);
    fgets(h.fileName, sizeof h.fileName, fp);
    fseek(fp, h.headerIndex + FILE_SIZE_INDEX, SEEK_SET);
    fscanf(fp, "%o", &h.fileSize);
    int remainder = h.fileSize % BLOCKSIZE;
    h.bufferSize = h.fileSize;
    if (remainder > 0){
        h.bufferSize += (BLOCKSIZE - remainder);
    }
    return h;
}

/**
 * Check for a leak starting at beginning the tar header file
 * to the end of the file content
 * @param index of the file's tar header
 * @return 0 if there's a leak, 1 if no leak
 */
int leaking(int index){
    if (index < BLOCKSIZE && index > USTAR_INDEX){
        return true;
    }
    header h = parseHeader(index);
    int secondUstar = h.headerIndex + USTAR_INDEX + h.bufferSize + BLOCKSIZE;
    if (secondUstar > (tarSize - (BLOCKSIZE*2))){
        secondUstar -= USTAR_INDEX;
        if (secondUstar!=(tarSize - (BLOCKSIZE*2))) return true;
    }
    else if(checkUstar(secondUstar)!=0){
        return true;
    }
    return false;
}

/**
 * Check for the leaked strings
 * at a specific index
 * @param index of the first char to compare to
 * @return the size of the leaked string if there's one,
 *         0 if not
 */
int checkLeak(int index){
    int l;
    int arrSize = sizeof(leaked_strings) / sizeof(leaked_strings[0]);
    for (l=0; l < arrSize; l++){
        if (findStr(leaked_strings[l], index)==true){
            return strlen(leaked_strings[l]);
        }
    }
    return 0;
}

/**
 * Copy a large file 512 bytes at a time
 * @param start - first index(inclusive)
 * @param finish - last index(exclusive)
 * @param stream to insert into
 * @return 0 for success
 */
int copy(int start, int finish){
    const int blockSize = BLOCKSIZE * 64;
    char buffer[blockSize];
    int i;
    fseek(fp, start, SEEK_SET);
    fseek(fcopy, 0, SEEK_END);
    for (i = start; i < finish; i += blockSize){
        int bufferSize = blockSize;
        if (finish - i < bufferSize){
            bufferSize = finish - i;
        }
        fread(buffer, 1, bufferSize, fp);
        fwrite(buffer, 1, bufferSize, fcopy);
    }
    return 0;
}

/**
 * Write the last 2 blocks of null chars
 * to the copy tar file
 * @param nullNum - number of null chars to insert
 * @return 0 for success
 */
int writeNull(int nullNum){
    char nullChar[nullNum];
    int i;
    for (i=0; i < sizeof nullChar; i++){
        nullChar[i] = (char)0;
    }
    fseek(fcopy, 0, SEEK_END);
    fwrite(nullChar, 1, sizeof nullChar, fcopy);
    return 0;
}

/**
 * The main public function to copy a clean version
 * of a corrupted tar file
 * @param fileName - the corrupted tar file
 * @param copyFileName - the file to copy into
 * @return 0 if both file streams close successfully
 */
int cleanAndCopy(const char* fileName, const char* copyFileName){
    fp = fopen(fileName, "rb");
    fcopy = fopen(copyFileName, "wb");
    tarSize = findSize();
    int j;
    for (j = 0; j < tarSize; j++){
        if (checkUstar(j + USTAR_INDEX)!=0) continue;
        header h = parseHeader(j);
        int end = h.headerIndex + BLOCKSIZE + h.bufferSize;
        if (leaking(h.headerIndex)==true){
            printf("%i Leaked file: %s\n", h.headerIndex, h.fileName);
            int k = 0;
            int startIndex = h.headerIndex;
            int index;
            int elements = end - h.headerIndex;
            while (k < elements){
                index = startIndex;
                while((index < tarSize) && (index < end) && checkLeak(index)<=0){
                    index+= BLOCKSIZE;
                }
                int leakLength = checkLeak(index);
                end += leakLength;
                copy(startIndex, index);
                k = k + (index - startIndex);
                startIndex = index + leakLength;
            }
        }
        else{
            copy(h.headerIndex, end);
        }
        j = end - 1;
    }
    writeNull(BLOCKSIZE * 2);
    return fclose(fcopy) + fclose(fp);
}
