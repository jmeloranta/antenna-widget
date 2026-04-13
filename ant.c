#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <gtk/gtk.h>

// Define the antenna names
#define ANTENNA1 "6m beam"
#define ANTENNA2 "WARC beam"
#define ANTENNA3 "20-15-10 stack"
#define ANTENNA4 "40m beam"
#define ANTENNA5 "No antenna"
#define ANTENNA6 "BOG NE"
#define ANTENNA7 "BOG SW"
#define ANTENNA8 "BOG NW"
#define ANTENNA9 "BOG SE"
#define ANTENNA10 "No antenna"
#define ANTENNA11 "No antenna"
#define ANTENNA12 "No antenna"
#define MAX_ANTENNAS 12

// Device for BM5
#define DEVICE "/dev/serial/by-id/usb-FTDI_FT232R_USB_UART_B003ISA1-if00-port0"

// Update every 100 msec
#define UPDATE 100

char *antennas[] = {ANTENNA1, ANTENNA2, ANTENNA3, ANTENNA4, ANTENNA5, ANTENNA6, 
                    ANTENNA7, ANTENNA8, ANTENNA9, ANTENNA10, ANTENNA11, ANTENNA12};

#define MAX_SM 11
char *stack_match[] = {"No antenna", "Top antenna", "Mid antenna", "Top+mid antennas",
                       "Bottom antenna", "Top+bot antennas", "Mid+bot antennas",
                       "All antennas", "AUX", "Unknown", "Phase"}; 

/*
 * RX antenna control:
 *
 * BOG NE = antenna 6 (no antenna; no relays selected)
 * BOG SW = antenna 7 (select relay 9)
 * BOG NW = antenna 8 (select relay 10)
 * BOG SE = antenna 9 (select relay 11)
 *
 * BOG selected for can be selected for 160, 80, 75, and 60. TX antenna is unaffected (radio -> acom -> inv L)
 *
 * Numbers below (6, 7, 8, 9) refer to antenna numbers defined in bandmaster V. Bands 40 - 6m are always
 * unchanged. Remember bandmaster uses hexadecimal everywhere.
 *
 */
char *bogs[] = {"[R29T09H060606060402030203020301]\r", "[R29T09H070707070402030203020301]\r",
                "[R29T09H080808080402030203020301]\r", "[R29T09H090909090402030203020301]\r"};

int fd, tx_on = 0;
GtkWidget *ant, *sm;

struct packet {
  unsigned char cmd;
  unsigned char dst;
  unsigned char src;
  unsigned char channel;
  int len;
  unsigned char data[256];
};

int read_untilcr(int fd, char *buf, int maxlen) {
  
  int pos = 0, bytes_rd;
  
  while(1) {
    if((bytes_rd = read(fd, buf + pos, 1)) < 0) return -1;
    if(bytes_rd == 0) {
      sleep(1); // could be less than 1sec
      continue;
    }
    if(!pos && (*buf == '\r' || *buf == '\n')) continue;
    if(pos == maxlen) return -1;
    if(buf[pos] == '\r') break;
    pos++;
  }
  buf[pos] = '\0';
  return pos;
}

int parse_packet(char *str, struct packet *packet) {
  
  memset((void *) packet, 0, sizeof(struct packet));
  if(sscanf(str, "[T%2xR%2x%2xH%2xM%[^]]", &(packet->cmd), &(packet->dst), &(packet->src), &(packet->channel), &(packet->data)) != 5) return -1;
  return 0;
}

void set_tty() {
  
  struct termios param;
  
  if(tcgetattr(fd, &param) < 0) {
    fprintf(stderr, "Status get failed.\n");
    exit(1);
  }
  
  param.c_cflag &= ~PARENB;
  param.c_cflag &= ~CSTOPB;
  param.c_cflag |= CS8;
  param.c_cflag &= ~CRTSCTS;
  param.c_cflag |= CREAD | CLOCAL;
  param.c_lflag &= ~ICANON;
  param.c_lflag &= ~ECHO;
  param.c_lflag &= ~ECHOE;
  param.c_lflag &= ~ECHONL;
  param.c_lflag &= ~ISIG;
  param.c_iflag &= ~(IXON | IXOFF | IXANY);
  param.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL);  
  param.c_oflag &= ~OPOST;
  param.c_oflag &= ~ONLCR;
  cfsetispeed(&param, B57600);
  cfsetospeed(&param, B57600);
  
  if(tcsetattr(fd, TCSANOW, &param) < 0) {
    fprintf(stderr, "Status set failed.\n");
    exit(1);
  }
}

static gboolean update_band_info(void *asd) {
  
  int len, antenna, relay;
  struct packet packet;
  char buf[512];
  
  if((len = read_untilcr(fd, buf, sizeof(buf))) < 0) {
    fprintf(stderr, "Read error.\n");
    exit(1);
  }
  
  if(parse_packet(buf, &packet) < 0) {
    printf("Incomplete packet -- skipping.\n");
    return 1;
  }
  
  switch(packet.cmd) {
  case 0xc4:  // C4 = Bandmaster 5 status
    if(sscanf(packet.data, "%2x01EEEEEEEEEEEE%4x", &antenna, &relay) != 2) {
      printf("Error extracting antenna & relay information.");
      antenna = 99;
    }
    if(antenna > 0 && antenna < MAX_ANTENNAS)
      sprintf(buf, "<span color='blue'>%s (relays = %012b)</span>", antennas[antenna-1], relay);
    else
      sprintf(buf, "<span color='red'>Unknown</span>");
    gtk_label_set_markup(GTK_LABEL(ant), buf);
    break;
  case 0x45: // Stack match
    sscanf(packet.data, "%2x", &antenna);
    if(antenna >= 128) {
      antenna -= 128; // transmitting
      tx_on = 1;
    } else tx_on = 0;
    if(antenna >= 0 && antenna < MAX_SM)
      sprintf(buf, "<span color='blue'>%s (%s)</span>", stack_match[antenna], tx_on?"TX":"RX");
    else
      strcpy(buf, "<span color='red'>Unknown state</span>");
    gtk_label_set_markup(GTK_LABEL(sm), buf);
    break;
  case 0x13: // decoder data
    printf("Decoder data ignored.\n");
    break;
  default:
    printf("Unknown packet (%x) - ignored.\n", packet.cmd);
  }
  return 1;
}

void closewin(GtkWidget *win, gpointer data) {

  gtk_main_quit();
}

void bog_ne(GtkWidget *win, gpointer data) {

  if(!tx_on) {
    printf("BOG NE selected\n");
    write(fd, bogs[0], strlen(bogs[0]));
  }
}

void bog_sw(GtkWidget *win, gpointer data) {

  if(!tx_on) {
    printf("BOG SW selected\n");
    write(fd, bogs[1], strlen(bogs[1]));
  }
}

void bog_nw(GtkWidget *win, gpointer data) {

  if(!tx_on) {
    printf("BOG NW selected\n");
    write(fd, bogs[2], strlen(bogs[2]));
  }
}

void bog_se(GtkWidget *win, gpointer data) {

  if(!tx_on) {
    printf("BOG SE selected\n");
    write(fd, bogs[3], strlen(bogs[3]));
  }
}

int main(int argc, char *argv[]) {

  GtkWidget *window;
  GtkWidget *ant_label, *sm_label, *hbox1, *hbox2, *hbox3, *vbox, *button1, *button2, *button3, *button4;

  gtk_init(&argc, &argv);
  window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(window), "Antenna Status");
  gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
  gtk_window_set_default_size(GTK_WINDOW(window), 350, 80);
  g_signal_connect(G_OBJECT(window), "destroy", G_CALLBACK(closewin), NULL);

  ant_label = gtk_label_new("Antenna:");
  gtk_label_set_xalign(GTK_LABEL(ant_label), 0.0); // 0 = left, 1 = right
  sm_label =  gtk_label_new("Stack match:");
  gtk_label_set_xalign(GTK_LABEL(sm_label), 0.0);
  ant = gtk_label_new("-----");
  gtk_label_set_xalign(GTK_LABEL(ant), 0.5);
  sm = gtk_label_new("-----");
  gtk_label_set_xalign(GTK_LABEL(sm), 0.5);

  hbox1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
  hbox2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
  hbox3 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
  
  vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);

  gtk_box_pack_start(GTK_BOX(hbox1), ant_label, TRUE, TRUE, 5);
  gtk_box_pack_start(GTK_BOX(hbox1), ant, TRUE, TRUE, 5);

  gtk_box_pack_start(GTK_BOX(hbox2), sm_label, TRUE, TRUE, 5);
  gtk_box_pack_start(GTK_BOX(hbox2), sm, TRUE, TRUE, 5);

  gtk_box_pack_start(GTK_BOX(vbox), hbox1, TRUE, TRUE, 5);
  gtk_box_pack_start(GTK_BOX(vbox), hbox2, TRUE, TRUE, 5);

  button1 = gtk_button_new_with_label("BOG NE");
  button2 = gtk_button_new_with_label("BOG SW");
  button3 = gtk_button_new_with_label("BOG NW");
  button4 = gtk_button_new_with_label("BOG SE");
  gtk_box_pack_start(GTK_BOX(hbox3), button1, TRUE, TRUE, 5);
  gtk_box_pack_start(GTK_BOX(hbox3), button2, TRUE, TRUE, 5);
  gtk_box_pack_start(GTK_BOX(hbox3), button3, TRUE, TRUE, 5);
  gtk_box_pack_start(GTK_BOX(hbox3), button4, TRUE, TRUE, 5);

  gtk_box_pack_start(GTK_BOX(vbox), hbox3, TRUE, TRUE, 5);

  gtk_container_add(GTK_CONTAINER(window), vbox);
  g_signal_connect(button1, "clicked", G_CALLBACK(bog_ne), NULL);
  g_signal_connect(button2, "clicked", G_CALLBACK(bog_sw), NULL);
  g_signal_connect(button3, "clicked", G_CALLBACK(bog_nw), NULL);
  g_signal_connect(button4, "clicked", G_CALLBACK(bog_se), NULL);

  if((fd = open(DEVICE, O_RDWR)) < 0) {
    fprintf(stderr, "Can't open device.\n");
    exit(1);
  }
  set_tty();

  bog_ne(NULL, NULL); // BOG NE is the default on start up

  g_timeout_add(UPDATE, update_band_info, NULL);

  gtk_widget_show_all(window);
  gtk_main();

  close(fd);
  return 0;
}
