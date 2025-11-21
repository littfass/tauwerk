#ifndef LOCKFREE_ENGINE_HPP
#define LOCKFREE_ENGINE_HPP

#include <alsa/asoundlib.h>
#include <boost/lockfree/spsc_queue.hpp>
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/resource.h>  // Für rlimit
#include <atomic>
#include <chrono>
#include <vector>
#include <string>
#include <thread>          // Für std::this_thread

struct MidiMessage {
    uint8_t data[3];
    size_t size;
    int64_t timestamp;
    
    MidiMessage() : size(0), timestamp(0) {}
    MidiMessage(uint8_t status, uint8_t data1, uint8_t data2, int64_t ts = 0) 
        : size(3), timestamp(ts) {
        data[0] = status;
        data[1] = data1;
        data[2] = data2;
    }
};

class LockFreeEngine {
public:
    LockFreeEngine();
    ~LockFreeEngine();
    
    // 🎯 Echtzeit-Initialisierung
    bool initialize();
    
    // Engine Control
    bool start();
    void stop();
    
    // 🎵 Clock Control
    void setBpm(double bpm);
    void startClock();
    void stopClock();
    void setClockMode(int mode);
    
    // MIDI IO
    void sendMidiCC(int channel, int controller, int value);
    void sendMidiNote(int channel, int note, int velocity);
    void sendSysEx(const uint8_t* data, size_t size);
    
    // Statistik
    struct Stats {
        int64_t clock_ticks;
        int64_t midi_messages;
        int64_t max_latency_ns;
    };
    
    Stats getStats() const;

private:
    // ALSA
    snd_seq_t* seq_handle_;
    int duplex_port_;
    
    // 🚀 Echtzeit-Threads
    pthread_t clock_thread_;
    pthread_t midi_in_thread_;
    pthread_t midi_out_thread_;
    std::atomic<bool> running_{false};
    
    // 🔄 Lock-free Queues - KORRIGIERT
    static constexpr size_t QUEUE_SIZE = 1024;
    boost::lockfree::spsc_queue<MidiMessage, boost::lockfree::capacity<QUEUE_SIZE>> midi_out_queue_;
    boost::lockfree::spsc_queue<MidiMessage, boost::lockfree::capacity<QUEUE_SIZE>> midi_in_queue_;
    
    // 🎵 Atomic State
    std::atomic<double> bpm_{120.0};
    std::atomic<bool> clock_running_{false};
    std::atomic<int> clock_mode_{0}; // 0=internal, 1=master, 2=slave
    std::atomic<int64_t> tick_interval_ns_{2083333}; // 120 BPM
    std::atomic<int64_t> tick_counter_{0};
    
    // 📊 Atomic Statistics
    std::atomic<int64_t> stats_clock_ticks_{0};
    std::atomic<int64_t> stats_midi_messages_{0};
    std::atomic<int64_t> stats_max_latency_ns_{0};
    
    // Thread Functions
    static void* clockThread(void* arg);
    static void* midiInThread(void* arg);
    static void* midiOutThread(void* arg);
    
    // Internal
    void calculateInterval();
    void processClockTick();
    void processMidiInEvent(snd_seq_event_t* ev);
    void sendMidiMessage(const MidiMessage& msg);
    
    // 🔧 Echtzeit-Helper
    bool configureRealtime();
    void lockMemory();
};

#endif