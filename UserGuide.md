# Agentic Market Simulator: User Guide

## Overview
Welcome to the Agentic Market Simulator! This tool allows you to interact with a living, breathing financial market simulation populated by AI agents. You can watch the market evolve in real-time, trade against the AI, and trigger specific market events like "Pump & Dump" schemes or "Short Squeezes" to see how the market reacts.

**No coding knowledge is required to use this simulator.**

---

## 1. The Dashboard Layout

### **Top Bar (Your Portfolio)**
* **Price:** The current trading price of the stock.
* **Shares:** The number of shares you currently own (positive) or owe (negative/short).
* **P/L (Profit/Loss):** Your total profit or loss based on your current trades.

### **Left Panel (Controls)**
This is your command center.
* **Config:** Select the simulation personality.
    * *Moderate/Volatile:* Standard market conditions.
    * *Very Volatile:* **Recommended.** Enables the special **Market Scenarios** (Pump & Dump, Short Squeeze).
* **Sliders:** Adjust the population of different AI agents (e.g., increase "Noise" traders for more chaos).
* **Market Scenarios:** (Visible only in "Very Volatile" mode) Buttons to activate special game modes.
* **Whale Tools:** A powerful tool to execute massive trades instantly and manipulate the price.

### **Center (The Chart)**
* **Blue Line:** The real-time price history of the asset.
* **Volume Bars:** The vertical bars at the bottom show trading activity.
    * *Green:* Buying pressure dominates.
    * *Red:* Selling pressure dominates.

### **Right Panel (Market Intel)**
* **Last Second Sentiment:** A live heatmap showing who is buying and selling right now.
    * *Fundamental:* Smart investors looking for value.
    * *Momentum:* Traders who chase trends.
    * *Noise:* Random retail traders or hype chasers.
    * *Makers:* Banks providing liquidity (the "house").
* **Net Flow:** The total shares bought minus shares sold in the last second. Green numbers mean buyers are winning; Red numbers mean sellers are winning.

---

## 2. Market Scenarios
*Note: These are only available when "Very Volatile" is selected.*

### **Scenario A: Pump & Dump**
* **The Story:** A coordinated group (Noise Traders) creates artificial "Hype," aggressively buying the stock to inflate the price. Smart investors (Fundamental Traders) provide weak resistance.
* **What to Watch:**
    * **Hype Level:** Starts high (90%). As long as this is high, the price will trend upward.
    * **Overvalued:** Shows how inflated the price is compared to its "fair value."
    * **The Crash:** If the price drops significantly (about 10-15% from the peak), the "Hype" breaks. Buyers panic and switch to selling, causing a massive crash.
* **Your Goal:** Ride the wave! Buy early when Hype is high, but sell quickly before the panic sets in.

### **Scenario B: Short Squeeze**
* **The Story:** "Short Sellers" (Fundamental Traders) are betting the stock will go down. If the price goes **up**, they lose money. If it goes up *too much*, they panic and are forced to buy back shares to limit losses, which drives the price even higher.
* **What to Watch:**
    * **Short Interest:** The total number of shares these traders are betting against the market.
    * **Panic Meter:** Starts at 0%. If this hits 100%, the Short Sellers have "blown up" and will trigger a buying frenzy.
* **Your Goal:** Force the squeeze! Buy shares to push the price up. Watch the Panic Meter rise. Once it hits 100%, the price will explode upwards.

---

## 3. How to Trade

### **Manual Trading**
Use the **BUY** and **SELL** buttons below the chart for standard orders.
* **Qty:** The number of shares to trade.
* **Price:** The price limit for your order (automatically updates to current price).

### **Whale Tools (Market Manipulation)**
Use the **Yellow Slider** in the bottom left to act as a "Whale" (a massive market mover).
* **Slide Right (+):** Prepares a massive BUY order (Pump).
* **Slide Left (-):** Prepares a massive SELL order (Dump).
* **Execute Button:** Fires the trade instantly. Use this to trigger Squeezes or Crashes.
* **Liquidate:** Instantly sells everything you own to cash out your position.

---

## 4. Understanding the Agents
Hover over the **?** icons in the dashboard for quick definitions.

* **Market Makers:** They provide order flow. They don't care about price direction; they just want to trade constantly.
* **Fundamental Traders:** The "Smart Money." They buy when the price is low and sell when it is high. In a **Short Squeeze**, they are the victims.
* **Momentum Traders:** The "Trend Followers." If the price is going up, they buy. If it's going down, they sell.
* **Noise Traders:** The "Retail Crowd." In a **Pump & Dump**, they are the ones aggressively buying the hype.

---

## Troubleshooting
* **"Connection Error":** If the chart stops updating, click "Stop Sim" and then "Start Sim" again to reset the engine.
* **Price Flatlining:** In scenarios like "Pump & Dump," the price might pause briefly as buyers chew through sell orders. This is normal market resistance—give it a second or use the Whale Tool to push it through!