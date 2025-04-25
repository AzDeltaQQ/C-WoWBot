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

    // Alias for Read to maintain consistent naming with WriteMemory
    template<typename T>
    T ReadMemory(uintptr_t address) {
        return Read<T>(address);
    }

    // Add other memory functions (Write, ReadString etc.) using direct access as needed.

} // namespace MemoryReader

namespace MemoryWriter {

    // Writes a value of type T directly to the specified address within the current process.
    // Warning: Writing to invalid memory will likely cause a crash.
    template<typename T>
    void WriteMemory(uintptr_t address, T value) {
        try {
            // Check for null address to prevent crashes on zero pointers
            if (address == 0) {
                throw std::runtime_error("MemoryWriter::WriteMemory - Attempted to write to NULL address.");
            }
            // Cast to a non-const pointer of the appropriate type and write the value
            T* ptr = reinterpret_cast<T*>(address);
            *ptr = value;
        } catch (const std::exception& e) {
            // Catch potential access violations, though behavior might be crash
            throw std::runtime_error("MemoryWriter::WriteMemory - Exception during direct memory write at address 0x" + 
                                    std::to_string(address) + ": " + e.what());
        } catch (...) {
            throw std::runtime_error("MemoryWriter::WriteMemory - Unknown exception during direct memory write at address 0x" + 
                                    std::to_string(address));
        }
    }

} // namespace MemoryWriter 