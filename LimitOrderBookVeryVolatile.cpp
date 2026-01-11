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

class Agent { public: virtual ~Agent() = default; virtual std::optional<Order> act(double mid, double vol, double time, uint64_t& id) = 0; virtual std::string get_name() = 0; };
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
        if (time < next_act_time) return std::nullopt;
        next_act_time = time + wake_dist(gen);
        double my_fair_value = true_value * belief_noise;
        double deviation = (current_market_price - my_fair_value) / my_fair_value;
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
        if (time < next_act_time) return std::nullopt;
        next_act_time = time + wake_dist(gen);
        Side s = (std::uniform_real_distribution<>(0, 1)(gen) > 0.5) ? Side::BUY : Side::SELL;
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
        next_act_time = time + std::exponential_distribution<>(1.0 / reaction_speed)(gen);
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
    
    double time = 0.0; double price = 100.0; double true_value = 100.0; double realized_vol = 0.005; double vol_alpha = 0.01; double last_price = price;
    uint64_t oid = 1;
    AgentStats s_fund, s_mom, s_make, s_noise, s_user; int tick_count = 0;

    std::cout << "Very Volatile Engine Started." << std::endl;

    while (true) {
        auto start_tick = std::chrono::steady_clock::now();
        std::vector<UserOrder> user_orders; 
        if (!engine.checkCommands(user_orders)) break;

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
            if(filled_qty > 0) engine.broadcastTrade("USER", u.is_buy, filled_qty, total_val / filled_qty);
        }

        // 2. Fast Simulation (50Hz)
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
        for (auto& a : fundamental) process(a.act_with_market(true_value, mid, time, oid), s_fund);
        for (auto& a : noise) process(a.act(mid, realized_vol, time, oid), s_noise);
        for (auto& a : momentum) process(a.act(mid, realized_vol, time, oid), s_mom);

        if (price > 0) { double ret = std::log(price / last_price); realized_vol = (1 - vol_alpha) * realized_vol + vol_alpha * std::abs(ret); }
        last_price = price;
        
        // 3. Throttled Broadcast (5Hz)
        if (++tick_count % 10 == 0) {
            engine.broadcastSentiment(s_fund.buy_vol, s_fund.sell_vol, s_mom.buy_vol, s_mom.sell_vol, s_make.buy_vol, s_make.sell_vol, s_noise.buy_vol, s_noise.sell_vol, s_user.buy_vol, s_user.sell_vol);
            engine.broadcastData(price, tick_volume);
            s_fund.reset(); s_mom.reset(); s_make.reset(); s_noise.reset(); s_user.reset();
        }
        std::this_thread::sleep_until(start_tick + std::chrono::milliseconds(20)); // Fast Sim Loop
    }
    return 0;
}