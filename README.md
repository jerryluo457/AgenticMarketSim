# Agentic Market Simulator

## Overview
The Agentic Market Simulator is a high-performance, agent-based modeling system designed to simulate financial market microstructure and limit order book dynamics in real-time. The project visualizes the interactions between various autonomous trading agents and a human user, demonstrating phenomena such as liquidity crises, mean reversion, and momentum-driven volatility.

The system is architected as a hybrid application: a high-speed C++ engine handles the order book matching and agent logic, while a Python/FastAPI server manages the WebSocket communication to a web-based frontend dashboard.

## Key Features

### Core Simulation Engine (C++)
* **Limit Order Book:** Implements a standard double-auction order book matching engine.
* **Autonomous Agents:**
    * **Market Makers:** Provide liquidity by maintaining bid and ask quotes around the mid-price.
    * **Fundamental Traders:** Trade based on a mean-reverting "true value" process, correcting price deviations but potentially causing liquidity shocks during extreme volatility.
    * **Momentum Traders:** Follow price trends, buying when prices rise and selling when they fall, often amplifying volatility.
    * **Noise Traders:** Execute random trades to simulate organic market turnover.
* **Volatility Modes:** Four distinct simulation presets (Moderate, Volatile, Very Volatile, Most Volatile) that adjust agent aggressiveness, market noise, and event frequency.

### Interactive Dashboard
* **Real-Time Visualization:** Utilizes Lightweight Charts to render tick-by-tick price action and volume data.
* **Live Sentiment Analysis:** Displays the net buying or selling volume for each agent class in real-time, allowing users to analyze market flow.
* **Whale Tools:** A market manipulation interface allowing the user to execute massive buy or sell orders (up to 100% of realized volume) to test market resilience and liquidity depth.
* **User Trading:** Allows the user to intervene in the market by placing their own limit and market orders.

## Architecture
1.  **Simulation Engine (C++17):** Compiles into standalone binaries for different volatility profiles. Uses ZeroMQ (ZMQ) for low-latency Inter-Process Communication (IPC).
2.  **Server (Python 3):** Built with FastAPI and Socket.IO. It acts as a bridge, receiving broadcast data from the C++ engine via ZMQ and pushing updates to the web client.
3.  **Frontend (HTML/JS):** A responsive interface using Tailwind CSS for styling and Socket.IO client for real-time data streaming.

## Flaws and Imperfections
* **It is impossible to accurately model the market** I have modified, added, and removed many agents for the market simulation to "look natural". However, the real securities market has way more than 4 general types of agents, and each type of agents behave differently. The purpose of this simulation is to simulate user input's effects on general market behavior.
* **Moderate and Volatile currently Beta:** Currently the chart sometimes demonstrate unexpected and unnatural behavior due to the complexity of agent design. 
* **Momentum-centered trading bipolarization** Currently the momentum traders either all buy or all sell, which is due to the uniform design of such agents for now. A more stochastic behavior of the momentum traders is currently being worked on.


## Installation and Usage

### Prerequisites
* C++ Compiler (g++ supporting C++17)
* Python 3.8+
* ZeroMQ libraries (libzmq)

### Compilation
Compile the simulation engines using the provided Makefile:
```bash
make all