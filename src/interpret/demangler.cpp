#include "demangler.h"

#include <cxxabi.h>
#include <dlfcn.h>

#include <algorithm>
#include <iostream>

Demangler::Demangler()
{
    loadDemanglers(
        {{"librustc_demangle.so", "rustc_demangle", "_R", "Rust", "https://github.com/rust-lang/rustc-demangle"},
         {"libd_demangle.so", "demangle_symbol", "_D", "D", "https://github.com/lievenhey/d_demangler"}});
}

Demangler::~Demangler()
{
    free(m_demangleBuffer);
}

std::string Demangler::demangle(const std::string& mangledName)
{
    if (mangledName.length() < 3) {
        return mangledName;
    }

    // Try external demanglers first, as __cxa_demangle will happily try to demangle symbols emitted by e.g. Rust.
    // rustc_demangle on the other hand will return an error if the symbol did not originate from Rust.
    if (tryExternalDemanglers(mangledName)) {
        return std::string(m_demangleBuffer);
    }

    // Require GNU v3 ABI by the "_Z" prefix.
    if (mangledName[0] == '_' && mangledName[1] == 'Z') {
        int status = -1;
        // Note: __cxa_demangle may realloc m_demangleBuffer
        auto* dsymname = abi::__cxa_demangle(mangledName.c_str(), m_demangleBuffer, &m_demangleBufferLength, &status);
        if (status == 0 && dsymname)
            return m_demangleBuffer = dsymname;
    }
    return mangledName;
}

void Demangler::loadDemanglers(std::vector<Demangler::DemangleLibSpec> specifiers)
{
    for (auto specifier : specifiers) {
        if (auto demanglerLib = dlopen(specifier.libName, RTLD_LAZY)) {
            if (auto demangleFnVoid = dlsym(demanglerLib, specifier.functionName)) {
                auto demangle = reinterpret_cast<DemangleFn>(demangleFnVoid);
                m_demanglers.push_back({demangle, specifier.prefix});
            } else {
                auto message = "Unknown error!";
                if (auto errorMessage = (dlerror())) {
                    message = errorMessage;
                }
                std::cerr << "Failed to find demangle function: " << message << '\n';
                std::cerr << specifier.languageName << " symbol demangling will not be possible.\n";
                std::cerr << "Please make sure the demangler is installed correctly: " << specifier.repository
                          << std::endl;
            }
        } else {
            // Clear the error message
            dlerror();
            // TODO: Ideally we want to be able to opt into logging the error.
            // As we expect the demanglers aren't always installed, it would be an
            // unnecessary annoyance to always print an error here.
        }
    }
}

bool Demangler::tryExternalDemanglers(const std::string& mangledName)
{
    // Fast path: check if the mangled name starts with a known prefix (like _R or _D)
    // Then pick the corresponding demangler.
    auto demangler = std::find_if(m_demanglers.cbegin(), m_demanglers.cend(), [&mangledName](const auto& demangler) {
        // Check if mangledName starts with the prefix of the demangler
        // TODO: Replace with starts_with, once C++20 features are available.
        return mangledName.compare(0, demangler.prefix.size(), demangler.prefix) == 0;
    });
    if (demangler != m_demanglers.cend()) {
        return demangler->demangle(mangledName.c_str(), m_demangleBuffer, m_demangleBufferLength);
    }

    // Slow path: Try every demangler and see if one thinks this is a valid symbol.
    for (const auto& demangler : m_demanglers) {
        if (demangler.demangle(mangledName.c_str(), m_demangleBuffer, m_demangleBufferLength)) {
            return true;
        }
    }

    return false;
}
