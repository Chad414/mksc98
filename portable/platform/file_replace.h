#ifndef PORTABLE_FILE_REPLACE_H
#define PORTABLE_FILE_REPLACE_H

#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
#endif

/* Publish a closed sibling temporary file, replacing an existing save without
 * deleting it first. Windows CRT rename refuses an existing destination. */
static inline int portable_file_replace(const char *temporary,const char *path)
{
    if(temporary==NULL||path==NULL)return 0;
#if defined(_WIN32) && defined(MKSC_WIN98)
    /* Windows 9x exports MoveFileExA only as a stub that always fails. A
     * plain move works for a first save; otherwise replace in two steps. */
    if(MoveFileA(temporary,path))return 1;
    return DeleteFileA(path)&&MoveFileA(temporary,path);
#elif defined(_WIN32)
    return MoveFileExA(temporary,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    return rename(temporary,path)==0;
#endif
}
#endif
