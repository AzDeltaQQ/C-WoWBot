#pragma once

#include <cstdint> // For uintptr_t and standard integer types
#include <stdexcept> // For potential exceptions on bad access (though less common)

namespace MemoryReader {

    // Reads a value of type T directly from the specified address within the current process.
    // Warning: Accessing invalid memory will likely cause a crash.
    template<typename T>
    T Read(uintptr_t address) {
        // Direct memory access since we are in-process
        try {
             // Check for null address to prevent crashes on zero pointers
            if (address == 0) {
                 throw std::runtime_error("MemoryReader::Read - Attempted to read from NULL address.");
            }
            // Treat the memory as volatile to prevent compiler optimizations from caching the read
            volatile T* ptr = reinterpret_cast<volatile T*>(address);
            return *ptr;
        } catch (const std::exception& e) {
            // Catch potential access violations, though behavior might be crash
            // Log or rethrow as needed
            throw std::runtime_error("MemoryReader::Read - Exception during direct memory access at address 0x" + 
                                     std::to_string(address) + ": " + e.what());
        } catch (...) {
             throw std::runtime_error("MemoryReader::Read - Unknown exception during direct memory access at address 0x" + 
                                     std::to_string(address));
        }
    }

    // Add other memory functions (Write, ReadString etc.) using direct access as needed.

} // namespace MemoryReader 