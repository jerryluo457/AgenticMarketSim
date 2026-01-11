#ifndef ENGINE_INTERFACE_HPP
#define ENGINE_INTERFACE_HPP

#include <zmq.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <atomic>
#include <thread>
#include <chrono>
#include <map>

struct SimConfig {
    int num_makers, num_fundamental, num_momentum, num_noise;
};

struct UserOrder {
    bool is_buy; int quantity; double price;
};

enum class MarketScenario { NORMAL = 0, PUMP_DUMP = 1, SHORT_SQUEEZE = 2 };

struct AgentStats {
    long buy_vol = 0; long sell_vol = 0;
    void add(bool is_buy, int qty) { if (is_buy) buy_vol += qty; else sell_vol += qty; }
    void reset() { buy_vol = 0; sell_vol = 0; }
};

class EngineInterface {
private:
    zmq::context_t context;
    zmq::socket_t publisher; 
    zmq::socket_t command_sub; 
    bool is_paused;
    
public:
    EngineInterface() : context(1), publisher(context, ZMQ_PUB), command_sub(context, ZMQ_SUB), is_paused(false) {
        publisher.bind("tcp://127.0.0.1:5555");
        command_sub.bind("tcp://127.0.0.1:5556");
        command_sub.set(zmq::sockopt::subscribe, "");
        int timeout = 0;
        command_sub.set(zmq::sockopt::rcvtimeo, timeout);
    }

    SimConfig waitForStart() {
        std::cout << "Waiting for Python configuration..." << std::endl;
        int timeout = -1; command_sub.set(zmq::sockopt::rcvtimeo, timeout);
        while (true) {
            zmq::message_t msg;
            if (command_sub.recv(msg, zmq::recv_flags::none)) {
                std::string s(static_cast<char*>(msg.data()), msg.size());
                std::stringstream ss(s); std::string cmd; ss >> cmd;
                if (cmd == "START") {
                    SimConfig c; ss >> c.num_makers >> c.num_fundamental >> c.num_momentum >> c.num_noise;
                    command_sub.set(zmq::sockopt::rcvtimeo, 0); is_paused = false; return c;
                }
            }
        }
    }

    int checkCommands(std::vector<UserOrder>& new_orders) {
        int scenario_signal = -1;
        while (true) {
            zmq::message_t msg;
            if (!command_sub.recv(msg, zmq::recv_flags::dontwait)) {
                if (is_paused) { std::this_thread::sleep_for(std::chrono::milliseconds(50)); continue; }
                return scenario_signal; 
            }
            std::string s(static_cast<char*>(msg.data()), msg.size());
            std::stringstream ss(s); std::string cmd; ss >> cmd;

            if (cmd == "STOP") return -2; 
            if (cmd == "PAUSE") is_paused = true;
            if (cmd == "RESUME") is_paused = false;
            
            if (cmd == "SCENARIO") {
                int type; ss >> type;
                scenario_signal = type;
            }

            if (cmd == "ORDER") {
                int side, qty; double price; ss >> side >> qty >> price;
                new_orders.push_back({side == 0, qty, price});
            }
        }
    }

    void broadcastData(double price, uint32_t volume) {
        std::string msg = "DATA " + std::to_string(price) + " " + std::to_string(volume);
        zmq::message_t message(msg.data(), msg.size());
        publisher.send(message, zmq::send_flags::none);
    }

    void broadcastTrade(std::string agent, bool is_buy, int qty, double price) {
        std::stringstream ss; ss << "TRADE " << agent << " " << (is_buy ? "BUY" : "SELL") << " " << qty << " " << price;
        std::string s = ss.str(); zmq::message_t m(s.data(), s.size()); publisher.send(m, zmq::send_flags::none);
    }

    void broadcastSentiment(long fb, long fs, long mb, long ms, long mkb, long mks, long nb, long ns, long ub, long us) {
        std::stringstream ss;
        ss << "SENTIMENT " << fb << " " << fs << " " << mb << " " << ms << " " << mkb << " " << mks << " " << nb << " " << ns << " " << ub << " " << us;
        std::string s = ss.str(); zmq::message_t m(s.data(), s.size()); publisher.send(m, zmq::send_flags::none);
    }

    void broadcastScenarioMetrics(double hype, double bubble, long short_interest, double panic) {
        std::stringstream ss;
        ss << "SCENARIO_METRICS " << hype << " " << bubble << " " << short_interest << " " << panic;
        std::string s = ss.str(); zmq::message_t m(s.data(), s.size()); publisher.send(m, zmq::send_flags::none);
    }

    // ADDED: Missing function that caused the error
    void broadcastMetrics(double spread, long liquidity) {
        std::stringstream ss;
        ss << "METRICS " << spread << " " << liquidity;
        std::string s = ss.str(); zmq::message_t m(s.data(), s.size()); publisher.send(m, zmq::send_flags::none);
    }
};
#endif