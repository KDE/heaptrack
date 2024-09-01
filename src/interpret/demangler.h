/*
    dwarfdiecache.h

    SPDX-FileCopyrightText: 2024 Klarälvdalens Datakonsult AB a KDAB Group company <info@kdab.com>
    SPDX-FileContributor: Leon Matthes <leon.matthes@kdab.com>

   SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef DEMANGLER_H
#define DEMANGLER_H

#include <cstdlib>
#include <string>
#include <vector>

class Demangler
{
public:
    Demangler();

    ~Demangler();

    std::string demangle(const std::string& mangledName);

private:
    using DemangleFn = int (*)(const char*, char*, size_t);
    struct DemangleLib
    {
        DemangleFn demangle;
        std::string prefix;
    };

    struct DemangleLibSpec
    {
        const char* libName = nullptr;
        const char* functionName = nullptr;
        const std::string prefix;
        const char* languageName = nullptr;
        const char* repository = nullptr;
    };

    void loadDemanglers(std::vector<DemangleLibSpec> specifiers);

    bool tryExternalDemanglers(const std::string& mangledName);

    size_t m_demangleBufferLength = 1024;
    // We must use malloc here instead of new or unique_ptr, as __cxa_demangle requires this.
    // Make sure this buffer is free'd in the destructor!
    char* m_demangleBuffer = reinterpret_cast<char*>(std::malloc(m_demangleBufferLength * sizeof(char)));
    std::vector<DemangleLib> m_demanglers;
};

#endif // DEMANGLER_H
