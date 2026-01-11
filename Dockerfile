# Use a lightweight Python version on Linux
FROM python:3.9-slim

# 1. Install system tools: C++ compiler (g++), Make, and ZeroMQ libraries
RUN apt-get update && apt-get install -y \
    g++ \
    make \
    libzmq3-dev \
    && rm -rf /var/lib/apt/lists/*

# Set working directory inside the container
WORKDIR /app

# 2. Copy all your project files into the container
COPY . .

# 3. Compile the C++ Simulation Engines
# This runs the 'make all' command from your Makefile
RUN make all

# 4. Install Python libraries
RUN pip install --no-cache-dir -r requirements.txt

# 5. Expose the port the server runs on
EXPOSE 8000

# 6. Command to start the application
CMD ["python", "server.py"]