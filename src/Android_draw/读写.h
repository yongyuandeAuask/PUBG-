#ifndef NATIVESURFACE_MEMREAD_H
#define NATIVESURFACE_MEMREAD_H

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "paradise_api.h"

// ==================== 全局实例 ====================
static paradise_driver *driver = new paradise_driver();
static pid_t pid = -1;

// ==================== 兼容旧接口 ====================

pid_t getPID(const char *PackageName)
{
    pid = driver->get_pid(PackageName);
    if (pid > 0)
        driver->initialize(pid);
    return pid;
}

long getModuleBase(const char *module_name)
{
    return (long)driver->get_module_base(module_name);
}

long ReadValue(long addr)
{
    long he = 0;
    if (addr < 0xFFFFFFFF)
        driver->read((uintptr_t)addr, &he, 4);
    else
        driver->read((uintptr_t)addr, &he, 8);
    return he;
}

long ReadDword(long addr)
{
    long he = 0;
    driver->read((uintptr_t)addr, &he, 4);
    return he;
}

float ReadFloat(long addr)
{
    float he = 0;
    driver->read((uintptr_t)addr, &he, 4);
    return he;
}

int WriteDword(long int addr, int value)
{
    driver->write((uintptr_t)addr, &value, 4);
    return 0;
}

int WriteFloat(long int addr, float value)
{
    driver->write((uintptr_t)addr, &value, 4);
    return 0;
}

#endif