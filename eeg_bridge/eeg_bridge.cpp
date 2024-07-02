#include "eeg_bridge.h"

// AMPLIFIER *100
// NANO TO MICRO /1000
const uint8_t DC_MODE_SCALE = 100;
const uint16_t NANO_TO_MICRO_CONVERSION = 1000;
const double DOUBLESCALINGFACTOR = 10000.0;

void EegBridge::bind_socket() {

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        std::cerr << "Socket creation failed" << '\n';
        exit(EXIT_FAILURE);
    }

    memset(&servaddr, 0, sizeof(servaddr));
    memset(&cliaddr, 0, sizeof(cliaddr));
    
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(PORT);
    
    if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        std::cerr << "Bind failed" << '\n';
        close(sockfd); // Ensure the socket is closed on failure
        exit(EXIT_FAILURE);
    }

    struct timeval timeout;
    timeout.tv_sec = socket_timeout; // Timeout after 10 second
    timeout.tv_usec = 0;

    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0) {
        std::cerr << "Failed to set socket timeout." << std::endl;
    }

  
    // Set socket buffer size to 10 MB
    int buffer_size = 1024 * 1024 * 10;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &buffer_size, sizeof(buffer_size)) == -1) {
        std::cerr << "Failed to set socket receive buffer size." << std::endl;
    }

    len = sizeof(cliaddr);
}

void EegBridge::spin(dataHandler &handler, volatile std::sig_atomic_t &signal_received) {
    running = true;
    std::cout << "Waiting for measurement start..." << '\n';
    eeg_bridge_status = WAITING_MEASUREMENT_START;
    while (!signal_received) {
        int n = recvfrom(sockfd, (char*)buffer, BUFFER_LENGTH, MSG_WAITALL, (struct sockaddr*)&cliaddr, &len);
        if (n < 0) {
            if (signal_received) break; // Check if the signal caused recvfrom to fail
            std::cerr << "Receive failed" << '\n';
            break; // Optionally break on other errors too
        }

        unsigned char firstByte = buffer[0];

        // Handle packets
        switch (firstByte)
        {
        case 0x01: { // MeasurementStartPacket
            
            std::cout << "MeasurementStart package received!\n";
            measurement_start_packet packet_info;
            std::vector<uint16_t> SourceChannels;
            std::vector<uint8_t> ChannelTypes;
            
            deserializeMeasurementStartPacket_pointer(buffer, n, packet_info, SourceChannels, ChannelTypes);

            // Divide channels into data and trigger sources
            std::vector<uint16_t> data_channel_sources;
            std::vector<uint16_t> trigger_channel_sources;
            for (size_t i = 0; i < SourceChannels.size(); i++) {
                uint16_t source = SourceChannels[i];
                if(source < 65535 ) { 
                    data_channel_sources.push_back(source);
                } else {
                    trigger_channel_sources.push_back(source); 
                }
            }

            numChannels = packet_info.NumChannels;
            numDataChannels = numChannels - trigger_channel_sources.size();              // Excluding trigger channels
            sampling_rate = packet_info.SamplingRateHz;
            lastSequenceNumber = -1;

            handler.setSourceChannels(data_channel_sources);
            handler.setTriggerSource(trigger_channel_sources[0]);

            data_handler_samples = Eigen::MatrixXd::Zero(numChannels, 100);

            std::cout << "MeasurementStart package processed!\n";

            handler.reset_handler(numDataChannels, sampling_rate);
            std::cout << "DataHandler reset!\n";
            eeg_bridge_status = MEASUREMENT_IN_PROGRESS;
            std::cout << "Waiting for packets..." << '\n';

            break;

        } case 0x02: { // SamplesPacket                                                                                         TODO:: Handle triggers properly
            
            if (eeg_bridge_status == WAITING_MEASUREMENT_START || !handler.isReady()) break;

            // Deserialize the received data into a sample_packet instance
            sample_packet packet_info;
            deserializeSamplePacketEigen_pointer(buffer, n, packet_info, data_handler_samples);
            int sequenceNumber = packet_info.PacketSeqNo;

            Eigen::MatrixXd data_samples = ((data_handler_samples.topRows(numDataChannels) * DC_MODE_SCALE) / NANO_TO_MICRO_CONVERSION);
            Eigen::VectorXd triggers = data_handler_samples.row(numDataChannels);
            
            for (int i = 0; i < packet_info.NumSampleBundles; i++) {
                handler.addData(data_samples.col(i), static_cast<double>(packet_info.FirstSampleTime), static_cast<int>(triggers(i)), sequenceNumber);
            }

            // Check for dropped packets
            if (lastSequenceNumber != -1 && sequenceNumber != (lastSequenceNumber + 1)) {
                std::cerr << "Packet loss detected. Expected sequence: " << (lastSequenceNumber + 1) << ", but received: " << sequenceNumber << '\n';
                // TODO: Handle packet loss
            }

            lastSequenceNumber = sequenceNumber; // Update the latest sequence number

            // std::cout << "Package " << sequenceNumber << " received!" << '\n';

            // std::cout << "Channels: " << data_samples.rows() << ", " << data_samples.cols() << '\n';

            // std::cout << handler.getDataInOrder(1) << '\n';
            // std::cout << handler.get_buffer_capacity() << '\n';
            break;

        } case 0x03: { // TriggerPacket
            /* code */
            break;

        } case 0x04: { // MeasurementEndPacket
            /* code */
            break;
        
        } case 0x05: { // HardwareStatePacket
            /* code */
            break;
        
        
        } default:
            break;
        }
    }

    running = false;
    std::cout << "Shutting down..." << '\n';
    close(sockfd);
}
