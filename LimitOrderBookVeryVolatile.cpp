#include "EngineInterface.hpp"
#include <vector>
#include <queue>
#include <unordered_map>
#include <optional>
#include <cstdint>
#include <iostream>
#include <random>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <thread>

enum class Side { BUY, SELL };
struct Order { uint64_t id; double timestamp; double price; uint32_t quantity; Side side; };
struct Trade { double price; uint32_t quantity; double timestamp; };

class LimitOrderBook {
private:
    std::unordered_map<uint64_t, Order> active_orders;
    struct askComp { bool operator()(const Order& a, const Order& b) const { return a.price != b.price ? a.price > b.price : a.timestamp > b.timestamp; } };
    struct bidComp { bool operator()(const Order& a, const Order& b) const { return a.price != b.price ? a.price < b.price : a.timestamp > b.timestamp; } };
public:
    std::priority_queue<Order, std::vector<Order>, askComp> askHeap;
    std::priority_queue<Order, std::vector<Order>, bidComp> bidHeap;
    LimitOrderBook() { active_orders.reserve(500000); }
    double get_mid(double fallback) { if (askHeap.empty() || bidHeap.empty()) return fallback; return 0.5 * (askHeap.top().price + bidHeap.top().price); }
    
    void clean_heaps() {
        while (!askHeap.empty() && !active_orders.count(askHeap.top().id)) askHeap.pop();
        while (!bidHeap.empty() && !active_orders.count(bidHeap.top().id)) bidHeap.pop();
    }

    std::pair<double, long> get_metrics() {
        clean_heaps();
        double spread = 0.0;
        long liquidity = 0;
        if (!askHeap.empty() && !bidHeap.empty()) {
            spread = askHeap.top().price - bidHeap.top().price;
            liquidity = askHeap.top().quantity + bidHeap.top().quantity;
        }
        return {spread, liquidity};
    }

    void decay(double percentage, std::mt19937& gen) {
        if (active_orders.empty()) return;
        std::vector<uint64_t> to_delete;
        std::uniform_real_distribution<> dist(0.0, 1.0);
        for (auto const& [id, order] : active_orders) { if (dist(gen) < percentage) to_delete.push_back(id); }
        for (uint64_t id : to_delete) active_orders.erase(id);
    }

    std::vector<Trade> add_order(Order order) {
        std::vector<Trade> trades;
        if (order.side == Side::SELL) {
            while (order.quantity > 0 && !bidHeap.empty()) {
                if (!active_orders.count(bidHeap.top().id)) { bidHeap.pop(); continue; }
                const Order& best = bidHeap.top(); if (best.price < order.price) break;
                uint32_t qty = std::min(best.quantity, order.quantity); trades.push_back({best.price, qty, order.timestamp});
                if (best.quantity > qty) { Order updated = best; updated.quantity -= qty; active_orders[best.id] = updated; bidHeap.pop(); bidHeap.push(updated); } else { active_orders.erase(best.id); bidHeap.pop(); }
                order.quantity -= qty;
            }
            if (order.quantity > 0) { active_orders[order.id] = order; askHeap.push(order); }
        } else {
            while (order.quantity > 0 && !askHeap.empty()) {
                if (!active_orders.count(askHeap.top().id)) { askHeap.pop(); continue; }
                const Order& best = askHeap.top(); if (best.price > order.price) break;
                uint32_t qty = std::min(best.quantity, order.quantity); trades.push_back({best.price, qty, order.timestamp});
                if (best.quantity > qty) { Order updated = best; updated.quantity -= qty; active_orders[best.id] = updated; askHeap.pop(); askHeap.push(updated); } else { active_orders.erase(best.id); askHeap.pop(); }
                order.quantity -= qty;
            }
            if (order.quantity > 0) { active_orders[order.id] = order; bidHeap.push(order); }
        }
        return trades;
    }
};

class Agent { 
public: 
    virtual ~Agent() = default; 
    virtual std::optional<Order> act(double mid, double vol, double time, uint64_t& id) = 0; 
    virtual std::string get_name() = 0; 
    MarketScenario current_scenario = MarketScenario::NORMAL; 
    
    // Track Peak Price for Pump & Dump Crash Logic
    static double peak_price; 
    void update_peak(double p) { if (p > peak_price) peak_price = p; }
    void set_scenario(MarketScenario s) { 
        current_scenario = s; 
        if(s != MarketScenario::PUMP_DUMP) peak_price = 0.0; 
    }
};
double Agent::peak_price = 0.0;

class MarketMaker : public Agent {
    std::mt19937 gen; std::exponential_distribution<> wake_dist; std::uniform_int_distribution<> size_dist; std::uniform_real_distribution<> spread_jitter; double next_act_time;
public:
    MarketMaker(unsigned int seed) : gen(seed), size_dist(100, 500), spread_jitter(0.9, 1.1) { wake_dist = std::exponential_distribution<>(1.0/1.5); next_act_time = 0; }
    std::string get_name() override { return "MARKET_MAKER"; }
    std::optional<Order> act(double mid, double vol, double time, uint64_t& id) override {
        if (time < next_act_time) return std::nullopt;
        next_act_time = time + wake_dist(gen);
        Side s = (std::uniform_real_distribution<>(0, 1)(gen) > 0.5) ? Side::BUY : Side::SELL;
        double spread = std::max(0.01, 0.2 * vol * mid) * spread_jitter(gen);
        
        // PUMP: Widen spreads to allow vertical moves
        if (current_scenario == MarketScenario::PUMP_DUMP) spread *= 4.0; 

        double p = (s == Side::BUY) ? mid - spread : mid + spread; if(p<0.01) p=0.01;
        return Order{id++, time, p, (uint32_t)size_dist(gen), s};
    }
};

class FundamentalTrader : public Agent {
    std::mt19937 gen; std::exponential_distribution<> wake_dist; double belief_noise; double next_act_time;
public:
    FundamentalTrader(unsigned int seed) : gen(seed) { wake_dist = std::exponential_distribution<>(1.0/5.0); std::normal_distribution<> bias(1.0, 0.005); belief_noise = bias(gen); next_act_time = 0; }
    std::string get_name() override { return "FUNDAMENTAL"; }
    
    std::optional<Order> act_with_market(double true_value, double current_market_price, double time, uint64_t& id) {
        update_peak(current_market_price);
        
        // PUMP FIX: Fast wake up (0.5s mean) to ensure activity
        if (time < next_act_time) return std::nullopt;
        double delay_mean = (current_scenario == MarketScenario::PUMP_DUMP) ? 0.5 : 5.0;
        next_act_time = time + std::exponential_distribution<>(1.0/delay_mean)(gen);
        
        double my_fair_value = true_value * belief_noise;
        if (current_scenario == MarketScenario::SHORT_SQUEEZE) my_fair_value *= 0.95; 

        double deviation = (current_market_price - my_fair_value) / my_fair_value;
        
        // --- PUMP & DUMP LOGIC ---
        if (current_scenario == MarketScenario::PUMP_DUMP) {
            if (std::abs(deviation) < 0.005) return std::nullopt; 
            
            // Consistent Volume (60% of normal)
            uint32_t qty = 50 + static_cast<uint32_t>((std::abs(deviation)/0.02) * 400);
            qty = std::max(20u, (uint32_t)(qty * 0.6)); 
            
            if (deviation > 0) {
                // Mix of Passive (Ladder) and Aggressive (Market Sell)
                if (std::uniform_real_distribution<>(0, 1)(gen) < 0.3) {
                    return Order{id++, time, current_market_price * 0.99, qty, Side::SELL};
                } else {
                    std::uniform_real_distribution<> ladder(1.005, 1.02); 
                    return Order{id++, time, current_market_price * ladder(gen), qty, Side::SELL};
                }
            } else {
                return Order{id++, time, current_market_price * 0.99, qty, Side::BUY};
            }
        }
        // --- SHORT SQUEEZE LOGIC ---
        else if (current_scenario == MarketScenario::SHORT_SQUEEZE) {
            if (deviation > 0.15) return Order{id++, time, current_market_price * 1.02, 5000, Side::BUY}; 
            else if (deviation > 0) {
                uint32_t qty = 50 + static_cast<uint32_t>(std::min(1.0, std::abs(deviation)/0.02) * 400);
                qty *= 3; 
                return Order{id++, time, current_market_price * 0.995, qty, Side::SELL};
            }
        }
        
        // --- NORMAL LOGIC ---
        double aggressiveness = std::min(1.0, std::abs(deviation) / 0.02);
        uint32_t qty = 50 + static_cast<uint32_t>(aggressiveness * 400);
        if (deviation > 0) return Order{id++, time, (1.0 - aggressiveness) * my_fair_value + aggressiveness * (current_market_price * 0.998), qty, Side::SELL};
        else return Order{id++, time, (1.0 - aggressiveness) * my_fair_value + aggressiveness * (current_market_price * 1.002), qty, Side::BUY};
    }
    std::optional<Order> act(double mid, double vol, double time, uint64_t& id) override { return std::nullopt; }
};

class NoiseTrader : public Agent {
    std::mt19937 gen; std::exponential_distribution<> wake_dist; std::lognormal_distribution<> size_dist; std::normal_distribution<> impact_dist; double next_act_time;
public:
    NoiseTrader(unsigned int seed) : gen(seed), size_dist(4.0, 0.5), impact_dist(0.0, 1.0) { wake_dist = std::exponential_distribution<>(1.0/15.0); next_act_time = 0; }
    std::string get_name() override { return "NOISE"; }
    std::optional<Order> act(double mid, double vol, double time, uint64_t& id) override {
        update_peak(mid);
        if (time < next_act_time) return std::nullopt;
        
        double wake_speed = (current_scenario == MarketScenario::PUMP_DUMP) ? 5.0 : 1.0; 
        next_act_time = time + std::exponential_distribution<>(1.0/15.0 * wake_speed)(gen);
        
        Side s;
        
        // --- PUMP & DUMP LOGIC ---
        if (current_scenario == MarketScenario::PUMP_DUMP) {
            // CASCADING PANIC LOGIC
            double drawdown = (peak_price > 0) ? (peak_price - mid) / peak_price : 0.0;
            
            // 90% STARTING HYPE (0.9 base)
            double buy_prob = 0.9 - (drawdown * 8.0);
            
            if (buy_prob < 0.05) {
                // FULL PANIC
                s = Side::SELL;
                uint32_t panic_qty = std::min(2000u, std::max(100u, (uint32_t)size_dist(gen) * 8)); 
                return Order{id++, time, mid * 0.85, panic_qty, s}; 
            } 
            else {
                // Hype / Wavering State
                s = (std::uniform_real_distribution<>(0, 1)(gen) < buy_prob) ? Side::BUY : Side::SELL;
                
                // JITTER
                double size_mult = (std::uniform_real_distribution<>(0, 1)(gen) < 0.2) ? 3.0 : 1.5;
                uint32_t qty = std::min(500u, std::max(1u, (uint32_t)(size_dist(gen) * size_mult)));
                
                if (s == Side::BUY) return Order{id++, time, mid * 1.05, qty, s}; 
                else return Order{id++, time, mid * 0.95, qty, s}; 
            }
        }
        // --- SHORT SQUEEZE LOGIC ---
        else if (current_scenario == MarketScenario::SHORT_SQUEEZE) {
            //modify short squeeze, 65% sale probability normalized
            s = (std::uniform_real_distribution<>(0, 1)(gen) > 0.65) ? Side::BUY : Side::SELL;
        }
        // --- NORMAL LOGIC ---
        else {
            s = (std::uniform_real_distribution<>(0, 1)(gen) > 0.5) ? Side::BUY : Side::SELL;
        }

        // Common Execution for Normal/Squeeze
        double impact = std::abs(impact_dist(gen)) * (0.05 + 0.5 * vol) * mid;
        double p = (s == Side::BUY) ? mid + impact : mid - impact; if(p<0.01) p=0.01;
        uint32_t qty = std::min(200u, std::max(1u, (uint32_t)size_dist(gen)));
        return Order{id++, time, p, qty, s};
    }
};

class MomentumTrader : public Agent {
    std::mt19937 gen; double ema_s, ema_l; double next_act_time; double reaction_speed;
public:
    MomentumTrader(unsigned int seed, double p) : gen(seed), ema_s(p), ema_l(p) { reaction_speed = 3.0; next_act_time = 20.0; }
    std::string get_name() override { return "MOMENTUM"; }
    std::optional<Order> act(double mid, double vol, double time, uint64_t& id) override {
        ema_s = 0.05 * mid + 0.95 * ema_s; ema_l = 0.01 * mid + 0.99 * ema_l; 
        if (time < next_act_time) return std::nullopt;
        
        double speed = (current_scenario == MarketScenario::NORMAL) ? reaction_speed : reaction_speed * 3.0;
        next_act_time = time + std::exponential_distribution<>(1.0 / speed)(gen);
        
        double signal = ema_s - ema_l; double offset = 0.05 * vol * mid;
        if (signal > offset) return Order{id++, time, mid + offset, 50, Side::BUY}; 
        if (signal < -offset) return Order{id++, time, mid - offset, 50, Side::SELL};
        return std::nullopt;
    }
};

int main() {
    EngineInterface engine; SimConfig config = engine.waitForStart(); LimitOrderBook book;
    
    double annual_return = 0.28; double annual_volatility = 1.50; 
    double seconds_per_year = 252 * 6.5 * 60 * 60; double dt = 60.0; 
    
    std::random_device rd; std::mt19937 gen(rd()); std::normal_distribution<> Z(0.0, 1.0);
    std::vector<MarketMaker> makers; for (int i=0; i<config.num_makers; ++i) makers.emplace_back(rd());
    std::vector<NoiseTrader> noise; for (int i=0; i<config.num_noise; ++i) noise.emplace_back(rd());
    std::vector<MomentumTrader> momentum; for (int i=0; i<config.num_momentum; ++i) momentum.emplace_back(rd(), 100.0);
    std::vector<FundamentalTrader> fundamental; for (int i=0; i<config.num_fundamental; ++i) fundamental.emplace_back(rd());
    
    double time = 0.0; double price = 100.0; double true_value = 100.0; 
    double realized_vol = 0.005; double vol_alpha = 0.01; double last_price = price;
    uint64_t oid = 1;
    AgentStats s_fund, s_mom, s_make, s_noise, s_user; int tick_count = 0;
    
    // FIX: Initialize peak_price to start price to ensure hype starts at 90% immediately
    Agent::peak_price = 100.0; 
    long short_interest = 0; 
    MarketScenario current_scen = MarketScenario::NORMAL;

    std::cout << "Very Volatile Engine Started." << std::endl;

    while (true) {
        auto start_tick = std::chrono::steady_clock::now();
        std::vector<UserOrder> user_orders; 
        
        int status = engine.checkCommands(user_orders);
        if (status == -2) break;
        if (status >= 0) {
            current_scen = static_cast<MarketScenario>(status);
            for(auto& a : makers) a.set_scenario(current_scen);
            for(auto& a : noise) a.set_scenario(current_scen);
            for(auto& a : momentum) a.set_scenario(current_scen);
            for(auto& a : fundamental) a.set_scenario(current_scen);
        }

        uint32_t tick_volume = 0; 

        // 1. Process User
        for(auto& u : user_orders) {
            Order o = {oid++, time, u.price, (uint32_t)u.quantity, u.is_buy ? Side::BUY : Side::SELL};
            auto trades = book.add_order(o);
            uint32_t filled_qty = 0; double total_val = 0;
            for(auto& t : trades) { 
                tick_volume += t.quantity; price = t.price; s_user.add(u.is_buy, t.quantity);
                filled_qty += t.quantity; total_val += t.quantity * t.price;
            }
            if(filled_qty > 0) engine.broadcastTrade("USER", u.is_buy, filled_qty, total_val/filled_qty);
        }

        // 2. Fast Sim
        time += dt;
        double dt_year = dt / seconds_per_year;
        double drift = (annual_return - 0.5 * std::pow(annual_volatility, 2)) * dt_year;
        double shock = annual_volatility * std::sqrt(dt_year) * Z(gen);
        true_value *= std::exp(drift + shock);
        double mid = book.get_mid(price);

        auto process = [&](std::optional<Order> o, AgentStats& stats) {
            if (o) {
                auto trades = book.add_order(*o);
                for (auto& t : trades) { 
                    tick_volume += t.quantity; price = t.price; stats.add(o->side == Side::BUY, t.quantity); 
                }
            }
        };
        for (auto& a : makers) process(a.act(mid, realized_vol, time, oid), s_make);
        
        for (auto& a : fundamental) {
            std::optional<Order> o = a.act_with_market(true_value, mid, time, oid);
            if (o) {
                auto trades = book.add_order(*o);
                for(auto& t : trades) {
                    tick_volume += t.quantity; price = t.price; s_fund.add(o->side == Side::BUY, t.quantity);
                    if (o->side == Side::SELL) short_interest += t.quantity;
                    else short_interest -= t.quantity;
                }
            }
        }
        
        for (auto& a : noise) process(a.act(mid, realized_vol, time, oid), s_noise);
        for (auto& a : momentum) process(a.act(mid, realized_vol, time, oid), s_mom);

        if (price > 0) { double ret = std::log(price / last_price); realized_vol = (1 - vol_alpha) * realized_vol + vol_alpha * std::abs(ret); }
        last_price = price;
        
        // 3. Throttled Broadcast (5Hz)
        if (++tick_count % 10 == 0) {
            book.decay(0.05, gen);
            engine.broadcastSentiment(s_fund.buy_vol, s_fund.sell_vol, s_mom.buy_vol, s_mom.sell_vol, s_make.buy_vol, s_make.sell_vol, s_noise.buy_vol, s_noise.sell_vol, s_user.buy_vol, s_user.sell_vol);
            
            // Dynamic Hype Metric Calculation
            double drawdown = (Agent::peak_price > 0) ? (Agent::peak_price - price) / Agent::peak_price : 0.0;
            // 90% Start (0.9 base), reduces as drawdown increases
            double hype_val = (current_scen == MarketScenario::PUMP_DUMP) ? std::max(0.0, (0.9 - (drawdown * 8.0)) * 100.0) : 0.0;

            double bubble_ratio = (price > true_value) ? ((price - true_value) / true_value) * 100.0 : 0.0;
            double panic_meter = (current_scen == MarketScenario::SHORT_SQUEEZE) ? std::min(100.0, bubble_ratio * 3.0) : 0.0;
            
            engine.broadcastScenarioMetrics(hype_val, bubble_ratio, short_interest, panic_meter);
            engine.broadcastData(price, tick_volume);
            
            auto [spread, liq] = book.get_metrics();
            engine.broadcastMetrics(spread, liq);

            s_fund.reset(); s_mom.reset(); s_make.reset(); s_noise.reset(); s_user.reset();
        }
        std::this_thread::sleep_until(start_tick + std::chrono::milliseconds(20)); 
    }
    return 0;
}