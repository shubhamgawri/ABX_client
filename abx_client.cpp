#include <iostream>
#include <fstream>
#include <vector>
#include <set>
#include <string>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <thread>
#define _WIN32_WINNT 0x0601 
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "json.hpp"
using json = nlohmann::json;

#pragma comment(lib, "Ws2_32.lib")

struct Packet {
    char symbol[5]; 
    char buySellindicator; 
    int32_t quantity;
    int32_t price; 
    int32_t packetSequence; 

    // Convert to JSON object
    json to_json() const {
        json j;
        j["symbol"] = std::string(symbol);
        j["buysellindicator"] = std::string(1, buySellindicator);
        j["quantity"] = ntohl(quantity);
        j["price"] = ntohl(price);
        j["packetSequence"] = ntohl(packetSequence);
        return j;
    }
};

class ABXClient {
private:
    const std::string serverAddress;
    const int serverPort;
    SOCKET clientSocket;
    std::vector<Packet> receivedPackets;
    std::set<int> missingSequences;
    int maxSequence = 0;
    bool wsaInitialized = false;
    const int CONNECTION_RETRY_DELAY_MS = 500;
    const int MAX_RETRIES = 3; 

public:
    ABXClient(const std::string& address, int port)
        : serverAddress(address), serverPort(port), clientSocket(INVALID_SOCKET) {
        
        // Initialize Winsock
        WSADATA wsaData;
        int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (result != 0) {
            std::cerr << "WSAStartup failed: " << result << std::endl;
            return;
        }
        wsaInitialized = true;
    }

    ~ABXClient() {
        disconnectIfConnected();
        
        if (wsaInitialized) {
            WSACleanup();
        }
    }
    
    // Cleanly disconnect if connected
    void disconnectIfConnected() {
        if (clientSocket != INVALID_SOCKET) {
            closesocket(clientSocket);
            clientSocket = INVALID_SOCKET;
        }
    }

    // Connect to the ABX server with retry logic
    bool connect(int retryCount = 0) {
        // Make sure any existing connection is closed
        disconnectIfConnected();
        
        struct addrinfo *result = NULL, hints;

        ZeroMemory(&hints, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        // Resolve the server address and port
        std::string portStr = std::to_string(serverPort);
        int addrResult = getaddrinfo(serverAddress.c_str(), portStr.c_str(), &hints, &result);
        if (addrResult != 0) {
            std::cerr << "getaddrinfo failed: " << addrResult << std::endl;
            return false;
        }

        // Create socket
        clientSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
        if (clientSocket == INVALID_SOCKET) {
            std::cerr << "Error creating socket: " << WSAGetLastError() << std::endl;
            freeaddrinfo(result);
            return false;
        }

        // Connect to server
        int connectResult = ::connect(clientSocket, result->ai_addr, (int)result->ai_addrlen);
        freeaddrinfo(result); 
        if (connectResult == SOCKET_ERROR) {
            std::cerr << "Error connecting to server: " << WSAGetLastError() << std::endl;
            disconnectIfConnected();
            
            // Retry logic
            if (retryCount < MAX_RETRIES) {
                std::cout << "Connection failed. Retrying in " << CONNECTION_RETRY_DELAY_MS << "ms... (" 
                          << retryCount + 1 << "/" << MAX_RETRIES << ")" << std::endl;
                Sleep(CONNECTION_RETRY_DELAY_MS); // Windows-specific sleep function
                return connect(retryCount + 1);
            }
            
            return false;
        }

        std::cout << "Connected to ABX server at " << serverAddress << ":" << serverPort << std::endl;
        return true;
    }

    // Send request to stream all packets
    bool requestAllPackets() {
        std::cout << "Requesting all packets..." << std::endl;
        
        uint8_t request[2];
        request[0] = 1; // Call type 1: Stream All Packets
        request[1] = 0; // Not used for this call type
        
        if (send(clientSocket, (const char*)request, sizeof(request), 0) != sizeof(request)) {
            std::cerr << "Error sending request: " << WSAGetLastError() << std::endl;
            return false;
        }
        
        // Receive and process all packets
        processIncomingPackets();
        return true;
    }

    // Request a specific packet by sequence number
    bool requestPacketBySequence(uint8_t sequenceNumber) {
        std::cout << "Requesting packet with sequence: " << (int)sequenceNumber << std::endl;
        
        uint8_t request[2];
        request[0] = 2; // Call type 2: Resend Packet
        request[1] = sequenceNumber; // Sequence number to resend
        
        if (send(clientSocket, (const char*)request, sizeof(request), 0) != sizeof(request)) {
            std::cerr << "Error sending resend request: " << WSAGetLastError() << std::endl;
            return false;
        }
        
        // Receive and process the requested packet
        processIncomingPackets(true); // true = single packet mode
        
        return true;
    }

    // Process incoming packets from the server
    void processIncomingPackets(bool singlePacketMode = false) {
        const int packetSize = 17; // 4 + 1 + 4 + 4 + 4 bytes
        char buffer[packetSize];
        int packetsReceived = 0;
        
        struct timeval tv;
        tv.tv_sec = 3; 
        tv.tv_usec = 0;
        
        if (setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv)) < 0) {
            std::cerr << "Warning: Failed to set socket timeout" << std::endl;
        }
        
        while (true) {
            int bytesRead = recv(clientSocket, buffer, packetSize, 0);
            
            if (bytesRead <= 0) {
                if (bytesRead < 0 && WSAGetLastError() != WSAETIMEDOUT) {
                    std::cerr << "Error receiving data: " << WSAGetLastError() << std::endl;
                }
                
                if (singlePacketMode && packetsReceived > 0) {
                    std::cout << "Received " << packetsReceived << " packet(s) as requested" << std::endl;
                }
                break;
            }
            
            packetsReceived++;
            
            if (bytesRead != packetSize) {
                std::cerr << "Incomplete packet received: " << bytesRead << " bytes instead of " << packetSize << std::endl;
                continue;
            }
            
            // Parse the received packet
            Packet packet;
            memcpy(packet.symbol, buffer, 4);
            packet.symbol[4] = '\0'; // Ensure null termination
            packet.buySellindicator = buffer[4];
            memcpy(&packet.quantity, buffer + 5, 4);
            memcpy(&packet.price, buffer + 9, 4);
            memcpy(&packet.packetSequence, buffer + 13, 4);
            
            // Convert from network byte order (already in big endian)
            int sequence = ntohl(packet.packetSequence);
            
            std::cout << "Received packet: Symbol=" << packet.symbol 
                      << ", Type=" << packet.buySellindicator 
                      << ", Quantity=" << ntohl(packet.quantity) 
                      << ", Price=" << ntohl(packet.price) 
                      << ", Sequence=" << sequence << std::endl;
            
            // Add packet to our collection
            receivedPackets.push_back(packet);
            
            // Update max sequence number
            maxSequence = std::max(maxSequence, sequence);
            
            // In single packet mode, break after receiving one packet
            if (singlePacketMode) {
                break;
            }
        }
    }

    // Find all missing sequences
    void identifyMissingSequences() {
        std::set<int> receivedSequences;
        
        // Extract all received sequence numbers
        for (const auto& packet : receivedPackets) {
            receivedSequences.insert(ntohl(packet.packetSequence));
        }
        
        // Find missing sequences
        for (int i = 1; i <= maxSequence; ++i) {
            if (receivedSequences.find(i) == receivedSequences.end()) {
                missingSequences.insert(i);
            }
        }
        
        std::cout << "Identified " << missingSequences.size() << " missing sequences" << std::endl;
        
        // Print missing sequences for debugging
        if (!missingSequences.empty()) {
            std::cout << "Missing sequences: ";
            for (int seq : missingSequences) {
                std::cout << seq << " ";
            }
            std::cout << std::endl;
        }
    }

    // Request all missing packets with proper connection handling
    void requestMissingPackets() {
        if (missingSequences.empty()) {
            std::cout << "No missing packets to request" << std::endl;
            return;
        }
        
        std::cout << "Requesting " << missingSequences.size() << " missing packets..." << std::endl;
        
        std::set<int> sequencesToRequest = missingSequences;
        
        for (const auto& seq : sequencesToRequest) {
            // Make sure we have a valid connection for each request
            if (clientSocket == INVALID_SOCKET && !connect()) {
                std::cerr << "Failed to connect for packet sequence " << seq << std::endl;
                continue;
            }
            
            if (!requestPacketBySequence(seq)) {
                std::cerr << "Failed to request packet sequence " << seq << std::endl;
                
                // Reset connection and try again
                disconnectIfConnected();
                
                Sleep(CONNECTION_RETRY_DELAY_MS); 
                
                if (connect()) {
                    if (!requestPacketBySequence(seq)) {
                        std::cerr << "Failed to request packet sequence " << seq << " on retry" << std::endl;
                    }
                }
            }
            
            Sleep(200);
        }
        
        // Disconnect after all requests
        disconnectIfConnected();
    }

    // Save all packets to JSON file
    bool saveToJson(const std::string& filename) {
        // Sort packets by sequence number
        std::sort(receivedPackets.begin(), receivedPackets.end(), 
            [](const Packet& a, const Packet& b) {
                return ntohl(a.packetSequence) < ntohl(b.packetSequence);
            });
        
        // Create JSON array
        json packetsArray = json::array();
        for (const auto& packet : receivedPackets) {
            packetsArray.push_back(packet.to_json());
        }
        
        // Write JSON to file
        try {
            std::ofstream file(filename);
            if (!file.is_open()) {
                std::cerr << "Failed to open output file: " << filename << std::endl;
                return false;
            }
            
            file << packetsArray.dump(4); // Pretty print with 4-space indentation
            file.close();
            
            std::cout << "Successfully saved " << receivedPackets.size() << " packets to " << filename << std::endl;
            
            std::cout << "All sequences were successfully collected!" << std::endl;
            
            
            return true;
        } catch (const std::exception& e) {
            std::cerr << "Error saving JSON: " << e.what() << std::endl;
            return false;
        }
    }

    bool run(const std::string& outputFile) {
        if (!connect()) {
            return false;
        }
        
        if (!requestAllPackets()) {
            return false;
        }
        
        // Disconnect after initial packet stream
        disconnectIfConnected();
        
        // Identify missing sequences
        identifyMissingSequences();
        
        // Request missing packets with fresh connections
        requestMissingPackets();
        
        return saveToJson(outputFile);
    }
};

int main(int argc, char* argv[]) {
    std::string serverAddress = "localhost";
    int serverPort = 3000;
    std::string outputFile = "abx_orderbook.json";
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--host" && i + 1 < argc) {
            serverAddress = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            serverPort = std::stoi(argv[++i]);
        } else if (arg == "--output" && i + 1 < argc) {
            outputFile = argv[++i];
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  --host HOST     Server hostname or IP (default: localhost)" << std::endl;
            std::cout << "  --port PORT     Server port (default: 3000)" << std::endl;
            std::cout << "  --output FILE   Output JSON file (default: abx_orderbook.json)" << std::endl;
            std::cout << "  --help          Show this help message" << std::endl;
            return 0;
        }
    }
    
    ABXClient client(serverAddress, serverPort);
    if (!client.run(outputFile)) {
        std::cerr << "Failed to complete the ABX client workflow" << std::endl;
        return 1;
    }
    
    std::cout << "\nPress Enter to exit..." << std::endl;
    std::cin.get();
    
    return 0;
}