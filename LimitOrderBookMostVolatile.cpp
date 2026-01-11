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
    double last_traded_price;

    LimitOrderBook() { active_orders.reserve(500000); last_traded_price = 100.0; }

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
                const Order& best = bidHeap.top();
                if (best.price < order.price) break;
                uint32_t qty = std::min(best.quantity, order.quantity);
                trades.push_back({best.price, qty, order.timestamp});
                last_traded_price = best.price;
                if (best.quantity > qty) { Order updated = best; updated.quantity -= qty; active_orders[best.id] = updated; bidHeap.pop(); bidHeap.push(updated); } 
                else { active_orders.erase(best.id); bidHeap.pop(); }
                order.quantity -= qty;
            }
            if (order.quantity > 0) { active_orders[order.id] = order; askHeap.push(order); }
        } else {
            while (order.quantity > 0 && !askHeap.empty()) {
                if (!active_orders.count(askHeap.top().id)) { askHeap.pop(); continue; }
                const Order& best = askHeap.top();
                if (best.price > order.price) break;
                uint32_t qty = std::min(best.quantity, order.quantity);
                trades.push_back({best.price, qty, order.timestamp});
                last_traded_price = best.price;
                if (best.quantity > qty) { Order updated = best; updated.quantity -= qty; active_orders[best.id] = updated; askHeap.pop(); askHeap.push(updated); } 
                else { active_orders.erase(best.id); askHeap.pop(); }
                order.quantity -= qty;
            }
            if (order.quantity > 0) { active_orders[order.id] = order; bidHeap.push(order); }
        }
        return trades;
    }
};

class Agent { public: virtual ~Agent() = default; virtual std::optional<Order> act(double ref_price, double time, uint64_t& id) = 0; virtual std::string get_name() = 0; };

class MarketMaker : public Agent {
    std::mt19937 gen; std::exponential_distribution<> wake_dist; std::uniform_int_distribution<> size_dist; double next_act_time;
public:
    MarketMaker(unsigned int seed) : gen(seed), size_dist(10, 100) { wake_dist = std::exponential_distribution<>(1.0/10.0); next_act_time = 0; }
    std::string get_name() override { return "MARKET_MAKER"; }
    std::optional<Order> act(double ref_price, double time, uint64_t& id) override {
        if (time < next_act_time) return std::nullopt;
        next_act_time = time + wake_dist(gen);
        Side s = (std::uniform_real_distribution<>(0, 1)(gen) > 0.5) ? Side::BUY : Side::SELL;
        std::uniform_real_distribution<> spread_dist(0.002, 0.01); 
        double spread = ref_price * spread_dist(gen);
        double p = (s == Side::BUY) ? ref_price - spread : ref_price + spread;
        if (p < 0.01) p = 0.01;
        return Order{id++, time, p, (uint32_t)size_dist(gen), s};
    }
};

class FundamentalTrader : public Agent {
    std::mt19937 gen; double belief_noise; double next_act_time;
public:
    FundamentalTrader(unsigned int seed) : gen(seed) { std::normal_distribution<> bias(1.0, 0.05); belief_noise = bias(gen); next_act_time = 0; }
    std::string get_name() override { return "FUNDAMENTAL"; }
    std::optional<Order> act_with_market(double true_value, double current_price, double time, uint64_t& id) {
        if (time < next_act_time) return std::nullopt;
        next_act_time = time + std::uniform_real_distribution<>(0.1, 0.5)(gen);
        double my_fair_value = true_value * belief_noise;
        double deviation = (current_price - my_fair_value) / my_fair_value;
        if (std::abs(deviation) < 0.01) return std::nullopt;
        uint32_t qty = 300; 
        if (deviation > 0) return Order{id++, time, current_price * 0.99, qty, Side::SELL};
        else return Order{id++, time, current_price * 1.01, qty, Side::BUY};
    }
    std::optional<Order> act(double ref_price, double time, uint64_t& id) override { return std::nullopt; }
};

class NoiseTrader : public Agent {
    std::mt19937 gen; std::exponential_distribution<> wake_dist; std::lognormal_distribution<> size_dist; std::normal_distribution<> impact_dist; double next_act_time;
public:
    NoiseTrader(unsigned int seed) : gen(seed), size_dist(4.0, 0.5), impact_dist(0.0, 1.0) { wake_dist = std::exponential_distribution<>(1.0/5.0); next_act_time = 0; }
    std::string get_name() override { return "NOISE"; }
    std::optional<Order> act(double ref_price, double time, uint64_t& id) override {
        if (time < next_act_time) return std::nullopt;
        next_act_time = time + wake_dist(gen);
        Side s = (std::uniform_real_distribution<>(0, 1)(gen) > 0.5) ? Side::BUY : Side::SELL;
        double impact = std::abs(impact_dist(gen)) * (0.01 + 0.05 * ref_price); // Random impact
        double p = (s == Side::BUY) ? ref_price + impact : ref_price - impact;
        if (p < 0.01) p = 0.01;
        uint32_t qty = std::min(200u, std::max(1u, (uint32_t)size_dist(gen)));
        return Order{id++, time, p, qty, s};
    }
};

class MomentumTrader : public Agent {
    std::mt19937 gen; double ema_s, ema_l; double next_act_time; double reaction_speed;
public:
    MomentumTrader(unsigned int seed, double p) : gen(seed), ema_s(p), ema_l(p) { reaction_speed = 3.0; next_act_time = 10.0; }
    std::string get_name() override { return "MOMENTUM"; }
    std::optional<Order> act(double ref_price, double time, uint64_t& id) override {
        ema_s = 0.05 * ref_price + 0.95 * ema_s; ema_l = 0.01 * ref_price + 0.99 * ema_l; 
        if (time < next_act_time) return std::nullopt;
        next_act_time = time + std::exponential_distribution<>(1.0 / reaction_speed)(gen);
        double signal = ema_s - ema_l; double offset = 0.0002 * ref_price; 
        if (signal > offset) return Order{id++, time, ref_price + offset, 50, Side::BUY}; 
        if (signal < -offset) return Order{id++, time, ref_price - offset, 50, Side::SELL};
        return std::nullopt;
    }
};

int main() {
    EngineInterface engine; SimConfig config = engine.waitForStart(); LimitOrderBook book;
    double time = 0.0, dt = 60.0, true_value = 100.0; uint64_t oid = 1;
    
    std::random_device rd; std::mt19937 gen(rd()); std::normal_distribution<> Z(0.0, 1.0);
    std::vector<MarketMaker> makers; for (int i = 0; i < config.num_makers; ++i) makers.emplace_back(rd());
    std::vector<FundamentalTrader> fundamental; for (int i = 0; i < config.num_fundamental; ++i) fundamental.emplace_back(rd());
    std::vector<NoiseTrader> noise; for (int i = 0; i < config.num_noise; ++i) noise.emplace_back(rd());
    std::vector<MomentumTrader> momentum; for (int i = 0; i < config.num_momentum; ++i) momentum.emplace_back(rd(), 100.0);
    
    AgentStats s_fund, s_mom, s_make, s_noise, s_user; int tick_count = 0;

    std::cout << "Most Volatile Engine Started." << std::endl;

    while (true) { 
        auto start_tick = std::chrono::steady_clock::now();
        std::vector<UserOrder> user_orders; 
        if (!engine.checkCommands(user_orders)) break;

        uint32_t tick_volume = 0;
        
        for(auto& u : user_orders) {
            Order o = {oid++, time, u.price, (uint32_t)u.quantity, u.is_buy ? Side::BUY : Side::SELL};
            auto trades = book.add_order(o);
            uint32_t filled_qty = 0; double total_val = 0.0;
            for(auto& t : trades) { 
                tick_volume += t.quantity; book.last_traded_price = t.price; s_user.add(u.is_buy, t.quantity);
                filled_qty += t.quantity; total_val += (t.price * t.quantity);
            }
            if(filled_qty > 0) engine.broadcastTrade("USER", u.is_buy, filled_qty, total_val/filled_qty);
        }

        double shock = 0.01 * Z(gen); true_value *= std::exp(shock); 
        double ref_price = book.last_traded_price;

        auto process_agent = [&](std::optional<Order> o, AgentStats& stats) {
            if (o) {
                auto trades = book.add_order(*o);
                for(auto& t : trades) {
                    tick_volume += t.quantity; book.last_traded_price = t.price; stats.add(o->side == Side::BUY, t.quantity);
                }
            }
        };

        for (auto& a : makers) process_agent(a.act(ref_price, time, oid), s_make);
        for (auto& a : fundamental) process_agent(a.act_with_market(true_value, ref_price, time, oid), s_fund);
        for (auto& a : noise) process_agent(a.act(ref_price, time, oid), s_noise);
        for (auto& a : momentum) process_agent(a.act(ref_price, time, oid), s_mom);
        
        // Throttled Broadcast (10 ticks ~ 200ms)
        if (++tick_count % 10 == 0) {
            book.decay(0.05, gen);
            engine.broadcastSentiment(s_fund.buy_vol, s_fund.sell_vol, s_mom.buy_vol, s_mom.sell_vol, s_make.buy_vol, s_make.sell_vol, s_noise.buy_vol, s_noise.sell_vol, s_user.buy_vol, s_user.sell_vol);
            engine.broadcastData(book.last_traded_price, tick_volume);
            s_fund.reset(); s_mom.reset(); s_make.reset(); s_noise.reset(); s_user.reset();
        }

        time += dt;
        std::this_thread::sleep_until(start_tick + std::chrono::milliseconds(20)); // Fast Sim Loop (50Hz)
    }
    return 0;
}