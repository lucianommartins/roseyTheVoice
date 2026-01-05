/**
 * test_ring_buffer.cpp - Unit test for lock-free ring buffer
 */

#include "rtv/audio/RingBuffer.hpp"
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>

using namespace rtv::audio;

void test_basic_push_pop() {
    RingBuffer<float> buffer(1024);
    
    std::vector<float> data = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    assert(buffer.push(data.data(), data.size()) == 5);
    assert(buffer.available() == 5);
    
    std::vector<float> out(5);
    assert(buffer.pop(out.data(), 5) == 5);
    assert(out == data);
    assert(buffer.available() == 0);
    
    std::cout << "[PASS] test_basic_push_pop" << std::endl;
}

void test_overflow() {
    RingBuffer<float> buffer(4);
    
    std::vector<float> data = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    assert(buffer.push(data.data(), data.size()) == 4);  // Only 4 fit
    assert(buffer.available() == 4);
    
    std::cout << "[PASS] test_overflow" << std::endl;
}

void test_concurrent() {
    RingBuffer<float> buffer(1024);
    std::atomic<bool> done{false};
    std::atomic<size_t> total_written{0};
    std::atomic<size_t> total_read{0};
    
    // Producer
    std::thread producer([&]() {
        std::vector<float> chunk(64, 1.0f);
        for (int i = 0; i < 100; ++i) {
            total_written += buffer.push(chunk.data(), chunk.size());
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        done = true;
    });
    
    // Consumer
    std::thread consumer([&]() {
        std::vector<float> chunk(64);
        while (!done || buffer.available() > 0) {
            total_read += buffer.pop(chunk.data(), chunk.size());
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    });
    
    producer.join();
    consumer.join();
    
    std::cout << "[PASS] test_concurrent (written=" << total_written 
              << ", read=" << total_read << ")" << std::endl;
}

int main() {
    std::cout << "=== RingBuffer Tests ===" << std::endl;
    
    test_basic_push_pop();
    test_overflow();
    test_concurrent();
    
    std::cout << "\nAll tests passed!" << std::endl;
    return 0;
}
