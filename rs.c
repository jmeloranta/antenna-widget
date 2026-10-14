#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/socket.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/l2cap.h>

// CONFIGURATION: Replace with your H5080's MAC Address
#define DEVICE_MAC "A4:C1:38:XX:XX:XX"

#define ATT_OP_WRITE_REQ    0x12
#define ATT_OP_READ_REQ     0x0A

// Standard ATT Handles for the H5080
#define HANDLE_WRITE        0x0015  // Handle to write commands
#define HANDLE_READ         0x001B  // Handle to read responses (corresponds to AUTH_READ_UUID)

// Connects to the BLE device via L2CAP socket
int connect_ble() {

  int sock;
  struct sockaddr_l2 addr = {0};
  
  sock = socket(AF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
  if (sock < 0) {
    perror("Error opening Bluetooth socket");
    return -1;
  }

  addr.l2_family = AF_BLUETOOTH;
  addr.l2_cid = htobs(4); // Fixed ATT Channel for BLE
  str2ba(DEVICE_MAC, &addr.l2_bdaddr);
  addr.l2_bdaddr_type = BDADDR_LE_PUBLIC; 
  
  printf("Connecting to H5080 (%s)... ", DEVICE_MAC);
  fflush(stdout);
  if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("Connection failed");
    close(sock);
    return -1;
  }
  printf("Connected!\n");
  return sock;
}

// Pair with the plug to get the authentication bytes
void do_pairing() {

  printf("\n[Pairing Mode Setup]\n");
  printf("1. Unplug the Govee H5080 from the wall and plug it back in.\n");
  printf("2. Hold down the side button until the light flashes rapidly.\n");
  printf("Press [ENTER] when ready...");
  getchar();
  
  int sock = connect_ble();
  if (sock < -1) return;
  
  // 1. Send the pairing initialization packet (3 bytes ATT header + 20 bytes Govee frame)
  uint8_t init_packet[23] = {
    ATT_OP_WRITE_REQ,
    HANDLE_WRITE & 0xFF, (HANDLE_WRITE >> 8) & 0xFF, // ATT Handle
    0xAA, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x8E  // Checksum 0x8E
  };
  
  printf("Sending pairing initialization handshake...\n");
  if (write(sock, init_packet, sizeof(init_packet)) < 0) {
    perror("Failed to send pairing initialization");
    close(sock);
    return;
  }
  
  sleep(1); // Give the plug a brief second to process and generate the key
  
  // 2. Send an ATT Read Request to pull the generated tokens
  uint8_t read_req[3] = {
    ATT_OP_READ_REQ,
    HANDLE_READ & 0xFF, (HANDLE_READ >> 8) & 0xFF
  };
  
  printf("Reading authentication tokens back from plug...\n");
  if (write(sock, read_req, sizeof(read_req)) < 0) {
    perror("Failed to request read handle");
    close(sock);
    return;
  }
  
  // 3. Read the incoming response buffer
  uint8_t rx_buffer[40];
  int bytes_read = read(sock, rx_buffer, sizeof(rx_buffer));
  close(sock);
  
  if (bytes_read > 7) {
    // The ATT Read Response header takes up index 0, followed by the Govee payload.
    // The auth tokens live inside the 3rd, 4th, and 5th byte positions of the payload.
    printf("\n========================================\n");
    printf(" SUCCESS! PAIRING KEY FOUND\n");
    printf("========================================\n");
    printf("Add these numbers to your execution command:\n\n");
    printf("./govee_h5080_auto on 0x%02X 0x%02X 0x%02X\n", rx_buffer[4], rx_buffer[5], rx_buffer[6]);
    printf("========================================\n");
  } else {
    printf("Error: Received an invalid or too short reply from the plug.\n");
  }
}

// Sends the switch power state using provided auth tokens
void send_command(int turn_on, uint8_t b1, uint8_t b2, uint8_t b3) {

  int sock = connect_ble();
  if (sock < 0) return;
  
  uint8_t packet[23] = {0};
  
  // ATT Header
  packet[0] = ATT_OP_WRITE_REQ;
  packet[1] = HANDLE_WRITE & 0xFF;
  packet[2] = (HANDLE_WRITE >> 8) & 0xFF;
  
  // Govee Payload Structure
  packet[3] = 0xAA;               // Header
  packet[4] = 0x01;               // Power command
  packet[5] = turn_on ? 0x01 : 0x00; // State
  packet[6] = b1;                 // Extracted token 1
  packet[7] = b2;                 // Extracted token 2
  packet[8] = b3;                 // Extracted token 3
  
  // Calculate XOR Checksum over payload bytes (indices 3 to 21)
  uint8_t checksum = 0;
  for (int i = 3; i < 22; i++) {
    checksum ^= packet[i];
  }
  packet[22] = checksum;
  
  printf("Sending payload command...\n");
  if (write(sock, packet, sizeof(packet)) < 0) {
    perror("Failed to toggle relay");
  } else {
    printf("Success: Plug commanded %s!\n", turn_on ? "ON" : "OFF");
  }
  close(sock);
}

int main(int argc, char *argv[]) {

  if (argc < 2) {
    fprintf(stderr, "Usage:\n  To Pair: %s pair\n  To Control: %s [on|off] [auth_byte1] [auth_byte2] [auth_byte3]\n", argv[0], argv[0]);
    return EXIT_FAILURE;
  }
  
  if (strcasecmp(argv[1], "pair") == 0) {
    do_pairing();
  } else if (strcasecmp(argv[1], "on") == 0 || strcasecmp(argv[1], "off") == 0) {
    if (argc < 5) {
      fprintf(stderr, "Error: Control commands require you to supply the 3 auth bytes.\nExample: %s on 0x1A 0x4F 0xFE\n", argv[0]);
      return EXIT_FAILURE;
    }
    int turn_on = (strcasecmp(argv[1], "on") == 0);
    uint8_t b1 = (uint8_t)strtol(argv[2], NULL, 16);
    uint8_t b2 = (uint8_t)strtol(argv[3], NULL, 16);
    uint8_t b3 = (uint8_t)strtol(argv[4], NULL, 16);
    
    send_command(turn_on, b1, b2, b3);
  } else {
    fprintf(stderr, "Unknown command: %s\n", argv[1]);
    return EXIT_FAILURE;
  }
  
  return EXIT_SUCCESS;
}
