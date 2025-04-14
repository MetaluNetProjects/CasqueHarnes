// GD3300 driver
#include "fraise.h"
#include "hardware/uart.h"
#include "stdint.h"
#include "gd3300-cmd.h"

class GD3300 {
using byte = uint8_t;
private:
    int tx_pin;
    int rx_pin;
    uart_inst_t *uart;
    char rx_buf[16];
    int rx_len = 0;
    int nb_tracks = 0;
    bool playing = false;
    void process_rx() {
        fraise_put_init();
        fraise_put_uint8(10);
        for(int i = 0; i < rx_len; i++) fraise_put_uint8(rx_buf[i]);
        fraise_put_send();
        if(rx_buf[0] == 126 && rx_buf[1] == 255 && rx_buf[2] == 6) {
            switch(rx_buf[3]) {
                case 58: // sdcard inserted
                    printf("l sd_card inserted\n");
                    sleep_ms(800);
                    qTTracks();
                    break;
                case 61: // end of track
                    playing = false;
                    break;
                case 66: // status
                    playing = (rx_buf[6] == 1);
                    break;
                case 72: // nb tracks
                    nb_tracks = rx_buf[6];
                    printf("l nb_tracks %d\n", nb_tracks);
                    break;
            }
        }
    }
public:
    void setup(int tx, int rx, uart_inst_t *u) {
        tx_pin = tx;
        rx_pin = rx;
        uart = u;
        uart_init(uart, 9600);
        gpio_set_function(tx_pin, GPIO_FUNC_UART);
        gpio_set_function(rx_pin, GPIO_FUNC_UART);
        rx_len = 0;
        sleep_ms(1000);
        qTTracks();
    }

    void update() {
        while(uart_is_readable(uart)) {
            char b = uart_getc(uart);
            if(b == 0x7E) rx_len = 0; // if there are "0x7E" it's a beginning.
            if(rx_len >= (int)sizeof(rx_buf)) continue; // buffer full!
            rx_buf[rx_len++] = b;
            if(b == 0xEF && rx_len == 10) process_rx();
        }
    }

    int get_nb_tracks() {
        return nb_tracks;
    }

    bool is_playing() {
        return playing;
    }

    void sendCommand(byte command, byte dat1, byte dat2, bool query = false) {
        byte tx_buf[8] = {0}; // Buffer for Send commands.
        // Command Structure 0x7E 0xFF 0x06 CMD FBACK DAT1 DAT2 0xEF
        tx_buf[0] = 0x7E;           // Start byte
        tx_buf[1] = 0xFF;           // Version
        tx_buf[2] = 0x06;           // Command length not including Start and End byte.
        tx_buf[3] = command;        // Command
        tx_buf[4] = 0x01 * query;   // Feedback 0x00 NO, 0x01 YES
        tx_buf[5] = dat1;           // DATA1 datah
        tx_buf[6] = dat2;           // DATA2 datal
        tx_buf[7] = 0xEF;           // End byte
        for(int i = 0; i < 8; i++) uart_putc_raw(uart, tx_buf[i]) ;
    }

    void sendCommand(byte command) {
        sendCommand(command, 0, 0, false);
    }

    void sendCommand(byte command, byte dat2) {
        sendCommand(command, 0, dat2, false);
    }

    void sendQuery(byte command) {
        sendCommand(command, 0, 0, true);
    }

    void playNext(){
        sendCommand(CMD_NEXT);
    }

    void playPrevious(){
        sendCommand(CMD_PREV);
    }

    void volUp(){
        sendCommand(CMD_VOL_UP);
    }

    void volDown(){
        sendCommand(CMD_VOL_DOWN);
    }

    void setVol(byte v){
        // Set volume (0-30)
        sendCommand(CMD_SET_VOL, v);
    }

    void playSL(byte n){
        // Play single loop n file
        sendCommand(CMD_PLAY_SLOOP, n);
    }

    void playSL(byte f, byte n){
        // Single loop play n file from f folder
        sendCommand(CMD_PLAY_SLOOP, f, n);
    }
    void playL(bool on){
        // Single loop play n file from f folder
        if(on) sendCommand(CMD_SET_SPLAY, 0, 0);
        else sendCommand(CMD_SET_SPLAY, 0, 1);
    }

    void play(){
        sendCommand(CMD_PLAY);
        playing = true;
    }

    void pause(){
       sendCommand(CMD_PAUSE);
    }

    void play(byte n){
       // n number of the file that must be played.
       // n possible values (1-255)
       sendCommand(CMD_PLAYN, n);
       playing = true;
    }

    void play(byte n, byte vol){
       // n number of the file that must be played
       sendCommand(CMD_PLAY_W_VOL, vol, n);
       playing = true;
    }

    void playF(byte f){
       // Play all files in the f folder
       sendCommand(CMD_FOLDER_CYCLE, f, 0);
    }
    void playF(byte f,byte n){
       // Play all files in the f folder
       sendCommand(CMD_PLAY_F_FILE, f, n);
    }

    void shuffle() {
        sendCommand(CMD_PLAY_SHUFFLE);
    }

    void stop(){
       sendCommand(CMD_STOP_PLAY);
       playing = false;
    }

    void qPlaying(){
      // Ask for the file is playing
       sendQuery(CMD_PLAYING_N);
    }

    void qStatus(){
       // Ask for the status.
       sendQuery(CMD_QUERY_STATUS);
    }

    void qVol(){
      // Ask for the volumen
       sendQuery(CMD_QUERY_VOLUME);
    }

    void qFTracks(){    // !!! Nonsense answer
      // Ask for the number of tracks folders
       sendQuery(CMD_QUERY_FLDR_TRACKS);
    }

    void qTTracks(){
      // Ask for the total of tracks
       sendQuery(CMD_QUERY_TOT_TRACKS);
    }

    void qTFolders(){
      // Ask for the number of folders
       sendQuery(CMD_QUERY_FLDR_COUNT);
    }

    void sleep(){
      // Send sleep command
      sendCommand(CMD_SLEEP_MODE);
    }

    void wakeup(){
      // Send wake up command
      sendCommand(CMD_WAKE_UP);
    }

    void reset(){
      // Send reset command
      sendCommand(CMD_RESET);
    }
    
    void receivebytes(const char *data, uint8_t len) {
        byte command = fraise_get_uint8();
        switch(command) {
            case 0: play(fraise_get_uint8()); break;
            case 1: stop(); break;
            case 2: setVol(fraise_get_uint8()); break;
            case 3: qTTracks(); break;
            case 4: qTFolders(); break;
            case 5: shuffle(); break;
            case 6: playSL(fraise_get_uint8() != 0); break;
            case 7: qPlaying(); break;
            case 8: qStatus(); break;
            case 100: printf("l nb_tracks %d\n", nb_tracks); break;
        }
    }
};
