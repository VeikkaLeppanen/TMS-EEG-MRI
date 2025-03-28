#include <iostream>
#include <vector>
#include <cstring>
#include <cstdint>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <chrono>
#include <thread>
#include <random>
#include <fstream>
#include <sstream>
#include <string>

/*
This can be used to send packets to the port used by the main program to simulate the retrieval of bittium packets.
Current available packages:
    MeasurementStartPackage
    SamplesPackage

TODO:
    MeasurementEndPackage
*/

// Simulating parameters
const uint16_t CHANNEL_COUNT = 13;
const uint32_t SAMPLINGRATE = 5000;

const uint8_t DC_MODE_SCALE = 100;
const uint16_t NANO_TO_MICRO_CONVERSION = 1000;
const double DOUBLESCALINGFACTOR = 10000.0;

// Target port
#define PORT 50000

int32_t getRandomNumber(int32_t min, int32_t max) {
    std::random_device rd; // Obtain a random number from hardware
    std::mt19937 gen(rd()); // Seed the generator
    std::uniform_int_distribution<> distr(min, max); // Define the range

    return distr(gen); // Generate and return the random number
}


std::vector<uint8_t> serializeMeasurementStartPacketData(
    uint8_t FrameType,
    uint8_t MainUnitNum,
    uint8_t Reserved[2],
    uint32_t SamplingRateHz,
    uint32_t SampleFormat,
    uint32_t TriggerDefs,
    uint16_t NumChannels,
    const std::vector<uint16_t>& SourceChannels,
    const std::vector<uint8_t>& ChannelTypes) {
    
    std::vector<uint8_t> buffer;
    
    // Assuming the buffer should have a basic capacity. Adjust as needed.
    buffer.reserve(1472); // Reserve an arbitrary size for simplicity

    // Serialize fixed-size fields in big-endian order
    buffer.push_back(FrameType);
    buffer.push_back(MainUnitNum);
    buffer.insert(buffer.end(), Reserved, Reserved + 2); // Assuming Reserved is an array
    
    // Serialize multi-byte integers in big-endian order
    buffer.push_back((SamplingRateHz >> 24) & 0xFF);
    buffer.push_back((SamplingRateHz >> 16) & 0xFF);
    buffer.push_back((SamplingRateHz >> 8) & 0xFF);
    buffer.push_back(SamplingRateHz & 0xFF);

    buffer.push_back((SampleFormat >> 24) & 0xFF);
    buffer.push_back((SampleFormat >> 16) & 0xFF);
    buffer.push_back((SampleFormat >> 8) & 0xFF);
    buffer.push_back(SampleFormat & 0xFF);

    buffer.push_back((TriggerDefs >> 24) & 0xFF);
    buffer.push_back((TriggerDefs >> 16) & 0xFF);
    buffer.push_back((TriggerDefs >> 8) & 0xFF);
    buffer.push_back(TriggerDefs & 0xFF);

    buffer.push_back((NumChannels >> 8) & 0xFF);
    buffer.push_back(NumChannels & 0xFF);

    // Serialize SourceChannels
    for (auto channel : SourceChannels) {
        buffer.push_back((channel >> 8) & 0xFF);
        buffer.push_back(channel & 0xFF);
    }

    // Serialize ChannelTypes
    for (auto type : ChannelTypes) {
        buffer.push_back(type);
    }

    return buffer;
}

std::vector<uint8_t> serializeSamplePacketData(
    uint8_t FrameType, uint8_t MainUnitNum, uint8_t Reserved[2], uint32_t PacketSeqNo, 
    uint16_t NumChannels, uint16_t NumSampleBundles, uint64_t FirstSampleIndex, uint64_t FirstSampleTime, 
    std::vector<std::vector<int32_t>> &int24Data) {
    
    std::vector<uint8_t> buffer;
    buffer.reserve(1472); // Adjust based on int24Data size

    // Serialize fixed-size fields in big-endian order
    buffer.push_back(FrameType);
    buffer.push_back(MainUnitNum);
    buffer.insert(buffer.end(), Reserved, Reserved + 2);
    
    // Serialize multi-byte integers in big-endian order
    buffer.push_back((PacketSeqNo >> 24) & 0xFF);
    buffer.push_back((PacketSeqNo >> 16) & 0xFF);
    buffer.push_back((PacketSeqNo >> 8) & 0xFF);
    buffer.push_back(PacketSeqNo & 0xFF);

    buffer.push_back((NumChannels >> 8) & 0xFF);
    buffer.push_back(NumChannels & 0xFF);

    buffer.push_back((NumSampleBundles >> 8) & 0xFF);
    buffer.push_back(NumSampleBundles & 0xFF);

    for(int i = 7; i >= 0; --i) {
        buffer.push_back((FirstSampleIndex >> (i * 8)) & 0xFF);
    }

    for(int i = 7; i >= 0; --i) {
        buffer.push_back((FirstSampleTime >> (i * 8)) & 0xFF);
    }

    // Serialize int24[N][C] in big-endian order
    for (auto &row : int24Data) {
        for (auto &val : row) {
            // Assuming val represents a signed 24-bit integer correctly, including handling negative values
            buffer.push_back((val >> 16) & 0xFF); // MSB
            buffer.push_back((val >> 8) & 0xFF);
            buffer.push_back(val & 0xFF); // LSB
        }
    }

    return buffer;
}

std::vector<uint8_t> generateExampleMeasurementStartPacket() {
    uint8_t FrameType = 1; // Assuming 1 indicates a Measurement Start packet
    uint8_t MainUnitNum = 2;
    uint8_t Reserved[2] = {0, 0}; // Fill with appropriate values if needed
    uint32_t SamplingRateHz = SAMPLINGRATE; // Example: 5000 Hz
    uint32_t SampleFormat = 1; // Example format
    uint32_t TriggerDefs = 0; // Example trigger definitions
    uint16_t NumChannels = CHANNEL_COUNT; // Number of channels

    // Example source channels and types
    std::vector<uint16_t> SourceChannels(CHANNEL_COUNT, 0); // Example channel IDs

    for(size_t i = 0; i < CHANNEL_COUNT - 1; i++) {
        SourceChannels[i] = i + 1;
    }
    SourceChannels[CHANNEL_COUNT - 1] = 65535;

    std::vector<uint8_t> ChannelTypes(CHANNEL_COUNT, 0); // Example channel types (0 and 1 for demonstration)

    return serializeMeasurementStartPacketData(
        FrameType, MainUnitNum, Reserved, SamplingRateHz,
        SampleFormat, TriggerDefs, NumChannels, SourceChannels, ChannelTypes);
}

// Example usage
std::vector<uint8_t> generateExampleSamplePacket_random() {
    uint8_t FrameType = 2, MainUnitNum = 2, Reserved[2] = {3, 4};
    uint32_t PacketSeqNo = 123456;
    uint16_t NumChannels = CHANNEL_COUNT, NumSampleBundles = 2;
    uint64_t FirstSampleIndex = 123456789012345, FirstSampleTime = 98765432109876;

    std::vector<std::vector<int32_t>> Samples(NumSampleBundles, std::vector<int32_t>(NumChannels));

    for (auto &vec : Samples) {
        for (auto &val : vec) {
            val = getRandomNumber(-10, 10);
        }
    }

    // for (auto &vec : Samples) {
    //     for (auto &val : vec) {
    //         std::cout << val << ' ';
    //     }
    //     std::cout << '\n';
    // }

    return serializeSamplePacketData(FrameType, MainUnitNum, Reserved, PacketSeqNo, NumChannels, NumSampleBundles, FirstSampleIndex, FirstSampleTime, Samples);
}

// Example usage
std::vector<uint8_t> generateExampleSamplePacket_csv(std::vector<int32_t> sample_vector, uint32_t SeqNo, uint64_t SampleTime) {
    uint8_t FrameType = 2, MainUnitNum = 2, Reserved[2] = {3, 4};
    uint32_t PacketSeqNo = SeqNo;
    uint16_t NumChannels = CHANNEL_COUNT, NumSampleBundles = 1;
    uint64_t FirstSampleIndex = 123456789012345, FirstSampleTime = SampleTime;

    std::vector<std::vector<int32_t>> Samples(NumSampleBundles, std::vector<int32_t>(NumChannels));

    for (auto &vec : Samples) {
        vec = sample_vector;
    }

    if (SeqNo % 10000 == 0) {
	Samples.front().back() |= 0x08;
	//std::cout << "Trigger_A set" << '\n';
    }
    if (sample_vector.back() == 1) {
	Samples.front().back() |= 0x02;
	//std::cout << "Trigger_B set " << sample_vector.back() << '\n';
    }
    
    return serializeSamplePacketData(FrameType, MainUnitNum, Reserved, PacketSeqNo, NumChannels, NumSampleBundles, FirstSampleIndex, FirstSampleTime, Samples);
}

void sendUDP(const std::vector<uint8_t> &data, const std::string &address, int port) {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        std::cerr << "Error opening socket" << std::endl;
        return;
    }

    sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);
    servaddr.sin_addr.s_addr = inet_addr(address.c_str());

    sendto(sockfd, data.data(), data.size(), 0, (const sockaddr *)&servaddr, sizeof(servaddr));
    close(sockfd);
}

int main() {
    std::vector<uint8_t> MSdata = generateExampleMeasurementStartPacket();
    // std::string IP_address = "127.0.0.1"; // Localhost
    // std::string IP_address = "192.168.0.100";
    std::string IP_address = "192.168.0.105";

    sendUDP(MSdata, IP_address, PORT);
    std::cout << "MeasurementStartPackage sent!" << '\n';

    std::this_thread::sleep_for(std::chrono::seconds(1));

    // std::ifstream csvFile("/home/user/EEG/data/testdata_veikka_raw.csv");
    std::ifstream csvFile("/home/veikka/Work/EEG/DataStream/mat_file_conversion/testdata_interleaved_sept.csv");
    // std::ifstream csvFile("/home/user/EEG/data/testdata_interleaved_sept.csv");
    // std::ifstream csvFile("/home/user/EEG/data/testdata_continuous_sept.csv");
    // std::ifstream csvFile("/home/user/EEG/data/5_eeg_7_cwl_reference.csv");
    // std::ifstream csvFile("/home/user/EEG/data/testdata_sine.csv");
    // std::ifstream csvFile("/home/veikka/Work/EEG/DataStream/mat_file_conversion/testdata_veikka_raw.csv");
    std::string line;
    uint32_t sequenceNumber = 0;
    auto sleepDurationMicroseconds = static_cast<long long>(1000000) / SAMPLINGRATE;

    auto startTimePoint = std::chrono::system_clock::now();
    auto lastTimePoint = std::chrono::steady_clock::now();

    int number_of_sample_packets_to_send = 40000000;//14999;
    while (std::getline(csvFile, line) && sequenceNumber < number_of_sample_packets_to_send) {
        std::stringstream lineStream(line);
        std::string cell;
        std::vector<int32_t> sampleVector;

        while (std::getline(lineStream, cell, ',')) {
            sampleVector.push_back(std::stod(cell));
        }
        
        auto timeStamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now() - startTimePoint);
        uint64_t SampleTime = static_cast<uint64_t>(timeStamp.count());
        std::vector<uint8_t> samplePacket = generateExampleSamplePacket_csv(sampleVector, sequenceNumber, SampleTime);

        sendUDP(samplePacket, IP_address, PORT);

        std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - lastTimePoint;
        std::cout << "Package " << sequenceNumber << " sent! Time since last packet: " << elapsed.count() << " seconds.\n";

        // Throttle sending to maintain sampling rate
        std::this_thread::sleep_for(std::chrono::microseconds(sleepDurationMicroseconds));
        lastTimePoint = std::chrono::steady_clock::now();

        sequenceNumber++;
    }

    return 0;
}
