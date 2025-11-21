// ipc_server.cpp
#include "lockfree_engine.hpp"
#include <zmq.hpp>
#include <thread>
#include <iostream>
#include <json/json.h>

class IPCServer {
public:
    IPCServer(LockFreeEngine& engine) : engine_(engine), running_(false) {}
    
    bool start() {
        if (running_.exchange(true)) return false;
        
        context_ = std::make_unique<zmq::context_t>(1);
        socket_ = std::make_unique<zmq::socket_t>(*context_, ZMQ_PAIR);
        socket_->bind("ipc:///tmp/tauwerk_midi");
        
        thread_ = std::thread(&IPCServer::run, this);
        std::cout << "IPC Server started" << std::endl;
        return true;
    }
    
    void stop() {
        if (!running_.exchange(false)) return;
        
        if (thread_.joinable()) {
            thread_.join();
        }
        
        socket_->close();
        context_->close();
        std::cout << "IPC Server stopped" << std::endl;
    }
    
private:
    void run() {
        while (running_.load()) {
            zmq::message_t message;
            
            if (socket_->recv(message, zmq::recv_flags::dontwait)) {
                processMessage(std::string(static_cast<char*>(message.data()), message.size()));
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    
    void processMessage(const std::string& json_str) {
        try {
            Json::Value root;
            Json::Reader reader;
            
            if (reader.parse(json_str, root)) {
                std::string type = root["type"].asString();
                
                if (type == "cc") {
                    int channel = root["channel"].asInt();
                    int controller = root["controller"].asInt();
                    int value = root["value"].asInt();
                    engine_.sendMidiCC(channel, controller, value);
                    std::cout << "IPC: CC ch:" << channel << " ctrl:" << controller << " val:" << value << std::endl;
                }
                else if (type == "note") {
                    int channel = root["channel"].asInt();
                    int note = root["note"].asInt();
                    int velocity = root["velocity"].asInt();
                    engine_.sendMidiNote(channel, note, velocity);
                    std::cout << "IPC: Note ch:" << channel << " note:" << note << " vel:" << velocity << std::endl;
                }
                else if (type == "bpm") {
                    double bpm = root["bpm"].asDouble();
                    engine_.setBpm(bpm);
                    std::cout << "IPC: BPM set to " << bpm << std::endl;
                }
                else if (type == "clock_mode") {
                    int mode = root["mode"].asInt();
                    engine_.setClockMode(mode);
                    std::cout << "IPC: Clock mode set to " << mode << std::endl;
                }
                else if (type == "clock_start") {
                    engine_.startClock();
                    std::cout << "IPC: Clock started" << std::endl;
                }
                else if (type == "clock_stop") {
                    engine_.stopClock();
                    std::cout << "IPC: Clock stopped" << std::endl;
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "IPC Error: " << e.what() << std::endl;
        }
    }
    
    LockFreeEngine& engine_;
    std::atomic<bool> running_;
    std::unique_ptr<zmq::context_t> context_;
    std::unique_ptr<zmq::socket_t> socket_;
    std::thread thread_;
};