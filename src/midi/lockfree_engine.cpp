#include "lockfree_engine.hpp"
#include <iostream>
#include <cstring>
#include <thread>
#include <poll.h>

LockFreeEngine::LockFreeEngine() : seq_handle_(nullptr), duplex_port_(-1) {
}

LockFreeEngine::~LockFreeEngine() {
    stop();
    if (seq_handle_) {
        snd_seq_close(seq_handle_);
    }
}

bool LockFreeEngine::initialize() {
    // 🔒 Memory locking
    lockMemory();
    
    // 🎵 ALSA Initialisierung
    if (snd_seq_open(&seq_handle_, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0) {
        std::cerr << "ERROR: Cannot open ALSA sequencer" << std::endl;
        return false;
    }
    
    snd_seq_set_client_name(seq_handle_, "Tauwerk_LockFree");
    snd_seq_set_output_buffer_size(seq_handle_, 65536);
    
    duplex_port_ = snd_seq_create_simple_port(seq_handle_, "Tauwerk",
        SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_WRITE |
        SND_SEQ_PORT_CAP_SUBS_READ | SND_SEQ_PORT_CAP_SUBS_WRITE,
        SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    
    calculateInterval();
    return true;
}

bool LockFreeEngine::start() {
    if (running_.exchange(true)) {
        return false; // Already running
    }
    
    // 🚀 Threads starten
    pthread_create(&clock_thread_, nullptr, &LockFreeEngine::clockThread, this);
    pthread_create(&midi_in_thread_, nullptr, &LockFreeEngine::midiInThread, this);
    pthread_create(&midi_out_thread_, nullptr, &LockFreeEngine::midiOutThread, this);
    
    std::cout << "LockFree Engine started" << std::endl;
    return true;
}

void LockFreeEngine::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    
    pthread_join(clock_thread_, nullptr);
    pthread_join(midi_in_thread_, nullptr);
    pthread_join(midi_out_thread_, nullptr);
    
    std::cout << "LockFree Engine stopped" << std::endl;
}

// 🎵 Clock Control Funktionen
void LockFreeEngine::setBpm(double bpm) {
    bpm_.store(bpm);
    calculateInterval();
    std::cout << "BPM set to: " << bpm << std::endl;
}

void LockFreeEngine::startClock() {
    clock_running_.store(true);
    std::cout << "Clock started" << std::endl;
}

void LockFreeEngine::stopClock() {
    clock_running_.store(false);
    std::cout << "Clock stopped" << std::endl;
}

void LockFreeEngine::setClockMode(int mode) {
    clock_mode_.store(mode);
    std::cout << "Clock mode set to: " << mode << std::endl;
}

// Thread Implementations
void* LockFreeEngine::clockThread(void* arg) {
    LockFreeEngine* engine = static_cast<LockFreeEngine*>(arg);
    
    auto next_tick = std::chrono::steady_clock::now();
    
    while (engine->running_.load()) {
        if (engine->clock_running_.load()) {
            auto now = std::chrono::steady_clock::now();
            
            if (now >= next_tick) {
                engine->processClockTick();
                
                int64_t interval = engine->tick_interval_ns_.load();
                next_tick += std::chrono::nanoseconds(interval);
                engine->stats_clock_ticks_.fetch_add(1);
            }
            
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    
    return nullptr;
}

void* LockFreeEngine::midiInThread(void* arg) {
    LockFreeEngine* engine = static_cast<LockFreeEngine*>(arg);
    
    // Polling setup
    int npfd = snd_seq_poll_descriptors_count(engine->seq_handle_, POLLIN);
    struct pollfd pfds[npfd];
    snd_seq_poll_descriptors(engine->seq_handle_, pfds, npfd, POLLIN);
    
    while (engine->running_.load()) {
        if (poll(pfds, npfd, 100) > 0) { // 100ms timeout
            snd_seq_event_t* ev = nullptr;
            
            while (snd_seq_event_input(engine->seq_handle_, &ev) > 0) {
                if (ev) {
                    engine->processMidiInEvent(ev);
                    snd_seq_free_event(ev);
                }
            }
        }
    }
    
    return nullptr;
}

void* LockFreeEngine::midiOutThread(void* arg) {
    LockFreeEngine* engine = static_cast<LockFreeEngine*>(arg);
    
    while (engine->running_.load()) {
        MidiMessage msg;
        
        // 🔄 Outgoing Messages verarbeiten
        while (engine->midi_out_queue_.pop(msg)) {
            engine->sendMidiMessage(msg);
        }
        
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    
    return nullptr;
}

void LockFreeEngine::processMidiInEvent(snd_seq_event_t* ev) {
    int64_t timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    switch (ev->type) {
        case SND_SEQ_EVENT_CLOCK:
            // External Clock
            if (clock_mode_.load() == 2) {
                tick_counter_.fetch_add(1);
            }
            break;
            
        case SND_SEQ_EVENT_NOTEON: {
            MidiMessage msg(0x90 | ev->data.note.channel, 
                           ev->data.note.note, 
                           ev->data.note.velocity,
                           timestamp);
            if (!midi_in_queue_.push(msg)) {
                std::cerr << "MIDI input queue full!" << std::endl;
            }
            break;
        }
            
        case SND_SEQ_EVENT_CONTROLLER: {
            MidiMessage msg(0xB0 | ev->data.control.channel,
                           ev->data.control.param,
                           ev->data.control.value,
                           timestamp);
            if (!midi_in_queue_.push(msg)) {
                std::cerr << "MIDI input queue full!" << std::endl;
            }
            break;
        }
            
        case SND_SEQ_EVENT_START:
            std::cout << "MIDI Start received" << std::endl;
            break;
            
        case SND_SEQ_EVENT_STOP:
            std::cout << "MIDI Stop received" << std::endl;
            break;
            
        case SND_SEQ_EVENT_CONTINUE:
            std::cout << "MIDI Continue received" << std::endl;
            break;
    }
    
    stats_midi_messages_.fetch_add(1);
}

void LockFreeEngine::processClockTick() {
    tick_counter_.fetch_add(1);
    
    // Master Mode: MIDI Clock senden
    if (clock_mode_.load() == 1) {
        MidiMessage clock_msg(0xF8, 0, 0); // MIDI Clock
        if (!midi_out_queue_.push(clock_msg)) {
            std::cerr << "MIDI output queue full!" << std::endl;
        }
    }
}

void LockFreeEngine::sendMidiMessage(const MidiMessage& msg) {
    snd_seq_event_t ev;
    snd_seq_ev_clear(&ev);
    
    switch (msg.data[0] & 0xF0) {
        case 0x80: // Note Off
            snd_seq_ev_set_noteoff(&ev, msg.data[0] & 0x0F, msg.data[1], msg.data[2]);
            break;
        case 0x90: // Note On
            snd_seq_ev_set_noteon(&ev, msg.data[0] & 0x0F, msg.data[1], msg.data[2]);
            break;
        case 0xB0: // Control Change
            snd_seq_ev_set_controller(&ev, msg.data[0] & 0x0F, msg.data[1], msg.data[2]);
            break;
        case 0xF0: // System
            if (msg.data[0] == 0xF8) { // Clock
                ev.type = SND_SEQ_EVENT_CLOCK;
            }
            break;
    }
    
    if (ev.type != SND_SEQ_EVENT_NONE) {
        snd_seq_ev_set_source(&ev, duplex_port_);
        snd_seq_ev_set_subs(&ev);
        snd_seq_ev_set_direct(&ev);
        snd_seq_event_output_direct(seq_handle_, &ev);
    }
}

void LockFreeEngine::sendMidiCC(int channel, int controller, int value) {
    MidiMessage msg(0xB0 | channel, controller, value);
    if (!midi_out_queue_.push(msg)) {
        std::cerr << "MIDI output queue full!" << std::endl;
    }
}

void LockFreeEngine::sendMidiNote(int channel, int note, int velocity) {
    uint8_t status = velocity > 0 ? 0x90 : 0x80;
    MidiMessage msg(status | channel, note, velocity);
    if (!midi_out_queue_.push(msg)) {
        std::cerr << "MIDI output queue full!" << std::endl;
    }
}

void LockFreeEngine::sendSysEx(const uint8_t* data, size_t size) {
    // Einfache SysEx Implementation
    snd_seq_event_t ev;
    snd_seq_ev_clear(&ev);
    ev.type = SND_SEQ_EVENT_SYSEX;
    ev.data.ext.len = size;
    ev.data.ext.ptr = const_cast<uint8_t*>(data);
    
    snd_seq_ev_set_source(&ev, duplex_port_);
    snd_seq_ev_set_subs(&ev);
    snd_seq_ev_set_direct(&ev);
    snd_seq_event_output_direct(seq_handle_, &ev);
}

void LockFreeEngine::calculateInterval() {
    double bpm = bpm_.load();
    if (bpm < 20.0) bpm = 20.0;
    if (bpm > 300.0) bpm = 300.0;
    
    int64_t ns_per_tick = static_cast<int64_t>(60'000'000'000 / (bpm * 24));
    tick_interval_ns_.store(ns_per_tick);
}

void LockFreeEngine::lockMemory() {
    if (mlockall(MCL_CURRENT | MCL_FUTURE) == -1) {
        std::cerr << "WARNING: Cannot lock memory - " << strerror(errno) << std::endl;
    }
}

LockFreeEngine::Stats LockFreeEngine::getStats() const {
    return Stats{
        stats_clock_ticks_.load(),
        stats_midi_messages_.load(),
        stats_max_latency_ns_.load()
    };
}