#if defined(TARGET_PC) && !defined(RECVX_NATIVE_DISC)
#include "ee_asm.h"
#endif
#include "../../../ps2/veronica/prog/ps2_sg_gd.h"
#include "../../../ps2/veronica/prog/ps2_dvd_image.h"
#ifdef RECVX_NATIVE_DISC
#include <stddef.h>
#include <limits.h>
#endif

//#include <string.h> /* ERROR: causes linker to fail */

static GDFS_HANDLE* __file_handle __attribute__((aligned(64)));
static GDFS_WORK* __gds_fs_work;
GDFS_DIRREC_ENT* __gdfs_dir_ent;
GDFS_DIRREC_TBL* __current_dir;

// 100% matching! 
void  gdFsClose(GDFS gdfs)
{
    gdfs->used = 0;
    
    gdfs->fad = 0;
    
    gdfs->fsize = 0;
    gdfs->fsctsize = 0;
}

// 100% matching!
GDFS_DIRREC  gdFsCreateDirhn(void *dirbuf, Sint32 max_dirent)
{
    return __current_dir;
}

// 100% matching! 
void  gdFsFinish(void)
{
#ifdef RECVX_NATIVE_DISC
    __file_handle = NULL;
    __gds_fs_work = NULL;
    __gdfs_dir_ent = NULL;
    __current_dir = NULL;
#endif
    sceCdInit(SCECdEXIT);
}

// 100% matching! 
Sint32  gdFsGetDirInfo(const char *name, GDFS_DIRINFO *dirinfo)
{
    int i; 
    char read_name[256]; 
    if (!__current_dir || strlen(name) >= sizeof(read_name)) return -8;

    for (i = 0; i < (unsigned int)strlen(name); i++) 
    {
        if ((name[i] >= 'a') && (name[i] <= 'z')) 
        {
            read_name[i] = name[i] - ' ';
        } 
        else
        {
            read_name[i] = name[i];
        }
    } 

    read_name[i] = 0; 
    
#if defined(TARGET_PC) && !defined(RECVX_NATIVE_DISC)
    {   /* a file in the mods folder replaces (or adds) this one (pc/src/audio/adx.c) */
        int size = pc_mod_file_size(read_name);
        
        if (size >= 0) 
        {
            dirinfo->fad = 0;
            dirinfo->fsize = size;
            dirinfo->flag = 0;
            
            return 0;
        }
    }
    
#endif
    for (i = 0; i < __current_dir->dir_num; i++) 
    {
        if (strcmp(&read_name, __gdfs_dir_ent[i].fname) == 0)
        {
            dirinfo->fad = __gdfs_dir_ent[i].fad;
            
            dirinfo->fsize = __gdfs_dir_ent[i].fsize;
            
            dirinfo->flag = __gdfs_dir_ent[i].flag;
            
            return 0;
        }
    }
    
    return -8; 
}

// 100% matching! 
Sint32  gdFsGetDrvStat(void)
{
    return 2;
}

// 100% matching! 
Bool  gdFsGetFileSize(GDFS gdfs, Sint32 *fsize)
{
    if (gdfs == NULL) 
    {
        return 0;
    }
    
    *fsize = gdfs->fsize;
    
    return 1;
}

// 100% matching!
Sint32  gdFsInit(Sint32 max_open, void *gdfs_work, Sint32 max_dirent, void *dirbuf) 
{
#ifdef RECVX_NATIVE_DISC
    unsigned char local_sector_buffer[2048];
#else
	char local_sector_buffer[2048]; 
#endif
	char read_f_name[2048]; 
	sceCdlFILE file_data; 
	unsigned int i; 
	unsigned int j; 
	int link_file_max; 
	int sector_top; 
	int sector_size; 
	int file_size; 
	int file_count; 
    sceCdRMode file_read_mode = 
    {
        0,
        1,
        0,
        0
    }; 
    
#ifdef RECVX_NATIVE_DISC
    if (!gdfs_work || !dirbuf || max_open <= 0 || max_dirent <= 0)
        return GDD_ERR_NOREADABLE;
    __file_handle = (GDFS_HANDLE*)((unsigned char*)gdfs_work + sizeof(GDFS_WORK));
#else
    __file_handle = (GDFS_HANDLE*)((int)gdfs_work + 4308);
#endif
    
    __gds_fs_work = gdfs_work;
    
    __gds_fs_work->max_open = max_open;
    
    __gds_fs_work->pathtbl_fad = 0;
    __gds_fs_work->pathtbl_size = 0;
    
    __gds_fs_work->dcf = NULL;
    
    __gds_fs_work->curdir = dirbuf;
    
    __gds_fs_work->syshdl = NULL;
    
    __gds_fs_work->hndtbl = NULL;
    
    __gds_fs_work->now_work = NULL;
    
    __gds_fs_work->f_svr = 0;
    
    __gds_fs_work->g_errcb = NULL;
    __gds_fs_work->g_errcb_1st = NULL;
    
    __gds_fs_work->gdc_ver = 65792;
    
    __gds_fs_work->errstat = 0;
    
    __gds_fs_work->istray = 0;
    
    __gds_fs_work->f_init = 1;
    
    __gds_fs_work->daplayed = 0;
    
    __gds_fs_work->liftbl = NULL;

    for (i = 0; i < 1040; i++) 
    {
        __gds_fs_work->sctbuf[i] = 0;
    }
    
    for (i = 0; i < max_open; i++) 
    {
        __file_handle[i].wk = gdfs_work;
        
        __file_handle[i].fid = 0;
        __file_handle[i].fad = 0;
        
        __file_handle[i].fsize = 0;
        __file_handle[i].fsctsize = 0;
        
        __file_handle[i].ofs = 0;
        
        __file_handle[i].trnsed = 0;
        
        __file_handle[i].rsize = 0;
        __file_handle[i].trsize = 0;
        
        __file_handle[i].rdendcb = NULL;
        __file_handle[i].rdcb_1st = NULL;
        
        __file_handle[i].trendcb = NULL;
        __file_handle[i].trcb_1st = NULL;
        
        __file_handle[i].errcb = NULL;
        __file_handle[i].errcb_1st = NULL;
        
        __file_handle[i].gdchn = 0;
        __file_handle[i].gdchn_wait = 0;
        
        __file_handle[i].ex_errcode = 0;
        
        __file_handle[i].act = 0;
        
        __file_handle[i].trflag = 0;
        
        __file_handle[i].used = 0;
        
        __file_handle[i].tmode = 0;
        
        __file_handle[i].stat = 0;
        
        __file_handle[i].err = 0;
    }
    
    __current_dir = dirbuf;
    
#ifdef RECVX_NATIVE_DISC
    __gdfs_dir_ent = (GDFS_DIRREC_ENT*)((unsigned char*)dirbuf + offsetof(GDFS_DIRREC_TBL, dirrec_tbl));
#else
    __gdfs_dir_ent = (GDFS_DIRREC_ENT*)((int)dirbuf + 104);
#endif
    
    __current_dir->dir_num = 0;
    
    __current_dir->max_ent = max_dirent;
    
    __current_dir->dir_fad = 0;
    
    for (i = 0; i < max_dirent; i++) 
    {
        __gdfs_dir_ent[i].fad = 0;
        
        __gdfs_dir_ent[i].fsize = 0;
        
        __gdfs_dir_ent[i].flag = 0;
        
        __gdfs_dir_ent[i].sid = 0;
        
        __gdfs_dir_ent[i].fname[0] = 0; 
    }
    
#ifdef RECVX_NATIVE_DISC
    if (sceCdDiskReady(0) != 2) return GDD_ERR_NOREADABLE;
#else
    sceCdDiskReady(0);
#endif
    
    file_count = 0;

    for (i = 0; i < dvd_image_data_max; i++) 
    {
#ifndef RECVX_NATIVE_DISC
        for (j = 0; j < strlen(dvd_image_data[i].file_name); j++) 
        {
            if ((dvd_image_data[i].file_name[j] >= 'a') && (dvd_image_data[i].file_name[j] <= 'z')) 
            {
                dvd_image_data[i].file_name[j] = dvd_image_data[i].file_name[j] - ' ';
            }
        }
        
        dvd_image_data[i].file_name[j] = 0;
#endif
        
        sprintf(read_f_name, "%s%s;1", "\\", dvd_image_data[i].file_name);
        
#ifdef RECVX_NATIVE_DISC
        if (file_count >= max_dirent || !sceCdSearchFile(&file_data, read_f_name) ||
            file_data.lsn > INT_MAX || file_data.size > INT_MAX - 2047)
            return GDD_ERR_NOREADABLE;
#else
        while (sceCdSearchFile(&file_data, read_f_name) == 0) 
        {
            int loop;
            
            for (loop = 0; loop < 20; loop++) 
            {
#ifndef TARGET_PC
                asm("nop");
#else
                { /* asm2c */ }
#endif
#ifndef TARGET_PC
                asm("nop");
#else
                { /* asm2c */ }
#endif
#ifndef TARGET_PC
                asm("nop");
#else
                { /* asm2c */ }
#endif
#ifndef TARGET_PC
                asm("nop");
#else
                { /* asm2c */ }
#endif
            }
            
            printf("found_serach %s\n", read_f_name);
        }
#endif
        
        if (dvd_image_data[i].link_file != 0) 
        {
            __gdfs_dir_ent[file_count].fad = file_data.lsn;
            
            __gdfs_dir_ent[file_count].fsize = file_data.size;
            
            __gdfs_dir_ent[file_count].flag = 0;
            
            __gdfs_dir_ent[file_count].sid = 0;
            
            sprintf((char*)__gdfs_dir_ent[file_count].fname, "%s", dvd_image_data[i].file_name);
            
            file_count++;
            
#ifdef RECVX_NATIVE_DISC
            if (file_data.size < sizeof(local_sector_buffer) ||
                !sceCdRead(file_data.lsn, 1, local_sector_buffer, &file_read_mode) ||
                sceCdSync(0) != 0 || sceCdGetError() != 0 ||
                memcmp(local_sector_buffer, "AFS\0", 4))
                return GDD_ERR_NOREADABLE;
            uint32_t entries, first_offset;
            memcpy(&entries, local_sector_buffer + 4, sizeof(entries));
            memcpy(&first_offset, local_sector_buffer + 8, sizeof(first_offset));
            if (entries != (unsigned)rdx_image_data_max ||
                entries > (sizeof(local_sector_buffer) - 8) / 8 ||
                entries > (unsigned)(max_dirent - file_count) ||
                first_offset < 8 + entries * 8 || first_offset % 2048)
                return GDD_ERR_NOREADABLE;
            link_file_max = (int)entries;
            sector_top = first_offset / 2048;
#else
            do 
            {
                while (sceCdRead(file_data.lsn, 1, &local_sector_buffer, &file_read_mode) == 0) 
                {
                    int loop;
                    
                    for (loop = 0; loop < 20; loop++) 
                    {
#ifndef TARGET_PC
                        asm("nop");
#else
                        { /* asm2c */ }
#endif
#ifndef TARGET_PC
                        asm("nop");
#else
                        { /* asm2c */ }
#endif
#ifndef TARGET_PC
                        asm("nop");
#else
                        { /* asm2c */ }
#endif
#ifndef TARGET_PC
                        asm("nop");
#else
                        { /* asm2c */ }
#endif
                    }
                }
                
                sceCdSync(0);
            } while (sceCdGetError() != 0);
            
            link_file_max = ((int*)local_sector_buffer)[1];
            
            sector_top = ((int*)local_sector_buffer)[2] / 2048; 
#endif
            
            for (j = 0; j < link_file_max; j++) 
            {
#ifdef RECVX_NATIVE_DISC
                uint32_t offset, length;
                memcpy(&offset, local_sector_buffer + 8 + j * 8, sizeof(offset));
                memcpy(&length, local_sector_buffer + 12 + j * 8, sizeof(length));
                if (offset != (unsigned)sector_top * 2048 || offset > file_data.size ||
                    !length || length > file_data.size - offset || length > INT_MAX - 2047 ||
                    file_data.lsn > (unsigned)(INT_MAX - sector_top))
                    return GDD_ERR_NOREADABLE;
                file_size = (int)length;
#else
                file_size = ((int*)local_sector_buffer)[(j * 2) + 3]; // fuck knows
#endif
                sector_size = (file_size + 2047) / 2048;
                
                __gdfs_dir_ent[file_count].fad = file_data.lsn + sector_top;
                
                __gdfs_dir_ent[file_count].fsize = file_size;
                
                __gdfs_dir_ent[file_count].flag = 0;
                
                __gdfs_dir_ent[file_count].sid = i;
                
                sprintf((char*)__gdfs_dir_ent[file_count].fname, "%s", dvd_image_data[i].link_file[j]);
                
                sector_top += sector_size;
                
                file_count++;
            }
        }
        else
        {
            __gdfs_dir_ent[file_count].fad = file_data.lsn;
            
            __gdfs_dir_ent[file_count].fsize = file_data.size;
            
            __gdfs_dir_ent[file_count].flag = 0;
            
            __gdfs_dir_ent[file_count].sid = 0; 
            
            sprintf((char*)__gdfs_dir_ent[file_count].fname, "%s", dvd_image_data[i].file_name);
            
            file_count++;
        }
    }
    
    __current_dir->dir_num = file_count;
    
    return 0;
}

// 100% matching! 
GDFS  gdFsOpen(const char *fname, GDFS_DIRREC gf_dirrec)
{
    int i;
    int j;
    char read_name[256];
    if (!__current_dir || strlen(fname) >= sizeof(read_name)) return NULL;

    for (i = 0; i < (unsigned int)strlen(fname); i++) 
    {
        if ((fname[i] >= 'a') && (fname[i] <= 'z')) 
        {
            read_name[i] = fname[i] - ' ';
        } 
        else
        {
            read_name[i] = fname[i];
        }
    } 

    read_name[i] = 0; 
    
    for (i = 0; i < __current_dir->dir_num; i++) 
    {
        if (strcmp(&read_name, __gdfs_dir_ent[i].fname) == 0) 
        {
            for (j = 0; j < __gds_fs_work->max_open; j++) 
            {
               if (__file_handle[j].used == 0) 
               {
                    __file_handle[j].used = 1;
                   
                    __file_handle[j].fad = __gdfs_dir_ent[i].fad;
                   
                    __file_handle[j].fsize = __gdfs_dir_ent[i].fsize;
                    __file_handle[j].fsctsize = (__gdfs_dir_ent[i].fsize + 2047) / 2048;
                   
                    return &__file_handle[j];
                }
            }
        }
    }
    return NULL;
}

// 100% matching! 
Sint32  gdFsReqDrvStat(void)
{
    return 0;
}

// 100% matching!
Sint32  gdFsSetDir(GDFS_DIRREC gf_dirrec)
{
    return 0;
}

// 100% matching!
void  gdFsEntryErrFuncAll(GDFS_ERRFUNC erfunc, void *obj)
{

}

// 100% matching!
Sint32  gdFsLoadDir(const char *dirname, GDFS_DIRREC gf_dirrec)
{
    return 0;
}

// 100% matching! 
Sint32  gdFsRead(GDFS gdfs, Sint32 nsct, void *buf)
{
#ifdef RECVX_NATIVE_DISC
    if (!gdfs || !buf || nsct < 0 || nsct > gdfs->fsctsize) return GDD_ERR_NOREADABLE;
    if (!nsct) return 0;
    sceCdRMode read_mode = {0, 1, 0, 0};
    if (!sceCdRead(gdfs->fad, nsct, buf, &read_mode) || sceCdSync(0) != 0 || sceCdGetError() != 0)
        return GDD_ERR_NOREADABLE;
    return 0;
#else
    sceCdRMode read_mode;
    int loop;

    read_mode.trycount = 0;
    
    read_mode.spindlctrl = 1;
    
    read_mode.datapattern = 0;
    
    do 
    {
        while (sceCdRead(gdfs->fad, nsct, buf, &read_mode) == 0)
        {
            for (loop = 0; loop < 20; loop++) 
            {
#ifndef TARGET_PC
                asm("nop");
#else
                { /* asm2c */ }
#endif
#ifndef TARGET_PC
                asm("nop");
#else
                { /* asm2c */ }
#endif
#ifndef TARGET_PC
                asm("nop");
#else
                { /* asm2c */ }
#endif
#ifndef TARGET_PC
                asm("nop");
#else
                { /* asm2c */ }
#endif
            }
        }
        
        sceCdSync(0);
    } while (sceCdGetError() != 0);
    
    return 0;
#endif
}
