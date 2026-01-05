/**
 * SharedMemoryIPC.cpp - boost::interprocess wrapper for LLM communication
 */

#include <iostream>
#include <string>
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>

namespace rtv::ipc {

namespace bip = boost::interprocess;

class SharedMemoryIPC {
public:
    explicit SharedMemoryIPC(const std::string& name, size_t size = 1024 * 1024)
        : name_(name)
    {
        try {
            // Create or open shared memory
            shm_ = bip::shared_memory_object(
                bip::open_or_create,
                name.c_str(),
                bip::read_write
            );
            shm_.truncate(size);
            
            region_ = bip::mapped_region(shm_, bip::read_write);
            
            std::cout << "[SharedMemoryIPC] Created: " << name 
                      << " (" << size / 1024 << " KB)" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "[SharedMemoryIPC] Error: " << e.what() << std::endl;
        }
    }
    
    ~SharedMemoryIPC() {
        bip::shared_memory_object::remove(name_.c_str());
    }
    
    void* data() { return region_.get_address(); }
    size_t size() const { return region_.get_size(); }

private:
    std::string name_;
    bip::shared_memory_object shm_;
    bip::mapped_region region_;
};

} // namespace rtv::ipc
